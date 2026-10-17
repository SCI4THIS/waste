#!/usr/bin/env bash
# Capture actual LLVM argument fits and exec limits observations; expectations live entirely in WAST.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/argument-limits.wasm > /tmp/argument-limits.bin
printf '%s\n' "$?" > /tmp/argument-limits-status.txt
/bin/wast --verbose /tmp/argument-limits.wast
exit $?
COMMANDS
printf 'Results: %s/results.json\n' "$TEST_RUN"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 180000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/argument-limits.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/argument-limits.wast 644 "$REPO_ROOT/src/system-tests/clang/argument-limits/argument-limits.wast" \
  --stage-file /tmp/argument-script.wat 755 "$REPO_ROOT/src/system-tests/clang/argument-limits/script.wat" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
