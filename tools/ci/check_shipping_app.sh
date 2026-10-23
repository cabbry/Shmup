#!/bin/bash
# What the App Store build must not contain. testflight.yml runs this on the
# archived Shmup.app before it is exported or uploaded; it runs on any folder,
# so it can be tried locally:
#
#   tools/ci/check_shipping_app.sh path/to/Shmup.app
#
# Everything a reviewer cannot see is a fault here: a test switch, a
# diagnostic probe, test data, a hidden settings row, trial code, a developer
# note. The executable must not read the environment at all.
set -u
APP="${1:?usage: check_shipping_app.sh path/to/Shmup.app}"
BIN="$APP/Shmup"
fail=0
bad() { echo "::error::$*"; fail=1; }

[ -f "$BIN" ] || { echo "::error::no executable at $BIN"; exit 1; }

# 1. The executable: test switches, probes, the old trial, developer lines.
for pat in 'SHMUP_[A-Z_]+' '\(CI\)' 'disabled in prod' 'Shmup Lite' 'full version' 'Thanks for trying' \
           '\[cull\]' '\[scene\]' '\[title\]' '\[devil\]' '\[replay\]' '\[fake\]' '\[spawn\]' \
           'Fps: ' 'Net_Sent' 'Poly Count' 'Texture Switches' \
           'Setup cmd' 'state=NET_' 'LAST SENT' 'My IP' 'Others > Network' 'MEMORY LEAK' 'shmup_log.txt'; do
	hits=$(grep -a -o -E "$pat" "$BIN" | sort -u | head -5)
	[ -n "$hits" ] && bad "the executable contains '$pat': $(echo $hits)"
done

# 2. No environment variable is read: the import table has no getenv/setenv.
if command -v nm >/dev/null 2>&1; then
	imports=$(nm -u "$BIN" 2>/dev/null | grep -E '(getenv|setenv)$' || true)
	[ -n "$imports" ] && bad "the executable imports $(echo $imports)"
else
	echo "(no nm here: import check skipped)"
fi

# 3. Anywhere in the app: test data, switch names, settings rows hidden by an
#    invented specifier type.
for pat in 'SHMUP_[A-Z_]+' 'test_rules' 'Rules_bench' 'Disabled[A-Za-z]*Specifier' 'reachable only by'; do
	files=$(grep -r -a -l -E "$pat" "$APP" 2>/dev/null | head -5)
	[ -n "$files" ] && bad "'$pat' found in: $(echo $files)"
done

# 4. Files that have no place in the app.
for f in Settings.bundle Entitlements.plist; do
	[ -e "$APP/$f" ] && bad "$f is inside the app"
done

# 5. No trial switch left in Info.plist.
if [ -x /usr/libexec/PlistBuddy ]; then
	/usr/libexec/PlistBuddy -c "Print :License" "$APP/Info.plist" >/dev/null 2>&1 && bad "Info.plist has a License key"
elif [ -f "$APP/Info.plist" ] && grep -a -q 'License' "$APP/Info.plist"; then
	bad "Info.plist has a License key"
fi

[ $fail -eq 0 ] && echo "clean: nothing hidden, nothing for testing, in $(basename "$APP")"
exit $fail
