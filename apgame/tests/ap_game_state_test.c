#undef NDEBUG
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../code/game/g_local.h"
#include "../code/game/g_ap.h"
#include "../../code/qcommon/ap_runtime_state.h"

static apclRuntimeState_t filler;

int trap_AP_TakeFiller( int itemId, int capacity ) {
	return (int)APCL_RuntimeTakeFiller( &filler, itemId, capacity );
}

static int generation = 1;
static int query_count;
static int bad_api;
static int bad_hash;
static uint32_t received_families = 1u;
static int respawn_count;
static int sent_locations[Q3AP_LOCATION_MAP_STRIDE];
static int sent_count;
static int marker_clear_count;
static int location_classification = Q3AP_CLASSIFICATION_USEFUL;
static int active_map_index = 1;

level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];

void G_Printf( const char *format, ... ) { (void)format; }
void RespawnItem( gentity_t *ent ) { (void)ent; ++respawn_count; }
void G_APClearPickupMarker( gentity_t *ent ) { (void)ent; ++marker_clear_count; }
void G_APUpdatePickupMarker( gentity_t *ent ) { (void)ent; }
void G_APRefreshPickupMarkers( void ) {}
void trap_SendServerCommand( int clientNum, const char *text ) { (void)clientNum; (void)text; }
void trap_GetServerinfo( char *buffer, int bufferSize ) {
	strncpy( buffer, "\\mapname\\q3dm1", bufferSize - 1 );
	buffer[bufferSize - 1] = '\0';
}
char *Info_ValueForKey( const char *s, const char *key ) { (void)s; (void)key; return "q3dm1"; }
qboolean OnSameTeam( gentity_t *ent1, gentity_t *ent2 ) { (void)ent1; (void)ent2; return qfalse; }
char * QDECL va( char *format, ... ) {
	static char buffer[1024];
	va_list args;
	va_start( args, format );
	vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	return buffer;
}

int trap_AP_Query( int selector, int argument ) {
	++query_count;
	switch ( selector ) {
	case Q3AP_GAME_API_VERSION: return bad_api ? 99 : Q3AP_API_VERSION;
	case Q3AP_GAME_STATE_GENERATION: return generation;
	case Q3AP_GAME_AUTHENTICATED: return 1;
	case Q3AP_GAME_SELECTED_MAP_MASK: return argument == 0 ? 1u << 1 : 0;
	case Q3AP_GAME_UNLOCKED_MAP_MASK: return argument == 0 ? 1u << 1 : 0;
	case Q3AP_GAME_ACTIVE_MAP_INDEX: return active_map_index;
	case Q3AP_GAME_ITEM_COUNT: {
		int family;
		for ( family = 0; family < Q3AP_FAMILY_COUNT; ++family )
			if ( argument == q3ap_catalog_families[family].item_id )
				return !!( received_families & ( 1u << family ) );
		return 0;
	}
	case Q3AP_GAME_LOCATION_CHECKED: {
		int index;
		if ( argument == q3ap_catalog_maps[1].pickups[0].location_id ) return 1;
		for ( index = 0; index < sent_count; ++index )
			if ( argument == sent_locations[index] ) return 1;
		return 0;
	}
	case Q3AP_GAME_LOCATION_CLASSIFICATION: return location_classification;
	default: return 0;
	}
}

qboolean trap_AP_GetString( int selector, char *buffer, int size ) {
	const char *value = bad_hash ? "bad" : Q3AP_CATALOG_HASH;
	(void)selector;
	strncpy( buffer, value, size - 1 );
	buffer[size - 1] = '\0';
	return qtrue;
}

qboolean trap_AP_SendLocation( int location_id ) {
	sent_locations[sent_count++] = location_id;
	return qtrue;
}

int main( void ) {
	gclient_t client;
	gclient_t bot_client;
	gclient_t second_human;
	gentity_t *pickup = &g_entities[MAX_CLIENTS];
	int calls, kill, sent_before;
	memset( &client, 0, sizeof( client ) );
	memset( &bot_client, 0, sizeof( bot_client ) );
	memset( &second_human, 0, sizeof( second_human ) );
	memset( g_entities, 0, sizeof( g_entities ) );
	level.maxclients = 1;
	level.num_entities = MAX_CLIENTS + 1;
	g_entities[0].client = &client;
	client.pers.connected = CON_CONNECTED;
	GAP_Init();
	assert( gap_state.compatible && gap_state.authenticated );
	assert( gap_state.weapon_family_mask == 1u );
	assert( GAP_FamilyForClassname( "weapon_shotgun" ) == 0 );
	assert( GAP_FamilyUnlocked( 0 ) && !GAP_FamilyUnlocked( 1 ) );
	assert( GAP_StageSelectionAllowed( 1 ) && !GAP_StageSelectionAllowed( 2 ) );
	assert( GAP_CatalogPickupForOrdinal( 1, 8, "ammo_bullets" ) == q3ap_catalog_maps[1].pickups[0].location_id );
	assert( GAP_CatalogPickupForOrdinal( 1, 100, "item_health" ) !=
		GAP_CatalogPickupForOrdinal( 1, 104, "item_health" ) );
	assert( GAP_NextKillLocation() == Q3AP_LOCATION_BASE + Q3AP_LOCATION_MAP_STRIDE + 401 );
	assert( GAP_NextKillLocation() == Q3AP_LOCATION_BASE + Q3AP_LOCATION_MAP_STRIDE + 402 );
	assert( GAP_WinningRank( 0 ) && !GAP_WinningRank( 1 ) && !GAP_WinningRank( RANK_TIED_FLAG ) );
	assert( GAP_LocationChecked( q3ap_catalog_maps[1].pickups[0].location_id ) );
	assert( GAP_LocationClassification( q3ap_catalog_maps[1].pickups[0].location_id ) ==
		Q3AP_CLASSIFICATION_USEFUL );
	pickup->apLocationId = q3ap_catalog_maps[1].pickups[0].location_id;
	GAP_TouchStaticPickup( pickup, &g_entities[0] );
	assert( pickup->apLocationSent && marker_clear_count == 1 );
	pickup->apLocationId = q3ap_catalog_maps[1].pickups[1].location_id;
	pickup->apLocationSent = qfalse;
	GAP_TouchStaticPickup( pickup, &g_entities[0] );
	assert( pickup->apLocationSent && marker_clear_count == 2 && sent_count == 1 );
	pickup->apTraversalLocationId = q3ap_catalog_maps[0].traversals[0].location_id;
	GAP_TouchTraversal( pickup, &g_entities[0] );
	assert( pickup->apTraversalLocationSent && sent_count == 2 );
	sent_count = 0;
	GAP_ApplySpawnInventory( &g_entities[0] );
	assert( client.ps.stats[STAT_WEAPONS] == ( ( 1 << WP_GAUNTLET ) | ( 1 << WP_SHOTGUN ) ) );
	assert( client.ps.ammo[WP_GAUNTLET] == -1 && client.ps.ammo[WP_SHOTGUN] == 10 );
	assert( client.ps.ammo[WP_MACHINEGUN] == 0 );
	pickup->inuse = qtrue;
	pickup->apLocked = qtrue;
	pickup->classname = "weapon_rocketlauncher";
	assert( !GAP_PickupUnlocked( pickup, NULL ) );
	location_classification = Q3AP_CLASSIFICATION_UNKNOWN;
	assert( !GAP_PickupUnlocked( pickup, NULL ) );
	location_classification = Q3AP_CLASSIFICATION_USEFUL;
	received_families |= 1u << 3;
	++generation;
	assert( GAP_Synchronize() );
	assert( client.ps.stats[STAT_WEAPONS] & ( 1 << WP_ROCKET_LAUNCHER ) );
	assert( client.ps.ammo[WP_ROCKET_LAUNCHER] == 10 );
	assert( !pickup->apLocked && respawn_count == 1 && GAP_PickupUnlocked( pickup, NULL ) );
	calls = query_count;
	/* Unchanged state still checks authentication for pending filler rewards. */
	assert( !GAP_Synchronize() && query_count == calls + 2 );
	++generation;
	assert( GAP_Synchronize() );
	GAP_Init(); /* Reset the kill counter advanced by GAP_NextKillLocation checks. */
	level.maxclients = 2;
	g_entities[1].client = &bot_client;
	g_entities[1].r.svFlags = SVF_BOT;
	bot_client.pers.connected = CON_CONNECTED;
	GAP_RecordKill( &g_entities[1], &g_entities[0] );
	assert( sent_count == 1 && sent_locations[0] == GAP_KillLocation( 1, 1 ) );
	level.maxclients = 3;
	g_entities[2].client = &second_human;
	second_human.pers.connected = CON_CONNECTED;
	GAP_RecordKill( &g_entities[1], &g_entities[0] );
	assert( sent_count == 1 );
	g_entities[2].client = NULL;
	level.maxclients = 2;
	GAP_RecordKill( &g_entities[0], &g_entities[0] );
	GAP_RecordKill( &g_entities[0], &g_entities[1] );
	GAP_RecordKill( &g_entities[0], NULL );
	assert( sent_count == 1 );
	GAP_RecordKill( &g_entities[1], &g_entities[0] );
	assert( sent_count == 2 && sent_locations[1] == GAP_KillLocation( 1, 2 ) );
	GAP_Init();
	GAP_RecordKill( &g_entities[1], &g_entities[0] );
	GAP_RecordKill( &g_entities[1], &g_entities[0] );
	assert( sent_count == 2 );
	for ( kill = 2; kill < q3ap_catalog_maps[1].frag_limit + 2; ++kill )
		GAP_RecordKill( &g_entities[1], &g_entities[0] );
	assert( sent_count == q3ap_catalog_maps[1].frag_limit );
	sent_before = sent_count;
	client.ps.persistant[PERS_RANK] = 0;
	GAP_RecordStageClear();
	GAP_RecordStageClear();
	assert( sent_count == sent_before + 1 &&
		sent_locations[sent_before] == q3ap_catalog_maps[1].clear_location_id );
	GAP_Init();
	client.ps.persistant[PERS_RANK] = 1;
	GAP_RecordStageClear();
	assert( sent_count == sent_before + 1 );
	GAP_Init();
	client.ps.persistant[PERS_RANK] = RANK_TIED_FLAG;
	GAP_RecordStageClear();
	assert( sent_count == sent_before + 1 );
	GAP_Init();
	client.ps.persistant[PERS_RANK] = 0;
	GAP_RecordStageClear();
	assert( sent_count == sent_before + 1 );

	active_map_index = 2;
	client.ps.powerups[PW_QUAD] = 1000;
	GAP_Init();
	sent_before = sent_count;
	GAP_RecordKill( &g_entities[1], &g_entities[0] );
	assert( sent_count == sent_before + 2 );
	assert( sent_locations[sent_before] == q3ap_catalog_maps[2].powerup_frag_location_id );
	assert( sent_locations[sent_before + 1] == GAP_KillLocation( 2, 1 ) );
	client.ps.powerups[PW_QUAD] = 0;
	active_map_index = 1;

	/* Refills wait through warmup/death/spectating and only consume the deficit. */
	level.maxclients = 1;
	g_entities[0].health = 99;
	client.ps.stats[STAT_MAX_HEALTH] = 100;
	client.ps.stats[STAT_ARMOR] = 199;
	client.ps.stats[STAT_WEAPONS] |= 1 << WP_SHOTGUN;
	client.ps.ammo[WP_SHOTGUN] = 199;
	APCL_RuntimeQueueFiller( &filler, Q3AP_HEALTH_FILLER_ITEM_ID );
	APCL_RuntimeQueueFiller( &filler, Q3AP_HEALTH_FILLER_ITEM_ID );
	APCL_RuntimeQueueFiller( &filler, Q3AP_ARMOR_FILLER_ITEM_ID );
	APCL_RuntimeQueueFiller( &filler, Q3AP_ARMOR_FILLER_ITEM_ID );
	APCL_RuntimeQueueFiller( &filler, Q3AP_AMMO_FILLER_ITEM_BASE );
	level.warmupTime = -1; GAP_Synchronize();
	level.warmupTime = 0;
	level.intermissiontime = 1; GAP_Synchronize();
	level.intermissiontime = 0;
	client.ps.pm_type = PM_SPECTATOR; GAP_Synchronize();
	client.ps.pm_type = PM_DEAD; GAP_Synchronize();
	assert( filler.pending_filler[0] == 2 && filler.pending_filler[1] == 2 && filler.pending_filler[2] == 2 );
	client.ps.pm_type = PM_NORMAL; GAP_Synchronize();
	assert( g_entities[0].health == 100 && client.ps.stats[STAT_HEALTH] == 100 );
	assert( client.ps.stats[STAT_ARMOR] == 200 && client.ps.ammo[WP_SHOTGUN] == 200 );
	GAP_Synchronize();
	assert( filler.pending_filler[0] == 1 && filler.pending_filler[1] == 1 && filler.pending_filler[2] == 1 );
	/* Starting another arena retains the unused queue, including overheal. */
	g_entities[0].health = 125;
	GAP_Init();
	assert( g_entities[0].health == 125 && filler.pending_filler[0] == 1 );
	g_entities[0].health = 98;
	client.ps.stats[STAT_ARMOR] = 190;
	client.ps.ammo[WP_SHOTGUN] = 190;
	client.ps.stats[STAT_WEAPONS] &= ~( 1 << WP_SHOTGUN );
	GAP_Synchronize();
	assert( g_entities[0].health == 99 && client.ps.stats[STAT_ARMOR] == 191 );
	assert( client.ps.ammo[WP_SHOTGUN] == 190 && filler.pending_filler[2] == 1 );
	client.ps.stats[STAT_WEAPONS] |= 1 << WP_SHOTGUN;
	GAP_Synchronize();
	assert( client.ps.ammo[WP_SHOTGUN] == 191 && filler.pending_filler[2] == 0 );

	bad_api = 1; GAP_Init(); assert( !gap_state.compatible ); bad_api = 0;
	bad_hash = 1; GAP_Init(); assert( !gap_state.compatible );
	return 0;
}
