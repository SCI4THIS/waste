# Guest mapping advice

The shared libc `posix_madvise` wrapper calls the engine's versioned provider.
The engine validates every page in the rounded range, including `PROT_NONE`,
and preserves data, permissions, dirty state and backing identity. Optimization
is optional: existing pages are resident or demand-zero. Returns are zero or
positive error numbers; errno is preserved. Invalid advice, unaligned addresses
and zero lengths return EINVAL; any unmapped portion returns ENOMEM.
This follows the [POSIX advisory contract](https://pubs.opengroup.org/onlinepubs/009604399/functions/posix_madvise.html).
Linux's destructive `madvise(MADV_DONTNEED)` is not advertised.

`prepare-mapping.py` extracts unchanged, licensed methods from the staged LLVM
Unix/Path.inc and Path.cpp, recording both source hashes. The ordinary shared CRT
executes actual mapping construction, the three LLVM advice methods, accessors,
move/unmap, default-object guards and failed construction. `advice.wast` checks
38 observations. `ranges.cpp` captures public C++ calls for anonymous and file
mappings; `ranges.wast` owns 161 assertions covering all five hints, page rounding,
errors/errno, inaccessible pages, holes, adjacent mappings, unsigned arguments,
private modifications, shared aliases, dirty writeback, truncation and retained
backing after close/unlink. Observation programs carry no functional assertions.
The fixture explicitly imports libc advice and retains the legacy env adapter;
its import rewrite checks both signatures against the actual public provider.

WAST handlers have no application VM capsule. The mapping operations therefore
execute in an ordinary guest process; WAST reads the resulting files. Private
process lifecycle checks cover opaque page/refcount/VMA/protection invariants.
This fixture does not execute all LLVM Path methods or establish C++ shutdown:
it reuses the platform fixture's real error-category object with diagnostic-only
`-fno-c++-static-destructors`. Production libc++ archives retain registration;
proper static-exit registration remains a separate required implementation.

```sh
make -C src/aux test-advice
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-advice \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect 38 and 161 passes, exit zero. Artifacts and session JSON are under
`build/system-tests/clang/advice`. Run installation, snapshot refresh/audit,
packaging and guest sessions sequentially. Production worker verification:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/advice/browser.json \
  > build/engine/logs/clang-browser-advice-results.json
```

Expect 200 worker passes including Bash launch. Optional manual check: open a
fresh `bash.html`, run `upload /tmp/advice.wasm`, and select
`build/system-tests/clang/advice/probe.wasm`. Then:

```sh
chmod +x /tmp/advice.wasm
/tmp/advice.wasm > /tmp/advice.bin
printf '%s\n' "$?" > /tmp/advice-status.txt
wast --verbose /root/test/system/clang/advice/advice.wast > /tmp/advice-results.txt
printf '%s\n' "$?"
wast --verbose /root/test/system/clang/advice/ranges.wast > /tmp/advice-ranges-results.txt
printf '%s\n' "$?"
download /tmp/advice-results.txt
download /tmp/advice-ranges-results.txt
```

Expect 38 and 161 passes and zero statuses. Save optional downloads under
`build/html-rt`. A complete guest LLVM build must pass the mapping advice compile
boundary; realpath has its own system fixture. Descriptor locks, yielding usleep
and static exit registration remain separate required capabilities.
