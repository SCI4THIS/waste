# LLVM process time and immediate exit

The fixture compiles the complete, unchanged LLVM `Process.cpp` from the staged
source with the current guest configuration, CRT and C++ archives. Public C++
static assertions check the time64 `timeval`/`rusage` layout and C declarations.
`process.wast` owns 55 runtime expectations over actual `getProcessId` and
`GetTimeUsage` calls, the retained resource-limit facade, and ordinary children
terminated through shared-libc `_Exit`, `_exit` and legacy `env._Exit`.
Children leave a blocked signal pending; the checks verify low-eight-bit wait
statuses, no signal-handler execution, pipe closure and parent survival.
The wall-time comparison floors its lower nanosecond sample to libc++'s
microsecond resolution, checked by a C++ type assertion. This accounts for
normal clock conversion precision without accepting an arbitrary time window.

The public wasm32 `rusage` is 88 bytes: two 16-byte time64 `timeval` records
followed by fourteen 32-bit `long` counters. Its fields follow the
[resource-usage interface](https://sourceware.org/glibc/manual/2.43/html_node/Resource-Usage.html).
Neither runtime currently supplies guest CPU accounting. Both shared libc and
the legacy env adapter return -1/ENOSYS for SELF/CHILDREN, or -1/EINVAL for an
invalid selector, without touching the output. Package `resource-usage.wast`
owns 42 checks, including canaries and null/overflowing output pointers.
The guest LLVM profile disables `HAVE_GETRUSAGE`; its existing fallback reports
wall time with zero CPU durations. Those zeros are LLVM's fallback, not
successful CPU-accounting observations.

`_Exit` reuses the existing engine `_exit` path, including child status and
descriptor teardown. It follows the
[immediate-exit interface](https://pubs.opengroup.org/onlinepubs/9799919799/functions/_exit.html).
This fixture does not establish `atexit`/C++ destructor registration, stream
flush behavior or actual LLVM `Process::Exit` crash-context handling. Those
methods are discarded by the linker's section collection; complete Support
linking and shutdown remain separate acceptance work. Only this diagnostic
fixture allows LLVM's expected compile warnings about unavailable CPU usage
and malloc statistics; production build flags are unchanged.

```sh
make -C src/system-tests test-clang-process CLANG_JOBS=1
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-clang-process CLANG_JOBS=1 \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect 55 passes and exit zero. Generated observations/results live under
`build/system-tests/clang/process`; verification logs belong in
`build/engine/logs`. Keep one CMake owner, one LLVM compile job, and VFS installs
separate from running guest readers.

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/process/browser.json \
  > build/engine/logs/clang-browser-process-results.json
```

Expect 98 worker passes including 42 package checks, 55 process checks and Bash
launch. A GUI test is optional. To check manually, open a fresh `bash.html`,
run `upload /tmp/process.wasm` and select
`build/system-tests/clang/process/probe.wasm`, then run:

```sh
chmod +x /tmp/process.wasm
/tmp/process.wasm > /tmp/process.bin
printf '%s\n' "$?" > /tmp/process-status.txt
wast --verbose /root/test/system/clang/process/process.wast > /tmp/process-results.txt
printf '%s\n' "$?"
download /tmp/process-results.txt
wast --verbose /root/test/aux/libc/resource-usage.wast
```

Expect 55 process passes, status zero and 42 package passes. Save downloaded
evidence under `build/html-rt`.
