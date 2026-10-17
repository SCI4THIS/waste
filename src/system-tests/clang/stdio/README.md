# Compiled variadic formatter boundary

`make -C src/system-tests test-stdio` builds and runs real guest C and C++
clients of the installed shared `vsnprintf`. This prerequisite does not need
guest Clang/LLD to have built: the host Clang cross-compiles both inputs against
the public guest SDK, with libc++'s generated headers for `<cstdio>`.

`probe.h` only records observations. Both clients call the formatter twice
with the same `va_list`, then read its original first argument. Eight cases
capture full output, truncation, exact fit, one-byte output, zero size with a
nonnull/null destination, empty format and UTF-8 byte counts. Eight further
cases use the public `PRIX8/16/32/64` macros: zero/maximal values, small unsigned
integer promotions, high bits, alternating 32/64-bit argument alignment,
zero padding, truncated 64-bit output and uppercase/lowercase output. The C++
client also includes `<cinttypes>`. They capture
the return counts, errno and both complete output buffers with surrounding
sentinels. The 176-byte observation record has four guest i32 fields followed
by two 80-byte buffers; these are fixture wire layouts, not host structures.

`format.wast` owns 35 assertions: capture lengths, both process exit
statuses, and sixteen cases for each language. It verifies full return lengths,
termination, every unwritten byte, unchanged errno and preserved argument cursor.
The C++ fixture calls `std::vsnprintf`; it uses explicit C linkage for `main`
to match the shared C CRT under `-ffreestanding`. The ordinary shared-import
tool assigns raw SDK imports to the real libc provider, then checks their
signatures. No formatter or second libc is embedded in either probe.

```sh
make -C src/system-tests test-stdio
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-stdio \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Outputs and commands/results live under `build/system-tests/clang/stdio`.
Use the common native `wasm`/`wasm-sanitize` transports; complete installation
before launching tests that read `src/vfs`. To repeat against a stable tree
without an installation prerequisite, call `run.sh` with repository root,
VFS root, fixture build directory and the absolute runtime executable path.

Refresh installed snapshots and rebuild the offline page, then check the
same compiled C/C++ boundary plus ten package formatter assertions:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/stdio/browser.json \
  > build/engine/logs/clang-browser-stdio-results.json
```

Expect 50 passing worker results, including ten package assertions and the Bash
bootstrap. The probes do
not link libc++'s runtime archives or exercise C++ static initialization.
Formatting coverage applies to the existing shared integer/string subset;
it does not establish support for every printf conversion or flag.

For an optional manual browser check, first upload the two host-built probes:

```sh
upload /tmp/format-c.wasm
chmod +x /tmp/format-c.wasm
upload /tmp/format-cxx.wasm
chmod +x /tmp/format-cxx.wasm
/tmp/format-c.wasm > /tmp/format-c.bin
printf '%s\n' "$?" > /tmp/format-c-status.txt
/tmp/format-cxx.wasm > /tmp/format-cxx.bin
printf '%s\n' "$?" > /tmp/format-cxx-status.txt
wast --verbose /root/test/system/clang/stdio/format.wast > /tmp/stdio-results.txt
echo "$?"
download /tmp/stdio-results.txt
```

Select `build/system-tests/clang/stdio/probe-c.wasm` and `probe-cxx.wasm` in
the respective upload dialogs. Expect 39 passes and status zero; keep
downloaded evidence under `build/html-rt`. The ten package-only assertions
can run directly with `wast --verbose /root/test/aux/libc/vsnprintf.wast`.
Manual browser evidence is optional for this SDK/compiler prerequisite.
