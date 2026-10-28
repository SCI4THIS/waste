#!/usr/bin/env bash
# Bundle the source and build inputs corresponding to the built guest binary.
set -euo pipefail
REPO_ROOT="$(realpath -- "$1")"
AWK_BUILD="$(realpath -- "$2")"
case "$AWK_BUILD" in
  "$REPO_ROOT/build/"*) ;;
  *) printf 'error: source packages must stay under build/\n' >&2; exit 1 ;;
esac
PACKAGE_STAGE="$(mktemp -d "$AWK_BUILD/source-package-XXXXXX")"
trap 'chmod -R u+w "$PACKAGE_STAGE"; rm -rf -- "$PACKAGE_STAGE"' EXIT
# Include pristine upstream inputs; the build reapplies the owned patch.
mkdir -p "$PACKAGE_STAGE/waste/submodules/gawk"
tar -C "$REPO_ROOT/submodules/gawk" --exclude=.git -cf - . |
  tar -C "$PACKAGE_STAGE/waste/submodules/gawk" -xf -
chmod -R u+w "$PACKAGE_STAGE/waste/submodules/gawk"
for INPUT in start.sh .gitmodules src/config.h src/aux src/engine src/cli-rt src/html-rt \
  src/vfs/root/test src/vfs/usr/include src/vfs/usr/lib/waste/cc/include \
  src/vfs/usr/share/licenses/clang skills/add-aux-binary docs/architecture.md docs/techniques.md docs/guest-sdk.md; do
  mkdir -p "$PACKAGE_STAGE/waste/$(dirname -- "$INPUT")"
  cp -a "$REPO_ROOT/$INPUT" "$PACKAGE_STAGE/waste/$INPUT"
done
mkdir -p "$PACKAGE_STAGE/waste/build/aux/awk/configure"
for INPUT in libc-config.site configure/config.h configure/config.log; do
  cp "$AWK_BUILD/$INPUT" "$PACKAGE_STAGE/waste/build/aux/awk/$INPUT"
done
REVISION="$(git -C "$REPO_ROOT/submodules/gawk" rev-parse HEAD 2>/dev/null || cat "$REPO_ROOT/submodules/gawk/.waste-source-revision")"
printf '%s\n' "$REVISION" > "$PACKAGE_STAGE/waste/submodules/gawk/.waste-source-revision"
# Shared libc's portable math dependency, including its license.
LLVM_LIBC_DEST="$PACKAGE_STAGE/waste/submodules/llvm-project/libc"
mkdir -p "$LLVM_LIBC_DEST/src" "$LLVM_LIBC_DEST/include"
for INPUT in hdr src/__support include/llvm-libc-macros include/llvm-libc-types LICENSE.TXT; do
  cp -a "$REPO_ROOT/submodules/llvm-project/libc/$INPUT" "$LLVM_LIBC_DEST/$INPUT"
done
chmod -R u+w "$LLVM_LIBC_DEST"
HASH="$(sha256sum "$AWK_BUILD/awk.wasm" | cut -d ' ' -f1)"
printf '{"format":1,"source_commit":"%s","binary_sha256":"%s","instructions":"waste/src/aux/awk/README.md"}\n' \
  "$REVISION" "$HASH" > "$PACKAGE_STAGE/SOURCE-BUNDLE.json"
tar --sort=name --mtime='UTC 1970-01-01' --owner=0 --group=0 --numeric-owner \
  --exclude=.git --exclude=__pycache__ --exclude='*.pyc' --exclude='*.wasm' \
  --exclude='*.o' --exclude='*.a' -cf - -C "$PACKAGE_STAGE" . |
  gzip -n > "$AWK_BUILD/awk-corresponding-source.tar.gz"
printf 'Awk corresponding source: %s\n' "$AWK_BUILD/awk-corresponding-source.tar.gz"
sha256sum "$AWK_BUILD/awk-corresponding-source.tar.gz"
