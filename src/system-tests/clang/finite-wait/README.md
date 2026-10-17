# Actual LLVM finite child waits

The observer links the real `libLLVMSupport.a` and `libLLVMDemangle.a`, current
C++ archives, production CRT and installed shared libc. Actual Wait,
ExecuteAndWait and ExecuteNoWait supply the behavior. No extracted method,
compiler-specific clock or replacement process provider is used.
All runtime expectations live in WAST; C++ records observations only.

`make -C src/system-tests test-finite-wait CLANG_JOBS=1` runs 217 WAST assertions
inside native Bash. For sanitizer execution, first build
`make -C src/cli-rt applications-sanitize`, then use:

```sh
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-finite-wait CLANG_JOBS=1 \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect 217 passes and exit zero. Logs and result JSON belong under `build/`.
Use one LLVM CMake owner and one compile job; finish installation before running
guest readers.

Wait computes one checked absolute monotonic deadline through the shared clock,
then calls `waste_waitpid_until`. Caught signals retry that same deadline.
Polling expiry returns PID/code zero and retains the live child. Ordinary
expiry sends SIGKILL and reaps only the target with waitpid, retrying EINTR.
The guest returns the reaped target PID and code -2. SIGALRM dispositions,
masks and independent alarms remain owned by the application; native LLVM
branches retain their implementation. The unchanged LLVM error helper formats
successful timeout termination as `Child timed out: ` with an empty error
description. Requested statistics remain unsupported before creating/reaping;
memory budgets and affinity remain unsupported before fork.

Live checks cover polling and later kill/reap, an unrelated ready sibling,
timeout without an error destination, early exits, UINT_MAX seconds, interrupted
and restarted finite waits, ExecuteAndWait success and sleeping/CPU-bound
timeouts, child liveness/absence of post-timeout side effects, ExecuteNoWait
followed by finite Wait, and normal missing-child errors.
The live worker manifest uses the production worker's ordinary cooperative
quantum from src/config.h, also used by native Bash and the browser suite. This
lets the scheduler resume the waiting parent while a CPU-bound child remains
runnable, without a test-only scheduling option. Pump-to-input transitions
publish the new wait kind through the ordinary worker readiness notification.

`make -C src/system-tests test-finite-wait-controlled` runs the same archive
methods with native sanitizer/browser API parity: 135 controlled WAST assertions
check just-before/exact expiry, masked pending alarms, both signal retry modes,
independent active alarms, exact-target kill/reap, ExecuteAndWait early completion/
timeout and cleanup before a later sleep. Another 32 WAST assertions start one
second below UINT64_MAX and prove that a maximum unsigned duration saturates
instead of wrapping. Injected nanosecond values use decimal JSON strings when
needed to retain uint64 precision in Python and JavaScript. These are harness
clock events; production timing uses the ordinary shared sleep scheduler.
The shared scheduler must distinguish a finite UINT64_MAX timer from an
indefinite pipe wait with the same comparison sentinel; the saturation scenario
checks that the timer is published and resumes at exact expiry.
LeakSanitizer can be disabled in the documented ptrace environment; ASan/UBSan
remain active. Set ASAN_OPTIONS explicitly to override the target's default.

After fixture builds and snapshot/page refresh, run the packaged offline worker:

```sh
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/finite-wait/browser.json \
  > build/engine/logs/clang-browser-finite-wait-results.json
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/finite-wait/controlled.json \
  > build/engine/logs/clang-browser-finite-wait-controlled-results.json
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/finite-wait/saturation.json \
  > build/engine/logs/clang-browser-finite-wait-saturation-results.json
```

Manual Firefox verification is optional after worker acceptance. Open the fresh
offline `bash.html`, upload `build/system-tests/clang/finite-wait/probe.wasm`
to `/tmp`, then run:

```sh
/tmp/probe.wasm > /tmp/finite-wait.bin
printf '%s\n' "$?" > /tmp/finite-wait-status.txt
wast --verbose /root/test/system/clang/finite-wait/finite-wait.wast > /tmp/finite-wait.txt
echo $?
download /tmp/finite-wait.txt
```

Expect 217 PASS and exit zero; save the download under `build/html-rt`.
Controlled/saturation cases require the harness. Full Clang/LLD binaries,
C++ startup/shutdown and crash recovery remain separate acceptance work.
