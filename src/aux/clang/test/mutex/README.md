# LLVM compiler-profile recursive locks

The bootstrap guest compiler and its libc++ are built with threads disabled.
LLVM's `SmartMutex` still names `std::recursive_mutex` unconditionally upstream.
The owned LLVM patch removes that dependency only for `LLVM_WASTE_GUEST` with
`LLVM_ENABLE_THREADS=0`, using LLVM's existing recursive acquisition counter.
Successful `try_lock` acquires a counted lock; scoped release and noncopyable
ownership remain enforced. Native and threaded profiles retain the upstream
implementation. These compiler-local counters provide no inter-thread exclusion
and must not be used to implement WASTE kernel or scheduler synchronization.

```sh
make -C src/aux test-mutex
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-mutex \
  WASM_RUNNER=../../build/cli-rt/wasm-sanitize
```

`mutex-fixture` compiles the real patched LLVM header with the generated guest
configuration and links the current bootstrap `libc++.a`. All 40 functional
assertions live in `mutex.wast`. The observer records profile flags, type
ownership, recursive lock/try/unlock results for both SmartMutex variants,
nested scope release, deferred/adopted standard lock wrappers and independent
once flags. `llvm::call_once` executes the real archive implementation, verifying
argument forwarding and suppression of repeated initialization. This is not
acceptance of owning strings, general C++ startup or other LLVM lock classes.

Reports and logs go under `build/system-tests/clang/mutex`. Refresh installed
snapshots and rebuild the production page in sequence:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/mutex/browser.json \
  > build/engine/logs/clang-browser-mutex-results.json
```

Expect 41 passing worker results including Bash bootstrap, and exit status zero.
No manual browser test is required for this compiled-header boundary. For an
optional manual check, upload `build/system-tests/clang/mutex/probe.wasm` as
`/tmp/mutex.wasm` in the rebuilt page, then run:

```sh
chmod +x /tmp/mutex.wasm
/tmp/mutex.wasm > /tmp/mutex.bin
printf '%s\n' "$?" > /tmp/mutex-status.txt
wast --verbose /root/test/system/clang/mutex/mutex.wast > /tmp/mutex-results.txt
echo "$?"
download /tmp/mutex-results.txt
```

Expect 40 passes and status zero. Save downloaded evidence under `build/html-rt`.
