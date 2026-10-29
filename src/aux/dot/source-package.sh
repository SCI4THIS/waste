#!/usr/bin/env bash
# Preserve the selected Graphviz sources, private build inputs and shared libc.
set -euo pipefail
REPO_ROOT="$(realpath -- "$1")"
DOT_BUILD="$(realpath -- "$2")"
case "$DOT_BUILD" in "$REPO_ROOT/build/"*) ;; *) exit 1 ;; esac
PACKAGE_STAGE="$(mktemp -d "$DOT_BUILD/source-package-XXXXXX")"
trap 'chmod -R u+w "$PACKAGE_STAGE"; rm -rf -- "$PACKAGE_STAGE"' EXIT
UPSTREAM="$REPO_ROOT/submodules/graphviz"
DEST="$PACKAGE_STAGE/waste/submodules/graphviz"
mkdir -p "$DEST/cmd" "$DEST/plugin"
cp -a "$UPSTREAM/lib" "$DEST/"
cp -a "$UPSTREAM/cmd/dot" "$DEST/cmd/"
cp -a "$UPSTREAM/plugin/core" "$UPSTREAM/plugin/dot_layout" "$DEST/plugin/"
for INPUT in COPYING LICENSE EPL-2.0.txt CHANGELOG.md graphviz_version.h.in; do
  cp -a "$UPSTREAM/$INPUT" "$DEST/"
done
cp "$DOT_BUILD/source/.source-revision" "$DEST/.waste-source-revision"
for INPUT in LICENSE start.sh .gitmodules src/config.h src/aux src/engine src/cli-rt src/html-rt \
  src/vfs/root/test src/vfs/usr/include src/vfs/usr/lib/waste/cc/include \
  src/vfs/usr/share/licenses/clang skills/add-aux-binary docs/architecture.md docs/techniques.md docs/guest-sdk.md; do
  mkdir -p "$PACKAGE_STAGE/waste/$(dirname -- "$INPUT")"
  cp -a "$REPO_ROOT/$INPUT" "$PACKAGE_STAGE/waste/$INPUT"
done
LLVM_LIBC_DEST="$PACKAGE_STAGE/waste/submodules/llvm-project/libc"
mkdir -p "$LLVM_LIBC_DEST/src" "$LLVM_LIBC_DEST/include"
for INPUT in hdr src/__support include/llvm-libc-macros include/llvm-libc-types LICENSE.TXT; do
  cp -a "$REPO_ROOT/submodules/llvm-project/libc/$INPUT" "$LLVM_LIBC_DEST/$INPUT"
done
HASH="$(sha256sum "$DOT_BUILD/dot.wasm" | cut -d ' ' -f1)"
printf '{"format":1,"source_commit":"%s","binary_sha256":"%s","instructions":"waste/src/aux/dot/README.md"}\n' \
  "$(cat "$DOT_BUILD/source/.source-revision")" "$HASH" > "$PACKAGE_STAGE/SOURCE-BUNDLE.json"
tar --sort=name --mtime='UTC 1970-01-01' --owner=0 --group=0 --numeric-owner \
  --exclude=.git --exclude=__pycache__ --exclude='*.pyc' --exclude='*.wasm' \
  --exclude='*.o' --exclude='*.a' -cf - -C "$PACKAGE_STAGE" . |
  gzip -n > "$DOT_BUILD/dot-corresponding-source.tar.gz"
printf 'Dot corresponding source: %s\n' "$DOT_BUILD/dot-corresponding-source.tar.gz"
sha256sum "$DOT_BUILD/dot-corresponding-source.tar.gz"
