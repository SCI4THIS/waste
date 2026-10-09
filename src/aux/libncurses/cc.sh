#!/usr/bin/env bash
# Ncurses compiler profile; public headers come directly from the mounted SDK.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
SDK_ROOT="${WASTE_SDK_ROOT:-$REPO_ROOT/src/vfs}"
TOOLCHAIN="$REPO_ROOT/build/engine/toolchain/usr"
if [[ -x "$TOOLCHAIN/bin/wasm-ld" ]]; then
  export LD_LIBRARY_PATH="$TOOLCHAIN/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  set -- -B "$TOOLCHAIN/bin" "$@"
fi
exec "${WASTE_NCURSES_CC:-clang}" --target=wasm32 -std=c11 -ffreestanding -fno-builtin \
  -DWASTE_WASM -fno-stack-protector -fdata-sections -ffunction-sections \
  -nostdinc -nostdlib -isystem "$SDK_ROOT/usr/include" \
  -isystem "$SDK_ROOT/usr/lib/waste/cc/include" \
  -Wno-unused-command-line-argument -Wl,--no-entry "$@"
