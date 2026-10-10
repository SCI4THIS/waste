#!/usr/bin/env bash
# Stage upstream Vim sources without writing into the read-only submodule.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
VIM_BUILD="$(realpath -m -- "$1")"
case "$VIM_BUILD" in
  "$REPO_ROOT/build/"*) ;;
  *) printf 'error: Vim outputs must stay under build/\n' >&2; exit 1 ;;
esac
mkdir -p "$VIM_BUILD"
STAGE="$(mktemp -d "$VIM_BUILD/source-stage-XXXXXX")"
trap 'rm -rf -- "$STAGE"' EXIT
tar -C "$REPO_ROOT/submodules/vim" --exclude=.git -cf - . |
  tar -C "$STAGE" -xf -
chmod -R u+w "$STAGE"
if [[ -s "$SCRIPT_DIR/vim-waste.patch" ]]; then
  patch --batch --forward --fuzz=0 -d "$STAGE" -p1 < "$SCRIPT_DIR/vim-waste.patch"
fi
rm -rf -- "$VIM_BUILD/source" "$VIM_BUILD/configure"
mv -- "$STAGE" "$VIM_BUILD/source"
