# Public execution and child waiting

The compiled C++ observer uses public guest headers, the ordinary CRT, shared
libc and the current C++ archives. `exec-wait.wast` owns 234 runtime assertions:
exact-path failures without PATH search, invalid guest pointers, successful
absolute/relative replacement, copied argv, inherited/empty/explicit environments,
stable PID, inherited descriptors and CLOEXEC. It also checks any-child ordering,
WNOHANG, NULL/write-only status, interruption/restart, single consumption, parent
survival and invalid/protected/cross-page destinations that preserve a zombie.
Its pipe EOF check establishes termination before testing those failures.

`execv` delegates to `execve` with the current public `environ`. `wait` delegates
to the existing env waitpid adapter with PID -1 and options zero. These follow
the [execution](https://pubs.opengroup.org/onlinepubs/9799919799/functions/exec.html)
and [waiting](https://pubs.opengroup.org/onlinepubs/9699919799/functions/wait.html)
interfaces; no alternate process table or scheduler is used. Invalid vector
pointers return EFAULT; actual argument-budget exhaustion remains E2BIG.
Before reaping, the engine validates the full status destination and prepares
lazy/copy-on-write pages without modifying bytes. Resumes revalidate the saved
integer address; no guest pointer survives a yield. Signals and the observer's
microsecond waits use the existing engine signal/sleep continuation paths.

```sh
make -C src/aux test-exec-wait CLANG_JOBS=1
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-exec-wait CLANG_JOBS=1 \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect 234 passes and exit zero. Generated results are under
`build/system-tests/clang/exec-wait`; keep logs under `build/engine/logs`.
Keep one CMake owner and one LLVM compile job; install the VFS separately
from running guest readers. This fixture checks public APIs, not actual LLVM
Program methods. Those still require its wait/statistics, session, timeout and
argument-limit boundaries. CPU accounting, C++ shutdown and full Clang/LLD
linking remain separate acceptance work.

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/exec-wait/browser.json \
  > build/engine/logs/clang-browser-exec-wait-results.json
```

Expect 235 passes including Bash launch. A GUI check is optional. For a manual
check, open a fresh `bash.html`, run `upload /tmp/exec-wait.wasm` and
`upload /tmp/exec-wait-copy.wasm`, selecting
`build/system-tests/clang/exec-wait/probe.wasm` for both uploads, then run:

```sh
chmod +x /tmp/exec-wait.wasm /tmp/exec-wait-copy.wasm
/tmp/exec-wait.wasm > /tmp/exec-wait.bin
printf '%s\n' "$?" > /tmp/exec-wait-status.txt
wast --verbose /root/test/system/clang/exec-wait/exec-wait.wast > /tmp/exec-wait-results.txt
printf '%s\n' "$?"
download /tmp/exec-wait-results.txt
```

Expect 234 passes and status zero; save evidence under `build/html-rt`.
