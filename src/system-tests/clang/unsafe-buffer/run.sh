#!/usr/bin/env bash
# Stage compiler input and capture observations; assertions live in WAST.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
SOURCE="$REPO_ROOT/src/system-tests/clang/unsafe-buffer"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
. /tmp/unsafe-buffer-setup.sh
/bin/wast --verbose /tmp/unsafe-buffer.wast
exit "$?"
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms "${WASTE_TEST_TIMEOUT_MS:-180000}" \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/unsafe-buffer.cpp 644 "$SOURCE/input.cpp" \
  --stage-file /tmp/unsafe-buffer-setup.sh 644 "$SOURCE/setup.sh" \
  --stage-file /tmp/unsafe-buffer.wast 644 "$SOURCE/unsafe-buffer.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
