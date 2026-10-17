# Shared process sessions and terminal detachment

The C++ observer uses public guest headers, the ordinary three-argument CRT,
shared libc and current C++ archives. `session.wast` owns 86 runtime assertions.
It checks PID/session/group queries, errno preservation, invalid inputs, group
leader rejection, child detachment, retained descriptors, nested fork/exec,
repeat rejection, zombie/reaped identities and unchanged parent terminal state.
The linker rejects warnings, including mismatched CRT/main signatures.

`setsid` uses the existing engine process table: reject an allocated group with
the caller's PID, then atomically set its SID/PGID and clear controlling-terminal
ownership. Fork inherits this state and exec preserves it. The inherited terminal
OFD retains its original session and foreground group. Physical `isatty`, I/O,
termios and descriptor flags remain usable; controlling-terminal queries and
mutations fail after detachment. Public `tcgetpgrp` now reports real ownership,
EBADF for closed descriptors and ENOTTY for streams/detached terminals, replacing
its old constant-one answer. See the POSIX
[session interface](https://pubs.opengroup.org/onlinepubs/9699919799/functions/setsid.html)
and [terminal query](https://pubs.opengroup.org/onlinepubs/9699919799/functions/tcgetpgrp.html).

The legacy raw env setpgid/tcsetpgrp conventions remain compatible, with checks
preventing mutations across sessions or joining another session's existing group.
This fixture does not establish a complete POSIX setpgid or job-control backend.
It does not add controlling-terminal acquisition, session-leader exit hangups,
orphan-group stop/continue, CPU accounting or LLVM positive wait deadlines.
Native capture uses streams; the production worker also verifies physical terminal
aliases. Private sanitizer guards in `tests/c-engine-process-lifecycle.c` check
OFD reference ownership, queued input and host foreground-signal routing with an
artificial group-ID collision, which guest syscalls cannot create. They also
check routing after the detached child closes all terminal aliases and safe
delivery when the final device reference is released by its owner's exit.

```sh
make -C src/system-tests test-session CLANG_JOBS=1
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-session CLANG_JOBS=1 \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect 86 passes and exit zero. Results go under
`build/system-tests/clang/session/run-*`. Keep one CMake owner/LLVM build job
and finish VFS installation before starting guest readers. This tests public
process operations; actual LLVM ExecuteAndWait/ExecuteNoWait and full compiler
startup/shutdown remain acceptance work. Complete guest Program.cpp compilation
is checked separately; Support currently first fails in Watchdog.cpp on alarm.

```sh
make -C src/html-rt vfs-tests-install vfs-tests-check vfs-install-app bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/session/browser.json \
  > build/engine/logs/clang-browser-session-results.json
```

Expect 87 passes including Bash launch. No GUI test is required for this slice.
For an optional manual check, open the newly generated `bash.html`, upload
`build/system-tests/clang/session/probe.wasm` to both `/tmp/session.wasm` and
`/tmp/session-copy.wasm`, then run:

```sh
chmod +x /tmp/session.wasm /tmp/session-copy.wasm
/tmp/session.wasm > /tmp/session.bin
printf '%s\n' "$?" > /tmp/session-status.txt
wast --verbose /root/test/system/clang/session/session.wast > /tmp/session-results.txt
printf '%s\n' "$?"
download /tmp/session-results.txt
```

Expect 86 passes and status zero; save evidence under `build/html-rt`.
