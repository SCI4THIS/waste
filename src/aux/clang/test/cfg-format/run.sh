#!/usr/bin/env bash
# Supply actual CFG formatter observations; WAST owns the functional assertions.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/cfg-format.wasm > /tmp/cfg-format.bin
printf '%s\n' "$?" > /tmp/cfg-format-status.txt
/bin/wast --verbose /tmp/cfg-format.wast
exit "$?"
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/cfg-format.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/cfg-format.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/cfg-format/cfg-format.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
