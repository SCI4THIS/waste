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
/tmp/session.wasm > /tmp/session.bin
printf '%s\n' "$?" > /tmp/session-status.txt
/bin/wast --verbose /tmp/session.wast
exit $?
COMMANDS
printf 'Results: %s/results.json\n' "$TEST_RUN"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/session.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/session-copy.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/session.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/session/session.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
