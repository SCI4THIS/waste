# LLVM ordinary signals and shared libc registration

`signals.wast` owns 136 runtime assertions. The ordinary C++ observer calls
public `signal`/`sigaction` and actual staged LLVM info, interrupt, pipe,
registration, unregister and temporary-file cleanup methods. It checks
persistent callbacks, errno preservation, action masks/flags, aliased buffers,
rejected flags and ranges, legacy/versioned layouts, file-list erase/reuse,
regular-only deletion, ignored utility dispositions and forked child signal
statuses. Compiled callbacks and terminal/application state use the existing
shared kernel; no compiler-specific scheduler or platform signal shim is used.

`prepare-signals.py` selects uniquely bounded source sections from staged
`Signals.cpp` and `Unix/Signals.inc`, preserves their upstream license and
records source/generated hashes beside `methods.cpp`. It compiles the real
`ManagedStatic.cpp`, current C++ archives and normal three-argument CRT.
A diagnostic bridge calls the real private utility registration function.
The unchanged callback storage/RunSignalHandlers implementation is included,
but callback insertion/fatal-error support, crash recovery, fault contexts,
stack symbolication and full Support linking are outside this fixture. No
substitute implementation supplies those dependencies. Production retains the
complete Signals.cpp; its whole guest/native compilation is checked separately.

Guest LLVM uses ordinary one-argument handlers with flags zero. It restores
previous dispositions and explicitly re-raises guest signals after cleanup.
The SDK aliases SIGINFO to SIGUSR1, so guest registration records it once.
Utility registration records only successful nonignored installations.
Native LLVM keeps its original siginfo/alternate-stack branches. Guest machine
backtraces are disabled without disabling the real dlopen provider.

Public `sigaction` translates the fixed 24-byte versioned kernel boundary's
negative errno values into -1/errno. Unknown flags and SIG_ERR handlers fail
with EINVAL before output/state changes; bad ranges return EFAULT.
`signal` installs a persistent SA_RESTART action and returns the previous
handler. Legacy table-free WAT keeps its 20-byte prefix and -2 SIG_IGN sentinel;
the public versioned API normalizes that sentinel to the SDK's value one.
SIGKILL/SIGSTOP cannot be caught. siginfo callbacks and alternate stacks remain
unavailable. Signal handlers may not suspend. Forked applications retain their
existing application flag independently of executable-image ownership; queued
signals unblocked by handler return are delivered before application code resumes.

```sh
make -C src/system-tests test-signals CLANG_JOBS=1
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-signals CLANG_JOBS=1 \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect 136 passes and exit zero. Results go under
`build/system-tests/clang/signals/run-*`. Keep one CMake owner/LLVM build job;
finish all VFS installations before starting guest readers.

```sh
make -C src/html-rt vfs-tests-install vfs-tests-check bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/signals/browser.json \
  > build/engine/logs/clang-browser-signals-results.json
```

Expect 137 passes including Bash launch. No manual browser test is required.
For an optional check, open the freshly generated bash.html and upload
`build/system-tests/clang/signals/probe.wasm` as `/tmp/signals.wasm`, then run:

```sh
chmod +x /tmp/signals.wasm
/tmp/signals.wasm > /tmp/signals.bin
printf '%s\n' "$?" > /tmp/signals-status.txt
wast --verbose /root/test/system/clang/signals/signals.wast > /tmp/signals-results.txt
printf '%s\n' "$?"
download /tmp/signals-results.txt
```

Expect 136 passes and status zero; save downloaded evidence in build/html-rt.
