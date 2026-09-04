#include "ui_local.h"
#include "ui_archipelago.h"
#include "../../../code/ap/q3ap_catalog.h"

#define ID_AP_CONNECT 100
#define ID_AP_DISCONNECT 101
#define ID_AP_STAGES 102
#define ID_AP_BACK 103
#define ID_AP_LIST 200
#define ID_AP_PLAY 201
#define ID_AP_STAGE_BACK 202

typedef struct {
	menuframework_s menu;
	menutext_s banner;
	menufield_s host, port, slot, password;
	menutext_s connect, disconnect, stages, back;
	apUIState_t state;
	char localStatus[Q3AP_STATUS_TEXT_SIZE];
	qboolean openedStages;
} apConnectionMenu_t;

typedef struct {
	menuframework_s menu;
	menutext_s banner;
	menulist_s list;
	menutext_s play, back;
	apUIState_t state;
	char rows[Q3AP_MAP_COUNT][96];
	const char *rowPointers[Q3AP_MAP_COUNT + 1];
	int mapIndices[Q3AP_MAP_COUNT];
} apStageMenu_t;

static apConnectionMenu_t s_ap;
static apStageMenu_t s_stages;
static vmCvar_t ui_apHost, ui_apPort, ui_apSlot;

static void AP_TextItem( menutext_s *item, int id, int x, int y, const char *text,
	void (*callback)( void *, int ) ) {
	item->generic.type = MTYPE_PTEXT;
	item->generic.flags = QMF_CENTER_JUSTIFY | QMF_PULSEIFFOCUS;
	item->generic.x = x;
	item->generic.y = y;
	item->generic.id = id;
	item->generic.callback = callback;
	item->string = (char *)text;
	item->color = color_red;
	item->style = UI_CENTER | UI_SMALLFONT;
}

static void AP_DrawPassword( void *self ) {
	menufield_s *field = (menufield_s *)self;
	char password[MAX_EDIT_LINE];
	int index, length = strlen( field->field.buffer );
	Q_strncpyz( password, field->field.buffer, sizeof( password ) );
	for ( index = 0; index < length; ++index ) field->field.buffer[index] = '*';
	field->generic.ownerdraw = NULL;
	MenuField_Draw( field );
	field->generic.ownerdraw = AP_DrawPassword;
	Q_strncpyz( field->field.buffer, password, sizeof( field->field.buffer ) );
	memset( password, 0, sizeof( password ) );
}

static void AP_ConnectionDraw( void ) {
	menucommon_s *blocked[] = { &s_ap.host.generic, &s_ap.port.generic, &s_ap.slot.generic,
		&s_ap.password.generic, &s_ap.connect.generic, &s_ap.back.generic };
	qboolean waiting;
	int index;
	trap_AP_GetState( &s_ap.state );
	Q_strupr( s_ap.state.status_text );
	Q_strupr( s_ap.localStatus );
	waiting = s_ap.state.connection_status == Q3AP_CONNECTION_CONNECTING ||
		( s_ap.state.connection_status == Q3AP_CONNECTION_AUTHENTICATED && !s_ap.state.slot_valid );
	for ( index = 0; index < (int)( sizeof( blocked ) / sizeof( blocked[0] ) ); ++index ) {
		if ( waiting ) blocked[index]->flags |= QMF_GRAYED;
		else blocked[index]->flags &= ~QMF_GRAYED;
	}
	if ( s_ap.state.slot_valid && !s_ap.openedStages ) {
		s_ap.openedStages = qtrue;
		UI_ArchipelagoStagesMenu();
		return;
	}
	if ( s_ap.state.slot_valid ) s_ap.stages.generic.flags &= ~QMF_GRAYED;
	else s_ap.stages.generic.flags |= QMF_GRAYED;
	Menu_Draw( &s_ap.menu );
	UI_DrawString( 320, 350, s_ap.localStatus[0] ? s_ap.localStatus : s_ap.state.status_text,
		UI_CENTER | UI_SMALLFONT, menu_text_color );
}

static void AP_ConnectionEvent( void *ptr, int event ) {
	apConnectRequest_t request;
	int port;
	if ( event != QM_ACTIVATED ) return;
	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_AP_CONNECT:
		port = atoi( s_ap.port.field.buffer );
		if ( !s_ap.host.field.buffer[0] || !s_ap.slot.field.buffer[0] || port < 1 || port > 65535 ) {
			Q_strncpyz( s_ap.localStatus, "Host, port (1-65535), and slot are required", sizeof( s_ap.localStatus ) );
			return;
		}
		memset( &request, 0, sizeof( request ) );
		Q_strncpyz( request.host, s_ap.host.field.buffer, sizeof( request.host ) );
		Q_strncpyz( request.slot, s_ap.slot.field.buffer, sizeof( request.slot ) );
		Q_strncpyz( request.password, s_ap.password.field.buffer, sizeof( request.password ) );
		request.port = port;
		trap_Cvar_Set( "ui_apHost", request.host );
		trap_Cvar_Set( "ui_apPort", s_ap.port.field.buffer );
		trap_Cvar_Set( "ui_apSlot", request.slot );
		s_ap.openedStages = qfalse;
		if ( !trap_AP_Connect( &request ) )
			Q_strncpyz( s_ap.localStatus, "Connection request was rejected", sizeof( s_ap.localStatus ) );
		else s_ap.localStatus[0] = '\0';
		memset( request.password, 0, sizeof( request.password ) );
		memset( s_ap.password.field.buffer, 0, sizeof( s_ap.password.field.buffer ) );
		break;
	case ID_AP_DISCONNECT: trap_AP_Disconnect(); s_ap.localStatus[0] = '\0'; s_ap.openedStages = qfalse; break;
	case ID_AP_STAGES: if ( s_ap.state.slot_valid ) UI_ArchipelagoStagesMenu(); break;
	case ID_AP_BACK: UI_PopMenu(); break;
	}
}

static void AP_Field( menufield_s *field, const char *name, int y, int width, int maxchars, int flags ) {
	field->generic.type = MTYPE_FIELD;
	field->generic.name = name;
	field->generic.flags = QMF_PULSEIFFOCUS | QMF_SMALLFONT | flags;
	field->generic.x = 230;
	field->generic.y = y;
	field->field.widthInChars = width;
	field->field.maxchars = maxchars;
}

void UI_ArchipelagoMenu( void ) {
	memset( &s_ap, 0, sizeof( s_ap ) );
	trap_Cvar_Register( &ui_apHost, "ui_apHost", "localhost", CVAR_ARCHIVE );
	trap_Cvar_Register( &ui_apPort, "ui_apPort", "38281", CVAR_ARCHIVE );
	trap_Cvar_Register( &ui_apSlot, "ui_apSlot", "", CVAR_ARCHIVE );
	s_ap.menu.wrapAround = qtrue;
	s_ap.menu.fullscreen = qtrue;
	s_ap.menu.draw = AP_ConnectionDraw;
	s_ap.banner.generic.type = MTYPE_BTEXT;
	s_ap.banner.generic.x = 320; s_ap.banner.generic.y = 40;
	s_ap.banner.string = "ARCHIPELAGO"; s_ap.banner.color = color_white; s_ap.banner.style = UI_CENTER;
	AP_Field( &s_ap.host, "Host:", 120, 32, Q3AP_HOST_SIZE - 1, 0 );
	AP_Field( &s_ap.port, "Port:", 155, 6, 5, QMF_NUMBERSONLY );
	AP_Field( &s_ap.slot, "Slot:", 190, 24, Q3AP_SLOT_SIZE - 1, 0 );
	AP_Field( &s_ap.password, "Password:", 225, 24, Q3AP_PASSWORD_SIZE - 1, 0 );
	s_ap.password.generic.ownerdraw = AP_DrawPassword;
	AP_TextItem( &s_ap.connect, ID_AP_CONNECT, 170, 285, "CONNECT", AP_ConnectionEvent );
	AP_TextItem( &s_ap.disconnect, ID_AP_DISCONNECT, 320, 285, "DISCONNECT", AP_ConnectionEvent );
	AP_TextItem( &s_ap.stages, ID_AP_STAGES, 466, 285, "STAGES", AP_ConnectionEvent );
	AP_TextItem( &s_ap.back, ID_AP_BACK, 320, 405, "BACK", AP_ConnectionEvent );
	Menu_AddItem( &s_ap.menu, &s_ap.banner ); Menu_AddItem( &s_ap.menu, &s_ap.host );
	Menu_AddItem( &s_ap.menu, &s_ap.port ); Menu_AddItem( &s_ap.menu, &s_ap.slot );
	Menu_AddItem( &s_ap.menu, &s_ap.password ); Menu_AddItem( &s_ap.menu, &s_ap.connect );
	Menu_AddItem( &s_ap.menu, &s_ap.disconnect ); Menu_AddItem( &s_ap.menu, &s_ap.stages );
	Menu_AddItem( &s_ap.menu, &s_ap.back );
	trap_Cvar_VariableStringBuffer( "ui_apHost", s_ap.host.field.buffer, sizeof( s_ap.host.field.buffer ) );
	trap_Cvar_VariableStringBuffer( "ui_apPort", s_ap.port.field.buffer, sizeof( s_ap.port.field.buffer ) );
	trap_Cvar_VariableStringBuffer( "ui_apSlot", s_ap.slot.field.buffer, sizeof( s_ap.slot.field.buffer ) );
	UI_PushMenu( &s_ap.menu );
}

static void AP_RebuildStageRows( void ) {
	int map, row = 0;
	for ( map = 0; map < Q3AP_MAP_COUNT; ++map ) {
		const char *status;
		uint64_t bit = 1ULL << map;
		if ( !( s_stages.state.selected_map_mask & bit ) ) continue;
		status = !( s_stages.state.unlocked_map_mask & bit ) ? "Locked" :
			( s_stages.state.cleared_map_mask & bit ) ? "Cleared" :
			!( s_stages.state.in_logic_map_mask & bit ) ? "Out of Logic" : "Unlocked";
		Com_sprintf( s_stages.rows[row], sizeof( s_stages.rows[row] ), "%s (%s) - %s%s",
			q3ap_catalog_maps[map].name, q3ap_catalog_maps[map].key, status,
			map == s_stages.state.starting_map_index ? " [START]" : "" );
		s_stages.rowPointers[row] = s_stages.rows[row];
		s_stages.mapIndices[row++] = map;
	}
	s_stages.rowPointers[row] = NULL;
	s_stages.list.itemnames = s_stages.rowPointers;
	s_stages.list.numitems = row;
	if ( s_stages.list.curvalue >= row ) s_stages.list.curvalue = row ? row - 1 : 0;
}

static void AP_StageDraw( void ) {
	trap_AP_GetState( &s_stages.state );
	AP_RebuildStageRows();
	if ( !s_stages.state.slot_valid || !s_stages.list.numitems ||
		 !( s_stages.state.unlocked_map_mask & ( 1ULL << s_stages.mapIndices[s_stages.list.curvalue] ) ) )
		s_stages.play.generic.flags |= QMF_GRAYED;
	else s_stages.play.generic.flags &= ~QMF_GRAYED;
	Menu_Draw( &s_stages.menu );
	UI_DrawString( 320, 370, va( "Goal: %u / %u %s", s_stages.state.goal_cleared, s_stages.state.goal_required,
		s_stages.state.goal_type == Q3AP_GOAL_QUAD_TOKEN_HUNT ? "tokens" : "stages" ),
		UI_CENTER | UI_SMALLFONT, menu_text_color );
	if ( !s_stages.state.slot_valid ) UI_DrawString( 320, 392, s_stages.state.status_text,
		UI_CENTER | UI_SMALLFONT, menu_text_color );
}

static void AP_StageEvent( void *ptr, int event ) {
	int map;
	if ( event != QM_ACTIVATED ) return;
	if ( ((menucommon_s *)ptr)->id == ID_AP_STAGE_BACK ) { UI_PopMenu(); return; }
	if ( ((menucommon_s *)ptr)->id != ID_AP_PLAY || !s_stages.state.slot_valid || !s_stages.list.numitems ) return;
	map = s_stages.mapIndices[s_stages.list.curvalue];
	if ( s_stages.state.unlocked_map_mask & ( 1ULL << map ) ) trap_AP_StartStage( map );
}

void UI_ArchipelagoStagesMenu( void ) {
	UI_SPLevelMenu();
}
