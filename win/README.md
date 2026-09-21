# SHMUP Reborn on Windows

The game on a PC, for playing and for testing without a TestFlight round
trip. Round 87 (2026-09-22), on Fabien's `src/backends` layout: the core is
untouched, Windows plugs in four backends.

## Build and run

    .\win\build.ps1          # -> win\build\ShmupReborn.exe (zig cc, nothing else to install)
    .\win\build.ps1 -Run     # build and launch
    .\win\build\ShmupReborn.exe [--size WxH] [--scene N] [--data <repo root>]

`zig` is the only tool (0.14 or later, on the PATH or `-Zig path`). The
executable finds the repository's `data/` by walking up from its own folder;
`--data` or the `RD` environment variable override that. Settings, the
loadout and the progress live in `%LOCALAPPDATA%\ShmupReborn\settings.cfg`.
`SHMUP_LOG_FILE=1` also writes the log there; `SHMUP_CULL_DEBUG=1` prints
the `[cull]` probe as on iOS.

The CI cross-compiles the same sources on every push (`win.yml`, zig on a
Linux runner, `win/build.sh`) and keeps the executable as an artifact.

## Controls

- **Mouse**: press and drag is the finger. In swipe mode the ship follows
  the drag; in pad mode (Others > Custom) the virtual pad is drawn and
  pressed like on the phone.
- **Keyboard**: the arrows, or WASD / ZQSD, move the ship in swipe mode (a
  synthetic finger dragging from the screen's centre at 600 px/s).
- **Escape**: back to the home menu (the five-finger touch of iOS); on the
  home menu, quits.
- The window is resizable; the engine letterboxes as on any device.

## From Visual Studio

Open the repository as a folder (File > Open > Folder). `win/vs/` holds the
two files Visual Studio reads from `.vs/` in folder mode: copy them there
once (`copy win\vs\*.json .vs\`), then **Build** on the root folder's context
menu runs `win/build.ps1`, and the debug target list offers *SHMUP Reborn*,
*acte 1 direct* and *le boss* for F5. `CppProperties.json` at the root gives
IntelliSense the include paths.

## Probes

- `--shots 5,15,30` or `SHMUP_SHOTS=5,15,30`: the frame is read back at those
  seconds after launch and written to `%LOCALAPPDATA%\ShmupReborn\shot_<s>.png`,
  the port's camera (what `simctl io screenshot` was to the Simulator smokes).
- `[wall]` lines on stdout date every scene change and print the rendered
  frame rate every five seconds: the simulation steps a fixed 16.67 ms per
  frame, so 60 fps is the contract, and the loop paces itself to it
  (`PaceFrame`, no vsync -- a 120 Hz monitor must not double the game).

## What Windows plugs in

| backend | file | replaces |
|---|---|---|
| renderer | `src/backends/gl/renderer_gl.c` | `renderer_metal.m`: OpenGL 2.1 + GLSL 1.20, the Metal file's passes line for line |
| effects | `src/backends/win/sound_waveout.c` | `sound_av.m`: a software mixer on waveOut, 8 voices |
| music | `src/backends/win/music_mci.c` | `music_av.m`: MP3 through the Media Control Interface |
| native services | `src/backends/win/native_win.c` | `dEngineAppDelegate.m` / `EAGLView.m`: PNG through WIC, settings file, language, version |
| window and input | `win/main.c` | `EAGLView.m`: Win32 window, WGL context, the mouse as the finger |

Not on Windows: Game Center (leaderboard, online matches) and the LAN mode
(Bonjour is Apple's; `netchannel.c` compiles its stubs). The core's three
Windows hooks are `GL_RENDERER` in `SCR_BindMethods` and the two
`Native_Save*` calls widened from `__APPLE__` to Windows.

## Verified on the first day (round 87)

Intel integrated graphics, OpenGL 4.6: the home screen with the rotating
ship, the brush title, the menu in the system language, the version line;
Act I from the first frame (city, arrows, enemies, bullets, HUD) and the
final act's arrival, at a measured 60 fps with the simulation clock at
wall speed after a one-second load.
