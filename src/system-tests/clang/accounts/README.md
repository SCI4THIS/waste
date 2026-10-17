# Guest reentrant account lookup

`probe.cpp` records public C++ calls through the ordinary shared CRT, installed
libc and current libc++ archives. `accounts.wast` owns all 47 expectations:
the 28-byte Wasm passwd layout, C linkage, caller-owned strings, independent
results, account replacement, missing entries, buffer/record preservation,
direct error codes, errno, and sysconf selectors. This is a compiler-boundary
fixture, not execution of the full LLVM Path methods. Package behavior checks
live in `src/aux/libc/tests/reentrant-accounts.wast` (84 assertions).

```sh
make -C src/system-tests test-accounts
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-accounts \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect 47 passes and exit zero. Artifacts and session JSON live under
`build/system-tests/clang/accounts`. Run installation, snapshot refresh/audit,
HTML packaging and guest sessions sequentially. For the production worker:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/accounts/browser.json \
  > build/engine/logs/clang-browser-accounts-results.json
```

Expect 48 worker passes, including Bash launch. For an optional manual browser
check, open a fresh `bash.html`, run `upload /tmp/accounts.wasm`, and select
`build/system-tests/clang/accounts/probe.wasm`. Then run:

```sh
chmod +x /tmp/accounts.wasm
/tmp/accounts.wasm > /tmp/accounts.bin
printf '%s\n' "$?" > /tmp/accounts-status.txt
wast --verbose /root/test/system/clang/accounts/accounts.wast > /tmp/accounts-results.txt
printf '%s\n' "$?"
download /tmp/accounts-results.txt
```

Expect 47 passes and a zero status. The package check can also run directly:

```sh
wast --verbose /root/test/aux/libc/reentrant-accounts.wast > /tmp/reentrant-accounts-results.txt
printf '%s\n' "$?"
download /tmp/reentrant-accounts-results.txt
```

Save optional downloads under `build/html-rt`. LLVM's account-query declarations
are required; the next full-source build must demonstrate that their compile
errors are gone. Mapping advice and realpath have their own system fixtures;
descriptor locks and yielding usleep remain required providers. C++ static exit
registration is also pending.

`package-browser.json` runs the 84 package checks through the same worker
transport and expects 85 passes including Bash launch. Use it in place of
`browser.json` and save the report to a distinct filename. The shared package
runner stages tests directly under the existing `/tmp` directory; explicit file
staging does not create parent directories.
