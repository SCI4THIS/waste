# LLVM GSYM conversion and creator ownership

The full LLD build exposed `std::mutex` owners in the actual DWARF transformer
and GSYM creator. The disabled-thread guest profile uses LLVM's existing
`sys::SmartMutex<true>`; native and threaded profiles retain their standard
mutexes. Lock guards infer the selected type. All upstream changes are staged
from the repository patch, leaving the LLVM submodule unchanged.

`probe.cpp` links the production GSYM, DWARF and supporting archives. It supplies
two authored DWARF 4 compilation units, each with one named function and a known
address range. It records production results; `gsym.wast` owns every expected
value, exact decoded name, capture size and exit-status assertion.
The C++ entry uses C linkage and the shared CRT's three-argument `main`.
The writer selects its string-offset width from the actual creator, matching
the production save path for the two format versions.

The four cases cover GSYM versions 1 and 2, little and big endian output, and
transformer task counts 1 and 3. They exercise creator string/file interning,
copied string ownership, conversion, finalization, encoding, reader creation,
base and function addresses, range sizes, gap/end rejection, and reader ownership
after the source encoding is overwritten. The two task-count-3 cases execute
the transformer's task-pool/logging branch through the existing disabled-thread
LLVM backend. They do not establish concurrent LLVM execution or cross-thread
exclusion. The project kernel and runtime scheduler are unchanged.

The observer writes 114 signed 64-bit observations (912 bytes) to stdout,
decoded names to `/tmp/gsym-names.txt`, and returns 31 intentionally. WAST has
117 assertions; the worker includes launch for 118 results. This fixture checks
the tested GSYM conversion and ownership paths, not all DWARF/GSYM features or
complete compiler/linker execution.

Run from the repository root:

```sh
make -C src/aux clang-gsym
make -C src/aux gsym-native-source gsym-guest-source
make -C src/aux test-gsym
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-gsym \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/gsym/browser.json \
  > build/engine/logs/clang-gsym-browser.json
```

The source gates check complete native and threaded translation units plus
actual guest objects for `DwarfTransformer.cpp`, `GsymCreator.cpp`,
`GsymCreatorV1.cpp` and `GsymCreatorV2.cpp`, including the changed creator header.

For manual offline-browser acceptance, open the freshly built `bash.html`, run
`upload /tmp/gsym.wasm`, and select `build/system-tests/clang/gsym/probe.wasm`.
Then run these commands in browser Bash:

```sh
chmod +x /tmp/gsym.wasm
/tmp/gsym.wasm > /tmp/gsym.bin
printf '%s\n' "$?" > /tmp/gsym-status.txt
wast --verbose /root/test/system/clang/gsym/gsym.wast > /tmp/gsym-results.txt
printf '%s\n' "$?" > /tmp/gsym-test-exit.txt
download /tmp/gsym-results.txt
download /tmp/gsym-status.txt
download /tmp/gsym-test-exit.txt
```

Expect 117 PASS, zero failures, observer status 31 and WAST exit status 0. Save
the downloaded files under `build/html-rt/`. To keep native output out of the
terminal, redirect `make -C src/aux test-gsym` into
`build/engine/logs/clang-gsym-manual.log`.
