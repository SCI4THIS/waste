#!/usr/bin/env bash
# Capture actual LLVM Wait observations; expectations live entirely in WAST.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/program-wait.wasm > /tmp/program-wait.bin
printf '%s\n' "$?" > /tmp/program-wait-status.txt
/bin/wast --verbose /tmp/program-wait.wast
exit $?
COMMANDS
printf 'Results: %s/results.json\n' "$TEST_RUN"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/program-wait.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/program-wait.wast 644 "$REPO_ROOT/src/system-tests/clang/program-wait/program-wait.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
