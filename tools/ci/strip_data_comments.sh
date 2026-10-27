#!/usr/bin/env bash
# Strip the developer comments from the game's text data, in place.
#
#   tools/ci/strip_data_comments.sh <data dir>
#
# The scenes, the configs and the material files are commented for whoever
# edits them -- design notes, history, the tester's specs. Those notes are for
# the repository, not for the App Store bundle: testflight.yml runs this on the
# checkout just before the archive, and check_shipping_app.sh then refuses an
# app whose data still has a comment in it.
#
# It removes exactly what the game's lexer skips (src/core/lexer.c,
# LE_skipWhiteSpace): a '#' or a '//' that starts a token runs to the end of
# the line, a '/*' that starts a token runs to the next '*/'. A token starts
# after one of the lexer's white characters -- space, tab, CR, LF, '(', ')',
# '*', ':', ';' -- and a token that opens with '"' is a string, read to the
# next '"' whatever it holds. So the game reads the same tokens before and
# after; a line that held only a comment is dropped.
set -euo pipefail
export LC_ALL=C      # bytes, not characters: the French text is cp1252, not UTF-8
dir="${1:?usage: strip_data_comments.sh <data dir>}"
[ -d "$dir" ] || { echo "no such directory: $dir" >&2; exit 2; }

count=0
while IFS= read -r -d '' f; do
  awk '
    BEGIN { inBlock = 0; inQuote = 0 }
    {
      line = $0; out = ""; n = length(line); i = 1; ws = 1; cut = inBlock
      while (i <= n) {
        c = substr(line, i, 1)
        if (inBlock) {
          if (c == "*" && substr(line, i + 1, 1) == "/") { inBlock = 0; i += 2; ws = 1; cut = 1; continue }
          i++; continue
        }
        if (inQuote) {
          out = out c; if (c == "\"") inQuote = 0
          i++; continue
        }
        if (ws) {
          if (c == "#") { cut = 1; break }
          if (c == "/" && substr(line, i + 1, 1) == "/") { cut = 1; break }
          if (c == "/" && substr(line, i + 1, 1) == "*") { inBlock = 1; cut = 1; i += 2; continue }
          if (c == "\"") { inQuote = 1; out = out c; ws = 0; i++; continue }
        }
        out = out c
        ws = (index(" \t\r():;*", c) > 0)
        i++
      }
      if (cut) {
        sub(/[ \t\r]+$/, "", out)
        if (out ~ /^[ \t]*$/) next      # the line held only a comment
      }
      print out
    }
  ' "$f" > "$f.stripped"
  # awk ends every line with a newline. A file that had none must keep it so:
  # the lexer never reads a file's last byte, and tutorial.mtl has read its
  # final "illum 10" as "illum 1" since 2010 -- the game sees what it always saw.
  if [ -n "$(tail -c 1 "$f")" ]; then perl -pi -e 'chomp if eof' "$f.stripped"; fi
  mv "$f.stripped" "$f"
  count=$((count + 1))
done < <(find "$dir" -type f \( -name '*.scene' -o -name '*.cfg' -o -name '*.mtl' -o -name '*.lbr' -o -name '*.map' \) -print0)
echo "stripped the developer comments from $count text data files in $dir"
