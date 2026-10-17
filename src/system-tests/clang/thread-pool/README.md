# LLVM deferred task results

The guest compiler profile disables LLVM and libc++ threads. LLVM's original
ThreadPoolInterface still requires standard futures in that profile. Its owned
patch selects the private `llvm::ThreadPoolFuture` implementation only for
`LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`. Native and threaded profiles retain
`std::shared_future` and `std::async(std::launch::deferred, ...)`.

The guest result owns a shared deferred callable and optional value, reference
or void result. Submission and handle destruction do not execute it. A future
wait/get or LLVM's queued thunk executes it once; copies share the result.
Value get returns a const reference, reference get preserves identity, and
move-only and over-aligned values keep their own lifetimes. Timed waits and
exception transport are outside this disabled-thread, disabled-exception
profile. This compiler-local queue does not implement WASTE kernel threading.

```sh
make -C src/system-tests test-thread-pool
make -C src/system-tests thread-pool-native-headers
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-thread-pool \
  WASM_RUNNER=../../build/cli-rt/wasm-sanitize
```

Finish any `make -C src/aux clang` attempt before these targets: their input
prerequisite may refresh the same guest configuration. The fixture compiles
actual staged LLVM `ThreadPool.cpp` and `Threading.cpp`, linking current guest
libc++/libc++abi archives and the production CRT/libc. Link-time optimization
removes their unused diagnostic paths using the real single-thread strategy;
no executor, constructor or provider is substituted. Static destructor
registration remains enabled; this fixture has no static object requiring it
and does not establish the still-pending C++ static shutdown provider.

All 79 functional assertions are in `thread-pool.wast`. The C++ observer records
submission order, sharing, move-only capture/result destruction, alignment,
references, bound arguments, void results, nested submissions and destruction.
SingleThreadExecutor's existing group wait drains all groups, including a
recursive group wait. A future get may execute its task before earlier queued
tasks; the later queued thunk sees completion and does not repeat it. The
native declaration gate checks standard return types and task instantiations
with the host's C++ library, including the guest/threaded preprocessor branch;
it makes no guest pthread capability claim.

After refreshing installed tests and rebuilding the page, run the same fixture
through the production Bash worker:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/thread-pool/browser.json \
  > build/engine/logs/clang-browser-thread-pool-results.json \
  2> build/engine/logs/clang-browser-thread-pool-errors.log
```

For an optional manual browser check, upload
`build/system-tests/clang/thread-pool/probe.wasm` to `/tmp/thread-pool.wasm` in
`bash.html`, then run:

```sh
chmod +x /tmp/thread-pool.wasm
/tmp/thread-pool.wasm > /tmp/thread-pool.bin
printf '%s\n' "$?" > /tmp/thread-pool-status.txt
wast --verbose /root/test/system/clang/thread-pool/thread-pool.wast > /tmp/thread-pool.txt
download /tmp/thread-pool.txt
```

Expect `WAST: 79 PASS, 0 FAIL, 79 total`. The worker adds the launch assertion.
These checks establish the task boundary, not runnable guest Clang/LLD.
