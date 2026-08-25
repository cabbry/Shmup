/*
 *  peer.inc -- one PEER instance of the REAL engine/src/netchannel.c.
 *
 *  Included four times (peer0.c .. peer3.c), each with PEER_ID set. Every
 *  symbol netchannel.c defines, and every piece of engine state it touches,
 *  is macro-renamed to <name>_<id> so the four instances coexist in one
 *  process with genuinely separate state -- the way four devices do. Nothing
 *  in netchannel.c is modified or transcribed: it is compiled verbatim.
 */
#define PPCAT_(a,b) a##b
#define PPCAT(a,b)  PPCAT_(a,b)
#define PS(name)    PPCAT(name##_, PEER_ID)

/* --- state netchannel.c defines --- */
#define net                 PS(net)
#define browseRef           PS(browseRef)
#define registerRef         PS(registerRef)
#define resolveRef          PS(resolveRef)
#define ownAddr             PS(ownAddr)
#define ownAddrValid        PS(ownAddrValid)
#define serviceInterfaces   PS(serviceInterfaces)
#define outSequenceNumber   PS(outSequenceNumber)
#define inSequenceNumber    PS(inSequenceNumber)
#define lastFullUpdateTime  PS(lastFullUpdateTime)

/* --- entry points --- */
#define NET_IsOnline                        PS(NET_IsOnline)
#define NET_OnNetworkDataFrom               PS(NET_OnNetworkDataFrom)
#define NET_StartOnlineMatch                PS(NET_StartOnlineMatch)
#define NET_AbortOnlineMatch                PS(NET_AbortOnlineMatch)
#define NET_Free                            PS(NET_Free)
#define NET_IsNetworkAvailable              PS(NET_IsNetworkAvailable)
#define NET_GetAddressForInterfaceName      PS(NET_GetAddressForInterfaceName)
#define NET_InterfaceIndexForInterfaceName  PS(NET_InterfaceIndexForInterfaceName)
#define DNSServiceRegisterReplyCallback     PS(DNSServiceRegisterReplyCallback)
#define DNSServiceBrowseReplyCallback       PS(DNSServiceBrowseReplyCallback)
#define DNSServiceResolveReplyCallback      PS(DNSServiceResolveReplyCallback)
#define DNSServiceQueryRecordReplyCallback  PS(DNSServiceQueryRecordReplyCallback)
#define NET_CheckServerAvailability         PS(NET_CheckServerAvailability)
#define NET_ResolveNetworkServer            PS(NET_ResolveNetworkServer)
#define NET_CreateSocket                    PS(NET_CreateSocket)
#define NET_OnPeerLost                      PS(NET_OnPeerLost)
#define NET_OnSeatLost                      PS(NET_OnSeatLost)
#define Net_ProcessSetupPacket              PS(Net_ProcessSetupPacket)
#define NET_Setup                           PS(NET_Setup)
#define NET_IsInitialized                   PS(NET_IsInitialized)
#define NET_Receive                         PS(NET_Receive)
#define NET_Send                            PS(NET_Send)
#define Net_SendDie                         PS(Net_SendDie)
#define NET_Init                            PS(NET_Init)
#define NET_OnNextLevelLoad                 PS(NET_OnNextLevelLoad)
#define NET_IsRunning                       PS(NET_IsRunning)
#define NET_GetDropedPackets                PS(NET_GetDropedPackets)
#define NET_IsInMatch                       PS(NET_IsInMatch)

/* --- engine state this peer owns --- */
#define players            PS(players)
#define numPlayers         PS(numPlayers)
#define controlledPlayer   PS(controlledPlayer)
#define engine             PS(engine)
#define simulationTime     PS(simulationTime)
#define toSend             PS(toSend)
#define commandsBuffers    PS(commandsBuffers)
#define numPlayerRespawn   PS(numPlayerRespawn)
#define gShipChoice        PS(gShipChoice)
#define gBulletColor       PS(gBulletColor)
#define gMPShipChoice      PS(gMPShipChoice)
#define gMPBulletColor     PS(gMPBulletColor)

/* --- engine functions, mocked per peer --- */
#define Log_Printf                     PS(Log_Printf)
#define MENU_Set                       PS(MENU_Set)
#define MENU_GetMultiplayerTextLine    PS(MENU_GetMultiplayerTextLine)
#define SND_PauseSoundTrack            PS(SND_PauseSoundTrack)
#define SND_ResumeSoundTrack           PS(SND_ResumeSoundTrack)
#define Timer_Pause                    PS(Timer_Pause)
#define Timer_Resume                   PS(Timer_Resume)
#define Timer_resetTime                PS(Timer_resetTime)
#define dEngine_RequireSceneId         PS(dEngine_RequireSceneId)
#define dEngine_CheckState             PS(dEngine_CheckState)
#define P_ReloadShip                   PS(P_ReloadShip)
#define Native_GKSendData              PS(Native_GKSendData)
#define Native_CancelOnlineMatchmaking PS(Native_CancelOnlineMatchmaking)

#include "shim/posix_shim.h"
#include "rig_api.h"

/* THE REAL THING */
#include "netchannel.c"

/* --- storage + mocks for everything above, for this peer --- */
player_t          players[MAX_NUM_PLAYERS];
uchar             numPlayers = 1;
uchar             controlledPlayer = 0;
engine_info_t     engine;
int               simulationTime = 0;
command_t         toSend;
command_buffer_t  commandsBuffers[MAX_NUM_PLAYERS];
unsigned char     numPlayerRespawn[] = {6,3,1};
int               gShipChoice = 0;
int               gBulletColor = 0;
int               gMPShipChoice[MAX_NUM_PLAYERS]  = {0,1,2,3};
int               gMPBulletColor[MAX_NUM_PLAYERS] = {0,1,2,3};

static char gMpText[8][256];
char* MENU_GetMultiplayerTextLine(int i) { return gMpText[(i < 0 || i > 7) ? 0 : i]; }

int Log_Printf(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	rig_vlog(PEER_ID, fmt, ap);
	va_end(ap);
	return 0;
}

void MENU_Set(signed char menuId)          { rig_note_menu(PEER_ID, menuId); }
void SND_PauseSoundTrack(void)             {}
void SND_ResumeSoundTrack(void)            {}
void Timer_Pause(void)                     { rig_set_paused(PEER_ID, 1); }
void Timer_Resume(void)                    { rig_set_paused(PEER_ID, 0); }
void Timer_resetTime(void)                 { simulationTime = 0; }
void dEngine_RequireSceneId(int sceneId)   { engine.requiredSceneId = sceneId; }
void dEngine_CheckState(void)              { engine.sceneId = engine.requiredSceneId; rig_note_load(PEER_ID, engine.sceneId); }
void P_ReloadShip(void)                    { rig_note_reload(PEER_ID); }
void Native_GKSendData(const void* d, int len, int reliable) { rig_gk_send(PEER_ID, d, len, reliable); }
void Native_CancelOnlineMatchmaking(void)  {}

/* The driver's window into this peer (one set per peer, named by id). */
void PS(rig_tick)(int ms)
{
	gRigCurrentIp = rig_ip_of(PEER_ID);
	if (!rig_is_paused(PEER_ID))
		simulationTime += ms;
	NET_Setup();
	NET_Receive();
	NET_Send();
}

void PS(rig_start_online)(int mySeat, int numSeats)
{
	gRigCurrentIp = rig_ip_of(PEER_ID);
	NET_StartOnlineMatch(mySeat, numSeats);
}

void PS(rig_init_lan)(void)
{
	gRigCurrentIp = rig_ip_of(PEER_ID);
	int i;
	for (i = 0; i < MAX_NUM_PLAYERS; i++)
	{
		players[i].shouldDraw = 1;		/* as P_ResetPlayer does on the device */
		players[i].playerId = i;
	}
	engine.mode = DE_MODE_MULTIPLAYER;
	engine.numScenes = 5;
	engine.sceneId = 0;
	engine.requiredSceneId = 0;
	NET_Init();
}

void PS(rig_deliver_gk)(int senderSeat, const void* data, int len)
{
	gRigCurrentIp = rig_ip_of(PEER_ID);
	NET_OnNetworkDataFrom(senderSeat, data, len);
}

/* Resolve a peer's ip into THIS peer's Bonjour path: the exact callback the
   real mDNS query would fire, so LAN_AddRosterIp runs for real. */
void PS(rig_resolve_peer)(unsigned int ip)
{
	unsigned int netOrder = htonl(ip);
	gRigCurrentIp = rig_ip_of(PEER_ID);
	DNSServiceQueryRecordReplyCallback((DNSServiceRef)0, 0, 0, kDNSServiceErr_NoError,
	                                   "peer", kDNSServiceType_A, kDNSServiceClass_IN,
	                                   4, &netOrder, 120, 0);
}

void PS(rig_next_level)(void)
{
	gRigCurrentIp = rig_ip_of(PEER_ID);
	NET_OnNextLevelLoad();
}

/* Read-only probes for the assertions. */
int  PS(rig_state)(void)     { return net.state; }
int  PS(rig_seat)(void)      { return net.ownSeat; }
int  PS(rig_numseats)(void)  { return net.numSeats; }
int  PS(rig_numplayers)(void){ return numPlayers; }
int  PS(rig_controlled)(void){ return controlledPlayer; }
int  PS(rig_lives)(void)     { return players[0].respawnCounter; }
int  PS(rig_scene)(void)     { return engine.sceneId; }
int  PS(rig_drawn)(int s)    { return players[s].shouldDraw; }
int  PS(rig_color)(int s)    { return gMPBulletColor[s]; }
int  PS(rig_ship)(int s)     { return gMPShipChoice[s]; }
void PS(rig_set_loadout)(int ship, int color) { gShipChoice = ship; gBulletColor = color; }
void PS(rig_set_lives)(int n) { int i; for (i=0;i<MAX_NUM_PLAYERS;i++) players[i].respawnCounter = n; }
