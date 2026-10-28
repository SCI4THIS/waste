#!/usr/bin/env bash
# Never configure, regenerate parsers, or compile inside the upstream checkout.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
AWK_BUILD="$(realpath -m -- "$1")"
case "$AWK_BUILD" in
  "$REPO_ROOT/build/"*) ;;
  *) printf 'error: Awk outputs must stay under build/\n' >&2; exit 1 ;;
esac
test -f "$REPO_ROOT/submodules/gawk/configure" || {
  echo 'error: initialize submodules/gawk before building awk' >&2; exit 1;
}
mkdir -p "$AWK_BUILD"
STAGE="$(mktemp -d "$AWK_BUILD/source-stage-XXXXXX")"
trap 'rm -rf -- "$STAGE"' EXIT
tar -C "$REPO_ROOT/submodules/gawk" --exclude=.git -cf - . | tar -C "$STAGE" -xf -
chmod -R u+w "$STAGE"
patch --batch --forward --fuzz=0 -d "$STAGE" -p1 < "$SCRIPT_DIR/waste.patch"
# Use the checked-in parser outputs and Autotools files without host generators.
touch "$STAGE/aclocal.m4" "$STAGE/configure" "$STAGE/configh.in"
find "$STAGE" -name Makefile.in -exec touch {} +
touch "$STAGE/awkgram.c" "$STAGE/command.c"
rm -rf -- "$AWK_BUILD/source" "$AWK_BUILD/configure"
mv -- "$STAGE" "$AWK_BUILD/source"
