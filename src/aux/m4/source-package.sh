#!/usr/bin/env bash
# Bundle the source and build inputs corresponding to the built guest binary.
set -euo pipefail
REPO_ROOT="$(realpath -- "$1")"
M4_BUILD="$(realpath -- "$2")"
case "$M4_BUILD" in
  "$REPO_ROOT/build/"*) ;;
  *) printf 'error: source packages must stay under build/\n' >&2; exit 1 ;;
esac
PACKAGE_STAGE="$(mktemp -d "$M4_BUILD/source-package-XXXXXX")"
trap 'chmod -R u+w "$PACKAGE_STAGE"; rm -rf -- "$PACKAGE_STAGE"' EXIT
# Export the exact immutable source objects used by this package.
M4_REVISION="$(cat "$REPO_ROOT/src/aux/m4/source-revision")"
GNULIB_REVISION="$(cat "$REPO_ROOT/src/aux/m4/gnulib-revision")"
bash "$REPO_ROOT/src/aux/m4/archive-source.sh" "${M4_SOURCE_REPO:-$REPO_ROOT/submodules/m4}" \
  "$M4_REVISION" "$PACKAGE_STAGE/waste/submodules/m4"
bash "$REPO_ROOT/src/aux/m4/archive-source.sh" "${M4_GNULIB_SOURCE_REPO:-$REPO_ROOT/submodules/coreutils/gnulib}" \
  "$GNULIB_REVISION" "$PACKAGE_STAGE/waste/submodules/coreutils/gnulib"
for INPUT in LICENSE start.sh .gitmodules src/config.h src/aux src/engine src/cli-rt src/html-rt \
  src/vfs/root/test src/vfs/usr/include src/vfs/usr/lib/waste/cc/include \
  src/vfs/usr/share/licenses/clang skills/add-aux-binary docs/architecture.md docs/techniques.md docs/guest-sdk.md; do
  mkdir -p "$PACKAGE_STAGE/waste/$(dirname -- "$INPUT")"
  cp -a "$REPO_ROOT/$INPUT" "$PACKAGE_STAGE/waste/$INPUT"
done
mkdir -p "$PACKAGE_STAGE/waste/build/aux/m4/configure"
for INPUT in libc-config.site configure/lib/config.h configure/config.log; do
  mkdir -p "$PACKAGE_STAGE/waste/build/aux/m4/$(dirname -- "$INPUT")"
  cp "$M4_BUILD/$INPUT" "$PACKAGE_STAGE/waste/build/aux/m4/$INPUT"
done
# Shared libc's portable math dependency, including its license.
LLVM_LIBC_DEST="$PACKAGE_STAGE/waste/submodules/llvm-project/libc"
mkdir -p "$LLVM_LIBC_DEST/src" "$LLVM_LIBC_DEST/include"
for INPUT in hdr src/__support include/llvm-libc-macros include/llvm-libc-types LICENSE.TXT; do
  cp -a "$REPO_ROOT/submodules/llvm-project/libc/$INPUT" "$LLVM_LIBC_DEST/$INPUT"
done
chmod -R u+w "$LLVM_LIBC_DEST"
HASH="$(sha256sum "$M4_BUILD/m4.wasm" | cut -d ' ' -f1)"
printf '{"format":1,"source_commit":"%s","gnulib_commit":"%s","binary_sha256":"%s","instructions":"waste/src/aux/m4/README.md"}\n' \
  "$M4_REVISION" "$GNULIB_REVISION" "$HASH" > "$PACKAGE_STAGE/SOURCE-BUNDLE.json"
tar --sort=name --mtime='UTC 1970-01-01' --owner=0 --group=0 --numeric-owner \
  --exclude=.git --exclude=__pycache__ --exclude='*.pyc' --exclude='*.wasm' \
  --exclude='*.o' --exclude='*.a' -cf - -C "$PACKAGE_STAGE" . |
  gzip -n > "$M4_BUILD/m4-corresponding-source.tar.gz"
printf 'M4 corresponding source: %s\n' "$M4_BUILD/m4-corresponding-source.tar.gz"
sha256sum "$M4_BUILD/m4-corresponding-source.tar.gz"
