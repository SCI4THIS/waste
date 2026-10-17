# C++ static cleanup through shared libc

The compiled observer uses ordinary static-destructor registration, the current
LLVM Support/Demangle and libc++/libc++abi archives, production CRT and shared
libc. All expectations live in `shutdown.wast`: 305 assertions over 277 recorded
values, exact capture lengths and nine exit statuses. The second binary keeps
legacy `env.exit`, `env.atexit`, `env.__cxa_atexit` and `env.__cxa_finalize`
imports to verify that they reach the same process-local libc implementation.
The shell driver only runs commands and captures results: 610 assertions total.

Actual heap-backed global and function-local C++ objects verify reverse cleanup
on return from main and explicit exit. C callbacks registered during cleanup
run in order; their `usleep` uses the existing SELECT/timer continuation.
Explicit DSO tokens verify filtered, repeated and recursive finalization,
registration during finalization, invalid registrations and 64 retained entries.
Forked cleanup uses each process's inherited list and private state. `_Exit`
and guest SIGABRT bypass callbacks. Successful exec discards old registrations;
rejected executable preflight preserves the old heap and callback list.

The cache case executes the actual LLVM `PerThreadAllocator` template. Wrappers
observe malloc/free and delegate to the real shared allocator; they do not
replace allocation or destruction. A callback registered before the cache
records that its function-local vector's backing allocation was freed at exit,
after allocator ownership ended and before earlier global registrations run.

```sh
make -C src/system-tests test-shutdown CLANG_JOBS=1
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-shutdown CLANG_JOBS=1 \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect two `WAST: 305 PASS, 0 FAIL, 305 total` summaries and command status zero.
Bash also prints two intentional guest ABORT diagnostics. Results remain under
`build/system-tests/clang/shutdown/run-*`. Finish installations before starting
guest readers and use one LLVM build owner/job.

```sh
make -C src/html-rt vfs-tests-install vfs-tests-check bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/shutdown/browser.json \
  > build/engine/logs/clang-browser-shutdown-results.json \
  2> build/engine/logs/clang-browser-shutdown.log
```

Expect 611 passes including Bash launch, final status zero. No manual browser
test is required. For optional Firefox evidence, upload both compiled binaries
from `build/system-tests/clang/shutdown` and this fixture's `commands.sh` as
`/tmp/shutdown.wasm`, `/tmp/shutdown-legacy.wasm` and
`/tmp/shutdown-commands.sh` in the rebuilt `bash.html`, then run:

```bash
chmod +x /tmp/shutdown.wasm /tmp/shutdown-legacy.wasm
cat /root/test/system/clang/shutdown/shutdown.wast > /tmp/shutdown.wast
. /tmp/shutdown-commands.sh > /tmp/shutdown-results.txt 2>&1
echo "$?"
download /tmp/shutdown-results.txt
```

Save downloaded evidence under `build/html-rt`. Buffered stream flush,
concurrent registration from multiple guest threads and automatic C++ DSO
finalization on dlclose are separate acceptance work. The compiler bootstrap
profile remains single-threaded; the engine scheduler is unchanged. Explicit
`__cxa_finalize(dso)` is covered. Full LLVM TimeProfiler lifecycle and
Clang/LLD execution are not established by this fixture.
