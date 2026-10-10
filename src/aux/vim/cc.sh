#!/usr/bin/env bash
# Vim package compiler profile; host generators use CC_FOR_BUILD separately.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
SDK_ROOT="${WASTE_SDK_ROOT:-$REPO_ROOT/src/vfs}"
TOOLCHAIN="$REPO_ROOT/build/engine/toolchain/usr"
if [[ -x "$TOOLCHAIN/bin/wasm-ld" ]]; then
  export LD_LIBRARY_PATH="$TOOLCHAIN/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  set -- -B "$TOOLCHAIN/bin" "$@"
fi
# A stub archive dir supplies empty libncurses.a / libtinfo.a / libtermcap.a so
# configure link tests that pass -lncurses succeed without resolving tgetent at
# host-link time; the real symbols come from the engine's shared libncurses
# provider at guest runtime.
STUB_LIBS="${WASTE_VIM_STUB_LIBS:-}"
LIB_FLAGS=()
if [[ -n "$STUB_LIBS" ]]; then
  LIB_FLAGS+=(-L "$STUB_LIBS")
fi
exec "${WASTE_VIM_CC:-clang}" --target=wasm32 -std=gnu11 -ffreestanding -fno-builtin \
  -DWASTE_WASM -fPIC -fno-stack-protector -fdata-sections -ffunction-sections \
  -nostdinc -nostdlib -I "$SCRIPT_DIR/include" \
  -isystem "$SDK_ROOT/usr/include" -isystem "$SDK_ROOT/usr/lib/waste/cc/include" \
  -Wno-implicit-function-declaration -Wno-int-conversion \
  -Wno-deprecated-non-prototype -Wno-incompatible-function-pointer-types \
  -Wno-unused-command-line-argument \
  "${LIB_FLAGS[@]}" \
  -Wl,--no-entry,--export-if-defined=main \
  -Wl,--unresolved-symbols=import-dynamic,--experimental-pic "$@"
