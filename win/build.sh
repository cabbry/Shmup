#!/usr/bin/env bash
# Builds SHMUP Reborn for Windows with zig cc, from any host (the CI runs
# this on Linux, cross-compiling; Git Bash on Windows works too). The
# PowerShell twin, build.ps1, is the everyday one on a PC.
#
#   win/build.sh                 -> win/build/ShmupReborn.exe
#   ZIG=path/to/zig win/build.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
zig="${ZIG:-zig}"
out="$root/win/build"
mkdir -p "$out"

version="$(sed -nE '/CFBundleShortVersionString/{n;s/.*<string>([^<]+)<\/string>.*/\1/p}' "$root/ios/shmup.plist")"
printf '#define SHMUP_VERSION "%s"\n' "${version:-dev}" > "$out/shmup_version.h"

sources=( "$root"/src/core/*.c "$root/src/backends/posix/filesystem.c" "$root/src/backends/gl/renderer_gl.c" "$root"/src/backends/win/*.c "$root/win/main.c" )
flags=( -target x86_64-windows-gnu -std=gnu99 -O2 -DWIN32 -D_CRT_SECURE_NO_WARNINGS -include "$out/shmup_version.h"
        -iquote "$root/src/core" -iquote "$root/src/backends/gl" -iquote "$root/src/backends/win"
        -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable
        -Wno-unknown-pragmas -Wno-missing-braces -Wno-sign-compare -Wno-format -Wno-parentheses
        -Wno-unused-value -Wno-misleading-indentation -Wno-int-conversion
        -fno-sanitize=undefined -fno-strict-aliasing )
libs=( -liphlpapi -lopengl32 -lgdi32 -luser32 -lwinmm -lole32 -lwindowscodecs -lws2_32 -lshell32 -ladvapi32 )

echo "SHMUP Reborn ${version:-dev} -- ${#sources[@]} files, $zig cc (x86_64-windows-gnu)"
"$zig" cc "${flags[@]}" "${sources[@]}" "${libs[@]}" -o "$out/ShmupReborn.exe"
echo "built $out/ShmupReborn.exe"
