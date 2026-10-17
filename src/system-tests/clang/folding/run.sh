#!/usr/bin/env bash
# Supply actual folding constant-folding observations; WAST owns the functional assertions.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/folding.wasm > /tmp/folding.bin
printf '%s\n' "$?" > /tmp/folding-status.txt
/bin/wast --verbose /tmp/folding.wast
exit "$?"
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/folding.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/folding.wast 644 "$REPO_ROOT/src/system-tests/clang/folding/folding.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
