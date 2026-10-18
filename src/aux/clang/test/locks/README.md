# Guest record locks

`locks.wast` owns 341 runtime expectations. It checks the fixed kernel, legacy
fixed env, explicit variadic kernel and shared-libc call boundaries, caller
memory, errno, access modes and invalid ranges. The ordinary C++ observer
checks process contention, shared readers, partial unlock/downgrade, negative
lengths and SEEK_CUR/SEEK_END, locks beyond EOF, close-any-alias release,
fork/exec/CLOEXEC ownership, inode aliases after rename/unlink/path reuse,
protected-page copies, exit/death, blocking wakeup, EINTR, SA_RESTART and a
process wait cycle returning EDEADLK. The restart handler seeks the descriptor;
the acquired lock must retain its original absolute range.

The public process-lock semantics follow the
[GNU libc file-lock reference](https://sourceware.org/glibc/manual/latest/html_node/File-Locks.html).
These are advisory locks. They do not prevent ordinary reads/writes. A child
has a new lock owner; an exec preserves its owner. Closing any descriptor for
an inode releases that process's locks on it. Other processes' locks survive.

`prepare-locks.py` extracts unchanged `tryLockFile`, `lockFile` and `unlockFile` methods from
the staged LLVM Unix/Path source, retaining its license and recording its hash.
The fixture links the current C++ archives and ordinary CRT. The existing
real diagnostic error-category object suppresses static destructor registration
only in this fixture; production archives keep normal flags. The
[timed-lock fixture](../timed-locks/README.md) checks `tryLockFile` and yielding
usleep. Full Path, C++ shutdown, Clang and LLD acceptance remain pending.

Guest `struct flock` is a 16-byte wasm32 record with signed 32-bit off_t fields.
Public constants and engine copy offsets share `waste/abi/fcntl.h`. Limits on
records per inode and the bounded deadlock walk use `POSIX_RECORD_LOCK_MAX`
in `src/config.h`. Admission failures return ENOLCK without publishing partial
changes. Blocking operations retain copied normalized interests in the existing
kernel wait record and resume with the SELECT scheduler already used by sleep;
no platform lock registry or new timer/scheduler is introduced.

```sh
make -C src/aux test-locks
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-locks \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect 341 WAST passes and zero exit status. Results and fixture outputs live
under `build/system-tests/clang/locks`. Run VFS installs and reads sequentially.
The private kernel sanitizer checks retained inode/owner identities, copied
wait references, teardown and configuration-derived admission limits.

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/locks/browser.json \
  > build/engine/logs/clang-browser-locks-results.json
```

Expect 342 worker passes including Bash launch. No GUI test is required for
this slice. Optional manual reproduction: open a fresh `bash.html`, run
`upload /tmp/locks.wasm`, select `build/system-tests/clang/locks/probe.wasm`,
and run:

```sh
chmod +x /tmp/locks.wasm
/tmp/locks.wasm > /tmp/locks.bin
printf '%s\n' "$?" > /tmp/locks-status.txt
wast --verbose /root/test/system/clang/locks/locks.wast > /tmp/locks-results.txt
printf '%s\n' "$?"
download /tmp/locks-results.txt
```

Expect 341 passes and zero status; save the download in `build/html-rt`.
Use a fresh page/session before repeating, because this fixture creates names.
