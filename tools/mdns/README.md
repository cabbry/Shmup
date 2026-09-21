# tools/mdns — the bench for the Windows mDNS shim

`src/backends/win/dnssd_win.c` is what lets the PC play on the LAN with an
iPhone: a small mDNS responder and browser behind Apple's `dns_sd.h` names,
so `netchannel.c` compiles unchanged and the iPhone sees a service like its
own (`_DodgeServer._udp`, port 31978). Bonjour is not on Windows; this is
the part of it the game needs, about five hundred lines.

`mdns_check.c` drives the shim exactly as the game does -- register, browse,
and on every browse hit resolve then query the A record with the same
bounded selects -- and prints what it finds.

## Build and run (Windows, zig cc)

    zig cc -std=gnu99 -O1 -DWIN32 -iquote src/core -iquote src/backends/win \
      tools/mdns/mdns_check.c src/backends/win/dnssd_win.c src/core/log.c \
      src/backends/posix/filesystem.c -liphlpapi -lws2_32 -o tools/mdns/build/mdns_check.exe

Two on one PC must find each other (they need two host names, which the
real world gives for free):

    SHMUP_MDNS_NAME="Dodge shmup server A" SHMUP_MDNS_HOST=pc-a tools/mdns/build/mdns_check.exe 12 &
    SHMUP_MDNS_NAME="Dodge shmup server B" SHMUP_MDNS_HOST=pc-b tools/mdns/build/mdns_check.exe 10

Each prints `browse ADD`, `resolved ... :31978`, `PEER <host>.local. -> <ip>`
and exits 0. One of them next to an iPhone on the Wi-Fi waiting in
*Jeu Multi > Local* prints the iPhone's address the same way.

The responder also answers the classic resolvers (a query from a port other
than 5353 is answered unicast, RFC 6762 6.7), so while a bench or the game
runs, Windows' own tools can read its records:

    nslookup -type=PTR -port=5353 _DodgeServer._udp.local 224.0.0.251

## What the shim does and does not do

- Announces our PTR / SRV / TXT / A on registration (every second for five
  seconds, then every three) and answers queries for them; sends a goodbye
  (TTL 0) on deallocation.
- Browses by sending PTR queries (every second until someone answers, then
  every four), resolves with an SRV query, gets the address with an A query,
  retrying every 400 ms from the per-frame tick (`DNSSD_WIN_Tick`, called by
  `win/main.c`).
- Reads the socket only inside `DNSServiceProcessResult`, so the game's
  `select` on `DNSServiceRefSockFD` stays truthful.
- No probing, no conflict resolution, no name compression on output, IPv4
  only: the instance name carries the PC's name so two registrations never
  collide, and everything else the game never asks for.
