#!/usr/bin/env bash
# Capture compiled observations and status; WAST owns every assertion.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/wide.wasm > /tmp/wide.bin
printf '%s\n' "$?" > /tmp/wide-status.txt
/bin/wast --verbose /tmp/wide.wast
exit "$?"
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/wide.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/wide.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/wide/wide.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
