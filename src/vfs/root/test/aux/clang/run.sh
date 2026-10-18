#!/usr/bin/env bash
# Compiler cold loads validate the complete tool; allow the package to finish.
set -euo pipefail
REPO_ROOT="$1"
export WASTE_TEST_TIMEOUT_MS="${WASTE_TEST_TIMEOUT_MS:-600000}"
exec bash "$REPO_ROOT/src/aux/run-wast-tests.sh" "$@"
