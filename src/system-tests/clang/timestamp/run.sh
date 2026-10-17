#!/usr/bin/env bash
# Stage observations; WAST owns all runtime expectations.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/timestamp.wasm > /tmp/timestamp.bin
printf '%s\n' "$?" > /tmp/timestamp-status.txt
/bin/wast --verbose /tmp/timestamp.wast
exit "$?"
COMMANDS
printf 'Results: %s/results.json\n' "$TEST_RUN"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/timestamp.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/timestamp-reference.bin 644 "$TEST_OUTPUT/reference.bin" \
  --stage-file /tmp/timestamp.wast 644 "$REPO_ROOT/src/system-tests/clang/timestamp/timestamp.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
