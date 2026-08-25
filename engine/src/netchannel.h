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
 *  netchannel.h
 *  dEngine
 *
 *  Created by fabien sanglard on 10-06-11.
 *  Copyright 2010 Memset software Inc. All rights reserved.
 *
 */



#ifndef DF_NETCHANNEL
#define DF_NETCHANNEL


#include "commands.h"
#include "globals.h"



#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include "menu.h"
#include "music.h"
#include "timer.h"
#include "dEngine.h"
#include "player.h"



#define NET_OK		(-1)
#define NET_NO_NETWORK 0
#define NET_NOT_SERVER 1
#define NET_BIND_ERROR 2
#define NET_UDP_SOCKET_CANNOT_BE_CREATED 3

int NET_Init(void);




#define NET_REGISTER 1
#define NET_COMMANDS 2
#define NET_PING_REQUEST 3
#define NET_PING_RESPONSE 4

#define BUFFER_SIZE 1024

#if !defined(WIN32) && !defined(ANDROID) && !defined(LINUX)
#include <dns_sd.h>
#include <netdb.h>		
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>

typedef struct net_channel_t
{
	// v2: the buffer FIRST. Every read path casts it to net_packet_t*, and at
	// its old offset (22, after the sockaddr + two chars) that cast was
	// misaligned by 2 -- undefined behaviour the 2010 code got away with
	// because ARM tolerates unaligned loads. Offset 0 is aligned by
	// construction; nothing here travels on the wire, so this is free.
	uchar					buffer[BUFFER_SIZE];
	int						udpSocket;
	struct sockaddr_in		peerAddr; 
	char					serverAddResolved ;
	char					setupRequested;

#define NET_UNKNOWN 0
#define NET_SERVER  1
#define NET_CLIENT  2
int				type;
	
#define NET_UNDETERMINED	0
#define NET_STARTED			1
#define NET_PRELOADED		2
#define NET_RUNNING			3
int				state;

#define NET_TRANSPORT_LAN			0	// peer-to-peer UDP + Bonjour (local network)
#define NET_TRANSPORT_GAMECENTER	1	// GameKit GKMatch (online, NAT-traversed)
int				transport;

	// v2 P1: seat identity (0..numSeats-1, seat 0 hosts). At 2 players seat 0
	// is exactly the old NET_SERVER and seat 1 the old NET_CLIENT.
	int				ownSeat;
	int				numSeats;

	unsigned int lastReceivedSequenceNumber;
	unsigned int lastSentSequenceNumber;

	uint numDropedPackets;

} net_channel_t;
#else
	
typedef struct net_channel_t
{
	uchar					buffer[BUFFER_SIZE];	// v2: first, so the net_packet_t* cast is aligned
	int						udpSocket;
	//struct sockaddr_in		peerAddr;
	char					serverAddResolved ;
	char					setupRequested;

#define NET_UNKNOWN 0
#define NET_SERVER  1
#define NET_CLIENT  2
int				type;
	
#define NET_UNDETERMINED	0
#define NET_STARTED			1
#define NET_PRELOADED		2
#define NET_RUNNING			3
int				state;

#define NET_TRANSPORT_LAN			0	// peer-to-peer UDP + Bonjour (local network)
#define NET_TRANSPORT_GAMECENTER	1	// GameKit GKMatch (online, NAT-traversed)
int				transport;

	// v2 P1: seat identity (0..numSeats-1, seat 0 hosts). At 2 players seat 0
	// is exactly the old NET_SERVER and seat 1 the old NET_CLIENT.
	int				ownSeat;
	int				numSeats;

	unsigned int lastReceivedSequenceNumber;
	unsigned int lastSentSequenceNumber;

	uint numDropedPackets;

} net_channel_t;
	
#endif



extern net_channel_t net;



void NET_Setup(void);
void NET_Receive(void);
void NET_Send(void);

void NET_Free(void);

char NET_IsInitialized();

void Net_SendDie(command_t* command);


void NET_OnNextLevelLoad(void);
char NET_IsRunning(void);
char NET_IsInMatch(void);	// v2: RUNNING *or* between levels (see the .c)

uint NET_GetDropedPackets(void);

// Online (GameKit) multiplayer entry points, called FROM the iOS GameKit layer.
// v2 P1: the boolean role became a SEAT. Every device sorts all gamePlayerIDs
// (its own included) ascending; the index in that order is the seat, seat 0
// hosts. Deterministic on every device without negotiation -- the N-player
// generalization of the old "lowest id wins SERVER" pairwise compare, and
// bit-identical to it at 2 players.
void NET_StartOnlineMatch(int mySeat, int numSeats);
void NET_AbortOnlineMatch(void);					// matchmaking cancelled, peer dropped, or failed
void NET_OnPeerLost(void);							// LAST peer vanished mid-match: clean reset + notice
// v2 P2: ONE seat vanished mid-match -- park its ship and keep playing; falls
// back to NET_OnPeerLost() when no remote seat is left. Called by the per-seat
// liveness timeout (all transports) and by the GameKit disconnect callback.
void NET_OnSeatLost(int seat);
// v2 P1: inbound packets carry their SENDER SEAT (the GameKit layer maps the
// GKPlayer back through the seat table). With one peer the seat is redundant;
// with three it is the only honest identity -- the in-packet playerId is
// sender-declared and will be validated against this, never trusted alone.
void NET_OnNetworkDataFrom(int senderSeat, const void* data, int len);
char NET_IsOnline(void);							// true when the active transport is GameKit
#endif