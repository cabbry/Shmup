// dnssd_probe.c
//
// Standalone reproduction of SHMUP's Bonjour/DNS-SD matchmaking, runnable on the
// macOS GitHub Actions runner (mDNSResponder + <dns_sd.h>) to test the mechanism
// WITHOUT an iOS device.
//
// v2 tests the AUTO-RENAME election approach: register two services with
// auto-rename (simulating two devices) and confirm they BOTH stay advertised with
// distinct names AND that a browse discovers both. (The cross-device IP tiebreak
// itself can't be tested here -- a single host has one IP -- so that part is
// validated on the two real devices via the on-screen DIAG.)
//
// Caveat: macOS, not iOS -- no iOS "Local Network" permission gate here.
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

static int gBrowseCount = 0;

static void regCB(DNSServiceRef r, DNSServiceFlags f, DNSServiceErrorType err,
                  const char *name, const char *regtype, const char *domain, void *ctx)
{
    printf("    [regCB] err=%d advertised name=\"%s\"\n", err, name ? name : "(null)");
}

// Drain a ref for a short window so its callbacks fire.
static void drain(DNSServiceRef ref, int ms)
{
    int fd = DNSServiceRefSockFD(ref);
    if (fd < 0) { printf("    (bad sockfd=%d)\n", fd); return; }
    fd_set set; FD_ZERO(&set); FD_SET(fd, &set);
    struct timeval tv; tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
    if (select(fd + 1, &set, NULL, NULL, &tv) > 0) DNSServiceProcessResult(ref);
}

static void browseCB(DNSServiceRef r, DNSServiceFlags f, uint32_t ifIdx,
                     DNSServiceErrorType err, const char *name, const char *regtype,
                     const char *domain, void *ctx)
{
    if (f & kDNSServiceFlagsAdd) {
        char ifn[IF_NAMESIZE]; if_indextoname(ifIdx, ifn);
        gBrowseCount++;
        printf("    [browseCB] ADD #%d if=%s(%u) name=\"%s\"\n", gBrowseCount, ifn, ifIdx, name);
    }
}

int main(void)
{
    printf("=== SHMUP DNS-SD probe v2: auto-rename election (macOS CI) ===\n");
    printf("if_nametoindex: en0=%u  en1=%u  lo0=%u\n\n",
           if_nametoindex("en0"), if_nametoindex("en1"), if_nametoindex("lo0"));

    uint32_t ifIdx = if_nametoindex("en0");

    printf("--- Register TWO services with AUTO-RENAME (both should advertise) ---\n");
    DNSServiceRef a = NULL, b = NULL;
    DNSServiceErrorType e1 = DNSServiceRegister(&a, 0 /*auto-rename*/, ifIdx,
        "Dodge shmup server", kType, NULL, NULL, htons(PORT), 0, NULL, regCB, NULL);
    printf("  register A err=%d\n", e1);
    if (a) drain(a, 1500);
    DNSServiceErrorType e2 = DNSServiceRegister(&b, 0 /*auto-rename*/, ifIdx,
        "Dodge shmup server", kType, NULL, NULL, htons(PORT), 0, NULL, regCB, NULL);
    printf("  register B err=%d\n", e2);
    if (b) drain(b, 1500);

    printf("\n--- Browse: should discover BOTH advertised instances ---\n");
    DNSServiceRef br = NULL;
    DNSServiceErrorType e3 = DNSServiceBrowse(&br, 0, 0, kType, NULL, browseCB, NULL);
    printf("  browse err=%d\n", e3);
    if (br) for (int i = 0; i < 6; i++) drain(br, 800);   // ~4.8s of discovery

    printf("\n  => total ADD callbacks seen: %d (expect >= 2 if auto-rename coexistence works)\n", gBrowseCount);

    if (br) DNSServiceRefDeallocate(br);
    if (a)  DNSServiceRefDeallocate(a);
    if (b)  DNSServiceRefDeallocate(b);
    printf("=== done ===\n");
    return 0;
}
