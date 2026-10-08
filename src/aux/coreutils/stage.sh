#!/usr/bin/env bash
# Stage read-only upstream inputs; patch only the writable build copy.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
COREUTILS_BUILD="$(realpath -m -- "$1")"
case "$COREUTILS_BUILD" in
  "$REPO_ROOT/build/"*) ;;
  *) printf 'error: Coreutils outputs must stay under build/\n' >&2; exit 1 ;;
esac
mkdir -p "$COREUTILS_BUILD"
STAGE="$(mktemp -d "$COREUTILS_BUILD/source-stage-XXXXXX")"
trap 'rm -rf -- "$STAGE"' EXIT
tar -C "$REPO_ROOT/submodules/coreutils" --exclude=.git -cf - . |
  tar -C "$STAGE" -xf -
chmod -R u+w "$STAGE"
patch --batch --forward --fuzz=0 -d "$STAGE" -p1 -i "$SCRIPT_DIR/coreutils-waste.patch"
rm -rf -- "$COREUTILS_BUILD/source" "$COREUTILS_BUILD/configure"
rm -f -- "$COREUTILS_BUILD/.objects-built" "$COREUTILS_BUILD/libcoreutils-runtime.a"
mv -- "$STAGE" "$COREUTILS_BUILD/source"
mkdir -p "$COREUTILS_BUILD/include"
for HEADER in obstack unistr; do
  cp "$COREUTILS_BUILD/source/gnulib/lib/$HEADER.in.h" "$COREUTILS_BUILD/include/$HEADER.h"
done
git -C "$REPO_ROOT/submodules/coreutils" rev-parse HEAD > "$COREUTILS_BUILD/source-revision.txt"
sha256sum "$SCRIPT_DIR/coreutils-waste.patch" > "$COREUTILS_BUILD/patch-sha256.txt"
printf '{"format":2,"source":{"path":"submodules/coreutils","commit":"%s"},"gnulib_commit":"%s","managed_patch":{"path":"src/aux/coreutils/coreutils-waste.patch","sha256":"%s"}}\n' \
  "$(cat "$COREUTILS_BUILD/source-revision.txt")" \
  "$(git -C "$REPO_ROOT/submodules/coreutils/gnulib" rev-parse HEAD)" \
  "$(cut -d ' ' -f1 "$COREUTILS_BUILD/patch-sha256.txt")" > "$COREUTILS_BUILD/coreutils-provenance.json"
