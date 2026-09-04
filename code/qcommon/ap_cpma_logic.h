#ifndef AP_CPMA_LOGIC_H
#define AP_CPMA_LOGIC_H

int Q3AP_CPMA_PlayerTouches( const float player[3], const float item[3] );
int Q3AP_CPMA_IsTriggerScan( const float mins[3], const float maxs[3], const float player[3] );
int Q3AP_CPMA_PlayerSweptTouches( const float start[3], const float end[3], const float item[3] );
float Q3AP_CPMA_PickupEventDistance( const float player[3], const float item[3],
	int eventModel, int itemModel );
int Q3AP_CPMA_PickupCandidateBetter( int touches, int entityNum, float distance,
	int bestTouches, int bestEntityNum, float bestDistance );
int Q3AP_CPMA_PickupEventReady( int lastEventTime, int now );
float Q3AP_CPMA_SpawnDistance( const float bspOrigin[3], const float itemOrigin[3] );
int Q3AP_CPMA_RespawnSeconds( const char *classname, int family, int weaponRespawn );
int Q3AP_CPMA_RespawnAt( int now, int nextthink, int think, int respawnThink );

#endif
