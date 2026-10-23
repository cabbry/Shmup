#!/bin/bash
# What the App Store build must not contain. testflight.yml runs this on the
# archived Shmup.app before it is exported, and again on the app inside the
# exported .ipa before it is uploaded; it runs on any folder, so it can be
# tried locally:
#
#   tools/ci/check_shipping_app.sh path/to/Shmup.app
#
# Everything a reviewer cannot see is a fault here: a test switch, a
# diagnostic probe, test data, a hidden settings row, trial code, a developer
# note, a gesture the game no longer has. The executable must not read the
# environment at all.
set -u
export LC_ALL=C		# byte-wise matching on a binary, whatever the runner's locale
APP="${1:?usage: check_shipping_app.sh path/to/Shmup.app}"
BIN="$APP/Shmup"
fail=0
bad() { echo "::error::$*"; fail=1; }

[ -f "$BIN" ] || { echo "::error::no executable at $BIN"; exit 1; }

# 1. The executable: test switches, probes, the old trial, the developer tools
#    of 2010 (time jump, input recorder, frame grabber), developer lines.
for pat in 'SHMUP_[A-Z_]+' '\(CI\)' 'disabled in prod' 'Shmup Lite' 'full version' 'Thanks for trying' \
           '\[cull\]' '\[scene\]' '\[title\]' '\[devil\]' '\[replay\]' '\[fake\]' '\[spawn\]' \
           '\[music\] t=' '\[snd\] t=' '\[boss\]' '\[arm\]' '\[players\]' '\[catchup\]' '\[cp keyframe\]' '\[rule\] t=%d fire' \
           'EnvHooks' 'ProbeTick' 'JumpInTime' 'SaveFramesToCP2Binary' 'stop recording' 'recording stopped' \
           '_t=%05d\.tga' 'audioTrack\.wav' 'SHIP c%d' 'v1 build\?' \
           'Fps: ' 'Net_Sent' 'Poly Count' 'Texture Switches' \
           'Setup cmd' 'state=NET_' 'LAST SENT' 'My IP' 'Others > Network' 'MEMORY LEAK' 'shmup_log.txt'; do
	hits=$(grep -a -o -E "$pat" "$BIN" | sort -u | head -5)
	[ -n "$hits" ] && bad "the executable contains '$pat': $(echo $hits)"
done

# 2. No environment variable is read: the import table has no getenv/setenv.
#    On the runner nm must be there: a check that silently did not run is not
#    a pass.
if command -v nm >/dev/null 2>&1; then
	imports=$(nm -u "$BIN" 2>/dev/null | grep -E '(getenv|setenv|_NSGetEnviron)$' || true)
	[ -n "$imports" ] && bad "the executable imports $(echo $imports)"
elif [ -n "${CI:-}" ]; then
	bad "no nm on this runner: the import check could not run"
else
	echo "(no nm here: import check skipped -- it runs on the CI runner)"
fi

# 3. Anywhere in the app: test data, switch names, settings rows hidden by an
#    invented specifier type, a gesture or a line of text the game no longer has.
for pat in 'SHMUP_[A-Z_]+' 'test_rules' 'Rules_bench' 'Disabled[A-Za-z]*Specifier' 'reachable only by' \
           'five_fingers' 'Hello_this_is_an_other_string' 'To_Be_Continued'; do
	files=$(grep -r -a -l -E "$pat" "$APP" 2>/dev/null | head -5)
	[ -n "$files" ] && bad "'$pat' found in: $(echo $files)"
done

# 3b. The game's text files carry no developer notes about hidden things or
#     test runs. (Not the executable: UIKit's own selectors say "Hidden".)
if [ -d "$APP/data" ]; then
	files=$(grep -r -l -i -E 'hidden|simulator|cheat' "$APP/data" \
	        --include='*.scene' --include='*.cfg' --include='*.mtl' --include='*.lbr' --include='*.map' 2>/dev/null | head -5)
	[ -n "$files" ] && bad "a developer note in the game's data: $(echo $files)"
fi

# 4. Files that have no place in the app.
for f in Settings.bundle Entitlements.plist; do
	[ -e "$APP/$f" ] && bad "$f is inside the app"
done

# 5. No trial switch left in Info.plist, and no way in that the review notes do
#    not describe: no URL scheme, no file sharing.
if [ -x /usr/libexec/PlistBuddy ]; then
	for key in License CFBundleURLTypes UIFileSharingEnabled LSSupportsOpeningDocumentsInPlace UIApplicationShortcutItems; do
		/usr/libexec/PlistBuddy -c "Print :$key" "$APP/Info.plist" >/dev/null 2>&1 && bad "Info.plist has a $key key"
	done
elif [ -f "$APP/Info.plist" ]; then
	for key in License CFBundleURLTypes UIFileSharingEnabled LSSupportsOpeningDocumentsInPlace UIApplicationShortcutItems; do
		grep -a -q "$key" "$APP/Info.plist" && bad "Info.plist has a $key key"
	done
fi

[ $fail -eq 0 ] && echo "clean: nothing hidden, nothing for testing, in $(basename "$APP")"
exit $fail
