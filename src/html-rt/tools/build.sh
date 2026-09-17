#!/usr/bin/env bash
# build.sh — Amalgamate staging src/ files into a single self-contained HTML.
#
# Usage:
#   bash build.sh tests   # Build test dashboard → build/html-rt/test.html
#   bash build.sh bash    # Build bash terminal  → build/html-rt/bash.html
#
# Prerequisites:
#   - src/{tests,bash}/ staging files must exist (run Python generators first)
#   - tarballjs and zlib-wasm submodules must be populated
#   - gzip, tar, python3 must be available
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HTML_RT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$HTML_RT/../.." && pwd)"
SRC_DIR="$HTML_RT/src"
SHARED_DIR="$SRC_DIR/shared"
BUILD_DIR="$REPO_ROOT/build/html-rt"

TARGET="${1:-}"
if [ -z "$TARGET" ]; then
  echo "Usage: $0 {tests|bash}" >&2
  exit 1
fi

if [ "$TARGET" != "tests" ] && [ "$TARGET" != "bash" ]; then
  echo "error: unknown target '$TARGET' (expected 'tests' or 'bash')" >&2
  exit 1
fi

PAGE_DIR="$SRC_DIR/$TARGET"
if [ ! -f "$PAGE_DIR/index.html" ]; then
  echo "error: $PAGE_DIR/index.html not found — run the Python generator first" >&2
  exit 1
fi

TARBALL_JS="$REPO_ROOT/submodules/tarballjs/tarball.js"
ZLIBAUX_WASM="$REPO_ROOT/submodules/zlib-wasm/zlibaux.wasm"
LOADER_JS="$SHARED_DIR/loader.js"
AMALGAMATE="$SCRIPT_DIR/amalgamate.py"

for f in "$TARBALL_JS" "$ZLIBAUX_WASM" "$LOADER_JS" "$AMALGAMATE"; do
  if [ ! -f "$f" ]; then
    echo "error: required file not found: $f" >&2
    exit 1
  fi
done

mkdir -p "$BUILD_DIR"
WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/waste-build-XXXXXX")
trap 'rm -rf "$WORK_DIR"' EXIT

echo "=== Building $TARGET ==="

# --- Step 1: Collect files into tar staging directory ---
STAGING="$WORK_DIR/tar-staging"
mkdir -p "$STAGING"

if [ "$TARGET" = "tests" ]; then
  cp "$PAGE_DIR/worker.js" "$STAGING/"
  if [ -f "$PAGE_DIR/waste-wast.wasm" ]; then
    cp "$PAGE_DIR/waste-wast.wasm" "$STAGING/"
  elif [ -f "$BUILD_DIR/waste-wast.wasm" ]; then
    cp "$BUILD_DIR/waste-wast.wasm" "$STAGING/"
  else
    echo "error: waste-wast.wasm not found" >&2
    exit 1
  fi
  if [ -f "$PAGE_DIR/payload.json" ]; then
    cp "$PAGE_DIR/payload.json" "$STAGING/"
  else
    echo "error: payload.json not found — run the Python generator with --output-dir first" >&2
    exit 1
  fi
  # Copy WAST files from their original source locations (submodule, tests/)
  # using the sourcePath fields recorded in payload.json.
  python3 -c "
import json, sys, os, shutil
payload = json.load(open('$PAGE_DIR/payload.json'))
repo = '$REPO_ROOT'
staging = '$STAGING'
for t in payload['tests']:
    sp = t['spec'].get('sourcePath')
    if not sp:
        continue
    dest = os.path.join(staging, 'wast', t['file'])
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    shutil.copy2(os.path.join(repo, sp), dest)
"
elif [ "$TARGET" = "bash" ]; then
  cp "$PAGE_DIR/worker.js" "$STAGING/"
  if [ -f "$PAGE_DIR/waste-wast.wasm" ]; then
    cp "$PAGE_DIR/waste-wast.wasm" "$STAGING/"
  elif [ -f "$BUILD_DIR/waste-wast.wasm" ]; then
    cp "$BUILD_DIR/waste-wast.wasm" "$STAGING/"
  else
    echo "error: waste-wast.wasm not found" >&2
    exit 1
  fi
  if [ -f "$PAGE_DIR/launch.wast" ]; then
    cp "$PAGE_DIR/launch.wast" "$STAGING/"
  elif [ -f "$BUILD_DIR/bash-runtime.wast" ]; then
    cp "$BUILD_DIR/bash-runtime.wast" "$STAGING/launch.wast"
  else
    echo "error: launch.wast not found" >&2
    exit 1
  fi
fi

echo "  Staged $(find "$STAGING" -type f | wc -l) files"

# --- Step 2: Create tar.gz ---
TAR_GZ="$WORK_DIR/manifest.tar.gz"
tar -czf "$TAR_GZ" -C "$STAGING" .
TAR_SIZE=$(stat -c%s "$TAR_GZ" 2>/dev/null || stat -f%z "$TAR_GZ")
echo "  Compressed tar: $TAR_SIZE bytes"

# --- Step 3: Determine output path ---
if [ "$TARGET" = "tests" ]; then
  FINAL="$BUILD_DIR/test.html"
elif [ "$TARGET" = "bash" ]; then
  FINAL="$BUILD_DIR/bash.html"
fi

# --- Step 4: Amalgamate via Python ---
python3 "$AMALGAMATE" \
  --page-dir "$PAGE_DIR" \
  --tarball-js "$TARBALL_JS" \
  --loader-js "$LOADER_JS" \
  --manifest-tar-gz "$TAR_GZ" \
  --zlibaux-wasm "$ZLIBAUX_WASM" \
  --output "$FINAL"

echo "=== Done: $FINAL ==="
