/* Archipelago stage browser, reusing Quake III's levelshot/enemy layout. */
#include "ui_local.h"
#include "../../../code/ap/q3ap_catalog.h"

#define ART_LEVELFRAME_FOCUS    "menu/art/maps_select"
#define ART_LEVELFRAME_SELECTED "menu/art/maps_selected"
#define ART_ARROW               "menu/art/narrow_0"
#define ART_ARROW_FOCUS         "menu/art/narrow_1"
#define ART_MAP_UNKNOWN         "menu/art/unknownmap"
#define ART_MAP_COMPLETE        "menu/art/level_complete1"
#define ART_BACK0               "menu/art/back_0"
#define ART_BACK1               "menu/art/back_1"
#define ART_FIGHT0              "menu/art/fight_0"
#define ART_FIGHT1              "menu/art/fight_1"

#define ID_LEFTARROW 10
#define ID_PICTURE0  11
#define ID_RIGHTARROW 15
#define ID_BACK       23
#define ID_NEXT       26
#define MAPS_PER_PAGE 4

typedef struct {
	menuframework_s menu;
	menutext_s banner;
	menubitmap_s leftArrow, maps[MAPS_PER_PAGE], rightArrow, back, fight, nullItem;
	apUIState_t state;
	int mapIndices[Q3AP_MAP_COUNT];
	int mapCount, page, selected, pageMapCount;
	char levelPicNames[MAPS_PER_PAGE][MAX_QPATH];
	char levelNames[MAPS_PER_PAGE][16];
	qhandle_t selectedPic, focusPic, completePic;
	int numBots;
	qhandle_t botPics[7];
	char botNames[7][16];
} apLevelMenu_t;

static apLevelMenu_t s_levels;

static void AP_PlayerIcon( const char *modelAndSkin, char *iconName, int size ) {
	char model[MAX_QPATH], *skin;
	Q_strncpyz( model, modelAndSkin, sizeof( model ) );
	skin = Q_strrchr( model, '/' );
	if ( skin ) *skin++ = '\0'; else skin = "default";
	Com_sprintf( iconName, size, "models/players/%s/icon_%s.tga", model, skin );
	if ( !trap_R_RegisterShaderNoMip( iconName ) && Q_stricmp( skin, "default" ) )
		Com_sprintf( iconName, size, "models/players/%s/icon_default.tga", model );
}

static int AP_SelectedMap( void ) {
	int index = s_levels.page * MAPS_PER_PAGE + s_levels.selected;
	return index >= 0 && index < s_levels.mapCount ? s_levels.mapIndices[index] : -1;
}

static void AP_SetBots( void ) {
	char bots[MAX_INFO_STRING], *p, *bot, *botInfo, icon[MAX_QPATH];
	int map = AP_SelectedMap();
	s_levels.numBots = 0;
	if ( map < 0 ) return;
	Q_strncpyz( bots, q3ap_catalog_maps[map].bots, sizeof( bots ) );
	for ( p = bots; *p && s_levels.numBots < 7; ) {
		while ( *p == ' ' ) ++p;
		if ( !*p ) break;
		bot = p;
		while ( *p && *p != ' ' ) ++p;
		if ( *p ) *p++ = '\0';
		botInfo = UI_GetBotInfoByName( bot );
		if ( botInfo ) {
			AP_PlayerIcon( Info_ValueForKey( botInfo, "model" ), icon, sizeof( icon ) );
			s_levels.botPics[s_levels.numBots] = trap_R_RegisterShaderNoMip( icon );
			Q_strncpyz( s_levels.botNames[s_levels.numBots], Info_ValueForKey( botInfo, "name" ), sizeof( s_levels.botNames[0] ) );
		} else {
			s_levels.botPics[s_levels.numBots] = 0;
			Q_strncpyz( s_levels.botNames[s_levels.numBots], bot, sizeof( s_levels.botNames[0] ) );
		}
		Q_CleanStr( s_levels.botNames[s_levels.numBots++] );
	}
}

static void AP_SetPage( void ) {
	int slot;
	s_levels.pageMapCount = s_levels.mapCount - s_levels.page * MAPS_PER_PAGE;
	if ( s_levels.pageMapCount > MAPS_PER_PAGE ) s_levels.pageMapCount = MAPS_PER_PAGE;
	if ( s_levels.selected >= s_levels.pageMapCount ) s_levels.selected = s_levels.pageMapCount - 1;
	if ( s_levels.selected < 0 ) s_levels.selected = 0;
	for ( slot = 0; slot < MAPS_PER_PAGE; ++slot ) {
		menubitmap_s *item = &s_levels.maps[slot];
		if ( slot < s_levels.pageMapCount ) {
			int map = s_levels.mapIndices[s_levels.page * MAPS_PER_PAGE + slot];
			Q_strncpyz( s_levels.levelNames[slot], q3ap_catalog_maps[map].key, sizeof( s_levels.levelNames[slot] ) );
			Q_strupr( s_levels.levelNames[slot] );
			Com_sprintf( s_levels.levelPicNames[slot], sizeof( s_levels.levelPicNames[slot] ), "levelshots/%s", q3ap_catalog_maps[map].key );
			if ( !trap_R_RegisterShaderNoMip( s_levels.levelPicNames[slot] ) )
				Q_strncpyz( s_levels.levelPicNames[slot], ART_MAP_UNKNOWN, sizeof( s_levels.levelPicNames[slot] ) );
			item->generic.flags &= ~( QMF_INACTIVE | QMF_HIDDEN );
		} else {
			s_levels.levelNames[slot][0] = '\0';
			s_levels.levelPicNames[slot][0] = '\0';
			item->generic.flags |= QMF_INACTIVE | QMF_HIDDEN;
		}
		item->shader = 0;
	}
	if ( s_levels.page == 0 ) s_levels.leftArrow.generic.flags |= QMF_INACTIVE | QMF_HIDDEN;
	else s_levels.leftArrow.generic.flags &= ~( QMF_INACTIVE | QMF_HIDDEN );
	if ( ( s_levels.page + 1 ) * MAPS_PER_PAGE >= s_levels.mapCount ) s_levels.rightArrow.generic.flags |= QMF_INACTIVE | QMF_HIDDEN;
	else s_levels.rightArrow.generic.flags &= ~( QMF_INACTIVE | QMF_HIDDEN );
	AP_SetBots();
}

static void AP_Event( void *ptr, int event ) {
	int id, map;
	if ( event != QM_ACTIVATED ) return;
	id = ( (menucommon_s *)ptr )->id;
	if ( id >= ID_PICTURE0 && id < ID_PICTURE0 + MAPS_PER_PAGE ) { s_levels.selected = id - ID_PICTURE0; AP_SetBots(); return; }
	if ( id == ID_LEFTARROW && s_levels.page > 0 ) { --s_levels.page; AP_SetPage(); return; }
	if ( id == ID_RIGHTARROW && ( s_levels.page + 1 ) * MAPS_PER_PAGE < s_levels.mapCount ) { ++s_levels.page; AP_SetPage(); return; }
	if ( id == ID_BACK ) { UI_PopMenu(); return; }
	map = AP_SelectedMap();
	if ( id == ID_NEXT && map >= 0 && ( s_levels.state.unlocked_map_mask & ( 1ULL << map ) ) ) trap_AP_StartStage( map );
}

static const char *AP_MapStatus( int map ) {
	uint64_t bit = 1ULL << map;
	if ( !( s_levels.state.unlocked_map_mask & bit ) ) return "LOCKED";
	if ( s_levels.state.cleared_map_mask & bit ) return "CLEARED";
	if ( !( s_levels.state.in_logic_map_mask & bit ) ) return "OUT OF LOGIC";
	return "UNLOCKED";
}

static void AP_Draw( void ) {
	int slot, map, x, y, pad;
	char line[128];
	trap_AP_GetState( &s_levels.state );
	map = AP_SelectedMap();
	if ( map < 0 || !( s_levels.state.unlocked_map_mask & ( 1ULL << map ) ) ) s_levels.fight.generic.flags |= QMF_GRAYED;
	else s_levels.fight.generic.flags &= ~QMF_GRAYED;
	Menu_Draw( &s_levels.menu );
	UI_DrawProportionalString( 18, 38, va( "Stages %d-%d of %d", s_levels.page * MAPS_PER_PAGE + 1,
		s_levels.page * MAPS_PER_PAGE + s_levels.pageMapCount, s_levels.mapCount ), UI_LEFT | UI_SMALLFONT, color_orange );
	for ( slot = 0; slot < s_levels.pageMapCount; ++slot ) {
		uint64_t bit;
		map = s_levels.mapIndices[s_levels.page * MAPS_PER_PAGE + slot];
		bit = 1ULL << map;
		x = s_levels.maps[slot].generic.x; y = s_levels.maps[slot].generic.y;
		UI_FillRect( x, y + 96, 128, 18, color_black );
		UI_DrawString( x + 64, y + 96, s_levels.levelNames[slot], UI_CENTER | UI_SMALLFONT, color_orange );
		if ( s_levels.state.cleared_map_mask & bit ) UI_DrawHandlePic( x, y, 128, 96, s_levels.completePic );
		if ( !( s_levels.state.unlocked_map_mask & bit ) ) { vec4_t shade = { 0, 0, 0, 0.55f }; UI_FillRect( x, y, 128, 96, shade ); }
		if ( slot == s_levels.selected ) {
			UI_DrawHandlePic( x - 1, y - 1, 130, 116, s_levels.selectedPic );
		}
	}
	map = AP_SelectedMap();
	if ( map < 0 ) return;
	Com_sprintf( line, sizeof( line ), "%s: %s", q3ap_catalog_maps[map].key, q3ap_catalog_maps[map].name );
	Q_strupr( line );
	UI_DrawProportionalString( 320, 192, line, UI_CENTER | UI_SMALLFONT, color_orange );
	Com_sprintf( line, sizeof( line ), "%s%s    CHECKS: %u / %u", AP_MapStatus( map ),
		map == s_levels.state.starting_map_index ? " [START]" : "", s_levels.state.map_checks_checked[map], s_levels.state.map_checks_total[map] );
	UI_DrawString( 320, 214, line, UI_CENTER | UI_SMALLFONT, menu_text_color );
	y = 244; pad = ( 7 - s_levels.numBots ) * 90 / 2;
	for ( slot = 0; slot < s_levels.numBots; ++slot ) {
		x = 18 + pad + 90 * slot;
		if ( s_levels.botPics[slot] ) UI_DrawHandlePic( x, y, 64, 64, s_levels.botPics[slot] );
		else { UI_FillRect( x, y, 64, 64, color_black ); UI_DrawProportionalString( x + 22, y + 18, "?", UI_BIGFONT, color_orange ); }
		UI_DrawString( x, y + 64, s_levels.botNames[slot], UI_SMALLFONT | UI_LEFT, color_orange );
	}
	UI_DrawString( 320, 350, va( "GOAL: %u / %u %s", s_levels.state.goal_cleared, s_levels.state.goal_required,
		s_levels.state.goal_type == Q3AP_GOAL_QUAD_TOKEN_HUNT ? "TOKENS" : "STAGES" ),
		UI_CENTER | UI_SMALLFONT, menu_text_color );
}

void UI_SPLevelMenu_Cache( void ) {
	trap_R_RegisterShaderNoMip( ART_LEVELFRAME_FOCUS ); trap_R_RegisterShaderNoMip( ART_LEVELFRAME_SELECTED );
	trap_R_RegisterShaderNoMip( ART_ARROW ); trap_R_RegisterShaderNoMip( ART_ARROW_FOCUS );
	trap_R_RegisterShaderNoMip( ART_MAP_UNKNOWN ); trap_R_RegisterShaderNoMip( ART_MAP_COMPLETE );
	trap_R_RegisterShaderNoMip( ART_BACK0 ); trap_R_RegisterShaderNoMip( ART_BACK1 );
	trap_R_RegisterShaderNoMip( ART_FIGHT0 ); trap_R_RegisterShaderNoMip( ART_FIGHT1 );
}

static void AP_Bitmap( menubitmap_s *item, int id, int x, int y, int width, int height, const char *name, char *focus ) {
	item->generic.type = MTYPE_BITMAP; item->generic.name = name;
	item->generic.flags = QMF_LEFT_JUSTIFY | QMF_PULSEIFFOCUS;
	item->generic.x = x; item->generic.y = y; item->generic.id = id; item->generic.callback = AP_Event;
	item->width = width; item->height = height; item->focuspic = focus;
}

static void AP_Init( void ) {
	int map, slot, startPosition = 0;
	memset( &s_levels, 0, sizeof( s_levels ) );
	trap_AP_GetState( &s_levels.state );
	for ( map = 0; map < Q3AP_MAP_COUNT; ++map ) if ( s_levels.state.selected_map_mask & ( 1ULL << map ) ) {
		if ( map == s_levels.state.starting_map_index ) startPosition = s_levels.mapCount;
		s_levels.mapIndices[s_levels.mapCount++] = map;
	}
	s_levels.page = startPosition / MAPS_PER_PAGE; s_levels.selected = startPosition % MAPS_PER_PAGE;
	s_levels.menu.fullscreen = qtrue; s_levels.menu.wrapAround = qtrue; s_levels.menu.draw = AP_Draw;
	s_levels.banner.generic.type = MTYPE_BTEXT; s_levels.banner.generic.x = 320; s_levels.banner.generic.y = 16;
	s_levels.banner.string = "ARCHIPELAGO STAGES"; s_levels.banner.color = color_red; s_levels.banner.style = UI_CENTER;
	AP_Bitmap( &s_levels.leftArrow, ID_LEFTARROW, 18, 64, 16, 114, ART_ARROW, ART_ARROW_FOCUS );
	for ( slot = 0; slot < MAPS_PER_PAGE; ++slot ) {
		AP_Bitmap( &s_levels.maps[slot], ID_PICTURE0 + slot,
			46 + 140 * slot, 64, 128, 96, s_levels.levelPicNames[slot], NULL );
		s_levels.maps[slot].generic.flags = QMF_LEFT_JUSTIFY;
	}
	AP_Bitmap( &s_levels.rightArrow, ID_RIGHTARROW, 606, 64, -16, 114, ART_ARROW, ART_ARROW_FOCUS );
	AP_Bitmap( &s_levels.back, ID_BACK, 0, 416, 128, 64, ART_BACK0, ART_BACK1 );
	AP_Bitmap( &s_levels.fight, ID_NEXT, 640, 416, 128, 64, ART_FIGHT0, ART_FIGHT1 );
	s_levels.fight.generic.flags = QMF_RIGHT_JUSTIFY | QMF_PULSEIFFOCUS;
	s_levels.nullItem.generic.type = MTYPE_BITMAP;
	s_levels.nullItem.generic.flags = QMF_LEFT_JUSTIFY | QMF_MOUSEONLY | QMF_SILENT;
	s_levels.nullItem.width = 640; s_levels.nullItem.height = 480;
	UI_SPLevelMenu_Cache();
	s_levels.selectedPic = trap_R_RegisterShaderNoMip( ART_LEVELFRAME_SELECTED );
	s_levels.focusPic = trap_R_RegisterShaderNoMip( ART_LEVELFRAME_FOCUS );
	s_levels.completePic = trap_R_RegisterShaderNoMip( ART_MAP_COMPLETE );
	Menu_AddItem( &s_levels.menu, &s_levels.banner ); Menu_AddItem( &s_levels.menu, &s_levels.leftArrow );
	for ( slot = 0; slot < MAPS_PER_PAGE; ++slot ) Menu_AddItem( &s_levels.menu, &s_levels.maps[slot] );
	Menu_AddItem( &s_levels.menu, &s_levels.rightArrow ); Menu_AddItem( &s_levels.menu, &s_levels.back );
	Menu_AddItem( &s_levels.menu, &s_levels.fight ); Menu_AddItem( &s_levels.menu, &s_levels.nullItem );
	AP_SetPage();
}

void UI_SPLevelMenu( void ) {
	apUIState_t state;
	if ( !trap_AP_GetState( &state ) || !state.slot_valid ) { UI_ArchipelagoMenu(); return; }
	AP_Init(); UI_PushMenu( &s_levels.menu ); Menu_SetCursorToItem( &s_levels.menu, &s_levels.fight );
}

void UI_SPLevelMenu_f( void ) { trap_Key_SetCatcher( KEYCATCH_UI ); uis.menusp = 0; UI_SPLevelMenu(); }
void UI_SPLevelMenu_ReInit( void ) {}
