// dnssd_probe.c
//
// Standalone reproduction of SHMUP's Bonjour/DNS-SD matchmaking
// (engine/src/netchannel.c : NET_CheckServerAvailability), so it can be run on
// the macOS GitHub Actions runner -- which has mDNSResponder + <dns_sd.h> -- to
// empirically test the registration mechanism WITHOUT an iOS device.
//
// Questions it answers:
//   - does if_nametoindex("en0") return a valid index, or 0 (== all interfaces)?
//   - does the register callback fire via select()+DNSServiceProcessResult on
//     interface 0 vs a specific en0 index?
//   - does a second registration of the same name conflict (-> CLIENT)?
//
// Caveat: macOS, not iOS -- there is no iOS "Local Network" permission gate here.
//
//   clang -O0 -g tools/dnssd_probe.c -o /tmp/probe && /tmp/probe

#include <dns_sd.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>

#define PORT 31978
static const char *kType = "_DodgeServer._udp.";

typedef struct { int fired; DNSServiceErrorType err; } result_t;

static void regCB(DNSServiceRef r, DNSServiceFlags f, DNSServiceErrorType err,
                  const char *name, const char *regtype, const char *domain, void *ctx)
{
    result_t *res = (result_t *)ctx;
    res->fired = 1;
    res->err = err;
    printf("    [regCB] err=%d (%s) name=%s\n", err,
           err == kDNSServiceErr_NoError    ? "NoError -> SERVER" :
           err == kDNSServiceErr_NameConflict ? "NameConflict -> CLIENT" : "OTHER",
           name ? name : "(null)");
}

// Mirrors NET_CheckServerAvailability: register, then select()+ProcessResult.
// We loop select in 1-second slices so the timing of the reply is visible.
static DNSServiceRef probeRegister(const char *label, uint32_t ifIdx, int waitSec)
{
    printf("[%s] DNSServiceRegister(ifIdx=%u)\n", label, ifIdx);
    result_t res = { 0, 0 };
    DNSServiceRef ref = NULL;
    DNSServiceErrorType err = DNSServiceRegister(&ref, kDNSServiceFlagsNoAutoRename,
        ifIdx, "Dodge shmup server", kType, NULL, NULL, htons(PORT), 0, NULL, regCB, &res);
    printf("    register() returned err=%d\n", err);
    if (err != kDNSServiceErr_NoError) { printf("    => REGISTER CALL FAILED\n\n"); return NULL; }

    int fd = DNSServiceRefSockFD(ref);
    printf("    sockfd=%d\n", fd);

    for (int i = 0; i < waitSec && !res.fired; i++) {
        fd_set set; FD_ZERO(&set); FD_SET(fd, &set);
        struct timeval tv; tv.tv_sec = 1; tv.tv_usec = 0;
        errno = 0;
        int s = select(fd + 1, &set, NULL, NULL, &tv);
        printf("    [t+%ds] select=%d errno=%d\n", i + 1, s, errno);
        if (s > 0) DNSServiceProcessResult(ref);
    }

    printf("    => callbackFired=%d result=%s\n\n", res.fired,
           !res.fired ? "NONE (timeout, role never decided)" :
           res.err == kDNSServiceErr_NoError ? "SERVER" : "CLIENT");
    return ref; // keep alive for the conflict test
}

int main(void)
{
    printf("=== SHMUP DNS-SD probe (macOS CI) ===\n");
    printf("if_nametoindex: en0=%u  en1=%u  lo0=%u\n\n",
           if_nametoindex("en0"), if_nametoindex("en1"), if_nametoindex("lo0"));

    printf("--- TEST 1: register on interface 0 (ALL interfaces) ---\n");
    DNSServiceRef a = probeRegister("iface0", 0, 8);

    printf("--- TEST 2: register on en0 specifically ---\n");
    if (a) { DNSServiceRefDeallocate(a); a = NULL; sleep(1); } // free the name first
    DNSServiceRef b = probeRegister("en0", if_nametoindex("en0"), 8);

    printf("--- TEST 3: conflict (register the SAME name again, B still alive) ---\n");
    DNSServiceRef c = probeRegister("conflict", if_nametoindex("en0"), 8);

    if (b) DNSServiceRefDeallocate(b);
    if (c) DNSServiceRefDeallocate(c);
    printf("=== done ===\n");
    return 0;
}
