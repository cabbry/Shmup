# Builds SHMUP Reborn for Windows with zig cc (no Visual Studio, no SDK beyond
# what zig ships). Output: win\build\ShmupReborn.exe. Run it from anywhere: it
# finds the repository's data/ folder by walking up from its own location.
#
#   .\win\build.ps1            release build
#   .\win\build.ps1 -Debug     -O0 -g, asserts on
#   .\win\build.ps1 -Run       build, then launch
param(
    [switch]$Debug,
    [switch]$Run,
    [string]$Zig = "zig",
    [string]$OutName = "ShmupReborn.exe"   # another name when the usual one is running
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

# The marketing version, from the iOS plist (the one place it is kept).
$plist = Get-Content "$root\ios\shmup.plist" -Raw
$version = "dev"
if ($plist -match '<key>CFBundleShortVersionString</key>\s*<string>([^<]+)</string>') { $version = $Matches[1] }

$sources = @()
$sources += Get-ChildItem "$root\src\core\*.c" | ForEach-Object { $_.FullName }
$sources += "$root\src\backends\posix\filesystem.c"
$sources += "$root\src\backends\gl\renderer_gl.c"
$sources += Get-ChildItem "$root\src\backends\win\*.c" | ForEach-Object { $_.FullName }
$sources += "$root\win\main.c"

$out = "$root\win\build"
New-Item -ItemType Directory -Force $out | Out-Null
# The version reaches the C code through a generated header: a -D with quotes
# does not survive PowerShell's argument passing.
Set-Content -Path "$out\shmup_version.h" -Value "#define SHMUP_VERSION `"$version`"" -Encoding ascii

$flags = @(
    "-std=gnu99", "-DWIN32", "-D_CRT_SECURE_NO_WARNINGS", "-include", "$out\shmup_version.h",
    # -iquote, not -I: src/core/math.h must not shadow the system <math.h>
    "-iquote", "$root\src\core", "-iquote", "$root\src\backends\gl", "-iquote", "$root\src\backends\win",
    "-Wall", "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-unused-function", "-Wno-unused-but-set-variable",
    "-Wno-unknown-pragmas", "-Wno-missing-braces", "-Wno-sign-compare", "-Wno-format", "-Wno-parentheses",
    "-Wno-unused-value", "-Wno-misleading-indentation", "-Wno-int-conversion",
    "-fno-sanitize=undefined", "-fno-strict-aliasing"
)
if ($Debug) { $flags += @("-O0", "-g") } else { $flags += @("-O2") }
$libs = @("-liphlpapi", "-lopengl32", "-lgdi32", "-luser32", "-lwinmm", "-lole32", "-lwindowscodecs", "-lws2_32", "-lshell32", "-ladvapi32")

Write-Host "SHMUP Reborn $version -- $($sources.Count) files, zig cc"
& $Zig cc @flags @sources @libs -o "$out\$OutName"
if ($LASTEXITCODE -ne 0) { throw "build failed" }
Write-Host "built $out\$OutName"

if ($Run) { & "$out\$OutName" }
