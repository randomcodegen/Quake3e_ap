/* Shared plain-C ABI between Quake3e, native qagame, and native UI. */
#ifndef Q3AP_API_H
#define Q3AP_API_H

#include <stdint.h>

#define Q3AP_API_VERSION 8
#define Q3AP_SLOT_SCHEMA_VERSION 8
#define Q3AP_UI_MAX_MAPS 64
#define Q3AP_HOST_SIZE 256
#define Q3AP_SLOT_SIZE 128
#define Q3AP_PASSWORD_SIZE 128
#define Q3AP_STATUS_TEXT_SIZE 128
#define Q3AP_CATALOG_HASH_SIZE 65
#define Q3AP_MARKER_MAGIC 0x41504D
#define Q3AP_RESPAWN_MAGIC 0x415054

typedef enum {
    Q3AP_CONNECTION_DISCONNECTED,
    Q3AP_CONNECTION_CONNECTING,
    Q3AP_CONNECTION_AUTHENTICATED,
    Q3AP_CONNECTION_ERROR
} q3apConnectionStatus_t;

typedef enum {
    Q3AP_GOAL_STAGE_CLEARS,
    Q3AP_GOAL_QUAD_TOKEN_HUNT
} q3apGoalType_t;

typedef enum {
    Q3AP_GAME_API_VERSION,
    Q3AP_GAME_STATE_GENERATION,
    Q3AP_GAME_AUTHENTICATED,
    Q3AP_GAME_SELECTED_MAP_MASK,
    Q3AP_GAME_UNLOCKED_MAP_MASK,
    Q3AP_GAME_STARTING_MAP_INDEX,
    Q3AP_GAME_GOAL_REQUIRED,
    Q3AP_GAME_WEAPON_LOGIC_PERCENTAGE,
    Q3AP_GAME_ITEM_LOGIC_PERCENTAGE,
    Q3AP_GAME_ITEM_COUNT,
    Q3AP_GAME_LOCATION_CHECKED,
    Q3AP_GAME_ACTIVE_MAP_INDEX,
    Q3AP_GAME_LOCATION_CLASSIFICATION
} q3apGameQuery_t;

#define Q3AP_CLASSIFICATION_UNKNOWN (-1)
#define Q3AP_CLASSIFICATION_PROGRESSION 1
#define Q3AP_CLASSIFICATION_USEFUL 2
#define Q3AP_CLASSIFICATION_TRAP 4

/* One-time refills never reduce an existing over-cap stat. */
static int Q3AP_RefillStat( int current, int limit, uint32_t amount ) {
    if ( current >= limit || !amount ) return current;
    return amount >= (uint32_t)( limit - current ) ? limit : current + (int)amount;
}

typedef enum {
    Q3AP_GAME_STRING_CATALOG_HASH
} q3apGameString_t;

typedef struct {
    char host[Q3AP_HOST_SIZE];
    char slot[Q3AP_SLOT_SIZE];
    char password[Q3AP_PASSWORD_SIZE];
    int32_t port;
} apConnectRequest_t;

typedef struct {
    uint32_t api_version;
    uint32_t state_generation;
    uint32_t connection_status;
    uint32_t slot_valid;
    uint64_t selected_map_mask;
    uint64_t unlocked_map_mask;
    uint64_t cleared_map_mask;
    uint64_t in_logic_map_mask;
    int32_t starting_map_index;
    uint32_t goal_cleared;
    uint32_t goal_required;
    uint32_t goal_type;
    uint32_t weapon_logic_percentage;
    uint32_t item_logic_percentage;
    char status_text[Q3AP_STATUS_TEXT_SIZE];
    uint8_t map_checks_checked[Q3AP_UI_MAX_MAPS];
    uint8_t map_checks_total[Q3AP_UI_MAX_MAPS];
    uint8_t reserved[8];
} apUIState_t;

typedef char q3ap_connect_request_size_check[(sizeof(apConnectRequest_t) == 516) ? 1 : -1];
typedef char q3ap_ui_state_size_check[(sizeof(apUIState_t) == 336) ? 1 : -1];

#endif
