#!/usr/bin/env bash
# Stage real observations; WAST owns all functional assertions.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/assignment-format.wasm > /tmp/assignment-format.bin
printf '%s\n' "$?" > /tmp/assignment-format-status.txt
/bin/wast --verbose /tmp/assignment-format.wast
exit "$?"
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/assignment-format.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/assignment-format.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/assignment-format/assignment-format.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
