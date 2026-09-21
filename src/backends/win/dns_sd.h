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
 *  dns_sd.h -- the slice of Apple's DNS-SD API that netchannel.c uses,
 *  on Windows (round 88). Same names, same signatures, same flags, so the
 *  LAN code compiles unchanged; behind it, dnssd_win.c speaks mDNS on the
 *  wire by itself (Bonjour is not on Windows). Only what the game calls:
 *  Register, Browse, Resolve, QueryRecord for an A record, the socket to
 *  select on, ProcessResult, Deallocate -- and if_indextoname.
 */
#ifndef SHMUP_DNS_SD_WIN
#define SHMUP_DNS_SD_WIN

#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>

typedef struct dnssd_ref_t* DNSServiceRef;
typedef uint32_t DNSServiceFlags;
typedef int32_t  DNSServiceErrorType;

enum { kDNSServiceErr_NoError = 0, kDNSServiceErr_Unknown = -65537, kDNSServiceErr_NoMemory = -65539 };
enum { kDNSServiceFlagsAdd = 0x2, kDNSServiceFlagsForceMulticast = 0x400 };
enum { kDNSServiceType_A = 1, kDNSServiceType_PTR = 12, kDNSServiceType_TXT = 16, kDNSServiceType_SRV = 33 };
enum { kDNSServiceClass_IN = 1 };

typedef void (*DNSServiceRegisterReply)(DNSServiceRef sdRef, DNSServiceFlags flags, DNSServiceErrorType errorCode,
                                        const char* name, const char* regtype, const char* domain, void* context);
typedef void (*DNSServiceBrowseReply)(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                                      DNSServiceErrorType errorCode, const char* serviceName, const char* regtype,
                                      const char* replyDomain, void* context);
typedef void (*DNSServiceResolveReply)(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                                       DNSServiceErrorType errorCode, const char* fullname, const char* hosttarget,
                                       uint16_t port, uint16_t txtLen, const unsigned char* txtRecord, void* context);
typedef void (*DNSServiceQueryRecordReply)(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                                           DNSServiceErrorType errorCode, const char* fullname, uint16_t rrtype,
                                           uint16_t rrclass, uint16_t rdlen, const void* rdata, uint32_t ttl, void* context);

DNSServiceErrorType DNSServiceRegister(DNSServiceRef* sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                                       const char* name, const char* regtype, const char* domain, const char* host,
                                       uint16_t portInNetworkByteOrder, uint16_t txtLen, const void* txtRecord,
                                       DNSServiceRegisterReply callBack, void* context);
DNSServiceErrorType DNSServiceBrowse(DNSServiceRef* sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                                     const char* regtype, const char* domain, DNSServiceBrowseReply callBack, void* context);
DNSServiceErrorType DNSServiceResolve(DNSServiceRef* sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                                      const char* name, const char* regtype, const char* domain,
                                      DNSServiceResolveReply callBack, void* context);
DNSServiceErrorType DNSServiceQueryRecord(DNSServiceRef* sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                                          const char* fullname, uint16_t rrtype, uint16_t rrclass,
                                          DNSServiceQueryRecordReply callBack, void* context);
int                 DNSServiceRefSockFD(DNSServiceRef sdRef);
DNSServiceErrorType DNSServiceProcessResult(DNSServiceRef sdRef);
void                DNSServiceRefDeallocate(DNSServiceRef sdRef);

#ifndef IF_NAMESIZE
#define IF_NAMESIZE 16
#endif
// Windows has an if_indextoname of its own (netioapi.h, another signature);
// the game's calls go to the shim's, which names every LAN interface "lan".
char* DNSSD_WIN_IfName(unsigned int ifindex, char* ifname);
#define if_indextoname(i, n) DNSSD_WIN_IfName((i), (n))

// The Windows extras netchannel.c's platform branch and win/main.c call.
int  DNSSD_WIN_LocalIPv4(struct in_addr* out);	// the LAN adapter's address, 1 if found
void DNSSD_WIN_Tick(void);						// once per frame: announcements and query retries

#endif
