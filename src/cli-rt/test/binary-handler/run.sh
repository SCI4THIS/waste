#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
TEST_SOURCE="$REPO_ROOT/src/cli-rt/test/binary-handler"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/bin-handler-printf 644 "$VFS_ROOT/usr/bin/printf" \
  --stage-file /tmp/bin-handler-text.wat 644 "$TEST_SOURCE/not-binary.wat" \
  --stage-file /tmp/bin-handler-results.wast 644 "$REPO_ROOT/src/vfs/root/test/system/cli-runtime/binary-handler/results.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_SOURCE/commands.sh"
