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
  `git tag v0.1.x && git push <fork> v0.1.x`.
- App: bundle id `com.cabbry.shmup`, store name **"SHMUP Reborn"**.

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
- 🔄 Full-screen: first pass shipped; HUD/controls edge-anchoring next.

## Roadmap

- Full-screen pass 2: HUD/score/controls anchored to the real edges; touch mapping.
- Optional deeper modernization: ARC migration, 64-bit audit, `AVAudioSession`, and
  clearing the ~600 deprecation warnings (then re-enabling the strict clang flags).
- A new level (study `data/scenes`, the `event` system, the on-rails `cameraPath`, and
  the preprocessor), reusing the existing assets.

---

## Changelog

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
