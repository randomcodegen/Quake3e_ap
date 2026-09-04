#include <assert.h>
#include <string.h>

#include "../code/qcommon/ap_slot_state.h"
#include "../code/ap/q3ap_catalog.h"

static void SetValid( apclSlotState_t *state ) {
	const char *maps[] = { "q3dm1", "cpma3" };
	int32_t pickups[] = { q3ap_catalog_maps[1].pickups[0].location_id,
		q3ap_catalog_maps[59].pickups[0].location_id,
		q3ap_catalog_maps[59].traversals[0].location_id };
	APCL_SlotStateInit( state );
	APCL_SlotSetInteger( state, APCL_SLOT_SCHEMA_VERSION, Q3AP_SLOT_SCHEMA_VERSION );
	APCL_SlotSetInteger( state, APCL_SLOT_CPMA, 1 );
	APCL_SlotSetCatalogHash( state, Q3AP_CATALOG_HASH );
	APCL_SlotSetSelectedMaps( state, maps, 2 );
	APCL_SlotSetPickupLocations( state, pickups, 3 );
	APCL_SlotSetStartingMap( state, "q3dm1" );
	APCL_SlotSetInteger( state, APCL_SLOT_GOAL_TYPE, Q3AP_GOAL_STAGE_CLEARS );
	APCL_SlotSetInteger( state, APCL_SLOT_GOAL_REQUIRED, 1 );
	APCL_SlotSetInteger( state, APCL_SLOT_KILL_CHECK_INCREMENT, 5 );
	APCL_SlotSetInteger( state, APCL_SLOT_WEAPON_LOGIC_PERCENTAGE, 50 );
	APCL_SlotSetInteger( state, APCL_SLOT_ITEM_LOGIC_PERCENTAGE, 50 );
}

int main( void ) {
	apclSlotState_t state;
	apclRuntimeState_t runtime;
	const char *duplicate[] = { "q3dm1", "q3dm1" };
	const char *unknown[] = { "q3dm1", "not-a-map" };

	SetValid( &state );
	assert( APCL_SlotValidate( &state ) );
	assert( APCL_SlotValidateRuntime( &state, 1 ) );
	assert( !APCL_SlotValidateRuntime( &state, 0 ) && strstr( state.error, "CPMA launcher" ) );
	SetValid( &state );
	assert( APCL_SlotValidateRuntime( &state, 1 ) );
	assert( state.selected_map_mask == ( ( 1ULL << 1 ) | ( 1ULL << 59 ) ) );
	assert( APCL_SlotPickupIncluded( &state, q3ap_catalog_maps[1].pickups[0].location_id ) );
	assert( !APCL_SlotPickupIncluded( &state, q3ap_catalog_maps[1].pickups[1].location_id ) );
	assert( APCL_SlotPickupIncluded( &state, q3ap_catalog_maps[59].traversals[0].location_id ) );
	APCL_RuntimeInit( &runtime );
	assert( !APCL_SlotLocationsReady( &state, &runtime ) );
	APCL_RuntimeSetLocationClassification( &runtime, q3ap_catalog_maps[1].pickups[0].location_id, 0 );
	assert( !APCL_SlotLocationsReady( &state, &runtime ) );
	APCL_RuntimeSetLocationClassification( &runtime, q3ap_catalog_maps[59].pickups[0].location_id, 1 );
	assert( !APCL_SlotLocationsReady( &state, &runtime ) );
	APCL_RuntimeSetLocationClassification( &runtime, q3ap_catalog_maps[59].traversals[0].location_id, 0 );
	assert( APCL_SlotLocationsReady( &state, &runtime ) );

	SetValid( &state ); APCL_SlotSetCatalogHash( &state, "bad" );
	assert( !APCL_SlotValidate( &state ) && strstr( state.error, "hash" ) );
	SetValid( &state ); APCL_SlotSetSelectedMaps( &state, duplicate, 2 );
	assert( !APCL_SlotValidate( &state ) );
	SetValid( &state ); APCL_SlotSetSelectedMaps( &state, unknown, 2 );
	assert( !APCL_SlotValidate( &state ) );
	SetValid( &state ); APCL_SlotSetStartingMap( &state, "q3dm2" );
	assert( !APCL_SlotValidate( &state ) );
	SetValid( &state ); APCL_SlotSetInteger( &state, APCL_SLOT_GOAL_REQUIRED, 3 );
	assert( !APCL_SlotValidate( &state ) );
	SetValid( &state ); APCL_SlotSetInteger( &state, APCL_SLOT_GOAL_TYPE, Q3AP_GOAL_QUAD_TOKEN_HUNT );
	APCL_SlotSetInteger( &state, APCL_SLOT_GOAL_REQUIRED, 30 );
	assert( APCL_SlotValidate( &state ) );
	SetValid( &state ); APCL_SlotSetInteger( &state, APCL_SLOT_ITEM_LOGIC_PERCENTAGE, 101 );
	assert( !APCL_SlotValidate( &state ) );
	SetValid( &state ); APCL_SlotSetInteger( &state, APCL_SLOT_ITEM_LOGIC_PERCENTAGE, 0x100000032ULL );
	assert( !APCL_SlotValidate( &state ) );
	SetValid( &state ); APCL_SlotSetInteger( &state, APCL_SLOT_CPMA, 2 );
	assert( !APCL_SlotValidate( &state ) );
	SetValid( &state ); APCL_SlotSetInteger( &state, APCL_SLOT_KILL_CHECK_INCREMENT, 0 );
	assert( !APCL_SlotValidate( &state ) );
	return 0;
}
