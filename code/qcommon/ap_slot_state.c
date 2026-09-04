#include <stdio.h>
#include <string.h>

#include "ap_slot_state.h"
#include "../ap/q3ap_catalog.h"

enum {
	SEEN_SCHEMA = 1u << 0,
	SEEN_HASH = 1u << 1,
	SEEN_MAPS = 1u << 2,
	SEEN_START = 1u << 3,
	SEEN_GOAL_TYPE = 1u << 4,
	SEEN_GOAL = 1u << 5,
	SEEN_WEAPON = 1u << 6,
	SEEN_ITEM = 1u << 7,
	SEEN_PICKUPS = 1u << 8,
	SEEN_CPMA = 1u << 9,
	SEEN_KILL_INCREMENT = 1u << 10,
	SEEN_ALL = ( 1u << 11 ) - 1
};

static int APCL_CopyChanged( char *target, int size, const char *value ) {
	char copy[Q3AP_STATUS_TEXT_SIZE];
	if ( !value ) value = "";
	snprintf( copy, sizeof( copy ), "%s", value );
	copy[sizeof( copy ) - 1] = '\0';
	if ( !strcmp( target, copy ) ) return 0;
	snprintf( target, size, "%s", copy );
	target[size - 1] = '\0';
	return 1;
}

static int APCL_MapIndex( const char *key ) {
	const q3ap_catalog_map_t *map = Q3AP_CatalogMapByKey( key );
	return map ? map->map_index : -1;
}

void APCL_SlotStateInit( apclSlotState_t *state ) {
	memset( state, 0, sizeof( *state ) );
	state->starting_map_index = -1;
}

int APCL_SlotSetInteger( apclSlotState_t *state, apclSlotInteger_t field, uint64_t value ) {
	int32_t *target;
	uint32_t bit;
	switch ( field ) {
	case APCL_SLOT_SCHEMA_VERSION: target = &state->schema_version; bit = SEEN_SCHEMA; break;
	case APCL_SLOT_CPMA: target = &state->cpma; bit = SEEN_CPMA; break;
	case APCL_SLOT_GOAL_TYPE: target = &state->goal_type; bit = SEEN_GOAL_TYPE; break;
	case APCL_SLOT_GOAL_REQUIRED: target = &state->goal_required; bit = SEEN_GOAL; break;
	case APCL_SLOT_KILL_CHECK_INCREMENT: target = &state->kill_check_increment; bit = SEEN_KILL_INCREMENT; break;
	case APCL_SLOT_WEAPON_LOGIC_PERCENTAGE: target = &state->weapon_logic_percentage; bit = SEEN_WEAPON; break;
	case APCL_SLOT_ITEM_LOGIC_PERCENTAGE: target = &state->item_logic_percentage; bit = SEEN_ITEM; break;
	default: return 0;
	}
	if ( value > 0x7fffffffULL ) value = 0x80000000ULL;
	if ( ( state->seen & bit ) && *target == (int32_t)value ) return 0;
	*target = (int32_t)value;
	state->seen |= bit;
	state->valid = 0;
	return 1;
}

int APCL_SlotSetCatalogHash( apclSlotState_t *state, const char *value ) {
	int changed = !( state->seen & SEEN_HASH ) || strcmp( state->catalog_hash, value ? value : "" );
	if ( !changed ) return 0;
	snprintf( state->catalog_hash, sizeof( state->catalog_hash ), "%s", value ? value : "" );
	state->catalog_hash[sizeof( state->catalog_hash ) - 1] = '\0';
	state->seen |= SEEN_HASH;
	state->valid = 0;
	return 1;
}

int APCL_SlotSetStartingMap( apclSlotState_t *state, const char *value ) {
	int index = APCL_MapIndex( value );
	if ( ( state->seen & SEEN_START ) && state->starting_map_index == index ) return 0;
	state->starting_map_index = index;
	state->seen |= SEEN_START;
	state->valid = 0;
	return 1;
}

int APCL_SlotSetSelectedMaps( apclSlotState_t *state, const char *const *values, int count ) {
	uint64_t mask = 0;
	int i;
	int invalid = 0;
	for ( i = 0; i < count; ++i ) {
		int index = APCL_MapIndex( values[i] );
		if ( index < 0 || ( mask & ( 1ULL << index ) ) ) invalid = 1;
		else mask |= 1ULL << index;
	}
	if ( invalid ) mask = 0;
	if ( ( state->seen & SEEN_MAPS ) && state->selected_map_mask == mask ) return 0;
	state->selected_map_mask = mask;
	state->seen |= SEEN_MAPS;
	state->valid = 0;
	return 1;
}

int APCL_SlotSetPickupLocations( apclSlotState_t *state, const int32_t *values, int count ) {
	uint32_t locations[Q3AP_UI_MAX_MAPS][Q3AP_LOCATION_MAP_STRIDE / 32] = { { 0 } };
	int invalid = 0, i;
	for ( i = 0; i < count; ++i ) {
		int known = Q3AP_CatalogPickupByLocation( values[i] ) != NULL ||
			Q3AP_CatalogTraversalByLocation( values[i] ) != NULL;
		int map, offset;
		if ( !known ) { invalid = 1; continue; }
		map = ( values[i] - Q3AP_LOCATION_BASE ) / Q3AP_LOCATION_MAP_STRIDE;
		offset = ( values[i] - Q3AP_LOCATION_BASE ) % Q3AP_LOCATION_MAP_STRIDE;
		if ( locations[map][offset >> 5] & ( 1u << ( offset & 31 ) ) ) invalid = 1;
		locations[map][offset >> 5] |= 1u << ( offset & 31 );
	}
	if ( ( state->seen & SEEN_PICKUPS ) && state->pickup_locations_invalid == invalid &&
		 !memcmp( state->pickup_locations, locations, sizeof( locations ) ) ) return 0;
	memcpy( state->pickup_locations, locations, sizeof( locations ) );
	state->pickup_locations_invalid = invalid;
	state->seen |= SEEN_PICKUPS;
	state->valid = 0;
	return 1;
}

int APCL_SlotPickupIncluded( const apclSlotState_t *state, int location_id ) {
	int relative = location_id - Q3AP_LOCATION_BASE;
	int map, offset;
	if ( relative < 0 ) return 0;
	map = relative / Q3AP_LOCATION_MAP_STRIDE;
	offset = relative % Q3AP_LOCATION_MAP_STRIDE;
	return map < Q3AP_UI_MAX_MAPS &&
		!!( state->pickup_locations[map][offset >> 5] & ( 1u << ( offset & 31 ) ) );
}

int APCL_SlotLocationsReady( const apclSlotState_t *state, const apclRuntimeState_t *runtime ) {
	int map, pickup;
	if ( !state->valid ) return 0;
	for ( map = 0; map < Q3AP_MAP_COUNT; ++map ) {
		if ( !( state->selected_map_mask & ( 1ULL << map ) ) ) continue;
		for ( pickup = 0; pickup < q3ap_catalog_maps[map].pickup_count; ++pickup ) {
			int location = q3ap_catalog_maps[map].pickups[pickup].location_id;
			int offset = ( location - Q3AP_LOCATION_BASE ) % Q3AP_LOCATION_MAP_STRIDE;
			if ( APCL_SlotPickupIncluded( state, location ) &&
				 runtime->location_classification[map][offset] == Q3AP_CLASSIFICATION_UNKNOWN ) return 0;
		}
		for ( pickup = 0; pickup < q3ap_catalog_maps[map].traversal_count; ++pickup ) {
			int location = q3ap_catalog_maps[map].traversals[pickup].location_id;
			int offset = ( location - Q3AP_LOCATION_BASE ) % Q3AP_LOCATION_MAP_STRIDE;
			if ( APCL_SlotPickupIncluded( state, location ) &&
				 runtime->location_classification[map][offset] == Q3AP_CLASSIFICATION_UNKNOWN ) return 0;
		}
	}
	return 1;
}

int APCL_SlotValidate( apclSlotState_t *state ) {
	uint64_t selected;
	int map_count = 0;
	state->valid = 0;
	if ( state->seen != SEEN_ALL ) {
		APCL_CopyChanged( state->error, sizeof( state->error ), "Waiting for complete slot data" );
		return 0;
	}
	/* Schema 7 adds health/armor refills, 8 adds ammo; older seeds remain playable. */
	if ( state->schema_version < 6 || state->schema_version > Q3AP_SLOT_SCHEMA_VERSION ) {
		APCL_CopyChanged( state->error, sizeof( state->error ), "Unsupported slot-data schema" );
		return 0;
	}
	if ( strcmp( state->catalog_hash, Q3AP_CATALOG_HASH ) ) {
		APCL_CopyChanged( state->error, sizeof( state->error ), "Catalogue hash mismatch" );
		return 0;
	}
	if ( state->cpma < 0 || state->cpma > 1 ) {
		APCL_CopyChanged( state->error, sizeof( state->error ), "CPMA option is invalid" );
		return 0;
	}
	selected = state->selected_map_mask;
	while ( selected ) {
		map_count += (int)( selected & 1ULL );
		selected >>= 1;
	}
	if ( map_count < 1 ) {
		APCL_CopyChanged( state->error, sizeof( state->error ), "Selected map list is invalid" );
		return 0;
	}
	if ( state->pickup_locations_invalid ) {
		APCL_CopyChanged( state->error, sizeof( state->error ), "Pickup location list is invalid" );
		return 0;
	}
	{
		int map, word;
		for ( map = 0; map < Q3AP_UI_MAX_MAPS; ++map )
			if ( !( state->selected_map_mask & ( 1ULL << map ) ) )
				for ( word = 0; word < Q3AP_LOCATION_MAP_STRIDE / 32; ++word )
					if ( state->pickup_locations[map][word] ) {
						APCL_CopyChanged( state->error, sizeof( state->error ), "Pickup belongs to an unselected map" );
						return 0;
					}
	}
	if ( state->starting_map_index < 0 || !( state->selected_map_mask & ( 1ULL << state->starting_map_index ) ) ) {
		APCL_CopyChanged( state->error, sizeof( state->error ), "Starting map is not selected" );
		return 0;
	}
	if ( state->goal_type < Q3AP_GOAL_STAGE_CLEARS || state->goal_type > Q3AP_GOAL_QUAD_TOKEN_HUNT ) {
		APCL_CopyChanged( state->error, sizeof( state->error ), "Goal type is invalid" );
		return 0;
	}
	if ( state->goal_required < 1 ||
		 ( state->goal_type == Q3AP_GOAL_STAGE_CLEARS && state->goal_required > map_count ) ) {
		APCL_CopyChanged( state->error, sizeof( state->error ), "Goal count is invalid" );
		return 0;
	}
	if ( state->kill_check_increment < 1 || state->kill_check_increment > 50 ) {
		APCL_CopyChanged( state->error, sizeof( state->error ), "Kill check increment must be between 1 and 50" );
		return 0;
	}
	if ( state->weapon_logic_percentage < 0 || state->weapon_logic_percentage > 100 ||
		 state->item_logic_percentage < 0 || state->item_logic_percentage > 100 ) {
		APCL_CopyChanged( state->error, sizeof( state->error ), "Logic percentage must be between 0 and 100" );
		return 0;
	}
	state->valid = 1;
	state->error[0] = '\0';
	return 1;
}

int APCL_SlotValidateRuntime( apclSlotState_t *state, int cpma ) {
	if ( !APCL_SlotValidate( state ) ) return 0;
	if ( state->cpma != !!cpma ) {
		state->valid = 0;
		APCL_CopyChanged( state->error, sizeof( state->error ),
			state->cpma ? "Seed requires the CPMA launcher" : "Seed requires the vanilla launcher" );
		return 0;
	}
	return 1;
}
