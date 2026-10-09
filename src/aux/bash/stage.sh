#!/usr/bin/env bash
# Stage upstream inputs without writing into the read-only submodule.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
BASH_BUILD="$(realpath -m -- "$1")"
case "$BASH_BUILD" in
  "$REPO_ROOT/build/"*) ;;
  *) printf 'error: Bash outputs must stay under build/\n' >&2; exit 1 ;;
esac
mkdir -p "$BASH_BUILD"
STAGE="$(mktemp -d "$BASH_BUILD/source-stage-XXXXXX")"
trap 'rm -rf -- "$STAGE"' EXIT
tar -C "$REPO_ROOT/submodules/bash" --exclude=.git -cf - . |
  tar -C "$STAGE" -xf -
chmod -R u+w "$STAGE"
patch --batch --forward --fuzz=0 -d "$STAGE" -p1 < "$SCRIPT_DIR/bash-waste.patch"
rm -rf -- "$BASH_BUILD/source" "$BASH_BUILD/configure"
mv -- "$STAGE" "$BASH_BUILD/source"
