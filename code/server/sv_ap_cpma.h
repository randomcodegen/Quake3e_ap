#ifndef SV_AP_CPMA_H
#define SV_AP_CPMA_H

#include "../qcommon/q_shared.h"

typedef struct {
	vec3_t origin;
	int classification;
} svapCpmaMarker_t;

typedef struct {
	vec3_t origin;
	int respawnAt;
	int durationMs;
	qboolean estimated;
} svapRespawn_t;

void SVAP_CPMA_BeginMap( void );
void SVAP_CPMA_EndMap( void );
void SVAP_CPMA_EntityToken( const char *token );
qboolean SVAP_CPMA_LinkEntity( void *entity );
void SVAP_CPMA_UnlinkEntity( void *entity );
int SVAP_CPMA_FilterAreaEntities( const vec3_t mins, const vec3_t maxs, int *entities, int count );
void SVAP_CPMA_BeforeFrame( void );
void SVAP_CPMA_AfterFrame( void );
qboolean SVAP_CPMA_ApplyStageLimits( int gameType, int fragLimit );
int SVAP_CPMA_CopyMarkers( svapCpmaMarker_t *markers, int capacity );
int SVAP_CopyRespawns( svapRespawn_t *respawns, int capacity );
qboolean SVAP_RespawnVisible( const vec3_t viewOrigin, const vec3_t itemOrigin );

#endif
