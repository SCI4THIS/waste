# LLVM raw-stream boundary

The guest Clang/LLD target graphs have no caller of `llvm::raw_os_ostream`,
the adapter to standard C++ streams. Its production caller is clangFormat;
other callers are test support or unselected projects. The guest Support
source list omits that adapter and declares it optional through LLVM's normal
source inventory mechanism. The socket-stream implementation is also optional
in the guest: its only upstream caller is the unselected LLVM Support unit test.
Both implementations remain in native builds. This leaves libc++ localization
disabled and preserves LLVM's own `raw_ostream` and proxy implementations.
Recheck selected consumers before adding compiler tools or enabling more projects.
The fixture below tests memory output; it does not verify socket or poll APIs.

`test-raw-stream` compiles actual staged `raw_ostream.cpp` and
`NativeFormatting.cpp`, without LTO, and links the current C++ archives, CRT
and shared libc. Its observer uses a nonterminal memory sink implementing
the abstract write/pwrite callbacks. The sink captures bytes and write counts;
LLVM owns buffering, flushes, stream positions, integer formatting, hexadecimal
output and escaping. A separate case uses actual `raw_string_ostream` with
heap-backed storage and independent copies.

Memory-stream vtables retain the base color methods. The fixture generator
isolates the unchanged ANSI constants from Process.cpp and helper definitions
from Unix/Process.inc, recording both complete source hashes. It fails if those
fragment boundaries change. This test-only translation unit provides the actual
color sequences without pretending to compile full Process.cpp or providing
its unavailable resource APIs. Production sources/providers are unchanged.
Terminal descriptors and full POSIX Process support remain unverified here.

All 51 functional assertions are in `raw-stream.wast`. The observer records
44 values plus an observation count in a 64-i32 block, followed by four
256-byte output slots (1,280 bytes total). WAST checks full output and untouched
slot tails, deferred writes, exact buffer boundaries, flushes during buffer
changes, pwrite while trailing bytes remain buffered, binary/NUL bytes,
maximal 64-bit integers, heap-backed
string copies and destruction flushing after input mutation.

```sh
make -C src/system-tests test-raw-stream
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-raw-stream \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Artifacts and native commands/results are under
`build/system-tests/clang/raw-stream`. To repeat without installation or
configuration, call `run.sh` with the repository root, VFS root, fixture build
directory and absolute runtime path. The Make target configures/builds its
shared aux inputs first; do not run another LLVM CMake owner concurrently.

Refresh and audit installed snapshots, then rebuild the page sequentially:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/raw-stream/browser.json \
  > build/engine/logs/clang-browser-raw-stream-results.json
```

Expect 52 worker passes, including Bash launch. A GUI check is optional. To
repeat manually in `bash.html`, upload
`build/system-tests/clang/raw-stream/probe.wasm` to `/tmp/raw-stream.wasm`:

```sh
upload /tmp/raw-stream.wasm
chmod +x /tmp/raw-stream.wasm
/tmp/raw-stream.wasm > /tmp/raw-stream.bin
printf '%s\n' "$?" > /tmp/raw-stream-status.txt
wast --verbose /root/test/system/clang/raw-stream/raw-stream.wast > /tmp/raw-stream.txt
echo "$?"
download /tmp/raw-stream.txt
```

Expect 51 passes and status zero; save downloaded evidence under
`build/html-rt`. This verifies the memory-output core. Actual LLVM descriptor
streams, terminal colors, error cleanup, standard iostreams, floating output,
C++ static-exit registration and complete compiler/linker execution remain
separate acceptance work. Normal static registration flags remain enabled.
