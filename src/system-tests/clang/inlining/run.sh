#!/usr/bin/env bash
# Supply actual inlining statistics observations; WAST owns the functional assertions.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/inlining.wasm > /tmp/inlining.bin 2> /tmp/inlining-dump.txt
printf '%s\n' "$?" > /tmp/inlining-status.txt
/bin/wast --verbose /tmp/inlining.wast
exit "$?"
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/inlining.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/inlining.wast 644 "$REPO_ROOT/src/system-tests/clang/inlining/inlining.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
