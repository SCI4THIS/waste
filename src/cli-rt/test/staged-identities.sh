#!/usr/bin/env bash
# Boundary transport only; the guest WAST owns identity assertions.
set -euo pipefail
REPO_ROOT="$(realpath -- "$1")"
VFS_ROOT="$(realpath -- "$2")"
WASM_RUNNER="$(realpath -- "$3")"
TEST_OUTPUT="$4"
TEST_SOURCE="$REPO_ROOT/src/vfs/root/test/system/cli-runtime/staged-identities.wast"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
printf '/bin/wast --verbose /tmp/staged-identities.wast\nexit $?\n' > "$TEST_RUN/commands.sh"
printf 'Results: %s/results.json\n' "$TEST_RUN"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/staged-first 644 "$TEST_SOURCE" \
  --stage-file /tmp/staged-second 644 "$TEST_SOURCE" \
  --stage-file /tmp/staged-identities.wast 644 "$TEST_SOURCE" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
