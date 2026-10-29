#!/usr/bin/env bash
# Read pinned source objects without changing either submodule's checkout.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
M4_BUILD="$(realpath -m -- "$1")"
case "$M4_BUILD" in
  "$REPO_ROOT/build/"*) ;;
  *) echo 'error: M4 outputs must stay under build/' >&2; exit 1 ;;
esac
M4_REVISION="$(cat "$SCRIPT_DIR/source-revision")"
GNULIB_REVISION="$(cat "$SCRIPT_DIR/gnulib-revision")"
M4_UPSTREAM="${M4_SOURCE_REPO:-$REPO_ROOT/submodules/m4}"
GNULIB_UPSTREAM="${M4_GNULIB_SOURCE_REPO:-$REPO_ROOT/submodules/coreutils/gnulib}"
mkdir -p "$M4_BUILD"
STAGE="$(mktemp -d "$M4_BUILD/source-stage-XXXXXX")"
trap 'rm -rf -- "$STAGE"' EXIT
bash "$SCRIPT_DIR/archive-source.sh" "$M4_UPSTREAM" "$M4_REVISION" "$STAGE"
mkdir -p "$STAGE/gnulib"
bash "$SCRIPT_DIR/archive-source.sh" "$GNULIB_UPSTREAM" "$GNULIB_REVISION" "$STAGE/gnulib"
chmod -R u+w "$STAGE"
if [[ -s "$SCRIPT_DIR/waste.patch" ]]; then
  patch --batch --forward --fuzz=0 -d "$STAGE" -p1 < "$SCRIPT_DIR/waste.patch"
fi
printf '1.4.21\n' > "$STAGE/.tarball-version"
printf '%s\n' "$M4_REVISION" > "$STAGE/.waste-source-revision"
printf '%s\n' "$GNULIB_REVISION" > "$STAGE/gnulib/.waste-source-revision"
bash "$SCRIPT_DIR/bootstrap.sh" "$STAGE"
rm -rf -- "$M4_BUILD/source" "$M4_BUILD/configure"
mv -- "$STAGE" "$M4_BUILD/source"
