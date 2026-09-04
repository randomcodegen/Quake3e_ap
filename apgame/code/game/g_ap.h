#ifndef G_AP_H
#define G_AP_H

#include "q_shared.h"
#include "../../../code/ap/q3ap_api.h"
#include "../../../code/ap/q3ap_catalog.h"

typedef struct gentity_s gentity_t;

typedef struct {
	int compatible;
	int authenticated;
	uint32_t generation;
	uint64_t selected_map_mask;
	uint64_t unlocked_map_mask;
	int active_map_index;
	uint32_t weapon_family_mask;
	uint32_t pickup_family_mask;
	uint32_t checked[Q3AP_MAP_COUNT][Q3AP_LOCATION_MAP_STRIDE / 32];
} gapState_t;

extern gapState_t gap_state;

void GAP_Init( void );
int GAP_Synchronize( void );
int GAP_LocationChecked( int location_id );
int GAP_LocationClassification( int location_id );
qboolean GAP_SendLocation( int location_id );
qboolean GAP_StageSelectionAllowed( int map_index );
qboolean GAP_CurrentStageAllowed( void );
int GAP_CatalogPickupForOrdinal( int map_index, int bsp_ordinal, const char *classname );
void GAP_BeginStaticMapping( void );
void GAP_MapStaticPickup( gentity_t *ent, int bsp_ordinal );
void GAP_MapStaticTraversal( gentity_t *ent, int bsp_ordinal );
qboolean GAP_FinishStaticMapping( void );
void GAP_TouchStaticPickup( gentity_t *ent, gentity_t *player );
void GAP_TouchTraversal( gentity_t *ent, gentity_t *player );
int GAP_FamilyForClassname( const char *classname );
qboolean GAP_FamilyUnlocked( int family_index );
qboolean GAP_PickupUnlocked( gentity_t *ent, gentity_t *player );
void GAP_ApplySpawnInventory( gentity_t *player );
int GAP_KillLocation( int map_index, int kill_number );
int GAP_NextKillLocation( void );
void GAP_RecordKill( gentity_t *victim, gentity_t *attacker );
qboolean GAP_WinningRank( int rank );
void GAP_RecordStageClear( void );

#endif
