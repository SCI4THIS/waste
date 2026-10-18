# Shared child deadlines

`make -C src/aux test-child-deadline` runs 107 WAST assertions in
installed Bash using compiled C calls, production CRT and shared libc.
Use `WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"` after building
`make -C src/cli-rt applications-sanitize`; set
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`.
The observer records values only. Runtime expectations live in
`child-deadline.wast`; `declarations.cpp` checks public C++ linkage/signatures.

The public `waste_waitpid_until(pid, status, options, deadline_ns)` takes an
absolute CLOCK_MONOTONIC nanosecond deadline. A ready child is reaped even
at/past the deadline. Otherwise expiry returns zero without changing status,
errno or the child. WNOHANG is immediate; other options return EINVAL.
Positive PIDs select one child; 0/-1 select any child, matching the current
engine wait model; other negative PIDs return EINVAL. Stop/continue and group
waits are unavailable. Blocking calls without an installed clock return ENOSYS
before reaping; WNOHANG does not require a clock. The versioned
`waste_kernel.waitpid_until_v1` boundary returns negative errno; libc translates
failures to -1/errno. Expiry does not kill a child; callers own termination policy.

Live checks cover expiry, nonblocking queries, raw/public errors, status/errno
preservation, PID selection, later reaping, NULL status, protected/unmapped
output, caught/restarted signals, early child completion and independently
waiting parents with nested children.

`make -C src/aux test-child-deadline-controlled` checks 62 WAST
assertions through existing injected-clock sessions, with native ASan/UBSan
and browser API parity. The clock advances just before and exactly onto
deadlines. Checks cover explicit EINTR retries and SA_RESTART without extending
the deadline, independent active/masked alarms, UINT64_MAX, and a later sleep
proving the reaped child's deadline was removed. `controlled.json` carries
clock/input events and expected WAST totals, not syscall assertions.

After building fixtures and refreshing `bash.html`, run the offline worker:

```sh
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/child-deadline/browser.json \
  > build/engine/logs/clang-browser-child-deadline-results.json
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/child-deadline/controlled.json \
  > build/engine/logs/clang-browser-child-deadline-controlled-results.json
```

Manual Firefox verification is optional after worker acceptance. Open the
fresh offline page, upload `build/system-tests/clang/child-deadline/probe.wasm`
to `/tmp`, then run:

```sh
/tmp/probe.wasm > /tmp/child-deadline.bin
printf '%s\n' "$?" > /tmp/child-deadline-status.txt
wast --verbose /root/test/system/clang/child-deadline/child-deadline.wast > /tmp/child-deadline.txt
echo $?
download /tmp/child-deadline.txt
```

Expect 107 PASS, zero failures and exit zero. Save the download under
`build/html-rt`. Controlled clocks require the harness.

This accepts the shared primitive used by finite LLVM waits. Their polling
expiry and kill/exact-child-reap integration run through the actual Support
archive in the [finite-wait fixture](../finite-wait/README.md).
Statistics, child memory budgets and affinity remain unsupported.
