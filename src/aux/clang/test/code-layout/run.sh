#!/usr/bin/env bash
# Stage real observations; WAST owns all functional assertions.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/code-layout.wasm > /tmp/code-layout-default.bin
printf '%s\n' "$?" > /tmp/code-layout-status.txt
/tmp/code-layout.wasm --ext-tsp-fallthrough-weight-uncond=2 > /tmp/code-layout-weighted.bin
printf '%s\n' "$?" >> /tmp/code-layout-status.txt
/bin/wast --verbose /tmp/code-layout.wast
exit "$?"
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/code-layout.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/code-layout.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/code-layout/code-layout.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
