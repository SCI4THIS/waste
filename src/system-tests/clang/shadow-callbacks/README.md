# Shadow callback declarations

AddressSanitizer's stack callback initializer formats two-digit lowercase
hexadecimal suffixes with LLVM's existing raw-string sink in the explicit
disabled-thread guest profile. Native and threaded profiles retain standard
streams. The callback set, function signatures and insertion behavior are
unchanged.

The observer includes the complete production source and calls its actual
callback initializer with real LLVM modules. It adds no production test hook,
replacement formatter or relaxed access check. The 47 WAST assertions check
all 13 suffixes, leading zeroes and lowercase letters, void/two-argument
signatures for 32- and 64-bit IR pointer widths, declaration counts, reuse of
a pre-existing declaration, repeated initialization and unpopulated callback
slots. Cases cover Never, Runtime and Always stack-return modes and enabled
scope poisoning. Captured names outlive every module, context and formatter.
This accepts the callback declaration boundary, rather than the complete
instrumentation pass or a guest sanitizer runtime.

From the repository root:

```sh
make -C src/system-tests shadow-callbacks-fixture shadow-callbacks-native-source shadow-callbacks-guest-source
make -C src/system-tests test-shadow-callbacks
make -C src/cli-rt BUILD_DIR=../../build/cli-rt ../../build/cli-rt/wasm-sanitize
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  bash src/system-tests/clang/shadow-callbacks/run.sh "$PWD" "$PWD/src/vfs" \
  "$PWD/build/system-tests/clang/shadow-callbacks" "$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install bash-html vfs-tests-check
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/shadow-callbacks/browser.json
```

Expect 47 PASS, observer status 37 and WAST exit zero. The packaged worker
expects 48/48 results including launch. Full-source checks cover native,
native threaded and guest threaded profiles. Keep logs under
`build/engine/logs`. ASan/UBSan remain enabled; leak checking is disabled
for the ptraced runner.

For manual acceptance, open the rebuilt `build/html-rt/bash.html` and run:

```sh
upload /tmp/shadow-callbacks.wasm
```

Choose `build/system-tests/clang/shadow-callbacks/probe.wasm`, then run:

```sh
chmod +x /tmp/shadow-callbacks.wasm
/tmp/shadow-callbacks.wasm > /tmp/shadow-callbacks.bin
printf '%s\n' "$?" > /tmp/shadow-callbacks-status.txt
wast --verbose /root/test/system/clang/shadow-callbacks/shadow-callbacks.wast > /tmp/shadow-callbacks-results.txt
printf '%s\n' "$?" > /tmp/shadow-callbacks-test-exit.txt
download /tmp/shadow-callbacks-results.txt
download /tmp/shadow-callbacks-status.txt
download /tmp/shadow-callbacks-test-exit.txt
```

Save the three downloads under `build/html-rt`. Expect 47 PASS, zero failures,
observer status 37 and WAST exit zero.
