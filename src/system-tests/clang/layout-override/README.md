# Clang layout-override file input

The guest-only LayoutOverrideSource reader uses the private WasteFileInput
adapter shared with CompilerInvocation's seed reader. It reads real files
through the installed shared libc fopen/getline/close APIs. Native and threaded
profiles keep the original C++ streams; the production layout parser is unchanged.

```sh
make -C src/system-tests layout-override-fixture layout-override-guest-source layout-override-native-source
make -C src/system-tests test-layout-override
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-layout-override \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

All 43 functional assertions live in layout-override.wast. The observer links
the complete production LayoutOverrideSource.cpp object and inspects its parsed
records using test-only private access. Cases cover missing/empty files,
legacy and byte-size layouts, CRLF, a 5000-byte ignored line, a long record name,
64-bit offsets, anonymous-record filtering, duplicate replacement, field/base/
virtual-base offsets and the final record without a trailing newline. Observed
values outlive parser objects, input buffers and a shared sleep yield.
This covers constructor parsing, not the public AST RecordDecl layout callback
or execution of the still-unbuilt guest compiler.

The source gates build the complete guest object and check the full production
source under native, native-threaded and guest-threaded profiles. After building
bash.html, run the same WAST assertions through its production worker:

```sh
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/layout-override/browser.json
```

Expect 44 results including launch. Its linked Clang AST code contains a valid
528-block function. Shared EXECUTION_MAX_CONTROL_DEPTH in src/config.h governs
loading, validation and execution; the portable control-depth regression covers
512 and 1024 explicit blocks without relying on deeply nested WAT parsing.

The fixture also exercises a large loader allocation workload. Browser runtime
allocator regressions have a separate native sanitizer gate:
`make -C src/html-rt browser-allocator`. Guest memory, files and sleep still use
the existing shared libc/kernel. The private browser engine heap is distinct.
