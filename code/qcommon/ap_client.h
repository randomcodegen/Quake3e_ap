/*
===========================================================================
Quake III Arena Archipelago client integration.
===========================================================================
*/

#ifndef AP_CLIENT_H
#define AP_CLIENT_H

#include "../ap/q3ap_api.h"

void APCL_Init( void );
void APCL_Frame( void );
void APCL_Shutdown( void );
int APCL_GameQuery( int selector, int argument );
qboolean APCL_GameString( int selector, char *buffer, int size );
qboolean APCL_SendLocation( int locationId );
qboolean APCL_CopyUIState( apUIState_t *state, int size );
qboolean APCL_UIConnect( const apConnectRequest_t *request, int size );
void APCL_UIDisconnect( void );
qboolean APCL_UIStartStage( int mapIndex );

#endif
