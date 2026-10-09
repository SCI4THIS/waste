#!/usr/bin/env bash
# Bundle the source and build inputs corresponding to the built guest binary.
set -euo pipefail
REPO_ROOT="$(realpath -- "$1")"
BASH_BUILD="$(realpath -- "$2")"
case "$BASH_BUILD" in
  "$REPO_ROOT/build/"*) ;;
  *) printf 'error: source packages must stay under build/\n' >&2; exit 1 ;;
esac
PACKAGE_STAGE="$(mktemp -d "$BASH_BUILD/source-package-XXXXXX")"
trap 'chmod -R u+w "$PACKAGE_STAGE"; rm -rf -- "$PACKAGE_STAGE"' EXIT
# Include pristine upstream inputs; the build reapplies the owned patch.
mkdir -p "$PACKAGE_STAGE/bash-source" "$PACKAGE_STAGE/waste"
tar -C "$REPO_ROOT/submodules/bash" --exclude=.git -cf - . |
  tar -C "$PACKAGE_STAGE/bash-source" -xf -
chmod -R u+w "$PACKAGE_STAGE/bash-source"
for INPUT in start.sh .gitmodules src/config.h src/aux src/engine src/cli-rt src/html-rt src/system-tests \
  src/vfs/usr/include src/vfs/usr/lib/waste/cc/include src/vfs/usr/share/licenses/clang \
  tests docs/architecture.md docs/techniques.md docs/guest-sdk.md; do
  mkdir -p "$PACKAGE_STAGE/waste/$(dirname -- "$INPUT")"
  cp -a "$REPO_ROOT/$INPUT" "$PACKAGE_STAGE/waste/$INPUT"
done
mkdir -p "$PACKAGE_STAGE/waste/build/aux/bash/configure"
for INPUT in libc-config.site configure/config.h configure/config.log; do
  cp "$BASH_BUILD/$INPUT" "$PACKAGE_STAGE/waste/build/aux/bash/$INPUT"
done
REVISION="$(git -C "$REPO_ROOT/submodules/bash" rev-parse HEAD 2>/dev/null || cat "$REPO_ROOT/submodules/bash/.waste-source-revision")"
printf '%s\n' "$REVISION" > "$PACKAGE_STAGE/bash-source/.waste-source-revision"
HASH="$(sha256sum "$BASH_BUILD/bash.wasm" | cut -d ' ' -f1)"
printf '{"format":1,"source_commit":"%s","binary_sha256":"%s","instructions":"waste/src/aux/bash/README.md"}\n' \
  "$REVISION" "$HASH" > "$PACKAGE_STAGE/SOURCE-BUNDLE.json"
tar --sort=name --mtime='UTC 1970-01-01' --owner=0 --group=0 --numeric-owner \
  --exclude=.git --exclude=__pycache__ --exclude='*.pyc' --exclude='*.wasm' \
  --exclude='*.o' --exclude='*.a' -cf - -C "$PACKAGE_STAGE" . |
  gzip -n > "$BASH_BUILD/bash-corresponding-source.tar.gz"
printf 'Bash corresponding source: %s\n' "$BASH_BUILD/bash-corresponding-source.tar.gz"
sha256sum "$BASH_BUILD/bash-corresponding-source.tar.gz"
