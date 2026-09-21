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
 *  dnssd_win.c -- a small mDNS responder and browser behind dns_sd.h
 *  (round 88, the LAN on Windows).
 *
 *  What mDNSResponder does for the iPhone, done by hand for one service:
 *  one UDP socket on 224.0.0.251:5353, our own PTR / SRV / TXT / A records
 *  announced and answered, PTR / SRV / A queries sent for the peers. The
 *  iPhone needs no change: it sees a service like its own. Names are kept
 *  as DNS-SD spells them ("Dodge shmup server._DodgeServer._udp.local."),
 *  compared without case.
 *
 *  The contract with netchannel.c is the API's: a ref, its socket to select
 *  on, ProcessResult when readable, callbacks. Packets are only READ inside
 *  ProcessResult (so the game's select stays true); what has to go out on a
 *  clock -- announcements, query retries -- goes out from DNSSD_WIN_Tick,
 *  which win/main.c calls every frame.
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>		// before dns_sd.h: its if_indextoname macro must not touch netioapi's declaration
#include "dns_sd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "log.h"

#define MDNS_PORT      5353
#define MDNS_GROUP     "224.0.0.251"
#define MAX_REFS       16
#define MAX_INSTANCES  8
#define MAX_HOSTS      8
#define NAME_MAX_LEN   256
#define IFINDEX_LAN    10		// what the browse callback sees; netchannel maps it to "lan" via if_indextoname

enum { REF_FREE = 0, REF_REGISTER, REF_BROWSE, REF_RESOLVE, REF_QUERY };

struct dnssd_ref_t
{
	int      kind;
	char     name[NAME_MAX_LEN];		// browse: the regtype; resolve: the instance fullname; query: the host fullname
	void*    cb;
	void*    ctx;
	int      delivered[MAX_INSTANCES];	// browse: instance slots already announced
	int      done;						// resolve / query: answered once
	uint32_t lastSentMs;
};

typedef struct { char full[NAME_MAX_LEN]; char target[NAME_MAX_LEN]; uint16_t port; int hasSrv; int used; } instance_t;
typedef struct { char host[NAME_MAX_LEN]; struct in_addr ip; int used; } host_t;

static struct dnssd_ref_t gRefs[MAX_REFS];
static instance_t gInst[MAX_INSTANCES];
static host_t     gHosts[MAX_HOSTS];
static SOCKET     gSock = INVALID_SOCKET;
static int        gWsaUp = 0;
static struct in_addr gLocalIp;
static int        gLocalIpValid = 0;
static char       gOwnHost[NAME_MAX_LEN];		// "pc-name.local."
static char       gOwnInstance[NAME_MAX_LEN];	// "Dodge shmup server (pc-name)._DodgeServer._udp.local."
static char       gOwnType[NAME_MAX_LEN];		// "_DodgeServer._udp.local."
static uint16_t   gOwnPort = 0;					// host order
static int        gRegistered = 0;
static uint32_t   gRegisteredAt = 0, gLastAnnounceMs = 0;
static uint16_t   gTxId = 1;

// ---------------------------------------------------------------------------
//  Small helpers
// ---------------------------------------------------------------------------
static uint32_t NowMs(void) { return (uint32_t)GetTickCount(); }

static int NameEq(const char* a, const char* b)
{
	// case-insensitive, a trailing dot optional on either side
	size_t la = strlen(a), lb = strlen(b);
	if (la && a[la - 1] == '.') la--;
	if (lb && b[lb - 1] == '.') lb--;
	if (la != lb) return 0;
	return _strnicmp(a, b, la) == 0;
}

static void WithDot(char* name)
{
	size_t n = strlen(name);
	if (n && name[n - 1] != '.' && n + 1 < NAME_MAX_LEN) { name[n] = '.'; name[n + 1] = 0; }
}

// "a.b.c." -> the labels, honouring '\.' escapes in an instance label
static int PutName(unsigned char* buf, int cap, int off, const char* name)
{
	const char* p = name;
	while (*p)
	{
		unsigned char label[64]; int ll = 0;
		while (*p && !(*p == '.' ) && ll < 63)
		{
			if (*p == '\\' && p[1]) p++;
			label[ll++] = (unsigned char)*p++;
		}
		if (*p == '.') p++;
		if (ll == 0) break;
		if (off + 1 + ll >= cap) return -1;
		buf[off++] = (unsigned char)ll;
		memcpy(buf + off, label, ll); off += ll;
	}
	if (off >= cap) return -1;
	buf[off++] = 0;
	return off;
}

static int PutU16(unsigned char* b, int off, unsigned v) { b[off] = (unsigned char)(v >> 8); b[off + 1] = (unsigned char)v; return off + 2; }
static int PutU32(unsigned char* b, int off, uint32_t v) { b[off] = (unsigned char)(v >> 24); b[off+1] = (unsigned char)(v >> 16); b[off+2] = (unsigned char)(v >> 8); b[off+3] = (unsigned char)v; return off + 4; }
static unsigned GetU16(const unsigned char* b, int off) { return ((unsigned)b[off] << 8) | b[off + 1]; }

// Reads a possibly compressed name at off into out ("a.b.c."); returns the
// offset after it in the message, or -1.
static int ReadName(const unsigned char* msg, int len, int off, char* out)
{
	int outLen = 0, jumped = 0, ret = -1, hops = 0;
	out[0] = 0;
	while (off < len && hops < 64)
	{
		unsigned c = msg[off];
		if (c == 0) { off++; break; }
		if ((c & 0xC0) == 0xC0)
		{
			int ptr;
			if (off + 1 >= len) return -1;
			ptr = ((c & 0x3F) << 8) | msg[off + 1];
			if (!jumped) ret = off + 2;
			jumped = 1; hops++;
			if (ptr >= len) return -1;
			off = ptr;
			continue;
		}
		off++;
		if (off + (int)c > len) return -1;
		if (outLen + (int)c + 2 >= NAME_MAX_LEN) return -1;
		{
			unsigned k;
			for (k = 0; k < c; k++)
			{
				char ch = (char)msg[off + k];
				if (ch == '.') { if (outLen + 2 >= NAME_MAX_LEN) return -1; out[outLen++] = '\\'; }
				out[outLen++] = ch;
			}
		}
		out[outLen++] = '.';
		off += c;
	}
	out[outLen] = 0;
	return jumped ? ret : off;
}

// ---------------------------------------------------------------------------
//  The socket and our identity
// ---------------------------------------------------------------------------
int DNSSD_WIN_LocalIPv4(struct in_addr* out)
{
	ULONG size = 0;
	IP_ADAPTER_ADDRESSES* list;
	IP_ADAPTER_ADDRESSES* a;
	struct in_addr best; int bestScore = -1;
	if (!gWsaUp) { WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w) == 0) gWsaUp = 1; }
	if (gLocalIpValid) { *out = gLocalIp; return 1; }
	memset(&best, 0, sizeof(best));
	GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, NULL, &size);
	if (size == 0) return 0;
	list = (IP_ADAPTER_ADDRESSES*)malloc(size);
	if (!list) return 0;
	if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, list, &size) != NO_ERROR)
	{
		free(list);
		return 0;
	}
	for (a = list; a; a = a->Next)
	{
		IP_ADAPTER_UNICAST_ADDRESS* u;
		int score;
		if (a->OperStatus != IfOperStatusUp) continue;
		if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->IfType == IF_TYPE_TUNNEL) continue;
		// prefer the adapter that routes to the world (a gateway), then Wi-Fi/Ethernet over the rest
		score = (a->FirstGatewayAddress ? 10 : 0) + ((a->IfType == IF_TYPE_ETHERNET_CSMACD || a->IfType == IF_TYPE_IEEE80211) ? 1 : 0);
		for (u = a->FirstUnicastAddress; u; u = u->Next)
		{
			struct sockaddr_in* sin = (struct sockaddr_in*)u->Address.lpSockaddr;
			if (sin->sin_family != AF_INET) continue;
			if ((ntohl(sin->sin_addr.s_addr) & 0xFF000000u) == 0x7F000000u) continue;		// 127/8
			if ((ntohl(sin->sin_addr.s_addr) & 0xFFFF0000u) == 0xA9FE0000u) score -= 5;		// 169.254/16 last
			if (score > bestScore) { bestScore = score; best = sin->sin_addr; }
			break;
		}
	}
	free(list);
	if (bestScore < 0) return 0;
	gLocalIp = best; gLocalIpValid = 1;
	*out = best;
	return 1;
}

static void OwnHostName(void)
{
	char pc[64] = "shmup-pc"; DWORD n = sizeof(pc);
	int i, j = 0;
	if (gOwnHost[0]) return;
	GetComputerNameA(pc, &n);
	if (getenv("SHMUP_MDNS_HOST"))		// the bench: two processes on one PC need two host names
		strncpy(pc, getenv("SHMUP_MDNS_HOST"), sizeof(pc) - 1), pc[sizeof(pc) - 1] = 0;
	for (i = 0; pc[i] && j < 60; i++)
	{
		char c = (char)tolower((unsigned char)pc[i]);
		gOwnHost[j++] = (isalnum((unsigned char)c) ? c : '-');
	}
	if (j == 0) gOwnHost[j++] = 'p';
	gOwnHost[j] = 0;
	strcat(gOwnHost, ".local.");
}

static int EnsureSocket(void)
{
	struct sockaddr_in local;
	struct ip_mreq mreq;
	int yes = 1; u_long nb = 1; unsigned char ttl = 255, loop = 1;	// loop on: our echoes are filtered by name, and two processes on one PC (the bench) must hear each other
	struct in_addr ip;
	if (gSock != INVALID_SOCKET) return 1;
	if (!DNSSD_WIN_LocalIPv4(&ip)) { Log_Printf("[mdns] no LAN address\n"); return 0; }
	gSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (gSock == INVALID_SOCKET) return 0;
	setsockopt(gSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = htons(MDNS_PORT);
	if (bind(gSock, (struct sockaddr*)&local, sizeof(local)) != 0)
	{
		// 5353 held exclusively by someone: an ephemeral port still sends
		// queries (answers come back unicast) and announcements.
		local.sin_port = 0;
		if (bind(gSock, (struct sockaddr*)&local, sizeof(local)) != 0)
		{
			Log_Printf("[mdns] bind failed (%d)\n", WSAGetLastError());
			closesocket(gSock); gSock = INVALID_SOCKET;
			return 0;
		}
		Log_Printf("[mdns] port 5353 busy: queries only answered unicast, announcing on a clock\n");
	}
	mreq.imr_multiaddr.s_addr = inet_addr(MDNS_GROUP);
	mreq.imr_interface = ip;
	setsockopt(gSock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
	setsockopt(gSock, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&ip, sizeof(ip));
	setsockopt(gSock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
	setsockopt(gSock, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof(loop));
	ioctlsocket(gSock, FIONBIO, &nb);
	OwnHostName();
	Log_Printf("[mdns] up on %s as %s\n", inet_ntoa(ip), gOwnHost);
	return 1;
}

static void SendTo(const unsigned char* msg, int len, const struct sockaddr_in* to)
{
	struct sockaddr_in group;
	if (gSock == INVALID_SOCKET) return;
	if (!to)
	{
		memset(&group, 0, sizeof(group));
		group.sin_family = AF_INET;
		group.sin_addr.s_addr = inet_addr(MDNS_GROUP);
		group.sin_port = htons(MDNS_PORT);
		to = &group;
	}
	sendto(gSock, (const char*)msg, len, 0, (const struct sockaddr*)to, sizeof(*to));
}

// ---------------------------------------------------------------------------
//  Building: queries, and our own answers
// ---------------------------------------------------------------------------
static void SendQuery(const char* name, unsigned rrtype)
{
	unsigned char m[512]; int off;
	if (!EnsureSocket()) return;
	memset(m, 0, 12);
	off = PutU16(m, 0, 0);			// mDNS: transaction id 0
	off = PutU16(m, off, 0);		// flags: standard query
	off = PutU16(m, off, 1);		// one question
	off = 12;
	off = PutName(m, sizeof(m), off, name);
	if (off < 0) return;
	off = PutU16(m, off, rrtype);
	off = PutU16(m, off, kDNSServiceClass_IN);	// QM: everybody hears the answer
	SendTo(m, off, NULL);
}

// One record: name, type, class (cache-flush optional), ttl, rdata.
static int PutRecord(unsigned char* m, int cap, int off, const char* name, unsigned type, int flush, uint32_t ttl,
                     const unsigned char* rdata, int rdlen)
{
	off = PutName(m, cap, off, name); if (off < 0) return -1;
	if (off + 10 + rdlen > cap) return -1;
	off = PutU16(m, off, type);
	off = PutU16(m, off, kDNSServiceClass_IN | (flush ? 0x8000 : 0));
	off = PutU32(m, off, ttl);
	off = PutU16(m, off, (unsigned)rdlen);
	memcpy(m + off, rdata, rdlen); off += rdlen;
	return off;
}

// Our four records, as a response: PTR, SRV, TXT, A, as selected. A legacy
// unicast reply (RFC 6762 6.7) copies the query's id, keeps the ttl at 10
// and sets no cache-flush bit; multicast replies carry id 0 as mDNS does.
static void SendOurRecords(int wantPtr, int wantSrv, int wantTxt, int wantA, const struct sockaddr_in* to, unsigned queryId, int legacy,
                           const char* qname, unsigned qtype)
{
	unsigned char m[1024], rd[300]; int off, n = 0, rl;
	int flush = legacy ? 0 : 1;
	uint32_t ttlLong = legacy ? 10 : 4500, ttlShort = legacy ? 10 : 120;
	if (!gRegistered || !EnsureSocket()) return;
	memset(m, 0, 12);
	PutU16(m, 0, legacy ? queryId : 0);
	off = 12;
	m[2] = 0x84;	// response, authoritative
	if (legacy && qname)
	{
		// a legacy resolver expects its question echoed (RFC 6762 6.7)
		off = PutName(m, sizeof(m), off, qname);
		if (off < 0) return;
		off = PutU16(m, off, qtype);
		off = PutU16(m, off, kDNSServiceClass_IN);
		PutU16(m, 4, 1);
	}
	if (wantPtr)
	{
		rl = PutName(rd, sizeof(rd), 0, gOwnInstance);
		off = PutRecord(m, sizeof(m), off, gOwnType, kDNSServiceType_PTR, 0, ttlLong, rd, rl); n++;
	}
	if (wantSrv)
	{
		rl = 0; rl = PutU16(rd, rl, 0); rl = PutU16(rd, rl, 0); rl = PutU16(rd, rl, gOwnPort);
		rl = PutName(rd, sizeof(rd), rl, gOwnHost);
		off = PutRecord(m, sizeof(m), off, gOwnInstance, kDNSServiceType_SRV, flush, ttlShort, rd, rl); n++;
	}
	if (wantTxt)
	{
		rd[0] = 0;	// an empty TXT is one zero-length string
		off = PutRecord(m, sizeof(m), off, gOwnInstance, kDNSServiceType_TXT, flush, ttlShort, rd, 1); n++;
	}
	if (wantA)
	{
		memcpy(rd, &gLocalIp, 4);
		off = PutRecord(m, sizeof(m), off, gOwnHost, kDNSServiceType_A, flush, ttlShort, rd, 4); n++;
	}
	if (off < 0) return;
	PutU16(m, 6, (unsigned)n);		// answer count
	SendTo(m, off, to);
	if (to)
		Log_Printf("[mdns] answered %d record(s) to %s:%u%s\n", n, inet_ntoa(to->sin_addr), ntohs(to->sin_port), legacy ? " (legacy)" : "");
}

// ---------------------------------------------------------------------------
//  Caches: what the LAN told us
// ---------------------------------------------------------------------------
static instance_t* FindInstance(const char* full, int create)
{
	int i, freeSlot = -1;
	for (i = 0; i < MAX_INSTANCES; i++)
	{
		if (gInst[i].used && NameEq(gInst[i].full, full)) return &gInst[i];
		if (!gInst[i].used && freeSlot < 0) freeSlot = i;
	}
	if (!create || freeSlot < 0) return NULL;
	memset(&gInst[freeSlot], 0, sizeof(instance_t));
	strncpy(gInst[freeSlot].full, full, NAME_MAX_LEN - 1);
	WithDot(gInst[freeSlot].full);
	gInst[freeSlot].used = 1;
	return &gInst[freeSlot];
}

static host_t* FindHost(const char* host, int create)
{
	int i, freeSlot = -1;
	for (i = 0; i < MAX_HOSTS; i++)
	{
		if (gHosts[i].used && NameEq(gHosts[i].host, host)) return &gHosts[i];
		if (!gHosts[i].used && freeSlot < 0) freeSlot = i;
	}
	if (!create) return NULL;
	if (freeSlot < 0) freeSlot = 0;		// recycle: eight hosts is plenty for a party of four
	memset(&gHosts[freeSlot], 0, sizeof(host_t));
	strncpy(gHosts[freeSlot].host, host, NAME_MAX_LEN - 1);
	WithDot(gHosts[freeSlot].host);
	gHosts[freeSlot].used = 1;
	return &gHosts[freeSlot];
}

// A record of a response (answer or additional).
static void Learn(const unsigned char* msg, int len, const char* name, unsigned type, uint32_t ttl, int rdOff, int rdLen)
{
	char tmp[NAME_MAX_LEN];
	if (type == kDNSServiceType_PTR && NameEq(name, gOwnType))
	{
		if (ReadName(msg, len, rdOff, tmp) < 0) return;
		if (gRegistered && NameEq(tmp, gOwnInstance)) return;		// our own echo
		if (ttl == 0) { instance_t* in = FindInstance(tmp, 0); if (in) in->used = 0; return; }	// goodbye
		FindInstance(tmp, 1);
	}
	else if (type == kDNSServiceType_SRV)
	{
		instance_t* in;
		if (rdLen < 7) return;
		if (gRegistered && NameEq(name, gOwnInstance)) return;
		in = FindInstance(name, 1);
		if (!in) return;
		in->port = (uint16_t)GetU16(msg, rdOff + 4);
		if (ReadName(msg, len, rdOff + 6, tmp) < 0) return;
		strncpy(in->target, tmp, NAME_MAX_LEN - 1);
		in->hasSrv = (ttl != 0);
	}
	else if (type == kDNSServiceType_A && rdLen == 4)
	{
		host_t* h;
		if (gRegistered && NameEq(name, gOwnHost)) return;
		h = FindHost(name, 1);
		memcpy(&h->ip, msg + rdOff, 4);
	}
}

// One packet from the wire: answer what asks about us, learn what is answered.
static void Process(const unsigned char* msg, int len, const struct sockaddr_in* from)
{
	int qd, an, ns, ar, i, off = 12;
	int wantPtr = 0, wantSrv = 0, wantTxt = 0, wantA = 0, unicast = 0;
	char name[NAME_MAX_LEN], qname[NAME_MAX_LEN] = ""; unsigned qtype = 0;
	if (len < 12) return;
	if (msg[2] & 0x80) { qd = 0; }	// a response: nothing to answer, only to learn
	qd = (msg[2] & 0x80) ? 0 : (int)GetU16(msg, 4);
	an = (int)GetU16(msg, 6); ns = (int)GetU16(msg, 8); ar = (int)GetU16(msg, 10);
	// RFC 6762 6.7: a query from a port other than 5353 is a legacy unicast
	// query (nslookup, a resolver): the answer goes back unicast to it.
	if (from && ntohs(from->sin_port) != MDNS_PORT) unicast = 1;
	if (qd == 0 && (msg[2] & 0x80) == 0) qd = (int)GetU16(msg, 4);
	for (i = 0; i < qd; i++)
	{
		unsigned type, cls;
		off = ReadName(msg, len, off, name);
		if (off < 0 || off + 4 > len) return;
		type = GetU16(msg, off); cls = GetU16(msg, off + 2); off += 4;
		if (i == 0) { strncpy(qname, name, NAME_MAX_LEN - 1); qtype = type; }
		if (cls & 0x8000) unicast = 1;
		if (!gRegistered) continue;
		if ((type == kDNSServiceType_PTR || type == 255) && NameEq(name, gOwnType)) wantPtr = wantSrv = wantTxt = wantA = 1;
		if ((type == kDNSServiceType_SRV || type == 255) && NameEq(name, gOwnInstance)) wantSrv = wantA = 1;
		if ((type == kDNSServiceType_TXT || type == 255) && NameEq(name, gOwnInstance)) wantTxt = 1;
		if ((type == kDNSServiceType_A || type == 255) && NameEq(name, gOwnHost)) wantA = 1;
	}
	if (wantPtr || wantSrv || wantTxt || wantA)
	{
		int legacy = from && ntohs(from->sin_port) != MDNS_PORT;
		SendOurRecords(wantPtr, wantSrv, wantTxt, wantA, unicast ? from : NULL, GetU16(msg, 0), legacy, qname, qtype);
	}
	for (i = 0; i < an + ns + ar; i++)
	{
		unsigned type; uint32_t ttl; int rdLen;
		off = ReadName(msg, len, off, name);
		if (off < 0 || off + 10 > len) return;
		type = GetU16(msg, off);
		ttl = ((uint32_t)GetU16(msg, off + 4) << 16) | GetU16(msg, off + 6);
		rdLen = (int)GetU16(msg, off + 8);
		off += 10;
		if (off + rdLen > len) return;
		Learn(msg, len, name, type, ttl, off, rdLen);
		off += rdLen;
	}
}

static void Drain(void)
{
	unsigned char buf[2048];
	struct sockaddr_in from; int flen; int n, k;
	if (gSock == INVALID_SOCKET) return;
	for (k = 0; k < 64; k++)
	{
		flen = sizeof(from);
		n = recvfrom(gSock, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &flen);
		if (n <= 0) break;
		Process(buf, n, &from);
	}
}

// ---------------------------------------------------------------------------
//  The API
// ---------------------------------------------------------------------------
static struct dnssd_ref_t* NewRef(int kind)
{
	int i;
	for (i = 0; i < MAX_REFS; i++)
		if (gRefs[i].kind == REF_FREE)
		{
			memset(&gRefs[i], 0, sizeof(gRefs[i]));
			gRefs[i].kind = kind;
			return &gRefs[i];
		}
	return NULL;
}

static void FullTypeName(char* out, const char* regtype, const char* domain)
{
	snprintf(out, NAME_MAX_LEN, "%s", regtype);
	WithDot(out);
	strncat(out, (domain && domain[0]) ? domain : "local.", NAME_MAX_LEN - strlen(out) - 1);
	WithDot(out);
}

DNSServiceErrorType DNSServiceRegister(DNSServiceRef* sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                                       const char* name, const char* regtype, const char* domain, const char* host,
                                       uint16_t portInNetworkByteOrder, uint16_t txtLen, const void* txtRecord,
                                       DNSServiceRegisterReply callBack, void* context)
{
	struct dnssd_ref_t* r;
	char pc[64];
	(void)flags; (void)interfaceIndex; (void)host; (void)txtLen; (void)txtRecord;
	if (!EnsureSocket()) return kDNSServiceErr_Unknown;
	r = NewRef(REF_REGISTER);
	if (!r) return kDNSServiceErr_NoMemory;
	FullTypeName(gOwnType, regtype, domain);
	// our instance carries the PC's name: two devices may register the same
	// label, and we have no conflict resolution to rename ours
	strncpy(pc, gOwnHost, sizeof(pc) - 1); pc[sizeof(pc) - 1] = 0;
	{ char* d = strchr(pc, '.'); if (d) *d = 0; }
	snprintf(gOwnInstance, NAME_MAX_LEN, "%s (%s).%s", name, pc, gOwnType);
	gOwnPort = ntohs(portInNetworkByteOrder);
	gRegistered = 1;
	gRegisteredAt = NowMs();
	gLastAnnounceMs = 0;
	*sdRef = r;
	Log_Printf("[mdns] registered %s port %u\n", gOwnInstance, gOwnPort);
	if (callBack) callBack(r, 0, kDNSServiceErr_NoError, name, regtype, "local.", context);
	return kDNSServiceErr_NoError;
}

DNSServiceErrorType DNSServiceBrowse(DNSServiceRef* sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                                     const char* regtype, const char* domain, DNSServiceBrowseReply callBack, void* context)
{
	struct dnssd_ref_t* r;
	(void)flags; (void)interfaceIndex;
	if (!EnsureSocket()) return kDNSServiceErr_Unknown;
	r = NewRef(REF_BROWSE);
	if (!r) return kDNSServiceErr_NoMemory;
	FullTypeName(r->name, regtype, domain);
	r->cb = (void*)callBack; r->ctx = context;
	SendQuery(r->name, kDNSServiceType_PTR);
	r->lastSentMs = NowMs();
	*sdRef = r;
	return kDNSServiceErr_NoError;
}

DNSServiceErrorType DNSServiceResolve(DNSServiceRef* sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                                      const char* name, const char* regtype, const char* domain,
                                      DNSServiceResolveReply callBack, void* context)
{
	struct dnssd_ref_t* r;
	char type[NAME_MAX_LEN];
	(void)flags; (void)interfaceIndex;
	if (!EnsureSocket()) return kDNSServiceErr_Unknown;
	r = NewRef(REF_RESOLVE);
	if (!r) return kDNSServiceErr_NoMemory;
	FullTypeName(type, regtype, domain);
	snprintf(r->name, NAME_MAX_LEN, "%s.%s", name, type);
	r->cb = (void*)callBack; r->ctx = context;
	SendQuery(r->name, kDNSServiceType_SRV);
	r->lastSentMs = NowMs();
	*sdRef = r;
	return kDNSServiceErr_NoError;
}

DNSServiceErrorType DNSServiceQueryRecord(DNSServiceRef* sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                                          const char* fullname, uint16_t rrtype, uint16_t rrclass,
                                          DNSServiceQueryRecordReply callBack, void* context)
{
	struct dnssd_ref_t* r;
	(void)flags; (void)interfaceIndex; (void)rrclass;
	if (rrtype != kDNSServiceType_A) return kDNSServiceErr_Unknown;	// the game only ever asks for an address
	if (!EnsureSocket()) return kDNSServiceErr_Unknown;
	r = NewRef(REF_QUERY);
	if (!r) return kDNSServiceErr_NoMemory;
	strncpy(r->name, fullname, NAME_MAX_LEN - 1);
	WithDot(r->name);
	r->cb = (void*)callBack; r->ctx = context;
	SendQuery(r->name, kDNSServiceType_A);
	r->lastSentMs = NowMs();
	*sdRef = r;
	return kDNSServiceErr_NoError;
}

int DNSServiceRefSockFD(DNSServiceRef sdRef)
{
	if (!sdRef || sdRef->kind == REF_FREE || gSock == INVALID_SOCKET) return -1;
	return (int)gSock;
}

// The label of an instance fullname, unescaped, as the browse callback hands it.
static void InstanceLabel(const char* full, char* out)
{
	int i = 0, j = 0;
	while (full[i] && j < NAME_MAX_LEN - 1)
	{
		if (full[i] == '\\' && full[i + 1]) { out[j++] = full[i + 1]; i += 2; continue; }
		if (full[i] == '.') break;
		out[j++] = full[i++];
	}
	out[j] = 0;
}

DNSServiceErrorType DNSServiceProcessResult(DNSServiceRef r)
{
	int i;
	if (!r || r->kind == REF_FREE) return kDNSServiceErr_Unknown;
	Drain();
	switch (r->kind)
	{
		case REF_BROWSE:
			for (i = 0; i < MAX_INSTANCES; i++)
			{
				char label[NAME_MAX_LEN];
				if (!gInst[i].used || r->delivered[i]) continue;
				if (!NameEq(gInst[i].full + strlen(gInst[i].full) - (strlen(r->name) < strlen(gInst[i].full) ? strlen(r->name) : 0), r->name)) continue;
				r->delivered[i] = 1;
				InstanceLabel(gInst[i].full, label);
				Log_Printf("[mdns] browse: %s\n", gInst[i].full);
				((DNSServiceBrowseReply)r->cb)(r, kDNSServiceFlagsAdd, IFINDEX_LAN, kDNSServiceErr_NoError, label, "_DodgeServer._udp.", "local.", r->ctx);
			}
			break;
		case REF_RESOLVE:
			if (!r->done)
			{
				instance_t* in = FindInstance(r->name, 0);
				if (in && in->hasSrv)
				{
					unsigned char txt = 0;
					r->done = 1;
					Log_Printf("[mdns] resolve: %s -> %s:%u\n", in->full, in->target, in->port);
					((DNSServiceResolveReply)r->cb)(r, 0, IFINDEX_LAN, kDNSServiceErr_NoError, in->full, in->target, htons(in->port), 0, &txt, r->ctx);
				}
			}
			break;
		case REF_QUERY:
			if (!r->done)
			{
				host_t* h = FindHost(r->name, 0);
				if (h)
				{
					r->done = 1;
					Log_Printf("[mdns] address: %s -> %s\n", h->host, inet_ntoa(h->ip));
					((DNSServiceQueryRecordReply)r->cb)(r, kDNSServiceFlagsAdd, IFINDEX_LAN, kDNSServiceErr_NoError, h->host, kDNSServiceType_A, kDNSServiceClass_IN, 4, &h->ip, 120, r->ctx);
				}
			}
			break;
		default:
			break;
	}
	return kDNSServiceErr_NoError;
}

void DNSServiceRefDeallocate(DNSServiceRef r)
{
	int i, alive = 0;
	if (!r) return;
	if (r->kind == REF_REGISTER)
	{
		// goodbye: the PTR with ttl 0, so the peers drop us at once
		unsigned char m[512], rd[300]; int off, rl;
		if (gRegistered && gSock != INVALID_SOCKET)
		{
			memset(m, 0, 12); m[2] = 0x84; off = 12;
			rl = PutName(rd, sizeof(rd), 0, gOwnInstance);
			off = PutRecord(m, sizeof(m), off, gOwnType, kDNSServiceType_PTR, 0, 0, rd, rl);
			if (off > 0) { PutU16(m, 6, 1); SendTo(m, off, NULL); }
		}
		gRegistered = 0;
	}
	r->kind = REF_FREE;
	for (i = 0; i < MAX_REFS; i++) if (gRefs[i].kind != REF_FREE) alive = 1;
	if (!alive)
	{
		// a session ended: forget the peers so the next one starts clean
		memset(gInst, 0, sizeof(gInst));
		memset(gHosts, 0, sizeof(gHosts));
	}
}

char* DNSSD_WIN_IfName(unsigned int ifindex, char* ifname)
{
	strcpy(ifname, ifindex == 1 ? "lo" : "lan");
	return ifname;
}

// Once per frame from win/main.c: what goes out on a clock.
void DNSSD_WIN_Tick(void)
{
	uint32_t now = NowMs();
	int i;
	if (gSock == INVALID_SOCKET) return;
	if (gRegistered)
	{
		// announce: every second for the first five, then every three -- the
		// peers' caches never go stale and a late iPhone hears us at once
		uint32_t period = (now - gRegisteredAt < 5000) ? 1000 : 3000;
		if (now - gLastAnnounceMs >= period)
		{
			SendOurRecords(1, 1, 1, 1, NULL, 0, 0, NULL, 0);
			gLastAnnounceMs = now;
		}
	}
	for (i = 0; i < MAX_REFS; i++)
	{
		struct dnssd_ref_t* r = &gRefs[i];
		if (r->kind == REF_BROWSE && now - r->lastSentMs >= 1000)
		{
			int known = 0, k;
			for (k = 0; k < MAX_INSTANCES; k++) if (gInst[k].used) known++;
			if (known == 0 || (now - r->lastSentMs >= 4000))	// keep asking, slower once someone answered
			{
				SendQuery(r->name, kDNSServiceType_PTR);
				r->lastSentMs = now;
			}
		}
		else if ((r->kind == REF_RESOLVE || r->kind == REF_QUERY) && !r->done && now - r->lastSentMs >= 400)
		{
			SendQuery(r->name, r->kind == REF_RESOLVE ? kDNSServiceType_SRV : kDNSServiceType_A);
			r->lastSentMs = now;
		}
	}
}
