# LLVM lock-owner process queries

The guest-only LockFileManager patch calls the private LLVM-owned
`wasteLockOwnerStillExecuting` helper. It uses the actual shared hostname
provider and a positive-PID `kill(pid, 0)` query against the engine process table.
Native LLVM retains its original `getsid` path. No session-ID provider or host
PID lookup is added.

A local owner is gone only on `-1/ESRCH`. A different host, failed/empty identity
lookup or any other query error conservatively retains the owner. Negative local
owner IDs are invalid and never become process-group queries; zero retains the
original current-process interpretation. Guest lock creation also propagates a
hostname-provider error instead of writing an empty identity.

The kernel query generates no signal and does not cancel a wait, change a mask,
close descriptors or consume a child status. An unreaped zombie still owns a PID;
after waitpid reaps it the query returns `-1/ESRCH`. Invalid signal numbers return
`-1/EINVAL`. The public `ESRCH` constant and libc++'s actual `errc` mapping agree
with the kernel, and shared `strerror` supplies the corresponding message.
See [POSIX kill](https://pubs.opengroup.org/onlinepubs/009604499/functions/kill.html)
for the null-signal and error contract. This boundary does not establish
cross-user permissions, all-process broadcast selection or session IDs.

Wait for any active aux Clang build attempt to finish, then run:

```sh
make -C src/system-tests test-lock-owner lock-owner-native-source
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-lock-owner \
  WASM_RUNNER=../../build/cli-rt/wasm-sanitize
```

The C++ observer records values only. All 83 functional assertions are in
`lock-owner.wast`, covering live/missing owners, stale errno, remote identities,
invalid IDs/signals, group queries, blocked-pending signals, a child waiting on
a pipe, zombie/reaped states, hostname-provider errors, libc++ errno mapping
and the error message. Pipe EOF proves exit before the zombie query; waitpid
then verifies the original raw status. The identity-error case exercises the
real libc provider with an oversized hostname, restoring it afterward.

The fixture uses the actual private helper consumed by LockFileManager, generated
LLVM/libc++ headers and production CRT/libc. It requires no C++ archive symbols
and does not claim complete LLVM file-lock acquisition, contention, stale-file
cleanup or C++ shutdown. Full-source guest and native compiles separately check
the actual LockFileManager implementation; complete runtime acceptance needs
its remaining Support dependencies.

After refreshing snapshots and rebuilding the self-contained page:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/lock-owner/browser.json \
  > build/engine/logs/clang-browser-lock-owner-results.json
```

Expect 84 production-worker results including launch, status zero and empty
stderr. No manual browser check is required. For optional live evidence, upload
`build/system-tests/clang/lock-owner/probe.wasm` to `/tmp/lock-owner.wasm`, then:

```sh
chmod +x /tmp/lock-owner.wasm
/tmp/lock-owner.wasm > /tmp/lock-owner.bin
printf '%s\n' "$?" > /tmp/lock-owner-status.txt
wast --verbose /root/test/system/clang/lock-owner/lock-owner.wast > /tmp/lock-owner.txt
echo "$?"
download /tmp/lock-owner.txt
```

Expect 83 passes and status zero; save downloaded evidence under `build/html-rt`.
