# LLD diagnostic and context boundary

This fixture compiles the complete staged LLD `ErrorHandler.cpp`,
`CommonLinkerContext.cpp` and `Memory.cpp`, plus actual LLVM CodeGen/MC command
flag initialization. It links the complete guest Support/Demangle archives,
current libc++/libc++abi, production CRT and shared libc. C++ only records;
all 90 functional expectations live in `diagnostics.wast`.

Only `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS` selects the already accepted
LLVM SmartMutex for diagnostics. CTAD lock guards retain the actual mutex type.
The same guest branch uses LLVM's existing POSIX regex backend for the nine
Visual Studio location patterns and duplicate-definition splitting. libc++'s
localization-disabled profile omits `std::regex`; this private adaptation avoids
enabling an unrelated standard-library feature or replacing the regex engine.
Native/threaded branches retain their standard mutex and ECMAScript regexes.

WAST checks exact stdout/stderr bytes, embedded NULs, multiline separation,
warning suppression/promotion, muted output, error counters/limits, all nine
location forms, fallback and duplicate splitting, and actual SyncStream levels
and move ownership. A failing error-handling script uses actual LLVM child
execution and the installed `false` command. Fatal recovery removes an actual
FileOutputBuffer temporary file while preserving its live mapping. Context
teardown destroys arena owners before its callback; that callback yields through
shared usleep and resumes with the context cleared. A second context reuses
real static CodeGen/MC options. Separate fatal/error-limit children verify
managed shutdown, buffered-output flushing, `_exit` skipping ordinary C/C++ and
context callbacks, raw wait status and parent survival.

The guest CMake profile explicitly selects `LLD_WASTE_WASM_ONLY`. It builds
Common/Wasm libraries, links only those drivers, creates the `wasm-ld` alias,
and supplies a static immutable Wasm-only entrypoint table. Native selection
keeps all five drivers and its original aliases. Compilation and graph/symbol
inspection establish selection; full driver dispatch/linking still requires
the final executable.

Run serially from the repository root:

```sh
make -C src/aux lld-diagnostics-fixture lld-diagnostics-native-source lld-diagnostics-guest-source
make -C src/aux test-lld-diagnostics
make -C src/aux test-lld-diagnostics WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install vfs-tests-check bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html src/aux/clang/test/lld-diagnostics/browser.json > build/engine/logs/clang-browser-lld-diagnostics.json
```

Keep one LLVM build owner and one job. The guest source gate uses production
CMake/PCH rules, including `vt_gen` and the real CodeGen PCH. It compiles the
complete diagnostic owner, context and driver entrypoint. Native and forced
threaded-guest gates compile the complete diagnostic source and all-driver
entrypoint. Finish VFS installation before running sessions. Original native
results/transcripts remain in fresh directories under
`build/system-tests/clang/lld-diagnostics`; the `lld-diagnostics-native.json`
and `lld-diagnostics-sanitize.json` files hold the latest results. The same
artifact/WAST/events run in the packaged production worker without a GUI.
Sanitizer sessions use ASan/UBSan with leak detection disabled under the
established sandbox profile.

The installed snapshot at `/root/test/system/clang/lld-diagnostics/diagnostics.wast`
requires the observer's generated files; running it alone in a fresh Bash is
not a complete scenario. It remains skipped by isolated batch execution.
No compiler/linker executable is installed by this fixture. File/output
ownership has a separate [filesystem fixture](../lld-filesystem/README.md).
Full LLD driver, IR diagnostic and Clang compile/link/run acceptance remain
in the active plan.
