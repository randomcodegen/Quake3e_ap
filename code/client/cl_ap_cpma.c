#include "client.h"
#include "../server/sv_ap_cpma.h"
#include "../ap/q3ap_api.h"
#include "../ap/q3ap_catalog.h"

static qhandle_t clap_markerModels[4];
static cvar_t *clap_respawnCountdown;
static cvar_t *clap_minRespawnTimer;
static cvar_t *clap_respawnThroughWalls;

void CLAP_CPMA_Reset( void ) {
	memset( clap_markerModels, 0, sizeof( clap_markerModels ) );
}

void CLAP_CPMA_AddMarkers( void ) {
	static const char *modelNames[] = {
		"models/powerups/ap/filler.md3",
		"models/powerups/ap/useful.md3",
		"models/powerups/ap/progression.md3",
		"models/powerups/ap/trap.md3"
	};
	svapCpmaMarker_t markers[Q3AP_LOCATION_MAP_STRIDE];
	int count, index;
	count = SVAP_CPMA_CopyMarkers( markers, ARRAY_LEN( markers ) );
	for ( index = 0; index < count; ++index ) {
		refEntity_t ref;
		int modelIndex = markers[index].classification & Q3AP_CLASSIFICATION_PROGRESSION ? 2 :
			( markers[index].classification & Q3AP_CLASSIFICATION_USEFUL ? 1 :
			( markers[index].classification & Q3AP_CLASSIFICATION_TRAP ? 3 : 0 ) );
		if ( !clap_markerModels[modelIndex] )
			clap_markerModels[modelIndex] = re.RegisterModel( modelNames[modelIndex] );
		if ( !clap_markerModels[modelIndex] ) continue;
		memset( &ref, 0, sizeof( ref ) );
		ref.reType = RT_MODEL;
		ref.renderfx = RF_NOSHADOW;
		ref.hModel = clap_markerModels[modelIndex];
		VectorCopy( markers[index].origin, ref.origin );
		VectorCopy( markers[index].origin, ref.oldorigin );
		AxisClear( ref.axis );
		re.AddRefEntityToScene( &ref, qfalse );
	}
}

static qboolean CLAP_Project( const refdef_t *refdef, const vec3_t origin, float *x, float *y ) {
	vec3_t delta;
	float forward, xscale, yscale;
	VectorSubtract( origin, refdef->vieworg, delta );
	forward = DotProduct( delta, refdef->viewaxis[0] );
	if ( forward <= 1 || refdef->fov_x <= 0 || refdef->fov_y <= 0 ) return qfalse;
	xscale = 320.0f / tanf( DEG2RAD( refdef->fov_x * 0.5f ) );
	yscale = 240.0f / tanf( DEG2RAD( refdef->fov_y * 0.5f ) );
	*x = 320.0f - DotProduct( delta, refdef->viewaxis[1] ) * xscale / forward;
	*y = 240.0f - DotProduct( delta, refdef->viewaxis[2] ) * yscale / forward;
	return *x >= 0 && *x <= 640 && *y >= 0 && *y <= 480;
}

void CLAP_DrawRespawnCountdowns( const refdef_t *refdef ) {
	static const vec4_t color = { 1, 1, 1, 0.85f };
	svapRespawn_t respawns[Q3AP_LOCATION_MAP_STRIDE];
	int count, index;
	if ( !clap_respawnCountdown )
		clap_respawnCountdown = Cvar_Get( "ap_show_respawntimer", "1", CVAR_ARCHIVE );
	if ( !clap_minRespawnTimer ) {
		clap_minRespawnTimer = Cvar_Get( "ap_minrespawntimer", "20", CVAR_ARCHIVE );
		Cvar_CheckRange( clap_minRespawnTimer, "0", "600", CV_INTEGER );
		Cvar_Get( "ap_show_all_respawns", "0", CVAR_ARCHIVE );
		clap_respawnThroughWalls = Cvar_Get( "ap_timer_throughwalls", "1", CVAR_ARCHIVE );
	}
	if ( !clap_respawnCountdown->integer || !Cvar_VariableIntegerValue( "ap_automap" ) || !refdef ) return;
	count = SVAP_CopyRespawns( respawns, ARRAY_LEN( respawns ) );
	for ( index = 0; index < count; ++index ) {
		char text[16];
		float x, y;
		int seconds = ( respawns[index].respawnAt - refdef->time + 999 ) / 1000;
		if ( seconds < 1 ) continue;
		if ( respawns[index].durationMs < clap_minRespawnTimer->integer * 1000 ) continue;
		if ( !CLAP_Project( refdef, respawns[index].origin, &x, &y ) ) {
			if ( DistanceSquared( cl.snap.ps.origin, respawns[index].origin ) > 4096 ) continue;
			x = 320;
			y = 240;
		}
		if ( !clap_respawnThroughWalls->integer &&
			!SVAP_RespawnVisible( refdef->vieworg, respawns[index].origin ) ) continue;
		Com_sprintf( text, sizeof( text ), respawns[index].estimated ? "<=%is" : "%is", seconds );
		SCR_DrawStringExt( (int)x - (int)strlen( text ) * 5, (int)y - 5, 10, text, color, qtrue, qtrue );
	}
}
