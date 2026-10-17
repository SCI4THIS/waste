#!/usr/bin/env bash
# Compiled observers write results; WAST owns all runtime expectations.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/watchdog.wasm > /tmp/watchdog.bin
printf '%s\n' "$?" > /tmp/watchdog-status.txt
/bin/wast --verbose /tmp/watchdog.wast
exit $?
COMMANDS
printf 'Results: %s/results.json\n' "$TEST_RUN"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/watchdog.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/watchdog.wast 644 "$REPO_ROOT/src/system-tests/clang/watchdog/watchdog.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
