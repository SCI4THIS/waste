# Supply compiler inputs/capture outputs only; compiled.wast owns assertions.
cat > /tmp/clang-probe.c <<'CLANG_INPUT'
__attribute__((import_module("env"), import_name("exit")))
extern void guest_exit(int);
__attribute__((export_name("_start")))
void _start(void) { guest_exit(6 * 7); }
CLANG_INPUT
/usr/bin/clang --target=wasm32-unknown-unknown -nostdlib \
  -Wl,--no-entry,--export=_start,--import-memory,--import-table \
  /tmp/clang-probe.c -o /tmp/clang-probe.wasm
printf '%s\n' "$?" > /tmp/clang-compile-status.txt
/bin/wasm /tmp/clang-probe.wasm
printf '%s\n' "$?" > /tmp/clang-run-status.txt
