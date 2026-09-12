#include <stddef.h>
#include <string.h>

#include "../code/qcommon/q_shared.h"

/* qcommon.h has engine-only include prerequisites; this public trap is the
 * only qcommon value the ABI test needs to anchor the AP extension range. */
#define COM_TRAP_GETVALUE 700
#include "../code/game/g_public.h"
#include "../code/ui/ui_public.h"
#include "../code/ap/q3ap_api.h"
#include "../code/ap/q3ap_catalog.h"

#define ABI_ASSERT(name, expression) typedef char name[(expression) ? 1 : -1]

ABI_ASSERT(game_print_unchanged, G_PRINT == 0);
ABI_ASSERT(botlib_setup_unchanged, BOTLIB_SETUP == 200);
ABI_ASSERT(ui_error_unchanged, UI_ERROR == 0);
ABI_ASSERT(ui_floor_unchanged, UI_FLOOR == 107);
ABI_ASSERT(game_ap_syscalls, G_AP_QUERY == 710 && G_AP_GET_STRING == 711 &&
		   G_AP_SEND_LOCATION == 712 && G_AP_TAKE_FILLER == 713);
ABI_ASSERT(ui_ap_syscalls, UI_AP_GET_STATE == 710 && UI_AP_CONNECT == 711 &&
		   UI_AP_DISCONNECT == 712 && UI_AP_START_STAGE == 713);
ABI_ASSERT(connect_port_offset, offsetof(apConnectRequest_t, port) == 512);
ABI_ASSERT(ui_status_offset, offsetof(apUIState_t, status_text) == 72);
ABI_ASSERT(api_version_is_eight, Q3AP_API_VERSION == 8);
ABI_ASSERT(ui_check_counts_offset, offsetof(apUIState_t, map_checks_checked) == 200);
ABI_ASSERT(ui_map_capacity, Q3AP_UI_MAX_MAPS >= Q3AP_MAP_COUNT);

int main(void) {
	int map;
	for (map = 0; map < Q3AP_MAP_COUNT; ++map)
		if (q3ap_catalog_maps[map].pickup_count + q3ap_catalog_maps[map].traversal_count +
			q3ap_catalog_maps[map].frag_limit + 2 > Q3AP_LOCATION_MAP_STRIDE)
			return 2;
    return strlen(Q3AP_CATALOG_HASH) != 64;
}
