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

Take them from the build being submitted, as a player gets it -- TestFlight on
a device, or the Simulator with no test switch. A capture from a test build
(autofire, a jump to a scene) or from an older version is not the app as it
runs: the HUD's pause button, the version on the home menu give it away.

`APP_IPAD_PRO_3GEN_129` is the 13-inch iPad set (2048 x 2732), required as
the app runs on iPad.

The menus follow the device language, English or French: a capture of a menu
belongs in its locale's folder. Captures of the action can be shared.
