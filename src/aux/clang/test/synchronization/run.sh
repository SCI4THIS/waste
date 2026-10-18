#!/usr/bin/env bash
# Capture compiled observations; WAST owns the assertions.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/synchronization.wasm > /tmp/synchronization.bin
printf '%s\n' "$?" > /tmp/synchronization-status.txt
/bin/wast --verbose /tmp/synchronization.wast
exit "$?"
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/synchronization.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/synchronization.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/synchronization/synchronization.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
