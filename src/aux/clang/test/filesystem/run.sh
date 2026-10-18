#!/usr/bin/env bash
# Capture compiled observations and exit status; WAST owns the assertions.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/filesystem.wasm > /tmp/filesystem.bin
printf '%s\n' "$?" > /tmp/filesystem-status.txt
/bin/wast --verbose /tmp/filesystem.wast
filesystem_status=$?
COMMANDS
FILESYSTEM_LAUNCH=("$VFS_ROOT/usr/bin/bash" --norc)
FILESYSTEM_CONTROLS=()
if [[ $# -eq 5 ]]; then
  FILESYSTEM_LAUNCH=(--script "$VFS_ROOT/usr/share/waste/launch.wast")
  FILESYSTEM_CONTROLS=(--clock-realtime-ns "$5"
    --stage-file /tmp/filesystem-clock.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/filesystem/clock.wast")
  cat >> "$TEST_RUN/commands.sh" <<'COMMANDS'
if [ "$filesystem_status" -eq 0 ]; then
  /bin/wast --verbose /tmp/filesystem-clock.wast
  filesystem_status=$?
fi
COMMANDS
fi
printf '%s\n' 'exit "$filesystem_status"' >> "$TEST_RUN/commands.sh"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/filesystem.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/filesystem.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/filesystem/filesystem.wast" \
  "${FILESYSTEM_CONTROLS[@]}" "${FILESYSTEM_LAUNCH[@]}" < "$TEST_RUN/commands.sh"
