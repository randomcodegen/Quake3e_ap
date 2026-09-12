#include <string.h>

#include "g_local.h"
#include "g_ap.h"

gapState_t gap_state;
static int gap_mappedPickups;
static int gap_mappedTraversals;
static qboolean gap_mappingMismatch;
static int gap_killCounter;
static qboolean gap_stageClearSent;

static uint64_t GAP_QueryMapMask( int selector ) {
	return (uint32_t)trap_AP_Query( selector, 0 ) |
		( (uint64_t)(uint32_t)trap_AP_Query( selector, 1 ) << 32 );
}

static int GAP_LocationOffset( int location_id, int *map_index, int *offset ) {
	int relative = location_id - Q3AP_LOCATION_BASE;
	if ( relative < 0 ) return 0;
	*map_index = relative / Q3AP_LOCATION_MAP_STRIDE;
	*offset = relative % Q3AP_LOCATION_MAP_STRIDE;
	return *map_index >= 0 && *map_index < Q3AP_MAP_COUNT;
}

static void GAP_SetChecked( int location_id ) {
	int map_index, offset;
	if ( GAP_LocationOffset( location_id, &map_index, &offset ) &&
		 trap_AP_Query( Q3AP_GAME_LOCATION_CHECKED, location_id ) )
		gap_state.checked[map_index][offset >> 5] |= 1u << ( offset & 31 );
}

#ifndef Q3AP_GAME_STATE_TEST
static int GAP_WeaponForFamily( int family_index ) {
	static const int weapons[] = { WP_SHOTGUN, WP_MACHINEGUN, WP_GRENADE_LAUNCHER,
		WP_ROCKET_LAUNCHER, WP_LIGHTNING, WP_RAILGUN, WP_PLASMAGUN, WP_BFG };
	return family_index >= 0 && family_index < (int)( sizeof( weapons ) / sizeof( weapons[0] ) ) ?
		weapons[family_index] : WP_NONE;
}

static int GAP_StartingAmmo( int weapon ) {
	switch ( weapon ) {
	case WP_MACHINEGUN: return 100;
	case WP_LIGHTNING: return 100;
	case WP_PLASMAGUN: return 50;
	case WP_BFG: return 20;
	default: return 10;
	}
}

static void GAP_ApplyNewWeapons( uint32_t added ) {
	int family, client;
	for ( family = 0; family < Q3AP_FAMILY_COUNT; ++family ) {
		int weapon;
		if ( !( added & ( 1u << family ) ) ) continue;
		weapon = GAP_WeaponForFamily( family );
		if ( weapon == WP_NONE ) continue;
		for ( client = 0; client < level.maxclients; ++client ) {
			gentity_t *ent = &g_entities[client];
			if ( ent->client && ent->client->pers.connected == CON_CONNECTED &&
				 !( ent->r.svFlags & SVF_BOT ) ) {
				ent->client->ps.stats[STAT_WEAPONS] |= 1 << weapon;
				if ( ent->client->ps.ammo[weapon] < GAP_StartingAmmo( weapon ) )
					ent->client->ps.ammo[weapon] = GAP_StartingAmmo( weapon );
			}
		}
	}
}

static void GAP_RevealNewPickups( uint32_t added ) {
	int entity;
	for ( entity = MAX_CLIENTS; added && entity < level.num_entities; ++entity ) {
		gentity_t *ent = &g_entities[entity];
		int family;
		if ( !ent->inuse || !ent->apLocked ) continue;
		family = GAP_FamilyForClassname( ent->classname );
		if ( family < 0 || !( added & ( 1u << family ) ) ) continue;
		ent->apLocked = qfalse;
		RespawnItem( ent );
	}
}

static void GAP_ApplyFiller( void ) {
	int client, index;
	if ( !trap_AP_Query( Q3AP_GAME_AUTHENTICATED, 0 ) || level.warmupTime ||
		level.intermissiontime || level.intermissionQueued ) return;
	for ( client = 0; client < level.maxclients; ++client ) {
		gentity_t *ent = &g_entities[client];
		playerState_t *ps;
		int limit;
		if ( !ent->client || ent->client->pers.connected != CON_CONNECTED ||
			( ent->r.svFlags & SVF_BOT ) || ent->health <= 0 ||
			ent->client->ps.pm_type != PM_NORMAL ) continue;
		ps = &ent->client->ps;
		limit = ps->stats[STAT_MAX_HEALTH];
		ent->health = Q3AP_RefillStat( ent->health, limit,
			trap_AP_TakeFiller( Q3AP_HEALTH_FILLER_ITEM_ID, limit - ent->health ) );
		ps->stats[STAT_HEALTH] = ent->health;
		limit *= 2;
		ps->stats[STAT_ARMOR] = Q3AP_RefillStat( ps->stats[STAT_ARMOR], limit,
			trap_AP_TakeFiller( Q3AP_ARMOR_FILLER_ITEM_ID, limit - ps->stats[STAT_ARMOR] ) );
		for ( index = 0; index < Q3AP_AMMO_FILLER_COUNT; ++index ) {
			const q3ap_ammo_filler_t *ammo = &q3ap_ammo_fillers[index];
			int weapon = GAP_WeaponForFamily( ammo->family_index );
			if ( weapon == WP_NONE || ps->ammo[weapon] < 0 || !( ps->stats[STAT_WEAPONS] & ( 1 << weapon ) ) ||
				!trap_AP_Query( Q3AP_GAME_ITEM_COUNT, q3ap_catalog_families[ammo->family_index].item_id ) ) continue;
			ps->ammo[weapon] = Q3AP_RefillStat( ps->ammo[weapon], 200,
				trap_AP_TakeFiller( ammo->item_id, 200 - ps->ammo[weapon] ) );
		}
		break;
	}
}
#endif

void GAP_Init( void ) {
	char hash[Q3AP_CATALOG_HASH_SIZE];
	memset( &gap_state, 0, sizeof( gap_state ) );
	gap_killCounter = 0;
	gap_stageClearSent = qfalse;
	if ( trap_AP_Query( Q3AP_GAME_API_VERSION, 0 ) != Q3AP_API_VERSION ) {
		G_Printf( "Archipelago: engine API version mismatch\n" );
		return;
	}
	hash[0] = '\0';
	if ( !trap_AP_GetString( Q3AP_GAME_STRING_CATALOG_HASH, hash, sizeof( hash ) ) ||
		 strcmp( hash, Q3AP_CATALOG_HASH ) ) {
		G_Printf( "Archipelago: catalogue hash mismatch\n" );
		return;
	}
	gap_state.compatible = 1;
	GAP_Synchronize();
}

int GAP_Synchronize( void ) {
	uint32_t generation, old_families;
	int map_index, pickup_index, traversal_index, family_index, kill;
	if ( !gap_state.compatible ) return 0;
	generation = (uint32_t)trap_AP_Query( Q3AP_GAME_STATE_GENERATION, 0 );
	if ( generation == gap_state.generation ) {
#ifndef Q3AP_GAME_STATE_TEST
		GAP_ApplyFiller();
#endif
		return 0;
	}
	old_families = gap_state.weapon_family_mask | gap_state.pickup_family_mask;
	gap_state.generation = generation;
	gap_state.authenticated = trap_AP_Query( Q3AP_GAME_AUTHENTICATED, 0 );
	gap_state.selected_map_mask = gap_state.authenticated ? GAP_QueryMapMask( Q3AP_GAME_SELECTED_MAP_MASK ) : 0;
	gap_state.unlocked_map_mask = gap_state.authenticated ? GAP_QueryMapMask( Q3AP_GAME_UNLOCKED_MAP_MASK ) : 0;
	gap_state.active_map_index = gap_state.authenticated ?
		trap_AP_Query( Q3AP_GAME_ACTIVE_MAP_INDEX, 0 ) : -1;
	gap_state.weapon_family_mask = 0;
	gap_state.pickup_family_mask = 0;
	memset( gap_state.checked, 0, sizeof( gap_state.checked ) );
	if ( gap_state.authenticated ) {
		for ( family_index = 0; family_index < Q3AP_FAMILY_COUNT; ++family_index ) {
			if ( !trap_AP_Query( Q3AP_GAME_ITEM_COUNT, q3ap_catalog_families[family_index].item_id ) ) continue;
			if ( q3ap_catalog_families[family_index].weapon ) gap_state.weapon_family_mask |= 1u << family_index;
			else gap_state.pickup_family_mask |= 1u << family_index;
		}
		for ( map_index = 0; map_index < Q3AP_MAP_COUNT; ++map_index ) {
			if ( !( gap_state.selected_map_mask & ( 1ULL << map_index ) ) ) continue;
			for ( pickup_index = 0; pickup_index < q3ap_catalog_maps[map_index].pickup_count; ++pickup_index )
				GAP_SetChecked( q3ap_catalog_maps[map_index].pickups[pickup_index].location_id );
			for ( traversal_index = 0; traversal_index < q3ap_catalog_maps[map_index].traversal_count; ++traversal_index )
				GAP_SetChecked( q3ap_catalog_maps[map_index].traversals[traversal_index].location_id );
			if ( q3ap_catalog_maps[map_index].powerup_frag_location_id )
				GAP_SetChecked( q3ap_catalog_maps[map_index].powerup_frag_location_id );
			for ( kill = 1; kill <= q3ap_catalog_maps[map_index].frag_limit; ++kill )
				GAP_SetChecked( Q3AP_LOCATION_BASE + map_index * Q3AP_LOCATION_MAP_STRIDE +
					Q3AP_KILL_LOCATION_OFFSET + kill );
			GAP_SetChecked( q3ap_catalog_maps[map_index].clear_location_id );
		}
	}
#ifndef Q3AP_GAME_STATE_TEST
	GAP_ApplyNewWeapons( gap_state.weapon_family_mask & ~old_families );
	GAP_RevealNewPickups( ( gap_state.weapon_family_mask | gap_state.pickup_family_mask ) & ~old_families );
	G_APRefreshPickupMarkers();
	GAP_ApplyFiller();
#endif
	return 1;
}

int GAP_LocationChecked( int location_id ) {
	int map_index, offset;
	if ( !GAP_LocationOffset( location_id, &map_index, &offset ) ) return 0;
	return !!( gap_state.checked[map_index][offset >> 5] & ( 1u << ( offset & 31 ) ) );
}

int GAP_LocationClassification( int location_id ) {
	if ( !gap_state.authenticated || !location_id ) return Q3AP_CLASSIFICATION_UNKNOWN;
	return trap_AP_Query( Q3AP_GAME_LOCATION_CLASSIFICATION, location_id );
}

qboolean GAP_SendLocation( int location_id ) {
	if ( !gap_state.authenticated || GAP_LocationChecked( location_id ) ) return qfalse;
	return trap_AP_SendLocation( location_id );
}

qboolean GAP_StageSelectionAllowed( int map_index ) {
	uint64_t bit;
	if ( !gap_state.authenticated ) return qtrue;
	if ( map_index < 0 || map_index >= Q3AP_MAP_COUNT ) return qfalse;
	bit = 1ULL << map_index;
	return gap_state.active_map_index == map_index &&
		( gap_state.selected_map_mask & bit ) && ( gap_state.unlocked_map_mask & bit );
}

qboolean GAP_CurrentStageAllowed( void ) {
#ifdef Q3AP_GAME_STATE_TEST
	return qtrue;
#else
	char serverinfo[MAX_INFO_STRING];
	const q3ap_catalog_map_t *map;
	if ( !gap_state.authenticated ) return qtrue;
	trap_GetServerinfo( serverinfo, sizeof( serverinfo ) );
	map = Q3AP_CatalogMapByKey( Info_ValueForKey( serverinfo, "mapname" ) );
	return map && GAP_StageSelectionAllowed( map->map_index );
#endif
}

int GAP_CatalogPickupForOrdinal( int map_index, int bsp_ordinal, const char *classname ) {
	const q3ap_catalog_map_t *map;
	int index;
	if ( map_index < 0 || map_index >= Q3AP_MAP_COUNT || !classname ) return 0;
	map = &q3ap_catalog_maps[map_index];
	for ( index = 0; index < map->pickup_count; ++index ) {
		const q3ap_catalog_pickup_t *pickup = &map->pickups[index];
		if ( pickup->bsp_entity_ordinal == bsp_ordinal )
			return !strcmp( pickup->classname, classname ) ? pickup->location_id : 0;
	}
	return 0;
}

void GAP_BeginStaticMapping( void ) {
	gap_mappedPickups = 0;
	gap_mappedTraversals = 0;
	gap_mappingMismatch = qfalse;
}

void GAP_MapStaticTraversal( gentity_t *ent, int bsp_ordinal ) {
	const q3ap_catalog_map_t *map;
	int index, kind;
	if ( !gap_state.authenticated || gap_state.active_map_index < 0 || !ent || !ent->classname ) return;
	kind = !strcmp( ent->classname, "trigger_push" ) ? 0 :
		!strcmp( ent->classname, "trigger_teleport" ) && !( ent->spawnflags & 1 ) ? 1 : -1;
	if ( kind < 0 ) return;
	map = &q3ap_catalog_maps[gap_state.active_map_index];
	for ( index = 0; index < map->traversal_count; ++index ) {
		const q3ap_catalog_traversal_t *traversal = &map->traversals[index];
		if ( traversal->bsp_entity_ordinal != bsp_ordinal || traversal->kind != kind ) continue;
		ent->apTraversalLocationId = traversal->location_id;
		++gap_mappedTraversals;
		G_APUpdatePickupMarker( ent );
		return;
	}
	gap_mappingMismatch = qtrue;
}

void GAP_MapStaticPickup( gentity_t *ent, int bsp_ordinal ) {
	int location;
	if ( !gap_state.authenticated || !ent->inuse || !ent->item || ( ent->flags & FL_DROPPED_ITEM ) ) return;
	location = GAP_CatalogPickupForOrdinal( gap_state.active_map_index, bsp_ordinal, ent->classname );
	if ( !location ) {
		gap_mappingMismatch = qtrue;
		G_Printf( "Archipelago: static pickup catalogue mismatch at BSP entity %i (%s)\n",
			bsp_ordinal, ent->classname ? ent->classname : "unknown" );
		return;
	}
	ent->apLocationId = location;
	ent->apLocationSent = qfalse;
	++gap_mappedPickups;
}

qboolean GAP_FinishStaticMapping( void ) {
	if ( gap_state.authenticated && gap_state.active_map_index >= 0 &&
		 ( gap_mappedPickups != q3ap_catalog_maps[gap_state.active_map_index].pickup_count ||
		   gap_mappedTraversals != q3ap_catalog_maps[gap_state.active_map_index].traversal_count ) ) {
		gap_mappingMismatch = qtrue;
		G_Printf( "Archipelago: mapped %i/%i pickups and %i/%i traversals\n",
			gap_mappedPickups, q3ap_catalog_maps[gap_state.active_map_index].pickup_count,
			gap_mappedTraversals, q3ap_catalog_maps[gap_state.active_map_index].traversal_count );
	}
	return !gap_mappingMismatch;
}

void GAP_TouchTraversal( gentity_t *ent, gentity_t *player ) {
	if ( !gap_state.authenticated || !ent->apTraversalLocationId || ent->apTraversalLocationSent ||
		 !player || !player->client || ( player->r.svFlags & SVF_BOT ) ) return;
	if ( GAP_LocationChecked( ent->apTraversalLocationId ) || GAP_SendLocation( ent->apTraversalLocationId ) ) {
		ent->apTraversalLocationSent = qtrue;
		G_APClearPickupMarker( ent );
	}
}

void GAP_TouchStaticPickup( gentity_t *ent, gentity_t *player ) {
	int entity;
	if ( !gap_state.authenticated || !ent->apLocationId || ent->apLocationSent ||
		 !player || !player->client || ( player->r.svFlags & SVF_BOT ) ) return;
	if ( GAP_LocationChecked( ent->apLocationId ) ) {
		ent->apLocationSent = qtrue;
		G_APClearPickupMarker( ent );
		return;
	}
	if ( GAP_SendLocation( ent->apLocationId ) ) {
		for ( entity = MAX_CLIENTS; entity < level.num_entities; ++entity ) {
			gentity_t *shared = &g_entities[entity];
			if ( shared->apLocationId != ent->apLocationId ) continue;
			shared->apLocationSent = qtrue;
			G_APClearPickupMarker( shared );
		}
	}
}

int GAP_FamilyForClassname( const char *classname ) {
	int map_index, pickup_index;
	if ( !classname ) return -1;
	for ( map_index = 0; map_index < Q3AP_MAP_COUNT; ++map_index )
		for ( pickup_index = 0; pickup_index < q3ap_catalog_maps[map_index].pickup_count; ++pickup_index )
			if ( !strcmp( classname, q3ap_catalog_maps[map_index].pickups[pickup_index].classname ) )
				return q3ap_catalog_maps[map_index].pickups[pickup_index].family_index;
	return -1;
}

qboolean GAP_FamilyUnlocked( int family_index ) {
	uint32_t mask;
	if ( !gap_state.authenticated ) return qtrue;
	if ( family_index < 0 || family_index >= Q3AP_FAMILY_COUNT ) return qfalse;
	mask = q3ap_catalog_families[family_index].weapon ?
		gap_state.weapon_family_mask : gap_state.pickup_family_mask;
	return !!( mask & ( 1u << family_index ) );
}

qboolean GAP_PickupUnlocked( gentity_t *ent, gentity_t *player ) {
	int family;
	if ( !gap_state.authenticated ) return qtrue;
	family = GAP_FamilyForClassname( ent->classname );
	if ( GAP_FamilyUnlocked( family ) ) return qtrue;
#ifndef Q3AP_GAME_STATE_TEST
	if ( player && player->client && level.time >= ent->apLockedMessageTime ) {
		trap_SendServerCommand( player->s.number, va( "cp \"%s is locked by Archipelago\"\n",
			family >= 0 ? q3ap_catalog_families[family].name : "Pickup" ) );
		ent->apLockedMessageTime = level.time + 1500;
	}
#else
	(void)player;
#endif
	return qfalse;
}

void GAP_ApplySpawnInventory( gentity_t *player ) {
#ifndef Q3AP_GAME_STATE_TEST
	int family;
	if ( !gap_state.authenticated || !player || !player->client || ( player->r.svFlags & SVF_BOT ) ) return;
	player->client->ps.stats[STAT_WEAPONS] = 1 << WP_GAUNTLET;
	memset( player->client->ps.ammo, 0, sizeof( player->client->ps.ammo ) );
	player->client->ps.ammo[WP_GAUNTLET] = -1;
	player->client->ps.ammo[WP_GRAPPLING_HOOK] = -1;
	for ( family = 0; family < Q3AP_FAMILY_COUNT; ++family ) {
		int weapon;
		if ( !( gap_state.weapon_family_mask & ( 1u << family ) ) ) continue;
		weapon = GAP_WeaponForFamily( family );
		if ( weapon == WP_NONE ) continue;
		player->client->ps.stats[STAT_WEAPONS] |= 1 << weapon;
		player->client->ps.ammo[weapon] = GAP_StartingAmmo( weapon );
	}
#else
	(void)player;
#endif
}

int GAP_KillLocation( int map_index, int kill_number ) {
	if ( map_index < 0 || map_index >= Q3AP_MAP_COUNT || kill_number < 1 ||
		 kill_number > q3ap_catalog_maps[map_index].frag_limit ) return 0;
	return Q3AP_LOCATION_BASE + map_index * Q3AP_LOCATION_MAP_STRIDE +
		Q3AP_KILL_LOCATION_OFFSET + kill_number;
}

int GAP_NextKillLocation( void ) {
	int location = GAP_KillLocation( gap_state.active_map_index, gap_killCounter + 1 );
	if ( location ) ++gap_killCounter;
	return location;
}

void GAP_RecordKill( gentity_t *victim, gentity_t *attacker ) {
#ifndef Q3AP_GAME_STATE_TEST
	int client, humans = 0, location;
	gentity_t *human = NULL;
	if ( !gap_state.authenticated || !victim || !attacker || victim == attacker ||
		 !victim->client || !attacker->client || !( victim->r.svFlags & SVF_BOT ) ||
		 ( attacker->r.svFlags & SVF_BOT ) || OnSameTeam( victim, attacker ) ) return;
	for ( client = 0; client < level.maxclients; ++client ) {
		gentity_t *ent = &g_entities[client];
		if ( ent->client && ent->client->pers.connected == CON_CONNECTED &&
			 !( ent->r.svFlags & SVF_BOT ) && ent->client->sess.sessionTeam != TEAM_SPECTATOR ) {
			human = ent;
			++humans;
		}
	}
	if ( humans != 1 || human != attacker ) return;
	if ( gap_state.active_map_index >= 0 ) {
		const q3ap_catalog_map_t *map = &q3ap_catalog_maps[gap_state.active_map_index];
		int powerup;
		for ( powerup = PW_QUAD; map->powerup_frag_location_id && powerup <= PW_FLIGHT; ++powerup )
			if ( attacker->client->ps.powerups[powerup] > level.time ) {
				GAP_SendLocation( map->powerup_frag_location_id );
				break;
			}
	}
	location = GAP_NextKillLocation();
	if ( location ) GAP_SendLocation( location );
#else
	(void)victim; (void)attacker;
#endif
}

qboolean GAP_WinningRank( int rank ) {
	return rank == 0;
}

void GAP_RecordStageClear( void ) {
#ifndef Q3AP_GAME_STATE_TEST
	int client, humans = 0;
	gentity_t *human = NULL;
	if ( !gap_state.authenticated || gap_stageClearSent || gap_state.active_map_index < 0 ) return;
	for ( client = 0; client < level.maxclients; ++client ) {
		gentity_t *ent = &g_entities[client];
		if ( ent->client && ent->client->pers.connected == CON_CONNECTED &&
			 !( ent->r.svFlags & SVF_BOT ) && ent->client->sess.sessionTeam != TEAM_SPECTATOR ) {
			human = ent;
			++humans;
		}
	}
	if ( humans == 1 && GAP_WinningRank( human->client->ps.persistant[PERS_RANK] ) &&
		 GAP_SendLocation( q3ap_catalog_maps[gap_state.active_map_index].clear_location_id ) )
		gap_stageClearSent = qtrue;
#endif
}
