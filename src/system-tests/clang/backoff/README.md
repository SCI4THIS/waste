# LLVM backoff on the guest POSIX runtime

The owned LLVM patch selects `llvm::detail::WasteBackoffRandom` and a
deadline-based POSIX wait only for `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`.
Native/threaded profiles retain `std::random_device` and
`std::this_thread::sleep_for`. LLVM's uniform distribution, exponential
multiplier, maximum interval and timeout calculation remain upstream code.
The guest backoff stays noncopyable.

The guest generator uses the existing shared-libc `getrandom` provider, which
currently supplies process-local xorshift compatibility bytes. These provide
non-security jitter and do not establish operating-system entropy. libc++'s
random-device feature stays disabled. `sys/random.h` has C++ C linkage, and
the SDK describes the provider's limitation explicitly.

The wait calls shared `nanosleep`, which yields through the engine's timed
readiness ABI. It recomputes the remaining interval from the monotonic clock
after EINTR because the current libc remainder is inaccurate. Other wait errors
or a failed random read abort through the real guest provider. No blocking
standard C++ thread implementation, fake clock or replacement executor is used.

```sh
make -C src/system-tests test-backoff backoff-native-headers
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-backoff \
  WASM_RUNNER=../../build/cli-rt/wasm-sanitize
make -C src/system-tests test-backoff-controlled
make -C src/system-tests test-backoff-controlled \
  BACKOFF_SESSION_RUNNER=../../build/cli-rt/private/guest-session-sanitize
```

Wait for any active aux Clang build attempt to finish before these targets;
their prerequisites may refresh the same guest configuration. The fixtures compile the actual staged
`ExponentialBackoff.cpp`, link current libc++/libc++abi archives and the production
CRT/libc, and retain static destructor registration. Their ordinary static data
does not establish the still-pending C++ shutdown provider.

The live fixture has 12 WAST assertions covering profile, random range/variation,
expired timeouts and an actual minimum sleep. The controlled fixture has 20
assertions: two exact one-second waits, a final half-second deadline clamp,
timeout completion, a caught signal and unchanged overall duration. Host events
try early wakeups and interrupt the second wait; WAST verifies recorded values.
The C++ observer records data only. The native declaration gate also compiles
the unchanged standard backends, including the guest/threaded branch.

After refreshing snapshots and rebuilding `bash.html`, run the live fixture
through the production worker with both live and controlled clocks:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/backoff/browser.json \
  > build/engine/logs/clang-browser-backoff-results.json
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/backoff/controlled.json \
  > build/engine/logs/clang-browser-backoff-controlled-results.json
```

The worker adds the launch assertion: expect 13 live and 21 controlled results.
The controlled scenario requires clock injection and must use its harness;
running its WAST alone with live clocks cannot reproduce its exact timestamps.
No manual browser check is required. For optional live evidence, upload
`build/system-tests/clang/backoff/probe.wasm` to `/tmp/backoff.wasm`, then run:

```sh
chmod +x /tmp/backoff.wasm
/tmp/backoff.wasm > /tmp/backoff.bin
printf '%s\n' "$?" > /tmp/backoff-status.txt
wast --verbose /root/test/system/clang/backoff/backoff.wast > /tmp/backoff.txt
echo "$?"
download /tmp/backoff.txt
```

Expect 12 passes and status zero; keep the download under `build/html-rt`.
These checks establish this backoff boundary, not complete Clang/LLD acceptance.
