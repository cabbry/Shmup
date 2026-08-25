/* Minimal mock of Apple's dns_sd.h -- syntax-check harness only.
   Declares exactly what netchannel.c uses. */
#ifndef _DNS_SD_MOCK_H
#define _DNS_SD_MOCK_H

#include <stdint.h>
#include <sys/types.h>

typedef struct _DNSServiceRef_t* DNSServiceRef;
typedef uint32_t DNSServiceFlags;
typedef int32_t  DNSServiceErrorType;

enum {
    kDNSServiceErr_NoError = 0
};

enum {
    kDNSServiceFlagsMoreComing     = 0x1,
    kDNSServiceFlagsAdd            = 0x2,
    kDNSServiceFlagsForceMulticast = 0x400
};

enum {
    kDNSServiceClass_IN = 1
};

enum {
    kDNSServiceType_A = 1
};

enum {
    kDNSServiceInterfaceIndexAny = 0
};

typedef void (*DNSServiceRegisterReply)(DNSServiceRef, DNSServiceFlags, DNSServiceErrorType,
    const char*, const char*, const char*, void*);
typedef void (*DNSServiceBrowseReply)(DNSServiceRef, DNSServiceFlags, uint32_t, DNSServiceErrorType,
    const char*, const char*, const char*, void*);
typedef void (*DNSServiceResolveReply)(DNSServiceRef, DNSServiceFlags, uint32_t, DNSServiceErrorType,
    const char*, const char*, uint16_t, uint16_t, const unsigned char*, void*);
typedef void (*DNSServiceQueryRecordReply)(DNSServiceRef, DNSServiceFlags, uint32_t, DNSServiceErrorType,
    const char*, uint16_t, uint16_t, uint16_t, const void*, uint32_t, void*);

DNSServiceErrorType DNSServiceRegister(DNSServiceRef*, DNSServiceFlags, uint32_t,
    const char*, const char*, const char*, const char*, uint16_t, uint16_t, const void*,
    DNSServiceRegisterReply, void*);
DNSServiceErrorType DNSServiceBrowse(DNSServiceRef*, DNSServiceFlags, uint32_t,
    const char*, const char*, DNSServiceBrowseReply, void*);
DNSServiceErrorType DNSServiceResolve(DNSServiceRef*, DNSServiceFlags, uint32_t,
    const char*, const char*, const char*, DNSServiceResolveReply, void*);
DNSServiceErrorType DNSServiceQueryRecord(DNSServiceRef*, DNSServiceFlags, uint32_t,
    const char*, uint16_t, uint16_t, DNSServiceQueryRecordReply, void*);
DNSServiceErrorType DNSServiceProcessResult(DNSServiceRef);
int  DNSServiceRefSockFD(DNSServiceRef);
void DNSServiceRefDeallocate(DNSServiceRef);

#endif
