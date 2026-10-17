# Compiled LLVM endian boundary

This probe includes the real staged `llvm/ADT/bit.h` first, taking its bare-Wasm
`machine/endian.h` fallback. The guest compiler's private compatibility header
derives the BSD names from the public SDK's `endian.h`. Neither host headers
nor Linux/Emscripten platform macros are supplied. The C++ input only records
observations; all functional assertions belong to `endian.wast`.

```sh
make -C src/system-tests test-endian
```

The fixture builds/configures the source-built LLVM prerequisites and compiles
an observer with the host cross-compiler. It does not need a completed guest
Clang executable. `endian-fixture` only builds the observer. Outputs live under
`build/system-tests/clang/endian`; native runs write their result JSON there.

Twenty-one WAST assertions check the report size, exit status, public/private
byte-order codes, LLVM's native byte order, the actual Wasm target, absent host
OS macros, 16/32/64-bit swaps and SDK conversions, and stored byte ordering.
Volatile inputs keep the swaps from being folded to constants. The report is
eight i32 metadata values followed by three records of four i64 values.

Run the same compiled artifact through the offline page's production worker:

```sh
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/endian/browser.json \
  > build/engine/logs/clang-browser-endian-results.json
```

Expect 22 passing results including Bash bootstrap, then Bash exit status zero.
For an optional manual run, first refresh snapshots with `vfs-tests-install`
and rebuild `bash.html`. Upload `build/system-tests/clang/endian/probe.wasm`
as `/tmp/endian.wasm`, then run in browser Bash:

```sh
chmod +x /tmp/endian.wasm
/tmp/endian.wasm > /tmp/endian.bin
printf '%s\n' "$?" > /tmp/endian-status.txt
wast --verbose /root/test/system/clang/endian/endian.wast > /tmp/endian-results.txt
echo "$?"
download /tmp/endian-results.txt
```

Expect 21 passing assertions and status zero. Store downloaded evidence under
`build/html-rt`. This verifies the header/compiled-code boundary, not full
Clang startup, POSIX support or compiler acceptance.
