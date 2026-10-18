# Shared process alarms and LLVM Watchdog

The observer compiles the complete staged upstream `Watchdog.cpp`, including
its Unix implementation, with the ordinary CRT and production shared libc.
There is no extracted replacement or compiler-local timer. `watchdog.wast`
owns 65 live runtime assertions; `controlled.wast` owns 47 assertions at exact
injected clock boundaries. C++ records observations and checks public types.

Each process has one alarm on the same monotonic clock and scheduler used by
sleep and timed readiness. Replacement returns remaining seconds rounded up;
zero cancels the timer but does not remove an already pending SIGALRM. Fork
clears the child's timer; exec retains the timer and resets caught actions.
Expiry respects masks and dispositions. Blocking reads/waits use their existing
EINTR handling; CPU-bound guest code handles signals at bounded opcode polls.
Handlers still execute synchronously and cannot suspend.
Unblocking a pending application signal delivers it before `sigprocmask`
returns, rather than letting that old notification interrupt a later READ.
This follows the [POSIX signal-mask boundary](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_sigmask.html).

The retained raw `read` provider returns negative EINTR and preserves errno;
the fixture records that existing boundary explicitly. `waitpid` uses its
public -1/errno boundary. Child observations travel through pipes, avoiding
the separately documented forked regular-file offset limitation.

The widened `waste_kernel.alarm_v1` result distinguishes UINT_MAX from negative
errno. Public libc preserves errno on success. Production runtimes install a
clock; a clockless private sandbox returns ENOSYS through the kernel boundary
(the unsigned public wrapper returns zero and sets errno). Nanosecond deadline
overflow saturates at UINT64_MAX, as for other shared waits. No host SIGALRM,
interval timer suite, pause or signal-mask checkpoint capability is added.

```sh
make -C src/aux test-watchdog CLANG_JOBS=1
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-watchdog CLANG_JOBS=1 \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-watchdog-controlled CLANG_JOBS=1
```

Expect 65 live passes and exit zero. Results are under
`build/system-tests/clang/watchdog/run-*`. The controlled transport exercises
the native sanitizer and browser API, checking below-deadline readiness,
masked expiry, retained pending signals, remaining seconds and interrupted
READ. Its 48 passes include the launch assertion. Controlled tests require
clock injection; they are not standalone manual browser tests.

Keep one CMake owner/LLVM job and finish VFS installation before guest readers.
To verify the same tests in the production packaged worker without a GUI:

```sh
make -C src/html-rt vfs-tests-install vfs-tests-check bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/watchdog/browser.json \
  > build/engine/logs/clang-browser-watchdog-results.json
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/watchdog/controlled.json \
  > build/engine/logs/clang-browser-watchdog-controlled-results.json
```

Expect 66 and 48 passes including Bash launch, with empty stderr.
An optional Firefox check uses the generated `bash.html`. Upload
`build/system-tests/clang/watchdog/probe.wasm` to `/tmp/watchdog.wasm`, then:

```sh
chmod +x /tmp/watchdog.wasm
/tmp/watchdog.wasm > /tmp/watchdog.bin
printf '%s\n' "$?" > /tmp/watchdog-status.txt
wast --verbose /root/test/system/clang/watchdog/watchdog.wast > /tmp/watchdog-results.txt
printf '%s\n' "$?"
download /tmp/watchdog-results.txt
```

Expect 65 passes and status zero; save evidence in `build/html-rt`.
Whole-source compilation and these runtime tests establish Watchdog's alarm
boundary, not complete LLVM child-execution or Clang/LLD acceptance.
