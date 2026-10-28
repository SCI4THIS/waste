#!/usr/bin/env bash
# Compile the real guest CRT/SDK boundary and transport authored WAST assertions.
set -euo pipefail
REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd)"
CHECK_BUILD="${WASTE_CALLBACK_BUILD:-$REPO_ROOT/build/cli-rt/relocated-callbacks}"
RUNNER="${1:-$REPO_ROOT/build/cli-rt/wasm}"
mkdir -p "$CHECK_BUILD"
FLAGS=(--target=wasm32 -O2 -std=c11 -ffreestanding -fno-builtin -DWASTE_WASM -DNDEBUG
  -fPIC -fno-stack-protector -fdata-sections -ffunction-sections -nostdinc -nostdlib
  -isystem "$REPO_ROOT/src/vfs/usr/include" -isystem "$REPO_ROOT/src/vfs/usr/lib/waste/cc/include")
"${WASTE_GUEST_CC:-clang}" "${FLAGS[@]}" -c "$REPO_ROOT/src/cli-rt/test/relocated-callbacks/probe.c" -o "$CHECK_BUILD/probe.o"
"${WASTE_GUEST_CC:-clang}" "${FLAGS[@]}" -DWASTE_MAIN_TWO_ARGS -c "$REPO_ROOT/src/aux/libc/waste-crt.c" -o "$CHECK_BUILD/crt.o"
"${WASTE_GUEST_LD:-wasm-ld}" --no-entry --import-memory --import-table --export=_start --export=main \
  --export=__heap_base --export=__data_end --export=__stack_pointer --experimental-pic \
  --unresolved-symbols=import-dynamic --gc-sections --global-base=655360 --table-base=1024 \
  -z stack-size=524288 "$CHECK_BUILD/probe.o" "$CHECK_BUILD/crt.o" -o "$CHECK_BUILD/raw.wasm"
python3 "$REPO_ROOT/src/aux/libc/tools/shared_libc.py" --provider "$REPO_ROOT/src/vfs/lib/libc.so.wasm" \
  --rewrite "$CHECK_BUILD/raw.wasm" --output "$CHECK_BUILD/probe.wasm"
cat > "$CHECK_BUILD/commands.sh" <<'COMMANDS'
/tmp/relocated-callback > /tmp/relocated-callback-output
printf '%s\n' "$?" > /tmp/relocated-callback-status
/bin/wast --verbose /tmp/relocated-callback.wast
exit "$?"
COMMANDS
"$RUNNER" --vfs-root "$REPO_ROOT/src/vfs" --timeout-ms 300000 \
  --stage-file /tmp/relocated-callback 755 "$CHECK_BUILD/probe.wasm" \
  --stage-file /tmp/relocated-callback.wast 644 "$REPO_ROOT/src/vfs/root/test/system/cli-runtime/relocated-callbacks.wast" \
  "$REPO_ROOT/src/vfs/usr/bin/bash" --norc < "$CHECK_BUILD/commands.sh"
