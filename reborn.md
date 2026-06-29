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
  internet (not just the LAN); Apple handles matchmaking and NAT traversal. *(Awaiting
  2-device validation.)*

## Known issues

- Minor: backgrounding the app on the GAME OVER screen still shows the
  3-2-1-SHMUP overlay (cosmetic).
- Minor/latent: the menu titles' safe-area offset is computed once at init,
  before the inset is known, so it stays inactive — the titles clear the notch
  via fixed margins today but wouldn't auto-adapt to a larger inset.

## Roadmap

- Polish & smaller features: ship + bullet-colour selection before a run.
- Optional deeper modernization: ARC migration, 64-bit audit, `AVAudioSession`, and
  clearing the ~600 deprecation warnings (then re-enabling the strict clang flags).
- A new level (study `data/scenes`, the `event` system, the on-rails `cameraPath`, and
  the preprocessor), reusing the existing assets.
- **TTB system** (homage to the manga *Tokyo Toy Box*): a scripted perspective shift
  mid-level — the camera swinging into a close 3/4 / side view and back at preprogrammed
  points — authored as a beat in the new level, with the camera path and decor built for
  it. Made possible by the game being real 3D.

---

## Changelog

### 2026-06-29
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
