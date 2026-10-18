#!/usr/bin/env bash
# Capture compiled observations and exit status; WAST owns the assertions.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/lock-owner.wasm > /tmp/lock-owner.bin
printf '%s\n' "$?" > /tmp/lock-owner-status.txt
/bin/wast --verbose /tmp/lock-owner.wast
exit "$?"
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/lock-owner.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/lock-owner.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/lock-owner/lock-owner.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
