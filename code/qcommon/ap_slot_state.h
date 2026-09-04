#ifndef AP_SLOT_STATE_H
#define AP_SLOT_STATE_H

#include <stdint.h>

#include "../ap/q3ap_api.h"
#include "../ap/q3ap_catalog.h"
#include "ap_runtime_state.h"

#define APCL_SLOT_HASH_SIZE 65

typedef enum {
	APCL_SLOT_SCHEMA_VERSION,
	APCL_SLOT_CPMA,
	APCL_SLOT_GOAL_TYPE,
	APCL_SLOT_GOAL_REQUIRED,
	APCL_SLOT_KILL_CHECK_INCREMENT,
	APCL_SLOT_WEAPON_LOGIC_PERCENTAGE,
	APCL_SLOT_ITEM_LOGIC_PERCENTAGE
} apclSlotInteger_t;

typedef struct {
	uint32_t seen;
	uint64_t selected_map_mask;
	uint32_t pickup_locations[Q3AP_UI_MAX_MAPS][Q3AP_LOCATION_MAP_STRIDE / 32];
	int pickup_locations_invalid;
	int32_t schema_version;
	int32_t cpma;
	int32_t starting_map_index;
	int32_t goal_type;
	int32_t goal_required;
	int32_t kill_check_increment;
	int32_t weapon_logic_percentage;
	int32_t item_logic_percentage;
	char catalog_hash[APCL_SLOT_HASH_SIZE];
	char error[Q3AP_STATUS_TEXT_SIZE];
	int valid;
} apclSlotState_t;

void APCL_SlotStateInit( apclSlotState_t *state );
int APCL_SlotSetInteger( apclSlotState_t *state, apclSlotInteger_t field, uint64_t value );
int APCL_SlotSetCatalogHash( apclSlotState_t *state, const char *value );
int APCL_SlotSetStartingMap( apclSlotState_t *state, const char *value );
int APCL_SlotSetSelectedMaps( apclSlotState_t *state, const char *const *values, int count );
int APCL_SlotSetPickupLocations( apclSlotState_t *state, const int32_t *values, int count );
int APCL_SlotPickupIncluded( const apclSlotState_t *state, int location_id );
int APCL_SlotValidate( apclSlotState_t *state );
int APCL_SlotValidateRuntime( apclSlotState_t *state, int cpma );
int APCL_SlotLocationsReady( const apclSlotState_t *state, const apclRuntimeState_t *runtime );

#endif
