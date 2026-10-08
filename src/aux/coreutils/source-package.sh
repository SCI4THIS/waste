#!/usr/bin/env bash
# Package actual staged source and repository build inputs using standard tar.
set -euo pipefail
REPO_ROOT="$(realpath -- "$1")"
COREUTILS_BUILD="$(realpath -- "$2")"
AUX_BUILD="$(realpath -- "$3")"
shift 3
UTILITIES=("$@")
case "$COREUTILS_BUILD" in
  "$REPO_ROOT/build/"*) ;;
  *) printf 'error: source packages must stay under build/\n' >&2; exit 1 ;;
esac
PACKAGE_STAGE="$(mktemp -d "$COREUTILS_BUILD/source-package-XXXXXX")"
trap 'rm -rf -- "$PACKAGE_STAGE"' EXIT
# Bootstrap can create absolute links to its staged Gnulib files. Materialize
# those files so the release archive doesn't depend on this build directory.
cp -aL "$COREUTILS_BUILD/source" "$PACKAGE_STAGE/coreutils-source"
mkdir -p "$PACKAGE_STAGE/waste"
for INPUT in start.sh .gitmodules src/config.h src/aux src/engine src/cli-rt src/libc src/html-rt \
  src/vfs/usr/include src/vfs/usr/lib/waste/cc/include src/vfs/usr/share/licenses/clang \
  docs/coreutils-source-distribution.md docs/architecture.md docs/techniques.md docs/guest-sdk.md; do
  mkdir -p "$PACKAGE_STAGE/waste/$(dirname -- "$INPUT")"
  cp -a "$REPO_ROOT/$INPUT" "$PACKAGE_STAGE/waste/$INPUT"
done
mkdir -p "$PACKAGE_STAGE/waste/build/aux/coreutils"
for INPUT in source-revision.txt patch-sha256.txt coreutils-provenance.json configure/lib/config.h configure/config.log; do
  mkdir -p "$PACKAGE_STAGE/waste/build/aux/coreutils/$(dirname -- "$INPUT")"
  cp "$COREUTILS_BUILD/$INPUT" "$PACKAGE_STAGE/waste/build/aux/coreutils/$INPUT"
done
REVISION="$(cat "$COREUTILS_BUILD/source-revision.txt")"
{
  printf '{"format":1,"source_commit":"%s","utilities":[' "$REVISION"
  SEPARATOR=''
  for UTILITY in "${UTILITIES[@]}"; do
    printf '%s"%s"' "$SEPARATOR" "$UTILITY"
    SEPARATOR=,
  done
  printf '],"binaries":{'
  SEPARATOR=''
  for UTILITY in "${UTILITIES[@]}"; do
    HASH="$(sha256sum "$AUX_BUILD/$UTILITY/$UTILITY.wasm" | cut -d ' ' -f1)"
    printf '%s"%s":"%s"' "$SEPARATOR" "$UTILITY" "$HASH"
    SEPARATOR=,
  done
  printf '}}\n'
} > "$PACKAGE_STAGE/SOURCE-BUNDLE.json"
ARCHIVE="$COREUTILS_BUILD/coreutils-corresponding-source.tar.gz"
tar --sort=name --mtime='UTC 1970-01-01' --owner=0 --group=0 --numeric-owner \
  --exclude=.git --exclude=__pycache__ --exclude='*.pyc' --exclude='*.wasm' \
  --exclude='*.o' --exclude='*.a' --exclude=dcgen \
  -cf - -C "$PACKAGE_STAGE" . | gzip -n > "$ARCHIVE"
HASH="$(sha256sum "$ARCHIVE" | cut -d ' ' -f1)"
printf '{"format":1,"archive":"coreutils-corresponding-source.tar.gz","sha256":"%s","source_commit":"%s","utilities":[' \
  "$HASH" "$REVISION" > "$COREUTILS_BUILD/coreutils-source-package.json"
SEPARATOR=''
for UTILITY in "${UTILITIES[@]}"; do
  printf '%s"%s"' "$SEPARATOR" "$UTILITY" >> "$COREUTILS_BUILD/coreutils-source-package.json"
  SEPARATOR=,
done
printf '],"instructions":"docs/coreutils-source-distribution.md"}\n' >> "$COREUTILS_BUILD/coreutils-source-package.json"
printf 'Coreutils corresponding source: %s\nSHA-256: %s\n' "$ARCHIVE" "$HASH"
