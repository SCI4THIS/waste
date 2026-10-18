#!/usr/bin/env bash
# Stage only COFF sources; all guest adaptations are repository-owned patches.
set -euo pipefail
PACKAGE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$PACKAGE_DIR/../../.." && pwd)"
LLVM_SOURCE="$(realpath -- "$1")"
PACKAGE_BUILD="$(realpath -m -- "$2")"
case "$PACKAGE_BUILD" in
  "$REPO_ROOT/build/"*) ;;
  *) printf 'error: lld-link outputs must stay under build/\n' >&2; exit 1 ;;
esac
mkdir -p "$PACKAGE_BUILD"
STAGE="$(mktemp -d "$PACKAGE_BUILD/source-stage-XXXXXX")"
trap 'rm -rf -- "$STAGE"' EXIT
cp -a -- "$LLVM_SOURCE/lld/COFF/." "$STAGE/"
chmod -R u+w "$STAGE"
patch --batch --forward --fuzz=0 -d "$STAGE" -p1 < "$PACKAGE_DIR/coff-waste.patch"
rm -rf -- "$PACKAGE_BUILD/source"
mv -- "$STAGE" "$PACKAGE_BUILD/source"
trap - EXIT
