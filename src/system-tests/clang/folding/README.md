# Optional LLVM host math folding

The observer links the completed production LLVM Analysis archive and its
real Core/Support dependencies against current guest
C++/libc providers. All functional assertions live in `folding.wast`.
The source gate also checks nine complete production object rules covering
ConstantFolding, TargetLibraryInfo, value/vector/assumption queries and
target-transform dependencies. The archive and these source objects share
their normal CMake producers.

LLVM can decline an optional fold without changing the original operation.
The explicit guest profile uses this result for host math functions that have
no shared libc provider. Native builds keep ordinary host evaluation. Exact
APFloat cases and supported providers keep their existing paths. The same
policy applies to the math-call side-effect query: an unevaluated power must
not be declared harmless from a fabricated result. Guest math availability
annotations remain intact.

The 63-assertion WAST verifies 119 recorded words (952 bytes), observer status
34, scalar double/float/half declines, both power operands, library-call
declines and retained IR instructions. It also checks exact logarithm special
values, signed zero, poison propagation, vector partial decline and complete
exact vector folds, shared sqrt/log2 results, APFloat fabs/floor/powi/fmod,
target-library availability, no-builtin behavior, nondeterminism controls and
errno preservation. AMDGPU cases retain exact quarter-integer trigonometric
folds and decline general host-trigonometric evaluation. These tests do not establish missing transcendental
providers, all LLVM Analysis behavior or a working compiler/linker.

From the repository root:

```sh
make -C src/system-tests folding-fixture folding-native-source folding-guest-source
make -C src/system-tests test-folding
make -C src/cli-rt BUILD_DIR=../../build/cli-rt ../../build/cli-rt/wasm-sanitize
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  bash src/system-tests/clang/folding/run.sh "$PWD" "$PWD/src/vfs" \
  "$PWD/build/system-tests/clang/folding" "$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install bash-html vfs-tests-check
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/folding/browser.json
```

Expect 63 PASS and observer status 34 in native/sanitizer runs, and 64/64 with
zero exit status in the packaged worker. Complete source checks cover native,
native threaded and guest threaded profiles. Save logs under
`build/engine/logs`. Leak checking remains disabled for the ptraced runner;
AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.

For manual acceptance, open a fresh rebuilt `build/html-rt/bash.html` and run:

```sh
upload /tmp/folding.wasm
```

Choose `build/system-tests/clang/folding/probe.wasm`, then run:

```sh
chmod +x /tmp/folding.wasm
/tmp/folding.wasm > /tmp/folding.bin
printf '%s\n' "$?" > /tmp/folding-status.txt
wast --verbose /root/test/system/clang/folding/folding.wast > /tmp/folding-results.txt
printf '%s\n' "$?" > /tmp/folding-test-exit.txt
download /tmp/folding-results.txt
download /tmp/folding-status.txt
download /tmp/folding-test-exit.txt
```

Save the downloads under `build/html-rt`. Expected results are 63 PASS, no
failures, observer status 34 and WAST exit status zero.
