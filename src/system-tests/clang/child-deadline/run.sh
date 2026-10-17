#!/usr/bin/env bash
# Compiled observations, followed by all runtime expectations in WAST.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/child-deadline.wasm > /tmp/child-deadline.bin
printf '%s\n' "$?" > /tmp/child-deadline-status.txt
/bin/wast --verbose /tmp/child-deadline.wast
exit $?
COMMANDS
printf 'Results: %s/results.json\n' "$TEST_RUN"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/child-deadline.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/child-deadline.wast 644 "$REPO_ROOT/src/system-tests/clang/child-deadline/child-deadline.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
