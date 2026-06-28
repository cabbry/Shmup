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
  submitted online; also the foundation for online (GameKit) multiplayer.

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
