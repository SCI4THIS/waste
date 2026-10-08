#!/usr/bin/env bash
# Coreutils-only compiler profile. Public headers come directly from src/vfs.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
SDK_ROOT="${WASTE_SDK_ROOT:-$REPO_ROOT/src/vfs}"
COREUTILS_BUILD="${WASTE_COREUTILS_BUILD_DIR:-$REPO_ROOT/build/aux/coreutils}"
TOOLCHAIN="$REPO_ROOT/build/engine/toolchain/usr"
if [[ -x "$TOOLCHAIN/bin/wasm-ld" ]]; then
  export LD_LIBRARY_PATH="$TOOLCHAIN/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  set -- -B "$TOOLCHAIN/bin" "$@"
fi
exec "${WASTE_COREUTILS_CC:-clang}" --target=wasm32 -std=gnu23 -ffreestanding -fno-builtin \
  -DWASTE_WASM -DWASTE_LEGACY_DECLARATIONS -fno-stack-protector \
  -fdata-sections -ffunction-sections -nostdinc -nostdlib \
  -isystem "$SDK_ROOT/usr/include" -isystem "$SDK_ROOT/usr/lib/waste/cc/include" \
  -I "$COREUTILS_BUILD/include" -I "$SCRIPT_DIR/include" \
  -include "$SCRIPT_DIR/include/waste-gnulib-compat.h" -Wl,--no-entry "$@"
