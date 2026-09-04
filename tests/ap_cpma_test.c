#undef NDEBUG
#include <assert.h>
#include "../code/qcommon/ap_cpma_logic.h"

int main( void ) {
	const float item[3] = { 100, 200, 300 };
	const float touching[3] = { 144, 164, 336 };
	const float outside[3] = { 145, 200, 300 };
	const float fastStart[3] = { 0, 200, 300 };
	const float fastEnd[3] = { 200, 200, 300 };
	const float missStart[3] = { 0, 300, 300 };
	const float fastMiss[3] = { 200, 300, 300 };
	const float dropped[3] = { 100, 200, 220 };
	const float otherSpawn[3] = { 102, 200, 220 };
	const float triggerMins[3] = { 104, 124, 284 };
	const float triggerMaxs[3] = { 184, 204, 388 };
	assert( Q3AP_CPMA_PlayerTouches( touching, item ) );
	assert( !Q3AP_CPMA_PlayerTouches( outside, item ) );
	assert( Q3AP_CPMA_IsTriggerScan( triggerMins, triggerMaxs, touching ) );
	assert( !Q3AP_CPMA_IsTriggerScan( triggerMins, triggerMaxs, item ) );
	assert( Q3AP_CPMA_PlayerSweptTouches( fastStart, fastEnd, item ) );
	assert( !Q3AP_CPMA_PlayerSweptTouches( missStart, fastMiss, item ) );
	assert( Q3AP_CPMA_PickupEventDistance( touching, item, 1, 1 ) >= 0 );
	assert( Q3AP_CPMA_PickupEventDistance( touching, item, 1, 2 ) < 0 );
	assert( Q3AP_CPMA_PickupCandidateBetter( 1, 5, 100, 1, 12, 10 ) );
	assert( !Q3AP_CPMA_PickupCandidateBetter( 0, 5, 1, 1, 12, 10 ) );
	assert( !Q3AP_CPMA_PickupEventReady( 9050, 9350 ) );
	assert( Q3AP_CPMA_PickupEventReady( 9050, 10050 ) );
	assert( Q3AP_CPMA_SpawnDistance( item, dropped ) == 6400 );
	assert( Q3AP_CPMA_SpawnDistance( item, otherSpawn ) < 0 );
	assert( Q3AP_CPMA_RespawnSeconds( "weapon_rocketlauncher", 3, 7 ) == 7 );
	assert( Q3AP_CPMA_RespawnSeconds( "ammo_rockets", 3, 7 ) == 40 );
	assert( Q3AP_CPMA_RespawnSeconds( "item_armor_shard", 8, 7 ) == 25 );
	assert( Q3AP_CPMA_RespawnSeconds( "item_quad", 13, 7 ) == 120 );
	assert( Q3AP_CPMA_RespawnAt( 1000, 59050, 0x2cf59, 0x2cf59 ) == 59050 );
	// Read the scheduled ammo delay, not the 40-second fallback above.
	assert( Q3AP_CPMA_RespawnAt( 1000, 21000, 0x2cf59, 0x2cf59 ) == 21000 );
	assert( Q3AP_CPMA_RespawnAt( 20999, 21000, 0x2cf59, 0x2cf59 ) == 21000 );
	assert( !Q3AP_CPMA_RespawnAt( 21000, 21000, 0x2cf59, 0x2cf59 ) );
	assert( Q3AP_CPMA_RespawnAt( 1000, 201900, 0x2cf59, 0x2cf59 ) == 201900 );
	assert( Q3AP_CPMA_RespawnAt( 180000, 239050, 0x2cf59, 0x2cf59 ) == 239050 );
	assert( !Q3AP_CPMA_RespawnAt( 1000, 59050, 0x2cf58, 0x2cf59 ) );
	assert( !Q3AP_CPMA_RespawnAt( 60000, 59050, 0x2cf59, 0x2cf59 ) );
	assert( !Q3AP_CPMA_RespawnAt( 1000, 601001, 0x2cf59, 0x2cf59 ) );
	return 0;
}
