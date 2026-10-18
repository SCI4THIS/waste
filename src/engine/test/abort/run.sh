#!/usr/bin/env bash
# Stage compiler inputs and capture observations; WAST owns assertions.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/abort-probe.wasm > /tmp/abort-observations.txt
/bin/wast --verbose /tmp/abort.wast
exit "$?"
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/abort-probe.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/abort.wast 644 "$REPO_ROOT/src/vfs/root/test/system/abort/abort.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
