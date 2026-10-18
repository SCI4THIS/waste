# LLVM MC/DC coverage formatting

Four real `MCDCRecord` string-formatting methods in `CoverageMapping.h` use
LLVM's existing `raw_string_ostream` in the disabled-thread guest profile.
Native and threaded branches keep upstream `std::ostringstream`. No coverage
logic, public API or runtime scheduling changes are introduced.

```sh
make -C src/aux coverage-format-fixture coverage-format-native-source
make -C src/aux test-coverage-format
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-coverage-format \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

The fixture compiles and links the actual production CoverageMapping object,
LLVMSupport and Demangle archives, installed shared libc and production CRT.
Its observer constructs real records through the public constructor and uses
real independence-pair detection. All 25 functional assertions live in WAST:
exact headers, covered/uncovered/folded conditions, true/false/don't-care rows,
unexecuted row numbering, permuted condition IDs, UINT32_MAX coordinates,
empty records and a twelve-condition header. Captured strings outlive their
records and a shared sleep yield. The complete production source also passes
native, native-threaded and guest-threaded syntax gates; those gates do not
establish a threaded guest runtime.

After `vfs-tests-install` and rebuilding `bash.html`, run:

```sh
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/coverage-format/browser.json
```

The production worker expects 26 results including launch. This checks coverage
record formatting, not execution of the still-unbuilt guest compiler.
