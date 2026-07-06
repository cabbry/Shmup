/*
	This file is part of SHMUP.

    SHMUP is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    SHMUP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with SHMUP.  If not, see <http://www.gnu.org/licenses/>.
*/    
/*
 *  native_services.h
 *  dEngine
 *
 *  Created by fabien sanglard on 10-10-08.
 *  Copyright 2010 Memset software Inc. All rights reserved.
 *
 */

#ifndef DE_NATIVE_SERVICES
#define DE_NATIVE_SERVICES

#include "globals.h"

int  Native_RetrieveListOf(char replayList[10][256]);
void Native_UploadFileTo(char path[256]);
void Action_ShowGameCenter(void* tag);
void Native_UploadScore(uint score);
void Native_LoginGameCenter(void);

// Online (GameKit GKMatch) real-time multiplayer bridge.
// Engine -> platform: the engine asks the GameKit layer to find a match and to
// send a packet to the peer. Implemented on iOS in dEngineAppDelegate.m.
// The reverse direction (match found / data received) calls back into the engine
// via NET_StartOnlineMatch / NET_OnNetworkData (declared in netchannel.h).
void Native_StartOnlineMatchmaking(void);
void Native_CancelOnlineMatchmaking(void);
void Native_GKSendData(const void* data, int len, int reliable);

// Persist the solo loadout (ship + bullet colour) so it survives app restarts.
void Native_SaveLoadout(int ship, int color);
void Native_SaveProgress(int highestAct);

#endif

