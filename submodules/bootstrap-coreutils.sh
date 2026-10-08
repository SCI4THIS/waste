#!/usr/bin/env bash
# Compatibility entry point; package build ownership lives in src/aux.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/../src/aux/coreutils/bootstrap.sh" "$@"
