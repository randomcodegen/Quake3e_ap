#ifndef AP_RUNTIME_STATE_H
#define AP_RUNTIME_STATE_H

#include <stdint.h>

#include "../ap/q3ap_api.h"
#include "../ap/q3ap_catalog.h"

typedef struct {
    uint32_t stage_items[Q3AP_MAP_COUNT];
    uint32_t family_items[Q3AP_FAMILY_COUNT];
    uint32_t quad_tokens;
    uint32_t pending_filler[Q3AP_REFILL_COUNT];
    uint32_t checked[Q3AP_MAP_COUNT][Q3AP_LOCATION_MAP_STRIDE / 32];
    uint32_t pending[Q3AP_MAP_COUNT][Q3AP_LOCATION_MAP_STRIDE / 32];
    int8_t location_classification[Q3AP_MAP_COUNT][Q3AP_LOCATION_MAP_STRIDE];
} apclRuntimeState_t;

void APCL_RuntimeInit( apclRuntimeState_t *state );
int APCL_RuntimeClearItems( apclRuntimeState_t *state );
int APCL_RuntimeReceiveItem( apclRuntimeState_t *state, uint64_t item_id );
int APCL_RuntimeQueueFiller( apclRuntimeState_t *state, uint64_t item_id );
uint32_t APCL_RuntimeTakeFiller( apclRuntimeState_t *state, int item_id, int capacity );
int APCL_RuntimeCheckLocation( apclRuntimeState_t *state, uint64_t location_id );
int APCL_RuntimeQueueLocation( apclRuntimeState_t *state, uint64_t location_id );
int APCL_RuntimeLocationPending( const apclRuntimeState_t *state, int location_id );
uint32_t APCL_RuntimeItemCount( const apclRuntimeState_t *state, int item_id );
int APCL_RuntimeLocationChecked( const apclRuntimeState_t *state, int location_id );
int APCL_RuntimeLocationKnown( int location_id );
int APCL_RuntimeSetLocationClassification( apclRuntimeState_t *state, int location_id, int flags );
int APCL_RuntimeLocationClassification( const apclRuntimeState_t *state, int location_id );
uint64_t APCL_RuntimeUnlockedMaps( const apclRuntimeState_t *state, uint64_t selected_mask );
uint64_t APCL_RuntimeClearedMaps( const apclRuntimeState_t *state, uint64_t selected_mask );
int APCL_RuntimeGoalProgress( const apclRuntimeState_t *state, uint64_t selected_mask );
uint64_t APCL_RuntimeInLogicMaps( const apclRuntimeState_t *state, uint64_t selected_mask,
	int weapon_percentage, int item_percentage );
int APCL_RuntimeCanStartStage( const apclRuntimeState_t *state, uint64_t selected_mask, int map_index );

#endif
