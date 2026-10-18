#!/usr/bin/env bash
# Cold compiler/linker loads need the same bounded window as test-clang.
set -euo pipefail
REPO_ROOT="$1"
export WASTE_TEST_TIMEOUT_MS="${WASTE_TEST_TIMEOUT_MS:-600000}"
exec bash "$REPO_ROOT/src/aux/run-wast-tests.sh" "$@"
