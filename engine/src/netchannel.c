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
 *  netchannel.c
 *  dEngine
 *
 *  Created by fabien sanglard on 10-06-11.
 *  Copyright 2010 Memset software Inc. All rights reserved.
 *
 */

#include "netchannel.h"
#include "native_services.h"	// Native_GKSendData / Native_StartOnlineMatchmaking (online MP)

// The network version was designed on iOS with Unix socket. This part still needs to be ported using winsock32.
#if defined(WIN32) || defined(ANDROID) || defined(LINUX)
	int NET_Init(void){return 1;}
	void NET_Setup(void){}
	void NET_Receive(void){}
	void NET_Send(void){}
	void NET_Free(void){}
	char NET_IsInitialized(){return 1;}
	void Net_SendDie(command_t* command){}
	void NET_OnActLoaded(void){}
	void NET_OnNextLevelLoad(void){}
	char NET_IsRunning(void){return 0;}
	uint NET_GetDropedPackets(void){return 0;}
	void NET_StartOnlineMatch(int isServer){}
	void NET_AbortOnlineMatch(void){}
	void NET_OnPeerLost(void){}
	void NET_OnNetworkData(const void* data, int len){}
	char NET_IsOnline(void){return 0;}

	net_channel_t net;
#else

#define DNSServiceRefDeallocate(x)Log_Printf("DNSServiceRefDeallocate(" #x ")\n"); DNSServiceRefDeallocate(x) 


#define MESSAGE_NETMYIP 1
#define MESSAGE_NETPEERPIP 2
#define MESSAGE_NETYPE 0
#define MESSAGE_NETSTATE 3
#define MESSAGE_NETLASTSENT 4
#define MESSAGE_NETLASTRECEIVED 5

#ifdef __APPLE__
	#include "TargetConditionals.h"
	#if TARGET_IPHONE_SIMULATOR
		#define INTERFACE_NAME "en1"
	#else
		#define INTERFACE_NAME "en0"
	#endif
#else
	#define INTERFACE_NAME "en0"
#endif

DNSServiceRef		browseRef=0;
DNSServiceRef		registerRef=0;
DNSServiceRef		resolveRef=0;

// Our own LAN address (en0), used to elect the role deterministically: the device
// with the lower IP becomes the SERVER (Player One). Computed once per session.
struct sockaddr_in	ownAddr;
int					ownAddrValid = 0;

// When the current peer browse was issued (see the periodic re-browse in
// NET_CheckServerAvailability).
static int			lastBrowseTime = 0;

static const char	*serviceName = "_DodgeServer._udp.";




typedef struct service_t{
	int				interfaceIndex;
	char			browseName[1024];
	char			browseRegtype[1024];
	char			browseDomain[1024];
} service_t ;

#define MAX_SERVICE_INTEFACES 10
service_t		serviceInterfaces[MAX_SERVICE_INTEFACES];


#define PORT_NUMBER 31978




// This is a designated initializers, a C99 feature which allows you to name members to be initialized
net_channel_t net = { .type = NET_UNKNOWN };

typedef struct net_packet_t
{
#define SETUP_PACKET 1
#define RUNTIME_PACKET 2
	char type;
	
	int sequenceNumber;
	int ackSequenceNumber;
	
	int time;
	int ackTime;

	
//#define NET_CMD_NOOP 0
#define NET_CMD_LOAD_NEXT_LEVEL 0
#define NET_CMD_NOTIFY_LOADED 1
#define NET_CMD_START_LEVEL 2

	command_t command;

	// Command redundancy (anti packet-loss): each runtime packet also carries the
	// previous few commands + their sequence numbers, so a lost packet's input is
	// recovered from the next one (the receiver applies any command it hasn't seen).
	// Not used by setup/ABS packets (numRedundant = 0).
#define NET_REDUNDANT_CMDS 2
	int			numRedundant;
	int			redundantSeq[NET_REDUNDANT_CMDS];
	command_t	redundant[NET_REDUNDANT_CMDS];

	// Custom loadout (setup packets only): the sender's chosen ship + bullet colour,
	// so each player keeps his Custom look in multiplayer (see NET_StorePeerLoadout).
	int			shipChoice;
	int			bulletColor;

} net_packet_t;

// Outgoing command-redundancy ring: the last few runtime commands we sent (oldest
// first), echoed in each packet's redundant[] so the peer can recover an input lost
// to a dropped packet.
static command_t	sentCmds[NET_REDUNDANT_CMDS];
static int			sentSeqs[NET_REDUNDANT_CMDS];
static int			sentCount = 0;


// ------------------------------------------------------------------------------
//  Transport abstraction: LAN (UDP+Bonjour) vs online (GameKit GKMatch)
// ------------------------------------------------------------------------------
// The whole lockstep protocol below is transport-agnostic: every message is a
// fixed-size net_packet_t. On the LAN it travels over a UDP socket
// (sendto/recvfrom); online it travels through GKMatch (Native_GKSendData out,
// NET_OnNetworkData in) with Apple handling matchmaking + NAT traversal. GKMatch
// is push-based, so inbound packets are queued here and drained by the very same
// read loops the UDP code already uses. LAN behaviour is unchanged.

#define NET_RXQUEUE_SIZE 64
typedef struct net_rx_entry_t { uchar data[BUFFER_SIZE]; int len; } net_rx_entry_t;
static net_rx_entry_t	netRxQueue[NET_RXQUEUE_SIZE];
static volatile int		netRxHead = 0;	// next slot to write (producer: GKMatch delegate)
static volatile int		netRxTail = 0;	// next slot to read  (consumer: game loop)

char NET_IsOnline(void) { return net.transport == NET_TRANSPORT_GAMECENTER; }

// Called by the GameKit layer when a packet arrives from the peer. GKMatch
// delivers on the main thread, same thread as the game loop that drains the
// queue, so no locking is needed (the volatile indices are belt-and-suspenders).
void NET_OnNetworkData(const void* data, int len)
{
	int next;
	if (len <= 0 || len > BUFFER_SIZE)
		return;
	next = (netRxHead + 1) % NET_RXQUEUE_SIZE;
	if (next == netRxTail)
		return;	// queue full: drop. Lockstep tolerates loss; the periodic ABS update re-syncs.
	memcpy(netRxQueue[netRxHead].data, data, len);
	netRxQueue[netRxHead].len = len;
	netRxHead = next;
}

// Drain one queued packet. Mirrors recvfrom's contract so the existing read
// loops are untouched: returns the byte count, or -1 with errno=EAGAIN when the
// queue is empty.
static int NET_RxDequeue(void* out, int maxlen)
{
	int len;
	if (netRxTail == netRxHead) { errno = EAGAIN; return -1; }
	len = netRxQueue[netRxTail].len;
	if (len > maxlen) len = maxlen;
	memcpy(out, netRxQueue[netRxTail].data, len);
	netRxTail = (netRxTail + 1) % NET_RXQUEUE_SIZE;
	return len;
}

// Unified send. Online: setup/death packets go reliable, per-frame runtime
// deltas go unreliable (lower latency; the periodic ABS update repairs drift).
static void NET_TransportSend(const void* data, int len)
{
	if (net.transport == NET_TRANSPORT_GAMECENTER)
	{
		const net_packet_t* p = (const net_packet_t*)data;
		int reliable = (p->type != RUNTIME_PACKET);
		Native_GKSendData(data, len, reliable);
	}
	else
	{
		sendto(net.udpSocket, data, len, 0, (struct sockaddr*)&net.peerAddr, sizeof(net.peerAddr));
	}
}

// Unified non-blocking receive. Returns bytes read, or -1/EAGAIN when nothing is
// available. fromAddr (LAN only) captures the sender so the server can learn the
// client's address; it is left untouched online (GKMatch has the single peer).
static int NET_TransportRecv(void* out, int maxlen, struct sockaddr_in* fromAddr)
{
	if (net.transport == NET_TRANSPORT_GAMECENTER)
		return NET_RxDequeue(out, maxlen);

	if (fromAddr)
	{
		socklen_t len = sizeof(*fromAddr);
		return recvfrom(net.udpSocket, out, maxlen, 0, (struct sockaddr*)fromAddr, &len);
	}
	return recvfrom(net.udpSocket, out, maxlen, 0, NULL, NULL);
}

// Begin an online match once GKMatch has connected both peers and a role has been
// elected (deterministically, in the GameKit layer). This bypasses the entire
// Bonjour election: the peer is the GKMatch, so the same handshake state machine
// (LOAD_NEXT_LEVEL -> NOTIFY_LOADED -> START_LEVEL) runs straight away.
void NET_StartOnlineMatch(int isServer)
{
	netRxHead = netRxTail = 0;					// fresh receive queue for this match
	net.transport = NET_TRANSPORT_GAMECENTER;
	net.type      = isServer ? NET_SERVER : NET_CLIENT;
	net.state     = NET_STARTED;
	net.serverAddResolved = 1;					// no resolve online; the peer is the match
	net.setupRequested    = 1;
	net.lastReceivedSequenceNumber = 0;
	net.lastSentSequenceNumber     = 1;

	if (isServer)
	{
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE),   "Online - you are Player ONE");
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE+1), " ");
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE+2), "Starting match...");
	}
	else
	{
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE),   "Online - you are Player TWO");
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE+1), " ");
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE+2), "Starting match...");
	}
}

void NET_AbortOnlineMatch(void)
{
	NET_Free();				// resets transport back to LAN, clears state
	MENU_Set(MENU_OTHERS);
}


// PREDICTION
#define MAX_CMD_HISTORY 4
typedef struct cmdHistory
{
	command_t array [MAX_CMD_HISTORY];
	uchar ptr;
} cmdHistory_t;
cmdHistory_t cmdHistory;

#define MAX_FAKE_CMD_HISTORY 16
typedef struct fakeCmdHistory_t
{
	command_t stack[MAX_FAKE_CMD_HISTORY];
	uchar num;
} fakeCmdHistory_t ;

fakeCmdHistory_t fakeCmdHistory;

// END PREDICTION


void NET_Free(void)
{
	Log_Printf("NET_FREE\n");

	// If this was an online session, disconnect the GKMatch (no-op if there isn't one).
	if (net.transport == NET_TRANSPORT_GAMECENTER)
		Native_CancelOnlineMatchmaking();

	// unregister
	DNSServiceRefDeallocate(browseRef); browseRef=0;
	DNSServiceRefDeallocate(registerRef);registerRef=0;
	DNSServiceRefDeallocate(resolveRef);resolveRef=0;
	
	net.type=NET_UNKNOWN;
	net.serverAddResolved = 0;
	net.setupRequested = 0;
	net.state = NET_UNDETERMINED;
	net.transport = NET_TRANSPORT_LAN;	// default transport; the online entry sets GameKit
	netRxHead = netRxTail = 0;			// flush any queued online packets
	ownAddrValid = 0;					// recompute our own IP next session (for role election)
	sentCount = 0;						// clear the outgoing command-redundancy ring

	net.lastReceivedSequenceNumber = 0;
	net.lastSentSequenceNumber = 1;
	
	net.numDropedPackets = 0 ;
	
	//free(buffer);
	
	// unbind. Guard against udpSocket==0: close(0) would close STDIN, freeing fd 0
	// so the next socket (e.g. the DNS-SD registration ref) gets fd 0 -- which the
	// "<= 0" sockfd checks then wrongly treated as an error. (This is what broke LAN:
	// register succeeded but DNSServiceRefSockFD returned 0 and was rejected.)
	if (net.udpSocket > 0)
		close(net.udpSocket);
	net.udpSocket=0;
	
	//Also reset all messages
	MENU_GetMultiplayerTextLine(0)[0]='\0';
	MENU_GetMultiplayerTextLine(1)[0]='\0';
	MENU_GetMultiplayerTextLine(2)[0]='\0';
	MENU_GetMultiplayerTextLine(3)[0]='\0';
	MENU_GetMultiplayerTextLine(4)[0]='\0';
	MENU_GetMultiplayerTextLine(5)[0]='\0';
}

char NET_IsNetworkAvailable() {
	struct ifaddrs *ifap;
	if ( getifaddrs( &ifap ) == -1 ) {
		return 0;
	}
	
	Log_Printf("NET_IsNetworkAvailable\n");
	
	//Log_Printf("NET_IsNetworkAvailable() searching for interface %s with type %d\n",INTERFACE_NAME,AF_INET);
	
	// We can't tell if bluetooth is available from here, because
	// the interface doesn't appear until after the service is found,
	// but I decided not to support bluetooth for now due to the poor performance.
	char	goodInterface = 0;
	
	for ( struct ifaddrs *ifa = ifap ; ifa ; ifa = ifa->ifa_next ) {
		struct sockaddr_in *ina = (struct sockaddr_in *)ifa->ifa_addr;
	//	Log_Printf("[NET_IsNetworkAvailable] Searching interface: %s, family=%d.\n",ifa->ifa_name,ina->sin_family);
	//	Log_Printf("current if: %s, family=%d @=%s IFF_UP=%d IFF_RUNNING=%d .\n",ifa->ifa_name,ina->sin_family,inet_ntoa(ina->sin_addr),ifa->ifa_flags & IFF_UP != 0, ifa->ifa_flags & IFF_RUNNING != 0);
		if ( ina->sin_family == AF_INET ) {
			if ( !strcmp( ifa->ifa_name, INTERFACE_NAME ) ) {
		//		Log_Printf("[NET_IsNetworkAvailable] Found interface: %s, family=%d.\n",ifa->ifa_name,ina->sin_family);
				goodInterface = 1;
				break;
			}
		}
	}
	freeifaddrs( ifap );
	
	return goodInterface;
}

struct sockaddr_in NET_GetAddressForInterfaceName( const char *ifname ) 
{	
	
	struct sockaddr_in s;
	
	
	Log_Printf("NET_GetAddressForInterfaceName\n");
	
	memset( &s, 0, sizeof( s ) );
	
	struct ifaddrs *ifap;
	if ( getifaddrs( &ifap ) == -1 ) {
		perror( "getifaddrs()" );
		return s;
	}
	
	struct ifaddrs *ifa;
	for ( ifa = ifap ; ifa ; ifa = ifa->ifa_next ) {
		struct sockaddr_in *ina = (struct sockaddr_in *)ifa->ifa_addr;
		if ( ina->sin_family == AF_INET && !strcmp( ifa->ifa_name, ifname ) ) {
			uchar *ip = (uchar *)&ina->sin_addr;
			Log_Printf("if: %s, family=%d @=%s IFF_UP=%d IFF_RUNNING=%d .\n",
                   ifa->ifa_name,ina->sin_family,
                   inet_ntoa(ina->sin_addr),
                   (ifa->ifa_flags & IFF_UP) != 0, 
                   (ifa->ifa_flags & IFF_RUNNING) != 0);
//			Log_Printf( "AddressForInterfaceName( %s ) = ifa_name: %s ifa_flags: %i sa_family: %i=AF_INET ip: %i.%i.%i.%i\n", ifname, ifa->ifa_name, ifa->ifa_flags,ina->sin_family, ip[0], ip[1], ip[2], ip[3]  );
			sprintf(MENU_GetMultiplayerTextLine(1),"My IP: %i.%i.%i.%i",ip[0], ip[1], ip[2], ip[3]);
			freeifaddrs( ifap );
			return *ina;
		}
	}
	freeifaddrs( ifap );
	Log_Printf( "AddressForInterfaceName( %s ): Couldn't find IP address\n", ifname );
	return s;
}

int NET_InterfaceIndexForInterfaceName( const char *ifname ) {
	struct if_nameindex *ifnames = if_nameindex();
	if ( !ifnames ) {
		perror( "if_nameindex()" );
		return 0;
	}
	for ( int i = 0 ; ifnames[i].if_index != 0 ; i++ ) {
		if ( !strcmp( ifname, ifnames[i].if_name ) ) {
			int	index = ifnames[i].if_index;
			if_freenameindex( ifnames );
			return index;
		}
	}	
	Log_Printf( "InterfaceIndexForName( %s ): Couldn't find interface\n", ifname );
	if_freenameindex( ifnames );
	return 0;
}


void DNSServiceRegisterReplyCallback ( 
									  DNSServiceRef sdRef, 
									  DNSServiceFlags flags, 
									  DNSServiceErrorType errorCode, 
									  const char *name, 
									  const char *regtype, 
									  const char *domain, 
									  void *context ) {
	
	Log_Printf("DNSServiceRegisterReplyCallback err=%d\n", errorCode);
	// The role is NO LONGER decided here. We register with auto-rename so BOTH devices
	// stay advertised and can discover each other; the role is then elected by IP
	// comparison once we resolve the peer (see DNSServiceQueryRecordReplyCallback).
	(void)name; (void)regtype; (void)domain;
}

// Defined further down, but NET_CheckServerAvailability now starts the browse itself.
void DNSServiceBrowseReplyCallback( DNSServiceRef sdRef, DNSServiceFlags flags,
                                    uint32_t interfaceIndex, DNSServiceErrorType errorCode,
                                    const char *serviceName, const char *regtype,
                                    const char *replyDomain, void *context );

int NET_CheckServerAvailability(void)
{
	int	socket;
	fd_set	set;
	struct timeval tv;

	int	ifIdx;

	net.type = NET_UNKNOWN;

	// Own LAN IP (en0), computed once, for the deterministic IP-based role election.
	if ( !ownAddrValid )
	{
		ownAddr = NET_GetAddressForInterfaceName( INTERFACE_NAME );
		ownAddrValid = ( ownAddr.sin_addr.s_addr != 0 );
	}

	ifIdx = NET_InterfaceIndexForInterfaceName( INTERFACE_NAME );

	// Register ONCE with AUTO-RENAME (no kDNSServiceFlagsNoAutoRename) so BOTH devices
	// stay advertised and discover each other -- there's no name conflict to race over,
	// so the start no longer has to be staggered. We don't drain our own register
	// socket: the service goes live in mDNSResponder regardless, and the role is no
	// longer taken from the register reply -- it's elected by IP below.
	if ( registerRef == 0 )
	{
		DNSServiceErrorType	rerr = DNSServiceRegister(
												 &registerRef,
												 0,									// auto-rename: both devices advertise
												 ifIdx,								// our LAN interface (en0)
												 "Dodge shmup server",
												 serviceName,
												 NULL, NULL,
												 htons( PORT_NUMBER ),
												 0, NULL,
												 DNSServiceRegisterReplyCallback,
												 NULL );
		if ( rerr != kDNSServiceErr_NoError ) { Log_Printf("DNSServiceRegister error\n"); registerRef = 0; }
	}

	// Browse for peers. Re-issue the browse every few seconds while nobody is found:
	// a long-lived mDNS browse backs its queries off exponentially, so if the other
	// device shows up "late" a single browse can sit silent for a long while -- a
	// fresh browse restarts the query schedule (this was the "have to back out and
	// retry" symptom). Once the peer is elected we leave the browse alone.
	if ( browseRef != 0 && net.type == NET_UNKNOWN && simulationTime - lastBrowseTime > 8000 )
	{
		DNSServiceRefDeallocate( browseRef );
		browseRef = 0;
	}
	if ( browseRef == 0 )
	{
		DNSServiceErrorType	berr = DNSServiceBrowse( &browseRef, 0, 0, serviceName, NULL,
		                                             DNSServiceBrowseReplyCallback, NULL );
		if ( berr != kDNSServiceErr_NoError ) { Log_Printf("DNSServiceBrowse error\n"); browseRef = 0; return 0; }
		lastBrowseTime = simulationTime;
	}

	socket = DNSServiceRefSockFD( browseRef );
	if ( socket < 0 )		// fd 0 is valid; only -1 is an error
		return 0;

	// Non-blocking drain, once per frame: discovery events are caught just as fast as
	// with a blocking wait, but the menu keeps rendering at full speed (the previous
	// 300ms blocking select made the whole waiting screen stutter at ~3 fps).
	FD_ZERO( &set );
	FD_SET( socket, &set );
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	if ( select( socket+1, &set, NULL, NULL, &tv ) > 0 )
		DNSServiceProcessResult( browseRef );		// -> browse cb -> resolve -> query -> election

	if (net.type == NET_SERVER)
	{
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE), "Waiting for client to connect...");
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE+1), " ");
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE+2), "You are Player ONE.");
		MENU_GetMultiplayerTextLine(MESSAGE_NETSTATE)[0] = '\0';	// clear the "second device" hint
	}
	else if (net.type == NET_CLIENT)
	{
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE), "Contacting server... You are Player TWO.");
		MENU_GetMultiplayerTextLine(MESSAGE_NETSTATE)[0] = '\0';	// clear the "second device" hint
	}
	else
	{
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE),   "Looking for the other player...");
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE+1), " ");
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE+2), "Open Others > Network");
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETSTATE), "on the second device.");
	}

	return 1;
}

// Drain one result from a one-shot ref, giving up after a short timeout instead of
// blocking forever: a stale ("ghost") advertisement left behind by a killed app would
// otherwise hang the menu inside DNSServiceProcessResult until the record expires.
static int NET_ProcessResultWithTimeout(DNSServiceRef ref, int timeoutMs)
{
	fd_set set;
	struct timeval tv;
	int fd = DNSServiceRefSockFD( ref );
	if ( fd < 0 )
		return 0;
	FD_ZERO( &set );
	FD_SET( fd, &set );
	tv.tv_sec  = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
	if ( select( fd+1, &set, NULL, NULL, &tv ) > 0 )
		return DNSServiceProcessResult( ref ) == kDNSServiceErr_NoError;
	return 0;	// timed out: no answer (ghost record) -- caller just moves on
}

void DNSServiceQueryRecordReplyCallback (
										 DNSServiceRef DNSServiceRef,
										 DNSServiceFlags flags,
										 uint32_t interfaceIndex,
										 DNSServiceErrorType errorCode,
										 const char *fullname,
										 uint16_t rrtype,
										 uint16_t rrclass,
										 uint16_t rdlen,
										 const void *rdata,
										 uint32_t ttl,
										 void *context ) {
	
	
	char	interfaceName[IF_NAMESIZE];
	Log_Printf("DNSServiceQueryRecordReplyCallback\n");
	
	if_indextoname( interfaceIndex, interfaceName );
	
	//Log_Printf( "DNSServiceQueryRecordReplyCallback: Found service %s on interface %s.\n",fullname,interfaceName);
	//Log_Printf( "DNSServiceQueryRecordReplyCallback: %s, interface[%i] = %s, [%i] = %i.%i.%i.%i\n", fullname, interfaceIndex, interfaceName, rdlen, ip[0], ip[1], ip[2], ip[3] );
	
	
	//ReportNetworkInterfaces();

	struct in_addr peerIp;
	memcpy( &peerIp, rdata, 4 );

	// We browse and resolve EVERY advertised instance, including our own (auto-rename
	// keeps us discoverable). Ignore the one that resolves to our own IP.
	if ( ownAddrValid && peerIp.s_addr == ownAddr.sin_addr.s_addr )
		return;

	// This is the peer. Record its address for the handshake.
	memset( &net.peerAddr, 0, sizeof( net.peerAddr ) );
	net.peerAddr.sin_len = sizeof( net.peerAddr );
	net.peerAddr.sin_family = AF_INET;
	net.peerAddr.sin_port = htons( PORT_NUMBER );
	net.peerAddr.sin_addr = peerIp;
	net.serverAddResolved = 1;

	// Deterministic election: the LOWER IP is the SERVER (Player One). Both devices
	// run the same comparison, so exactly one becomes server -- no need to stagger
	// the start. (compare in host byte order)
	if ( ownAddrValid && ntohl(ownAddr.sin_addr.s_addr) < ntohl(peerIp.s_addr) )
		net.type = NET_SERVER;
	else
		net.type = NET_CLIENT;
	net.state = NET_STARTED;

	sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETPEERPIP), "PEER %s -> %s",
	        inet_ntoa(peerIp), net.type == NET_SERVER ? "I am P1" : "I am P2");
}

void DNSServiceResolveReplyCallback ( 
									 DNSServiceRef sdRef, 
									 DNSServiceFlags flags, 
									 uint32_t interfaceIndex, 
									 DNSServiceErrorType errorCode, 
									 const char *fullname, 
									 const char *hosttarget, 
									 uint16_t port, 
									 uint16_t txtLen, 
									 const unsigned char *txtRecord, 
									 void *context ) {
	
	DNSServiceRef	queryRef;
	char	interfaceName[IF_NAMESIZE];
	
	Log_Printf("DNSServiceResolveReplyCallback\n");
	
	if_indextoname( interfaceIndex, interfaceName );
	//Log_Printf( "Resolve: interfaceIndex [%i]=%s : %s @ %s\n", interfaceIndex, interfaceName, fullname, hosttarget );
	

	
	// look up the name for this host
	DNSServiceErrorType err = DNSServiceQueryRecord ( 
													 &queryRef, 
													 kDNSServiceFlagsForceMulticast, 
													 interfaceIndex, 
													 hosttarget, 
													 kDNSServiceType_A,		// we want the host address
													 kDNSServiceClass_IN, 
													 DNSServiceQueryRecordReplyCallback, 
													 NULL /* may be NULL */
													 );  	
	if ( err != kDNSServiceErr_NoError )
	{
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETPEERPIP), "DNSServiceQueryRecord error");
	}
	else
	{
		// wait for the answer, but bounded: a ghost record would otherwise hang here
		NET_ProcessResultWithTimeout( queryRef, 2000 );
		DNSServiceRefDeallocate( queryRef );
	}
}
	

void DNSServiceBrowseReplyCallback(
								   DNSServiceRef sdRef, 
								   DNSServiceFlags flags, 
								   uint32_t interfaceIndex, 
								   DNSServiceErrorType errorCode, 
								   const char *serviceName, 
								   const char *regtype, 
								   const char *replyDomain, 
								   void *context ) {
	
	Log_Printf("DNSServiceBrowseReplyCallback\n");
	
	//Log_Printf( "DNSServiceBrowseReplyCallback %s: interface:%i name:%s regtype:%s domain:%s\n", (flags & kDNSServiceFlagsAdd) ? "ADD" : "REMOVE",interfaceIndex, serviceName, regtype, replyDomain );
	
	service_t* service ;
	
	if ( flags & kDNSServiceFlagsAdd )
	{
		// add it to the list
		if ( interfaceIndex == 1 )
		{
			Log_Printf( "Not adding service on loopback interface.\n" );
			return;
		}

		// Bookkeeping, bounds-checked: on iOS the interface index can exceed the
		// table (en0 is small, but awdl/llw/utun interfaces are not) and the old
		// unchecked serviceInterfaces[interfaceIndex] write was a latent overflow.
		if ( interfaceIndex < MAX_SERVICE_INTEFACES )
		{
			service = &serviceInterfaces[interfaceIndex];
			strncpy( service->browseName, serviceName, sizeof( service->browseName ) -1 );
			strncpy( service->browseRegtype, regtype, sizeof( service->browseRegtype ) -1 );
			strncpy( service->browseDomain, replyDomain, sizeof( service->browseDomain ) -1 );
			service->interfaceIndex = interfaceIndex;
		}

		char	interfaceName[IF_NAMESIZE];
		if_indextoname(interfaceIndex, interfaceName);

		// Resolve only peers seen on our LAN interface (en0), as the original did.
		if ( !strcmp(INTERFACE_NAME, interfaceName) )
		{
			DNSServiceRef	resolveRef2;
			DNSServiceErrorType err = DNSServiceResolve (
														 &resolveRef2,
														 kDNSServiceFlagsForceMulticast,	// always on local link
														 interfaceIndex ,		// the interface it was seen on
														 serviceName,
														 regtype,
														 replyDomain,
														 DNSServiceResolveReplyCallback,
														 NULL			/* context */
														 );

			if ( err != kDNSServiceErr_NoError ) {
				sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETPEERPIP), "DNSServiceResolve error");

			} else {
				// bounded wait: a ghost record would otherwise hang the menu here
				NET_ProcessResultWithTimeout( resolveRef2, 2000 );
				DNSServiceRefDeallocate( resolveRef2 );
			}
		}

	}
	else 
	{
		// remove it from the list
		for ( int i = 0 ; i < MAX_SERVICE_INTEFACES ; i++ ) 
		{
			if ( serviceInterfaces[i].interfaceIndex == interfaceIndex ) 
			{
				serviceInterfaces[i].interfaceIndex = -1;
			}
		}
	}
	
	
	// Need to resolved
}

int NET_ResolveNetworkServer( )
{
	fd_set	set;
	int	socket;
	struct timeval tv;

	// Re-issue the browse each call (as the original working build did), deallocating
	// the previous ref first so we don't leak one per frame. (Browse-once was tried
	// alongside register-once and is reverted for the same reason.) Interface 0 (all)
	// is correct for browsing -- only registration needed the specific en0 index.
	if ( browseRef ) { DNSServiceRefDeallocate( browseRef ); browseRef = 0; }

	Log_Printf("NET_ResolveNetworkServer: DNSServiceBrowse\n");
	DNSServiceErrorType err = DNSServiceBrowse (
												&browseRef,
												0,					/* flags */
												0,					/* all interfaces */
												serviceName,
												NULL,				/* domain */
												DNSServiceBrowseReplyCallback,
												NULL				/* context */
												);
	if ( err != kDNSServiceErr_NoError ) {
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETPEERPIP), "DNSServiceBrowse error");
		browseRef = 0;
		return 0;
	}

	// Read the browse reply (BLOCKS up to 5s, the proven mechanism): when a server
	// appears, DNSServiceProcessResult fires DNSServiceBrowseReplyCallback, which
	// resolves it, and DNSServiceResolveReplyCallback fills net.peerAddr + serverAddResolved.
	socket = DNSServiceRefSockFD( browseRef );
	if ( socket < 0 )		// fd 0 is VALID; only -1 means error
		return 1;

	FD_ZERO( &set );
	FD_SET( socket, &set );
	tv.tv_sec = 5;
	tv.tv_usec = 0;

	if ( select( socket+1, &set, NULL, NULL, &tv ) > 0 )
		DNSServiceProcessResult( browseRef );

	return 1;

}


//char isInitialized(void)
//{
//	return (netType != UNKNOWN && (netType == SERVER || (netType == CLIENT && serverAddResolved)));
//}


void NET_CreateSocket(void)
{
	struct sockaddr_in bindingIp_address;
	
	Log_Printf("NET_CreateSocket\n");
	
	bzero(&bindingIp_address, sizeof(bindingIp_address));
	//ip_address = NET_GetAddressForInterfaceName(INTERFACE_NAME);
	bindingIp_address.sin_family = AF_INET;
	bindingIp_address.sin_port = htons( PORT_NUMBER );
	//	bindingIp_address.sin_addr.s_addr = htonl(INADDR_ANY);
	//inet_pton(AF_INET,"10.0.1.3",&bindingIp_address.sin_addr.s_addr);
	
	
	// Create socket and bind it to IP+Port
	net.udpSocket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( net.udpSocket == -1 ) 
	{
		Log_Printf( "UDP socket failed: %s\n", strerror( errno ) );
		return ;
	}
	
	
	
	// enable non-blocking IO
	//int x;
	//x = fcntl(udpSocket,F_GETFL,0);
	if (fcntl(net.udpSocket,F_SETFL, O_NONBLOCK)== -1 ) {
		Log_Printf( "UDP fcntl failed: %s\n", strerror( errno ) );
		close( net.udpSocket );
		
		return ;
	}
	
	
	//if (netType == SERVER)
	int errorCheck;
	errorCheck = bind( net.udpSocket, (struct sockaddr *)&bindingIp_address, sizeof( struct sockaddr_in ) );
	if (errorCheck == -1)
	{
		Log_Printf("UDP bind failed: %s\n", strerror( errno ) );
		return ;
	}
	
	Log_Printf("[NETCHANNEL ] Bind on %s:%hud\n",inet_ntoa(bindingIp_address.sin_addr),bindingIp_address.sin_port);
	
	
}



// Send a level-load handshake command to the peer. On the LAN these are unreliable
// UDP datagrams, so we send a small BURST -- a single dropped handshake packet used
// to deadlock the level transition with the timer paused ("decor frozen, music still
// playing"). The receiver ignores duplicates once its state has advanced. Online
// (GKMatch) packets are already reliable, so one copy is enough.
static void NET_SendSetupCmd(char cmdType)
{
	net_packet_t p;
	int i, copies = NET_IsOnline() ? 1 : 6;
	p.type = SETUP_PACKET;
	p.command.type = cmdType;
	p.numRedundant = 0;
	p.shipChoice   = gShipChoice;	// carry our Custom loadout in every setup packet
	p.bulletColor  = gBulletColor;
	for (i = 0; i < copies; i++)
	{
		p.sequenceNumber = net.lastSentSequenceNumber++;
		p.ackSequenceNumber = net.lastReceivedSequenceNumber;
		NET_TransportSend(&p, sizeof(p));
	}
}

// Per-player custom loadout sync: each end sends its own Custom choice (ship +
// bullet colour) in its setup packets; once a setup packet from the peer arrives,
// both ends hold both choices and apply the same deterministic rule -- so the two
// devices render the match identically without negotiation.
static void NET_StorePeerLoadout(const net_packet_t* packet, int peerId)
{
	int ship  = packet->shipChoice;
	int color = packet->bulletColor;

	// Clamp (protects against a mismatched build on the other end).
	if (ship  < 0 || ship  >= NUM_SHIP_CHOICES)  ship  = peerId;	// classic P1/P2 ship
	if (color < 0 || color >= NUM_BULLET_COLORS) color = peerId;	// classic red/blue

	gMPShipChoice[peerId]  = ship;
	gMPBulletColor[peerId] = color;

	// Our own slot.
	gMPShipChoice[!peerId]  = (gShipChoice  >= 0 && gShipChoice  < NUM_SHIP_CHOICES)  ? gShipChoice  : !peerId;
	gMPBulletColor[!peerId] = (gBulletColor >= 0 && gBulletColor < NUM_BULLET_COLORS) ? gBulletColor : !peerId;

	// Same bullet colour on both sides would make the two players' shots
	// indistinguishable: shift PLAYER TWO's colour. Both ends run this same rule on
	// the same data, so they agree.
	if (gMPBulletColor[0] == gMPBulletColor[1])
		gMPBulletColor[1] = (gMPBulletColor[1] + 1) % NUM_BULLET_COLORS;

	Log_Printf("Loadout sync: P1 ship=%d color=%d | P2 ship=%d color=%d\n",
	           gMPShipChoice[0], gMPBulletColor[0], gMPShipChoice[1], gMPBulletColor[1]);
}

// Peer liveness: in a running match the peer sends every frame, so a long silence
// means it quit, was backgrounded, or lost the network.
#define NET_PEER_TIMEOUT_MS 5000
static int lastPeerPacketTime = 0;

// The peer is gone mid-match: end the match cleanly on THIS side too. Free the
// session, reload the menu scene NOW (otherwise the abandoned game keeps simulating
// behind the menu), then show a notice telling the player what happened.
void NET_OnPeerLost(void)
{
	Log_Printf("NET_OnPeerLost\n");

	NET_Free();						// also clears the multiplayer text lines

	numPlayers = 1;
	controlledPlayer = 0;
	engine.mode = DE_MODE_SINGLEPLAYER;

	dEngine_RequireSceneId(0);
	dEngine_CheckState();			// load the menu scene now (sets the HOME menu)

	MENU_Set(MENU_MULTIPLAYER);		// then show the notice over it
	sprintf(MENU_GetMultiplayerTextLine(0), "Connection lost !");
	sprintf(MENU_GetMultiplayerTextLine(2), "The other player");
	sprintf(MENU_GetMultiplayerTextLine(3), "left the game.");
}

void Net_ProcessSetupPacket(void)
{
	// Read all incoming UDP datagrams
	socklen_t len ;
	//struct sockaddr incomingAdd;
	struct sockaddr_in incomingAdd;
	int byteReceived;
	net_packet_t* packet;
	uchar packetConsumed = 0;

//	Log_Printf("Net_ProcessSetupPacket\n");
	
	bzero(&incomingAdd, sizeof(incomingAdd));
	len = sizeof(incomingAdd);
	//Log_Printf("Net_ProcessSetupPacket()\n");
	
	byteReceived = NET_TransportRecv(net.buffer, sizeof(net.buffer), &incomingAdd);
	if (byteReceived == -1)
	{
		if (errno != EAGAIN )
			sprintf(MENU_GetMultiplayerTextLine(4),"Error recvfrom:%d %s.\n",errno,strerror( errno ));
	
		//Log_Printf("No packets.\n");
		return;
	}

	Log_Printf("Net_ProcessSetupPacket() read %d bytes\n",byteReceived);

	packet = (net_packet_t*)net.buffer;
		
	Log_Printf("packet->type=%d\n",packet->type);
	
	if (packet->type != SETUP_PACKET)
	{
		Log_Printf("Not a setup packet.\n");
		return;
	}
		
	if (packet->sequenceNumber <= net.lastReceivedSequenceNumber)
	{
		Log_Printf("Old packet.\n");
		return;
	}
		
		

//	net.numDropedPackets += 1 - packet->sequenceNumber - net.lastReceivedSequenceNumber;
		
	net.lastReceivedSequenceNumber = packet->sequenceNumber;
		
	sprintf(MENU_GetMultiplayerTextLine(4),"Received setup packet %i.\n",packet->type);
		
	//outPacket.cmd = NET_CMD_NOOP;
	
	if (net.type == NET_SERVER && net.state == NET_STARTED &&  packet->command.type == NET_CMD_LOAD_NEXT_LEVEL)
	{
		packetConsumed=1;
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETLASTRECEIVED), "LAST RECV=NET_CMD_LOAD_NEXT_LEVEL");
		
		//Save peer informations to send replies as this is the first time the server will hear of the client
		//(LAN only: online, the peer is the GKMatch and there is no sockaddr to learn).
		if (!NET_IsOnline())
		{
			memcpy(&net.peerAddr,&incomingAdd,sizeof(incomingAdd));
			sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETPEERPIP),"PEER IP %s",inet_ntoa(net.peerAddr.sin_addr));
		}
		
		//Perform preload, pause music, pause timer
		dEngine_RequireSceneId((engine.sceneId + 1) % engine.numScenes);	// (parenthesized: "+ 1 % n" is just "+ 1")

		numPlayers=2;
		controlledPlayer=0;

		// The client's packet carries its Custom loadout; store both loadouts (and
		// dedupe colours) BEFORE the scene load below applies the ship models.
		NET_StorePeerLoadout(packet, 1);

		dEngine_CheckState();
		
//		Log_Printf("POST Player1=%p\n",players[0].entity.material);
//		Log_Printf("POST Player2=%p\n",players[1].entity.material);		
		
		SND_PauseSoundTrack();
		Timer_Pause();
		net.state = NET_PRELOADED;
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETSTATE), "state=NET_PRELOADED.\n");
		
		Log_Printf("Client loaded level, but Timer still paused sending NET_CMD_LOAD_NEXT_LEVEL.\n");
		
		// Trigger preload on the other end as well by sending a NET_CMD_LOAD_NEXT_LEVEL to peer
		NET_SendSetupCmd(NET_CMD_LOAD_NEXT_LEVEL);

		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETLASTSENT), "LAST SENT=NET_CMD_LOAD_NEXT_LEVEL");
	}
	
	if (net.type == NET_CLIENT && net.state == NET_STARTED &&  packet->command.type == NET_CMD_LOAD_NEXT_LEVEL)
	{
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETLASTRECEIVED), "LAST RECV=NET_CMD_LOAD_NEXT_LEVEL");
		packetConsumed=1;
		
		//Perform preload, pause music, pause timer
		dEngine_RequireSceneId((engine.sceneId + 1) % engine.numScenes);	// (parenthesized: "+ 1 % n" is just "+ 1")

		numPlayers=2;
		controlledPlayer=1;

		// The server's packet carries its Custom loadout; store both loadouts (and
		// dedupe colours) BEFORE the scene load below applies the ship models.
		NET_StorePeerLoadout(packet, 0);

		dEngine_CheckState();
		
		SND_PauseSoundTrack();
		Timer_Pause();
		net.state = NET_PRELOADED;
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETSTATE), "state=NET_PRELOADED.\n");

		Log_Printf("Client loaded level, but Timer still paused sending NET_CMD_NOTIFY_LOADED.\n");
		
		// Tell server we are ready to start by sending NET_CMD_NOTIFY_LOADED
		NET_SendSetupCmd(NET_CMD_NOTIFY_LOADED);

		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETLASTSENT), "LAST SENT=NET_CMD_NOTIFY_LOADED");
		
	}
	
	if (net.type == NET_SERVER && net.state == NET_PRELOADED &&  packet->command.type == NET_CMD_NOTIFY_LOADED)
	{
		packetConsumed=1;
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETLASTRECEIVED), "LAST RECV=NET_CMD_NOTIFY_LOADED");
		
		net.state = NET_RUNNING;
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETSTATE), "state=NET_RUNNING.\n");
		//Log_Printf(MENU_GetMultiplayerTextLine(MESSAGE_NETSTATE), "state=NET_RUNNING.\n");
		//Send NET_CMD_START_LEVEL
		
		//Start level (unpause time, unpause music)
		SND_ResumeSoundTrack();
		Timer_resetTime();
		Timer_Resume();
		lastPeerPacketTime = simulationTime;	// fresh peer-liveness window (just reset to 0)
		
		Log_Printf("Server Received NET_CMD_NOTIFY_LOADED, starting and asking client to start as well: NET_CMD_START_LEVEL.\n");
		
		MENU_Set(MENU_NONE);
		
		//Trigger client start
		NET_SendSetupCmd(NET_CMD_START_LEVEL);
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETLASTSENT), "LAST SENT=NET_CMD_START_LEVEL");
	}
	
	if (net.type == NET_CLIENT && net.state == NET_PRELOADED &&  packet->command.type == NET_CMD_START_LEVEL)
	{
		packetConsumed=1;
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETLASTRECEIVED), "LAST RECV=NET_CMD_START_LEVEL");
		
		net.state = NET_RUNNING;
		//Start level (unpause time, unpause music)
		SND_ResumeSoundTrack();
		Timer_resetTime();
		Timer_Resume();
		lastPeerPacketTime = simulationTime;	// fresh peer-liveness window (just reset to 0)
		
		Log_Printf("Client Received NET_CMD_START_LEVEL, starting.\n");
		
		MENU_Set(MENU_NONE);
		
		
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETSTATE), "state=NET_RUNNING.\n");
		//Log_Printf(MENU_GetMultiplayerTextLine(MESSAGE_NETSTATE), "state=NET_RUNNING.\n");
		
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETLASTSENT), "LAST SENT=NET_CMD_LOAD_NEXT_LEVEL");
	}
	
	//if(outPacket.cmd != NET_CMD_NOOP)
	//{
		
	//}
	
	if (!packetConsumed)
		Log_Printf("Packet type=%d was not consumed.",packet->command.type );
}

#define isInitialized (net.state == NET_RUNNING && (net.type == NET_SERVER || (net.type == NET_CLIENT && net.serverAddResolved)))
void NET_Setup(void)
{
	net_packet_t registerPacket;
	struct sockaddr_in localAddress;
	
	//Log_Printf("NET_Setup\n");
	
	if (!net.setupRequested)
	{
	//	Log_Printf("!setupRequested\n");
		return ;
	
	}
	if (isInitialized)
	{
	//	Log_Printf("isInitialized\n");
		return ;
	}
	
	//Log_Printf("NET_Setup\n");
	
	//NET_Free();
	//buffer = calloc(, sizeof(uchar));
	
	
	if (!NET_IsOnline() && net.type == NET_UNKNOWN && !NET_IsNetworkAvailable())
	{
		sprintf(MENU_GetMultiplayerTextLine(0), "No WIFI network available !");
		sprintf(MENU_GetMultiplayerTextLine(1), " ");
		sprintf(MENU_GetMultiplayerTextLine(2), "Make sure WIFI is enabled" );
		sprintf(MENU_GetMultiplayerTextLine(3), "and the device is connected." );
	
		return;	
	}
	
	
	if (!NET_IsOnline() && net.type == NET_UNKNOWN && !NET_CheckServerAvailability())
	{
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETMYIP), "Error while NET_CheckServerAvailability.\n");
		return ;
	}
	
	
	if (net.type == NET_CLIENT && !net.serverAddResolved)
	{
		if (!NET_ResolveNetworkServer())
		{
			sprintf(MENU_GetMultiplayerTextLine(0),   "Unable to find the server !");
			sprintf(MENU_GetMultiplayerTextLine(1),   " ");
			sprintf(MENU_GetMultiplayerTextLine(2), "Restart the server then try");
			sprintf(MENU_GetMultiplayerTextLine(3), "connecting again.");
			return ;
		}
		//sprintf(MENU_GetMultiplayerTextLine(0), "Resolved server :) !\n");
		//sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETPEERPIP), "Server IP: %s !",inet_ntoa(net.peerAddr.sin_addr));
	}
	

	if (!NET_IsOnline() && MENU_GetMultiplayerTextLine(MESSAGE_NETMYIP)[0] == '\0')
	{
		localAddress =  NET_GetAddressForInterfaceName(INTERFACE_NAME);
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETMYIP),"Local IP @:'%s'",inet_ntoa(localAddress.sin_addr));
	}


	if (!NET_IsOnline() && net.udpSocket == 0)
	{
		NET_CreateSocket();
		Log_Printf("File descriptor UDP socket = %d.\n",net.udpSocket);
	}
	
	//Process to setup
	//Log_Printf("net.state =%d\n",net.state );
	
	if (net.state == NET_STARTED)
	{
		//We need to register
		if (net.type == NET_CLIENT)
		{
			registerPacket.sequenceNumber = net.lastSentSequenceNumber++;
			registerPacket.ackSequenceNumber = net.lastReceivedSequenceNumber;
			registerPacket.type = SETUP_PACKET;
			registerPacket.command.type = NET_CMD_LOAD_NEXT_LEVEL;
			registerPacket.numRedundant = 0;
			registerPacket.shipChoice   = gShipChoice;	// carry our Custom loadout
			registerPacket.bulletColor  = gBulletColor;
			NET_TransportSend(&registerPacket, sizeof(registerPacket));
			//sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETLASTSENT), "LAST SENT=NET_CMD_LOAD_NEXT_LEVEL");
			
		}
		
	}
	
	if (net.state != NET_RUNNING)
	{
		Net_ProcessSetupPacket();
	}
	else
	{
		Log_Printf("Stoping setup, as we reached NET_RUNNING\n");
		net.setupRequested = 0;
		
		memset(&fakeCmdHistory,0,sizeof(fakeCmdHistory_t));
		
		memset(&cmdHistory,0,sizeof(cmdHistory_t));

	}
	
	
}

char NET_IsInitialized()
{
	return isInitialized;
}

uint outSequenceNumber;
uint inSequenceNumber;
typedef struct net_message_t
{
	uint sequenceNumber;
	uchar playerId;
	uchar type;
	void* payload;
	
} net_message_t;

#define deltaT 2
command_t* NET_GenerateFakeCMD(void)
{
	static command_t cmd;
	command_t* last;
	command_t* lastMinusOne;
	command_t* lastMinusTwo;
	vec3_t delta1;
	vec3_t delta2;
	float sumDelta;
	
	last = &cmdHistory.array[(cmdHistory.ptr -1) & (MAX_CMD_HISTORY-1) ];
	lastMinusOne = &cmdHistory.array[(cmdHistory.ptr -2) & (MAX_CMD_HISTORY-1) ];
	lastMinusTwo = &cmdHistory.array[(cmdHistory.ptr -3) & (MAX_CMD_HISTORY-1) ];
	
	delta1[X] =      lastMinusOne->delta[X] - lastMinusTwo->delta[X];
	delta1[Y] =      lastMinusOne->delta[Y] - lastMinusTwo->delta[Y];
	delta1[deltaT] = lastMinusOne->time     - lastMinusTwo->time;
	
	delta2[X] =      last->delta[X] - lastMinusOne->delta[X];
	delta2[Y] =      last->delta[Y] - lastMinusOne->delta[Y];
	delta2[deltaT] = last->time     - lastMinusOne->time;
	
	sumDelta = delta1[deltaT]+delta2[deltaT];
	if (sumDelta == 0)
	{
		delta1[X]=0;
		delta1[Y]=0;
		delta2[X]=0;
		delta2[Y]=0;
		sumDelta=0.001f;
	}
	//Need need to use the three previous cmds to generate two deltas.
	
	
	cmd.type = NET_RTM_COMMAND;
	cmd.time = simulationTime;
	cmd.playerId = !controlledPlayer;
	cmd.buttons = 0;
	cmd.delta[X] = delta1[X]*delta1[deltaT]/sumDelta + delta2[X]*delta2[deltaT] /sumDelta;
	cmd.delta[Y] = delta1[Y]*delta1[deltaT]/sumDelta + delta2[Y]*delta2[deltaT] /sumDelta;
	cmd.buttons = last->buttons;
	
	return &cmd;
}

void NET_AddFakeToHistory(command_t* cmd)
{
	if (fakeCmdHistory.num == MAX_FAKE_CMD_HISTORY-1)
		return;
	
	fakeCmdHistory.stack[fakeCmdHistory.num].delta[X] = cmd->delta[X];
	fakeCmdHistory.stack[fakeCmdHistory.num].delta[Y] = cmd->delta[Y];
	fakeCmdHistory.stack[fakeCmdHistory.num].time = cmd->time;
	
	fakeCmdHistory.num++;
}

void NET_AddCMDToHistory(command_t* cmd)
{
	command_t* emptySlot;
	
	emptySlot = &cmdHistory.array[cmdHistory.ptr];
	emptySlot->delta[X] = cmd->delta[X];
	emptySlot->delta[Y] = cmd->delta[Y];
	emptySlot->time = simulationTime;
	emptySlot->buttons = cmd->buttons;
	cmdHistory.ptr = (cmdHistory.ptr + 1) & (MAX_CMD_HISTORY-1);
}


void NET_Receive(void)
{
	int byteReceived = 0;
	socklen_t len ;
	net_packet_t rcv_packet;
	command_buffer_t* cmdBuffer;
	uchar numDeltaUpdateRecv=0;
	int i;
	command_t* cmd;
	
	//Log_Printf("NET_Receive\n");
	
	if (!isInitialized)
		return;
	
	cmdBuffer = &commandsBuffers[!controlledPlayer];
	commandsBuffers[!controlledPlayer].numCommands = 0;

	
	while (1)
	{
		byteReceived = NET_TransportRecv(&rcv_packet, sizeof(net_packet_t), NULL);
		
		if (byteReceived == -1)
		{
			if (errno != EAGAIN )
				sprintf(MENU_GetMultiplayerTextLine(4),"Error recvfrom:%d %s.\n",errno,strerror( errno ));
			break;
		}

		lastPeerPacketTime = simulationTime;	// any packet proves the peer is alive

		// Ignore handshake leftovers here (they're handled by Net_ProcessSetupPacket).
		// Their command fields are uninitialized, so applying them as input would glitch.
		if (rcv_packet.type == SETUP_PACKET)
			continue;

		// A runtime packet carries its current command plus a few redundant (previous)
		// commands. Walk them oldest-first and apply every command whose sequence we
		// haven't seen yet -- this recovers inputs lost to dropped packets.
		int nred = rcv_packet.numRedundant;
		if (nred < 0)                  nred = 0;
		if (nred > NET_REDUNDANT_CMDS) nred = NET_REDUNDANT_CMDS;

		for (i = 0; i <= nred; i++)		// [0..nred-1] = redundant (old->new); i==nred = current
		{
			command_t*	c;
			int			seq;
			int			slot;

			if (i < nred) { c = &rcv_packet.redundant[i]; seq = rcv_packet.redundantSeq[i]; }
			else          { c = &rcv_packet.command;      seq = rcv_packet.sequenceNumber;  }

			if (seq <= net.lastReceivedSequenceNumber)
				continue;			// already applied this command

			net.numDropedPackets += (seq - (1 + net.lastReceivedSequenceNumber));
			net.lastReceivedSequenceNumber = seq;

			//Safe guard against data corruption
			if (c->playerId < 2 && commandsBuffers[!controlledPlayer].numCommands < COMMAND_BUFFER_SIZE-1)
			{
				slot = commandsBuffers[!controlledPlayer].numCommands;
				memcpy(&commandsBuffers[!controlledPlayer].cmds[slot], c, sizeof(command_t));

				if (rcv_packet.type == NET_RTM_COMMAND)
				{
					NET_AddCMDToHistory(c);
					numDeltaUpdateRecv += 1;
				}

				commandsBuffers[!controlledPlayer].cmds[slot].time = simulationTime;
				commandsBuffers[!controlledPlayer].numCommands++;
			}
		}
	}
	
	
	Log_Printf("t=%d,numDeltaUpdateRecv=%d\n",simulationTime,numDeltaUpdateRecv);

	// Peer liveness: the peer sends every frame while the match runs, so a long
	// silence means it quit / was backgrounded / lost the network. End the match
	// cleanly instead of leaving this player in an abandoned game.
	if (simulationTime - lastPeerPacketTime > NET_PEER_TIMEOUT_MS)
		NET_OnPeerLost();

	return;
	
	// If no update was received, we need to create a fake one in order to avoid jerky mouvments.
	if (numDeltaUpdateRecv == 0)
	{
		
		//Gen fake command based on extrapolation
		cmd = NET_GenerateFakeCMD();
		
		//Add it to history
		NET_AddFakeToHistory(cmd);
		
		//Log_Printf("t=%d, missing deltaCmd: fake=%.4f,%.4f\n",simulationTime,cmd->delta[X],cmd->delta[Y]);
		if (commandsBuffers[!controlledPlayer].numCommands < COMMAND_BUFFER_SIZE-1)
		{
			memcpy(&commandsBuffers[!controlledPlayer].cmds[commandsBuffers[!controlledPlayer].numCommands], cmd, sizeof(command_t));
			cmdBuffer->cmds[commandsBuffers[!controlledPlayer].numCommands].time = simulationTime;
			commandsBuffers[!controlledPlayer].numCommands++;
		}
		
	}
	else 
	{
		//Log_Printf("t=%d: %d deltaCmd(s).\n",simulationTime,numDeltaUpdateRecv);
		// If we have received more than 1 update, we can undo that many fake commands previously generated
		for (i=1; i < numDeltaUpdateRecv && fakeCmdHistory.num >0 && commandsBuffers[!controlledPlayer].numCommands < COMMAND_BUFFER_SIZE-1; i++) 
		{
			//Log_Printf("t=%d: Undoing 1 fake deltaCmd.\n",simulationTime);
			cmd = &cmdBuffer->cmds[commandsBuffers[!controlledPlayer].numCommands];
			cmd->time = simulationTime;
			cmd->type = NET_RTM_COMMAND;
			cmd->delta[X] = -fakeCmdHistory.stack[fakeCmdHistory.num-1].delta[X];
			cmd->delta[Y] = -fakeCmdHistory.stack[fakeCmdHistory.num-1].delta[Y];
			cmd->buttons = 0;
			cmd->playerId = !controlledPlayer;
			commandsBuffers[!controlledPlayer].numCommands++;
			
			
			
			fakeCmdHistory.num--;
		} 
	}
 
	
	
	
}

int lastFullUpdateTime = 0;
void NET_Send()
{
	net_packet_t send_packet;
	int i, seq;

	if (!isInitialized)
		return;

	seq = net.lastSentSequenceNumber++;

	send_packet.type = RUNTIME_PACKET;
	send_packet.command.type = NET_RTM_COMMAND;
	send_packet.sequenceNumber = seq;
	send_packet.ackSequenceNumber = net.lastReceivedSequenceNumber;
	memcpy(&send_packet.command, &toSend, sizeof(command_t));

	// Attach the previous commands as redundancy (oldest first) so a lost packet's
	// input is recovered from this one.
	send_packet.numRedundant = sentCount;
	for (i = 0; i < sentCount; i++)
	{
		send_packet.redundant[i]    = sentCmds[i];
		send_packet.redundantSeq[i] = sentSeqs[i];
	}

	NET_TransportSend(&send_packet, sizeof(net_packet_t));

	// Push the command we just sent into the redundancy ring (keep the last N, oldest first).
	if (sentCount < NET_REDUNDANT_CMDS)
	{
		sentCmds[sentCount] = toSend;
		sentSeqs[sentCount] = seq;
		sentCount++;
	}
	else
	{
		for (i = 1; i < NET_REDUNDANT_CMDS; i++) { sentCmds[i-1] = sentCmds[i]; sentSeqs[i-1] = sentSeqs[i]; }
		sentCmds[NET_REDUNDANT_CMDS-1] = toSend;
		sentSeqs[NET_REDUNDANT_CMDS-1] = seq;
	}

	// Periodic absolute-position resync to correct residual drift. More frequent than
	// before (was 1000ms) since online packet loss lets drift build up faster.
	if (simulationTime - lastFullUpdateTime > 300)
	{
		// We are reusing the delta field to contain absolute position :/ No clean I know.
		send_packet.command.type = NET_RTM_ABS_UPDATE;
		send_packet.command.delta[X] = players[controlledPlayer].ss_position[X];
		send_packet.command.delta[Y] = players[controlledPlayer].ss_position[Y];
		send_packet.sequenceNumber = net.lastSentSequenceNumber++;
		send_packet.numRedundant = 0;		// the ABS correction packet carries no redundancy
		NET_TransportSend(&send_packet, sizeof(net_packet_t));
		lastFullUpdateTime = simulationTime;
	}
}
		
void Net_SendDie(command_t* command)
{

	net_packet_t send_packet;

	Log_Printf("Net_SendDie\n");
	
	send_packet.type = NET_RUNNING;
	send_packet.sequenceNumber = net.lastSentSequenceNumber++;
	send_packet.ackSequenceNumber = net.lastReceivedSequenceNumber;
	send_packet.numRedundant = 0;		// no redundant commands on the death packet
	memcpy(&send_packet.command,command,sizeof(command_t));

	NET_TransportSend(&send_packet, sizeof(net_packet_t));
	
}

int NET_Init(void)
{
	Log_Printf("NET_Init\n");
	NET_Free();
	net.setupRequested = 1;
	return 1;
}

void NET_OnNextLevelLoad(void)
{
	Log_Printf("NET_OnNextLevelLoad\n");
	net.setupRequested = 1;
	net.state = NET_STARTED;	
}

char NET_IsRunning(void)
{
	Log_Printf("NET_IsRunning\n");
	return (net.state == NET_RUNNING);
}

uint NET_GetDropedPackets(void)
{
	return net.numDropedPackets;
}


#endif