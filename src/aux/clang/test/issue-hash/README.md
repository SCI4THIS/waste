# Clang issue-line normalization

The disabled-localization guest branch of the actual `IssueHash.cpp`
`NormalizeLine` owner uses LLVM's existing raw string sink. Native/threaded
branches retain upstream `std::ostringstream`. The original lexer loop,
source selection and returned string ownership remain unchanged.

```sh
make -C src/aux issue-hash-fixture issue-hash-native-source
make -C src/aux test-issue-hash
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-issue-hash \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

The observer includes the complete production source to call its private
normalizer and MD5 helper. Real SourceManager/FileManager, diagnostics and raw
Lexer instances process comments, whitespace, source-line selection, a quoted
string with retained spaces/escapes, a blank line and a 256-byte identifier.
No-newline inputs retain the upstream loop's behavior: a final token that
reaches EOF is not appended. This fixture does not change that behavior.
Normalized text and its MD5 are copied before source-manager destruction and
retained across shared sleep. All 31 exact byte/count/status expectations live
in WAST. The installed production CRT and shared libc are used.

The standalone guest production object and the complete source in native,
native-threaded and guest-threaded syntax profiles have separate build gates.
These checks cover the changed normalizer and actual MD5 helper, not every
public issue-signature path or an invocation of the unfinished guest compiler.

After refreshing test snapshots and rebuilding `bash.html`:

```sh
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/issue-hash/browser.json
```

The production worker expects 32 results including launch. Leak detection is
disabled for the documented sanitizer environment; ASan and UBSan remain on.
