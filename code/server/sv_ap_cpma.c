#include "server.h"
#include "sv_ap_cpma.h"
#include "../qcommon/ap_client.h"
#include "../qcommon/ap_cpma_logic.h"
#include "../qcommon/surfaceflags.h"
#include "../ap/q3ap_catalog.h"

#define SVAP_CPMA_MAX_PICKUPS Q3AP_LOCATION_MAP_STRIDE

// CPMA 1.53 vm/qagame.qvm, SHA-256 30D41330AB94A510B95E568B14DD8A5D182C6C6BC97C638626AE140AB585FA46.
#define SVAP_CPMA153_QVM_CRC32 0xA2408C39u
#define SVAP_CPMA153_INSTRUCTIONS 277423
#define SVAP_CPMA153_DATA_LENGTH 4432088u
#define SVAP_CPMA153_GENTITY_SIZE 0x338
#define SVAP_CPMA153_INUSE_OFFSET 0x208u
#define SVAP_CPMA153_NEXTTHINK_OFFSET 0x2acu
#define SVAP_CPMA153_THINK_OFFSET 0x2b0u
#define SVAP_CPMA153_ITEM_OFFSET 0x328u
/* Pickup_Health at 0x2ce74 writes entity + 0x2d8, then client ps.stats[HEALTH]. */
#define SVAP_CPMA153_HEALTH_OFFSET 0x2d8u
#define SVAP_CPMA153_ITEM_GITYPE_OFFSET 0x34u
#define SVAP_CPMA153_RESPAWN_ITEM 0x2cf59
#define SVAP_CPMA153_DEDICATED_INTEGER 0x34ea8u
/* Arena warmup state used by CPMA's pickup scoring at 0x2cf09 (zero during play). */
#define SVAP_CPMA153_ARENA_OFFSET 0x32cu
#define SVAP_CPMA153_ARENA_SIZE 0xa40u
#define SVAP_CPMA153_ARENA_WARMUP 0x104c08u

typedef struct {
	const q3ap_catalog_pickup_t *catalog;
	vec3_t bspOrigin;
	vec3_t origin;
	int entityNum;
	int normalContents;
	int normalEFlags;
	int modelIndex;
	int lastPickupEventTime;
	int respawnAt;
	int respawnStartedAt;
	int privateEntityNum;
	int visibleAt;
	int normalSvFlags;
	int normalSingleClient;
	qboolean parsed;
	qboolean locked;
	qboolean hiddenFromPlayer;
	qboolean sent;
	qboolean present;
} svapCpmaPickup_t;

typedef struct {
	const q3ap_catalog_traversal_t *catalog;
	int entityNum;
	vec3_t origin;
} svapCpmaTraversal_t;

static struct {
	qboolean enabled;
	const q3ap_catalog_map_t *map;
	svapCpmaPickup_t pickups[SVAP_CPMA_MAX_PICKUPS];
	svapCpmaTraversal_t traversals[SVAP_CPMA_MAX_PICKUPS];
	int pickupCount;
	int parsedCount;
	int mappedCount;
	int traversalCount;
	int pendingTraversal;
	int entityOrdinal;
	qboolean inEntity;
	qboolean expectValue;
	char key[MAX_TOKEN_CHARS];
	char classname[MAX_QPATH];
	vec3_t parsedOrigin;
	qboolean hasOrigin;
	int lastSpawnCount;
	int lastScore;
	qboolean scoreInitialized;
	uint32_t familyMask;
	int killCount;
	vec3_t frameStartOrigin;
	qboolean haveFrameStartOrigin;
	int eventClientNum;
	int lastEventSequence;
	int lastExternalEventTime;
	int markerDebugCount;
	int mapStartTime;
	qboolean exactTimerDebugged;
	qboolean exactTimerMissingDebugged;
	qboolean estimatedTimerExpiredDebugged;
} svap;

static qboolean SVAP_CPMA_IsEnabled( void ) {
	char fsGame[MAX_QPATH];
	Cvar_VariableStringBuffer( "fs_game", fsGame, sizeof( fsGame ) );
	return !Q_stricmp( fsGame, "cpma-ap" );
}

static qboolean SVAP_CPMA_DebugPickups( void ) {
	return Cvar_VariableIntegerValue( "ap_debug_pickups" ) != 0;
}

static qboolean SVAP_CPMA_IsShard( const svapCpmaPickup_t *pickup ) {
	return pickup && !strcmp( pickup->catalog->classname, "item_armor_shard" );
}

static qboolean SVAP_CPMA_HasInitialPowerupDelay( const svapCpmaPickup_t *pickup ) {
	const char *classname = pickup->catalog->classname;
	return !strcmp( classname, "item_quad" ) || !strcmp( classname, "item_enviro" ) ||
		!strcmp( classname, "item_haste" ) || !strcmp( classname, "item_invis" ) ||
		!strcmp( classname, "item_regen" ) || !strcmp( classname, "item_flight" );
}

static void SVAP_CPMA_DebugShard( const char *phase, const svapCpmaPickup_t *pickup,
	const playerState_t *player, const sharedEntity_t *ent ) {
	if ( !SVAP_CPMA_DebugPickups() || !SVAP_CPMA_IsShard( pickup ) ) return;
	Com_Printf( "APDBG shard %s ord=%03i ent=%i model=%i/%i time=%i "
		"active=%i checked=%i locked=%i sent=%i player=%i "
		"p=(%.1f %.1f %.1f) i=(%.1f %.1f %.1f) touch=%i sweep=%i flags=%x contents=%x\n",
		phase, pickup->catalog->ordinal, ent ? SV_NumForGentity( (sharedEntity_t *)ent ) : pickup->entityNum,
		ent ? ent->s.modelindex : -1, pickup->modelIndex, sv.time,
		APCL_GameQuery( Q3AP_GAME_LOCATION_CLASSIFICATION, pickup->catalog->location_id ) !=
			Q3AP_CLASSIFICATION_UNKNOWN,
		APCL_GameQuery( Q3AP_GAME_LOCATION_CHECKED, pickup->catalog->location_id ),
		pickup->locked, pickup->sent, player ? player->clientNum : -1,
		player ? player->origin[0] : 0, player ? player->origin[1] : 0, player ? player->origin[2] : 0,
		pickup->origin[0], pickup->origin[1], pickup->origin[2],
		player ? Q3AP_CPMA_PlayerTouches( player->origin, pickup->origin ) : 0,
		player && svap.haveFrameStartOrigin ? Q3AP_CPMA_PlayerSweptTouches(
			svap.frameStartOrigin, player->origin, pickup->origin ) : 0,
		ent ? ent->s.eFlags : 0, ent ? ent->r.contents : 0 );
}

static int SVAP_CPMA_FamilyForModelIndex( int modelIndex ) {
	static const signed char families[] = {
		-1, 8, 8, 8, 9, 9, 9, 10, -1, 0, 1, 2, 3, 4, 5, 6, 7, -1,
		0, 1, 2, 6, 4, 3, 5, 7, 11, 12, 13, 14, 15, 16, 17, 18
	};
	return modelIndex >= 0 && modelIndex < (int)( sizeof( families ) / sizeof( families[0] ) ) ?
		families[modelIndex] : -1;
}

static qboolean SVAP_CPMA153_HasLayout( void ) {
	return gvm && !gvm->entryPoint && gvm->crc32sum == SVAP_CPMA153_QVM_CRC32 &&
		gvm->instructionCount == SVAP_CPMA153_INSTRUCTIONS &&
		gvm->exactDataLength == SVAP_CPMA153_DATA_LENGTH && sv.gentities &&
		sv.gentitySize == SVAP_CPMA153_GENTITY_SIZE;
}

static qboolean SVAP_CPMA153_PrivateInt( const sharedEntity_t *ent, unsigned offset, int *value ) {
	if ( !ent || !value || sv.gentitySize <= 0 || offset > (unsigned)sv.gentitySize ||
		sizeof( *value ) > (unsigned)sv.gentitySize - offset ) return qfalse;
	memcpy( value, (const byte *)ent + offset, sizeof( *value ) );
	return qtrue;
}

static qboolean SVAP_CPMA153_VMInt( uint32_t address, int *value ) {
	if ( !gvm || !gvm->dataBase || !value || address > gvm->exactDataLength ||
		sizeof( *value ) > gvm->exactDataLength - address ) return qfalse;
	memcpy( value, gvm->dataBase + address, sizeof( *value ) );
	return qtrue;
}

qboolean SVAP_CPMA_ApplyStageLimits( int fragLimit ) {
	int savedDedicated, consoleAccess = 1;
	qboolean accepted;
	if ( sv.state != SS_GAME || !SVAP_CPMA_IsEnabled() || fragLimit < 1 ||
		!SVAP_CPMA153_HasLayout() ||
		!SVAP_CPMA153_VMInt( SVAP_CPMA153_DEDICATED_INTEGER, &savedDedicated ) ) return qfalse;
	// CPMA 1.53 ConsoleCommand gates admin votes on its cached dedicated integer.
	memcpy( gvm->dataBase + SVAP_CPMA153_DEDICATED_INTEGER, &consoleAccess, sizeof( consoleAccess ) );
	Cmd_TokenizeString( va( "callvote limit %i 0", fragLimit ) );
	accepted = SV_GameCommand();
	Cmd_TokenizeString( "callvote timelimit 0 0" );
	accepted = SV_GameCommand() && accepted;
	Cmd_TokenizeString( "callvote warmup 0 0" );
	accepted = SV_GameCommand() && accepted;
	memcpy( gvm->dataBase + SVAP_CPMA153_DEDICATED_INTEGER, &savedDedicated, sizeof( savedDedicated ) );
	Com_Printf( "Archipelago: CPMA stage limits %s (frags %i, time 0, warmup 0, %s server)\n",
		accepted ? "applied" : "rejected", fragLimit, savedDedicated ? "dedicated" : "listen" );
	return accepted;
}

static qboolean SVAP_CPMA153_ItemMatches( const sharedEntity_t *ent, const char *classname ) {
	const char *storedClassname;
	const void *end;
	size_t available;
	int itemAddress, classnameAddress, itemType;
	if ( !classname || !SVAP_CPMA153_PrivateInt( ent, SVAP_CPMA153_ITEM_OFFSET, &itemAddress ) ||
		itemAddress <= 0 || !SVAP_CPMA153_VMInt( (uint32_t)itemAddress, &classnameAddress ) ||
		classnameAddress <= 0 ||
		!SVAP_CPMA153_VMInt( (uint32_t)itemAddress + SVAP_CPMA153_ITEM_GITYPE_OFFSET, &itemType ) ||
		itemType <= IT_BAD || itemType >= IT_TEAM ||
		(uint32_t)classnameAddress >= gvm->exactDataLength ) return qfalse;
	storedClassname = (const char *)gvm->dataBase + classnameAddress;
	available = gvm->exactDataLength - (uint32_t)classnameAddress;
	if ( available > MAX_QPATH ) available = MAX_QPATH;
	end = memchr( storedClassname, '\0', available );
	return end && !strcmp( storedClassname, classname );
}

static int SVAP_CPMA153_RespawnAt( const svapCpmaPickup_t *pickup, int *privateEntityNum ) {
	float bestDistance = -1;
	float bestEntityDistance = -1;
	int bestRespawnAt = 0;
	qboolean ambiguous = qfalse;
	int entityNum;
	if ( !pickup || !SVAP_CPMA153_HasLayout() ) return 0;
	for ( entityNum = MAX_CLIENTS; entityNum < sv.num_entities; ++entityNum ) {
		sharedEntity_t *ent = SV_GentityNum( entityNum );
		float distance;
		int inuse = -1, nextthink = -1, think = -1, respawnAt;
		qboolean itemMatches;
		distance = Q3AP_CPMA_SpawnDistance( pickup->bspOrigin, ent->r.currentOrigin );
		if ( ent->s.number != entityNum || ent->s.eType != ET_ITEM || ent->r.contents ||
			!( ent->s.eFlags & EF_NODRAW ) || distance < 0 ||
			SVAP_CPMA_FamilyForModelIndex( ent->s.modelindex ) != pickup->catalog->family_index ) continue;
		SVAP_CPMA153_PrivateInt( ent, SVAP_CPMA153_INUSE_OFFSET, &inuse );
		SVAP_CPMA153_PrivateInt( ent, SVAP_CPMA153_NEXTTHINK_OFFSET, &nextthink );
		SVAP_CPMA153_PrivateInt( ent, SVAP_CPMA153_THINK_OFFSET, &think );
		itemMatches = SVAP_CPMA153_ItemMatches( ent, pickup->catalog->classname );
		if ( inuse != qtrue || !itemMatches ) continue;
		if ( privateEntityNum && ( bestEntityDistance < 0 || distance < bestEntityDistance ) ) {
			bestEntityDistance = distance;
			*privateEntityNum = entityNum;
		}
		respawnAt = Q3AP_CPMA_RespawnAt( sv.time, nextthink, think, SVAP_CPMA153_RESPAWN_ITEM );
		if ( !respawnAt ) continue;
		if ( bestDistance < 0 || distance < bestDistance ) {
			bestDistance = distance;
			bestRespawnAt = respawnAt;
			ambiguous = qfalse;
		} else if ( distance == bestDistance ) {
			ambiguous = qtrue;
		}
	}
	return ambiguous ? 0 : bestRespawnAt;
}

static void SVAP_CPMA153_DebugLocation( const svapCpmaPickup_t *pickup, const char *phase ) {
	int entityNum;
	for ( entityNum = MAX_CLIENTS; entityNum < sv.num_entities; ++entityNum ) {
		sharedEntity_t *ent = SV_GentityNum( entityNum );
		int inuse = -1, nextthink = -1, think = -1, item = -1;
		float distance = Q3AP_CPMA_SpawnDistance( pickup->bspOrigin, ent->r.currentOrigin );
		if ( ent->s.eType != ET_ITEM || distance < 0 ) continue;
		SVAP_CPMA153_PrivateInt( ent, SVAP_CPMA153_INUSE_OFFSET, &inuse );
		SVAP_CPMA153_PrivateInt( ent, SVAP_CPMA153_NEXTTHINK_OFFSET, &nextthink );
		SVAP_CPMA153_PrivateInt( ent, SVAP_CPMA153_THINK_OFFSET, &think );
		SVAP_CPMA153_PrivateInt( ent, SVAP_CPMA153_ITEM_OFFSET, &item );
		Com_Printf( "APDBG CPMA153 %s ord=%03i expected=%s ent=%i model=%i family=%i/%i "
			"linked=%i contents=%08x eflags=%08x svflags=%08x inuse=%i next=%i think=%x item=%x match=%i\n",
			phase, pickup->catalog->ordinal, pickup->catalog->classname, entityNum,
			ent->s.modelindex, SVAP_CPMA_FamilyForModelIndex( ent->s.modelindex ),
			pickup->catalog->family_index, ent->r.linked, ent->r.contents,
			ent->s.eFlags, ent->r.svFlags, inuse, nextthink, think, item,
			SVAP_CPMA153_ItemMatches( ent, pickup->catalog->classname ) );
	}
}

static uint32_t SVAP_CPMA_FamilyMask( void ) {
	uint32_t mask = 0;
	int family;
	for ( family = 0; family < Q3AP_FAMILY_COUNT; ++family )
		if ( APCL_GameQuery( Q3AP_GAME_ITEM_COUNT, q3ap_catalog_families[family].item_id ) > 0 )
			mask |= 1u << family;
	return mask;
}

static qboolean SVAP_CPMA_LocationActive( const svapCpmaPickup_t *pickup ) {
	return APCL_GameQuery( Q3AP_GAME_LOCATION_CLASSIFICATION, pickup->catalog->location_id ) !=
		Q3AP_CLASSIFICATION_UNKNOWN;
}

static void SVAP_CPMA_StartRespawn( svapCpmaPickup_t *pickup ) {
	if ( pickup && pickup->respawnAt <= sv.time ) {
		pickup->respawnStartedAt = sv.time;
		pickup->respawnAt = sv.time + 1000 * Q3AP_CPMA_RespawnSeconds( pickup->catalog->classname,
			pickup->catalog->family_index, Cvar_VariableIntegerValue( "g_weaponrespawn" ) );
	}
}

static qboolean SVAP_CPMA_PickupUnlocked( const svapCpmaPickup_t *pickup ) {
	return ( svap.familyMask & ( 1u << pickup->catalog->family_index ) ) != 0;
}

static void SVAP_CPMA_SetPlayerVisibility( svapCpmaPickup_t *pickup,
	sharedEntity_t *ent, const playerState_t *player ) {
	if ( !pickup || !ent || !player ) return;
	if ( pickup->locked ) {
		if ( !pickup->hiddenFromPlayer ) {
			pickup->normalSvFlags = ent->r.svFlags;
			pickup->normalSingleClient = ent->r.singleClient;
			pickup->hiddenFromPlayer = qtrue;
		}
		ent->r.svFlags |= SVF_NOTSINGLECLIENT;
		ent->r.singleClient = player->clientNum;
	} else if ( pickup->hiddenFromPlayer ) {
		ent->r.svFlags = pickup->normalSvFlags;
		ent->r.singleClient = pickup->normalSingleClient;
		pickup->hiddenFromPlayer = qfalse;
	}
}

static void SVAP_CPMA_UpdatePlayerVisibility( const playerState_t *player ) {
	int index;
	for ( index = 0; player && index < svap.pickupCount; ++index ) {
		svapCpmaPickup_t *pickup = &svap.pickups[index];
		if ( pickup->entityNum >= 0 && ( pickup->locked || pickup->hiddenFromPlayer ) )
			SVAP_CPMA_SetPlayerVisibility( pickup, SV_GentityNum( pickup->entityNum ), player );
	}
}

static qboolean SVAP_CPMA_CommitPickup( svapCpmaPickup_t *pickup ) {
	int index;
	pickup->sent = APCL_SendLocation( pickup->catalog->location_id );
	if ( pickup->sent )
		for ( index = 0; index < svap.pickupCount; ++index )
			if ( svap.pickups[index].catalog->location_id == pickup->catalog->location_id )
				svap.pickups[index].sent = qtrue;
	if ( pickup->sent && SVAP_CPMA_DebugPickups() )
		Com_Printf( "Archipelago: CPMA sent pickup %03i - %s\n",
			pickup->catalog->ordinal, pickup->catalog->display_name );
	else if ( SVAP_CPMA_DebugPickups() )
		Com_Printf( "APDBG send rejected ord=%03i location=%i active=%i checked=%i\n",
			pickup->catalog->ordinal, pickup->catalog->location_id,
			SVAP_CPMA_LocationActive( pickup ),
			APCL_GameQuery( Q3AP_GAME_LOCATION_CHECKED, pickup->catalog->location_id ) );
	return pickup->sent;
}

static qboolean SVAP_CPMA_SendPickup( svapCpmaPickup_t *pickup, playerState_t *player ) {
	if ( !pickup || !player || pickup->locked || pickup->sent || pickup->visibleAt >= sv.time ||
		 !SVAP_CPMA_LocationActive( pickup ) ||
		 ( !Q3AP_CPMA_PlayerTouches( player->origin, pickup->origin ) &&
		 ( !svap.haveFrameStartOrigin || !Q3AP_CPMA_PlayerSweptTouches(
		 svap.frameStartOrigin, player->origin, pickup->origin ) ) ) ) return qfalse;
	return SVAP_CPMA_CommitPickup( pickup );
}

static playerState_t *SVAP_CPMA_HumanPlayer( void ) {
	int client;
	if ( !sv.gameClients ) return NULL;
	for ( client = 0; client < sv.maxclients; ++client ) {
		playerState_t *ps;
		if ( svs.clients[client].state < CS_ACTIVE ||
			 svs.clients[client].netchan.remoteAddress.type == NA_BOT ) continue;
		ps = SV_GameClientNum( client );
		if ( ps->persistant[PERS_TEAM] != TEAM_SPECTATOR ) return ps;
	}
	return NULL;
}

static svapCpmaPickup_t *SVAP_CPMA_MappedPickup( int entityNum ) {
	int index;
	for ( index = 0; index < svap.pickupCount; ++index )
		if ( svap.pickups[index].entityNum == entityNum ) return &svap.pickups[index];
	return NULL;
}

static qboolean SVAP_CPMA_PickupPresent( const svapCpmaPickup_t *pickup ) {
	sharedEntity_t *ent;
	if ( !pickup || pickup->entityNum < 0 ) return qfalse;
	ent = SV_GentityNum( pickup->entityNum );
	return ent->r.linked && ent->r.contents && !( ent->s.eFlags & EF_NODRAW ) &&
		!( ent->r.svFlags & SVF_NOCLIENT );
}

static svapCpmaPickup_t *SVAP_CPMA_PickupForEntity( sharedEntity_t *ent ) {
	float bestDistance = -1;
	svapCpmaPickup_t *best;
	int index, entityNum = SV_NumForGentity( ent );
	int family = SVAP_CPMA_FamilyForModelIndex( ent->s.modelindex );
	best = SVAP_CPMA_MappedPickup( entityNum );
	if ( best ) return best;
	best = NULL;
	for ( index = 0; index < svap.pickupCount; ++index ) {
		svapCpmaPickup_t *pickup = &svap.pickups[index];
		float distance;
		if ( pickup->entityNum >= 0 || !pickup->parsed ||
			 ( family >= 0 && family != pickup->catalog->family_index ) ) continue;
		distance = Q3AP_CPMA_SpawnDistance( pickup->bspOrigin, ent->r.currentOrigin );
		if ( distance >= 0 && ( bestDistance < 0 || distance < bestDistance ) ) {
			bestDistance = distance;
			best = pickup;
		}
	}
	return best;
}

static void SVAP_CPMA_ParseEntity( void ) {
	int index;
	for ( index = 0; index < svap.traversalCount; ++index ) {
		const q3ap_catalog_traversal_t *traversal = svap.traversals[index].catalog;
		const char *expected = traversal->kind ? "trigger_teleport" : "trigger_push";
		if ( traversal->bsp_entity_ordinal == svap.entityOrdinal && !strcmp( expected, svap.classname ) ) {
			svap.pendingTraversal = index;
			break;
		}
	}
	if ( !svap.hasOrigin || !svap.classname[0] ) return;
	for ( index = 0; index < svap.pickupCount; ++index ) {
		svapCpmaPickup_t *pickup = &svap.pickups[index];
		if ( pickup->catalog->bsp_entity_ordinal == svap.entityOrdinal &&
			 !strcmp( pickup->catalog->classname, svap.classname ) ) {
			VectorCopy( svap.parsedOrigin, pickup->bspOrigin );
			pickup->parsed = qtrue;
			++svap.parsedCount;
			return;
		}
	}
}

void SVAP_CPMA_BeginMap( void ) {
	int index, mapIndex;
	memset( &svap, 0, sizeof( svap ) );
	if ( !SVAP_CPMA_IsEnabled() ) return;
	mapIndex = APCL_GameQuery( Q3AP_GAME_ACTIVE_MAP_INDEX, 0 );
	if ( mapIndex < 0 || mapIndex >= Q3AP_MAP_COUNT ) return;
	svap.enabled = qtrue;
	svap.map = &q3ap_catalog_maps[mapIndex];
	svap.mapStartTime = sv.time;
	svap.pickupCount = svap.map->pickup_count;
	svap.traversalCount = svap.map->traversal_count;
	svap.pendingTraversal = -1;
	svap.lastSpawnCount = -1;
	svap.eventClientNum = -1;
	svap.markerDebugCount = -1;
	for ( index = 0; index < svap.pickupCount; ++index ) {
		svap.pickups[index].catalog = &svap.map->pickups[index];
		svap.pickups[index].entityNum = -1;
		svap.pickups[index].privateEntityNum = -1;
		svap.pickups[index].lastPickupEventTime = -1000;
	}
	for ( index = 0; index < svap.traversalCount; ++index ) {
		svap.traversals[index].catalog = &svap.map->traversals[index];
		svap.traversals[index].entityNum = -1;
	}
	for ( index = 1; index <= svap.map->frag_limit; ++index )
		if ( APCL_GameQuery( Q3AP_GAME_LOCATION_CHECKED,
			Q3AP_LOCATION_BASE + mapIndex * Q3AP_LOCATION_MAP_STRIDE + Q3AP_KILL_LOCATION_OFFSET + index ) )
			++svap.killCount;
	Com_Printf( "Archipelago: CPMA gameplay adapter active for %s\n", svap.map->key );
}

void SVAP_CPMA_EndMap( void ) {
	memset( &svap, 0, sizeof( svap ) );
}

void SVAP_CPMA_EntityToken( const char *token ) {
	if ( !svap.enabled || !token ) return;
	if ( !token[0] ) {
		Com_Printf( "Archipelago: CPMA parsed %i/%i catalogued pickups\n",
			svap.parsedCount, svap.pickupCount );
		return;
	}
	if ( !strcmp( token, "{" ) ) {
		svap.pendingTraversal = -1;
		svap.inEntity = qtrue;
		svap.expectValue = qfalse;
		++svap.entityOrdinal;
		svap.classname[0] = '\0';
		svap.hasOrigin = qfalse;
		return;
	}
	if ( !strcmp( token, "}" ) ) {
		if ( svap.inEntity ) SVAP_CPMA_ParseEntity();
		svap.inEntity = qfalse;
		return;
	}
	if ( !svap.inEntity ) return;
	if ( !svap.expectValue ) {
		Q_strncpyz( svap.key, token, sizeof( svap.key ) );
		svap.expectValue = qtrue;
		return;
	}
	if ( !strcmp( svap.key, "classname" ) )
		Q_strncpyz( svap.classname, token, sizeof( svap.classname ) );
	else if ( !strcmp( svap.key, "origin" ) &&
		sscanf( token, "%f %f %f", &svap.parsedOrigin[0], &svap.parsedOrigin[1], &svap.parsedOrigin[2] ) == 3 )
		svap.hasOrigin = qtrue;
	svap.expectValue = qfalse;
}

qboolean SVAP_CPMA_LinkEntity( void *entity ) {
	sharedEntity_t *ent = entity;
	svapCpmaPickup_t *pickup;
	if ( !svap.enabled || !ent ) return qtrue;
	if ( svap.pendingTraversal >= 0 &&
		 ( ent->s.eType == ET_PUSH_TRIGGER || ent->s.eType == ET_TELEPORT_TRIGGER ) ) {
		svapCpmaTraversal_t *traversal = &svap.traversals[svap.pendingTraversal];
		traversal->entityNum = SV_NumForGentity( ent );
		VectorAdd( ent->r.mins, ent->r.maxs, traversal->origin );
		VectorScale( traversal->origin, 0.5f, traversal->origin );
		VectorAdd( traversal->origin, ent->r.currentOrigin, traversal->origin );
		traversal->origin[2] += 32;
		svap.pendingTraversal = -1;
	}
	if ( ent->s.eType != ET_ITEM ) return qtrue;
	svap.familyMask = SVAP_CPMA_FamilyMask();
	pickup = SVAP_CPMA_PickupForEntity( ent );
	if ( pickup ) {
		if ( pickup->present && ( ent->s.eFlags & EF_NODRAW || !ent->r.contents ) ) {
			SVAP_CPMA_StartRespawn( pickup );
			playerState_t *player = SVAP_CPMA_HumanPlayer();
			SVAP_CPMA_DebugShard( "hide", pickup, player, ent );
			SVAP_CPMA_SendPickup( pickup, player );
		}
		if ( pickup->entityNum < 0 && ++svap.mappedCount == svap.pickupCount )
			Com_Printf( "Archipelago: CPMA mapped all %i static pickups\n", svap.mappedCount );
		pickup->entityNum = SV_NumForGentity( ent );
		if ( ent->r.contents && !( ent->s.eFlags & EF_NODRAW ) &&
			!( ent->r.svFlags & SVF_NOCLIENT ) ) {
			if ( pickup->respawnAt || pickup->modelIndex == 0 ) pickup->visibleAt = sv.time;
			pickup->respawnAt = 0;
			pickup->present = qtrue;
			pickup->normalContents = ent->r.contents;
			pickup->normalEFlags = ent->s.eFlags;
			pickup->modelIndex = ent->s.modelindex;
		} else {
			pickup->present = qfalse;
			if ( !pickup->normalContents ) {
				pickup->normalContents = CONTENTS_TRIGGER;
				pickup->normalEFlags = ent->s.eFlags & ~EF_NODRAW;
			}
		}
		VectorCopy( ent->r.currentOrigin, pickup->origin );
		if ( APCL_GameQuery( Q3AP_GAME_LOCATION_CHECKED, pickup->catalog->location_id ) ||
			 SVAP_CPMA_PickupUnlocked( pickup ) ) {
			pickup->locked = qfalse;
			return qtrue;
		}
		pickup->locked = qtrue;
	}
	// Keep CPMA authoritative and the item linked so bots retain vanilla access.
	return qtrue;
}

void SVAP_CPMA_UnlinkEntity( void *entity ) {
	sharedEntity_t *ent = entity;
	svapCpmaPickup_t *pickup;
	playerState_t *player;
	if ( !svap.enabled || !ent || ent->s.eType != ET_ITEM ) return;
	pickup = SVAP_CPMA_MappedPickup( SV_NumForGentity( ent ) );
	if ( !pickup || !pickup->present ) return;
	SVAP_CPMA_StartRespawn( pickup );
	pickup->present = qfalse;
	player = SVAP_CPMA_HumanPlayer();
	SVAP_CPMA_DebugShard( "unlink", pickup, player, ent );
	SVAP_CPMA_SendPickup( pickup, player );
}

int SVAP_CPMA_FilterAreaEntities( const vec3_t mins, const vec3_t maxs,
	int *entities, int count ) {
	playerState_t *player;
	int read, write = 0;
	if ( !svap.enabled || !entities || count <= 0 ) return count;
	player = SVAP_CPMA_HumanPlayer();
	// the syscall omits the toucher, CPMA's standard trigger bounds identify the local player.
	if ( !player || !Q3AP_CPMA_IsTriggerScan( mins, maxs, player->origin ) ) return count;
	for ( read = 0; read < count; ++read ) {
		sharedEntity_t *ent = SV_GentityNum( entities[read] );
		svapCpmaPickup_t *pickup = SVAP_CPMA_MappedPickup( entities[read] );
		int traversal;
		int family = ent->s.eType == ET_ITEM ? SVAP_CPMA_FamilyForModelIndex( ent->s.modelindex ) : -1;
		qboolean checked;
		for ( traversal = 0; traversal < svap.traversalCount; ++traversal )
			if ( svap.traversals[traversal].entityNum == entities[read] ) {
				APCL_SendLocation( svap.traversals[traversal].catalog->location_id );
				break;
			}
		if ( family >= 0 && !( svap.familyMask & ( 1u << family ) ) ) continue;
		if ( !pickup || !SVAP_CPMA_LocationActive( pickup ) ||
			 !SVAP_CPMA_PickupPresent( pickup ) ||
			 !Q3AP_CPMA_PlayerTouches( player->origin, pickup->origin ) ) {
			entities[write++] = entities[read];
			continue;
		}
		checked = APCL_GameQuery( Q3AP_GAME_LOCATION_CHECKED, pickup->catalog->location_id );
		if ( checked || pickup->sent ) {
			entities[write++] = entities[read];
			continue;
		}
		if ( pickup->visibleAt >= sv.time ) continue;
		if ( !pickup->sent ) SVAP_CPMA_CommitPickup( pickup );
		if ( SVAP_CPMA_DebugPickups() )
			Com_Printf( "APDBG filtered touch ord=%03i checked=%i sent=%i time=%i\n",
				pickup->catalog->ordinal, checked, pickup->sent, sv.time );
	}
	return write;
}

static void SVAP_CPMA_CheckPickupEvent( playerState_t *player, int event, int modelIndex,
	const char *source, int sequence ) {
		float bestDistance = -1;
		svapCpmaPickup_t *best = NULL;
		qboolean bestTouches = qfalse;
		int index;
		if ( event != EV_ITEM_PICKUP && event != EV_GLOBAL_ITEM_PICKUP ) return;
		if ( SVAP_CPMA_DebugPickups() )
			Com_Printf( "APDBG pickup %s client=%i sequence=%i event=%i model=%i time=%i\n",
				source, player->clientNum, sequence, event, modelIndex, sv.time );
		for ( index = 0; index < svap.pickupCount; ++index ) {
			svapCpmaPickup_t *pickup = &svap.pickups[index];
			float distance;
			qboolean touches;
			if ( pickup->entityNum < 0 ||
				 !Q3AP_CPMA_PickupEventReady( pickup->lastPickupEventTime, sv.time ) ) continue;
			distance = Q3AP_CPMA_PickupEventDistance( player->origin, pickup->origin,
				modelIndex, pickup->modelIndex );
			touches = distance >= 0 && Q3AP_CPMA_PlayerTouches( player->origin, pickup->origin );
			if ( SVAP_CPMA_DebugPickups() && SVAP_CPMA_IsShard( pickup ) )
				Com_Printf( "APDBG shard candidate ord=%03i eventModel=%i itemModel=%i "
					"touch=%i distance=%.1f entity=%i checked=%i active=%i sent=%i\n", pickup->catalog->ordinal,
					modelIndex, pickup->modelIndex, touches, distance, pickup->entityNum,
					APCL_GameQuery( Q3AP_GAME_LOCATION_CHECKED, pickup->catalog->location_id ),
					SVAP_CPMA_LocationActive( pickup ), pickup->sent );
			if ( Q3AP_CPMA_PickupCandidateBetter( touches, pickup->entityNum, distance,
				bestTouches, best ? best->entityNum : -1, bestDistance ) ) {
				bestDistance = distance;
				best = pickup;
				bestTouches = touches;
			}
		}
		if ( SVAP_CPMA_DebugPickups() )
			Com_Printf( "APDBG pickup event result ord=%03i\n", best ? best->catalog->ordinal : 0 );
		if ( best ) best->lastPickupEventTime = sv.time;
		if ( best && !best->locked && !best->sent && SVAP_CPMA_LocationActive( best ) &&
			 !APCL_GameQuery( Q3AP_GAME_LOCATION_CHECKED, best->catalog->location_id ) )
			SVAP_CPMA_CommitPickup( best );
	}

static void SVAP_CPMA_CheckPickupEvents( playerState_t *player ) {
	int sequence, first;
	if ( svap.eventClientNum != player->clientNum || svap.lastEventSequence > player->eventSequence ) {
		if ( SVAP_CPMA_DebugPickups() )
			Com_Printf( "APDBG event init client=%i sequence=%i time=%i\n",
				player->clientNum, player->eventSequence, sv.time );
		svap.eventClientNum = player->clientNum;
		svap.lastEventSequence = player->eventSequence;
		svap.lastExternalEventTime = player->externalEventTime;
		return;
	}
	first = svap.lastEventSequence;
	if ( first < player->eventSequence - MAX_PS_EVENTS ) first = player->eventSequence - MAX_PS_EVENTS;
	for ( sequence = first; sequence < player->eventSequence; ++sequence ) {
		int slot = sequence & ( MAX_PS_EVENTS - 1 );
		SVAP_CPMA_CheckPickupEvent( player, player->events[slot] & ~EV_EVENT_BITS,
			player->eventParms[slot], "event", sequence );
	}
	svap.lastEventSequence = player->eventSequence;
	if ( player->externalEventTime != svap.lastExternalEventTime ) {
		SVAP_CPMA_CheckPickupEvent( player, player->externalEvent & ~EV_EVENT_BITS,
			player->externalEventParm, "external", player->externalEventTime );
		svap.lastExternalEventTime = player->externalEventTime;
	}
}

static void SVAP_CPMA_CheckTouches( playerState_t *player ) {
	int index;
	for ( index = 0; index < svap.pickupCount; ++index ) {
		svapCpmaPickup_t *pickup = &svap.pickups[index];
		if ( pickup->entityNum < 0 || pickup->locked || pickup->sent ||
			 !SVAP_CPMA_LocationActive( pickup ) ||
			 APCL_GameQuery( Q3AP_GAME_LOCATION_CHECKED, pickup->catalog->location_id ) ) continue;
		if ( SVAP_CPMA_PickupPresent( pickup ) ) SVAP_CPMA_SendPickup( pickup, player );
	}
}

static int SVAP_CPMA_WeaponForFamily( int family ) {
	static const int weapons[] = { WP_SHOTGUN, WP_MACHINEGUN, WP_GRENADE_LAUNCHER,
		WP_ROCKET_LAUNCHER, WP_LIGHTNING, WP_RAILGUN, WP_PLASMAGUN, WP_BFG };
	return family >= 0 && family < (int)( sizeof( weapons ) / sizeof( weapons[0] ) ) ? weapons[family] : WP_NONE;
}

static int SVAP_CPMA_StartingAmmo( int weapon ) {
	switch ( weapon ) {
	case WP_MACHINEGUN: return 100;
	case WP_LIGHTNING: return 100;
	case WP_PLASMAGUN: return 50;
	case WP_BFG: return 20;
	default: return 10;
	}
}

static void SVAP_CPMA_ApplyInventory( playerState_t *player, uint32_t oldMask ) {
	int allowed = 1 << WP_GAUNTLET;
	int family;
	qboolean spawned = player->persistant[PERS_SPAWN_COUNT] != svap.lastSpawnCount;
	for ( family = 0; family < 8; ++family ) {
		int weapon = SVAP_CPMA_WeaponForFamily( family );
		if ( svap.familyMask & ( 1u << family ) ) allowed |= 1 << weapon;
		else player->ammo[weapon] = 0;
		if ( ( spawned || !( oldMask & ( 1u << family ) ) ) && ( svap.familyMask & ( 1u << family ) ) )
			player->ammo[weapon] = SVAP_CPMA_StartingAmmo( weapon );
	}
	player->stats[STAT_WEAPONS] &= allowed;
	if ( spawned || oldMask != svap.familyMask ) player->stats[STAT_WEAPONS] |= allowed;
	if ( !( player->stats[STAT_WEAPONS] & ( 1 << player->weapon ) ) ) player->weapon = WP_GAUNTLET;
	svap.lastSpawnCount = player->persistant[PERS_SPAWN_COUNT];
}

static void SVAP_CPMA_UnlockPickups( void ) {
	int index;
	for ( index = 0; index < svap.pickupCount; ++index ) {
		svapCpmaPickup_t *pickup = &svap.pickups[index];
		sharedEntity_t *ent;
		if ( !pickup->locked || pickup->entityNum < 0 ||
			( !SVAP_CPMA_PickupUnlocked( pickup ) &&
			 !APCL_GameQuery( Q3AP_GAME_LOCATION_CHECKED, pickup->catalog->location_id ) ) ) continue;
		ent = SV_GentityNum( pickup->entityNum );
		ent->s.eFlags = pickup->normalEFlags;
		ent->r.contents = pickup->normalContents;
		pickup->visibleAt = sv.time;
		pickup->present = qtrue;
		SV_LinkEntity( ent );
		pickup->locked = qfalse;
	}
}

static qboolean SVAP_CPMA_PrivateLocationPresent( const svapCpmaPickup_t *pickup ) {
	int index;
	for ( index = 0; index < svap.pickupCount; ++index ) {
		svapCpmaPickup_t *sibling = &svap.pickups[index];
		sharedEntity_t *ent;
		int entityNum;
		if ( sibling->catalog->location_id != pickup->catalog->location_id ) continue;
		entityNum = sibling->entityNum >= 0 ? sibling->entityNum : sibling->privateEntityNum;
		if ( entityNum < 0 ) continue;
		ent = SV_GentityNum( entityNum );
		if ( ent->r.linked && ent->r.contents && !( ent->s.eFlags & EF_NODRAW ) &&
			!( ent->r.svFlags & SVF_NOCLIENT ) ) return qtrue;
	}
	return qfalse;
}

static void SVAP_CPMA_EnsureRespawns( void ) {
	int index;
	for ( index = 0; index < svap.pickupCount; ++index ) {
		svapCpmaPickup_t *pickup = &svap.pickups[index];
		sharedEntity_t *ent;
		int restoreEntityNum, sibling;
		qboolean futureRespawn = qfalse;
		if ( !SVAP_CPMA_LocationActive( pickup ) ||
			( pickup->entityNum < 0 && pickup->privateEntityNum < 0 ) || pickup->respawnAt <= 0 ||
			pickup->respawnAt > sv.time || pickup->sent ||
			APCL_GameQuery( Q3AP_GAME_LOCATION_CHECKED, pickup->catalog->location_id ) ) continue;
		if ( SVAP_CPMA_PrivateLocationPresent( pickup ) ) {
			pickup->respawnAt = 0;
			continue;
		}
		for ( sibling = 0; sibling < svap.pickupCount; ++sibling ) {
			svapCpmaPickup_t *candidate = &svap.pickups[sibling];
			int privateEntityNum = -1;
			if ( candidate->catalog->location_id != pickup->catalog->location_id ) continue;
			if ( SVAP_CPMA153_RespawnAt( candidate, &privateEntityNum ) > sv.time ) futureRespawn = qtrue;
			if ( privateEntityNum >= 0 ) candidate->privateEntityNum = privateEntityNum;
		}
		if ( futureRespawn ) continue;
		restoreEntityNum = pickup->entityNum >= 0 ? pickup->entityNum : pickup->privateEntityNum;
		ent = SV_GentityNum( restoreEntityNum );
		if ( ent->s.eType != ET_ITEM || ent->r.contents || !( ent->s.eFlags & EF_NODRAW ) ) {
			pickup->respawnAt = 0;
			continue;
		}
		if ( pickup->entityNum < 0 ) {
			pickup->entityNum = restoreEntityNum;
			++svap.mappedCount;
		}
		pickup->privateEntityNum = -1;
		pickup->normalContents = CONTENTS_TRIGGER;
		pickup->normalEFlags = ent->s.eFlags & ~EF_NODRAW;
		pickup->modelIndex = ent->s.modelindex;
		ent->s.eFlags = pickup->normalEFlags;
		ent->r.contents = pickup->normalContents;
		ent->r.svFlags &= ~SVF_NOCLIENT;
		pickup->respawnAt = 0;
		pickup->visibleAt = sv.time;
		pickup->present = qtrue;
		SV_LinkEntity( ent );
		Com_Printf( "Archipelago: CPMA restored suppressed shared pickup %03i\n",
			pickup->catalog->ordinal );
	}
}

static void SVAP_CPMA_UpdateScore( playerState_t *player ) {
	int score = player->persistant[PERS_SCORE];
	int mapIndex = svap.map->map_index;
	if ( !svap.scoreInitialized ) {
		svap.lastScore = score;
		svap.scoreInitialized = qtrue;
		return;
	}
	while ( score > svap.lastScore && svap.killCount < svap.map->frag_limit ) {
		int powerup;
		for ( powerup = PW_QUAD; svap.map->powerup_frag_location_id && powerup <= PW_FLIGHT; ++powerup )
			if ( player->powerups[powerup] > sv.time ) {
				APCL_SendLocation( svap.map->powerup_frag_location_id );
				break;
			}
		++svap.killCount;
		APCL_SendLocation( Q3AP_LOCATION_BASE + mapIndex * Q3AP_LOCATION_MAP_STRIDE +
			Q3AP_KILL_LOCATION_OFFSET + svap.killCount );
		++svap.lastScore;
	}
	svap.lastScore = score;
	if ( score >= svap.map->frag_limit ) APCL_SendLocation( svap.map->clear_location_id );
}

void SVAP_CPMA_BeforeFrame( void ) {
	playerState_t *player;
	if ( !svap.enabled ) return;
	player = SVAP_CPMA_HumanPlayer();
	svap.haveFrameStartOrigin = player != NULL;
	if ( player ) {
		VectorCopy( player->origin, svap.frameStartOrigin );
		SVAP_CPMA_CheckTouches( player );
	}
}

static void SVAP_CPMA_ApplyFiller( playerState_t *player ) {
	sharedEntity_t *ent;
	int health, limit, index, arena, warmup;
	if ( !player || player->pm_type != PM_NORMAL || player->stats[STAT_HEALTH] <= 0 ||
		!SVAP_CPMA153_HasLayout() ) return;
	ent = SV_GentityNum( player->clientNum );
	if ( !SVAP_CPMA153_PrivateInt( ent, SVAP_CPMA153_ARENA_OFFSET, &arena ) || arena < 0 ||
		(uint32_t)arena >= gvm->exactDataLength / SVAP_CPMA153_ARENA_SIZE ||
		!SVAP_CPMA153_VMInt( SVAP_CPMA153_ARENA_WARMUP + (uint32_t)arena * SVAP_CPMA153_ARENA_SIZE,
			&warmup ) || warmup ) return;
	if ( !SVAP_CPMA153_PrivateInt( ent, SVAP_CPMA153_HEALTH_OFFSET, &health ) ||
		health != player->stats[STAT_HEALTH] ) return;
	limit = player->stats[STAT_MAX_HEALTH];
	health = Q3AP_RefillStat( health, limit,
		APCL_TakeFiller( Q3AP_HEALTH_FILLER_ITEM_ID, limit - health ) );
	memcpy( (byte *)ent + SVAP_CPMA153_HEALTH_OFFSET, &health, sizeof( health ) );
	player->stats[STAT_HEALTH] = health;
	limit *= 2;
	player->stats[STAT_ARMOR] = Q3AP_RefillStat( player->stats[STAT_ARMOR], limit,
		APCL_TakeFiller( Q3AP_ARMOR_FILLER_ITEM_ID, limit - player->stats[STAT_ARMOR] ) );
	for ( index = 0; index < Q3AP_AMMO_FILLER_COUNT; ++index ) {
		const q3ap_ammo_filler_t *ammo = &q3ap_ammo_fillers[index];
		int weapon = SVAP_CPMA_WeaponForFamily( ammo->family_index );
		if ( weapon == WP_NONE || player->ammo[weapon] < 0 ||
			!( player->stats[STAT_WEAPONS] & ( 1 << weapon ) ) ||
			!( svap.familyMask & ( 1u << ammo->family_index ) ) ) continue;
		player->ammo[weapon] = Q3AP_RefillStat( player->ammo[weapon], 200,
			APCL_TakeFiller( ammo->item_id, 200 - player->ammo[weapon] ) );
	}
}

void SVAP_CPMA_AfterFrame( void ) {
	playerState_t *player;
	uint32_t oldMask;
	if ( !svap.enabled ) return;
	oldMask = svap.familyMask;
	svap.familyMask = SVAP_CPMA_FamilyMask();
	SVAP_CPMA_UnlockPickups();
	SVAP_CPMA_EnsureRespawns();
	player = SVAP_CPMA_HumanPlayer();
	if ( !player ) return;
	SVAP_CPMA_UpdatePlayerVisibility( player );
	SVAP_CPMA_CheckTouches( player );
	SVAP_CPMA_CheckPickupEvents( player );
	SVAP_CPMA_ApplyInventory( player, oldMask );
	SVAP_CPMA_ApplyFiller( player );
	SVAP_CPMA_UpdateScore( player );
}

int SVAP_CPMA_CopyMarkers( svapCpmaMarker_t *markers, int capacity ) {
	int locations[SVAP_CPMA_MAX_PICKUPS];
	int index, count = 0;
	if ( !svap.enabled || !markers || capacity < 1 ) return 0;
	for ( index = 0; index < svap.pickupCount && count < capacity; ++index ) {
		svapCpmaPickup_t *pickup = &svap.pickups[index];
		int classification, prior;
		if ( ( pickup->entityNum < 0 && !pickup->parsed ) || !SVAP_CPMA_PickupUnlocked( pickup ) || pickup->sent ||
			APCL_GameQuery( Q3AP_GAME_LOCATION_CHECKED, pickup->catalog->location_id ) ) continue;
		classification = APCL_GameQuery( Q3AP_GAME_LOCATION_CLASSIFICATION, pickup->catalog->location_id );
		if ( classification == Q3AP_CLASSIFICATION_UNKNOWN ) continue;
		for ( prior = 0; prior < count; ++prior )
			if ( locations[prior] == pickup->catalog->location_id ) break;
		if ( prior < count ) continue;
		VectorCopy( pickup->entityNum >= 0 ? pickup->origin : pickup->bspOrigin, markers[count].origin );
		markers[count].origin[2] += 32;
		markers[count].classification = classification;
		locations[count] = pickup->catalog->location_id;
		++count;
	}
	for ( index = 0; index < svap.traversalCount && count < capacity; ++index ) {
		svapCpmaTraversal_t *traversal = &svap.traversals[index];
		int classification;
		if ( traversal->entityNum < 0 ||
			 APCL_GameQuery( Q3AP_GAME_LOCATION_CHECKED, traversal->catalog->location_id ) ) continue;
		classification = APCL_GameQuery( Q3AP_GAME_LOCATION_CLASSIFICATION,
			traversal->catalog->location_id );
		if ( classification == Q3AP_CLASSIFICATION_UNKNOWN ) continue;
		VectorCopy( traversal->origin, markers[count].origin );
		markers[count].classification = classification;
		++count;
	}
	if ( SVAP_CPMA_DebugPickups() && count != svap.markerDebugCount ) {
		Com_Printf( "APDBG markers exported=%i mapped=%i/%i familyMask=%08x\n",
			count, svap.mappedCount, svap.pickupCount, svap.familyMask );
		for ( index = 0; index < svap.pickupCount; ++index ) {
			svapCpmaPickup_t *pickup = &svap.pickups[index];
			int classification = APCL_GameQuery( Q3AP_GAME_LOCATION_CLASSIFICATION,
				pickup->catalog->location_id );
			if ( classification == Q3AP_CLASSIFICATION_UNKNOWN ) continue;
			Com_Printf( "APDBG marker ord=%03i ent=%i family=%i locked=%i sent=%i checked=%i class=%i\n",
				pickup->catalog->ordinal, pickup->entityNum, pickup->catalog->family_index,
				pickup->locked, pickup->sent,
				APCL_GameQuery( Q3AP_GAME_LOCATION_CHECKED, pickup->catalog->location_id ), classification );
		}
		svap.markerDebugCount = count;
	}
	return count;
}

qboolean SVAP_RespawnVisible( const vec3_t viewOrigin, const vec3_t itemOrigin ) {
	trace_t trace;
	if ( sv.state != SS_GAME ) return qfalse;
	// Include moving doors and platforms, but not players or pickup triggers.
	SV_Trace( &trace, viewOrigin, vec3_origin, vec3_origin, itemOrigin,
		ENTITYNUM_NONE, CONTENTS_SOLID, qfalse );
	return !trace.startsolid && !trace.allsolid && trace.fraction == 1.0f;
}

int SVAP_CopyRespawns( svapRespawn_t *respawns, int capacity ) {
	int locations[SVAP_CPMA_MAX_PICKUPS];
	int index, count = 0;
	if ( !respawns || capacity < 1 || sv.state != SS_GAME ) return 0;
	if ( svap.enabled ) {
		for ( index = 0; index < svap.pickupCount && count < capacity; ++index ) {
			svapCpmaPickup_t *pickup = &svap.pickups[index];
			svapCpmaPickup_t *displayPickup = pickup;
			int prior, respawnAt = pickup->respawnAt;
			qboolean estimated = qfalse;
			if ( !SVAP_CPMA_PickupUnlocked( pickup ) ) continue;
			if ( !Cvar_VariableIntegerValue( "ap_show_all_respawns" ) &&
				( !SVAP_CPMA_LocationActive( pickup ) || pickup->sent ||
				APCL_GameQuery( Q3AP_GAME_LOCATION_CHECKED, pickup->catalog->location_id ) ) ) continue;
			if ( SVAP_CPMA_PrivateLocationPresent( pickup ) ) continue;
			if ( pickup->parsed && SVAP_CPMA_HasInitialPowerupDelay( pickup ) ) {
				int sibling;
				int exactRespawnAt = 0;
				if ( SVAP_CPMA_DebugPickups() && !svap.exactTimerDebugged )
					Com_Printf( "APDBG CPMA153 layout=%i entry=%p crc=%08x/%08x ins=%i/%i "
						"jitCode=%u data=%u/%u gsize=%i/%i entities=%i\n",
						SVAP_CPMA153_HasLayout(), gvm ? (void *)gvm->entryPoint : NULL,
						gvm ? gvm->crc32sum : 0, SVAP_CPMA153_QVM_CRC32,
						gvm ? gvm->instructionCount : 0, SVAP_CPMA153_INSTRUCTIONS,
						gvm ? gvm->codeLength : 0,
						gvm ? gvm->exactDataLength : 0, SVAP_CPMA153_DATA_LENGTH,
						sv.gentitySize, SVAP_CPMA153_GENTITY_SIZE, sv.num_entities );
				for ( sibling = 0; sibling < svap.pickupCount; ++sibling ) {
					svapCpmaPickup_t *candidate = &svap.pickups[sibling];
					int candidateRespawnAt = candidate->respawnAt > sv.time ? candidate->respawnAt : 0;
					int privateEntityNum = -1;
					if ( candidate->catalog->location_id != pickup->catalog->location_id ||
						!candidate->parsed ||
						!SVAP_CPMA_HasInitialPowerupDelay( candidate ) ||
						!SVAP_CPMA_PickupUnlocked( candidate ) ) continue;
					{
						int privateRespawnAt = SVAP_CPMA153_RespawnAt( candidate, &privateEntityNum );
						if ( privateRespawnAt ) candidateRespawnAt = privateRespawnAt;
					}
					if ( privateEntityNum >= 0 ) candidate->privateEntityNum = privateEntityNum;
					if ( candidateRespawnAt ) candidate->respawnAt = candidateRespawnAt;
					if ( candidateRespawnAt && ( !exactRespawnAt || candidateRespawnAt < exactRespawnAt ) ) {
						exactRespawnAt = candidateRespawnAt;
						displayPickup = candidate;
					}
				}
				svap.exactTimerDebugged = qtrue;
				if ( !exactRespawnAt && SVAP_CPMA_DebugPickups() && !svap.exactTimerMissingDebugged ) {
					SVAP_CPMA153_DebugLocation( pickup, "exact-missing" );
					svap.exactTimerMissingDebugged = qtrue;
				}
				if ( exactRespawnAt ) {
					respawnAt = exactRespawnAt;
				} else if ( respawnAt <= sv.time ) {
					respawnAt = svap.mapStartTime + 60000;
					estimated = qtrue;
				}
			} else {
				// Ammo and other pickups use the same RespawnItem schedule.
				int exactRespawnAt = SVAP_CPMA153_RespawnAt( pickup, NULL );
				if ( exactRespawnAt ) respawnAt = exactRespawnAt;
				else if ( SVAP_CPMA153_HasLayout() ) continue;
				else estimated = qtrue;
			}
			if ( respawnAt <= sv.time ) {
				if ( estimated && SVAP_CPMA_DebugPickups() && !svap.estimatedTimerExpiredDebugged ) {
					SVAP_CPMA153_DebugLocation( pickup, "estimate-expired" );
					svap.estimatedTimerExpiredDebugged = qtrue;
				}
				continue;
			}
			for ( prior = 0; prior < count; ++prior )
				if ( locations[prior] == pickup->catalog->location_id ) break;
			if ( prior < count ) continue;
			VectorCopy( displayPickup->entityNum >= 0 ? displayPickup->origin : displayPickup->bspOrigin,
				respawns[count].origin );
			respawns[count].estimated = estimated;
			respawns[count].respawnAt = respawnAt;
			respawns[count].durationMs = respawnAt - ( displayPickup->respawnStartedAt ?
				displayPickup->respawnStartedAt : svap.mapStartTime );
			locations[count++] = pickup->catalog->location_id;
		}
		return count;
	}
	for ( index = MAX_CLIENTS; index < sv.num_entities && count < capacity; ++index ) {
		sharedEntity_t *item = SV_GentityNum( index );
		if ( item->s.eType != ET_ITEM || item->s.generic1 != Q3AP_RESPAWN_MAGIC ||
			item->s.time <= sv.time || !( item->s.eFlags & EF_NODRAW ) ) continue;
		VectorCopy( item->r.currentOrigin, respawns[count].origin );
		respawns[count].estimated = qfalse;
		respawns[count].durationMs = item->s.time2;
		respawns[count++].respawnAt = item->s.time;
	}
	return count;
}
