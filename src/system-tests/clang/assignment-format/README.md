# Assignment-tracking interval formatting

AssignmentTrackingAnalysis's private interval formatter uses LLVM's existing
`raw_string_ostream` in the explicit disabled-thread guest profile. Native and
threaded profiles retain `std::stringstream`. Interval analysis, debug-option
selection and the public SDK are unchanged.

The observer includes the complete production source and uses
`-fno-access-control` only to call its private formatter and supply actual IR
values and interval maps. Production compilation keeps normal access checks;
there are no production test hooks or replacement formatter implementations.
WAST owns all 23 assertions: exact bytes and lengths, optional newlines, empty
and exhausted iterators, no-location and unnamed values, unsigned upper bounds,
heap-backed names, embedded NUL/control bytes and retained strings after names
change and every IR/map/formatter owner is destroyed. This checks the formatter,
rather than accepting the entire assignment-tracking analysis or CodeGen.

From the repository root:

```sh
make -C src/system-tests assignment-format-fixture assignment-format-native-source assignment-format-guest-source
make -C src/system-tests test-assignment-format
make -C src/cli-rt BUILD_DIR=../../build/cli-rt ../../build/cli-rt/wasm-sanitize
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  bash src/system-tests/clang/assignment-format/run.sh "$PWD" "$PWD/src/vfs" \
  "$PWD/build/system-tests/clang/assignment-format" "$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install bash-html vfs-tests-check
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/assignment-format/browser.json
```

Expect 23 PASS, observer status 36 and WAST exit zero. The packaged worker
expects 24/24 results including launch. Complete source checks cover native,
native threaded and guest threaded profiles. Keep logs under
`build/engine/logs`. ASan and UBSan remain enabled; leak checking is disabled
for the ptraced runner.

For manual acceptance, open the rebuilt `build/html-rt/bash.html` and run:

```sh
upload /tmp/assignment-format.wasm
```

Choose `build/system-tests/clang/assignment-format/probe.wasm`, then run:

```sh
chmod +x /tmp/assignment-format.wasm
/tmp/assignment-format.wasm > /tmp/assignment-format.bin
printf '%s\n' "$?" > /tmp/assignment-format-status.txt
wast --verbose /root/test/system/clang/assignment-format/assignment-format.wast > /tmp/assignment-format-results.txt
printf '%s\n' "$?" > /tmp/assignment-format-test-exit.txt
download /tmp/assignment-format-results.txt
download /tmp/assignment-format-status.txt
download /tmp/assignment-format-test-exit.txt
```

Save the three downloads under `build/html-rt`. Expect 23 PASS, no failures,
observer status 36 and WAST exit status zero.
