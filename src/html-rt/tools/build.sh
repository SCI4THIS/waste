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
PACKAGE_AUDIT="$SCRIPT_DIR/audit-coreutils-package.py"

for f in "$TARBALL_JS" "$ZLIBAUX_WASM" "$LOADER_JS" "$AMALGAMATE" "$PACKAGE_AUDIT"; do
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
    cp -p "$PAGE_DIR/launch.wast" "$STAGING/"
  elif [ -f "$BUILD_DIR/bash-runtime.wast" ]; then
    cp -p "$BUILD_DIR/bash-runtime.wast" "$STAGING/launch.wast"
  else
    echo "error: launch.wast not found" >&2
    exit 1
  fi
  if [ -f "$REPO_ROOT/build/cli-rt/waste-probe.wasm" ]; then
    cp -p "$REPO_ROOT/build/cli-rt/waste-probe.wasm" "$STAGING/"
  fi
  for utility in true false pwd echo printf basename dirname cat wc ls date; do
    if [ -f "$PAGE_DIR/$utility.wasm" ]; then
      cp -p "$PAGE_DIR/$utility.wasm" "$STAGING/"
    else
      echo "error: linked coreutils $utility.wasm not found" >&2
      exit 1
    fi
  done
  provenance="$REPO_ROOT/build/coreutils/provenance.json"
  source_mapping="$REPO_ROOT/build/coreutils/coreutils-source-package.json"
  copying="$REPO_ROOT/submodules/coreutils/COPYING"
  if [ ! -f "$provenance" ]; then
    echo "error: coreutils provenance not found: $provenance" >&2
    exit 1
  fi
  if [ ! -f "$copying" ]; then
    echo "error: coreutils license notice not found: $copying" >&2
    exit 1
  fi
  if [ ! -f "$source_mapping" ]; then
    echo "error: Coreutils source-package mapping not found: $source_mapping" >&2
    exit 1
  fi
  mkdir -p "$STAGING/usr/share/waste" "$STAGING/usr/share/licenses/coreutils"
  cp -p "$provenance" "$STAGING/usr/share/waste/coreutils-provenance.json"
  cp -p "$source_mapping" "$STAGING/usr/share/waste/coreutils-source-package.json"
  cp -p "$copying" "$STAGING/usr/share/licenses/coreutils/COPYING"
  STAGING_PATH="$STAGING" python3 -c 'import json, os, pathlib; pathlib.Path(os.environ["STAGING_PATH"], "usr/share/waste/waste-interpreters.json").write_text(json.dumps({"interpreters":["/bin/wast","/bin/wat"]}, separators=(",", ":")) + "\n")'
  STAGING_PATH="$STAGING" python3 -c '
import json, os, pathlib
root = pathlib.Path(os.environ["STAGING_PATH"])
result = {}
for path in sorted(root.rglob("*")):
    if not path.is_file() or path.name == "vfs-mtimes.json":
        continue
    value = path.stat().st_mtime_ns
    result[path.relative_to(root).as_posix()] = {
        "sec": value // 1_000_000_000,
        "nsec": value % 1_000_000_000,
    }
(root / "vfs-mtimes.json").write_text(
    json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n")
'
fi

echo "  Staged $(find "$STAGING" -type f | wc -l) files"

# --- Step 2: Create tar.gz ---
TAR_GZ="$WORK_DIR/manifest.tar.gz"
TAR_RAW="$WORK_DIR/manifest.tar"
tar --sort=name --mtime='UTC 1970-01-01' --owner=0 --group=0 \
  --numeric-owner --format=ustar -cf "$TAR_RAW" -C "$STAGING" .
gzip -n -c "$TAR_RAW" > "$TAR_GZ"
python3 "$PACKAGE_AUDIT" --archive "$TAR_GZ"
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
