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
/tmp/finite-wait.wasm > /tmp/finite-wait.bin
printf '%s\n' "$?" > /tmp/finite-wait-status.txt
/bin/wast --verbose /tmp/finite-wait.wast
exit $?
COMMANDS
printf 'Results: %s/results.json\n' "$TEST_RUN"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/finite-wait.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/finite-wait.wast 644 "$REPO_ROOT/src/system-tests/clang/finite-wait/finite-wait.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
