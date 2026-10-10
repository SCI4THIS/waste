#!/usr/bin/env bash
# Boundary driver only: package/system WAST files own all assertions.
set -euo pipefail
REPO_ROOT="$(realpath -- "$1")"
VFS_ROOT="$(realpath -- "$2")"
TEST_NAME="$3"
TEST_SOURCE="$(realpath -- "$4")"
TEST_OUTPUT="$5"
WASM_RUNNER="$6"
TEST_SETUP="${7:-}"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
TEST_COMMANDS="$TEST_RUN/commands.sh"
printf 'status=0\n' > "$TEST_COMMANDS"
if [[ -n "$TEST_SETUP" ]]; then
  cat -- "$TEST_SETUP" >> "$TEST_COMMANDS"
  printf '\n' >> "$TEST_COMMANDS"
fi
shopt -s nullglob
TEST_FILES=("$TEST_SOURCE/"*.wast)
if ((${#TEST_FILES[@]} == 0)); then
  printf 'error: no WAST tests in %s\n' "$TEST_SOURCE" >&2
  exit 2
fi
set -- --vfs-root "$VFS_ROOT" --result-file "$TEST_RUN/results.json" \
  --timeout-ms "${WASTE_TEST_TIMEOUT_MS:-60000}"
for TEST_FILE in "${TEST_FILES[@]}"; do
  TEST_GUEST="/tmp/aux-tests/$TEST_NAME/$(basename -- "$TEST_FILE")"
  set -- "$@" --stage-file "$TEST_GUEST" 644 "$TEST_FILE"
  printf 'printf "%%s\\n" %q\n/bin/wast --verbose %q || status=1\n' \
    "$(basename -- "$TEST_FILE")" "$TEST_GUEST" >> "$TEST_COMMANDS"
done
printf 'exit "$status"\n' >> "$TEST_COMMANDS"
printf 'Results: %s/results.json\n' "$TEST_RUN"
cd "$REPO_ROOT"
exec "$WASM_RUNNER" "$@" "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_COMMANDS"
