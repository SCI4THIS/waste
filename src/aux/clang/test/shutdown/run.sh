#!/usr/bin/env bash
# Stage real artifacts and invoke Bash; all runtime expectations belong to WAST.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
. /tmp/shutdown-commands.sh
exit "$?"
COMMANDS
printf 'Results: %s/results.json\n' "$TEST_RUN"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/shutdown.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/shutdown-legacy.wasm 755 "$TEST_OUTPUT/probe-legacy.wasm" \
  --stage-file /tmp/shutdown.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/shutdown/shutdown.wast" \
  --stage-file /tmp/shutdown-commands.sh 644 "$REPO_ROOT/src/aux/clang/test/shutdown/commands.sh" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
