# SHMUP Reborn

A modernization of **SHMUP** — Fabien Sanglard's 2009 3D shoot'em up — so it builds,
ships and runs on current iPhones, on top of the original GPLv3 source (`dEngine`).

This file is the running log of that effort: what changed, *why*, and what's next.
It's meant to be shown to Fabien and kept up to date as the project evolves.

> The original engine holds up remarkably well for 2009: pure ANSI C, a hand-rolled
> renderer with both a fixed-function (GL ES 1.1) and a shader (GL ES 2.0) path, MD5
> meshes/anims, a precomputed visibility set, delta-compressed on-rails camera paths,
> and a tidy normalized screen-space gameplay model. Almost everything below is about
> *adapting* that design to 2026 toolchains and hardware — not rewriting it.

---

## Why

- The iOS target had bit-rotted (recent upstream work was Android-only) and no longer
  built from the repository as-is.
- Goal: get it compiling on a current Xcode, running **full-screen on a modern iPhone**,
  delivered via **TestFlight**, then iterate (new level, polish, …).

## Constraints (and how they shaped the approach)

- Development happens on **Windows**; the only Mac available is too old for Xcode 26.
  → **Every build runs on GitHub Actions** (macOS runner, Xcode 26). Using a *public*
  fork keeps Actions free.
- No local Apple toolchain → signing assets and App Store Connect state are inspected
  and validated through the **App Store Connect API** (a tiny Python + JWT helper)
  *before* spending CI time, so almost no build is wasted on a misconfiguration.

## Build & ship — quick reference

- CI repo: public fork **`cabbry/Shmup`**.
- **Compile check** — `.github/workflows/ios.yml`: builds the `Shmup` target for the iOS
  Simulator (arm64, unsigned) on every push.
- **TestFlight** — `.github/workflows/testflight.yml` (manual run or a `v*` tag): manual
  code signing (cert + provisioning profile imported into a temporary keychain), archive,
  export, then `xcrun altool --upload-app`. Ship a build with:
  `git tag v0.2.x && git push <fork> v0.2.x`.
- App: bundle id `com.cabbry.shmup`, store name **"SHMUP Reborn"**.
- Versioning: TestFlight shows the **marketing version** = the **git tag** without its `v`
  (tag `v1.1.0` → shown as **1.1.0**), plus an independent auto **build number**
  (`100 + CI run number`). **Lesson learned:** the version must only ever go **up** — an
  early dip to `0.2` (below the legacy `1.0` builds) made TestFlight keep ranking the old
  1.0 line as newest, and once those builds were expired it had nothing installable there
  → testers got stuck. Fixed by moving onto the **1.1.x** line (above 1.0). Tag a higher
  version each release (`v1.1.1`, `v1.1.2`, …); the build number climbs separately.

---

## Work log

### 1 — Compiling under Xcode 26

The project compiles cleanly once a few era-specific things are addressed:

- **Removed/renamed SDK APIs.** Dropped `<AudioToolBox/AudioSession.h>` (the C
  AudioSession API was removed); removed Game Center (the deprecated
  `GKLeaderboardViewController`/`GKScore` path, which was already disabled at runtime),
  the old `UIAlertView` cross-promo, and the replay-telemetry HTTP upload. The four
  engine-facing `native_services` entry points are kept as no-op stubs, exactly like
  every other platform backend (win32/linux/macOS/Android).
- **Stale Xcode project paths.** The `.pbxproj` assumed a local layout
  (`engine/iOS/src`, `engine/iOS/srciPhone`) that isn't in the repo — on the original
  machine these were almost certainly symlinks. Fixed the group paths (`src` →
  `../src`, made `srciPhone` a logical group) plus a handful of stray file references
  (`sound_openAL.c` in `engine/openal`, `filesystem.c` in `engine/src/filesystem`, the
  `data` folder, `Settings.bundle`, `Entitlements.plist`, `MainWindow.xib`, and the
  "Touch data" run-script phases).
- **Modern clang strictness.** clang 16 promotes the classic pre-C99 patterns
  (implicit function declarations, implicit int, int/pointer conversions) to hard
  errors. Added an `#include "native_services.h"` where it was missing and softened
  those specific diagnostics back to warnings in CI; tightening them up properly is on
  the roadmap.

### 2 — TestFlight delivery

Mirrors a known-good manual-signing pipeline: import an Apple Distribution certificate
and an App Store provisioning profile into a throwaway CI keychain, archive in Release
for a generic iOS device, export an `.ipa`, and upload with the App Store Connect API
key. The provisioning profile for `com.cabbry.shmup` was created through the ASC API.
A small `UILaunchScreen` key and `ITSAppUsesNonExemptEncryption=false` were added to the
Info.plist (native full-screen drawable, no export-compliance prompt), and the app icon
was rebuilt as a single 1024² asset cropped from the title-screen art.

Result: builds upload and validate; the game runs at full speed on device, with sound.

### 3 — True full-screen on modern iPhones

The engine renders into a fixed **320×480 (2:3) "active surface"**, letterboxed and
centered — perfect for a 2009 iPhone, black bars on a tall 2026 screen. The fix keeps
the original design intact and just makes it aspect-aware:

- The viewport now covers the whole drawable, and a single factor `renderer.vScale`
  captures how much taller the real screen is than the legacy 2:3 surface (1.0 on a 2:3
  device, ≈1.45 on a tall iPhone).
- The 3D camera uses the real screen aspect, with the **vertical FOV extended by
  `vScale`** so the screen fills while the original *horizontal* field of view is
  preserved — nothing is cropped on the sides.
- The 2D HUD/sprite ortho is extended by the same `vScale`, so the 2D and 3D layers
  scale identically and sprites stay undistorted.
- **No enemy "pop-in":** gameplay positions are normalized to the camera frustum
  (`heightAtDistance`/`widthAtDistance` in `player.c`/`enemy.c`), so an enemy that
  starts at `y = 1.2` is *always* 20% beyond the visible edge regardless of FOV — they
  keep entering from off-screen for free. (This is a nice property of the original
  design.)

Still to tune on-device: re-anchoring the score (top) and on-screen controls (bottom)
to the true screen edges, and the touch-coordinate mapping.

---

## Status

- ✅ Compiles on Xcode 26 (simulator build + signed device archive).
- ✅ Live on **TestFlight** — runs full-speed on device, sound and gameplay intact.
- ✅ Full-screen: fills tall iPhones with no black edge gaps, HUD anchored to the
  safe area, 2D sprites de-stretched (round sprites are round again).

## New features (added beyond the original 2009 game)

- 🆕 **Pause / resume** when the app is backgrounded, with a **3-2-1-SHMUP** countdown
  on return (the original just lost the game).
- 🆕 **Final score on the GAME OVER screen** (the original only showed it on the win
  / act-completed screen).
- 🆕 **Game Center sign-in + online "High Scores" leaderboard** — the final score is
  submitted online.
- 🆕 **Online multiplayer over Game Center (GKMatch)** — play a 2-player match over the
  internet (not just the LAN); Apple handles matchmaking and NAT traversal. Confirmed
  working on devices via Game Center quick-match.
- 🆕 **Ship + bullet-colour customisation ("Custom" menu)** — the engine could already do
  this (multiplayer always gave the two players distinct ships and bullet colours), but
  there was no menu to reach it: the ability existed, unexposed, since 2009. An
  **Others → Custom** screen now lets each player pick a ship (2 models) and a bullet
  colour (Red / Blue / Invisible / Yellow — straight from the original bullet atlas'
  columns), persisted across restarts. In multiplayer, each player's choice is synced
  during the handshake, and if both picked the same colour, player two's is shifted
  deterministically on both ends so the two players' shots stay distinguishable.

## Known issues

- Minor: backgrounding the app on the GAME OVER screen still shows the
  3-2-1-SHMUP overlay (cosmetic).
- Minor/latent: the menu titles' safe-area offset is computed once at init,
  before the inset is known, so it stays inactive — the titles clear the notch
  via fixed margins today but wouldn't auto-adapt to a larger inset.

## Roadmap

### Done — the 2009 wishlist, delivered

- **🎯 Boss fight — ✅ DONE** (Fabien's #1 wish — the thing he ran out of money for
  in 2009). The long-dormant LOFB is now a real climax, its own **Act IV**: an HP
  pool with a quad-drawn health bar, an attack ladder that unlocks with damage
  (aimed fans → rotating spray → escort waves → big energy shots → red homing
  seekers → frenzy), **destructible arms** that silence their side, shoot-downable
  homing missiles, and a **mega-laser** with a readable charge-up telegraph. The
  `boss.png` card announces it at last, and finishing it ends the game for real
  (MISSION COMPLETE card, rank D→S). Player-tested over ~18 rounds of feedback.
- **A new level — ✅ DONE: Act III, "夕 -Dusk"**, inserted before the boss act.
  Its own title card, a dusk sky with stars and crossing meteors, three phases of
  mixed-type waves the original acts never ran, the resurrection of **"le Devil"**
  (`ENEMY_HAB` — modeled and coded by Fabien in 2009, never once spawned by any
  shipped scene) in three costumes with three weapons, and a storm-lit cameo of
  the Act IV boss crossing the sky. Declared **frozen** by the tester after
  round 30: *"Pour moi l'act 3 est ok. On n'y touche plus."*
- **TTB system — ✅ DONE, and shipped as content** (homage to the manga *Tokyo Toy
  Box*): mid-level, the camera swings **90° from top-down to a true side view**
  for thirty seconds — the vertical shooter becomes a side-scroller, enemies,
  bullets and the ship itself all re-reading correctly — then swings back. The
  full journey (a naive roll, the corridor orbit, the ship's profile blend, the
  side-view sky, per-view wave authoring) is rounds 19-31 of the changelog.
- **Online multiplayer (GKMatch) — ✅ DONE** and listed under features; LAN co-op
  additionally gained the **second-chance rule** (round 31).

### Open

- **Enemy / boss scripting** (Fabien's suggestion): the `.scene` event format is
  declarative (spawn timelines); reactive behaviour still lives in C (the boss
  ladder is hardcoded in `lofb.c`, the Devil's weapons in `enemy.c`). If a
  second boss or community levels ever happen, evaluate the lightest thing that
  works — conditional triggers in the event system vs. a small VM/Lua.
- **Gameplay videos on YouTube** (Fabien's suggestion): record short progress
  videos (solo run, the Act III TTB beat, 2-player LAN, online match) so people
  can see the project evolve. With all four acts now playable end to end, this
  is mostly a recording session away.
- **App Store release?** The game is feature-complete: four acts, a boss, an
  ending, online multiplayer, leaderboards. Invitations are now handled in code
  too (v2 added the accept-an-invite listener — the matchmaker could always
  *send* one, but nothing was listening, so tapping Play did nothing), though
  iMessage invites and SharePlay only light up once the app is on the App Store.
- **🚀 v2 — 3-4 player multiplayer** (the next major version) — **on TestFlight
  (v2.0.8), 2-player LAN AND online device-confirmed, act transitions
  included** (rounds 32-34): the transport speaks SEATS (0..N-1, seat 0 hosts)
  on both GameKit and the LAN, the
  handshake is a counting barrier, per-seat sequence/liveness state replaces
  every "the peer" scalar, a mid-match drop parks that ship and the match
  continues, `MAX_NUM_PLAYERS` is 4 with a staggered 2-row formation, a shared
  pool of N×3 lives, ONE team score, 4 named ships (Falcon, Viper, the
  resurrected Phoenix, the translucent Ghost), a LAN roster that seats a party
  of four, a party-size picker, and a Custom screen
  that previews the picked ship on the menu stage, remote-ship de-jitter with
  the ABS resync in wire order, and a half-RTT clock alignment at the online
  GO, host migration (the lowest active seat leads, on every peer at once),
  and host-ruled deaths (one order, one pool, one survivor on every screen).
  Needs: a 4-device session -- everything above is rig-proven at four,
  device-proven at two.
- **🚀 v3 — the graphics overhaul** — **in progress on the `v3` branch (round
  35)**: stage 1 ✅ every warning fixed or explicitly retired per file, the
  project builds with `-Werror`; stage 2 ✅ ARC, a modern launch screen, the
  64-bit truncations made explicit; stage 3 ✅ the **Metal backend** is the
  default renderer — a third implementation of the renderer's 24-function
  table, a 1:1 port of the fixed-pipeline passes with the pipeline emulated in
  shaders, green on every smoke and on the first screenshots ever taken of
  the game; OpenGL ES stays one switch away until the device round confirms. Then the OpenGL ES 1.1 (and the dormant ES 2.0) renderers
  retire, and with them the last 511 deprecation sites.

---

## Changelog

### 2026-09-05 — round 35 (v3 opens: the warnings, then ARC — with `-Werror` on)

The tester's "fait toute la v3 en autonomie… Fire !" — the graphics overhaul,
staged by risk on a `v3` branch cut from `v2`'s head (the four-device test
still owes `v2` a session; fixes cherry-pick).

- **Stage 1 — the warnings, then the strict flags.** A new audit workflow
  keeps the full `xcodebuild` log and prints a histogram: **545 distinct
  sites, 511 of them deprecations** — every OpenGL ES / EAGL / OpenAL /
  CoreAudio call the platform has deprecated, i.e. the APIs the later stages
  retire, not warnings to "fix". The other 34 were real. Two were bugs the
  game has carried since 2009: the visibility bake's perspective divide wrote
  a *third* component into two-component screen vertices — past each entry,
  and past the whole array on the last vertex, at every bake; and `matrix.h`
  promised `vec3_t` for a function whose definition and every caller use
  `vec4_t` — the prototype lied about the fourth float the code reads and
  writes. The rest was hygiene made explicit (64→32-bit casts where the value
  is bounded, a `time_t` epoch base where it was not, format strings, a `bool`
  function that never returned, a log line sitting after its own `return`, a
  GCC-only `-fsingle-precision-constant` clang had never honoured). The
  deprecations are silenced *per file*, each pragma naming the stage that
  retires its API, so a new deprecation anywhere else still surfaces. Then
  `GCC_TREAT_WARNINGS_AS_ERRORS = YES` in the project, and the `-Wno-error=…`
  downgrades the Xcode 26 port had carried leave every build workflow.
  **Zero warnings, `-Werror`, green.**
- **Stage 2 — ARC.** Twenty-five manual retain/release sites, two deallocs
  and one autorelease pool across three files; one bridged cast
  (`NSString*` → `CFStringRef` in the audio queue) was all ARC asked to see.
  The deprecated launch image became an ordinary image set shown by the
  modern `UILaunchScreen` entry; `setStatusBarHidden:` gave way to a
  one-method root view controller; the dead pre-iOS-14 Game Center branch
  went; the touch-data script phases admit they run every build. Both smokes
  (act 3 twice, four ships through act 1) green on the result.
- **Stage 3 — Metal — begun.** Two facts decided the shape: all OpenGL lives
  in two files (the ES 1.1 renderer that ships, and a 2010 ES 2.0 renderer
  that has been dormant behind a setting for fifteen years) with *zero* GL
  calls elsewhere, and the engine talks to the renderer through a table of
  24 functions bound at start-up. So the Metal backend is a third
  implementation of that table (`renderer_metal.m`): a 1:1 port of the fixed
  renderer's passes — skybox domes, the boss cameo, the crossing stars, the
  live cull and its `[cull]` probe, ghost and flicker enemies — with the fixed
  pipeline emulated in one vertex/one fragment shader (GL's one-light
  default-material lighting, per-vertex linear fog, REPLACE/MODULATE/ADD,
  alpha and additive blending). It compiles clean under `-Werror`, sits
  behind `SHMUP_RENDERER=metal` (default still OpenGL), and gets its own
  smoke: act 3 on Metal held to the *same* luma assertions the OpenGL backend
  has passed for twenty builds — the parity contract — plus, for the first
  time, Simulator screenshots of the game, which the OpenGL layer never
  allowed.

- **Stage 3 — Metal is the default.** First real run: the parity contract
  held (26 side-view samples, no black sky, no missing city, the cameo's dip
  at 57 s where OpenGL measured it), and the first screenshots ever taken of
  this game — the prolog under the dusk dome, the boss silhouette among the
  crossing stars, the top-down chase, the *水 -Water Act IV* card mid-wipe,
  and the home screen with its orbiting hull — look like the game. The act
  runs in two minutes of wall-clock on Metal where the OpenGL emulation took
  ten. Default flipped ([7a7143f]); OpenGL ES stays one switch away
  (`SHMUP_RENDERER=gl`, or *RendererType 0* in the settings) for the device
  round; every smoke — act 3 twice with re-entry, four ships through act 1,
  menu plus act 3 with screenshots — is green on Metal. What remains: the
  tester's device verdict, then the retirement of both OpenGL renderers and
  EAGL — and with them the last 511 deprecation sites.


### 2026-08-29→30 — round 34 (online confirmed — and the two limits I had left open)

- **v2.0.8 (build 220): online plays, and the two screens agree.** The first
  working online match had reported "assez fluide mais légère désynchro"; two
  mechanisms, both fixed. The 300ms absolute-position correction had been
  jumping the de-jitter queue — applied before stale deltas it already
  contained, which then re-applied on top: overshoot, pull-back, never quite
  settling. It rides the queue now, in wire order. And a 2010 design flaw the
  LAN could never show: the host resets its sim clock when it *sends* the GO,
  a client when it *receives* it — a permanent half-RTT phase offset between
  two sims that derive every enemy from that clock. The GO answers the
  client's latest NOTIFY_LOADED one round trip later, so that gap is an RTT
  sample; the client now starts half of it ahead. Tester's verdict: *"ligne
  sur la 220 -> ok. fin d'acte en multijoueur -> ok"* — **two-player v2 is
  device-confirmed end to end**, LAN, online and act transitions.
- **Host migration.** The host was literally seat 0; lose it and the party
  played the level on leaderless, then hung at the next act's barrier. The
  host is now the *lowest active seat*, derived on every peer from the same
  activeMask — the survivors agree on the successor with nothing new on the
  wire, and the role (JOIN sender, barrier, death authority) follows it.
- **The host rules on deaths** — the review's Failure C, finally. Each device
  used to apply its own hull's death first and hear about the others later;
  two deaths within one latency at a pool of 2 left a *different* ship alive
  on each screen. A hit is now a request, the host applies the death and
  broadcasts one order carrying the pool it ruled on, and every device
  applies deaths in that order only. Collisions pause while a ruling is
  pending; an unanswered request is resent, then dropped, so a lost answer
  can never make a hull immortal.
- **Rig: 229 checks.** Two new scenarios stage exactly these — the host
  quitting mid-match (one successor, the next level's barrier clears under
  it), and both hulls hit in the same frame at a pool of 2 (same pool, same
  single survivor on both screens). A mutant that rules deaths locally, the
  v1 way, fails six assertions.

### 2026-08-25→28 — round 33 (v2 meets the devices: six builds of feedback)

v2.0.0 (build 212) went to TestFlight on the tester's go — and the first
device sessions did what no rig can: they played the game. Six builds in
three days, each one driven by a bug report in plain French.

- **"La caméra passe carrément dans le vaisseau du menu"** (212) — my own
  regression: `hpp.obj.md5mesh` is the model the title screen orbits, and
  Ship 3 pointed at that same file, so the ×2.8 rescale that gave the hull
  p1/p2's wingspan also blew up the intro. Same art, two scales, two files:
  the intro mesh went back byte-for-byte (blob hashes compared against the
  pre-212 commit), and Ship 3 got its own `hpp_ship.obj.md5mesh`. Lesson
  learned the hard way: before rescaling a "player" asset, grep `data/` for
  who else loads it.
- **"On n'arrive pas à faire une partie à 2"** (213) — the big one, LAN and
  online failing the same way. Reading the code found nothing; teaching the
  rig the case it had never played found everything: **mDNS that only flows
  one way**, which on a real network is the *normal* case. Two real faults:
  an unseated device was **deaf** (the handshake pump mistook "nobody has
  seated me yet" for "the watchdog tore this session down" — both read
  `NET_UNDETERMINED` — and returned before draining its socket, so the
  roster gossip built to repair exactly this could never run on the only
  device that needed it), and a gossip-seated device was **mute**
  (`serverAddResolved`, a v1 flag only the mDNS callback ever set, kept
  `isInitialized` false, so `NET_Send` returned early and the host dropped
  the silent peer after five seconds). Three new rig scenarios pin both
  directions plus a GameKit pair matched 300 frames apart: 199 checks.
- **"Le x passe de 2 à 0, il n'y a pas le 1"** (214, once the LAN pair
  worked) — a **2010 bug**: `P_Die` raises the invulnerability window, but
  `COLL_CheckPlayers` only reads it at the top of the function and then
  walks the bullet and missile loops with no exit after a kill — a burst
  whose bullets straddle the hull in one frame charged two or three lives.
  Fifteen years of three-lives-and-count-the-hearts read it as bad luck; a
  shared pool with a number on it made it arithmetic. One death per frame
  now, full stop.
- **Menus, en français** (215-217): the Custom grid aligned on one row
  pitch, Yellow above Invisible (the row carries its atlas column as its
  tag, so display order can never silently hand out unpicked bullets), the
  ships named — **Falcon, Viper, Phoenix, Ghost** — the home screen
  reworked (Game Solo / Game Multi → Local | Online), Tutorial moved into a
  reordered Others, the soundtrack no longer restarts between levels of the
  same act, French labels throughout, and the lives display is one icon +
  `xN` in every mode, snug against the screen edge, the icon staying put at
  `x0` instead of orphaning its counter.
- **"Ça saccade un peu"** (LAN, 2 players) — WiFi delivers in bursts: two
  movement commands one frame, none the next; applied raw that is a
  double-speed jump then a freeze. Remote movement now flows through a
  per-seat **de-jitter queue** drained at the sender's own emission rate
  (one per frame, two while a backlog exceeds three), corrections and
  deaths staying immediate — bounded latency, and the 300ms ABS resync
  mops up any residue.
- **The Custom screen shows the ship** — the menu's "rotating ship" was
  always a static entity circled by the camera path, so previewing your
  pick is a model swap on that entity plus the inverse of the famous ×2.8
  (player hulls at 0.35 fill the intro camera's framing exactly). Enter
  Custom: your ship is on the stage; tap another: the stage follows;
  leave: the classic intro hull returns.

Still open from these sessions: the **online** half of the 213 report was
never reproduced — the rig plays a staggered GameKit start clean, the
GameKit path reads clean, and the discriminant for the next device test is
the "Online - you are Player N of M" line: if it never appears, Apple never
matched the two devices and the fault is not in this code.

### 2026-08-25 — round 32 (v2 in five phases: the netcode learns to count past two)

The 4-player rewrite, executed as staged surgery on the v2 branch — each phase
compile-proven locally (zig cc against the real macOS headers, mock `dns_sd.h`)
and smoke-tested on CI before the next:

- **P0 — the minefield.** ~120 lines of dead 2010 prediction code removed (its
  only consumer sat after an unconditional `return`), the death packet got its
  own type (it shipped as `NET_RUNNING` — a value from an unrelated enum that
  collided with `NET_RTM_COMMAND`), and every hardcoded `2` in the player
  arrays now sizes off `MAX_NUM_PLAYERS`.
- **P1 — seats, not roles.** Every device sorts all `gamePlayerID`s (its own
  included); the index in that order is the SEAT, seat 0 hosts — the N-player
  generalization of "lowest id wins", bit-identical at 2. Inbound packets carry
  their sender's seat, mapped by the GameKit layer.
- **P2 — per-seat state.** `net_peer_t gPeers[]`: per-seat sequence space,
  liveness clock, barrier flags. The handshake is a COUNTING BARRIER (host
  counts joins → one preload echo; counts loads → dedupes the colour table,
  one GO carrying the full loadout table). Packets stamp `senderSeat` +
  `protoVersion` (recycled dead fields — same offsets, same size; a v1 build
  joining a v2 lobby is dropped at the door instead of desyncing). And the
  rule the tester asked for: **one player dropping must not kill the party** —
  `NET_OnSeatLost` parks the ship (the exact RIP idiom from `P_Die`) and play
  continues; only the LAST peer's loss ends the session.
- **P3 — four ships in the sky.** `MAX_NUM_PLAYERS = 4`; seats 2/3 tuck in
  behind-and-between the classic pair (quinconce; scene assets only author two
  spawn matrices, so `world.c` synthesizes the inner pair); shared pool of
  **N×3 lives** (12 at four — "le jeu est chaud, il faut au moins 10 vies")
  shown as one icon + `x12` (twelve icons would overflow the sprite buffer);
  **one team score** everywhere a score is shown or uploaded; enemy HP scales
  ×N; LEE aims at the *nearest* living ship (it had aimed at player 0 since
  2009); ships 3 & 4 exist (the `hpp` high-poly hull resurrected from the
  intro, and the Ghost — the classic hull at 55% alpha).
- **P4 — the lobby.** The LAN election becomes a sorted-IP ROSTER with a 4s
  settle window (so a party of four all get seated before anyone starts), the
  discovery pump keeps browsing after first contact, and sends broadcast one
  datagram per seated remote. Online, **Others → Online** now asks "How many
  players?" (2/3/4) and GameKit matches exactly that many.

**And then the part that actually matters: proving it.** A party of four cannot
be tested here — it needs four iPhones on one WiFi, or four Game Center
accounts. So `tools/netrig` runs **four instances of the real
`engine/src/netchannel.c` in a single process**: each peer is the engine file
compiled *verbatim* with its symbols macro-renamed, on top of a fake in-memory
UDP network and a GKMatch mock (plus POSIX/`dns_sd` shims, so the Apple branch
compiles off-iOS). 152 assertions over 7 scenarios — a party forming out of
order, a seat dying *during* the handshake, level transitions, a player
quitting mid-match, one device that never discovers another — and six
deliberate mutants that must fail, because a harness which passes on broken
code proves nothing.

It found ten defects, four of them deadlocks that would have shipped:

- **The handshake had no timeout at all.** The per-seat liveness check lives in
  `NET_Receive`, which returns early until the match is running — and the sim
  clock is *paused* during the handshake, so milliseconds cannot measure a
  silence there anyway. One player quitting between two acts hung the whole
  party, forever. Now a frame-counted watchdog drops the seat holding the
  barrier and re-evaluates it (no packet was coming to trigger that).
- **A silent host reads exactly like a dead host.** With four players the two
  early joiners tore their sessions down while the host was still waiting on
  the fourth — hence a lobby heartbeat.
- **The LAN seat table was private to each device.** Seats came from each
  device's own Bonjour browse, and a settled roster stopped re-browsing: device
  B could stay unaware that device C exists, seat everyone differently, and the
  party would split into two incompatible simulations — or deadlock outright
  when the seat *numbers* disagreed and every packet failed its identity check.
  Lobby packets now gossip their sender's roster; the tables converge.
- **Parking a lost ship was four independent stopwatches.** Hundreds of
  milliseconds apart (seconds, over GKMatch), and for that whole window the
  parked ship sat at different positions on different devices — enough for the
  aiming enemies to fire along different vectors and the simulations to part
  ways for good. The host decides now.

And a few older ghosts, from 2010: `net.buffer` sat two bytes off alignment
while every read path casts it to a packet struct (undefined behaviour ARM
happened to tolerate — the rig's sanitizer caught it on the first run); the
muzzle-flash budget counted one quad per ship where the engine draws two, so
the bullet vertex pool had always been a quad short per player; and the
absolute-position resync — the drift correction this streaming netcode leans
on — had been silent from act 2 onwards ever since, because the level start
rewinds the clock below its own timestamp.

### 2026-08-24 — round 31 (the second chance, the audit — and v1.8.0 closes the chapter)
- **🆕 The second-chance life** (the tester's design, formalizing a bug he loves):
  LAN co-op runs a shared life pool; when it dries up, the dead ship parks and
  the survivor plays on — and if the survivor finishes the act, the scene reset
  resurrects the fallen wingman for the next level. That accident is now a rule:
  the pool receives ONE gift life at level entry, strictly when both counters
  are at zero. Strictly: a review finding showed `<= 0` would also resurrect a
  LOST match (-1/-1) into a zombie run with an already-uploaded score whenever
  a scene load raced the game-over events.
- **A full 8-angle code review** over the sprint's diff, five parallel reviewers,
  ten findings, all fixed in one commit: the outro now reads the camera's own
  scene-safe drift velocity (the private tracker carried another scene's
  coordinates across the timer reset — the same static-vs-scene disease as the
  cameo, caught before shipping this time); the CI hooks are singleplayer-gated
  (local env state inside a lockstep sim is a desync waiting for a peer); the
  frame's client-state baseline lives once in `Set3DF`, mirroring `Set2DF`; the
  mesh-cache generation belongs to the cache itself (`ENT_CacheGeneration`,
  bumped inside the free — the next cross-scene entity holder is safe by
  construction); every diagnostic probe sits behind one cached gate, silent on
  player devices; and a lovely bash trap — `grep -c … || echo 0` prints "0\n0"
  on zero matches under Actions' `bash -e` — had killed the smoke's crash guard
  exactly in its target case.
- **🆕 The home screen finally says who it is**: a small *Reborn*, Brush Script
  like the act cards, scrawled uphill across the P of the 2009 SHMUP logo.

### 2026-08-24 — round 30 (the lottery was a dangling pointer — and the act is DONE)
- Three device runs of the same build: cameo once, nothing once, one crash — and
  once, the whole sky went dark gray. Two rounds of GL forensics (a client-state
  lockdown around the cameo, then a full state baseline for the decor pass,
  plus a smoke that finally *fires its guns* so the FX passes churn like a real
  game) hardened the renderer but didn't kill the lottery.
- The shared TestFlight crash log closed it: same site, but through the driver's
  client-array path this time — *the content was random*. The real killer:
  `ENT_ClearModelsLibrary` frees every non-static mesh at EVERY scene change,
  and the cameo's static entity kept its model pointer forever. First run after
  app launch: fresh model, works. Every replay — game over, menu, act 4 and
  back: freed heap, recycled by whatever the player's own run allocated.
  Drawable garbage, nothing, or SIGSEGV. **The lottery odds were set by the
  player's own session history — and the smoke, which ran the act exactly once
  from a cold boot, could never see it.**
- Cure and proof, both structural: the mesh cache bumps a generation on every
  purge and the cameo reloads when stale; and the smoke now **plays like a
  player** — it finishes act III, tears the scene down, re-enters, and a new
  assertion demands the cameo's probe dip in the REPLAY pass. The run prints
  its own verdict: *"CAMEO PRESENT ON REPLAY. The lottery is dead."*
- On device: **cameo with lightning, three runs out of three.** And with the
  outro rush finally outrunning the camera (the 2010 escape constant assumed a
  rail that had ended; act 3 detaches mid-cruise, so the camera used to overtake
  the fleeing ship and swallow it), the tester called it:
  ***"Pour moi l'act 3 est ok. On n'y touche plus."*** — Act III is frozen.

### 2026-08-24 — round 29 (the probe learns to see — measurement replaces hope)
- After three invisible-cameo builds, the loop changed: no more shipping and
  praying. The cameo's flight was moved across the patch of pixels the smoke's
  `[cull]` trace already measures every second — so a rendering cameo MUST dent
  the sky-band luma, and the lightning MUST spike it. Two Simulator runs
  measured exactly that (48 → 26, bumps at the strike timestamps), turning
  "je ne le vois pas" from a mystery into a differential: renders in the
  Simulator, invisible on device.
- That differential killed two hypotheses with one Release-configured smoke
  (compiler exonerated) and led to the device-only suspects: the silhouette
  went **untextured** — flat fixed-pipeline color, the exact path of the
  crossing stars that had always worked on device — erasing every texture-state
  divergence at once. The outro was mechanized the same way: the ship's escape
  now adds the camera's own measured speed, so the "fonce tout droit" exit
  reads identically in every act, and the prolog keeps its 2010 look bit-exact.
- The end-of-act mystery dissolved under the same instruments: the epilog →
  transition chain was traced (`[title]`, `[scene]`) and proved *working* — the
  "disappearing ship" was the classic fly-off playing 1 second under the epilog
  card's fade instead of 2 seconds on stage, as act 1 stages it.

### 2026-08-23/24 — round 28 (storm over the city — and a 16-year-old GL landmine)
- The boss cameo was reported invisible no matter its size or tint, and TestFlight
  logged a device crash. The crash log (pulled via the App Store Connect API) showed
  a SIGSEGV inside the GL driver under `RenderTTBBossCameoF`, faulting on address
  `0x0154014c014b014a` — **consecutive vertex indices read as a pointer**.
- **Both symptoms were one bug**: the runtime-loaded boss model lives in RAM, and
  `RenderEntityF`'s client-array path (2010) never unbinds `GL_ARRAY_BUFFER`. Any
  VRAM entity drawn earlier leaves its VBO bound, turning the cameo's heap pointers
  into buffer offsets: garbage triangles most frames (invisible), a wild read when
  the address falls badly (the crash). One `glBindBuffer(GL_ARRAY_BUFFER, 0)` fixes
  both. The 2010 code was never wrong on 2010's fixed draw order — the new act
  reordered the frame and armed it.
- **Belt and suspenders**: the cameo also now flies **above the true horizon**
  (the camera pitches 28° down; towers top out below camera height, so nothing can
  ever rise above that line to cover a no-depth-write silhouette drawn before the
  city). Proven with the scene mock at three crossing positions before pushing.
- **🆕 Lightning** (the tester's idea: "le faire clignoter comme éclairé par un
  éclair"): a fixed strike train — doubles, 120 ms exponential decay, pure function
  of the simulation clock so both lockstep peers see the same storm — flashes the
  hull from brooding silhouette (0.30) to near-white (0.94). The last strike lights
  its dive behind the skyline.
- The finale's last hedgehog circle was cut: its 6 s lifetime outlived the control
  lock before the stats card. The Devils came down from 80 to 55 HP.

### 2026-08-23 — round 27 (the hedgehog spin saga, or: trust the rig, not the axis names)
- Four device rounds to make the FHT roll properly in the side view, ending with a
  lesson worth the price: **the 2010 euler formulas have permuted axis names** — at
  x=z=0, `yAxisRot` builds a rotation about the matrix **Z**, and `zAxisRot` one
  about the matrix **Y**. Three shipped attempts each spun a wrong axis (loopings /
  yaw / tilted loopings, in that order).
- After the third miss the tester set the rule that should have been round one:
  *"arrête d'utiliser mon quota GitHub Actions — simule avant d'envoyer."* A local
  rig (`spin_rig.c`: the real `camera.c` + `matrix.c`, the euler construction
  verbatim, the real FHT mesh) replayed all four shipped configurations and
  **reproduced all four device verdicts** — then, and only then, was the fifth
  configuration believed: compose `blend34 · euler` with the spin on "zAxisRot" =
  rotation about the model's own tilted disc axis. Disc-normal drift over a full
  turn: **0.0°** — the 3/4 ellipse holds still, the spikes wheel inside it, a coin
  spinning at three quarters. Upright keeps the 2010 cartwheel bit-exact.
- The metric matters: bounding boxes barely move under a yaw (15%) — the
  discriminator that matches perception is the **projected disc normal**.

### 2026-08-23 — round 26 (the side view fights back)
- Tester's brief after the first armed pass: hedgehogs everywhere, a sweeper that
  actually sweeps, and craft you can recognize. Delivered as one round:
- **FHT ×3** (28 → 84 in the 26 s window), nearly all dead straight — the earlier
  "loopings" were authored curve control points, not the engine.
- **The balayeur climbs**: its 2010 drift is a hardcoded `+X` slide; in the side
  view the same drift now runs up the screen, curtain streaming left — two
  climbers scale the whole screen while firing.
- **The drops lie down**: the curtain's quad was axis-aligned; it now swaps extents
  and quarter-turns its texture with the beat, head leading. Upright emission stays
  bit-exact 2010. All of it proven in `tha_rig.c` (real `tha.c` compiled, 9 asserts,
  the bugs reproduced BEFORE the fixes were trusted).
- **3/4 poses**: `CAM_GetTTBBlendCapped(0.62)` — flat disc craft (turret, Devils,
  then the hedgehog too) hold a readable three-quarter pose instead of thinning to
  a 5.7-unit blade (11.5 units tall at 3/4).
- The three Devils tour the side view one per costume, and the V5 ambush closes as
  a simultaneous mirror pincer — the smoke log's new `[devil]` trace proved a
  "missing" ghost had spawned all along, just 400 ms too late to be seen.

### 2026-08-23 — round 25 (the Devils fight — phase 1 validated)
- **🆕 The Devil's weapons**, one per costume, all drawn from bullets the game
  already owns (the enemy particle pass binds the PLAYER's bullet atlas — a weapon
  is just texture coordinates): the Original fires a **trident** of three straight
  red streams; the Anthracite whips a **lasso** of sweeper drops; the Ghost drops
  a stone in water — expanding **rings of the player's own yellow shots**.
- Devils became real elites: a flat 80 HP (the type's 2009 base was 10) and a
  further ×0.85 mesh trim. The ghost's unreadable 0.30 alpha now **shimmers**
  0.42..0.75 on the simulation clock.
- Tester's verdict: *"les devils sont nickel"* — **phase 1 of Act III validated.**

### 2026-08-22/23 — round 24 (the Devil resurrected — a 2009 enemy's first spawn)
- **🆕 The hidden enemy ships.** `ENEMY_HAB`, "le Devil" — modeled, textured and
  coded by Fabien in 2009-2010, never once spawned by any shipped scene — enters
  the game seventeen years later. Its original texture was recovered by decoding
  the shipped `.pvr` (PVRTC1-4bpp decoder written for the occasion) and installed
  where `enemies.mtl` had pointed all along.
- **Three costumes** via the entity tint the boss missiles already used: the
  resurrected silver, an anthracite stealth coat, a translucent ghost (the enemy
  pass gained per-entity alpha blending). The tester picked all three: one Devil
  per phase of the act.
- Phase 1 redesigned to the tester's plan: three LEE columns under a parked
  turret; the three Devils; a four-sweeper pincer over hedgehog volleys;
  converging act-1-style columns; a rear-rake ambush.
- **🆕 The boss cameo** (tester's idea): the Act IV boss crosses the side-view sky
  once, face-on, a distant dark silhouette between the dome and the stars —
  foreshadowing, not a fight.

### 2026-08-17 — round 23 (Act III gets its enemies — and the smoke test earns its keep)
- **Waves authored per view** (104 events): mixed-type combos the original acts
  never ran, plus classics; the side view packs staggered hedgehog streams across
  the tall screen. Enemies blend to **profile** in the side view through the same
  shared matrix as the player, so their authored spins still read; authored bullet
  patterns rotate with the beat (`CAM_TTBRotateSS`), aimed shots stay aimed —
  screen space IS the screen in any view.
- **The CI mystery**: two red smoke runs showed "scene 0 at twenty minutes". Not a
  crash — the Simulator's idle ship was being **rammed** by the first hedgehog,
  game over, menu, and the sim clock never resets. The invulnerability guard only
  covered the bullet path; ramming deaths live in a second collision routine.
  Lessons now baked into the harness notes: percussion kills bypass
  `COLL_CheckPlayers`, the sim timer survives game-over, and stdout is buffered.
- A one-page **rogues' gallery PDF** (every enemy rendered from its real mesh and
  texture, the boss enthroned below) became the design table for everything above.

### 2026-08-16/17 — round 22 (the transition lands — "la transition est nickel")
- Build 190's verdicts closed one by one: the ship's hull reads 20% slimmer in
  profile, bullets thin with the beat, the ghost fan rotates at fire time, and the
  sky domes sank to -2500 so the horizon seam vanished behind the skyline.
- **🆕 Crossing stars**: nine of them on three parallax layers slide across the
  side-view sky, trails behind the motion, pure function of the simulation clock.
- **The 180° snap, root-caused**: at both beat transitions the ship flipped
  belly-first for one arc. The `fromAboveRotation` initializer's braces group **per
  column**, not per row — the in-plane arc was leaving from a transposed pose. Two
  sign flips fix it; at f=0 the blend is the original billboard **bit-exact**. The
  lesson that stuck: probe transitions at intermediate blend values, and read
  column-major initializers as columns.

### 2026-08-16 — round 21 (TTB on-device round 1: the ship flies right-side up, shoots forward)
- Build 188 on device: the side view itself reads (the act-1 rail's own camera
  moves in the top-down stretches are **kept** — "c'était bien comme ça"), but
  the ship flew **on its back** and kept shooting **up-screen**.
- **Upside-down fix**: the model's back is **-Y** (that is why `fromAbove` sends
  +Y *away* from the camera in the top-down view); the profile pose now sends
  -Y to screen-up instead of +Y.
- **🆕 Bullets follow the beat**: they travel along the rotated gameplay axis
  (up-screen upright, toward the nose in the side view — sin/cos of the same
  simulation-driven angle on both lockstep peers), the capsule sprite rotates
  with them from its center, and the hitbox AABB wraps the rotated capsule.
  Upright, all three reduce exactly to the original code.
- **The sky was blue, then black** (device screenshot): two causes, both
  invisible from the top-down view that had covered this sky for 16 years.
  (1) Act 1 stages dawn → evening → night with three overlapping domes; under
  live culling they are all drawn, without depth writes, so the LAST dome
  painted wins per pixel — and a far dome's near-black rim wiped the sky the
  near dome had just drawn. The skybox path now draws **only the dome nearest
  the camera**. (2) The remaining sky was the evening dome — handsome deep
  blue with a sunset streak, exactly "夕 -Dusk". The act keeps it everywhere:
  **`act3.map`** puts a new **`SkyDome_DuskStars`** dome (the evening mesh
  under its own shader name) in all three sky slots. Same entity count and
  order, so the act-1 rail's baked visibility stays aligned.
- **🆕 Stars** (user's ask — "si le bleu est foncé, on rajouterait pas des
  étoiles ?"): `SkyDome_DuskStars.png` = the evening texture plus a generated
  240-star field — seeded, denser and brighter toward the zenith where the
  blue is deepest, skipped over the sunset streak and bright clouds, a handful
  of glow-halo stars among pinpricks. Act 1's own sky is untouched (new
  shader + material entry, PVR/low-quality falls back to the starless dome).
- Known cosmetics left for the enemy round: the muzzle flash still sits above
  the ship in side view, and the ghosts' fan is still screen-up.
- **Process change (user's ask): no more TestFlight builds without an explicit
  go** — fixes accumulate on master (free compile checks only) until the user
  calls for a build.

### 2026-08-16 — round 20 (the TTB beat, redone as a real side view)
- **The v1.5.6 roll was wrong, and the tester said so immediately**: a pure roll
  around the view axis just rotates the picture — the decor turned on screen
  while the ship stayed top-down, "comme si on tournait le téléphone". A side
  VIEW needs the camera to **move**. The beat is now an **orbit of the flight
  corridor**: over 3 smoothstepped seconds the camera slides 420 units onto the
  flank, **climbs** 218 (act 1's towers reach y=319 — higher than the rail at
  162; a camera at gameplay height would sit inside the skyline), and tips its
  gaze from straight-down to a 28° look-down at the corridor, all while still
  flying forward. Horizon level, ground at the bottom, the city scrolling
  **left** — a side-scroller. The exit at 73 s is the same move in reverse.
- **🆕 The ship turns to its PROFILE during the beat** (user's call, after a
  mocked render): the player matrix blends from the usual "top toward the
  camera" billboard to "nose leading the travel", on the same clock as the
  camera swing, in both directions. Zero effect on any act without a `ttbRoll`.
- **🆕 A local, quota-free debug loop** (user's ask: stop burning CI runs to see
  a camera pose). `ttb_harness.c` (scratch, not committed) `#include`s the real
  `camera.c`, stubs the engine around it, drives the exact scene timeline, and
  renders **the real act-1 city mesh** (cityBlue.obj.md5mesh, its 48 map
  instances, painter-sorted, the scene's linear fog simulated) through the real
  `CAM_ApplyTTB` into SVG frames — plus numeric assertions (pose at key times,
  scroll direction, no >1.5°/frame snaps, exact return to the rail). The pose
  itself (**B**: 420 out / y=380 / 28°) was picked by the user from three
  candidate renders, and the 40→43 s swing storyboarded frame by frame, all
  before a single build left the machine. Found this way: **the stock fog
  (endAt 405) would have drowned the whole side view** — act 3's fog now ends
  at 2500, tuned in the same renders.
- **The act cards are honest now**: act 3 gets its own **`duskTitle.png`**
  ("夕 -Dusk / Act III", generated in the family style — kanji + script + rule),
  and `boss.png` reads **"Act IV"** (the old "Act iii" strokes located by alpha
  scan, cleared, redrawn).

### 2026-08-16 — round 19 (Act III: the TTB act — the game grows a fourth act)
- **🆕 A new Act III, built around the TTB beat**, inserted before the boss —
  which becomes **Act IV**, the proper finale. The act flies act 1's baked rail
  over the dawn city with a dusk-violet fog, and runs the timeline the beat was
  designed for: **30 s of the usual view, 30 s on the side, 30 s back** — the
  camera rolls 90° in 3 s (smoothstepped), the vertical shooter reads as a
  side-scroller for half a minute, then it rolls back up. **No enemies yet, by
  design**: the level flies empty so the beat can be judged on device first;
  waves come in a later round, authored per view.
- **🆕 The TTB system is real** (dormant since the 2026-07 prototype): a pure
  camera **ROLL around the view axis**, driven by a new scene event
  (`at 40000 ttbRoll angle 90 duration 3000`, absolute angles). A roll — not a
  reframing — is what makes it affordable: the camera keeps the rail's position
  *and* view direction, so the baked visibility stays aligned; the billboards
  (ship, enemies) are built from the view matrix so they stay upright and the
  controls need no inversion. The one thing a roll does break is the frustum's
  footprint, so **live decor culling switches on for exactly the length of the
  beat** and hands back to the bake once upright — the same "scope the freedom
  to where the constraint binds" rule the end-of-rail U-turn taught us.
- **Renumbering fallout, made generic instead of moved**: `driftAtEnd: 1` is now
  a scene-file camera key (was `sceneId == 3` hardcoded in dEngine); the
  progression gate and clamp follow `numScenes`; the act-select menu grew a 2×2
  grid (a fourth stacked button would have sat on the Back button); the per-act
  stats guard a zero-enemy denominator like the ending card always has. The two
  boss CI workflows follow the act: `smoke-boss.yml`, `bake-boss-rail.yml`.
- **Known papercuts, on purpose**: the boss's title card still reads "Act III"
  (`boss.png`, art to retouch), and the new act borrows `dawnTitle.png` ("Act 1")
  as a placeholder until it gets its own card.

### 2026-08-14 — round 18 (the ending screen, and two menu papercuts)
- **🆕 A real end-of-game screen (v1.5.4)**. Beating the LOFB used to reuse the
  per-act "act completed" strip — the same four stat lines, gone in 7 seconds,
  with the score row and lives still drawn on top. The last act's epilog now has
  its own card: **MISSION COMPLETE**, the run's numbers (difficulty, bullets fired,
  hits, **accuracy**, enemy cleared, total score) and a **RANK** from D to S,
  weighted mostly on how much of the map you cleared with accuracy and the
  difficulty level as tie-breakers. It holds **16 s** instead of 7, on a full-screen
  veil (the per-act epilog only dims the title strip at the top), fading in over
  1.2 s and back out over the last second. The in-game HUD steps aside while it is
  up, and the player is made **immune** for its duration — a stray escort bullet
  turning the victory card into a GAME OVER would have been a rotten way to end
  the game.
- **Back button in the Demo**, identical to the tutorial's: same top-centre
  `[ BACK ]`, same tap zone, same exit to the main menu. The demo used to be a
  142-second one-way trip.
- **Credits: `Reborn:  Jr Cabbry`** added to the top block, and the line spacing
  tightened (50 → 42 in that block, the rest re-balanced) so the whole roll still
  fits between the title card and the Back button.

### 2026-08-14 — round 17 ("Enemy cleared: 100%+" — the boss broke the maths)
- **The end-of-act stat could exceed 100% in act 3 (v1.5.3 fixes it)**. The
  denominator is counted once at scene load by walking the *scripted* spawn list —
  but the boss spawns its escorts and seekers at runtime, straight through
  `EV_SpawnEnemy`. Every reinforcement you shot down counted as a kill the total
  never knew about; a long fight pushed the ratio past 100%. Only act 3 spawns
  enemies dynamically, which is why sixteen years of acts 1–2 never showed it.
  Each dynamic spawn now increments the total too — the stat is honest again (and
  escorts that cross the screen unkilled now count against you, as they should).

### 2026-08-14 — round 16 (the finale can't be skipped anymore)
- **Playtest of round 15 surfaced a silent interaction (v1.5.2)**: the homing
  seekers unlocked at −75% but launched *from the arms* — and at 200 HP each, the
  arms rarely survived that long, so most players would never see the finale at all.
  New split, per the designer's call: the **body** launches the seekers (one per
  volley, alternating ports — the finale always shows up), the **arms** fire the
  big energy shots (destroy one to silence a side, both to cancel the attack — the
  strategic reward stays, moved to the −50% tier), and the arms are beefed up
  **200 → 400 HP** so they plausibly live to their unlock.
- Energy shots now aim from the arm that fires them, and the seeker cadence is one
  every 4.5s (the old arm pair fired two per 6.5s).

### 2026-08-14 — round 15 (boss pacing: attacks join one at a time)
- **The boss's arsenal now unlocks as a ladder (v1.5.1)** instead of everything
  arriving together at 85% HP: the fight opens on the aimed fan alone, then each
  milestone of health LOST adds one attack — **−15%** the rotating spray, **−25%**
  the escort waves, **−50%** the big energy shots, **−75%** the red homing seekers.
  The last quarter doubles as the frenzy (wider fan, faster cadences). The
  mega-laser keeps its own independent clock — its rhythm was judged right as is.
- **Boss HP 3400 → 2500** (5000 in multiplayer): with the readable health bar and
  the staged arsenal, the fight's length now comes from its pacing, not a sponge.
- **The beam finally has a rounded base**: each of the beam's six alpha strips now
  pulls its near edge back along a semicircle, so the laser starts as a capsule
  cap instead of a flat cut. Same trick as everything else in this fight's VFX:
  pure per-vertex geometry on the white texel, no new art.

### 2026-08-14 — round 14 (the return leg stays on the corridor)
- **Fixed the off-axis return (v1.5.0)**: the round-13 U-turn worked, but the flight
  back drifted sideways — half the screen slid into the dark, then all of it. The
  patrol was steering by the **last segment** of the rail, and act 2's rail ends on
  an outro flourish that veers ~3° sideways; over a 30 000-unit leg that's >1 500
  units off the city corridor. The axis now comes from the corridor **actually
  flown** (rail start → rail end), which is straight to a fraction of a degree.
- The smoke rail now ends on the same kind of flourish, and the test asserts the
  patrol stays within the corridor (measured |X| max 181 units, vs >1 500 with the
  bug) — every frame still "drew decor" while the real thing failed, so counting
  draws was again not enough.

### 2026-08-13 — round 13 (the U-turn, where it actually belongs)
- **The city turns around and scrolls back — without touching the flight (v1.4.9)**.
  The act flies its shipped **baked** rail, opening included: nothing re-authored.
  Live decor culling now switches on at exactly **one** point — when the rail runs
  out. That is the only place the 2010 bake stops being usable (frozen on its last
  frame), and measured on device it is where the decor collapsed to a single drawn
  entity. From there the camera is free, so the act flies a real patrol: it settles
  out of the rail's outro pose, turns a **true 180°**, flies a long leg back over
  the city already flown, turns again, and repeats — bounded by the stretch actually
  flown, so it can never sail out of the built world.
- *The lesson from three failed builds*: applying live culling to the **whole** act
  is what made act 3 black. The sky domes enclose the camera, so no per-entity
  frustum test can ever reject them — and unfogged, drawn every frame, they painted
  night sky over the entire city. Scope the freedom to where the constraint actually
  binds, and leave the working 95% alone.
- **Act 3 is no longer needlessly dark**: its fog ramp started at the camera while
  act 2's starts at 70 units, costing the city ~18% of its texture for nothing.
  Now measured at parity — 46 against act 2's 44.

### 2026-08-13 — round 12 (the sky was painted over the city)
- **Act 3 was black from the first second (v1.4.7 fixes it)**. The map's three
  "background entities" are sky domes, and they *enclose* the camera — so a
  per-entity frustum test can never reject them, unlike the 2010 bake, which
  simply dropped their faces whenever the view pointed down. Under the new live
  culling they were therefore drawn every frame, **unfogged**, covering the whole
  city with a near-black night sky. They are now drawn as a proper skybox, with
  depth writes off, so the city painted afterwards always wins.
- *Why the smoke test kept saying "fine"*: its brightness probe took a single
  sample after the frame was complete, so it was measuring **the dome** while
  reporting that the decor rendered. It now samples before *and* after the city
  is drawn — two equal numbers mean something is hiding it. (The simulator also
  only admitted the domes intermittently, where a real iPhone's wider field of
  view admits them constantly: CI was green while the device was black.)

### 2026-08-13 — round 11 (a 2010 lexer bug that silently zeroed every camera rail)
- **Act 3 opened flying backwards into the void (v1.4.6 fixes it)**. Two causes, and
  the second one is the interesting one.
- **Cause 1 — a borrowed outro shot.** The new rail's first keyframes were copied
  from `act2.cp`'s text: they look toward +Z while the flight travels toward −Z, so
  the camera faced *away* from the city and flew backwards through it. That text file
  turned out **not** to be the source of the `act2.cp.cp2b` act 2 actually ships —
  never assume a 2010 `.cp` matches the `.cp2b` sitting beside it. The act-3 rail now
  opens on its own top-down keyframe, already cruising at the rail's own ~240 u/s.
- **Cause 2 — `LE_skipWhiteSpace` mis-parsed consecutive comment lines.** After
  skipping a `#` line the lexer fell through to its whitespace test instead of
  re-entering its loop; if the next line was *also* a comment, its `#` is not
  whitespace, so the lexer returned and handed the comment's words back **as tokens**.
  Every read after that shifted by one, and in a `.cp` rail *every number became 0*:
  all keyframes at t=0, position (0,0,0). The camera sat at the origin while the
  end-of-rail hover pushed it slowly backwards — precisely the "we start backwards in
  the black" report. Any commented data file in the engine was exposed to this; the
  fix is one `continue`.
- **The smoke test now proves the rail is flown, not just that pixels exist.** It ran
  green through all of this because it swapped in a compressed test rail (starting
  mid-city, top-down) and only asserted "some decor was drawn" — which a camera stuck
  at the origin satisfies happily. It now flies the **real** rail first, dumps what
  the lexer made of each keyframe, and fails unless the camera is looking down and
  advancing into the level. Verified: `pos=(0,162,-640)` → `(0,162,-6183)` through the
  U-turn, never a frame without decor.

### 2026-08-12 — round 10 (a real U-turn: the boss act stops obeying the bake)
- **The city now turns around and scrolls back (v1.4.5)**. Previous rounds fought
  the same wall: at the end of the rail the camera could only pull back and hover,
  because the 2010 pipeline bakes *per camera frame* which faces of the city are
  visible and the renderer skips any entity the bake left empty — look anywhere
  else and the decor is simply gone (black). Round 9 removed the yoyo but left a
  ~90° pivot into the void, inherited from act 2's **outro keyframe** (a 2s dive +
  bank meant as the act-2 exit shot, never as a boss backdrop).
- **What changed**: the boss act now culls the decor **live** — one frustum test
  per building, per frame, using the very same test the offline bake uses. That
  frees the camera completely, so act 3 gets its own rail: act 2's flight verbatim
  (minus the outro keyframe), a **true 180° U-turn**, a full top-down return flight
  over the whole city, a second U-turn, then off again — over 5 minutes of scrolling
  after the boss arrives, and no baked visibility set to obey.
- *Why this was always the right move on modern hardware*: on a tall iPhone the
  renderer **already** threw the baked face lists away and redrew full meshes (that
  was the full-screen fix from the start of the project), so the bake had been
  reduced to switching whole buildings on and off. Doing that switch live costs one
  bounding-box test per building and removes a class of bugs — plus the rail is now
  an editable text file, expanded in memory at load, with no offline bake step at all.
- **Two traps worth recording for anyone touching the 2010 data pipeline**:
  the lexer has `,` **disabled** as a separator, so a comma-separated `.cp` rail
  (like the shipped ones) parses to garbage through the text path — every frame
  lands at t=0 and the camera sits at the origin; new rails are space-separated.
  And the engine's file logging had been compiled out since 2010, which is now
  runtime-enabled (`SHMUP_LOG_FILE`) — the only reliable way to see what the game
  did on a device or a CI Simulator.

### 2026-08-10 — round 9 (no more end-of-map yoyo + boss HP tune)
- **The end-of-map camera yoyo is gone (v1.4.4)**: when the baked rails run out
  mid-fight, the v1.4.2 patrol oscillated endlessly over the flown stretch — it
  read as "the ship sets off, comes back, sets off again…". Now the camera pulls
  back **once** (same smooth dive), then sets off forward again **ever more
  slowly** (exponential decay) and **never reverses**: the motion fades into a
  calm hover over the city, always stopping short of the void where the decor's
  bake ends. Same safety guarantees as before (never shows the un-baked black),
  just no more back-and-forth.
- **Boss HP −15%**: 4000 → 3400 (6800 in multiplayer) — round-8 playtest found
  the fight a touch long now that the health bar makes progress readable.

### 2026-08-07 — round 8 (the boss health bar, for real this time)
- **The health bar is now a real graphical bar (v1.4.3)** — a light frame, a dark
  background and a bright red fill (subtle top-to-bottom gradient) drawn as **solid
  colored quads** under the score. No font glyphs are involved anymore: the fill
  drains **smoothly with every hit** (not in 20 coarse steps), the spent portion
  stays visible as dark background inside the frame, and an arm kill visibly carves
  its −8% chunk. Only the "BOSS" label is still font text — the one part that always
  rendered. Fill width rounds up, so the bar reads empty exactly at the kill.
- *Post-mortem of the three failed text bars*: v1.3.7 (`=`/`-`) read as a static
  line, v1.4.0 (`=`/blank) read as invisible, and the round-7 `I`-segment fix
  (v1.4.2) — it turns out — **was never pushed or built**: the fork's latest tag was
  still v1.4.1, so the last TestFlight build (173) still had the invisible bar.
  Glyph-segment bars at that size are simply too subtle on device; this round stops
  fighting the font and draws geometry instead.
- New engine primitive: `renderer->RenderTexturelessSprites` (untextured,
  per-vertex-colored 2D quads) — implemented in the GL ES 1.1 fixed renderer the
  iPhone build uses (same state dance as `FadeScreenF`), stubbed in the GL ES 2.0
  path like its `FadeScreen`/`DrawControls` siblings. This ships round 7's changes
  too (end-of-decor yoyo fix, arm-kill triple explosion), which never made it out.

### 2026-08-05 — round 7 (end-of-decor yoyo, invisible health bar, arm-kill clarity)
- **No more black/decor yoyo at the end of the city (build 174, v1.4.2)**: the v1.4.0
  ping-pong swung the camera back **to the exact point where the decor (and its baked
  visibility) end** — so once per cycle the view stared into the un-built void: black,
  then decor, then black again. The patrol now dives back once and oscillates **over the
  back portion of the flown stretch only**, never returning to the void view. The camera
  also **eases out of the ~45° bank** the rail froze on (the path is cut mid-turn) back
  to a level down-the-corridor view. *(A true 180° U-turn isn't possible with the 2010
  bake: the delta-visibility stream deletes faces once they leave the frame, so looking
  backward shows nothing — this patrol is the closest look we can get without re-baking
  the path.)*
- **Boss health bar actually visible**: the bar's `=` glyph never rendered on device
  (only "BOSS" showed) even though the font atlas contains one — digits and letters from
  the same atlas render fine, so the bar now uses **`I` segments** (letters are proven
  on-screen). You'll finally see it drain — and see the **−8% chunk** when an arm dies.
- **Arm kills are unmistakable now**: a triple explosion walks off the destroyed arm.
  Playtest read "the laser destroyed its own arms" — in code the arms only ever take
  **player bullet** damage; what happens is the boss holds still through the 5.7s laser
  phase, your dodging fire sweeps the arm zones, and the arms tend to die exactly then.
  The kill needed to cut through that busy screen. (Quick check in play: as long as
  homing missiles still launch from a side, that arm is alive.)

### 2026-08-05 (hit feedback where you shoot + a 2010 FX bug)
- **Found & fixed a 16-year-old engine bug (build 173, v1.4.1)**: the enemy-FX index
  buffer was only initialised for its first **11 quads** (the init loop counted quads
  instead of indices — a bug shipped in the original 2010 source). Everything pushed past
  those 11 quads — most of the laser charge orbs, the warning line, the muzzle glows —
  rendered as invisible degenerate triangles. **The whole telegraph now actually shows.**
  The FX buffer also grew (64 → 192 quads) to fit it all.
- **Energy concentration before the mega-laser**: the charge orbs now hang wide then
  **accelerate into the muzzle** (instead of drifting in linearly), spin faster and
  faster, and each drags a bright **convergence tail pointing into the gun**; over the
  last half-second the whole gather **strobes**. Combined with the index fix above, the
  pre-fire tell went from "a faint purple blob" to an unmistakable implosion.
- **Hits flash where you shoot, not the whole ship**: the boss no longer does the
  full-body white flicker on every body hit (it's a single huge mesh, so shooting the
  nose lit up the arms too). Body hits read through the localised impact sparks; **arm
  hits now flash the arm itself** with a soft glow, plus the same bullet-burst particles
  as body hits — shooting an arm finally *feels* like hitting something.
- **Arm damage now shows and counts**: an arm below half HP trails light smoke (visible
  "keep shooting, it's working" progress), and **destroying an arm carves an 8% chunk
  off the global boss health bar**. The arms' own HP stays a separate pool (the bar
  still can't be emptied through the arms — the killing blow stays with direct fire).

### 2026-07-23 (destructible arms + no more black screen)
- **Destructible arms (build 172, v1.4.0)**: the two big side arms now have their own HP and
  are what launch the red homing missiles. Shoot an arm down and that side stops firing;
  destroy both and the homing missiles stop entirely. A destroyed arm bursts and keeps
  smoking/sparking. *(There's no dedicated broken-arm 3D model yet — the wreck sparks/smoke
  stand in for now; a real damaged-arm mesh is a follow-up if you want it.)*
- **No more black screen in long fights**: when the baked camera path runs out, the view used
  to coast into un-built void (black). It now gently drifts **back and forth** over the stretch
  already flown, so the city keeps scrolling under the boss — your "repartir dans l'autre sens".
- **Health bar clearer**: spent segments are now blank, so the lit marks visibly erase from the
  right as the boss loses HP (they blended together before and the bar looked static).
- **Laser charge much more visible**: it now gathers a swarm of big magenta **bullet-orbs**
  spiralling inward and swelling before it fires — an unmistakable "here it comes" tell.

### 2026-07-08 (boss readability pass)
- **Laser easier to anticipate (build 171, v1.3.9)**: the charge-up gathers a lot more energy
  now — longer telegraph (1.5→2.2s), twice as many sparks streaming in from further out, and a
  warning line that clearly shows *where* the beam will fire, ramping up sharply just before it
  erupts.
- **Missiles no longer loop back**: once a homing missile misses, it keeps going and exits
  instead of U-turning — a U-turn showed the underside of the mesh (which isn't modelled for
  that angle). They can still curve to chase you on the way down.
- **Boss damage is now visible**: hitting the boss throws off impact sparks right where your
  shots land on the arms, so you can see it taking damage (not just the health bar).

### 2026-07-08 (destructible homing missiles)
- **The boss arms now fire homing missiles (build 170, v1.3.8)**: from phase 2 on, both arms
  launch red-hot **heat-seeking missiles** that track you — but each has its own HP, so you can
  **shoot them down** before they reach you (they explode when destroyed). They turn at a
  limited rate, so weaving still loses them. Running into one is lethal. (No dedicated missile
  model yet — they reuse the small escort mesh, tinted red; art can be swapped in later.)

### 2026-07-08 (boss balance + laser pass 2)
- **Health bar now honest / no more "immortal" boss (build 169, v1.3.7)**: the bar used to
  read empty at the last ~5 % of HP (floor rounding), so the boss looked dead-but-alive. It
  now rounds up — at least one segment stays lit until HP actually reaches 0, and empties
  exactly at death. Max HP is captured while the boss is arriving (full HP, invulnerable) and
  frozen for the fight, so a frame hitch or an app background/resume can't make the bar refill.
- **Boss HP −20 %**: 5000 → 4000 (×2 in multiplayer).
- **Laser sweep faster** — a good bit quicker than v1.3.6, still a touch calmer than the very
  first version.
- **Muzzle/charge halo smoother** — was a magnified 16px sprite (pixelated), now a clean
  procedural radial glow.

### 2026-07-07 (laser polish)
- **Mega-laser look + feel (build 168, v1.3.6)**: the beam was a hard-edged block and
  swept a bit fast. It's now **soft-edged** — a bright white core fading through cyan to
  transparent edges, fading toward the far end too, with a subtle energy pulse; the charge
  and the muzzle use soft round glows (a swelling energy ball, sparks spiralling in) instead
  of flat squares. The sweep is **~28 % slower** — one calm left/right/left pass.

### 2026-07-07 (boss playtest round 2)
- **The boss got a signature move (build 167, v1.3.5)**, from Fabien's second round of
  on-device notes. (1) 🆕 **THE MEGA-LASER** — every ~32 s (the first at ~14 s), whatever
  the boss's health, it stops, **gathers energy** (a ring of sparks spiralling inward — the
  charge-up you asked for) for 1.5 s, then unleashes a **huge beam** for 3.4 s that pivots
  from straight-down and **sweeps left/right**. It only ever points downward, so the safe
  ground is **a top corner** — move up-left or up-right to survive. Hot white core inside a
  cyan glow. (2) **The laser vaporises the escort minions it sweeps through** (not the boss
  itself). (3) **A bonus escort wave spawns on every laser charge**, so there's always fodder
  for the beam. (4) **Phase 1 is much shorter** — the varied attacks (spirals, escorts, big
  shot) now start at 85 % HP instead of 66 %, so the triple-fan opening no longer drags.
  (5) **More gêneurs** in general: escort waves are now six ships (three per side, was four).
  (6) **The background no longer freezes** — in the boss act the camera keeps drifting forward
  once the baked flight path runs out, so the city keeps scrolling past through the whole
  fight instead of stopping dead. While the laser is charging/firing the boss braces perfectly
  still, then resumes its hover sway seamlessly.

### 2026-07-06 (boss playtest round 1)
- **Boss fight tuned from first playtests (build 166, v1.3.4)**: eight fixes/changes from
  on-device feedback. (1) **Locked acts now show "Locked"** on the act-select buttons (the
  label is swapped; the font is single-colour so a true grey-out would need renderer work).
  (2) **Act 3 now rides act 2's camera rails** — same decor, so its long straight flight and
  baked visibility are valid; Fabien's original act3 path was a short test spline that turned
  then froze before the boss arrived. (3) **Boss HP ×11** (450 → 5000; 10000 in multiplayer)
  — it went down far too easily. (4) **Score freezes at the killing blow** — leftover bullets
  mopping up escorts during the victory lap no longer pad the final score. (5) **The boss is
  untouchable while descending into position** (hitbox parked off-screen until its first
  lateral move — shooting it during the approach did damage). (6) **Fixed the visible
  position "jump"** at the start of its lateral sweep: the sway used total-time (sin was
  already mid-cycle when the fight began); it now uses fight-relative time, continuous with
  the arrival point. (7) **Escort waves doubled** (4 FHT per wave, two per side). (8) 🆕 **THE
  BIG SHOT**: from phase 2, a large slow orb (4× bullet size) aimed at the nearest player,
  every ~8.5 s (6 s in phase 3).

### 2026-07-06
- **Fix: unfair multiplayer game over while one player was still alive (build 165, v1.3.3)**:
  the shared-life pool intentionally lets the first player who exhausts it sit out (parked
  off-screen) while the other plays on — game over only when BOTH are down. But the parked
  "corpse" **could still be hit**: the collision checks only guarded invulnerability frames,
  so a stray bullet drifting off-screen would "kill" the dead ship again, push the shared
  counter below zero and end the match while the surviving player was doing fine. Dead
  players no longer collide (guards in both collision paths and in `P_Die` itself, identical
  on both lockstep sims so multiplayer stays in sync).
- **🆕 Act select, gated by progression (build 164, v1.3.2)**: New Game now goes difficulty →
  **"SELECT ACT"** (Act I / II / III), starting the chosen act with full lives. The game is
  hard — reaching the new act-3 boss by clearing the whole game in one run was a tall order,
  and practicing a single act is a standard nicety in arcade re-releases anyway. **An act is
  only selectable once it has been reached in play at least once** (solo or multiplayer): the
  furthest act reached is persisted across restarts, locked picks answer with
  *"Locked - finish Act N first"*, and the screen shows how far you've unlocked. Earning the
  boss stays part of the game.

### 2026-07-04
- **🆕 THE BOSS FIGHT (build 162, v1.3.0)** — Fabien's #1 wish, and a little archaeology: the
  2010 repo already contained the whole skeleton of a boss act that was never finished — act 3
  ("水 -Water", whose title card is literally named `boss.png`), the LOFB model/texture, a
  one-spawn scene that made the boss *appear* for 17 seconds and then quit to the menu, and an
  `updateLOFB()` left **empty** next to a scaffolded state machine. That fight is now real:
  - **`lofb.c` finally has its brain**: the boss eases down to a hover point, then fights in
    **three HP phases** — aimed fan bursts at the nearest player (always), a twin **rotating
    bullet spiral** (below ⅔ HP), and **FHT escort waves** (his scaffolded "Spawning" state,
    below ⅔ HP; everything faster and wider below ⅓). Attacks reuse the engine's own systems
    (SHAB's bullet emitter, the standard spawn payload), and all behaviour is driven purely by
    simulation state, so the fight stays **deterministic in lockstep multiplayer** (boss HP is
    doubled in MP like every enemy).
  - **Boss HP bar** under the score (`BOSS ====----`), boss-sized **hitbox** (the old one was
    the standard small-enemy box), HP 200 → 450, and a **victory bonus** (100 000 × difficulty).
  - **Winning ends the game properly**: on the killing blow — pyro burst, the ships fly to
    their rest position, the act-3 epilog plays, and the game returns to the menu. This also
    fixed a dormant 2009 bug: `sceneId + 1 % numScenes` parses as `sceneId + 1` (no wrap), so
    finishing the last act would have indexed past the scene table — never hit back then only
    because the stub act force-quit at 17 s.
  - **act3.scene rebuilt**: deep-blue "Water" fog, a ~30 s FHT gauntlet, a **WARNING** call-out,
    then the boss — and no more 17-second exit; the act ends when the boss dies.

### 2026-07-03
- **Smoother, more reliable LAN matchmaking screen (build 161, v1.2.10)**: three comfort
  fixes for "starting a local game is still awkward". (1) The waiting screen's background
  animation stuttered at ~3 fps — the discovery drain used a 300 ms *blocking* `select`
  every frame; it is now a non-blocking poll (same discovery latency, full frame rate).
  (2) Fewer "back out and retry" runs: a long-lived mDNS browse backs its queries off
  exponentially, so if the second device shows up late, discovery could sit silent for a
  long while — the browse is now re-issued every ~8 s until the peer is found, which
  restarts the query schedule. Also, the resolve steps' blocking waits are now bounded
  (2 s): a stale "ghost" advertisement left by a killed app could previously hang the
  menu indefinitely. (3) Clearer guidance: the screen now says "Looking for the other
  player... Open Others > Network on the second device", and the last diagnostic line
  is gone.

### 2026-07-02
- **🆕 Custom loadout in multiplayer + "player left" handling + start-of-match cleanup
  (build 160, v1.2.9)**: three multiplayer improvements from 2-player testing.
  - **Per-player Custom loadout online and on the LAN** — each device now sends its Custom
    choice (ship + bullet colour) inside the existing handshake packets; both ends then hold
    both loadouts and apply them when the level loads, so each player keeps his own look. If
    both players picked the **same bullet colour** (the real in-game differentiator), player
    two's colour is shifted **deterministically on both ends** — no negotiation, the two
    simulations stay identical. Defaults remain the classic P1/P2 look.
  - **"The other player left the game."** — a peer-liveness timeout (the peer normally sends
    every frame; ~5 s of silence means it quit, was backgrounded, or lost the network) ends
    the match cleanly on the remaining device: the session is freed, the menu scene is
    reloaded (previously the abandoned game kept simulating behind the menu), and a notice
    tells the player what happened. Online, a GKMatch disconnect triggers the same path
    immediately.
  - **No more ghost shots at match start** — at a multiplayer match start the timer is reset
    to 0, and the stale bullet pool (expiration time 0) briefly counted as alive ("0 < 0" is
    false), flashing old shots at their last positions. Expiry is now inclusive (`<=`) and
    reset bullets are parked offscreen.

### 2026-07-01
- **Loadout + multiplayer balance/robustness (build 159, v1.2.8)**: from on-device feedback —
  (1) confirmed the ship choice now takes effect and **removed the diagnostic overlay**;
  (2) **dropped Ship 3** (the `hpp` model renders far too small — Ship 1 & 2 kept); (3) the
  Custom buttons **deselect** after a pick instead of staying highlighted; (4) **enemies now
  have double HP in multiplayer** — with two ships firing, the game was too easy at solo HP
  (applied identically on both devices, so it stays in sync); (5) **backgrounding a 2-player
  game now ends the match cleanly** and returns to the menu, instead of leaving a frozen,
  desynced screen (you can't pause a live networked game; single-player still pauses + 3-2-1
  resumes).
- **Ship choice now applies + loadout persists (build 157, v1.2.6)**: the solo ship selection
  had no effect because the player model was loaded **once** at engine init (when the choice was
  still the default) and never reloaded. Now `P_ReloadShip()` re-points player 0's model to the
  chosen ship on every scene load (after the level config sets `modelPath`); it's leak-free
  because `ENT_LoadEntity` caches meshes by name. Multiplayer still uses the level's model0.
  Also: the loadout (ship + bullet colour) is now **persisted** in `NSUserDefaults` — saved on
  each pick, restored at launch — so it survives an app restart. (The temporary `SHIP cX`
  on-screen readout is kept one more build to confirm the choice now changes.)
- **Menu polish from feedback (build 156, v1.2.5)**: Others menu — swapped Network/Custom so the
  two multiplayer entries sit together, renamed **"Network" → "Local Network"**, evened the row
  spacing. Custom screen — the **Back** button no longer overlaps the last colour, and picking a
  ship/colour **no longer exits the screen**: it updates a status line ("Ship 1 - Red" by
  default) and you leave via Back (so you can set both ship and colour in one visit). This also
  makes it easy to verify the ship selection via the temporary in-game `SHIP cX` readout.
- **Custom-screen polish (build 155, v1.2.4)**: from on-device feedback — (1) the difficulty
  screen's **Back** button overlapped **Insane**; the three difficulty buttons were pulled up
  so Back gets its own row. (2) The bullet colours are now confirmed and **named** (Red / Blue
  / Invisible / Yellow — the invisible one is kept as a stealth option). (3) The **"Loadout"**
  button/screen is renamed **"Custom"**. (4) The three solo ships (p1/p2/hpp) looked identical
  in play even though the code loads three distinct meshes with distinct skins; added a
  temporary on-screen readout of the ship model actually loaded, to tell whether the choice is
  applied or the art is simply too similar.
- **🆕 Solo loadout: ship + bullet-colour selection, and a difficulty Back button (build 154, v1.2.3)** —
  new feature: an **Others → Loadout** screen lets the single-player pick their **ship**
  (among the 3 existing models p1/p2/hpp) and their **bullet colour**. Both reuse mechanisms
  the engine already had for multiplayer: the ship is loaded per-player from `modelPath`
  (`P_LoadPlayer`), and the bullet sprite already picks its colour from the atlas column by
  player index in `P_PrepareBulletSprites` — so single-player now substitutes the chosen ship
  path / colour column, while **multiplayer is untouched** (keeps its two distinct ships and
  bullet colours). Also added a **Back** button to the New Game → difficulty screen (it had no
  way out). Choices are in-memory for now (reset on app restart); colour labels are generic
  pending on-device confirmation of the atlas mapping.
- **Fix multiplayer end-of-level freeze (build 153, v1.2.2)**: at a level transition the game
  froze — decor still, soundtrack paused but the level-complete jingle kept playing. Cause:
  the level-load handshake (server→`LOAD_NEXT_LEVEL`, client→`NOTIFY_LOADED`,
  server→`START_LEVEL`) sent each of those responses **once**; only the client's initial
  request retried. On the LAN (unreliable UDP) a single dropped handshake packet deadlocked
  both sides with the timer paused. Fixed: each handshake command is now sent as a small
  **burst** on LAN (the receiver ignores duplicates once its state has advanced); online
  packets are already reliable so they still send one copy. Also hardened the runtime receiver
  to **skip stray setup packets** so leftover handshake copies can't be mis-applied as input.

### 2026-06-29
- **Command redundancy + faster resync to cut multiplayer desync (build 152, v1.2.1)**: two
  network robustness improvements, most valuable online (where packet loss is common) but they
  help LAN too. (1) **Command redundancy** — each runtime packet now also carries the previous
  couple of commands (with their sequence numbers); the receiver applies any command it hasn't
  seen yet, so an input lost to a dropped packet is recovered from the next packet instead of
  leaving the peer's ship stuck for a frame. (2) **Faster position resync** — the periodic
  absolute-position correction now fires every ~300 ms instead of every 1 s, so residual drift
  is pulled back sooner. First confirmed **online** game worked (via Game Center quick-match);
  invitations-by-iMessage and SharePlay only light up once the app is on the App Store, so
  quick-match is the way to test online in TestFlight.
- **Automatic LAN role election by IP (build 151, v1.2.0)**: you no longer have to start one
  device before the other. Both devices now register their Bonjour service with **auto-rename**
  (so both stay advertised — no name conflict to race over), both browse, and resolve the
  peer's address; the device with the **lower IP becomes the SERVER (Player One)**, computed
  identically on both ends so exactly one wins. Replaces the old "first to grab the name =
  server" scheme, whose simultaneous-start race produced "both Player One". On-screen DIAG
  (own IP, peer IP, elected role) is left on for one build to confirm on device. The
  auto-rename + browse mechanism was sanity-checked on the macOS CI runner via
  `tools/dnssd_probe.c`; the cross-device IP tiebreak is validated on the two devices.
- **Fixed timestep in multiplayer to cut desync (build 150, v1.1.9)**: multiplayer was using a
  *variable* wall-clock timestep (`timediff = currentTime - lastTime`) while single-player used
  a *fixed* ~16.67 ms step. Because the simulation (enemy movement, bullets, the per-frame
  survival score) integrates by `timediff`, a different dt on each device every frame made the
  two worlds drift apart — the desync seen in 2-player games. Both modes now use the fixed
  step, so the deterministic parts of the sim stay aligned. (This is "lightweight sync", not
  rollback/lockstep, so it greatly reduces desync rather than guaranteeing none; further steps
  — resending recent inputs, more frequent position resync, server-authoritative enemy state —
  remain available if needed.)
- **LAN confirmed working + 6 shared lives + cleanup (build 149, v1.1.8)**: with the fd-0 fix,
  two devices now connect over the LAN on device (start one first, then the other) and play.
  Bumped the multiplayer **shared life pool to 6** (2 players × 3) — it was initialised to 3,
  so the team only had 3 lives total instead of the expected 6. Removed the temporary
  on-screen DIAG lines now that the cause is found.
- **FIX the real LAN bug: fd 0 rejected (build 148, v1.1.7)**: on-device diagnostics (v1.1.6)
  showed `regErr=0 ifIdx=19` (registration succeeded, en0 found) but `bad sockfd=0` —
  `DNSServiceRefSockFD()` returned **file descriptor 0**, which the code rejected via a
  `socket <= 0` check. fd 0 is perfectly valid (DNS-SD returns -1 on error, not 0). Root
  cause: `NET_Free` called `close(net.udpSocket)` while `udpSocket` was still 0, **closing
  stdin (fd 0)** and freeing it, so the DNS-SD registration socket then got fd 0 — which the
  check rejected, bailing out of matchmaking. A long-latent bug, exposed when the added
  Game-Center / online-MP init shifted the process's file-descriptor layout. Fix: don't
  `close()` fd 0, and accept fd 0 as a valid socket in the register/browse checks. This is
  what actually broke the LAN; the earlier en0/blocking/once changes were red herrings for
  this symptom (though the leak fix is real and kept). DIAG lines left on for one build to
  confirm on device.
- **LAN hang diagnostics (build 147, v1.1.6)**: v1.1.5's en0 revert did NOT fix the hang on
  device, and the matchmaking code is now functionally identical to the last-working build —
  so the cause is no longer obvious by inspection. This build reverts to the original
  *re-issue-the-registration-each-call* pattern (deallocating the previous ref first, so no
  leak) and prints on-screen diagnostics on the LAN waiting screen (registration error code,
  the interface index actually used, the `select` result, and the resulting role) to pinpoint
  exactly where the handshake stalls.
- **Fix LAN "Determining player role…" hang (builds 145–146, v1.1.4 → v1.1.5)**: the v1.1.3
  resilience pass broke LAN matchmaking — the waiting screen got stuck on "Determining player
  role…". Two suspects, ruled out in order: (1) v1.1.4 restored the *blocking* DNS-SD read
  (v1.1.3 had switched to a non-blocking per-frame poll) — **did not help**, proving that
  wasn't the cause. (2) v1.1.5 found the real culprit: the registration interface had been
  changed from `en0` to `0` (all interfaces); on iOS that **stops the registration callback
  from firing**, so `net.type` was never set. Reverted registration and client-side resolve
  back to `en0` and dropped the experimental both-server demotion. **Kept** the safe wins:
  register/browse exactly once (no per-frame `DNSServiceRef` leak, the original "retry/long
  wait fails" bug) and the interface-table bounds-check. Net effect: LAN is back to its
  known-good behaviour plus the leak fix; the "advertise on all interfaces" idea is abandoned.
- **🆕 Online multiplayer over Game Center (build 144, v1.1.3)** — new feature: a second
  multiplayer mode that plays **beyond the LAN**, over the internet. A new **"Online"** button
  (Others menu) opens Apple's matchmaker (invite a friend or auto-match), and GameKit's
  **GKMatch** handles matchmaking + NAT traversal — no server to host. The key design point:
  the existing lockstep protocol is **transport-agnostic** (every message is a fixed
  `net_packet_t`), so online reuses the *exact same* handshake and command-sync that the LAN
  mode uses — only the transport is swapped (UDP/Bonjour ⇄ GKMatch) behind a small abstraction
  in `netchannel.c`. The LAN path is untouched. Roles are elected deterministically (lowest
  Game Center player id is Player One), so both ends agree without negotiation. Both players
  must be signed into Game Center. *Caveat:* lockstep over the internet is latency-sensitive,
  so expect it to feel less smooth than LAN (the per-second absolute-position update keeps the
  two ships in sync); smoothing/rollback is a later tuning pass.
- **More resilient LAN multiplayer connection (build 144, v1.1.3)**: the Bonjour/DNS-SD
  matchmaking was fragile — it failed about one time in two, needed a precise ~5 s gap
  between the two devices, and once it had failed a retry (or a long wait) would silently
  stop working. Three root causes, all fixed in `netchannel.c`:
  - **Per-frame `DNSServiceRef` leak.** `NET_Setup` runs every frame, and the old
    register/browse code blocked on a single 5-second `select` then, if the reply hadn't
    arrived, re-created the `DNSServiceRef` *the next frame* without freeing the old one —
    leaking a ref every frame until mDNSResponder gave up. This is why "if it takes too
    long" or "cut and retry" stopped working. Registration and browsing are now **issued
    once and polled non-blocking across frames** (no leak, and the menu no longer freezes
    for 5 s).
  - **`en0` hard-coding.** Both the service registration and the client-side resolve were
    locked to `en0`; on a modern iPhone the LAN link can ride a different interface
    (`awdl`/`llw`/…), so the client would *see* the server but refuse to resolve it. Now
    we advertise on **all interfaces** and resolve the server on **whatever interface it
    was discovered on** (with a bounds-check that also closes a latent buffer overflow in
    the interface table).
  - **No recovery from a "both became Player One" race.** If both devices registered the
    same service name at almost the same instant, both became server forever. Now the
    server keeps draining its registration socket while it waits, so mDNS's own
    deterministic conflict resolution reports a late `NameConflict` to exactly one device,
    which then **demotes itself to client** and joins the winner — no more manual timing.

### 2026-06-26
- **Truly freeze the world during the countdown (build 120)**: the 3-2-1-SHMUP
  countdown previously only zeroed the timestep, so the update functions still ran
  and leaked — collisions kept scoring and killing enemies, the player could fire
  or even die, and sparks/explosions/sounds kept going. Now `dEngine_HostFrame`
  skips the whole simulation/input/collision block during the countdown and only
  redraws the frozen frame; this let the earlier per-symptom guards be removed.
- **Fill the visibility-set edge gaps on tall screens (builds 121–122)**: the black
  wedges came from the baked per-frame visible-face set (computed offline for the
  original 2:3 frustum) not covering the widened view. On a stretched view we now
  draw the full mesh of any level entity still considered on-screen, so the edges
  fill in; fully off-screen entities stay culled, so far sections aren't drawn and
  the framerate is unaffected. Verified smooth on device.
- **Clear bullets on death (build 123)**: a dead ship's already-fired bullets kept
  hitting enemies for a second or two, so the score crept up through the death
  animation. `P_Die` now expires the player's in-flight bullets (standard shmup
  behaviour: your shots clear when you die).
- **Stop the survival score after death (build 126)**: the score kept ticking up on the
  GAME OVER screen — it was the passive per-frame "survival" score, added whenever the
  scene was live regardless of the player being alive. Gated it on `!autopilot.enabled`
  (autopilot stays on through respawn and game-over). Ghosts are now also expired on death.
- **🆕 Final score on the GAME OVER screen (build 133)** — new feature: the win
  (act-completed) screen already showed the score, but game-over didn't. Added a score
  line to the game-over menu, filled from `P_Die` with the player's final score.
- **Multiplayer over the Local Network permission (build 134)**: the "Network" menu entry
  is LAN peer-to-peer over Bonjour/DNS-SD, which modern iOS gates behind the Local Network
  permission. Added `NSLocalNetworkUsageDescription` + `NSBonjourServices`
  (`_DodgeServer._udp`) to the Info.plist so peer discovery is allowed again.
- **🆕 Game Center sign-in + online leaderboard (build 135)** — new feature: the app signs
  the player into Game Center (`GKLocalPlayer`) and submits the final score with the modern
  `GKLeaderboard.submitScore` to an online "High Scores" leaderboard (`shmup.highscores`).
  Game Center was enabled on the App ID + the leaderboard created via the App Store Connect
  API, and the App Store provisioning profile regenerated to carry the capability. This is
  also the **foundation for GameKit real-time (online) multiplayer**.
- **Tutorial exit + Game Center entitlement fix (build 136)**: the tutorial had no reliable
  way out (the 5-finger gesture is unreliable and it loops to the other tutorial) — added a
  top-centre **[ BACK ]** button (tutorial scenes only) that returns to the main menu. Also
  fixed Game Center not connecting on build 135: the app now actually ships the
  `com.apple.developer.game-center` entitlement (via `CODE_SIGN_ENTITLEMENTS`); the profile
  merely *allowing* it wasn't enough.
- **Leaderboard viewer + tutorial back position (build 138)**: a **"Scores"** button in the
  Others menu opens the Game Center "High Scores" leaderboard in-app
  (`GKGameCenterViewController`); and the tutorial **[ BACK ]** button was moved down off
  the score row.
- **Scores-button highlight fix + version ordering (build 139)**: the "Scores" button stayed
  stuck highlighted after closing the Game Center sheet (opening it never switches menus, so
  the press state wasn't reset) — added `MENU_ClearButtonStates()`. Also fixed TestFlight
  ordering: bumping 1.0 → 0.2 made the old 1.0 builds rank as "newer" (1.0 > 0.2), so the 34
  legacy 1.0 builds were expired, leaving 0.2 as the sole / priority line.
- **LAN multiplayer confirmed + no pause in MP (build 142, v1.1.1)**: two devices connect
  over the LAN — start one first (~5 s apart) so the Bonjour name-conflict election cleanly
  picks a server (Player One) and a client; starting both at once makes both register as
  server. The background pause / 3-2-1 countdown is now **single-player only**: freezing one
  device desynced the lockstep multiplayer game. (Versioning also moved onto the **1.1.x**
  line, above the legacy 1.0 — see Build & ship.)
- **Shared lives in multiplayer (build 143, v1.1.2)**: lives are now a **shared pool** across
  both players (the count is mirrored onto both on every death), so they run out together
  and the "both players out" game-over actually fires — previously the match never ended
  while one player still had lives, so it "didn't really stop".

### 2026-06-25
- **Full-screen fix (build 108)**: realigned the 2D overlay (player/enemy bullets,
  muzzle flash, ghost) with the 3D ship — reverted the 2D ortho scaling so ss_position
  maps to the full screen like the 3D does, and compensated glyph height so HUD text
  stays square. Bullets sit at a fixed distance in front of the ship again.
- *Known issue:* the precomputed visibility set culls some geometry too early in the
  widened view (black wedges at the bottom edge) — to be fixed next.
- **Death fire-stop (build 111)**: the ship no longer keeps firing for a moment after
  it dies — firing is gated on the respawn/autopilot state.
- **HUD anchoring (builds 110–113)**: lives counter aligned with the score under the
  iOS safe area; act-title card and menu titles (SHMUP / DIFFICULTY / level names)
  dropped below the status bar / notch so they're never clipped.
- **De-oval campaign (builds 110–117)**: with the action now filling the full tall
  screen on an unscaled 2:3 coordinate system, round 2D sprites were coming out
  vertically stretched. Introduced a single vertical-stretch factor (`gVScale`,
  mirrors `renderer.vScale`) and squashed each sprite's *vertical size only* by it,
  leaving positions aligned with the 3D scene. Fixed, in order: HUD text, lives icons,
  explosions, enemy bullets, the white spawn ring, the muzzle flash, exhaust smoke,
  the ghost weapon's ribbon, the boss charge orbs, and impact particles (sparks now
  round and spraying evenly instead of stretching upward). Circles are circles again.
- **Player 1 label (build 117)**: raised the "Player 1" pointer text so it sits just
  above its white underline (the underline had crept up onto it on tall screens).
- **🆕 Pause / resume on backgrounding + 3-2-1-SHMUP countdown (builds 118–119)** — new
  feature: leaving the app used to tear the game down to the menu. Now the in-progress game is
  frozen and kept — on resign-active the music queue is paused and the render loop is
  stopped; on become-active nothing is reset. The music resumes where it left off and,
  if a game is in progress, a centered "3 / 2 / 1 / SHMUP" overlay counts down over the
  frozen scene before handing control back. The freeze just zeroes the per-frame
  timestep, which is safe because singleplayer already runs at a fixed timestep, so no
  time jump accumulates while backgrounded. Two follow-ups: the music now uses AudioQueue
  **pause/resume** rather than stop/start (the first version drained the queue's buffers,
  giving silent music and a first-resume crash); and the SHAB/LEE hover wobble (a fixed
  per-frame nudge, not timestep-scaled) is held still during the countdown so enemies no
  longer drift while the world is paused.

### 2026-06-24
- First green build on Xcode 26 (iOS Simulator) after fixing the stale project paths,
  removed SDK APIs, and legacy-C diagnostics.
- Stood up the GitHub Actions CI (compile check + TestFlight pipeline).
- **First TestFlight build (103)** — verified running on a physical iPhone: full speed,
  sound, gameplay all working. OpenGL ES 1.1 is still alive on current iOS.
- **Full-screen pass 1 (build 104)** — viewport + 3D FOV + 2D ortho now fill modern
  tall screens with no distortion and no enemy pop-in. Verified on device.
- **Full-screen pass 2 (build 105)** — anchored the score just below the iOS safe
  area (status bar / Dynamic Island).
- **Full-screen pass 3 (build 106)** — reworked swipe controls to **1:1 finger
  tracking**: the ship now moves exactly as far as the finger, on both axes,
  independent of screen size and FOV (the previous speed-based gain drifted on a
  full-screen viewport). Virtual-pad mode unchanged. Lives-counter repositioning
  still to do.
