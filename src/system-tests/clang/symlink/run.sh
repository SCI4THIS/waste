#!/usr/bin/env bash
# WAST owns assertions; Bash transports observations from real guest children.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/symlink.wasm > /tmp/symlink.bin
printf '%s\n' "$?" > /tmp/symlink-status.txt
/bin/wast --verbose /tmp/symlink.wast
result=$?
/tmp/symlink-command
printf '%s\n' "$?" > /tmp/symlink-loader.txt
/tmp/symlink-bin/true
printf '%s\n' "$?" >> /tmp/symlink-loader.txt
/bin/wast --verbose /tmp/symlink-loader.wast
last=$?
exit $((result | last))
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/symlink.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/symlink.wast 644 "$REPO_ROOT/src/system-tests/clang/symlink/symlink.wast" \
  --stage-file /tmp/symlink-loader.wast 644 "$REPO_ROOT/src/system-tests/clang/symlink/symlink-loader.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
