# LLVM inlining statistics and narrow floating formatting

The observer includes the complete staged
`ImportedFunctionsInliningStatistics.cpp` to call its private statistics
formatter and public graph API. It links real LLVM Core/Support dependencies
and current guest C++ libraries. The guest-only formatter uses LLVM's raw string
sink and `format("%.4g", ...)`; native/threaded profiles retain standard streams
and `setprecision(4)`. Complete source checks cover both preserved profiles and
the actual guest object rule.

Shared libc's narrow printf now uses the same exact significand/decimal/hex
conversion as wide printf. A private parsed-spec header coordinates the two
sinks. Narrow output counts the complete result while retaining a bounded byte
prefix; wide output retains its insufficient-capacity error contract. Zero runs
are counted directly once narrow storage is full, including enormous requested
precision. No compiler-local numeric algorithm, host locale or new scheduling
mechanism is introduced.

The 16-assertion system WAST checks all 12 observations, a 96-byte capture,
exact 263-byte helper/compiled output and 710-byte verbose graph dump. Cases
include four significant digits, zero denominators, scientific notation,
negative/large fractions, omitted newlines and copied labels. A compiled C++
call mixes width/precision integers, double, integer, binary128 and a string.
Real IR functions supply imported metadata and a transitive inlining graph;
renaming them after recording verifies the graph's owned names.

The separate package `src/aux/libc/tests/narrow-floating-format.wast` has 51
assertions for f/e/g/a variants, binary64 extremes, ties-to-even, signs and
padding, aligned mixed arguments, exact fit/truncation, zero/null sizing,
overflow and maximum count without a precision-sized output loop. Real files
and allocating printf calls check successful output and error propagation before
any allocation or write. These checks
do not establish every printf extension, full LLVM Analysis or a working
compiler/linker.

From the repository root:

```sh
make -C src/system-tests inlining-fixture inlining-native-source inlining-guest-source
make -C src/system-tests test-inlining
make -C src/cli-rt BUILD_DIR=../../build/cli-rt ../../build/cli-rt/wasm-sanitize
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  bash src/system-tests/clang/inlining/run.sh "$PWD" "$PWD/src/vfs" \
  "$PWD/build/system-tests/clang/inlining" "$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install bash-html vfs-tests-check
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/inlining/browser.json
```

Expect 16 PASS for the system WAST, observer status 33, and 68/68 results with
zero exit status in the production worker, which also runs the 51 package
assertions. Leak checking is disabled for the ptraced runner; ASan and UBSan
remain enabled. Save verification output under `build/engine/logs`.

To test manually, open the rebuilt `build/html-rt/bash.html` in a fresh browser
tab and run:

```sh
upload /tmp/inlining.wasm
```

Choose `build/system-tests/clang/inlining/probe.wasm`, then run:

```sh
chmod +x /tmp/inlining.wasm
/tmp/inlining.wasm > /tmp/inlining.bin 2> /tmp/inlining-dump.txt
printf '%s\n' "$?" > /tmp/inlining-status.txt
wast --verbose /root/test/system/clang/inlining/inlining.wast > /tmp/inlining-results.txt
printf '%s\n' "$?" > /tmp/inlining-test-exit.txt
wast --verbose /root/test/aux/libc/narrow-floating-format.wast >> /tmp/inlining-results.txt
printf '%s\n' "$?" >> /tmp/inlining-test-exit.txt
download /tmp/inlining-results.txt
download /tmp/inlining-status.txt
download /tmp/inlining-test-exit.txt
```

Save the three downloads under `build/html-rt`. Expect 16 PASS and 51 PASS,
zero failures, observer status 33 and two WAST exit statuses of zero.
