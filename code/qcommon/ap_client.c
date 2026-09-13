/*
===========================================================================
Quake III Arena Archipelago client integration.
===========================================================================
*/

#include "q_shared.h"
#include "qcommon.h"
#include "ap_client.h"

#ifdef DEDICATED

void APCL_Init( void ) {}
void APCL_Frame( void ) {}
void APCL_Shutdown( void ) {}
int APCL_GameQuery( int selector, int argument ) { (void)selector; (void)argument; return 0; }
int APCL_TakeFiller( int itemId, int capacity ) { (void)itemId; (void)capacity; return 0; }
qboolean APCL_GameString( int selector, char *buffer, int size ) {
	(void)selector;
	if ( buffer && size > 0 ) buffer[0] = '\0';
	return qfalse;
}
qboolean APCL_SendLocation( int locationId ) { (void)locationId; return qfalse; }
qboolean APCL_CopyUIState( apUIState_t *state, int size ) {
	if ( state && size == sizeof( *state ) ) memset( state, 0, sizeof( *state ) );
	return qfalse;
}
qboolean APCL_UIConnect( const apConnectRequest_t *request, int size ) {
	(void)request; (void)size; return qfalse;
}
void APCL_UIDisconnect( void ) {}
qboolean APCL_UIStartStage( int mapIndex ) { (void)mapIndex; return qfalse; }

#else

#undef random
#include "APCc.h"
#include "../ap/q3ap_catalog.h"
#include "../client/client.h"
#include "../server/server.h"
#include "../server/sv_ap_cpma.h"
#include "ap_runtime_state.h"
#include "ap_slot_state.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define APCL_HOST_SIZE 256
#define APCL_SLOT_SIZE 128
#define APCL_PASSWORD_SIZE 128

static qboolean apcl_initialized;
static int apcl_lastStatus = -1;
static int apcl_connectStarted;
static int apcl_scoutStarted;
static qboolean apcl_slotReadyLogged;
static char apcl_host[APCL_HOST_SIZE];
static char apcl_slotName[APCL_SLOT_SIZE];
static char apcl_password[APCL_PASSWORD_SIZE];
static apclSlotState_t apcl_slot;
static apclRuntimeState_t apcl_runtime;
static GMutex apcl_fillerMutex;
static uint32_t apcl_stateGeneration;
static int apcl_activeMapIndex = -1;
static int apcl_reportedGoal = -1;
static qboolean apcl_storyComplete;
static uint64_t apcl_scoutedMapMask;
static qboolean apcl_locationDataReady;
static cvar_t *apcl_chatMessages;
static cvar_t *apcl_progressionSound;
static cvar_t *apcl_minNotify;

#ifdef _WIN32
static HANDLE apcl_serviceThread;
static volatile LONG apcl_stopService;

static DWORD WINAPI APCL_ServiceThread( LPVOID unused ) {
	(void)unused;
	while ( !InterlockedCompareExchange( &apcl_stopService, 0, 0 ) ) {
		AP_WebService();
		Sleep( 1 );
	}
	return 0;
}

static void APCL_StartServiceThread( void ) {
	if ( apcl_serviceThread ) return;
	InterlockedExchange( &apcl_stopService, 0 );
	apcl_serviceThread = CreateThread( NULL, 0, APCL_ServiceThread, NULL, 0, NULL );
	if ( !apcl_serviceThread ) {
		Com_Printf( "Archipelago: could not start network service thread\n" );
	}
}

static void APCL_StopServiceThread( void ) {
	if ( !apcl_serviceThread ) return;
	InterlockedExchange( &apcl_stopService, 1 );
	WaitForSingleObject( apcl_serviceThread, INFINITE );
	CloseHandle( apcl_serviceThread );
	apcl_serviceThread = NULL;
}
#else
static void APCL_StartServiceThread( void ) {}
static void APCL_StopServiceThread( void ) {}
#endif

static void APCL_Changed( void ) {
	++apcl_stateGeneration;
}

static qboolean APCL_Ready( void ) {
	return apcl_initialized && AP_GetConnectionStatus() == Authenticated &&
		apcl_slot.valid && apcl_locationDataReady;
}

static qboolean APCL_SessionReady( void ) {
	return apcl_initialized && apcl_slot.valid && apcl_locationDataReady;
}

static void APCL_FlushPendingLocations( void ) {
	int map, offset;
	if ( AP_GetConnectionStatus() != Authenticated ) return;
	for ( map = 0; map < Q3AP_MAP_COUNT; ++map )
		for ( offset = 1; offset <= Q3AP_CLEAR_LOCATION_OFFSET; ++offset ) {
			int location = Q3AP_LOCATION_BASE + map * Q3AP_LOCATION_MAP_STRIDE + offset;
			if ( APCL_RuntimeLocationPending( &apcl_runtime, location ) )
				AP_SendItem( (uint64_t)location );
		}
}

static void APCL_UpdateGoal( void ) {
	int progress;
	const char *unit;
	if ( !apcl_slot.valid ) return;
	if ( apcl_slot.goal_type == Q3AP_GOAL_QUAD_TOKEN_HUNT ) {
		progress = (int)APCL_RuntimeItemCount( &apcl_runtime, Q3AP_QUAD_TOKEN_ITEM_ID );
		unit = "tokens";
	} else {
		progress = APCL_RuntimeGoalProgress( &apcl_runtime, apcl_slot.selected_map_mask );
		unit = "stages";
	}
	if ( progress != apcl_reportedGoal ) {
		Com_Printf( "Archipelago: goal progress %i/%i %s\n", progress, apcl_slot.goal_required, unit );
		apcl_reportedGoal = progress;
	}
	if ( progress >= apcl_slot.goal_required && !apcl_storyComplete && apcl_initialized &&
		 AP_GetConnectionStatus() == Authenticated ) {
		AP_StoryComplete();
		apcl_storyComplete = qtrue;
		Com_Printf( "Archipelago: goal complete\n" );
	}
}

static const char *APCL_StatusName( AP_ConnectionStatus status ) {
	switch ( status ) {
	case Disconnected: return "disconnected";
	case Connected: return "connected";
	case Authenticated: return "authenticated";
	case ConnectionRefused: return "connection refused";
	default: return "unknown";
	}
}

static void APCL_PrintMessage( const struct AP_Message *message ) {
	char text[2048] = "^8Archipelago:^7 ";
	guint index;
	qboolean progressionReceived = qfalse;
	qboolean showInChat;
	int itemLevel = 0;

	if ( !message->messageParts ) {
		Q_strcat( text, sizeof( text ), message->text );
	} else {
		for ( index = 0; index < message->messageParts->len; ++index ) {
			const struct AP_MessagePart *part =
				g_array_index( message->messageParts, struct AP_MessagePart *, index );
			if ( !part || !part->text ) continue;
			switch ( part->type ) {
			case AP_LocationText:
				Q_strcat( text, sizeof( text ), "^2" );
				Q_strcat( text, sizeof( text ), part->text );
				Q_strcat( text, sizeof( text ), "^7" );
				continue;
			case AP_ItemText: Q_strcat( text, sizeof( text ),
				part->flags & 1 ? "^9" : part->flags & 2 ? "^o" : part->flags & 4 ? "^8" : "^5" );
				if ( part->flags & ( 1 | 4 ) ) itemLevel = 2;
				else if ( ( part->flags & 2 ) && itemLevel < 1 ) itemLevel = 1;
				if ( message->type == ItemRecv && ( part->flags & 1 ) ) progressionReceived = qtrue;
				break;
			case AP_PlayerText: Q_strcat( text, sizeof( text ),
				!Q_stricmp( part->text, apcl_slotName ) ? "^6" : "^3" ); break;
			default: Q_strcat( text, sizeof( text ), "^7" ); break;
			}
			Q_strcat( text, sizeof( text ), part->text );
		}
	}
	if ( ( message->type == ItemSend || message->type == ItemRecv ) &&
		apcl_minNotify && apcl_minNotify->integer > itemLevel ) return;
	showInChat = apcl_chatMessages && apcl_chatMessages->integer && com_sv_running->integer &&
		cls.state == CA_ACTIVE && svs.clients && clc.clientNum >= 0 && clc.clientNum < sv.maxclients;
	if ( !showInChat ) Com_Printf( "%s^7\n", text );
	else {
		char chat[900];
		char *cursor;
		Cvar_Set( "cg_nochatbeep", "1" );
		Q_strncpyz( chat, text, sizeof( chat ) );
		for ( cursor = chat; *cursor; ++cursor )
			if ( *cursor == '"' || *cursor == '\n' || *cursor == '\r' ) *cursor = *cursor == '"' ? '\'' : ' ';
		SV_SendServerCommand( svs.clients + clc.clientNum, "chat \"%s^7\"", chat );
	}
	if ( progressionReceived && apcl_progressionSound && apcl_progressionSound->integer )
		S_StartLocalSound( S_RegisterSound( "sound/misc/menu2.wav", qfalse ), CHAN_LOCAL_SOUND );
}

static void APCL_ItemClear( void ) {
	if ( APCL_RuntimeClearItems( &apcl_runtime ) ) APCL_Changed();
}
static void APCL_ItemReceived( uint64_t item, int player, bool notify ) {
	(void)player;
	/* APCc's notify flag excludes already-seen items during history replay. */
	if ( notify ) {
		int queued;
		g_mutex_lock( &apcl_fillerMutex );
		queued = APCL_RuntimeQueueFiller( &apcl_runtime, item );
		g_mutex_unlock( &apcl_fillerMutex );
		if ( queued ) APCL_Changed();
	}
	if ( APCL_RuntimeReceiveItem( &apcl_runtime, item ) ) {
		APCL_Changed();
		APCL_UpdateGoal();
	}
}
static void APCL_LocationChecked( uint64_t location ) {
	if ( APCL_RuntimeCheckLocation( &apcl_runtime, location ) ) {
		APCL_Changed();
		APCL_UpdateGoal();
	}
}

static void APCL_LocationInfo( GArray *locations ) {
	guint index;
	guint count = locations->len;
	int changed = 0;
	for ( index = 0; index < locations->len; ++index ) {
		struct AP_NetworkItem *item = g_array_index( locations, struct AP_NetworkItem *, index );
		changed |= APCL_RuntimeSetLocationClassification( &apcl_runtime,
			(int)item->location, item->flags );
		AP_NetworkItem_free( item );
	}
	g_array_free( locations, TRUE );
	apcl_locationDataReady = APCL_SlotLocationsReady( &apcl_slot, &apcl_runtime );
	if ( Cvar_VariableIntegerValue( "ap_debug_timing" ) )
		Com_Printf( "AP timing: scout response %u locations +%ims%s\n", count,
			Sys_Milliseconds() - apcl_scoutStarted, apcl_locationDataReady ? " (ready)" : "" );
	if ( changed ) APCL_Changed();
}

static void APCL_RequestLocationScouts( void ) {
	uint64_t maps;
	GArray *locations;
	int map, pickup;
	if ( !apcl_slot.valid ) return;
	maps = apcl_slot.selected_map_mask & ~apcl_scoutedMapMask;
	if ( !maps ) return;
	locations = g_array_new( FALSE, FALSE, sizeof( uint64_t ) );
	for ( map = 0; map < Q3AP_MAP_COUNT; ++map ) {
		int previousLocation = -1;
		if ( !( maps & ( 1ULL << map ) ) ) continue;
		for ( pickup = 0; pickup < q3ap_catalog_maps[map].pickup_count; ++pickup ) {
			uint64_t location = (uint64_t)q3ap_catalog_maps[map].pickups[pickup].location_id;
			if ( (int)location == previousLocation ) continue;
			previousLocation = (int)location;
			if ( !APCL_SlotPickupIncluded( &apcl_slot, (int)location ) ) continue;
			g_array_append_val( locations, location );
		}
		for ( pickup = 0; pickup < q3ap_catalog_maps[map].traversal_count; ++pickup ) {
			uint64_t location = (uint64_t)q3ap_catalog_maps[map].traversals[pickup].location_id;
			if ( !APCL_SlotPickupIncluded( &apcl_slot, (int)location ) ) continue;
			g_array_append_val( locations, location );
		}
	}
	apcl_scoutedMapMask |= maps;
	apcl_scoutStarted = Sys_Milliseconds();
	if ( Cvar_VariableIntegerValue( "ap_debug_timing" ) )
		Com_Printf( "AP timing: requesting %u location scouts +%ims\n",
			locations->len, apcl_scoutStarted - apcl_connectStarted );
	AP_SendLocationScouts( locations, 0 );
	g_array_free( locations, TRUE );
}

static void APCL_SlotChanged( int changed ) {
	if ( changed ) {
		APCL_SlotValidateRuntime( &apcl_slot,
			!Q_stricmp( Cvar_VariableString( "fs_game" ), "cpma-ap" ) );
		apcl_locationDataReady = APCL_SlotLocationsReady( &apcl_slot, &apcl_runtime );
		if ( apcl_slot.valid && !apcl_slotReadyLogged ) {
			apcl_slotReadyLogged = qtrue;
			if ( Cvar_VariableIntegerValue( "ap_debug_timing" ) )
				Com_Printf( "AP timing: slot data valid +%ims\n",
					Sys_Milliseconds() - apcl_connectStarted );
		}
		APCL_Changed();
		APCL_UpdateGoal();
	}
}

static void APCL_SlotSchema( uint64_t value ) {
	APCL_SlotChanged( APCL_SlotSetInteger( &apcl_slot, APCL_SLOT_SCHEMA_VERSION, value ) );
}

static void APCL_SlotCPMA( uint64_t value ) {
	APCL_SlotChanged( APCL_SlotSetInteger( &apcl_slot, APCL_SLOT_CPMA, value ) );
}

static void APCL_SlotGoal( uint64_t value ) {
	APCL_SlotChanged( APCL_SlotSetInteger( &apcl_slot, APCL_SLOT_GOAL_REQUIRED, value ) );
}

static void APCL_SlotGoalType( uint64_t value ) {
	APCL_SlotChanged( APCL_SlotSetInteger( &apcl_slot, APCL_SLOT_GOAL_TYPE, value ) );
}

static void APCL_SlotKillIncrement( uint64_t value ) {
	APCL_SlotChanged( APCL_SlotSetInteger( &apcl_slot, APCL_SLOT_KILL_CHECK_INCREMENT, value ) );
}

static void APCL_SlotWeaponLogic( uint64_t value ) {
	APCL_SlotChanged( APCL_SlotSetInteger( &apcl_slot, APCL_SLOT_WEAPON_LOGIC_PERCENTAGE, value ) );
}

static void APCL_SlotItemLogic( uint64_t value ) {
	APCL_SlotChanged( APCL_SlotSetInteger( &apcl_slot, APCL_SLOT_ITEM_LOGIC_PERCENTAGE, value ) );
}

static void APCL_SlotCatalogHash( json_t *value ) {
	APCL_SlotChanged( APCL_SlotSetCatalogHash( &apcl_slot,
		json_is_string( value ) ? json_string_value( value ) : "" ) );
}

static void APCL_SlotStartingMap( json_t *value ) {
	APCL_SlotChanged( APCL_SlotSetStartingMap( &apcl_slot,
		json_is_string( value ) ? json_string_value( value ) : "" ) );
}

static void APCL_SlotSelectedMaps( json_t *value ) {
	const char *maps[Q3AP_MAP_COUNT];
	size_t index;
	json_t *entry;
	int count = 0;
	if ( json_is_array( value ) && json_array_size( value ) <= Q3AP_MAP_COUNT ) {
		json_array_foreach( value, index, entry ) {
			maps[count++] = json_is_string( entry ) ? json_string_value( entry ) : "";
		}
	} else {
		maps[count++] = "";
	}
	APCL_SlotChanged( APCL_SlotSetSelectedMaps( &apcl_slot, maps, count ) );
}

static void APCL_SlotPickupLocations( json_t *value ) {
	int locations[Q3AP_MAP_COUNT * Q3AP_LOCATION_MAP_STRIDE];
	size_t index;
	json_t *entry;
	int count = 0;
	if ( json_is_array( value ) && json_array_size( value ) <= Q3AP_MAP_COUNT * Q3AP_LOCATION_MAP_STRIDE ) {
		json_array_foreach( value, index, entry )
			locations[count++] = json_is_integer( entry ) ? (int)json_integer_value( entry ) : 0;
	} else {
		locations[count++] = 0;
	}
	APCL_SlotChanged( APCL_SlotSetPickupLocations( &apcl_slot, locations, count ) );
}

static void APCL_RegisterCallbacks( void ) {
	AP_SetItemClearCallback( APCL_ItemClear );
	AP_SetItemRecvCallback( APCL_ItemReceived );
	AP_SetLocationCheckedCallback( APCL_LocationChecked );
	AP_SetLocationInfoCallback( APCL_LocationInfo );
	AP_RegisterSlotDataIntCallback( "schema_version", APCL_SlotSchema );
	AP_RegisterSlotDataIntCallback( "cpma", APCL_SlotCPMA );
	AP_RegisterSlotDataRawCallback( "catalog_hash", APCL_SlotCatalogHash );
	AP_RegisterSlotDataRawCallback( "selected_maps", APCL_SlotSelectedMaps );
	AP_RegisterSlotDataRawCallback( "pickup_locations", APCL_SlotPickupLocations );
	AP_RegisterSlotDataRawCallback( "starting_map", APCL_SlotStartingMap );
	AP_RegisterSlotDataIntCallback( "goal_type", APCL_SlotGoalType );
	AP_RegisterSlotDataIntCallback( "goal_required", APCL_SlotGoal );
	AP_RegisterSlotDataIntCallback( "kill_check_increment", APCL_SlotKillIncrement );
	AP_RegisterSlotDataIntCallback( "weapon_logic_percentage", APCL_SlotWeaponLogic );
	AP_RegisterSlotDataIntCallback( "item_logic_percentage", APCL_SlotItemLogic );
}

static void APCL_Disconnect( void ) {
	if ( apcl_initialized ) {
		AP_Shutdown();
		apcl_initialized = qfalse;
	}
	APCL_StopServiceThread();
	apcl_lastStatus = -1;
	apcl_connectStarted = 0;
	apcl_activeMapIndex = -1;
	apcl_reportedGoal = -1;
	apcl_storyComplete = qfalse;
	apcl_scoutedMapMask = 0;
	apcl_locationDataReady = qfalse;
	APCL_SlotStateInit( &apcl_slot );
	APCL_RuntimeInit( &apcl_runtime );
	APCL_Changed();
	memset( apcl_password, 0, sizeof( apcl_password ) );
	Com_Printf( "Archipelago: disconnected\n" );
}

static void APCL_Disconnect_f( void ) {
	APCL_Disconnect();
}

static void APCL_Status_f( void ) {
	if ( !apcl_initialized ) {
		Com_Printf( "Archipelago: disconnected\n" );
		return;
	}
	if ( AP_GetConnectionStatus() == Authenticated && !apcl_slot.valid ) {
		Com_Printf( "Archipelago: connected, invalid slot data: %s\n", apcl_slot.error );
		return;
	}
	Com_Printf( "Archipelago: %s%s\n", APCL_StatusName( AP_GetConnectionStatus() ),
		apcl_slot.valid ? ", slot data valid" : "" );
}

static void APCL_Say_f( void ) {
	if ( Cmd_Argc() < 2 ) { Com_Printf( "usage: ap_say <message>\n" ); return; }
	if ( !apcl_initialized || AP_GetConnectionStatus() != Authenticated ) {
		Com_Printf( "Archipelago: authenticate before sending chat\n" );
		return;
	}
	AP_SendMsg( Cmd_ArgsFrom( 1 ) );
}

static void APCL_CPMAStageLimits_f( void ) {
	const q3ap_catalog_map_t *map;
	if ( sv.state != SS_GAME || Q_stricmp( Cvar_VariableString( "fs_game" ), "cpma-ap" ) ) return;
	map = Q3AP_CatalogMapByKey( Cvar_VariableString( "mapname" ) );
	if ( !map ) return;
	if ( !SVAP_CPMA_ApplyStageLimits( map->game_type, map->frag_limit ) )
		Com_Printf( "Archipelago: could not apply CPMA stage limits (unsupported game VM or rejected command)\n" );
}

static qboolean APCL_StartStage( int mapIndex ) {
	const q3ap_catalog_map_t *map;
	char command[2048];
	char bots[256];
	char *bot;
	int skill, spread, maxSkill;
	if ( !APCL_Ready() ) {
		Com_Printf( "Archipelago: wait for connection and location synchronization before starting a stage\n" );
		return qfalse;
	}
	if ( !APCL_RuntimeCanStartStage( &apcl_runtime, apcl_slot.selected_map_mask, mapIndex ) ) {
		Com_Printf( "Archipelago: that stage is locked or not selected\n" );
		return qfalse;
	}
	map = &q3ap_catalog_maps[mapIndex];
	skill = Cvar_VariableIntegerValue( "g_spSkill" );
	if ( skill < 1 ) skill = 4;
	spread = (int)Com_Clamp( 0, 94, Cvar_VariableIntegerValue( "cg_skillspread" ) );
	maxSkill = (int)Com_Clamp( 6, 100, Cvar_VariableIntegerValue( "cg_maxskill" ) );
	Com_sprintf( command, sizeof( command ),
		"set server_useMapModes 0\nset mode_start %s\nset g_gametype %i\nset g_warmup 0\nset fraglimit %i\nset timelimit 0\nmap %s\nfraglimit %i\ntimelimit 0\n",
		map->game_type == 1 ? "1v1" : "ffa", map->game_type, map->frag_limit, map->key, map->frag_limit );
	if ( !Q_stricmp( Cvar_VariableString( "fs_game" ), "cpma-ap" ) )
		Q_strcat( command, sizeof( command ), "ap_cpma_stage_limits\n" );
	Q_strncpyz( bots, map->bots, sizeof( bots ) );
	bot = strtok( bots, " " );
	while ( bot ) {
		int botSkill = skill < 6 ? skill : (int)Com_Clamp( 6, maxSkill,
			skill + ( spread ? rand() % ( spread * 2 + 1 ) - spread : 0 ) );
		Q_strcat( command, sizeof( command ), va( "addbot %s %i\n", bot, botSkill ) );
		bot = strtok( NULL, " " );
	}
	Q_strcat( command, sizeof( command ), "team free\n" );
	apcl_activeMapIndex = mapIndex;
	APCL_Changed();
	Cbuf_ExecuteText( EXEC_INSERT, command );
	Com_Printf( "Archipelago: starting %s (%s)\n", map->name, map->key );
	return qtrue;
}

static void APCL_StartStage_f( void ) {
	const q3ap_catalog_map_t *map;
	if ( Cmd_Argc() != 2 ) { Com_Printf( "usage: ap_start_stage <mapkey>\n" ); return; }
	map = Q3AP_CatalogMapByKey( Cmd_Argv( 1 ) );
	if ( !map ) { Com_Printf( "Archipelago: unknown map key\n" ); return; }
	APCL_StartStage( map->map_index );
}

static void APCL_Maps_f( void ) {
	int index;
	if ( !apcl_slot.valid ) { Com_Printf( "Archipelago: no valid slot data\n" ); return; }
	for ( index = 0; index < Q3AP_MAP_COUNT; ++index ) {
		if ( !( apcl_slot.selected_map_mask & ( 1ULL << index ) ) ) continue;
		Com_Printf( "%-12s %s%s\n", q3ap_catalog_maps[index].key,
			APCL_RuntimeCanStartStage( &apcl_runtime, apcl_slot.selected_map_mask, index ) ? "unlocked" : "locked",
			APCL_RuntimeLocationChecked( &apcl_runtime, q3ap_catalog_maps[index].clear_location_id ) ? ", cleared" : "" );
	}
}

static qboolean APCL_Connect( const char *host, int port, const char *slot, const char *password ) {
	struct AP_NetworkVersion version = { 0, 6, 8 };
	int initStarted;
	if ( !host[0] || !slot[0] || port < 1 || port > 65535 ) return qfalse;
	if ( apcl_initialized ) APCL_Disconnect();
	Q_strncpyz( apcl_host, !Q_stricmp( host, "localhost" ) ? "127.0.0.1" : host, sizeof( apcl_host ) );
	Q_strncpyz( apcl_slotName, slot, sizeof( apcl_slotName ) );
	Q_strncpyz( apcl_password, password, sizeof( apcl_password ) );
	APCL_SlotStateInit( &apcl_slot );
	APCL_RuntimeInit( &apcl_runtime );
	apcl_scoutedMapMask = 0;
	apcl_scoutStarted = 0;
	apcl_slotReadyLogged = qfalse;
	apcl_locationDataReady = qfalse;
	apcl_connectStarted = initStarted = Sys_Milliseconds();
	AP_SetClientVersion( &version );
	AP_Init( apcl_host, port, "Quake III Arena", apcl_slotName, apcl_password );
	if ( Cvar_VariableIntegerValue( "ap_debug_timing" ) )
		Com_Printf( "AP timing: AP_Init %ims\n", Sys_Milliseconds() - initStarted );
	APCL_RegisterCallbacks();
	AP_Start();
	APCL_StartServiceThread();
	apcl_initialized = qtrue;
	apcl_lastStatus = -1;
	APCL_Changed();
	Com_Printf( "Archipelago: connecting to %s:%i as %s\n", apcl_host, port, apcl_slotName );
	return qtrue;
}

static void APCL_Connect_f( void ) {
	long port;
	char *end;

	if ( Cmd_Argc() != 4 ) {
		Com_Printf( "usage: ap_connect <host> <port> <slot>\n" );
		return;
	}

	port = strtol( Cmd_Argv( 2 ), &end, 10 );
	if ( *end || port < 1 || port > 65535 ) {
		Com_Printf( "Archipelago: port must be between 1 and 65535\n" );
		return;
	}

	APCL_Connect( Cmd_Argv( 1 ), (int)port, Cmd_Argv( 3 ), "" );
}

void APCL_Init( void ) {
	Cvar_Get( "cg_skillspread", "10", CVAR_ARCHIVE );
	Cvar_Get( "cg_maxskill", "100", CVAR_ARCHIVE );
	apcl_chatMessages = Cvar_Get( "ap_chat_messages", "0", CVAR_ARCHIVE );
	Cvar_SetDescription( apcl_chatMessages, "Also show Archipelago messages in the in-game chat area." );
	apcl_progressionSound = Cvar_Get( "ap_progression_sound", "1", CVAR_ARCHIVE );
	Cvar_SetDescription( apcl_progressionSound, "Play a sound when a progression item is received." );
	apcl_minNotify = Cvar_Get( "ap_minnotify", "0", CVAR_ARCHIVE );
	Cvar_CheckRange( apcl_minNotify, "0", "2", CV_INTEGER );
	Cvar_SetDescription( apcl_minNotify, "Minimum AP item notification: 0 all, 1 non-filler, 2 progression/traps." );
	Cvar_SetDescription( Cvar_Get( "ap_debug_timing", "0", 0 ),
		"Log Archipelago connection, slot-data, and location-scout timings." );
	if ( !Q_stricmp( Cvar_VariableString( "fs_game" ), "q3ap" ) ) {
		Cvar_Set( "vm_ui", "0" );
		Cvar_Set( "vm_game", "0" );
	}
	APCL_SlotStateInit( &apcl_slot );
	APCL_RuntimeInit( &apcl_runtime );
	Cmd_AddCommand( "ap_connect", APCL_Connect_f );
	Cmd_AddCommand( "ap_disconnect", APCL_Disconnect_f );
	Cmd_AddCommand( "ap_status", APCL_Status_f );
	Cmd_AddCommand( "ap_say", APCL_Say_f );
	Cmd_AddCommand( "ap_start_stage", APCL_StartStage_f );
	Cmd_AddCommand( "ap_cpma_stage_limits", APCL_CPMAStageLimits_f );
	Cmd_AddCommand( "ap_maps", APCL_Maps_f );
}

void APCL_Frame( void ) {
	AP_ConnectionStatus status;
	int messages = 0;

	if ( !apcl_initialized ) {
		return;
	}

	#ifdef _WIN32
	if ( !apcl_serviceThread ) AP_WebService();
	#else
	AP_WebService();
	#endif
	while ( messages++ < 32 && AP_IsMessagePending() ) {
		const struct AP_Message *message = (const struct AP_Message *)AP_GetLatestMessage();
		if ( message && message->text ) APCL_PrintMessage( message );
		AP_ClearLatestMessage();
	}
	status = AP_GetConnectionStatus();
	if ( (int)status != apcl_lastStatus ) {
		Com_Printf( "Archipelago: %s\n", APCL_StatusName( status ) );
		if ( Cvar_VariableIntegerValue( "ap_debug_timing" ) )
			Com_Printf( "AP timing: status %s +%ims\n", APCL_StatusName( status ),
				Sys_Milliseconds() - apcl_connectStarted );
		apcl_lastStatus = (int)status;
		APCL_Changed();
		if ( status == Authenticated ) {
			memset( apcl_password, 0, sizeof( apcl_password ) );
			APCL_FlushPendingLocations();
			APCL_UpdateGoal();
			APCL_StartServiceThread();
		}
	}
	if ( status == Authenticated ) APCL_RequestLocationScouts();
}

void APCL_Shutdown( void ) {
	Cmd_RemoveCommand( "ap_connect" );
	Cmd_RemoveCommand( "ap_disconnect" );
	Cmd_RemoveCommand( "ap_status" );
	Cmd_RemoveCommand( "ap_say" );
	Cmd_RemoveCommand( "ap_start_stage" );
	Cmd_RemoveCommand( "ap_cpma_stage_limits" );
	Cmd_RemoveCommand( "ap_maps" );
	if ( apcl_initialized ) {
		APCL_Disconnect();
	} else {
		memset( apcl_password, 0, sizeof( apcl_password ) );
	}
}

int APCL_GameQuery( int selector, int argument ) {
	qboolean authenticated = APCL_SessionReady();
	switch ( selector ) {
	case Q3AP_GAME_API_VERSION: return Q3AP_API_VERSION;
	case Q3AP_GAME_STATE_GENERATION: return (int)apcl_stateGeneration;
	case Q3AP_GAME_AUTHENTICATED: return authenticated;
	case Q3AP_GAME_SELECTED_MAP_MASK: return authenticated && argument >= 0 && argument < 2 ?
		(int)(uint32_t)( apcl_slot.selected_map_mask >> ( argument * 32 ) ) : 0;
	case Q3AP_GAME_UNLOCKED_MAP_MASK: return authenticated && argument >= 0 && argument < 2 ?
		(int)(uint32_t)( APCL_RuntimeUnlockedMaps( &apcl_runtime, apcl_slot.selected_map_mask ) >>
		( argument * 32 ) ) : 0;
	case Q3AP_GAME_STARTING_MAP_INDEX: return authenticated ? apcl_slot.starting_map_index : -1;
	case Q3AP_GAME_GOAL_REQUIRED: return authenticated ? apcl_slot.goal_required : 0;
	case Q3AP_GAME_WEAPON_LOGIC_PERCENTAGE: return authenticated ? apcl_slot.weapon_logic_percentage : 0;
	case Q3AP_GAME_ITEM_LOGIC_PERCENTAGE: return authenticated ? apcl_slot.item_logic_percentage : 0;
	case Q3AP_GAME_ITEM_COUNT: return authenticated ? (int)APCL_RuntimeItemCount( &apcl_runtime, argument ) : 0;
	case Q3AP_GAME_LOCATION_CHECKED: return authenticated ? APCL_RuntimeLocationChecked( &apcl_runtime, argument ) : 0;
	case Q3AP_GAME_ACTIVE_MAP_INDEX: return authenticated ? apcl_activeMapIndex : -1;
	case Q3AP_GAME_LOCATION_CLASSIFICATION: return authenticated ?
		APCL_RuntimeLocationClassification( &apcl_runtime, argument ) : Q3AP_CLASSIFICATION_UNKNOWN;
	default: return 0;
	}
}

int APCL_TakeFiller( int itemId, int capacity ) {
	int amount;
	if ( !APCL_SessionReady() || sv.state != SS_GAME || apcl_activeMapIndex < 0 || capacity <= 0 ) return 0;
	g_mutex_lock( &apcl_fillerMutex );
	amount = (int)APCL_RuntimeTakeFiller( &apcl_runtime, itemId, capacity );
	g_mutex_unlock( &apcl_fillerMutex );
	return amount;
}

qboolean APCL_GameString( int selector, char *buffer, int size ) {
	if ( !buffer || size < 1 ) return qfalse;
	buffer[0] = '\0';
	if ( selector != Q3AP_GAME_STRING_CATALOG_HASH ) return qfalse;
	Q_strncpyz( buffer, Q3AP_CATALOG_HASH, size );
	return qtrue;
}

qboolean APCL_SendLocation( int locationId ) {
	int mapIndex;
	if ( !APCL_SessionReady() || !APCL_RuntimeLocationKnown( locationId ) ) return qfalse;
	mapIndex = ( locationId - Q3AP_LOCATION_BASE ) / Q3AP_LOCATION_MAP_STRIDE;
	if ( !( apcl_slot.selected_map_mask & ( 1ULL << mapIndex ) ) ) return qfalse;
	if ( ( Q3AP_CatalogPickupByLocation( locationId ) || Q3AP_CatalogTraversalByLocation( locationId ) ) &&
		 !APCL_SlotPickupIncluded( &apcl_slot, locationId ) )
		return qfalse;
	if ( APCL_RuntimeLocationChecked( &apcl_runtime, locationId ) ) return qtrue;
	if ( APCL_RuntimeLocationPending( &apcl_runtime, locationId ) ) return qtrue;
	APCL_RuntimeQueueLocation( &apcl_runtime, (uint64_t)locationId );
	if ( AP_GetConnectionStatus() == Authenticated ) AP_SendItem( (uint64_t)locationId );
	return qtrue;
}

qboolean APCL_CopyUIState( apUIState_t *state, int size ) {
	AP_ConnectionStatus status;
	int map, pickup, kill;
	if ( !state || size != sizeof( *state ) ) return qfalse;
	memset( state, 0, sizeof( *state ) );
	state->api_version = Q3AP_API_VERSION;
	state->state_generation = apcl_stateGeneration;
	state->starting_map_index = -1;
	if ( !apcl_initialized ) {
		state->connection_status = Q3AP_CONNECTION_DISCONNECTED;
		return qtrue;
	}
	status = AP_GetConnectionStatus();
	state->connection_status = status == Authenticated ? Q3AP_CONNECTION_AUTHENTICATED :
		status == ConnectionRefused ? Q3AP_CONNECTION_ERROR :
		status == Disconnected && Sys_Milliseconds() - apcl_connectStarted >= 10000 ?
		Q3AP_CONNECTION_DISCONNECTED : Q3AP_CONNECTION_CONNECTING;
	if ( status == Authenticated ) {
		state->slot_valid = APCL_Ready();
		if ( state->slot_valid ) {
			state->selected_map_mask = apcl_slot.selected_map_mask;
			state->unlocked_map_mask = APCL_RuntimeUnlockedMaps( &apcl_runtime, apcl_slot.selected_map_mask );
			state->cleared_map_mask = APCL_RuntimeClearedMaps( &apcl_runtime, apcl_slot.selected_map_mask );
			state->in_logic_map_mask = APCL_RuntimeInLogicMaps( &apcl_runtime, apcl_slot.selected_map_mask,
				apcl_slot.weapon_logic_percentage, apcl_slot.item_logic_percentage );
			state->starting_map_index = apcl_slot.starting_map_index;
			state->goal_type = apcl_slot.goal_type;
			if ( state->goal_type == Q3AP_GOAL_QUAD_TOKEN_HUNT )
				state->goal_cleared = APCL_RuntimeItemCount( &apcl_runtime, Q3AP_QUAD_TOKEN_ITEM_ID );
			else {
				uint64_t cleared = state->cleared_map_mask;
				while ( cleared ) { state->goal_cleared += (uint32_t)( cleared & 1ULL ); cleared >>= 1; }
			}
			state->goal_required = apcl_slot.goal_required;
			state->weapon_logic_percentage = apcl_slot.weapon_logic_percentage;
			state->item_logic_percentage = apcl_slot.item_logic_percentage;
			for ( map = 0; map < Q3AP_MAP_COUNT; ++map ) {
				const q3ap_catalog_map_t *catalogMap = &q3ap_catalog_maps[map];
				int previousLocation = -1;
				state->map_checks_total[map] = catalogMap->frag_limit / apcl_slot.kill_check_increment +
					!!catalogMap->powerup_frag_location_id + 1;
				for ( pickup = 0; pickup < catalogMap->pickup_count; ++pickup ) {
					int location = catalogMap->pickups[pickup].location_id;
					if ( location == previousLocation ) continue;
					previousLocation = location;
					if ( !APCL_SlotPickupIncluded( &apcl_slot, location ) ) continue;
					++state->map_checks_total[map];
					state->map_checks_checked[map] += APCL_RuntimeLocationChecked( &apcl_runtime, location ) ? 1 : 0;
				}
				for ( kill = apcl_slot.kill_check_increment; kill <= catalogMap->frag_limit;
					 kill += apcl_slot.kill_check_increment )
					state->map_checks_checked[map] += APCL_RuntimeLocationChecked( &apcl_runtime,
						Q3AP_LOCATION_BASE + map * Q3AP_LOCATION_MAP_STRIDE + Q3AP_KILL_LOCATION_OFFSET + kill ) ? 1 : 0;
				for ( pickup = 0; pickup < catalogMap->traversal_count; ++pickup ) {
					int location = catalogMap->traversals[pickup].location_id;
					if ( !APCL_SlotPickupIncluded( &apcl_slot, location ) ) continue;
					++state->map_checks_total[map];
					state->map_checks_checked[map] += APCL_RuntimeLocationChecked( &apcl_runtime, location ) ? 1 : 0;
				}
				if ( catalogMap->powerup_frag_location_id )
					state->map_checks_checked[map] += APCL_RuntimeLocationChecked( &apcl_runtime,
						catalogMap->powerup_frag_location_id ) ? 1 : 0;
				state->map_checks_checked[map] += APCL_RuntimeLocationChecked( &apcl_runtime,
					catalogMap->clear_location_id ) ? 1 : 0;
			}
			Q_strncpyz( state->status_text, "Authenticated", sizeof( state->status_text ) );
		} else {
			Q_strncpyz( state->status_text, apcl_slot.valid ? "Synchronizing location data" :
				( apcl_slot.error[0] ? apcl_slot.error : "Waiting for slot data" ),
				sizeof( state->status_text ) );
		}
	} else {
		Q_strncpyz( state->status_text,
			state->connection_status == Q3AP_CONNECTION_CONNECTING ? "Connecting" : APCL_StatusName( status ),
			sizeof( state->status_text ) );
	}
	return qtrue;
}

qboolean APCL_UIConnect( const apConnectRequest_t *request, int size ) {
	apConnectRequest_t copy;
	qboolean result;
	if ( !request || size != sizeof( *request ) ) return qfalse;
	memcpy( &copy, request, sizeof( copy ) );
	copy.host[sizeof( copy.host ) - 1] = '\0';
	copy.slot[sizeof( copy.slot ) - 1] = '\0';
	copy.password[sizeof( copy.password ) - 1] = '\0';
	result = APCL_Connect( copy.host, copy.port, copy.slot, copy.password );
	memset( copy.password, 0, sizeof( copy.password ) );
	return result;
}

void APCL_UIDisconnect( void ) {
	APCL_Disconnect();
}

qboolean APCL_UIStartStage( int mapIndex ) {
	return APCL_StartStage( mapIndex );
}

#endif
