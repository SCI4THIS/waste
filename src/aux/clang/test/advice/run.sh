#!/usr/bin/env bash
# Capture real guest-process observations; all expectations are in WAST.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/advice.wasm > /tmp/advice.bin
printf '%s\n' "$?" > /tmp/advice-status.txt
/bin/wast --verbose /tmp/advice.wast
CPP_RESULT=$?
/bin/wast --verbose /tmp/advice-ranges.wast
RANGE_RESULT=$?
exit $((CPP_RESULT | RANGE_RESULT))
COMMANDS
printf 'Results: %s/results.json\n' "$TEST_RUN"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/advice.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/advice.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/advice/advice.wast" \
  --stage-file /tmp/advice-ranges.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/advice/ranges.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
