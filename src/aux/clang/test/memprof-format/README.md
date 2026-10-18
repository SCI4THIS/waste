# MemProf graph identifiers

The actual MemProf callsite-context graph formatter uses LLVM's existing
`utohexstr` helper to form an owned `N0x...` identifier in the explicit
disabled-thread guest profile. Native and threaded profiles retain their
standard string streams.

The observer includes the complete production source and opens private access
only in its own compilation to call the actual helper. Production source
compilation retains normal access checks. Its 34 WAST assertions cover zero,
small values, lowercase hex, high-bit and maximum 32-bit pointer values for
both regular-LTO and ThinLTO graph specializations. Numeric boundary pointers
are never dereferenced. Additional cases use real graph nodes, shared edges,
sorted context IDs and clone attributes. WAST independently constructs expected
hex from captured real addresses and checks copied names and attributes after
all graph, node, edge and formatter owners are destroyed. This accepts graph
identifier/attribute formatting, rather than the MemProf optimization pass.

The native cli-rt test runs automatically through:

```sh
make -C src/aux test-memprof-format
```

Expect `WAST: 34 PASS, 0 FAIL, 34 total` and Make exit status zero. The
observer's intentional status 38 is checked inside WAST.

Complete source compatibility, sanitizer and packaged-worker reproduction:

```sh
make -C src/aux memprof-format-fixture memprof-format-native-source memprof-format-guest-source
make -C src/cli-rt BUILD_DIR=../../build/cli-rt ../../build/cli-rt/wasm-sanitize
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  bash src/aux/clang/test/memprof-format/run.sh "$PWD" "$PWD/src/vfs" \
  "$PWD/build/system-tests/clang/memprof-format" "$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install bash-html vfs-tests-check
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/memprof-format/browser.json
```

Source checks cover guest, native, native threaded and guest threaded profiles.
The sanitizer run expects 34 PASS, and the packaged worker expects 35/35
results including launch with exit zero. ASan/UBSan remain enabled; leak
checking is disabled for the ptraced runner. Keep reports under
`build/engine/logs`. The worker check uses the production packaged page
without launching a GUI browser.
