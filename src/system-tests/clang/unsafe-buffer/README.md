# Actual compiler unsafe-buffer fix-its

The localization-free guest adapts seven string sinks in the real
UnsafeBufferUsage.cpp: six release fix-it owners and one debug-only ancestor
formatter. LLVM raw string streams replace unavailable std::stringstream only
in the disabled-thread guest branch. All AST analysis, source selection,
replacement spelling and native/threaded streams remain upstream.

Before the compiler is complete, the private source gate is:

```sh
make -C src/system-tests unsafe-buffer-source
```

It compiles the full production guest object, checks the debug-only owner with
the actual guest headers, and checks the complete source in three native/
threaded profiles with host C++ headers. Matching generated headers follow
native configuration headers to avoid unrelated installed LLVM enums.

After the real compiler is built and installed:

```sh
make -C src/system-tests test-unsafe-buffer
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-unsafe-buffer \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

This invokes the installed /usr/bin/clang on input.cpp inside guest Bash. All
13 functional assertions live in unsafe-buffer.wast. Ten exact diagnostic
ranges/replacement strings exercise local span declarations and initialization,
zero/nonzero array addresses, preincrement, parameter replacement, new overload
declarations and old overload wrappers. Compiler status and parent-shell
survival are checked too. The input needs no C++ headers or linked program;
it tests actual Clang parsing and fix-it generation, not a copied formatter.
Its setup also supplies a two-line layout seed file to exercise the real
compiler-invocation reader option in the same command.
Source cases follow the read-only upstream SemaCXX addressof-arraysubscript,
pre-increment and parm-span-overload tests. No upstream file is modified.

After rebuilding bash.html with the compiler installed:

```sh
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/unsafe-buffer/browser.json
```

The worker expects 14 results including launch. Native execution is bounded
at 180 seconds; the worker uses the same engine limit and a 240-second host
limit. These are initial diagnostic budgets, subject to actual measurements.

The development host Clang 22 preflight accepted the input/options and produced
the expected ten fix-it forms. It is not guest Clang acceptance. The installed guest compiler now passes all
13 native and sanitizer WAST assertions and all 14 production-worker results,
including launch. The debug-only formatter is source-checked; this release
compiler runtime scenario does not execute it.
