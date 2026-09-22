# SHMUP Reborn on Android

The Gradle project (`net.fabiensanglard.shmup`, a NativeActivity) builds the
same core as iOS and Windows, on the OpenGL backend's GL ES 2 flavour
(`src/backends/gl/renderer_gl.c`, `GLR_ES2`), with the Android glue of
`src/backends/android`: EGL, OpenSL ES for effects and music, libpng, the
asset manager for the data. Round 90 (2026-09-22).

## Build

The CI builds the debug APK on every push (`android.yml`, a Linux runner
with the SDK) and keeps it as the `shmup-android-debug` artifact. Locally:

    android/gradlew assembleDebug        # JDK 17, the SDK with NDK 26.3.11579264 and CMake 3.22.1

`android/local.properties` (`sdk.dir=...`) points Gradle at the SDK; it is
not committed. On one PC Gradle could not run at all -- its daemon needs a
Java NIO pipe, a loopback socket, and something on that machine refuses
`connect` on the loopback (Docker's or the VPN's network filter): the CI
APK was the way.

## Run on the emulator

    sdkmanager "system-images;android-34;google_apis;x86_64" emulator platform-tools
    avdmanager create avd -n shmup -k "system-images;android-34;google_apis;x86_64" -d pixel_6
    emulator -avd shmup -gpu host &
    adb install app-debug.apk           # uninstall first when the APK comes from another machine:
                                        # every CI run signs with a fresh debug key
    adb shell am start -n net.fabiensanglard.shmup/.Launcher
    adb exec-out screencap -p > shot.png
    adb logcat | grep net.fabiensanglard

## What Android plugs in, and what it lacks

| piece | file |
|---|---|
| renderer | `src/backends/gl/renderer_gl.c` (ES 2: direct entry points, GLSL ES shaders, no fixed pipeline) |
| window, input, loop | `src/backends/android/android_main.c`, `android_display.c` (EGL, ES 2 context, 16-bit depth) |
| effects and music | `android_music.c` (OpenSL ES) |
| files | `android_filesystem.c` (the APK's assets; `data/` is the assets folder) |
| PNG, settings, language, version | `android_native.c` (libpng to premultiplied RGBA; a settings file under internal storage; `AConfiguration_getLanguage`; the version from `ios/shmup.plist` through CMake) |

No Game Center, no LAN (Bonjour is Apple's; the Windows mDNS responder
could be ported the same way). Touch is the finger, as on iOS.
