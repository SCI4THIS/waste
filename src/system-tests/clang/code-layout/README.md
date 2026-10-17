# LLVM ExtTSP code layout

The guest compiler profile selects WebAssembly and Common/Wasm LLD drivers.
The complete CodeLayout source contains ExtTSP basic-block placement, used by
LLVM MachineBlockPlacement, and CDSort function ordering. CDSort's production
callers are LLD ELF and BOLT, which the selected guest graph excludes.

The owned patch excludes only CDSort's options, implementation and entrypoints
in the guest. Native profiles retain both algorithms. ExtTSP stays unchanged,
and no missing `pow` provider or SDK capability is fabricated. Re-audit the
selection when enabling other targets or linker drivers. The source file
remains in its normal CMake target; no whole-file exclusion is involved.

The observer links the complete production CodeLayout object, real Support
and current C++/libc providers. The 76-assertion WAST checks two 512-byte captures,
both observer statuses (35), required option registration and absent CDSort
options. Seven graph cases cover reversed/forward chains, cold/disconnected
nodes, a hot/cold diamond, backward loops and zero-count conditional edges.
Returned orders are written after inputs have been destroyed. Both score
overloads and computed layouts are checked with independently specified
scores, using a 2e-12 absolute bound for binary64 arithmetic. Distance cutoff,
zero-count and self-loop controls cover both sides of scoring boundaries.

The second invocation passes the real LLVM option
`--ext-tsp-fallthrough-weight-uncond=2`. WAST checks the corresponding score
changes while conditional fallthrough remains unchanged. These tests accept
the selected code-layout boundary, not CDSort, every TransformUtils operation,
missing transcendental math or a working compiler/linker.

From the repository root:

```sh
make -C src/system-tests code-layout-fixture code-layout-native-source code-layout-guest-source
make -C src/system-tests test-code-layout
make -C src/cli-rt BUILD_DIR=../../build/cli-rt ../../build/cli-rt/wasm-sanitize
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  bash src/system-tests/clang/code-layout/run.sh "$PWD" "$PWD/src/vfs" \
  "$PWD/build/system-tests/clang/code-layout" "$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install bash-html vfs-tests-check
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/code-layout/browser.json
```

Expect 76 PASS and two observer statuses of 35; the packaged worker expects
77/77 results and exit zero. Complete source checks cover native, native
threaded and guest threaded profiles. Save logs under `build/engine/logs`.
ASan and UBSan stay enabled; leak checking remains disabled for the ptraced
runner.

For manual acceptance, open a fresh rebuilt `build/html-rt/bash.html` and run:

```sh
upload /tmp/code-layout.wasm
```

Choose `build/system-tests/clang/code-layout/probe.wasm`, then run:

```sh
chmod +x /tmp/code-layout.wasm
/tmp/code-layout.wasm > /tmp/code-layout-default.bin
printf '%s\n' "$?" > /tmp/code-layout-status.txt
/tmp/code-layout.wasm --ext-tsp-fallthrough-weight-uncond=2 > /tmp/code-layout-weighted.bin
printf '%s\n' "$?" >> /tmp/code-layout-status.txt
wast --verbose /root/test/system/clang/code-layout/code-layout.wast > /tmp/code-layout-results.txt
printf '%s\n' "$?" > /tmp/code-layout-test-exit.txt
download /tmp/code-layout-results.txt
download /tmp/code-layout-status.txt
download /tmp/code-layout-test-exit.txt
```

Save the three downloads under `build/html-rt`. Expect 76 PASS, no failures,
two observer statuses of 35 and WAST exit status zero.
