#undef NDEBUG
#include <assert.h>
#include <string.h>

#include "../code/qcommon/ap_runtime_state.h"

int main( void ) {
	apclRuntimeState_t state, replay;
	int ammoIndex;
	int stage = q3ap_catalog_maps[1].stage_item_id;
	int clear = q3ap_catalog_maps[1].clear_location_id;
	int pickup = q3ap_catalog_maps[1].pickups[0].location_id;
	int traversal = q3ap_catalog_maps[1].traversal_count ? q3ap_catalog_maps[1].traversals[0].location_id :
		q3ap_catalog_maps[0].traversals[0].location_id;
	uint64_t selected = 1ULL << 1;
	uint64_t high_selected = 1ULL << 59;

	APCL_RuntimeInit( &state );
	assert( !APCL_RuntimeQueueFiller( &state, Q3AP_NOTHING_ITEM_ID ) );
	assert( APCL_RuntimeQueueFiller( &state, Q3AP_HEALTH_FILLER_ITEM_ID ) );
	assert( APCL_RuntimeQueueFiller( &state, Q3AP_HEALTH_FILLER_ITEM_ID ) );
	assert( APCL_RuntimeQueueFiller( &state, Q3AP_ARMOR_FILLER_ITEM_ID ) );
	/* An inventory replay must neither queue old refills nor discard pending ones. */
	APCL_RuntimeClearItems( &state );
	assert( !APCL_RuntimeReceiveItem( &state, Q3AP_HEALTH_FILLER_ITEM_ID ) );
	assert( APCL_RuntimeTakeFiller( &state, Q3AP_HEALTH_FILLER_ITEM_ID, 0 ) == 0 );
	assert( APCL_RuntimeTakeFiller( &state, Q3AP_HEALTH_FILLER_ITEM_ID, -25 ) == 0 );
	assert( APCL_RuntimeTakeFiller( &state, Q3AP_HEALTH_FILLER_ITEM_ID, 1 ) == 1 );
	APCL_RuntimeClearItems( &state );
	assert( APCL_RuntimeTakeFiller( &state, Q3AP_HEALTH_FILLER_ITEM_ID, 100 ) == 1 );
	assert( APCL_RuntimeTakeFiller( &state, Q3AP_HEALTH_FILLER_ITEM_ID, 0x7fffffff ) == 0 );
	assert( APCL_RuntimeTakeFiller( &state, Q3AP_ARMOR_FILLER_ITEM_ID, 0x7fffffff ) == 1 );
	for ( ammoIndex = 0; ammoIndex < Q3AP_AMMO_FILLER_COUNT; ++ammoIndex ) {
		const q3ap_ammo_filler_t *ammo = &q3ap_ammo_fillers[ammoIndex];
		assert( ammo->item_id == Q3AP_AMMO_FILLER_ITEM_BASE + ammoIndex );
		assert( ammo->family_index >= 0 && ammo->family_index < 8 );
		assert( APCL_RuntimeQueueFiller( &state, ammo->item_id ) );
		assert( APCL_RuntimeQueueFiller( &state, ammo->item_id ) );
		APCL_RuntimeClearItems( &state );
		assert( !APCL_RuntimeReceiveItem( &state, ammo->item_id ) );
		assert( APCL_RuntimeTakeFiller( &state, ammo->item_id, 0 ) == 0 );
		assert( APCL_RuntimeTakeFiller( &state, ammo->item_id, 1 ) == 1 );
		assert( APCL_RuntimeTakeFiller( &state, ammo->item_id, 200 ) == 2u * ammo->amount - 1 );
		assert( APCL_RuntimeTakeFiller( &state, ammo->item_id, 0x7fffffff ) == 0 );
	}
	assert( !APCL_RuntimeQueueFiller( &state, Q3AP_HEALTH_FILLER_ITEM_ID + Q3AP_REFILL_COUNT ) );
	assert( !APCL_RuntimeQueueFiller( &state, UINT64_MAX ) );
	assert( !APCL_RuntimeTakeFiller( &state, -1, 0x7fffffff ) );
	state.pending_filler[2] = 0x7ffffffeu;
	assert( APCL_RuntimeQueueFiller( &state, Q3AP_AMMO_FILLER_ITEM_BASE ) );
	assert( APCL_RuntimeTakeFiller( &state, Q3AP_AMMO_FILLER_ITEM_BASE, 0x7fffffff ) == 0x7fffffffu );
	assert( Q3AP_RefillStat( 100, 200, 1 ) == 101 );
	assert( Q3AP_RefillStat( 199, 200, 20 ) == 200 );
	assert( Q3AP_RefillStat( 250, 200, 1 ) == 250 );
	assert( Q3AP_RefillStat( 0, 200, 1 ) == 1 );
	assert( Q3AP_RefillStat( 100, 200, 0x7fffffffu ) == 200 );
	assert( !APCL_RuntimeReceiveItem( &state, Q3AP_NOTHING_ITEM_ID ) );
	assert( APCL_RuntimeReceiveItem( &state, Q3AP_QUAD_TOKEN_ITEM_ID ) );
	assert( APCL_RuntimeItemCount( &state, Q3AP_QUAD_TOKEN_ITEM_ID ) == 1 );
	assert( APCL_RuntimeReceiveItem( &state, stage ) );
	assert( APCL_RuntimeItemCount( &state, stage ) == 1 );
	assert( APCL_RuntimeUnlockedMaps( &state, selected ) == selected );
	assert( APCL_RuntimeCanStartStage( &state, selected, 1 ) );
	assert( !APCL_RuntimeCanStartStage( &state, selected, 2 ) );
	assert( APCL_RuntimeReceiveItem( &state, q3ap_catalog_maps[59].stage_item_id ) );
	assert( APCL_RuntimeUnlockedMaps( &state, high_selected ) == high_selected );
	assert( APCL_RuntimeCanStartStage( &state, high_selected, 59 ) );
	assert( APCL_RuntimeCheckLocation( &state, pickup ) );
	assert( APCL_RuntimeCheckLocation( &state, traversal ) );
	assert( !APCL_RuntimeCheckLocation( &state, pickup ) );
	assert( APCL_RuntimeQueueLocation( &state, clear ) );
	assert( APCL_RuntimeLocationPending( &state, clear ) );
	assert( APCL_RuntimeCheckLocation( &state, clear ) );
	assert( !APCL_RuntimeLocationPending( &state, clear ) );
	assert( APCL_RuntimeClearedMaps( &state, selected ) == selected );
	assert( !APCL_RuntimeCheckLocation( &state, Q3AP_LOCATION_BASE + 399 ) );
	assert( APCL_RuntimeLocationKnown( pickup ) && !APCL_RuntimeLocationKnown( Q3AP_LOCATION_BASE + 399 ) );
	assert( APCL_RuntimeLocationClassification( &state, pickup ) == Q3AP_CLASSIFICATION_UNKNOWN );
	assert( APCL_RuntimeSetLocationClassification( &state, pickup, Q3AP_CLASSIFICATION_USEFUL ) );
	assert( !APCL_RuntimeSetLocationClassification( &state, pickup, Q3AP_CLASSIFICATION_USEFUL ) );
	assert( APCL_RuntimeLocationClassification( &state, pickup ) == Q3AP_CLASSIFICATION_USEFUL );

	replay = state;
	assert( APCL_RuntimeClearItems( &state ) );
	assert( APCL_RuntimeItemCount( &state, Q3AP_QUAD_TOKEN_ITEM_ID ) == 0 );
	assert( !APCL_RuntimeClearItems( &state ) );
	assert( APCL_RuntimeReceiveItem( &state, stage ) );
	assert( !memcmp( state.checked, replay.checked, sizeof( state.checked ) ) );
	assert( APCL_RuntimeItemCount( &state, stage ) == APCL_RuntimeItemCount( &replay, stage ) );

	APCL_RuntimeInit( &state );
	assert( APCL_RuntimeInLogicMaps( &state, selected, 0, 0 ) == selected );
	assert( APCL_RuntimeInLogicMaps( &state, selected, 100, 100 ) == 0 );
	selected |= 1ULL << 2;
	assert( APCL_RuntimeCheckLocation( &state, q3ap_catalog_maps[1].clear_location_id ) );
	assert( APCL_RuntimeGoalProgress( &state, selected ) == 1 );
	assert( APCL_RuntimeCheckLocation( &state, q3ap_catalog_maps[3].clear_location_id ) );
	assert( APCL_RuntimeGoalProgress( &state, selected ) == 1 );
	assert( APCL_RuntimeCheckLocation( &state, q3ap_catalog_maps[2].clear_location_id ) );
	assert( APCL_RuntimeGoalProgress( &state, selected ) == 2 );
	return 0;
}
