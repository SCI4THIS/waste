#!/usr/bin/env bash
# All upstream inputs remain read-only; configure only the staged copy.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
NCURSES_BUILD="$(realpath -m -- "$1")"
case "$NCURSES_BUILD" in
  "$REPO_ROOT/build/"*) ;;
  *) printf 'error: ncurses outputs must stay under build/\n' >&2; exit 1 ;;
esac
mkdir -p "$NCURSES_BUILD"
STAGE="$(mktemp -d "$NCURSES_BUILD/source-stage-XXXXXX")"
trap 'rm -rf -- "$STAGE"' EXIT
tar -C "$REPO_ROOT/submodules/ncurses" --exclude=.git -cf - . |
  tar -C "$STAGE" -xf -
chmod -R u+w "$STAGE"
rm -rf -- "$NCURSES_BUILD/source" "$NCURSES_BUILD/build"
mv -- "$STAGE" "$NCURSES_BUILD/source"
