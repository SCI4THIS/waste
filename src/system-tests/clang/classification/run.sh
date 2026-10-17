#!/usr/bin/env bash
# Supply compiled C/C++ inputs and capture observations; WAST owns assertions.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/classification-c.wasm > /tmp/classification-c.bin
printf '%s\n' "$?" > /tmp/classification-c-status.txt
/tmp/classification-cxx.wasm > /tmp/classification-cxx.bin
printf '%s\n' "$?" > /tmp/classification-cxx-status.txt
/bin/wast --verbose /tmp/classification.wast
exit "$?"
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/classification-c.wasm 755 "$TEST_OUTPUT/probe-c.wasm" \
  --stage-file /tmp/classification-cxx.wasm 755 "$TEST_OUTPUT/probe-cxx.wasm" \
  --stage-file /tmp/classification.wast 644 "$REPO_ROOT/src/system-tests/clang/classification/classification.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
