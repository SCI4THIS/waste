#!/usr/bin/env bash
# Materialize the four terminal descriptions; fail rather than publish a stub.
set -euo pipefail
NCURSES_SOURCE="$1"
NCURSES_CONFIGURE="$2"
TIC="$3"
INFOCMP="$4"
cd "$NCURSES_CONFIGURE"
OUTPUT="ncurses/fallback.c"
TEMPORARY="$(mktemp ncurses/fallback-XXXXXX.c)"
trap 'rm -f -- "$TEMPORARY"' EXIT
bash -e -o pipefail "$NCURSES_SOURCE/ncurses/tinfo/MKfallback.sh" /usr/share/terminfo \
  "$NCURSES_SOURCE/misc/terminfo.src" "$TIC" "$INFOCMP" \
  xterm xterm-256color vt100 dumb > "$TEMPORARY"
grep -q 'static const TERMTYPE2 fallbacks\[4\]' "$TEMPORARY"
grep -q 'count total Booleans' "$TEMPORARY"
mv -- "$TEMPORARY" "$OUTPUT"
