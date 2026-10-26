#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
SDK_ROOT="${WASTE_SDK_ROOT:-$REPO_ROOT/src/vfs}"
exec "${WASTE_OPENSSL_CC:-clang}" --target=wasm32 -std=gnu11 -ffreestanding -fno-builtin \
  -DWASTE_WASM -DRIO_POLL_METHOD=3 -DNO_SYSLOG -D__STDC_NO_ATOMICS__=1 -fPIC -fno-stack-protector -fdata-sections -ffunction-sections \
  -nostdinc -nostdlib -isystem "$SDK_ROOT/usr/include" \
  -isystem "$SDK_ROOT/usr/lib/waste/cc/include" "$@"
