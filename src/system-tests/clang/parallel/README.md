# LLVM latch and scalar parallel algorithms

LLVM's disabled-thread guest profile executes TaskGroup callbacks immediately.
Its private Latch therefore needs a checked atomic counter rather than a mutex
and condition variable from the thread-disabled libc++. Only
`LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS` selects this implementation. Balanced
counts drain normally; unfinished sync/destruction, underflow and overflow call
the real shared `std::abort()`, including in release builds. Native and threaded
profiles retain LLVM's standard blocking synchronization. This compiler profile
does not change WASTE's process/thread scheduler.

```sh
make -C src/system-tests test-parallel parallel-native-source
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-parallel \
  WASM_RUNNER=../../build/cli-rt/wasm-sanitize
```

Finish any `make -C src/aux clang` attempt first: the fixture's prerequisite may
refresh the same guest configuration. The observer compiles actual staged LLVM
Parallel.cpp without LTO, links current libc++/libc++abi, production CRT and
shared libc, and records observations. All 124 functional assertions belong to
`parallel.wast`. Results go under `build/system-tests/clang/parallel/run-*/`.
Normal static-registration flags remain enabled; `-DNDEBUG` verifies that latch
rejections do not depend on assertions. Each invalid operation runs in a real
guest child, and WAST checks its SIGABRT wait status and parent survival.

Coverage includes default/nonzero latch counts, repeated completion, nested and
recursive TaskGroups, shared captures, heap-backed function closures, loop range
fences, empty/nested loops, in-place updates, reductions, owning strings and
ascending/descending sorting with duplicates and large heap-backed inputs.
Move-only reduction uses the iterator overload: upstream's range wrapper copies
its accumulator. The sort range wrapper receives an explicit comparator.
Native source gates also compile actual Parallel.cpp through native and threaded
guest branches, checking that their latch retains blocking fields. Error-joining
algorithms, threaded execution, full C++ startup/static exit and runnable guest
Clang/LLD remain separate acceptance work.

Refresh snapshots and package the page before the production worker check:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/parallel/browser.json \
  > build/engine/logs/clang-browser-parallel-results.json \
  2> build/engine/logs/clang-browser-parallel-errors.log
```

For an optional manual check, upload
`build/system-tests/clang/parallel/probe.wasm` as `/tmp/parallel.wasm` in
`bash.html`, then run:

```sh
chmod +x /tmp/parallel.wasm
/tmp/parallel.wasm > /tmp/parallel.bin
printf '%s\n' "$?" > /tmp/parallel-status.txt
wast --verbose /root/test/system/clang/parallel/parallel.wast > /tmp/parallel.txt
download /tmp/parallel.txt
```

Expect `WAST: 124 PASS, 0 FAIL, 124 total`. The worker adds the launch assertion.
