# HLSL Driver extension-name validation

The localization-free guest replaces std::regex only in the real HLSL Driver
helper with LLVM's existing POSIX regex backend. Native and threaded profiles
keep the original standard expression. Explicit full-match length preserves
std::regex_match behavior for newline and embedded-NUL inputs. This preserves
a Driver validation helper; it does not enable a SPIR-V or DXIL backend.

```sh
make -C src/system-tests spirv-name-fixture spirv-name-source
make -C src/system-tests test-spirv-name
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-spirv-name \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

The observer includes the complete production HLSL.cpp source to call its
anonymous-namespace helper. Section GC discards unrelated Driver methods.
All 34 functional assertions live in spirv-name.wast. Thirty fixed inputs cover
the four keyword aliases, valid SPV_ suffixes, a long name, case-sensitive
rejection, delimiters, whitespace, newline, UTF-8 and embedded NUL. An additional
bitmap checks all 256 single-byte suffixes. The complete observation packet
also matches the original native production std::regex implementation.
Captured bytes survive temporary input/regex cleanup and a shared sleep yield.
No regex, clock or filesystem runtime is added.

The source gate compiles the actual CMake HLSL.cpp guest object and checks the
complete source in native, native-threaded and guest-threaded profiles. After
refreshing installed snapshots and building bash.html:

```sh
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/spirv-name/browser.json
```

Expect 35 results including launch. This verifies the source helper and its
existing backend, not an execution of the still-unbuilt Clang command.
