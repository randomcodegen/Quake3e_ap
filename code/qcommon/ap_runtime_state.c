#include <string.h>

#include "ap_runtime_state.h"

static int APCL_FamilyIndex( int item_id ) {
	int index;
	for ( index = 0; index < Q3AP_FAMILY_COUNT; ++index )
		if ( q3ap_catalog_families[index].item_id == item_id ) return index;
	return -1;
}

static int APCL_LocationParts( uint64_t location_id, int *map_index, int *offset ) {
	uint64_t relative;
	const q3ap_catalog_map_t *map;
	if ( location_id < Q3AP_LOCATION_BASE ) return 0;
	relative = location_id - Q3AP_LOCATION_BASE;
	*map_index = (int)( relative / Q3AP_LOCATION_MAP_STRIDE );
	*offset = (int)( relative % Q3AP_LOCATION_MAP_STRIDE );
	if ( *map_index < 0 || *map_index >= Q3AP_MAP_COUNT ) return 0;
	map = &q3ap_catalog_maps[*map_index];
	return ( *offset >= 1 && *offset <= map->pickup_count ) ||
		( *offset > Q3AP_TRAVERSAL_LOCATION_OFFSET &&
		  *offset <= Q3AP_TRAVERSAL_LOCATION_OFFSET + map->traversal_count ) ||
		( map->powerup_frag_location_id && *offset == Q3AP_POWERUP_FRAG_LOCATION_OFFSET ) ||
		( *offset > Q3AP_KILL_LOCATION_OFFSET &&
		  *offset <= Q3AP_KILL_LOCATION_OFFSET + map->frag_limit ) ||
		*offset == Q3AP_CLEAR_LOCATION_OFFSET;
}

static int APCL_RequiredCount( int count, int percentage ) {
	return ( count * percentage + 99 ) / 100;
}

void APCL_RuntimeInit( apclRuntimeState_t *state ) {
	memset( state, 0, sizeof( *state ) );
	memset( state->location_classification, Q3AP_CLASSIFICATION_UNKNOWN,
		sizeof( state->location_classification ) );
}

int APCL_RuntimeClearItems( apclRuntimeState_t *state ) {
	int index, changed = 0;
	for ( index = 0; index < Q3AP_MAP_COUNT; ++index ) changed |= state->stage_items[index] != 0;
	for ( index = 0; index < Q3AP_FAMILY_COUNT; ++index ) changed |= state->family_items[index] != 0;
	changed |= state->quad_tokens != 0;
	memset( state->stage_items, 0, sizeof( state->stage_items ) );
	memset( state->family_items, 0, sizeof( state->family_items ) );
	state->quad_tokens = 0;
	return changed;
}

int APCL_RuntimeReceiveItem( apclRuntimeState_t *state, uint64_t item_id ) {
	int index;
	if ( item_id == Q3AP_QUAD_TOKEN_ITEM_ID ) {
		++state->quad_tokens;
		return 1;
	}
	if ( item_id >= Q3AP_ITEM_BASE && item_id < Q3AP_ITEM_BASE + Q3AP_MAP_COUNT ) {
		++state->stage_items[item_id - Q3AP_ITEM_BASE];
		return 1;
	}
	index = APCL_FamilyIndex( (int)item_id );
	if ( index >= 0 ) {
		++state->family_items[index];
		return 1;
	}
	return 0;
}

int APCL_RuntimeQueueFiller( apclRuntimeState_t *state, uint64_t item_id ) {
	int index;
	uint32_t amount;
	if ( item_id < Q3AP_HEALTH_FILLER_ITEM_ID ||
		item_id >= Q3AP_HEALTH_FILLER_ITEM_ID + Q3AP_REFILL_COUNT ) return 0;
	index = (int)( item_id - Q3AP_HEALTH_FILLER_ITEM_ID );
	amount = index < 2 ? 1 : q3ap_ammo_fillers[index - 2].amount;
	state->pending_filler[index] += amount > 0x7fffffffu - state->pending_filler[index] ?
		0x7fffffffu - state->pending_filler[index] : amount;
	return 1;
}

uint32_t APCL_RuntimeTakeFiller( apclRuntimeState_t *state, int item_id ) {
	int index;
	uint32_t amount;
	if ( item_id < Q3AP_HEALTH_FILLER_ITEM_ID ||
		item_id >= Q3AP_HEALTH_FILLER_ITEM_ID + Q3AP_REFILL_COUNT ) return 0;
	index = item_id - Q3AP_HEALTH_FILLER_ITEM_ID;
	amount = state->pending_filler[index];
	state->pending_filler[index] = 0;
	return amount;
}

int APCL_RuntimeCheckLocation( apclRuntimeState_t *state, uint64_t location_id ) {
	int map_index, offset;
	uint32_t bit;
	int changed;
	if ( !APCL_LocationParts( location_id, &map_index, &offset ) ) return 0;
	bit = 1u << ( offset & 31 );
	changed = !!( state->pending[map_index][offset >> 5] & bit );
	state->pending[map_index][offset >> 5] &= ~bit;
	if ( state->checked[map_index][offset >> 5] & bit ) return changed;
	state->checked[map_index][offset >> 5] |= bit;
	return 1;
}

int APCL_RuntimeQueueLocation( apclRuntimeState_t *state, uint64_t location_id ) {
	int map_index, offset;
	uint32_t bit;
	if ( !APCL_LocationParts( location_id, &map_index, &offset ) ) return 0;
	bit = 1u << ( offset & 31 );
	if ( ( state->checked[map_index][offset >> 5] |
		 state->pending[map_index][offset >> 5] ) & bit ) return 0;
	state->pending[map_index][offset >> 5] |= bit;
	return 1;
}

int APCL_RuntimeLocationPending( const apclRuntimeState_t *state, int location_id ) {
	int map_index, offset;
	if ( !APCL_LocationParts( (uint64_t)location_id, &map_index, &offset ) ) return 0;
	return !!( state->pending[map_index][offset >> 5] & ( 1u << ( offset & 31 ) ) );
}

uint32_t APCL_RuntimeItemCount( const apclRuntimeState_t *state, int item_id ) {
	int index;
	if ( item_id == Q3AP_QUAD_TOKEN_ITEM_ID ) return state->quad_tokens;
	if ( item_id >= Q3AP_ITEM_BASE && item_id < Q3AP_ITEM_BASE + Q3AP_MAP_COUNT )
		return state->stage_items[item_id - Q3AP_ITEM_BASE];
	index = APCL_FamilyIndex( item_id );
	return index >= 0 ? state->family_items[index] : 0;
}

int APCL_RuntimeLocationChecked( const apclRuntimeState_t *state, int location_id ) {
	int map_index, offset;
	if ( !APCL_LocationParts( (uint64_t)location_id, &map_index, &offset ) ) return 0;
	return !!( state->checked[map_index][offset >> 5] & ( 1u << ( offset & 31 ) ) );
}

int APCL_RuntimeLocationKnown( int location_id ) {
	int map_index, offset;
	return APCL_LocationParts( (uint64_t)location_id, &map_index, &offset );
}

int APCL_RuntimeSetLocationClassification( apclRuntimeState_t *state, int location_id, int flags ) {
	int map_index, offset;
	if ( !APCL_LocationParts( (uint64_t)location_id, &map_index, &offset ) ) return 0;
	if ( state->location_classification[map_index][offset] == (int8_t)flags ) return 0;
	state->location_classification[map_index][offset] = (int8_t)flags;
	return 1;
}

int APCL_RuntimeLocationClassification( const apclRuntimeState_t *state, int location_id ) {
	int map_index, offset;
	if ( !APCL_LocationParts( (uint64_t)location_id, &map_index, &offset ) )
		return Q3AP_CLASSIFICATION_UNKNOWN;
	return state->location_classification[map_index][offset];
}

uint64_t APCL_RuntimeUnlockedMaps( const apclRuntimeState_t *state, uint64_t selected_mask ) {
	uint64_t result = 0;
	int index;
	for ( index = 0; index < Q3AP_MAP_COUNT; ++index )
		if ( ( selected_mask & ( 1ULL << index ) ) && state->stage_items[index] ) result |= 1ULL << index;
	return result;
}

uint64_t APCL_RuntimeClearedMaps( const apclRuntimeState_t *state, uint64_t selected_mask ) {
	uint64_t result = 0;
	int index;
	for ( index = 0; index < Q3AP_MAP_COUNT; ++index )
		if ( ( selected_mask & ( 1ULL << index ) ) &&
			 APCL_RuntimeLocationChecked( state, q3ap_catalog_maps[index].clear_location_id ) ) result |= 1ULL << index;
	return result;
}

int APCL_RuntimeGoalProgress( const apclRuntimeState_t *state, uint64_t selected_mask ) {
	uint64_t cleared = APCL_RuntimeClearedMaps( state, selected_mask );
	int count = 0;
	while ( cleared ) { count += (int)( cleared & 1ULL ); cleared >>= 1; }
	return count;
}

uint64_t APCL_RuntimeInLogicMaps( const apclRuntimeState_t *state, uint64_t selected_mask,
	int weapon_percentage, int item_percentage ) {
	uint64_t result = 0;
	int map_index;
	for ( map_index = 0; map_index < Q3AP_MAP_COUNT; ++map_index ) {
		uint32_t families = 0;
		int weapon_count = 0, item_count = 0, weapon_have = 0, item_have = 0, pickup_index, family_index;
		if ( !( selected_mask & ( 1ULL << map_index ) ) ) continue;
		for ( pickup_index = 0; pickup_index < q3ap_catalog_maps[map_index].pickup_count; ++pickup_index )
			families |= 1u << q3ap_catalog_maps[map_index].pickups[pickup_index].family_index;
		for ( family_index = 0; family_index < Q3AP_FAMILY_COUNT; ++family_index ) {
			if ( !( families & ( 1u << family_index ) ) ) continue;
			if ( q3ap_catalog_families[family_index].weapon ) {
				++weapon_count;
				if ( state->family_items[family_index] ) ++weapon_have;
			} else {
				++item_count;
				if ( state->family_items[family_index] ) ++item_have;
			}
		}
		if ( weapon_have >= APCL_RequiredCount( weapon_count, weapon_percentage ) &&
			 item_have >= APCL_RequiredCount( item_count, item_percentage ) ) result |= 1ULL << map_index;
	}
	return result;
}

int APCL_RuntimeCanStartStage( const apclRuntimeState_t *state, uint64_t selected_mask, int map_index ) {
	return map_index >= 0 && map_index < Q3AP_MAP_COUNT &&
		( selected_mask & ( 1ULL << map_index ) ) && state->stage_items[map_index] != 0;
}
