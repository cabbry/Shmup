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
 *  mdns_check.c -- the bench for the Windows mDNS shim (round 88).
 *
 *  Drives dnssd_win.c exactly as netchannel.c does: register with the
 *  game's service type and port, browse, and on every browse hit resolve
 *  then query the A record with the same bounded selects. Two of these on
 *  one PC (different SHMUP_MDNS_NAME) must find each other; one of them next
 *  to the game, or to an iPhone on the Wi-Fi, must print its address.
 *
 *    mdns_check.exe [seconds]      exit 0 once a peer's address was seen
 */
#include "dns_sd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"

static int gFound = 0;

static int WaitAndProcess(DNSServiceRef ref, int timeoutMs)
{
	fd_set set; struct timeval tv;
	int fd = DNSServiceRefSockFD(ref);
	if (fd < 0) return 0;
	FD_ZERO(&set); FD_SET((SOCKET)fd, &set);
	tv.tv_sec = timeoutMs / 1000; tv.tv_usec = (timeoutMs % 1000) * 1000;
	if (select(fd + 1, &set, NULL, NULL, &tv) > 0)
		return DNSServiceProcessResult(ref) == kDNSServiceErr_NoError;
	return 0;
}

static void QueryCb(DNSServiceRef r, DNSServiceFlags f, uint32_t i, DNSServiceErrorType e, const char* full,
                    uint16_t t, uint16_t c, uint16_t rdlen, const void* rdata, uint32_t ttl, void* ctx)
{
	const unsigned char* ip = rdata;
	(void)r; (void)f; (void)i; (void)e; (void)t; (void)c; (void)ttl; (void)ctx;
	if (rdlen == 4)
	{
		printf("PEER %s -> %u.%u.%u.%u\n", full, ip[0], ip[1], ip[2], ip[3]);
		gFound = 1;
	}
}

static void ResolveCb(DNSServiceRef r, DNSServiceFlags f, uint32_t i, DNSServiceErrorType e, const char* full,
                      const char* host, uint16_t port, uint16_t txtLen, const unsigned char* txt, void* ctx)
{
	DNSServiceRef q;
	(void)r; (void)f; (void)e; (void)txtLen; (void)txt; (void)ctx;
	printf("resolved %s -> %s:%u\n", full, host, ntohs(port));
	if (DNSServiceQueryRecord(&q, kDNSServiceFlagsForceMulticast, i, host, kDNSServiceType_A, kDNSServiceClass_IN, QueryCb, NULL) == kDNSServiceErr_NoError)
	{
		int k;
		for (k = 0; k < 5 && !gFound; k++) { WaitAndProcess(q, 400); DNSSD_WIN_Tick(); }
		DNSServiceRefDeallocate(q);
	}
}

static void BrowseCb(DNSServiceRef r, DNSServiceFlags f, uint32_t i, DNSServiceErrorType e, const char* name,
                     const char* type, const char* domain, void* ctx)
{
	DNSServiceRef res;
	char ifname[IF_NAMESIZE];
	(void)r; (void)e; (void)ctx;
	if_indextoname(i, ifname);
	printf("browse %s: '%s' %s%s on %s\n", (f & kDNSServiceFlagsAdd) ? "ADD" : "REMOVE", name, type, domain, ifname);
	if (!(f & kDNSServiceFlagsAdd) || strcmp(ifname, "lan") != 0) return;
	if (DNSServiceResolve(&res, kDNSServiceFlagsForceMulticast, i, name, type, domain, ResolveCb, NULL) == kDNSServiceErr_NoError)
	{
		int k;
		for (k = 0; k < 5; k++) { if (WaitAndProcess(res, 400)) break; DNSSD_WIN_Tick(); }
		DNSServiceRefDeallocate(res);
	}
}

int main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 20;
	DNSServiceRef reg = NULL, browse = NULL;
	const char* name = getenv("SHMUP_MDNS_NAME") ? getenv("SHMUP_MDNS_NAME") : "Dodge shmup server";
	DWORD t0 = GetTickCount();
	struct in_addr ip;
	setvbuf(stdout, NULL, _IONBF, 0);
	if (!DNSSD_WIN_LocalIPv4(&ip)) { printf("no LAN address\n"); return 2; }
	printf("local address %s\n", inet_ntoa(ip));
	if (DNSServiceRegister(&reg, 0, 0, name, "_DodgeServer._udp.", NULL, NULL, htons(31978), 0, NULL, NULL, NULL) != kDNSServiceErr_NoError)
	{ printf("register failed\n"); return 2; }
	if (DNSServiceBrowse(&browse, 0, 0, "_DodgeServer._udp.", NULL, BrowseCb, NULL) != kDNSServiceErr_NoError)
	{ printf("browse failed\n"); return 2; }
	while ((int)(GetTickCount() - t0) < seconds * 1000)
	{
		WaitAndProcess(browse, 16);		// the game's per-frame drain
		DNSSD_WIN_Tick();
		if (gFound && argc > 2) break;	// a third argument: stop at the first address
	}
	DNSServiceRefDeallocate(browse);
	DNSServiceRefDeallocate(reg);
	printf(gFound ? "OK: a peer was found and resolved\n" : "no peer seen\n");
	return gFound ? 0 : 1;
}
