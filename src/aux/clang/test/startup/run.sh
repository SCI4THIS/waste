#!/usr/bin/env bash
# Stage inputs and invoke installed Bash; runtime assertions belong to WAST.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
. /tmp/startup-commands.sh
exit "$?"
COMMANDS
printf 'Results: %s/results.json\n' "$TEST_RUN"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/startup.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/startup-two.wasm 755 "$TEST_OUTPUT/probe-two.wasm" \
  --stage-file /tmp/startup.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/startup/startup.wast" \
  --stage-file /tmp/startup-input.txt 644 "$REPO_ROOT/src/aux/clang/test/startup/input.txt" \
  --stage-file /tmp/startup-commands.sh 644 "$REPO_ROOT/src/aux/clang/test/startup/commands.sh" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
