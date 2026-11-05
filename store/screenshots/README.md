# Screenshots

One folder per locale, then one per Apple display type; the push replaces the
whole set of a folder it finds, and leaves alone the ones it does not.

    store/screenshots/<locale>/<DISPLAY_TYPE>/*.png

Files go up in name order, ten at most. Name them `01-...png`, `02-...png`.

## The one set App Store Connect requires

`APP_IPHONE_67` — the 6.7 and 6.9 inch iPhones, and the only mandatory set:
Apple reuses it for every smaller iPhone.

| what | value |
| --- | --- |
| size | 1290 x 2796 or 1320 x 2868, portrait |
| format | PNG, no transparency |
| count | 3 to 10 |

A screenshot taken on an iPhone 15 Pro Max or 16 Pro Max is already the right
size: side button and volume up, then AirDrop the PNG. No status bar to hide,
the game runs full screen.

## What to photograph

The order is the order people see, and the first one is the one they judge:

1. **Act I from behind the ship**, enemies on screen and bullets flying.
2. **The final boss**, whole, its arms deployed.
3. **Act III seen from the side**, the beat that reads unlike anything else.
4. **The home menu**, for the title and the ship.
5. **A two-player match**, the second ship visible, if there is a good one.

Avoid: a pause or a menu overlay on top of the action, a game over screen,
an almost empty frame. Nothing may be added around the picture, no frame, no
caption: Apple asks for the app as it runs.

Take them from the build being submitted: TestFlight on a device, or
`shots.yml` in the Simulator on the same commit, with `version` set to the
submitted one. Nobody touches the Simulator's screen, so the action frames
use the test switches that play unattended -- `pack` jumps to the act,
`SHMUP_AUTOFIRE=1 SHMUP_INVULN=1 SHMUP_LIVES=3` in `env` -- and none of them
draws anything: the HUD, the font, the pause button are the player's. The one
difference on screen is the ship staying where it starts. Without
`SHMUP_LIVES` the HUD reads "x0", a last life. What gives an old capture away
is the pause button and the version on the home menu; both must match the
build. The Intel Simulator runs the game far slower than a phone and takes
each frame in about thirty seconds, so ask for plenty (`seconds`, every 30)
and pick.

The 5.0.19 set came that way (iPhone 16 Pro Max and iPad Pro 13-inch
Simulators, 2026-11-05); the multiplayer shot cannot be taken there.

`APP_IPAD_PRO_3GEN_129` is the 13-inch iPad set (2048 x 2732), required as
the app runs on iPad.

The menus follow the device language, English or French: a capture of a menu
belongs in its locale's folder. Captures of the action can be shared.
