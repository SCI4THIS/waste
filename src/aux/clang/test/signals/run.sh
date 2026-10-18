#!/usr/bin/env bash
# Capture ordinary guest children; expectations live entirely in WAST.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/signals.wasm > /tmp/signals.bin
printf '%s\n' "$?" > /tmp/signals-status.txt
/bin/wast --verbose /tmp/signals.wast
exit $?
COMMANDS
printf 'Results: %s/results.json\n' "$TEST_RUN"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/signals.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/signals.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/signals/signals.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
