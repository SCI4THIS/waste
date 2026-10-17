#!/usr/bin/env bash
# Stage real inputs; Bash orchestrates commands and WAST owns assertions.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
LLD_TOOL="$5"
COMMANDS_SOURCE="${6:-$REPO_ROOT/src/system-tests/clang/lld-link/commands.sh}"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
printf '. /tmp/lld-link-commands.sh\nexit "$?"\n' > "$TEST_RUN/commands.sh"
printf 'Results: %s/results.json\n' "$TEST_RUN"
cd "$REPO_ROOT"
SDK_STAGE_ARGS=()
if [[ "$COMMANDS_SOURCE" == "$REPO_ROOT/src/system-tests/clang/lld-link/sdk-commands.sh" ]]; then
  SDK_STAGE_ARGS=(
    --stage-file /tmp/sdk.o 644 "$TEST_OUTPUT/sdk.o"
    --stage-file /tmp/sdk-crt.o 644 "$TEST_OUTPUT/sdk-crt.o"
    --stage-file /tmp/bad-signature.o 644 "$TEST_OUTPUT/bad-signature.o"
    --stage-file /tmp/lld-sdk.wast 644 "$REPO_ROOT/src/system-tests/clang/lld-link/sdk.wast"
  )
fi
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms "${WASTE_TEST_TIMEOUT_MS:-180000}" \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/wasm-ld 755 "$LLD_TOOL" \
  --stage-file /tmp/link-entry.o 644 "$TEST_OUTPUT/entry.o" \
  --stage-file /tmp/link-answer.a 644 "$TEST_OUTPUT/answer.a" \
  --stage-file /tmp/lld-link.wast 644 "$REPO_ROOT/src/system-tests/clang/lld-link/link.wast" \
  --stage-file /tmp/link-answer.bc 644 "$TEST_OUTPUT/answer.bc" \
  --stage-file /tmp/link-answer-bc.a 644 "$TEST_OUTPUT/answer-bc.a" \
  --stage-file /tmp/lld-no-lto.wast 644 "$REPO_ROOT/src/system-tests/clang/lld-link/no-lto.wast" \
  --stage-file /tmp/lld-link-positive.sh 644 "$REPO_ROOT/src/system-tests/clang/lld-link/commands.sh" \
  "${SDK_STAGE_ARGS[@]}" \
  --stage-file /tmp/lld-link-commands.sh 644 "$COMMANDS_SOURCE" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
