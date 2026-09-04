#include "ap_cpma_logic.h"
#include <stdint.h>
#include <string.h>

int Q3AP_CPMA_PlayerTouches( const float player[3], const float item[3] ) {
	return player[0] - item[0] <= 44 && player[0] - item[0] >= -50 &&
		player[1] - item[1] <= 36 && player[1] - item[1] >= -36 &&
		player[2] - item[2] <= 36 && player[2] - item[2] >= -36;
}

int Q3AP_CPMA_IsTriggerScan( const float mins[3], const float maxs[3], const float player[3] ) {
	static const float range[3] = { 40, 40, 52 };
	int axis;
	for ( axis = 0; axis < 3; ++axis )
		if ( mins[axis] - ( player[axis] - range[axis] ) < -0.125f ||
			 mins[axis] - ( player[axis] - range[axis] ) > 0.125f ||
			 maxs[axis] - ( player[axis] + range[axis] ) < -0.125f ||
			 maxs[axis] - ( player[axis] + range[axis] ) > 0.125f ) return 0;
	return 1;
}

int Q3AP_CPMA_PlayerSweptTouches( const float start[3], const float end[3], const float item[3] ) {
	static const float mins[3] = { -50, -36, -36 };
	static const float maxs[3] = { 44, 36, 36 };
	float enter = 0, leave = 1;
	int axis;
	for ( axis = 0; axis < 3; ++axis ) {
		float delta = end[axis] - start[axis];
		float low = item[axis] + mins[axis];
		float high = item[axis] + maxs[axis];
		float first, last;
		if ( delta == 0 ) {
			if ( start[axis] < low || start[axis] > high ) return 0;
			continue;
		}
		first = ( low - start[axis] ) / delta;
		last = ( high - start[axis] ) / delta;
		if ( first > last ) { float swap = first; first = last; last = swap; }
		if ( first > enter ) enter = first;
		if ( last < leave ) leave = last;
		if ( enter > leave ) return 0;
	}
	return 1;
}

float Q3AP_CPMA_PickupEventDistance( const float player[3], const float item[3],
	int eventModel, int itemModel ) {
	float dx, dy, dz;
	if ( eventModel != itemModel ) return -1;
	dx = player[0] - item[0];
	dy = player[1] - item[1];
	dz = player[2] - item[2];
	return dx * dx + dy * dy + dz * dz;
}

int Q3AP_CPMA_PickupCandidateBetter( int touches, int entityNum, float distance,
	int bestTouches, int bestEntityNum, float bestDistance ) {
	if ( distance < 0 ) return 0;
	if ( touches ) return !bestTouches || entityNum < bestEntityNum;
	return !bestTouches && ( bestDistance < 0 || distance < bestDistance );
}

int Q3AP_CPMA_PickupEventReady( int lastEventTime, int now ) {
	return now - lastEventTime >= 1000;
}

float Q3AP_CPMA_SpawnDistance( const float bspOrigin[3], const float itemOrigin[3] ) {
	float dx = bspOrigin[0] - itemOrigin[0];
	float dy = bspOrigin[1] - itemOrigin[1];
	float dz = bspOrigin[2] - itemOrigin[2];
	if ( dx < -1 || dx > 1 || dy < -1 || dy > 1 || dz < -4096 || dz > 4096 ) return -1;
	return dx * dx + dy * dy + dz * dz;
}

int Q3AP_CPMA_RespawnSeconds( const char *classname, int family, int weaponRespawn ) {
	if ( classname && !strncmp( classname, "weapon_", 7 ) ) return weaponRespawn > 0 ? weaponRespawn : 5;
	if ( classname && !strncmp( classname, "ammo_", 5 ) ) return 40;
	if ( family == 8 ) return 25;
	if ( family == 9 || family == 10 ) return 35;
	if ( family == 11 || family == 12 ) return 60;
	return family >= 13 ? 120 : 35;
}

int Q3AP_CPMA_RespawnAt( int now, int nextthink, int think, int respawnThink ) {
	int64_t remaining = (int64_t)nextthink - now;
	return respawnThink > 0 && think == respawnThink &&
		remaining > 0 && remaining <= 600000 ? nextthink : 0;
}
