# LLVM time-profiler storage and lifecycle

TimeProfiler.cpp selects LLVM's counted `SmartMutex<true>` only for
`LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`. Its guards use the actual field type.
Native/threaded profiles retain `std::mutex`; registry ownership, static
registration, event algorithms and JSON emission remain upstream code. This
compiler profile does not change WASTE's scheduler.

```sh
make -C src/aux test-time-profiler time-profiler-native-source CLANG_JOBS=1
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-time-profiler \
  WASM_RUNNER=../../build/cli-rt/wasm-sanitize CLANG_JOBS=1
```

Complete other LLVM builds first: these targets refresh the same guest
configuration and Support archive, with one build job. The sanitizer invocation
selects both the sanitized public Wasm runner and sanitized private interactive
session adapter. `test-time-profiler-lifecycle` runs only the lifecycle fixture.

`probe.cpp` includes actual staged TimeProfiler.cpp to access private storage
and entry types. Its 72 observations and 74 WAST assertions check the selected
mutex/scoped locking, point-before-subtraction time conversion, negative and
full-width timestamps, heap-backed name/detail ownership, independent copies,
metadata mutation, moved buffers and 257 instant entries. Copied snapshots
outlive their original owner. Storage results are under
`build/system-tests/clang/time-profiler/run-*/`.

`lifecycle.cpp` uses public APIs linked against the complete real
LLVMSupport/Demangle and libc++/libc++abi archives, production CRT and shared
libc. It records 51 observations and writes real trace files; all 62 functional
expectations are in `lifecycle.wast`. It does not include TimeProfiler.cpp or
inspect its private fields. `lifecycle-native.py` stages files and drives the
existing native session adapter with the same `lifecycle.json` used by the
production browser worker. Six real `usleep(1000)` waits advance the injected
monotonic clock from 42 seconds to 42.013 seconds. Realtime stays fixed at
4,294,967,296,123,456,789 ns; no host timing tolerance is used.

The lifecycle checks disabled/lazy calls, verbose state, nested same-name
sections, RAII scopes, asynchronous begin/end pairs and instants. Finished
instances aggregate counts/durations, avoiding double counting recursive names.
Exact JSON bytes check event order, negative cross-instance timestamps,
heap-backed metadata after mutation, quote/backslash/newline/tab escaping,
process/thread metadata and full-width realtime microseconds. Sequential
finished instances exercise the disabled-thread compiler profile, without
claiming actual concurrent LLVM thread profiling.

The actual preferred, fallback and default file-writing APIs produce the same
bytes as the raw stream; missing parents report ENOENT and do not create files.
Granularity filters events and their instants while retaining short/zero totals.
Repeated cleanup and fresh initialization discard old entries/totals/metadata.
Malloc/free observers delegate to the real allocator and record the registry's
last backing allocation. The real function-local vector destructor frees it
exactly once before the earlier C atexit reporter runs, using ordinary static
registration flags. Normal process exit remains 17; the enclosing Bash exits
zero only after WAST accepts the capture. Native results and transcripts are
`lifecycle-{native,sanitize}.{json,txt}` in the fixture build directory.

Linking the actual JSON writer exposed its `modf` import. Shared libc supplies
it using existing exact truncation/sign primitives. Twenty package WAST
assertions cover signed zero, fractions, integral boundaries, subnormals,
infinities, NaNs, output canaries and errno preservation. The C++ SDK probe
checks the public declaration and `std::modf` linkage. General narrow floating
printf, wider math APIs and full Clang/LLD acceptance remain separate work.

Refresh snapshots and package the page before production worker checks:

```sh
make -C src/html-rt vfs-tests-install vfs-tests-check bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/time-profiler/lifecycle.json \
  > build/engine/logs/clang-browser-profiler-lifecycle-results.json \
  2> build/engine/logs/clang-browser-profiler-lifecycle-errors.log
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/time-profiler/browser.json \
  > build/engine/logs/clang-browser-profiler-storage-results.json \
  2> build/engine/logs/clang-browser-profiler-storage-errors.log
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/time-profiler/modf.json \
  > build/engine/logs/clang-browser-profiler-modf-results.json \
  2> build/engine/logs/clang-browser-profiler-modf-errors.log
```

Expect 63/63 lifecycle, 75/75 storage and 21/21 modf, including their launch
assertions, with exit status zero. No Chromium launch or manual browser test is
required. Exact lifecycle expectations require the injected-clock driver; a
normal interactive browser session uses real clocks and cannot reuse those
expected timestamps.

For an optional manual storage check, upload
`build/system-tests/clang/time-profiler/probe.wasm` as `/tmp/time-profiler.wasm`
in `bash.html`, then run:

```sh
chmod +x /tmp/time-profiler.wasm
/tmp/time-profiler.wasm > /tmp/time-profiler.bin
printf '%s\n' "$?" > /tmp/time-profiler-status.txt
wast --verbose /root/test/system/clang/time-profiler/time-profiler.wast > /tmp/time-profiler.txt
download /tmp/time-profiler.txt
```

Expect `WAST: 74 PASS, 0 FAIL, 74 total`. The installed modf test also runs
normally with `wast --verbose /root/test/aux/libc/modf.wast` (20 PASS).
