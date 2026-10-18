#!/usr/bin/env bash
# Supply compiled C/C++ inputs and capture observations; WAST owns assertions.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/checkpoint-c.wasm > /tmp/checkpoint-c.bin
printf '%s\n' "$?" > /tmp/checkpoint-c-status.txt
/tmp/checkpoint-cxx.wasm > /tmp/checkpoint-cxx.bin
printf '%s\n' "$?" > /tmp/checkpoint-cxx-status.txt
/bin/wast --verbose /tmp/checkpoint.wast && /bin/wast --verbose /tmp/checkpoint-jumps.wast
exit "$?"
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/checkpoint-c.wasm 755 "$TEST_OUTPUT/probe-c.wasm" \
  --stage-file /tmp/checkpoint-cxx.wasm 755 "$TEST_OUTPUT/probe-cxx.wasm" \
  --stage-file /tmp/checkpoint.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/checkpoint/checkpoint.wast" \
  --stage-file /tmp/checkpoint-jumps.wast 644 "$REPO_ROOT/src/vfs/root/test/engine-regressions/setjmp-buffer-copy.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
