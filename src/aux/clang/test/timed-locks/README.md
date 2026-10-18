# LLVM timed locks and microsecond sleep

`probe.cpp` captures public C++ calls and unchanged LLVM `tryLockFile`,
`lockFile` and `unlockFile` methods extracted from staged Unix/Path.inc by the
record-lock fixture. `timed-locks.wast` owns 53 expectations: the public
useconds_t/signature boundary, zero/invalid sleep arguments, immediate locking,
invalid descriptors, concurrent shared readers, zero-timeout contention,
finite timeout after a caught signal, and acquisition after another process
releases its lock. Elapsed-time checks use lower bounds; scheduler overhead
can lengthen a wait.

The controlled variant owns 25 WAST checks. The same existing native/browser
clock controls advance monotonic time to just before and then onto deadlines
for 1, 1000 and 999999 microseconds. Early resumes must stay blocked, success
preserves errno, and a caught SIGUSR1 interrupts a fourth wait with EINTR.
It uses the production libc, pselect boundary and engine continuation, without
a fixture clock or sleep implementation. Zero and invalid intervals have no
wait events. All runtime expectations are in WAST.

The shared libc follows the legacy
[POSIX usleep contract](https://pubs.opengroup.org/onlinepubs/009696899/functions/usleep.html):
zero has no effect and arguments of one million microseconds or more return
EINVAL. Longer intervals use nanosleep. The wrapper converts microseconds to
the existing timespec ABI and propagates interruption. It introduces no new
kernel wait reason, runtime import or platform timer. LLVM's unchanged method
retries its 1000-microsecond wait against its original steady-clock deadline.

```sh
make -C src/aux test-timed-locks CLANG_JOBS=1
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-timed-locks CLANG_JOBS=1 \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  make -C src/aux test-usleep-controlled CLANG_JOBS=1
```

Expect 53 native passes and 26 controlled session passes including Bash launch.
Generated inputs and native results are under `build/system-tests/clang/timed-locks`;
keep verification logs under `build/engine/logs`. CMake build/configuration must
have one owner, with one LLVM compile job. Run VFS installs and reads sequentially.
The diagnostic error-category object alone suppresses static destructor
registration; production C++ archives keep ordinary flags. Complete Path
execution, C++ shutdown and Clang/LLD acceptance remain separate work.

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/timed-locks/browser.json \
  > build/engine/logs/clang-browser-timed-locks-results.json
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  python3 src/engine/test/guest-session/guest-session-check.py --scenario clang-usleep \
  --native build/cli-rt/private/guest-session-sanitize --vfs-root src/vfs \
  --wasm build/html-rt/waste-wast.wasm --page build/html-rt/bash.html \
  > build/engine/logs/clang-usleep-controlled-parity.json
```

Expect 63 live worker passes including the nine package usleep checks and Bash
launch, and 26 controlled passes in each backend. A GUI check is optional.
For a manual check, open a fresh `bash.html`, run `upload /tmp/timed-locks.wasm`
and select `build/system-tests/clang/timed-locks/probe.wasm`. Then run:

```sh
chmod +x /tmp/timed-locks.wasm
/tmp/timed-locks.wasm > /tmp/timed-locks.bin
printf '%s\n' "$?" > /tmp/timed-locks-status.txt
wast --verbose /root/test/system/clang/timed-locks/timed-locks.wast > /tmp/timed-locks-results.txt
printf '%s\n' "$?"
download /tmp/timed-locks-results.txt
```

Expect 53 passes and status zero; save downloads under `build/html-rt`.
The installed package `/root/test/aux/libc/usleep.wast` can also run directly
and should report nine passes. The controlled WAST requires its injected-clock
observer and scenario, so use the automated controlled target for that evidence.
