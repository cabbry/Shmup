/*
 *  net_bus.c -- a fake UDP network in memory, plus the mDNS discovery the rig
 *  drives by hand. The REAL netchannel.c runs on top of this unchanged: the
 *  socket calls are macro-redirected here (posix_shim.h), so the LAN roster,
 *  the per-seat broadcast and LAN_SeatForAddr are exercised for real.
 *
 *  Every peer instance has its own socket handle; a datagram is delivered to
 *  whichever socket is bound to the destination ip's port. Loss and reorder
 *  are scriptable (rig_bus_loss).
 */
#include "shim/posix_shim.h"
#include "shim/dns_sd.h"
#include <stdio.h>
#include <stdlib.h>

#define BUS_MAX_SOCKETS 8
#define BUS_MAX_QUEUE   256
#define BUS_MTU         2048

typedef struct bus_dgram_t {
	unsigned char data[BUS_MTU];
	int  len;
	unsigned int fromIp;	/* host order */
} bus_dgram_t;

typedef struct bus_socket_t {
	int used;
	unsigned int ip;		/* host order: which peer owns this socket */
	unsigned short port;
	bus_dgram_t q[BUS_MAX_QUEUE];
	int head, tail;
} bus_socket_t;

static bus_socket_t gSockets[BUS_MAX_SOCKETS];
static int gNextFd = 3;

/* The peer whose engine code is currently executing: set by the driver so a
   send can be stamped with the right source ip and a socket() call attributed. */
unsigned int gRigCurrentIp = 0;
int          gRigDropAll   = 0;	/* simulate "this peer's network died" */
int          gRigDropFromIp = 0;	/* drop everything sent BY this ip */

static bus_socket_t* bus_find(int fd)
{
	int i;
	for (i = 0; i < BUS_MAX_SOCKETS; i++)
		if (gSockets[i].used && gSockets[i].used == fd)
			return &gSockets[i];
	return 0;
}

static bus_socket_t* bus_find_by_ip(unsigned int ip)
{
	int i;
	for (i = 0; i < BUS_MAX_SOCKETS; i++)
		if (gSockets[i].used && gSockets[i].ip == ip)
			return &gSockets[i];
	return 0;
}

void rig_bus_reset(void)
{
	memset(gSockets, 0, sizeof(gSockets));
	gNextFd = 3;
	gRigDropAll = 0;
	gRigDropFromIp = 0;
}

int rig_socket(int domain, int type, int protocol)
{
	int i;
	(void)domain; (void)type; (void)protocol;
	for (i = 0; i < BUS_MAX_SOCKETS; i++)
		if (!gSockets[i].used)
		{
			gSockets[i].used = gNextFd++;
			gSockets[i].ip   = gRigCurrentIp;
			gSockets[i].head = gSockets[i].tail = 0;
			return gSockets[i].used;
		}
	return -1;
}

int rig_bind(int s, const struct sockaddr* addr, socklen_t len)
{
	bus_socket_t* so = bus_find(s);
	const struct sockaddr_in* in = (const struct sockaddr_in*)addr;
	(void)len;
	if (!so) return -1;
	so->port = in->sin_port;
	return 0;
}

int rig_close(int s)
{
	bus_socket_t* so = bus_find(s);
	if (!so) return -1;
	memset(so, 0, sizeof(*so));
	return 0;
}

int rig_fcntl(int s, int cmd, int arg) { (void)s; (void)cmd; (void)arg; return 0; }
int rig_setsockopt(int s, int l, int o, const void* v, socklen_t n) { (void)s;(void)l;(void)o;(void)v;(void)n; return 0; }

int rig_sendto(int s, const void* buf, int len, int flags, const struct sockaddr* to, socklen_t tolen)
{
	const struct sockaddr_in* dst = (const struct sockaddr_in*)to;
	bus_socket_t* src = bus_find(s);
	bus_socket_t* peer;
	int next;
	(void)flags; (void)tolen;

	if (!src) return -1;
	if (gRigDropAll) return len;						/* our uplink is dead */
	if (gRigDropFromIp && src->ip == (unsigned int)gRigDropFromIp) return len;
	if (len > BUS_MTU) return -1;

	peer = bus_find_by_ip(ntohl(dst->sin_addr.s_addr));
	if (!peer) return len;								/* nobody listening: silently lost */

	next = (peer->head + 1) % BUS_MAX_QUEUE;
	if (next == peer->tail) return len;					/* receiver's queue full */
	memcpy(peer->q[peer->head].data, buf, len);
	peer->q[peer->head].len = len;
	peer->q[peer->head].fromIp = src->ip;
	peer->head = next;
	return len;
}

int rig_recvfrom(int s, void* buf, int len, int flags, struct sockaddr* from, socklen_t* fromlen)
{
	bus_socket_t* so = bus_find(s);
	bus_dgram_t* d;
	int n;
	(void)flags;

	if (!so) { errno = EAGAIN; return -1; }
	if (so->tail == so->head) { errno = EAGAIN; return -1; }

	d = &so->q[so->tail];
	n = (d->len < len) ? d->len : len;
	memcpy(buf, d->data, n);
	if (from)
	{
		struct sockaddr_in* in = (struct sockaddr_in*)from;
		memset(in, 0, sizeof(*in));
		in->sin_len = sizeof(*in);
		in->sin_family = AF_INET;
		in->sin_port = htons(31978);
		in->sin_addr.s_addr = htonl(d->fromIp);
		if (fromlen) *fromlen = sizeof(*in);
	}
	so->tail = (so->tail + 1) % BUS_MAX_QUEUE;
	return n;
}

/* The rig never uses select() for real: discovery is injected by the driver. */
int rig_select(int nfds, fd_set* r, fd_set* w, fd_set* e, struct timeval* tv)
{
	(void)nfds; (void)r; (void)w; (void)e; (void)tv;
	return 0;
}

/* One interface, "en0", carrying the current peer's ip. */
static struct ifaddrs     gIfa;
static struct sockaddr_in gIfaAddr;
static char               gIfaName[] = "en0";

int rig_getifaddrs(struct ifaddrs** ifap)
{
	memset(&gIfa, 0, sizeof(gIfa));
	memset(&gIfaAddr, 0, sizeof(gIfaAddr));
	gIfaAddr.sin_len = sizeof(gIfaAddr);
	gIfaAddr.sin_family = AF_INET;
	gIfaAddr.sin_addr.s_addr = htonl(gRigCurrentIp);
	gIfa.ifa_name = gIfaName;
	gIfa.ifa_addr = (struct sockaddr*)&gIfaAddr;
	gIfa.ifa_next = 0;
	*ifap = &gIfa;
	return 0;
}

void rig_freeifaddrs(struct ifaddrs* ifa) { (void)ifa; }

/* One interface named en0 at index 1, then the terminator. */
static struct if_nameindex gNameIdx[2];
struct if_nameindex* rig_if_nameindex(void)
{
	gNameIdx[0].if_index = 1;
	gNameIdx[0].if_name  = gIfaName;
	gNameIdx[1].if_index = 0;
	gNameIdx[1].if_name  = 0;
	return gNameIdx;
}
void rig_if_freenameindex(struct if_nameindex* p) { (void)p; }

char* rig_inet_ntoa(struct in_addr in)
{
	static char buf[32];
	unsigned int h = ntohl(in.s_addr);
	sprintf(buf, "%u.%u.%u.%u", (h >> 24) & 0xff, (h >> 16) & 0xff, (h >> 8) & 0xff, h & 0xff);
	return buf;
}

char* rig_if_indextoname(unsigned int idx, char* name)
{
	(void)idx;
	strcpy(name, "en0");
	return name;
}

/* --- the mDNS surface: the rig calls the peers' resolve callbacks itself --- */
DNSServiceErrorType DNSServiceRegister(DNSServiceRef* r, DNSServiceFlags f, uint32_t i,
	const char* a, const char* b, const char* c, const char* d, uint16_t p, uint16_t tl, const void* t,
	DNSServiceRegisterReply cb, void* ctx)
{
	(void)f;(void)i;(void)a;(void)b;(void)c;(void)d;(void)p;(void)tl;(void)t;(void)cb;(void)ctx;
	*r = (DNSServiceRef)1;
	return kDNSServiceErr_NoError;
}

DNSServiceErrorType DNSServiceBrowse(DNSServiceRef* r, DNSServiceFlags f, uint32_t i,
	const char* t, const char* dom, DNSServiceBrowseReply cb, void* ctx)
{
	(void)f;(void)i;(void)t;(void)dom;(void)cb;(void)ctx;
	*r = (DNSServiceRef)2;
	return kDNSServiceErr_NoError;
}

DNSServiceErrorType DNSServiceResolve(DNSServiceRef* r, DNSServiceFlags f, uint32_t i,
	const char* n, const char* t, const char* d, DNSServiceResolveReply cb, void* ctx)
{
	(void)f;(void)i;(void)n;(void)t;(void)d;(void)cb;(void)ctx;
	*r = (DNSServiceRef)3;
	return kDNSServiceErr_NoError;
}

DNSServiceErrorType DNSServiceQueryRecord(DNSServiceRef* r, DNSServiceFlags f, uint32_t i,
	const char* n, uint16_t rt, uint16_t rc, DNSServiceQueryRecordReply cb, void* ctx)
{
	(void)f;(void)i;(void)n;(void)rt;(void)rc;(void)cb;(void)ctx;
	*r = (DNSServiceRef)4;
	return kDNSServiceErr_NoError;
}

DNSServiceErrorType DNSServiceProcessResult(DNSServiceRef r) { (void)r; return kDNSServiceErr_NoError; }
int  DNSServiceRefSockFD(DNSServiceRef r) { (void)r; return 5; }
void DNSServiceRefDeallocate(DNSServiceRef r) { (void)r; }
