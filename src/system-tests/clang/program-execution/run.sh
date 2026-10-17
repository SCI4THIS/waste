#!/usr/bin/env bash
# Stage the compiled observer; WAST owns all runtime expectations.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/program-execution.wasm > /tmp/program-execution.bin
printf '%s\n' "$?" > /tmp/program-execution-status.txt
/bin/wast --verbose /tmp/program-execution.wast
exit $?
COMMANDS
printf 'Results: %s/results.json\n' "$TEST_RUN"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/program-execution.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/program-execution.wast 644 "$REPO_ROOT/src/system-tests/clang/program-execution/program-execution.wast" \
  --stage-file /tmp/program-execution-start.wat 755 "$REPO_ROOT/src/system-tests/clang/program-execution/start.wat" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
