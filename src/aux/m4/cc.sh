#!/usr/bin/env bash
# M4 uses only the mounted guest SDK; configure retains main for link probes.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
SDK_ROOT="${WASTE_SDK_ROOT:-$REPO_ROOT/src/vfs}"
TOOLCHAIN="$REPO_ROOT/build/engine/toolchain/usr"
if [[ -x "$TOOLCHAIN/bin/wasm-ld" ]]; then
  export LD_LIBRARY_PATH="$TOOLCHAIN/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  set -- -B "$TOOLCHAIN/bin" "$@"
fi
# GNU inline semantics prevent SDK redeclarations of Gnulib's inline character
# predicates from emitting the same external definition in multiple objects.
exec "${WASTE_M4_CC:-clang}" --target=wasm32 -std=gnu23 -fgnu89-inline -ffreestanding -fno-builtin \
  -DWASTE_WASM -DWASTE_LEGACY_DECLARATIONS -fPIC -fno-stack-protector -fdata-sections -ffunction-sections \
  -nostdinc -nostdlib -isystem "$SDK_ROOT/usr/include" \
  -isystem "$SDK_ROOT/usr/lib/waste/cc/include" \
  -I "$SCRIPT_DIR" -I "$REPO_ROOT/src/aux/coreutils/include" \
  -include "$SCRIPT_DIR/compat.h" \
  -Wno-unused-command-line-argument -Wl,--no-entry,--export-if-defined=main "$@"
