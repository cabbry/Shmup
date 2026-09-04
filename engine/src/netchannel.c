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
#include "text.h"				// DYN_TEXT_AddText: the on-screen "PLAYER n LEFT" notice

// The network version was designed on iOS with Unix socket. This part still needs to be ported using winsock32.
#if defined(WIN32) || defined(ANDROID) || defined(LINUX)
	int NET_Init(void){return 1;}
	void NET_Setup(void){}
	void NET_Receive(void){}
	void NET_Send(void){}
	void NET_Free(void){}
	char NET_IsInitialized(){return 1;}
	void Net_SendDie(command_t* command){}
	int  NET_DeathAuthority(void){return 0;}
	void NET_PlayerHit(int seat){}
	void NET_OnNextLevelLoad(void){}
	char NET_IsRunning(void){return 0;}
	char NET_IsInMatch(void){return 0;}
	void NET_SetPartyTarget(int n){}
	uint NET_GetDropedPackets(void){return 0;}
	void NET_StartOnlineMatch(int mySeat, int numSeats){}
	void NET_AbortOnlineMatch(void){}
	void NET_OnPeerLost(void){}
	void NET_OnSeatLost(int seat){}
	void NET_OnNetworkDataFrom(int senderSeat, const void* data, int len){}
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
// v2 P0: death packets used to go out with type NET_RUNNING (3) -- a value
// from an UNRELATED enum that collides with the inner NET_RTM_COMMAND (3),
// which made the comparison at the receive loop's history hook always-wrong
// (it fed the dead prediction code with deaths instead of commands). Own,
// non-overlapping value; still != RUNTIME_PACKET so the transport sends it
// reliable, and != SETUP_PACKET so the receive loop processes its command.
#define DEATH_PACKET 4
	char type;

	int sequenceNumber;
	int ackSequenceNumber;

	// v2 P2: the two fields here were dead weight since 2010 ("time"/"ackTime",
	// never written, never read). Recycled -- same offsets, same packet size:
	// - senderSeat: who this packet claims to come from; validated against the
	//   transport-level sender, never trusted alone.
	// - protoVersion: v1 builds left this uninitialized; v2 stamps NET_PROTO.
	//   A mismatch (e.g. a v1.8 tester joining a v2 lobby) is dropped at the
	//   door instead of desyncing mid-match.
#define NET_PROTO 3		// 3: v2.0.9 host authority on deaths (DIE_REQ/DIE_ORDER replace DIED)
	int senderSeat;
	int protoVersion;

	
//#define NET_CMD_NOOP 0
#define NET_CMD_LOAD_NEXT_LEVEL 0
#define NET_CMD_NOTIFY_LOADED 1
#define NET_CMD_START_LEVEL 2
// v2 P2: the lobby heartbeat. While the host waits for the rest of the party
// it has nothing to say -- and a silent host looks exactly like a dead host to
// a client's handshake watchdog (the rig caught precisely this: with four
// players, the two early joiners tore their sessions down while the host was
// still waiting on the fourth). This packet says "still here, still waiting".
#define NET_CMD_WAITING 3

	command_t command;

	// Command redundancy (anti packet-loss): each runtime packet also carries the
	// previous few commands + their sequence numbers, so a lost packet's input is
	// recovered from the next one (the receiver applies any command it hasn't seen).
	// Not used by setup/ABS packets (numRedundant = 0).
#define NET_REDUNDANT_CMDS 2
	int			numRedundant;
	int			redundantSeq[NET_REDUNDANT_CMDS];
	command_t	redundant[NET_REDUNDANT_CMDS];

	// Custom loadout (setup packets only). v2 P2: a per-seat TABLE (proto v2) --
	// a joining seat fills only its own slot; the host's START_LEVEL broadcasts
	// the complete, colour-deduped table so every client renders every ship
	// identically (clients never hear each other directly during the handshake).
	int			shipChoice[MAX_NUM_PLAYERS];
	int			bulletColor[MAX_NUM_PLAYERS];

	// v2 P2: bitmask of the seats still in the party (setup packets). Clients
	// never hear each other, so only the host knows that a seat timed out
	// during the handshake -- without this they would keep a ghost ship on
	// screen (and mirror its life counter) for a player who never arrived.
	int			activeMask;

	// v2 P4: the sender's LAN roster (host byte order, seat order, zero-padded).
	// Bonjour discovery is per-device and lossy: without this, device B can be
	// unaware of device C entirely, seat everyone differently from A, and the
	// party splits into two incompatible simulations (or deadlocks, when the
	// seat numbers disagree and every packet fails its identity check). Every
	// lobby packet gossips what its sender knows; the rosters converge before
	// the settle window can close, so the seat table is common ground.
	unsigned int rosterIps[MAX_NUM_PLAYERS];

} net_packet_t;

// Outgoing command-redundancy ring: the last few runtime commands we sent (oldest
// first), echoed in each packet's redundant[] so the peer can recover an input lost
// to a dropped packet. One ring is enough for N receivers: it carries OUR stream.
static command_t	sentCmds[NET_REDUNDANT_CMDS];
static int			sentSeqs[NET_REDUNDANT_CMDS];
static int			sentCount = 0;

// ------------------------------------------------------------------------------
//  v2 P2: per-peer runtime state, seat-indexed (our own seat's entry unused).
//  Everything that used to be a single "the peer" scalar lives here: sequence
//  tracking, liveness, and the handshake barrier flags the host counts.
// ------------------------------------------------------------------------------
typedef struct net_peer_t
{
	int				active;			// seated in this match and still alive
	unsigned int	lastRxSeq;		// last sequence applied from this seat
	int				lastPacketTime;	// liveness clock (simulationTime of last packet)
	int				lastSetupFrame;	// handshake liveness, in FRAMES (see below)
	char			joined;			// host barrier: join request seen
	char			loaded;			// host barrier: NOTIFY_LOADED seen

	// v2: de-jitter queue for this seat's MOVEMENT commands. WiFi delivers in
	// bursts: two commands one frame, none the next -- applied raw that is a
	// double-speed jump then a freeze, the "saccade" of the first LAN
	// playtests. The sender emits exactly one command per frame, so the
	// smooth playback is one per frame here too; the queue only absorbs the
	// bunching. Depth capping keeps the added latency bounded (see the
	// drain in NET_Receive).
#define NET_JITTER_Q 16
	command_t		jq[NET_JITTER_Q];
	int				jqHead;			// next write
	int				jqTail;			// next read
} net_peer_t;

// v2 P2: handshake liveness is counted in FRAMES, not milliseconds. During the
// handshake the sim clock is PAUSED (Timer_Pause) and about to be rewound
// (Timer_resetTime), so simulationTime cannot measure a silence here -- and
// NET_Receive, which owns the in-match per-seat timeout, early-returns until
// the state reaches NET_RUNNING. Without this counter a seat that vanished
// mid-handshake (app killed, WiFi dropped, player backed out) would leave the
// barrier waiting for it forever, with no way back to the menu.
#define NET_SETUP_TIMEOUT_FRAMES 900		// ~15s at 60fps
static int gSetupFrames = 0;

// Set by NET_OnPeerLost when it ends the session from INSIDE the handshake
// pump, so the pump can tell "this session was just torn down" from "this
// device has not been seated yet" -- both of which read net.state ==
// NET_UNDETERMINED. Confusing the two made an unseated device DEAF: it never
// drained its socket, so it could never learn about the peer that was talking
// to it, and a LAN pair whose mDNS only flowed one way never started at all.
static int gSetupTornDown = 0;

// v2.0.8: when this client last sent NOTIFY_LOADED, in handshake frames. The
// GO arrives one round trip after the notify that completed the host's
// barrier, so (gSetupFrames - this) is an RTT sample measured with the only
// clock that runs during the handshake. Used to start the client's sim clock
// half an RTT AHEAD: the host resets its clock when it SENDS the GO, a client
// when it RECEIVES it -- online that is 30-100ms of permanent phase offset
// between the two deterministic sims (enemies, waves), invisible on a LAN,
// the visible part of the online 'legere desynchro'.
static int gLastNotifySentFrame = 0;
static void NET_ArmSetupFrames(void);
static net_peer_t	gPeers[MAX_NUM_PLAYERS];

static void NET_PeersReset(void)
{
	memset(gPeers, 0, sizeof(gPeers));
}

// Count of remote seats still alive in the match.
static int NET_ActiveRemotes(void)
{
	int i, n = 0;
	for (i = 0; i < net.numSeats && i < MAX_NUM_PLAYERS; i++)
		if (i != net.ownSeat && gPeers[i].active)
			n++;
	return n;
}

// v2.0.9 HOST MIGRATION: the host is the LOWEST ACTIVE seat, not literally
// seat 0. Every peer derives it from the same activeMask (the host's own
// verdicts, replayed on every device), so when the host itself drops, the
// survivors agree on its successor without a single extra byte on the wire
// -- the next seat up simply starts counting the barrier, stamping masks and
// ruling on deaths, and everyone else already expects it to. Before this,
// a lost host left the party leaderless: the level played on, and the next
// act's barrier waited forever on a seat that would never answer.
static int NET_HostSeat(void)
{
	int s;
	for (s = 0; s < net.numSeats && s < MAX_NUM_PLAYERS; s++)
		if (s == net.ownSeat || gPeers[s].active)
			return s;
	return 0;
}

static int NET_IsHost(void)
{
	return net.ownSeat == NET_HostSeat();
}


// ------------------------------------------------------------------------------
//  v2.0.9 HOST AUTHORITY ON DEATHS.
//  A hull being hit is no longer a death -- it is a REQUEST. The host (see
//  NET_HostSeat) rules: it applies the death and broadcasts ONE order carrying
//  the pool value it ruled on and a running death sequence; every device applies
//  deaths in that order and that order only, so two hulls dying within one
//  network latency can no longer be sequenced differently on two screens (the
//  review's Failure C: a pool of 2, both die, each screen keeps a different ship).
//  The host's own hull goes through the same ruling, just without the wire.
// ------------------------------------------------------------------------------
static int gDeathSeq = 0;			// host: orders issued this session
static int gLastDeathOrderSeq = 0;	// client: last order applied (LAN sends 3 copies)
static int gRunFrames = 0;			// NET_Receive ticks, for the request-resend cadence
#define NET_DEATH_REQ_RESEND_FRAMES 12
#define NET_DEATH_PENDING_TIMEOUT_MS 2000

int NET_DeathAuthority(void)
{
	return net.state == NET_RUNNING && net.numSeats >= 2;
}

static void NET_SendDeathCmd(int type, int seat, int poolBefore, int seq)
{
	command_t c;
	int copies = NET_IsOnline() ? 1 : 3;	// DEATH_PACKET is reliable on GameKit; the LAN is raw UDP
	int i;
	memset(&c, 0, sizeof(c));
	c.type     = (uchar)type;
	c.playerId = (uchar)seat;
	c.time     = simulationTime;
	c.delta[0] = (float)poolBefore;
	c.delta[1] = (float)seq;
	for (i = 0; i < copies; i++)
		Net_SendDie(&c);
}

// Host side: rule on a hit for `seat`. Idempotent on purpose -- the request is
// resent until the order lands, and P_ApplyDeath raises invulnerableFor, so a
// repeat arriving after the ruling finds the hull invulnerable (or parked) and
// is ignored.
static void NET_HostRuleDeath(int seat)
{
	int poolBefore;
	if (seat < 0 || seat >= MAX_NUM_PLAYERS || seat >= numPlayers)
		return;
	if (players[seat].invulnerableFor > 0)
		return;						// just ruled (or respawning): a repeat, not a new death
	if (players[seat].respawnCounter <= 0 && players[seat].shouldDraw == 0)
		return;						// parked: the corpse cannot die again
	poolBefore = players[seat].respawnCounter;
	gDeathSeq++;
	P_ApplyDeath((uchar)seat);
	NET_SendDeathCmd(NET_RTM_DIE_ORDER, seat, poolBefore, gDeathSeq);
}

// Every device on receiving the host's order (the host never receives its own).
static void NET_ApplyDeathOrder(int seat, int poolBefore, int seq)
{
	int p;
	if (seq <= gLastDeathOrderSeq)
		return;						// a LAN duplicate, or already applied
	gLastDeathOrderSeq = seq;
	if (seat < 0 || seat >= MAX_NUM_PLAYERS || seat >= numPlayers)
		return;
	// The host's pre-death pool is the truth: adopt it, then apply the death
	// exactly as the host did (decrement, mirror, respawn or RIP, game over).
	for (p = 0; p < numPlayers && p < MAX_NUM_PLAYERS; p++)
		players[p].respawnCounter = (char)poolBefore;
	players[seat].deathPending = 0;
	P_ApplyDeath((uchar)seat);
}

// P_Die's multiplayer entry (player.c).
void NET_PlayerHit(int seat)
{
	if (seat < 0 || seat >= MAX_NUM_PLAYERS)
		return;
	if (players[seat].respawnCounter <= 0 && players[seat].shouldDraw == 0)
		return;						// parked hull: nothing to rule on
	if (NET_IsHost())
	{
		NET_HostRuleDeath(seat);
		return;
	}
	if (seat != net.ownSeat)
		return;						// only our own hull's collisions are ours to report
	if (players[seat].deathPending)
		return;						// already asked; the resend loop owns it now
	players[seat].deathPending = 1;
	players[seat].deathPendingSince = simulationTime;
	NET_SendDeathCmd(NET_RTM_DIE_REQ, seat, 0, 0);
}

// Once per NET_Receive: keep a pending request alive, and never let a lost
// answer leave a hull immortal -- after the timeout the request is dropped and
// the hull is simply hittable again.
static void NET_TickDeathPending(void)
{
	player_t* me;
	gRunFrames++;
	if (net.ownSeat < 0 || net.ownSeat >= MAX_NUM_PLAYERS)
		return;
	me = &players[net.ownSeat];
	if (!me->deathPending)
		return;
	if (simulationTime - me->deathPendingSince > NET_DEATH_PENDING_TIMEOUT_MS)
	{
		me->deathPending = 0;
		Log_Printf("Death request unanswered for %dms: dropped.\n", NET_DEATH_PENDING_TIMEOUT_MS);
		return;
	}
	if (NET_IsHost())
	{
		// We became the host while waiting (migration): rule on it ourselves.
		me->deathPending = 0;
		NET_HostRuleDeath(net.ownSeat);
		return;
	}
	if ((gRunFrames % NET_DEATH_REQ_RESEND_FRAMES) == 0)
		NET_SendDeathCmd(NET_RTM_DIE_REQ, net.ownSeat, 0, 0);
}

// Host barrier counters: how many remote seats have joined / finished loading.
// Only ACTIVE seats count -- a parked seat (mid-match drop) never sends again
// and must not deadlock the next level's barrier. The target for both is
// NET_ActiveRemotes().
static int NET_JoinedRemotes(void)
{
	int i, n = 0;
	for (i = 0; i < net.numSeats && i < MAX_NUM_PLAYERS; i++)
		if (i != net.ownSeat && gPeers[i].active && gPeers[i].joined)
			n++;
	return n;
}

static int NET_LoadedRemotes(void)
{
	int i, n = 0;
	for (i = 0; i < net.numSeats && i < MAX_NUM_PLAYERS; i++)
		if (i != net.ownSeat && gPeers[i].active && gPeers[i].loaded)
			n++;
	return n;
}

// ------------------------------------------------------------------------------
//  v2 P4: the LAN roster. Every device registers its Bonjour service and
//  browses for everyone else's; each resolved peer IP lands here. The sorted
//  list of ALL ips (our own included) IS the seat table -- the N-player
//  generalization of the old "lower IP is the server" pairwise election, and
//  bit-identical to it at 2 devices. The roster is ROLLING while players are
//  still appearing; the handshake only fires once it has been stable for
//  NET_LAN_SETTLE_MS (so a party of four all get seated before anyone starts),
//  and freezes for the session the moment the handshake leaves NET_STARTED.
// ------------------------------------------------------------------------------
#define NET_LAN_SETTLE_MS 4000
static unsigned int	gLanIps[MAX_NUM_PLAYERS];		// host byte order, sorted ascending
static int			gLanCount = 0;
static int			gLanChangedAt = 0;				// simulationTime of the last roster change
static int			gLanLocked = 0;					// match started: roster frozen for good
static int			gPartyTarget = 2;				// how many players this party is for
static struct sockaddr_in gSeatAddr[MAX_NUM_PLAYERS];	// LAN: seat -> udp address

// The party size the player asked for (the menu's 2/3/4 pick). The roster stops
// waiting the moment it reaches this, so a LAN duo starts as instantly as it did
// before the roster existed; the settle window below is only the fallback for a
// party that never fills.
void NET_SetPartyTarget(int n)
{
	if (n < 2) n = 2;
	if (n > MAX_NUM_PLAYERS) n = MAX_NUM_PLAYERS;
	gPartyTarget = n;
}

static void LAN_ResetRoster(void)
{
	gLanCount = 0;
	gLanChangedAt = 0;
	gLanLocked = 0;
	memset(gSeatAddr, 0, sizeof(gSeatAddr));
}

// Settled = safe to start the handshake. Once the match has locked the roster,
// permanently settled -- do NOT re-check the time window: Timer_resetTime at
// match start rewinds simulationTime below gLanChangedAt, and the level-2+
// handshakes (state drops back to NET_STARTED between levels) would otherwise
// wait forever on a window that can never close again.
static int LAN_RosterSettled(void)
{
	if (gLanLocked)
		return 1;
	if (gLanCount >= gPartyTarget)
		return 1;					// the party the player asked for is here
	// Fewer than asked for: give the network a few seconds to prove nobody
	// else is coming, then play with who showed up.
	return gLanCount >= 2 && (simulationTime - gLanChangedAt) > NET_LAN_SETTLE_MS;
}

// Map a datagram's source address back to its seat. -1 = not in the roster.
static int LAN_SeatForAddr(const struct sockaddr_in* addr)
{
	unsigned int ip = ntohl(addr->sin_addr.s_addr);
	int s;
	for (s = 0; s < gLanCount; s++)
		if (gLanIps[s] == ip)
			return s;
	return -1;
}

// Add one ip (host byte order) to the roster; re-derive seats. Rolling: only
// while the handshake hasn't started consuming (NET_STARTED or earlier).
static void LAN_AddRosterIp(unsigned int ip)
{
	int s, t;

	if (net.state > NET_STARTED || gLanLocked)
		return;								// roster frozen for this session
	for (s = 0; s < gLanCount; s++)
		if (gLanIps[s] == ip)
			return;							// already seated
	if (gLanCount >= MAX_NUM_PLAYERS)
		return;								// party is full

	gLanIps[gLanCount++] = ip;

	// insertion sort ascending -- the order IS the seat table
	for (s = 1; s < gLanCount; s++)
		for (t = s; t > 0 && gLanIps[t] < gLanIps[t-1]; t--)
		{
			unsigned int tmp = gLanIps[t];
			gLanIps[t] = gLanIps[t-1];
			gLanIps[t-1] = tmp;
		}

	gLanChangedAt = simulationTime;

	// Re-derive the seat view: our seat, the count, the per-seat addresses.
	net.numSeats = gLanCount;
	for (s = 0; s < gLanCount; s++)
	{
		memset(&gSeatAddr[s], 0, sizeof(gSeatAddr[s]));
		gSeatAddr[s].sin_len    = sizeof(gSeatAddr[s]);
		gSeatAddr[s].sin_family = AF_INET;
		gSeatAddr[s].sin_port   = htons(PORT_NUMBER);
		gSeatAddr[s].sin_addr.s_addr = htonl(gLanIps[s]);
		if (ownAddrValid && gLanIps[s] == ntohl(ownAddr.sin_addr.s_addr))
			net.ownSeat = s;
	}
	// Below 2 seats there is no role yet -- the "Looking for the other
	// player..." status (and the discovery pump gates) key off NET_UNKNOWN.
	net.type = (gLanCount >= 2) ? ((net.ownSeat == 0) ? NET_SERVER : NET_CLIENT) : NET_UNKNOWN;

	// Every remote seat currently on the roster starts alive.
	NET_PeersReset();
	for (s = 0; s < gLanCount; s++)
		if (s != net.ownSeat)
			gPeers[s].active = 1;

	if (gLanCount >= 2)
	{
		net.state = NET_STARTED;
		NET_ArmSetupFrames();	// v2 P2: fresh handshake watchdog window
		// The roster is what "we know where to send" MEANS in v2, so it is what
		// clears this v1 flag. It used to be set only by the mDNS query
		// callback -- so a device seated purely by gossip (its Bonjour never
		// resolved anyone) kept it at 0, which made isInitialized false, which
		// made NET_Send return early: it played the match completely MUTE and
		// the host dropped it after five seconds of silence.
		net.serverAddResolved = 1;
	}

	sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETPEERPIP), "Players found: %d -> you are P%d",
	        gLanCount, net.ownSeat + 1);
}

// Deterministic N-way colour dedupe, ascending seats: a seat whose colour
// collides with ANY lower seat steps forward until free. Run by the host just
// before the GO (the broadcast table is final); at 2 players and distinct
// picks this reduces to the classic "player two shifts".
// Column 2 of the bullet atlas is the INVISIBLE option -- a stealth handicap a
// player CHOOSES, never something the dedupe should hand him: at four players
// all defaulting to red, a plain walk would silently blind a seat. Stepping
// over it leaves three visible colours for four seats, so a fourth colliding
// seat keeps its own pick instead: two ships firing red is a readability
// annoyance, invisible bullets nobody asked for is a handicap.
#define NET_COLOR_INVISIBLE 2

static int NET_ColorTaken(int color, int upTo)
{
	int t;
	for (t = 0; t < upTo; t++)
		if (gMPBulletColor[t] == color)
			return 1;
	return 0;
}

static void NET_DedupeLoadouts(void)
{
	int s;
	for (s = 1; s < net.numSeats && s < MAX_NUM_PLAYERS; s++)
	{
		int tries;
		if (!NET_ColorTaken(gMPBulletColor[s], s))
			continue;					// his own pick is free: keep it

		for (tries = 0; tries < NUM_BULLET_COLORS; tries++)
		{
			int c = (gMPBulletColor[s] + 1 + tries) % NUM_BULLET_COLORS;
			if (c == NET_COLOR_INVISIBLE || NET_ColorTaken(c, s))
				continue;
			gMPBulletColor[s] = c;
			break;
		}
		// No visible colour left (four seats, three visible columns): the seat
		// keeps what its player picked.
	}
}


// ------------------------------------------------------------------------------
//  Transport abstraction: LAN (UDP+Bonjour) vs online (GameKit GKMatch)
// ------------------------------------------------------------------------------
// The whole lockstep protocol below is transport-agnostic: every message is a
// fixed-size net_packet_t. On the LAN it travels over a UDP socket
// (sendto/recvfrom); online it travels through GKMatch (Native_GKSendData out,
// NET_OnNetworkDataFrom in) with Apple handling matchmaking + NAT traversal. GKMatch
// is push-based, so inbound packets are queued here and drained by the very same
// read loops the UDP code already uses. LAN behaviour is unchanged.

#define NET_RXQUEUE_SIZE 64
typedef struct net_rx_entry_t { uchar data[BUFFER_SIZE]; int len; int senderSeat; } net_rx_entry_t;
static net_rx_entry_t	netRxQueue[NET_RXQUEUE_SIZE];
static volatile int		netRxHead = 0;	// next slot to write (producer: GKMatch delegate)
static volatile int		netRxTail = 0;	// next slot to read  (consumer: game loop)

char NET_IsOnline(void) { return net.transport == NET_TRANSPORT_GAMECENTER; }

// Called by the GameKit layer when a packet arrives from a peer, tagged with
// that peer's SEAT (v2 P1: the delegate maps GKPlayer -> seat; an unknown
// sender arrives as -1 and is dropped -- packets from outside the seat table
// have no business in the sim). GKMatch delivers on the main thread, same
// thread as the game loop that drains the queue, so no locking is needed
// (the volatile indices are belt-and-suspenders).
void NET_OnNetworkDataFrom(int senderSeat, const void* data, int len)
{
	int next;
	if (len <= 0 || len > BUFFER_SIZE)
		return;
	if (senderSeat < 0 || senderSeat >= MAX_NUM_PLAYERS)
		return;	// not a seated participant of this match
	next = (netRxHead + 1) % NET_RXQUEUE_SIZE;
	if (next == netRxTail)
		return;	// queue full: drop. Lockstep tolerates loss; the periodic ABS update re-syncs.
	memcpy(netRxQueue[netRxHead].data, data, len);
	netRxQueue[netRxHead].len = len;
	netRxQueue[netRxHead].senderSeat = senderSeat;
	netRxHead = next;
}

// Drain one queued packet. Mirrors recvfrom's contract so the existing read
// loops are untouched: returns the byte count, or -1 with errno=EAGAIN when the
// queue is empty. v2 P2: also yields the transport-attributed sender seat.
static int NET_RxDequeue(void* out, int maxlen, int* senderSeat)
{
	int len;
	if (netRxTail == netRxHead) { errno = EAGAIN; return -1; }
	len = netRxQueue[netRxTail].len;
	if (len > maxlen) len = maxlen;
	memcpy(out, netRxQueue[netRxTail].data, len);
	if (senderSeat)
		*senderSeat = netRxQueue[netRxTail].senderSeat;
	netRxTail = (netRxTail + 1) % NET_RXQUEUE_SIZE;
	return len;
}

// Unified send. Online: setup/death packets go reliable, per-frame runtime
// deltas go unreliable (lower latency; the periodic ABS update repairs drift);
// GKMatch broadcasts to every peer by itself. LAN (v2 P4): explicit broadcast,
// one datagram per seated remote (the roster owns the addresses).
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
		int s;
		for (s = 0; s < net.numSeats && s < MAX_NUM_PLAYERS; s++)
		{
			if (s == net.ownSeat || gSeatAddr[s].sin_family != AF_INET)
				continue;
			sendto(net.udpSocket, data, len, 0, (struct sockaddr*)&gSeatAddr[s], sizeof(gSeatAddr[s]));
		}
	}
}

// Unified non-blocking receive. Returns bytes read, or -1/EAGAIN when nothing is
// available. v2 P2/P4: senderSeat carries the TRANSPORT-attributed origin --
// online from the GKPlayer->seat map, on the LAN from the datagram's source
// address looked up in the roster (-1 if it isn't seated: the callers drop it).
// This is the identity the sim trusts; in-packet fields only corroborate.
static int NET_TransportRecv(void* out, int maxlen, struct sockaddr_in* fromAddr, int* senderSeat)
{
	struct sockaddr_in local;
	struct sockaddr_in* src;
	socklen_t alen;
	int n;

	if (net.transport == NET_TRANSPORT_GAMECENTER)
		return NET_RxDequeue(out, maxlen, senderSeat);

	src = fromAddr ? fromAddr : &local;
	alen = sizeof(*src);
	n = recvfrom(net.udpSocket, out, maxlen, 0, (struct sockaddr*)src, &alen);
	if (senderSeat)
		*senderSeat = (n > 0) ? LAN_SeatForAddr(src) : -1;
	return n;
}

// Begin an online match once GKMatch has connected both peers and a role has been
// elected (deterministically, in the GameKit layer). This bypasses the entire
// Bonjour election: the peer is the GKMatch, so the same handshake state machine
// (LOAD_NEXT_LEVEL -> NOTIFY_LOADED -> START_LEVEL) runs straight away.
void NET_StartOnlineMatch(int mySeat, int numSeats)
{
	int s;

	netRxHead = netRxTail = 0;					// fresh receive queue for this match
	net.transport = NET_TRANSPORT_GAMECENTER;
	net.ownSeat   = mySeat;
	net.numSeats  = numSeats;

	// v2 P2: every remote seat starts the match alive.
	NET_PeersReset();
	for (s = 0; s < numSeats && s < MAX_NUM_PLAYERS; s++)
		if (s != mySeat)
			gPeers[s].active = 1;
	// v2 P1: type derives from the seat -- the 2-player handshake below still
	// speaks SERVER/CLIENT until P2 rewrites it as a counting barrier. At 2
	// players this is bit-identical to the old boolean role.
	net.type      = (mySeat == 0) ? NET_SERVER : NET_CLIENT;
	net.state     = NET_STARTED;
	NET_ArmSetupFrames();						// v2 P2: fresh handshake watchdog window
	net.serverAddResolved = 1;					// no resolve online; the peer is the match
	net.setupRequested    = 1;
	net.lastReceivedSequenceNumber = 0;
	net.lastSentSequenceNumber     = 1;

	sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE),   "Online - you are Player %d of %d", mySeat + 1, numSeats);
	sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE+1), " ");
	sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE+2), "Starting match...");
}

void NET_AbortOnlineMatch(void)
{
	NET_Free();				// resets transport back to LAN, clears state
	MENU_Set(MENU_MULTI_MODE);	// v2: multiplayer lives under Game Multi now
}


// PREDICTION
// v2 P0: the 2010 extrapolation machinery (cmdHistory / fakeCmdHistory /
// NET_GenerateFakeCMD and the fake-undo pass) was DEAD CODE -- its only
// consumer sat after an unconditional return in NET_Receive, and its only
// feeder was gated on a broken comparison (outer packet type vs an inner
// command constant, see the DEATH_PACKET note below). ~120 lines removed
// rather than N-ified for nothing; if 4-player wants extrapolation, it will
// be written fresh against per-seat state.


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
	net.ownSeat  = 0;					// v2 P1: seats die with the session
	net.numSeats = 0;
	NET_PeersReset();					// v2 P2: per-seat state dies with the session
	gDeathSeq = 0;					// v2.0.9: the death ledger dies with it too
	gLastDeathOrderSeq = 0;
	{ int dp; for (dp = 0; dp < MAX_NUM_PLAYERS; dp++) players[dp].deathPending = 0; }
	LAN_ResetRoster();					// v2 P4: so does the LAN roster
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

	// v2 P4: only a roster below 2 seats has no role yet -- once seats exist
	// the role is roster-derived and this pump must not clobber it (it now
	// keeps running after discovery, to catch the 3rd and 4th player).
	if (gLanCount < 2)
		net.type = NET_UNKNOWN;

	// Own LAN IP (en0), computed once, for the deterministic IP-based role election.
	if ( !ownAddrValid )
	{
		ownAddr = NET_GetAddressForInterfaceName( INTERFACE_NAME );
		ownAddrValid = ( ownAddr.sin_addr.s_addr != 0 );
	}
	// v2 P4: WE hold the first seat on our own roster; every resolved peer joins it.
	if ( ownAddrValid )
		LAN_AddRosterIp( ntohl(ownAddr.sin_addr.s_addr) );

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

	// Browse for peers. Re-issue the browse every few seconds while the roster
	// can still grow: a long-lived mDNS browse backs its queries off
	// exponentially, so a device that shows up "late" (the 2nd -- or, v2 P4,
	// the 3rd and 4th) can sit undiscovered for a long while -- a fresh browse
	// restarts the query schedule. Once the roster settles we leave it alone.
	if ( browseRef != 0 && !LAN_RosterSettled() && simulationTime - lastBrowseTime > 8000 )
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

	if (net.type == NET_SERVER || net.type == NET_CLIENT)
	{
		// v2 P4: the roster view. The party can still grow until the settle
		// window closes; then the JOIN/barrier handshake fires by itself.
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE), "Party of %d - you are Player %d",
		        gLanCount, net.ownSeat + 1);
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE+1), " ");
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE+2),
		        LAN_RosterSettled() ? "Starting..." : "Waiting for more players...");
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

	// v2 P4: a peer resolved -- seat it. The sorted-IP roster IS the election
	// (bit-identical to the old pairwise "lower IP is the server" at 2), and
	// it keeps growing until the settle window closes or the handshake starts.
	LAN_AddRosterIp( ntohl(peerIp.s_addr) );
	net.serverAddResolved = 1;	// addresses live in the roster now (isInitialized gate)
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
	memset(&p, 0, sizeof(p));	// no uninitialized bytes on the wire
	p.type = SETUP_PACKET;
	p.command.type = cmdType;
	p.numRedundant = 0;
	p.senderSeat   = net.ownSeat;	// v2 P2: identity + protocol on every packet
	p.protoVersion = NET_PROTO;

	// Who is still in the party, as WE see it (the host's view is the truth).
	p.activeMask = (1 << net.ownSeat);
	for (i = 0; i < net.numSeats && i < MAX_NUM_PLAYERS; i++)
		if (gPeers[i].active)
			p.activeMask |= (1 << i);

	// ...and who we know is on the network (LAN gossip; zeros online).
	if (!NET_IsOnline())
		for (i = 0; i < gLanCount && i < MAX_NUM_PLAYERS; i++)
			p.rosterIps[i] = gLanIps[i];

	// Our own Custom loadout rides in our slot of the table; the host's GO
	// (START_LEVEL) broadcasts the COMPLETE deduped table instead.
	if (cmdType == NET_CMD_START_LEVEL)
	{
		for (i = 0; i < MAX_NUM_PLAYERS; i++)
		{
			p.shipChoice[i]  = gMPShipChoice[i];
			p.bulletColor[i] = gMPBulletColor[i];
		}
	}
	else
	{
		p.shipChoice[net.ownSeat]  = gShipChoice;
		p.bulletColor[net.ownSeat] = gBulletColor;
	}

	for (i = 0; i < copies; i++)
	{
		p.sequenceNumber = net.lastSentSequenceNumber++;
		p.ackSequenceNumber = 0;
		NET_TransportSend(&p, sizeof(p));
	}
}

// Per-player custom loadout sync: each end sends its own Custom choice (ship +
// bullet colour) in its setup packets; once a setup packet from the peer arrives,
// both ends hold both choices and apply the same deterministic rule -- so the two
// devices render the match identically without negotiation.
// v2 P2: store ONE seat's loadout from its slot in the packet table (plus our
// own from the local Custom picks). Colour deduping happens once, host-side,
// at the GO (NET_DedupeLoadouts) -- the broadcast table is final.
static void NET_StorePeerLoadout(const net_packet_t* packet, int seat)
{
	int ship, color;

	if (seat < 0 || seat >= MAX_NUM_PLAYERS)
		return;

	ship  = packet->shipChoice[seat];
	color = packet->bulletColor[seat];

	// Clamp (protects against a mismatched build on the other end).
	if (ship  < 0 || ship  >= NUM_SHIP_CHOICES)  ship  = seat % NUM_SHIP_CHOICES;
	if (color < 0 || color >= NUM_BULLET_COLORS) color = seat % NUM_BULLET_COLORS;

	gMPShipChoice[seat]  = ship;
	gMPBulletColor[seat] = color;

	// Our own slot, from the local Custom picks.
	gMPShipChoice[net.ownSeat]  = (gShipChoice  >= 0 && gShipChoice  < NUM_SHIP_CHOICES)  ? gShipChoice  : net.ownSeat % NUM_SHIP_CHOICES;
	gMPBulletColor[net.ownSeat] = (gBulletColor >= 0 && gBulletColor < NUM_BULLET_COLORS) ? gBulletColor : net.ownSeat % NUM_BULLET_COLORS;

	Log_Printf("Loadout sync: seat %d ship=%d color=%d\n", seat, ship, color);
}

// Client side of the GO: the host's START_LEVEL carries the final table --
// copy it verbatim (it is already deduped) and re-point the ship models.
static void NET_ApplyLoadoutTable(const net_packet_t* packet)
{
	int s;
	for (s = 0; s < net.numSeats && s < MAX_NUM_PLAYERS; s++)
	{
		int ship  = packet->shipChoice[s];
		int color = packet->bulletColor[s];
		if (ship  < 0 || ship  >= NUM_SHIP_CHOICES)  ship  = s % NUM_SHIP_CHOICES;
		if (color < 0 || color >= NUM_BULLET_COLORS) color = s % NUM_BULLET_COLORS;
		gMPShipChoice[s]  = ship;
		gMPBulletColor[s] = color;
	}
	P_ReloadShip();	// models were applied at preload; re-point with the final table
}

// Peer liveness: in a running match every peer sends every frame, so a long
// silence from a seat means it quit, was backgrounded, or lost the network.
#define NET_PEER_TIMEOUT_MS 5000

// The LAST peer is gone mid-match: end the match cleanly on THIS side too. Free
// the session, reload the menu scene NOW (otherwise the abandoned game keeps
// simulating behind the menu), then show a notice telling the player what happened.
void NET_OnPeerLost(void)
{
	Log_Printf("NET_OnPeerLost\n");

	gSetupTornDown = 1;				// tell the handshake pump this session is over

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

// v2 P2: ONE seat is gone mid-match -- the match continues for everyone else.
// Park the departed ship offscreen (the exact RIP-branch idiom from P_Die:
// autopilot pinned below the screen, no draw) so the sim stays deterministic
// on every surviving peer -- each one detects the silence on the same shared
// clock and parks the same seat. When the LAST remote seat drops, fall back
// to the classic end-of-session path.
void NET_OnSeatLost(int seat)
{
	if (seat < 0 || seat >= MAX_NUM_PLAYERS || seat == net.ownSeat)
		return;
	if (!gPeers[seat].active)
		return;						// already parked (timeout + transport can both fire)

	gPeers[seat].active = 0;
	Log_Printf("NET_OnSeatLost: seat %d (%d remotes left)\n", seat, NET_ActiveRemotes());

	// v2.0.9: the role follows the roster. If the seat that just left WAS the
	// host, NET_HostSeat() now names the next one up on every survivor at
	// once -- and net.type has to follow, because the JOIN sender, the
	// isInitialized gate and the barrier all key off it.
	{
		int wasHost = (seat == 0) || (seat < net.ownSeat && NET_IsHost());
		net.type = NET_IsHost() ? NET_SERVER : NET_CLIENT;
		if (wasHost)
			Log_Printf("Host migration: seat %d is the host now%s.\n",
			           NET_HostSeat(), NET_IsHost() ? " (that is us)" : "");
	}

	if (NET_ActiveRemotes() == 0)
	{
		NET_OnPeerLost();			// nobody left to play with
		return;
	}

	if (seat < numPlayers)
	{
		player_t* p = &players[seat];

		p->ss_position[1] = -1.4f;	// below the screen

		p->autopilot.enabled = 1;
		p->autopilot.end_ss_position[0] = p->ss_position[0];
		p->autopilot.end_ss_position[1] = -1.4f;
		p->autopilot.diff_ss_position[0] = 0;
		p->autopilot.diff_ss_position[1] = 0;
		p->autopilot.timeCounter  = 2000000;	// effectively forever
		p->autopilot.originalTime = 2000000;
		p->shouldDraw = 0;
	}

	// Say it ON SCREEN: the multiplayer text lines only show in the lobby, so
	// mid-match a seat simply used to vanish with no explanation. Three seconds,
	// drifting up out of the way of the fight.
	{
		vec2short_t from, to;
		char line[32];
		sprintf(line, "PLAYER %d LEFT", seat + 1);
		from[0] = to[0] = 0;
		from[1] = -120;
		to[1]   = -60;
		DYN_TEXT_AddText(from, to, 3000, 2.2f, line);
	}

	sprintf(MENU_GetMultiplayerTextLine(4), "Player %d left the game.", seat + 1);
}

// v2 P2: the handshake as a COUNTING BARRIER. The old machine was four
// hardcoded branches for a symmetric pair; this one is the same protocol
// generalized -- the host (seat 0) counts joins, then counts loads, then
// broadcasts one GO carrying the final loadout table. At 2 players every
// state transition happens on exactly the same packets as before.
//
//   client seats:  JOIN(loadout) --> ...              preload on echo,
//                  NOTIFY_LOADED --> ...              run on START_LEVEL
//   host seat 0:   all joined?  -> preload + echo JOIN to everyone
//                  all loaded?  -> dedupe colours, GO (table), run
static void NET_ArmLiveness(void)
{
	int s;
	for (s = 0; s < net.numSeats && s < MAX_NUM_PLAYERS; s++)
		gPeers[s].lastPacketTime = simulationTime;	// fresh window (clock just reset)
}

// Re-arm the frame-counted handshake watchdog: called on every state change of
// the barrier, so each phase gets a full window of its own.
static void NET_ArmSetupFrames(void)
{
	int s;
	for (s = 0; s < MAX_NUM_PLAYERS; s++)
		gPeers[s].lastSetupFrame = gSetupFrames;
}

// Fill the shared life pool at MATCH start only (we are still on the menu
// scene, about to load level 1): N players' worth. Between levels the pool
// carries over. Deterministic -- every peer runs this on its own preload.
static void NET_FillLifePoolIfMatchStart(void)
{
	if (engine.sceneId == 0)
	{
		// Sized by the seats actually PRESENT, not by the seats booked: a
		// player who never made it through the handshake shouldn't leave his
		// three lives in the pot. Every peer agrees here -- clients adopt the
		// host's party view (activeMask) before this runs.
		int lp, present = NET_ActiveRemotes() + 1;
		for (lp = 0; lp < MAX_NUM_PLAYERS; lp++)
			players[lp].respawnCounter = present * numPlayerRespawn[DIFFICULTY_NORMAL];
	}
}

// HOST barrier, gate 1: everyone who is still in the party has asked in ->
// preload here and order the same preload everywhere. Called both when a JOIN
// arrives and when the watchdog drops the seat that was holding this up.
static void NET_HostTryPreload(void)
{
	if (net.state != NET_STARTED)
		return;
	if (NET_JoinedRemotes() < NET_ActiveRemotes())
		return;
	if (NET_ActiveRemotes() < 1)
		return;					// nobody to play with (NET_OnSeatLost handles the exit)

	dEngine_RequireSceneId((engine.sceneId + 1) % engine.numScenes);
	numPlayers = net.numSeats;
	controlledPlayer = net.ownSeat;
	NET_FillLifePoolIfMatchStart();
	dEngine_CheckState();
	SND_PauseSoundTrack();
	Timer_Pause();
	net.state = NET_PRELOADED;
	gLanLocked = 1;			// v2 P4: the LAN roster is final for this match (no-op online)
	NET_ArmSetupFrames();	// fresh window for the "loaded" phase
	sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETSTATE), "state=NET_PRELOADED.\n");
	NET_SendSetupCmd(NET_CMD_LOAD_NEXT_LEVEL);
	sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETLASTSENT), "LAST SENT=NET_CMD_LOAD_NEXT_LEVEL");
}

// HOST barrier, gate 2: everyone is staged behind the curtain -> finalize the
// loadout table once and send one GO for all.
static void NET_HostTryStart(void)
{
	if (net.state != NET_PRELOADED)
		return;
	if (NET_LoadedRemotes() < NET_ActiveRemotes())
		return;
	if (NET_ActiveRemotes() < 1)
		return;

	NET_DedupeLoadouts();
	P_ReloadShip();

	net.state = NET_RUNNING;
	sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETSTATE), "state=NET_RUNNING.\n");
	SND_ResumeSoundTrack();
	Timer_resetTime();
	Timer_Resume();
	NET_ArmLiveness();
	MENU_Set(MENU_NONE);

	NET_SendSetupCmd(NET_CMD_START_LEVEL);	// carries the final table + party mask
	sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETLASTSENT), "LAST SENT=NET_CMD_START_LEVEL");
}

// CLIENT side: adopt the host's view of who is still in the party. Clients never
// hear each other, so this is the only way they learn that a seat timed out
// during the handshake -- otherwise a ghost ship would sit on their screen (and
// mirror the shared life counter) for a player who never arrived.
static void NET_ApplyActiveMask(int mask)
{
	int s;
	if (mask == 0)
		return;					// a v2 build always stamps at least its own bit
	for (s = 0; s < net.numSeats && s < MAX_NUM_PLAYERS; s++)
	{
		if (s == net.ownSeat)
			continue;
		if (gPeers[s].active && !(mask & (1 << s)))
			NET_OnSeatLost(s);
	}
}

// The watchdog itself, run once per frame while the handshake is up. Drops any
// seat we are still WAITING on after the timeout: the host stops waiting for a
// silent joiner (the barrier then completes with the seats that answered), a
// client that loses the host ends the session. Never fires while the LAN roster
// is still settling -- nobody is expected to speak yet.
static void NET_CheckSetupTimeouts(void)
{
	int s;

	if (!NET_IsOnline() && !LAN_RosterSettled())
	{
		NET_ArmSetupFrames();		// the party is still forming: no silence to punish
		return;
	}

	if (!NET_IsHost())
	{
		// A client only ever waits on the host. Past the preload it waits for
		// ONE packet the host has already queued (the GO), so the window there
		// is short: a straggler that misses it would otherwise wake up seconds
		// behind the level's clock, which no amount of position resync repairs.
		int limit = (net.state == NET_PRELOADED) ? 300 : NET_SETUP_TIMEOUT_FRAMES;
		int h = NET_HostSeat();
		if (gPeers[h].active && gSetupFrames - gPeers[h].lastSetupFrame > limit)
		{
			Log_Printf("Handshake timeout: the host went silent.\n");
			NET_OnPeerLost();
		}
		return;
	}

	// Host: drop whichever remote is holding the barrier up.
	for (s = 1; s < net.numSeats && s < MAX_NUM_PLAYERS; s++)
	{
		if (!gPeers[s].active)
			continue;
		if (net.state == NET_STARTED   && gPeers[s].joined) continue;
		if (net.state == NET_PRELOADED && gPeers[s].loaded) continue;
		if (gSetupFrames - gPeers[s].lastSetupFrame > NET_SETUP_TIMEOUT_FRAMES)
		{
			Log_Printf("Handshake timeout: seat %d never answered -- dropping it.\n", s);
			NET_OnSeatLost(s);		// falls back to NET_OnPeerLost when nobody is left
			// The barrier was waiting on that seat: re-evaluate it now, or the
			// party would sit at the curtain with no further packet to trigger it.
			NET_HostTryPreload();
			NET_HostTryStart();
		}
	}
}

// One inbound setup packet. Split out of Net_ProcessSetupPacket so the public
// entry point can DRAIN the socket: with three clients each sending a join
// every frame, reading a single datagram per frame lets the receive buffer back
// up permanently and the barrier starves behind stale packets (the rig caught
// the party stalling at the curtain for exactly this reason).
static void NET_HandleSetupPacket(net_packet_t* packet, int setupSeat, const struct sockaddr_in* from)
{
	uchar packetConsumed = 0;

	if (packet->type != SETUP_PACKET)
		return;

	// Identity and protocol at the door (v2 P2).
	if (packet->protoVersion != NET_PROTO)
	{
		Log_Printf("Setup packet with foreign protocol %d dropped (v1 build?).\n", packet->protoVersion);
		return;
	}

	// v2 P4: LAN gossip, BEFORE the identity checks -- a device missing from our
	// roster has no seat yet, so its packets would be dropped as unattributable
	// and we would never learn it exists. Merging here is safe: the sender's ip
	// comes from the datagram, not from the payload, and the roster is frozen
	// (gLanLocked) the moment the handshake commits.
	if (!NET_IsOnline() && !gLanLocked)
	{
		int r;
		if (from && from->sin_family == AF_INET)
			LAN_AddRosterIp(ntohl(from->sin_addr.s_addr));
		for (r = 0; r < MAX_NUM_PLAYERS; r++)
			if (packet->rosterIps[r])
				LAN_AddRosterIp(packet->rosterIps[r]);
		// Our seat may have just moved: re-attribute this datagram.
		if (from && from->sin_family == AF_INET)
			setupSeat = LAN_SeatForAddr(from);
	}

	if (setupSeat < 0 || setupSeat >= MAX_NUM_PLAYERS || setupSeat == net.ownSeat)
		return;
	if (packet->senderSeat != setupSeat)
		return;
	if (!gPeers[setupSeat].active)
		return;						// a dropped seat cannot rejoin mid-session

	// Per-seat sequence space.
	if (packet->sequenceNumber <= (int)gPeers[setupSeat].lastRxSeq)
		return;
	gPeers[setupSeat].lastRxSeq = packet->sequenceNumber;
	gPeers[setupSeat].lastPacketTime = simulationTime;
	gPeers[setupSeat].lastSetupFrame = gSetupFrames;	// proof of life, handshake clock

	sprintf(MENU_GetMultiplayerTextLine(4),"Setup cmd %d from seat %d.\n",packet->command.type, setupSeat);

	// v2 P4: LAN -- the host doesn't act on JOINs until the roster settles
	// (a 3rd/4th device may still be resolving; the JOIN is resent anyway).
	if (!NET_IsOnline() && net.state == NET_STARTED && !LAN_RosterSettled())
		return;

	// ---------------- HOST (seat 0): the barrier ----------------
	if (NET_IsHost() && packet->command.type == NET_CMD_LOAD_NEXT_LEVEL &&
	    (net.state == NET_STARTED || net.state == NET_PRELOADED))
	{
		packetConsumed = 1;

		// (v2 P4: no more "learn the client's address from its first packet" --
		// the LAN roster owns every seat's address since the election.)

		NET_StorePeerLoadout(packet, setupSeat);
		if (!gPeers[setupSeat].joined)
		{
			gPeers[setupSeat].joined = 1;
			Log_Printf("Seat %d joined (%d/%d remotes).\n", setupSeat, NET_JoinedRemotes(), NET_ActiveRemotes());
		}

		if (net.state == NET_STARTED)
			NET_HostTryPreload();
		else if (net.state == NET_PRELOADED)
		{
			// A late joiner's retry: idempotent re-echo of the preload order.
			NET_SendSetupCmd(NET_CMD_LOAD_NEXT_LEVEL);
		}
	}

	if (NET_IsHost() && net.state == NET_PRELOADED && packet->command.type == NET_CMD_NOTIFY_LOADED)
	{
		packetConsumed = 1;
		gPeers[setupSeat].loaded = 1;
		Log_Printf("Seat %d loaded (%d/%d remotes).\n", setupSeat, NET_LoadedRemotes(), NET_ActiveRemotes());

		NET_HostTryStart();
	}

	// ---------------- CLIENT seats ----------------
	// The host's heartbeat: proof of life only (the liveness stamp above did
	// the work), and it tells the player what the wait is for.
	if (!NET_IsHost() && setupSeat == NET_HostSeat() && packet->command.type == NET_CMD_WAITING)
	{
		packetConsumed = 1;
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETYPE+2), "Waiting for the other players...");
	}

	if (!NET_IsHost() && setupSeat == NET_HostSeat() && net.state == NET_STARTED &&
	    packet->command.type == NET_CMD_LOAD_NEXT_LEVEL)
	{
		packetConsumed = 1;

		NET_ApplyActiveMask(packet->activeMask);	// the host's party view is the truth
		dEngine_RequireSceneId((engine.sceneId + 1) % engine.numScenes);
		numPlayers = net.numSeats;
		controlledPlayer = net.ownSeat;
		NET_FillLifePoolIfMatchStart();		// same rule, same data, as the host
		NET_StorePeerLoadout(packet, 0);	// the host's own loadout rides its echo
		dEngine_CheckState();
		SND_PauseSoundTrack();
		Timer_Pause();
		net.state = NET_PRELOADED;
		gLanLocked = 1;	// v2 P4: the LAN roster is final for this match (no-op online)
		NET_ArmSetupFrames();	// fresh window while we wait for the GO
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETSTATE), "state=NET_PRELOADED.\n");

		gLastNotifySentFrame = gSetupFrames;
		NET_SendSetupCmd(NET_CMD_NOTIFY_LOADED);
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETLASTSENT), "LAST SENT=NET_CMD_NOTIFY_LOADED");
	}

	if (!NET_IsHost() && setupSeat == NET_HostSeat() && net.state == NET_PRELOADED &&
	    packet->command.type == NET_CMD_START_LEVEL)
	{
		packetConsumed = 1;

		NET_ApplyActiveMask(packet->activeMask);	// park any seat that never made it
		NET_ApplyLoadoutTable(packet);		// final, host-deduped table

		net.state = NET_RUNNING;
		SND_ResumeSoundTrack();
		Timer_resetTime();
		Timer_Resume();
		// v2.0.8: start this sim HALF AN RTT ahead (online only). The host's
		// clock started when it SENT this GO; ours would start on receipt,
		// a permanent phase lag between two sims that both derive every
		// enemy from simulationTime. The GO answers our latest NOTIFY one
		// round trip later, so that gap IS an RTT sample -- in handshake
		// frames (~16ms each), the only clock running here. Capped: with 3+
		// players the GO may answer someone ELSE's notify (overestimate),
		// and a bad estimate must stay a small error. LAN keeps its
		// confirmed bit-exact start (RTT there is a frame, ~nothing).
		if (NET_IsOnline())
		{
			int halfMs = (gSetupFrames - gLastNotifySentFrame) * 16 / 2;
			if (halfMs > 150) halfMs = 150;
			if (halfMs > 0)
			{
				simulationTime += halfMs;
				Log_Printf("Online clock offset: +%dms (half the handshake RTT).\n", halfMs);
			}
		}
		NET_ArmLiveness();
		MENU_Set(MENU_NONE);
		sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETSTATE), "state=NET_RUNNING.\n");
	}

	if (!packetConsumed)
		Log_Printf("Packet type=%d was not consumed.",packet->command.type );
}

#define NET_SETUP_DRAIN_MAX 32		// datagrams per frame; plenty for 3 peers

void Net_ProcessSetupPacket(void)
{
	struct sockaddr_in incomingAdd;
	int byteReceived;
	int setupSeat;
	int drained;

	// One tick of the handshake's own clock (the sim clock is paused here).
	gSetupFrames++;
	gSetupTornDown = 0;
	NET_CheckSetupTimeouts();
	if (gSetupTornDown)
		return;					// the watchdog just tore the session down
	// NOTE: do NOT return on net.state == NET_UNDETERMINED here. That is also
	// the state of a device nobody has seated yet, and such a device MUST keep
	// draining its socket: hearing a seated peer's packet is the only way it
	// can join the roster when Bonjour only delivered in one direction.

	// The lobby advert (NET_CMD_WAITING). Two jobs:
	//  - the host is allowed to sit silent while it waits for the rest of the
	//    party, and a silent host looks exactly like a dead one to a client's
	//    watchdog, so it says "still here" out loud;
	//  - on the LAN every seat sends it, because it carries the sender's roster
	//    and that gossip is what makes the four devices agree on the seat table
	//    (a device nobody told us about has no seat, and no voice).
	if (net.state == NET_STARTED && (gSetupFrames % 15) == 0 &&
	    (NET_IsHost() || !NET_IsOnline()) && net.numSeats >= 2)
		NET_SendSetupCmd(NET_CMD_WAITING);

	// A client stops sending joins the moment it preloads, so its single
	// NOTIFY_LOADED burst was the only one the host would ever get: lose it and
	// the party waits at the curtain for a message nobody will send again.
	// Repeat it while we wait for the GO.
	if (!NET_IsHost() && net.state == NET_PRELOADED && (gSetupFrames % 30) == 0)
	{
		gLastNotifySentFrame = gSetupFrames;
		NET_SendSetupCmd(NET_CMD_NOTIFY_LOADED);
	}

	for (drained = 0; drained < NET_SETUP_DRAIN_MAX; drained++)
	{
		bzero(&incomingAdd, sizeof(incomingAdd));
		setupSeat = -1;

		byteReceived = NET_TransportRecv(net.buffer, sizeof(net.buffer), &incomingAdd, &setupSeat);
		if (byteReceived == -1)
		{
			if (errno != EAGAIN )
				sprintf(MENU_GetMultiplayerTextLine(4),"Error recvfrom:%d %s.\n",errno,strerror( errno ));
			return;					// nothing left in the queue
		}

		// Full packets only: net.buffer keeps the PREVIOUS packet's bytes, so a
		// short datagram would be read as a mix of the two.
		if (byteReceived != (int)sizeof(net_packet_t))
			continue;

		NET_HandleSetupPacket((net_packet_t*)net.buffer, setupSeat, &incomingAdd);

		// The GO (or a teardown) ends the handshake: anything still queued
		// belongs to the running match, and NET_Receive owns that. An unseated
		// device (also NET_UNDETERMINED) keeps draining -- see the note above.
		if (net.state == NET_RUNNING || gSetupTornDown)
			return;
	}
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
	
	
	// v2 P4: keep the discovery pump running until the handshake takes over
	// (was: only while UNKNOWN) -- late 3rd/4th devices resolve through here.
	if (!NET_IsOnline() && net.state <= NET_STARTED && !gLanLocked)
	{
		if (!NET_CheckServerAvailability() && net.type == NET_UNKNOWN)
		{
			sprintf(MENU_GetMultiplayerTextLine(MESSAGE_NETMYIP), "Error while NET_CheckServerAvailability.\n");
			return ;
		}
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
		// v2 P4: on the LAN, hold the JOIN until the roster has settled --
		// firing on first contact would start a 2-seat match under the feet
		// of a 3rd/4th device still resolving. Online, GameKit already
		// delivered the full, final party.
		// Every 10th frame, not every frame (v2 P2): with three clients the
		// host cannot read the party's joins as fast as they arrive, and the
		// receive queue backs up behind them.
		if (net.type == NET_CLIENT && (NET_IsOnline() || LAN_RosterSettled()) &&
		    (gSetupFrames % 10) == 0)
		{
			memset(&registerPacket, 0, sizeof(registerPacket));	// no uninitialized bytes on the wire
			registerPacket.sequenceNumber = net.lastSentSequenceNumber++;
			registerPacket.type = SETUP_PACKET;
			registerPacket.command.type = NET_CMD_LOAD_NEXT_LEVEL;
			registerPacket.numRedundant = 0;
			registerPacket.senderSeat   = net.ownSeat;			// v2 P2
			registerPacket.protoVersion = NET_PROTO;
			registerPacket.shipChoice[net.ownSeat]  = gShipChoice;	// our Custom loadout, in OUR slot
			registerPacket.bulletColor[net.ownSeat] = gBulletColor;
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


void NET_Receive(void)
{
	int byteReceived = 0;
	net_packet_t rcv_packet;
	int i, s;
	int senderSeat;

	//Log_Printf("NET_Receive\n");

	if (!isInitialized)
		return;

	// v2 P2: fresh frame for every REMOTE seat's buffer (ours is the input side)
	for (s = 0; s < MAX_NUM_PLAYERS; s++)
		if (s != net.ownSeat)
			commandsBuffers[s].numCommands = 0;

	while (1)
	{
		senderSeat = -1;
		byteReceived = NET_TransportRecv(&rcv_packet, sizeof(net_packet_t), NULL, &senderSeat);

		if (byteReceived == -1)
		{
			if (errno != EAGAIN )
				sprintf(MENU_GetMultiplayerTextLine(4),"Error recvfrom:%d %s.\n",errno,strerror( errno ));
			break;
		}

		// A short datagram would leave the rest of the struct as stack garbage
		// (protoVersion, senderSeat, the redundancy ring, the loadout table) --
		// and this struct is not zeroed between reads. Our packets are always
		// exactly one net_packet_t; anything else is noise or a foreign build.
		if (byteReceived != (int)sizeof(net_packet_t))
			continue;

		// v2 P2: the transport-attributed seat is the identity the sim trusts.
		if (senderSeat < 0 || senderSeat >= MAX_NUM_PLAYERS || senderSeat == net.ownSeat)
			continue;
		if (rcv_packet.protoVersion != NET_PROTO)
			continue;					// a v1 build, or noise: not our protocol
		if (rcv_packet.senderSeat != senderSeat)
			continue;					// claims to be a seat it is not
		if (!gPeers[senderSeat].active)
			continue;					// parked seat: its ship is gone, its inputs with it

		gPeers[senderSeat].lastPacketTime = simulationTime;	// proof of life, per seat

		// Ignore handshake leftovers here (they're handled by Net_ProcessSetupPacket).
		// Their command fields are zeroed, not input.
		if (rcv_packet.type == SETUP_PACKET)
			continue;

		// The host's party view rules: a client parks a seat when the HOST says
		// it is gone, not when its own stopwatch runs out. Four independent
		// timeouts fire hundreds of milliseconds apart (seconds apart, on
		// GKMatch), and for that whole window the parked ship sits at different
		// positions on different devices -- enough for the aiming enemies to
		// shoot along different vectors and the sims to part ways for good.
		if (!NET_IsHost() && senderSeat == NET_HostSeat())
			NET_ApplyActiveMask(rcv_packet.activeMask);

		// A runtime packet carries its current command plus a few redundant (previous)
		// commands. Walk them oldest-first and apply every command whose sequence we
		// haven't seen yet -- this recovers inputs lost to dropped packets. All
		// bookkeeping is PER SEAT: each peer owns an independent sequence space.
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

			if (seq <= (int)gPeers[senderSeat].lastRxSeq)
				continue;			// already applied this command

			net.numDropedPackets += (seq - (1 + gPeers[senderSeat].lastRxSeq));
			gPeers[senderSeat].lastRxSeq = seq;

			// v2.0.9 host authority on deaths: protocol events, handled here, never
			// through the input buffers. A request must be about the SENDER's own
			// hull and is only the host's business; an order is only ever the host's.
			if (c->type == NET_RTM_DIE_REQ)
			{
				if (NET_IsHost() && c->playerId == senderSeat)
					NET_HostRuleDeath(senderSeat);
				continue;
			}
			if (c->type == NET_RTM_DIE_ORDER)
			{
				if (senderSeat == NET_HostSeat())
					NET_ApplyDeathOrder(c->playerId, (int)c->delta[0], (int)c->delta[1]);
				continue;
			}

			// The in-packet playerId must agree with the transport identity:
			// a command may only ever drive its sender's own ship.
			if (c->playerId != senderSeat)
				continue;

			if (c->type == NET_RTM_COMMAND || c->type == NET_RTM_ABS_UPDATE)
			{
				// v2.0.8: the ABS resync rides the SAME queue as the deltas.
				// Applied out of order (it used to jump the queue), the stale
				// deltas still queued BEHIND it re-applied movement the snapshot
				// already contained -- the correction overshot, the next one
				// pulled back, and the remote ship never quite settled: the
				// tester's 'legere desynchro' online. Wire order, paced.
				// Movement: through the de-jitter queue (drained below, one
				// per frame). Full queue: drop the OLDEST -- the total applied
				// delta then undershoots briefly, and the 300ms ABS resync
				// mops that up; freezing the newest would lag forever.
				int next = (gPeers[senderSeat].jqHead + 1) % NET_JITTER_Q;
				if (next == gPeers[senderSeat].jqTail)
					gPeers[senderSeat].jqTail = (gPeers[senderSeat].jqTail + 1) % NET_JITTER_Q;
				memcpy(&gPeers[senderSeat].jq[gPeers[senderSeat].jqHead], c, sizeof(command_t));
				gPeers[senderSeat].jqHead = next;
			}
			else if (commandsBuffers[senderSeat].numCommands < COMMAND_BUFFER_SIZE-1)
			{
				// Deaths: immediate -- rare, order-insensitive for the pool,
				// and a queue overflow must never be able to drop one.
				slot = commandsBuffers[senderSeat].numCommands;
				memcpy(&commandsBuffers[senderSeat].cmds[slot], c, sizeof(command_t));
				commandsBuffers[senderSeat].cmds[slot].time = simulationTime;
				commandsBuffers[senderSeat].numCommands++;
			}
		}
	}

	// Smooth playback: pop ONE queued movement per seat per frame (the sender's
	// own emission rate), two while the backlog exceeds three frames so a burst
	// is worked off gently instead of teleporting the ship.
	for (s = 0; s < net.numSeats && s < MAX_NUM_PLAYERS; s++)
	{
		int pops, depth;
		if (s == net.ownSeat || !gPeers[s].active)
			continue;
		depth = (gPeers[s].jqHead - gPeers[s].jqTail + NET_JITTER_Q) % NET_JITTER_Q;
		pops  = (depth > 3) ? 2 : (depth > 0) ? 1 : 0;
		while (pops-- > 0 && commandsBuffers[s].numCommands < COMMAND_BUFFER_SIZE-1)
		{
			int slot = commandsBuffers[s].numCommands;
			memcpy(&commandsBuffers[s].cmds[slot], &gPeers[s].jq[gPeers[s].jqTail], sizeof(command_t));
			commandsBuffers[s].cmds[slot].time = simulationTime;
			commandsBuffers[s].numCommands++;
			gPeers[s].jqTail = (gPeers[s].jqTail + 1) % NET_JITTER_Q;
		}
	}

	NET_TickDeathPending();		// v2.0.9: resend an unanswered hit, or give up on it

	// v2 P2: per-seat liveness. A silent seat is parked and the match goes on
	// (the user's rule: one player dropping must not kill the party); only the
	// LAST remote's loss ends the session -- which at 2 players is exactly the
	// old behavior.
	for (s = 0; s < net.numSeats && s < MAX_NUM_PLAYERS; s++)
	{
		if (s == net.ownSeat || !gPeers[s].active)
			continue;
		// Only the host declares a seat lost (its activeMask then tells the
		// others). A client still watches the HOST itself -- if that goes
		// silent nobody else can tell it, and the current level plays on
		// leaderless until the next act's barrier ends the session.
		if (!NET_IsHost() && s != NET_HostSeat())
			continue;
		if (simulationTime - gPeers[s].lastPacketTime > NET_PEER_TIMEOUT_MS)
			NET_OnSeatLost(s);
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

	memset(&send_packet, 0, sizeof(send_packet));	// no uninitialized bytes on the wire
	send_packet.type = RUNTIME_PACKET;
	send_packet.command.type = NET_RTM_COMMAND;
	send_packet.sequenceNumber = seq;
	send_packet.senderSeat   = net.ownSeat;		// v2 P2
	send_packet.protoVersion = NET_PROTO;
	// The party, as the sender sees it. Only the HOST's copy is acted on
	// (NET_Receive), which is what keeps "who is parked" a single decision
	// instead of four independent stopwatches.
	{
		int s;
		send_packet.activeMask = (1 << net.ownSeat);
		for (s = 0; s < net.numSeats && s < MAX_NUM_PLAYERS; s++)
			if (gPeers[s].active)
				send_packet.activeMask |= (1 << s);
	}
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
	// The `<` arm matters: Timer_resetTime rewinds simulationTime to 0 at every
	// level start while this stamp keeps the PREVIOUS level's value, so without
	// it the resync stayed silent for a whole act (the drift correction the
	// streaming design leans on, gone from act 2 onwards).
	if (simulationTime - lastFullUpdateTime > 300 || simulationTime < lastFullUpdateTime)
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

	memset(&send_packet, 0, sizeof(send_packet));	// no uninitialized bytes on the wire
	send_packet.type = DEATH_PACKET;	// own value (was NET_RUNNING -- see the define)
	send_packet.sequenceNumber = net.lastSentSequenceNumber++;
	send_packet.senderSeat   = net.ownSeat;		// v2 P2
	send_packet.protoVersion = NET_PROTO;
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
	int s;

	Log_Printf("NET_OnNextLevelLoad\n");
	net.setupRequested = 1;
	net.state = NET_STARTED;
	NET_ArmSetupFrames();	// v2 P2: fresh handshake watchdog window for this level

	// v2 P2: re-arm the barrier for the next level's handshake (active flags
	// survive -- a parked seat stays parked).
	for (s = 0; s < MAX_NUM_PLAYERS; s++)
	{
		gPeers[s].joined = 0;
		gPeers[s].loaded = 0;
	}
}

char NET_IsRunning(void)
{
	Log_Printf("NET_IsRunning\n");
	return (net.state == NET_RUNNING);
}

// v2 P2: true from the moment a match is being set up until it is over -- the
// level boundaries included (between acts the state drops back to NET_STARTED).
// NET_IsRunning() alone was the wrong question to ask on a disconnect: at a
// level boundary it answers "no", and the GameKit callback then took the
// matchmaking-abort path, which frees the session WITHOUT reloading the menu
// scene -- leaving the abandoned act simulating behind the menu.
char NET_IsInMatch(void)
{
	return (net.numSeats > 0 && net.state >= NET_STARTED);
}

uint NET_GetDropedPackets(void)
{
	return net.numDropedPackets;
}


#endif
