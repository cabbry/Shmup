# net_rig — four peers, one process, the real netchannel

A test harness for the 3-4 player netcode (v2). It runs **four instances of
`engine/src/netchannel.c`, compiled verbatim**, in a single process on any
machine — no devices, no Simulator, no Game Center accounts.

Testing a party of four on real hardware needs four iPhones on one WiFi (LAN)
or four Game Center accounts (online). This runs the same code paths in about
a second.

## How it works

- `peer0.c` … `peer3.c` each `#include "peer.inc"`, which macro-renames every
  symbol `netchannel.c` defines — and every piece of engine state it touches —
  to `<name>_<id>`, then includes the real `netchannel.c`. Four instances,
  genuinely separate state, zero transcription: a fix in the engine is a fix
  here, and a regression in the engine fails here.
- `shim/` provides just enough BSD sockets, `ifaddrs` and `dns_sd` for the
  Apple branch of `netchannel.c` to compile off-iOS.
- `net_bus.c` is a fake UDP network in memory (LAN) plus a GKMatch mock
  (online). `LAN_SeatForAddr`, the per-seat broadcast and the roster election
  run for real against it.
- `net_rig.c` drives the scenarios and asserts what the four peers must AGREE
  on: the seat table, the party size, the shared life pool, the loadout table,
  and who is on screen. Disagreement is what a player would see as ghost ships.

## Build & run

```bash
cd tools/netrig
zig cc -UWIN32 -U__WIN32__ -I shim -I ../../engine/src -I . -std=gnu99 \
    net_rig.c net_bus.c peer0.c peer1.c peer2.c peer3.c -o net_rig
./net_rig            # -v to see every peer's log
```

Exit code 0 = every check passed. `-UWIN32` matters: without it
`netchannel.c` compiles its WIN32 stub branch and the rig tests nothing.

## Scenarios

1. **LAN, party of four, scrambled discovery** — seats follow sorted IPs, the
   settle window holds the start, pool = 12, everyone agrees on the colours.
2. **A seat dies mid-handshake** — the frame-counted watchdog drops it and the
   remaining three start (the sim clock is paused here, so ms can't measure it).
3. **Level transition** — the barrier re-arms, the pool carries over.
4. **A player quits mid-match** — his ship parks, the others play on; the last
   one left falls back to the connection-lost teardown.
5. **Online GKMatch party of four** — seats handed down by the GameKit layer.
6. **The classic pair** — the 2-player regression path (host = lower IP, pool 6,
   the 2010 colour-shift rule).
7. **Partial discovery** — one device never hears about another through
   Bonjour; the roster gossip in the lobby adverts has to repair it, or the
   party splits into two incompatible simulations.
8. **A party of three** — an odd size, with no fourth seat to lean on.
9. **Online mid-match drop** — the host decides, and its `activeMask` is what
   tells the others (four independent stopwatches would part the sims).

## Calibration

A harness that passes on broken code is worthless, so each guard has a mutant
that must fail. Point `-I mutant` at a `sed`-patched copy of `netchannel.c`:

| mutation | failures |
|---|---|
| handshake watchdog defanged | 9 |
| clients ignore the host's activeMask | 2 |
| no lobby advert (silent host reads as dead) | 15 |
| no roster gossip | 8 |
| settle window removed | 26 |
| in-match per-seat liveness disabled | 4 |

## What it does not cover

Rendering, input, gameplay simulation, and the real Bonjour/GameKit stacks.
`smoke-ttb.yml` (Simulator, Release) covers the engine side; this covers
agreement between peers.
