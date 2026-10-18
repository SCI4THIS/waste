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
/tmp/wide-format-c.wasm > /tmp/wide-format-c.bin
printf '%s\n' "$?" > /tmp/wide-format-c-status.txt
/tmp/wide-format-cxx.wasm > /tmp/wide-format-cxx.bin
printf '%s\n' "$?" > /tmp/wide-format-cxx-status.txt
/bin/wast --verbose /tmp/wide-format.wast
exit "$?"
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms "${WASTE_TEST_TIMEOUT_MS:-60000}" \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/wide-format-c.wasm 755 "$TEST_OUTPUT/probe-c.wasm" \
  --stage-file /tmp/wide-format-cxx.wasm 755 "$TEST_OUTPUT/probe-cxx.wasm" \
  --stage-file /tmp/wide-format.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/wide-format/wide-format.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
