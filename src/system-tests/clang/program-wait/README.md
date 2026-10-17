# LLVM child waiting

The observer executes the actual staged `llvm::sys::Wait` method through the
ordinary CRT, current C++ archives, shared libc and shared C process scheduler.
`program-wait.wast` owns 254 runtime assertions. They cover ordinary exit
statuses, LLVM's 126/127 error handling, ECHILD, unhandled SIGTERM, zero-time
polling, SIGALRM disposition preservation and indefinite EINTR retry with and
without SA_RESTART. Rejected statistics requests return
ENOSYS without reaping or killing the child; a later supported wait succeeds.
Statistics output is reset, including when no error-string pointer is supplied.

The guest-only LLVM adaptation uses existing `waitpid` for indefinite waits
and zero-time polling. CPU accounting is unavailable; there is no fabricated
`wait4` provider. Finite waits use the shared absolute-deadline API; their actual
archive-linked checks live in the [finite-wait fixture](../finite-wait/README.md).
`ExecuteAndWait` checks unavailable accounting/clocks before creating a child.
Native LLVM branches are preserved.

`prepare-wait.py` isolates the staged Wait definition, ProcessInfo constructor
and Unix error-message helper without editing them. It preserves the upstream
license, checks extraction boundaries, and records source/generated SHA-256
values under `build/system-tests/clang/program-wait`. The fixture compiles the
complete unchanged `Errno.cpp` for real error strings. It does **not** establish
complete Program.cpp compilation or execute LLVM's ExecuteAndWait/ExecuteNoWait
methods. Argument limits have a separate
[fixture](../argument-limits/README.md). Session detachment and finite timeouts
have separate archive-linked fixtures; requested statistics and full compiler/
linker acceptance remain unavailable. No fixture
fragment is used as a production replacement.

```sh
make -C src/system-tests test-program-wait CLANG_JOBS=1
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-program-wait CLANG_JOBS=1 \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect 254 passes and exit zero. Generated observations/results live under
`build/system-tests/clang/program-wait`; verification logs belong in
`build/engine/logs`. Run one CMake owner with one LLVM compile job. Keep VFS
installation separate from running guest readers.

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/program-wait/browser.json \
  > build/engine/logs/clang-browser-program-wait-results.json
```

Expect 283 worker passes including Bash launch. A GUI browser check is optional.
To check manually, open a fresh `bash.html`, run `upload /tmp/program-wait.wasm`
and select `build/system-tests/clang/program-wait/probe.wasm`, then run:

```sh
chmod +x /tmp/program-wait.wasm
/tmp/program-wait.wasm > /tmp/program-wait.bin
printf '%s\n' "$?" > /tmp/program-wait-status.txt
wast --verbose /root/test/system/clang/program-wait/program-wait.wast > /tmp/program-wait-results.txt
printf '%s\n' "$?"
download /tmp/program-wait-results.txt
```

Expect 254 passes and status zero; save downloaded evidence under `build/html-rt`.
