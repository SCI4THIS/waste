#!/usr/bin/env bash
# build.sh — Amalgamate staging src/ files into a single self-contained HTML.
#
# Usage:
#   bash build.sh bash    # Build shell/test page → build/html-rt/bash.html
#
# Prerequisites:
#   - Authored frontend under src/; generated payload/bootstrap under build/
#   - tarballjs and zlib-wasm submodules must be populated
#   - gzip, tar, python3 must be available
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HTML_RT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$HTML_RT/../.." && pwd)"
SRC_DIR="$HTML_RT/src"
BUILD_DIR="$REPO_ROOT/build/html-rt"

TARGET="${1:-}"
if [ -z "$TARGET" ]; then
  echo "Usage: $0 bash" >&2
  exit 1
fi

if [ "$TARGET" != "bash" ]; then
  echo "error: unknown target '$TARGET' (only 'bash' is supported; tests run from bash.html)" >&2
  exit 1
fi

shift
PAGE_DIR="$SRC_DIR"
VFS_ROOT="$REPO_ROOT/src/vfs"
WASM_FILE="$BUILD_DIR/waste-wast.wasm"
FINAL="$BUILD_DIR/bash.html"
while (($#)); do
  if (($# < 2)); then
    echo "error: missing value for '$1'" >&2
    exit 1
  fi
  case "$1" in
    --source-dir) PAGE_DIR="$2" ;;
    --vfs-root) VFS_ROOT="$2" ;;
    --wasm) WASM_FILE="$2" ;;
    --launch) : ;; # Legacy argument; use usr/share/waste/launch.wast in VFS_ROOT.
    --payload) PAYLOAD_FILE="$2" ;;
    --output) FINAL="$2" ;;
    *) echo "error: unknown option '$1'" >&2; exit 1 ;;
  esac
  shift 2
done
ENTRY=index.html
WORKER=worker.js
if [ ! -f "$PAGE_DIR/$ENTRY" ]; then
  echo "error: authored frontend not found: $PAGE_DIR/$ENTRY" >&2
  exit 1
fi

TARBALL_JS="$REPO_ROOT/submodules/tarballjs/tarball.js"
ZLIBAUX_WASM="$REPO_ROOT/submodules/zlib-wasm/zlibaux.wasm"
LOADER_JS="$PAGE_DIR/loader.js"
AMALGAMATE="$SCRIPT_DIR/amalgamate.py"

for f in "$TARBALL_JS" "$ZLIBAUX_WASM" "$LOADER_JS" "$AMALGAMATE"; do
  if [ ! -f "$f" ]; then
    echo "error: required file not found: $f" >&2
    exit 1
  fi
done

mkdir -p "$BUILD_DIR"
python3 "$SCRIPT_DIR/runtime_config.py" --javascript > "$BUILD_DIR/runtime-config.js"
WORK_DIR=$(mktemp -d "$BUILD_DIR/package-$TARGET-XXXXXX")
trap 'rm -rf "$WORK_DIR"' EXIT

echo "=== Building $TARGET ==="

# --- Step 1: Collect files into tar staging directory ---
STAGING="$WORK_DIR/tar-staging"
mkdir -p "$STAGING"

cp "$REPO_ROOT/tests/browser-corpus-expected-failures.txt" "$STAGING/"
cp -p "$WASM_FILE" "$STAGING/waste-wast.wasm"
cp -p "$VFS_ROOT/usr/share/waste/launch.wast" "$STAGING/launch.wast"
python3 "$SCRIPT_DIR/vfs.py" package --root "$VFS_ROOT" --output "$STAGING"

echo "  Staged $(find "$STAGING" -type f | wc -l) files"

# --- Step 2: Create tar.gz ---
TAR_GZ="$WORK_DIR/manifest.tar.gz"
TAR_RAW="$WORK_DIR/manifest.tar"
tar --sort=name --mtime='UTC 1970-01-01' --owner=0 --group=0 \
  --numeric-owner --format=ustar -cf "$TAR_RAW" -C "$STAGING" .
gzip -n -c "$TAR_RAW" > "$TAR_GZ"
python3 "$SCRIPT_DIR/vfs.py" archive-audit --archive "$TAR_GZ"
TAR_SIZE=$(stat -c%s "$TAR_GZ" 2>/dev/null || stat -f%z "$TAR_GZ")
echo "  Compressed tar: $TAR_SIZE bytes"

# --- Step 4: Amalgamate via Python ---
python3 "$AMALGAMATE" \
  --page-dir "$PAGE_DIR" \
  --tarball-js "$TARBALL_JS" \
  --loader-js "$LOADER_JS" \
  --manifest-tar-gz "$TAR_GZ" \
  --zlibaux-wasm "$ZLIBAUX_WASM" \
  --output "$FINAL"

echo "=== Done: $FINAL ==="
