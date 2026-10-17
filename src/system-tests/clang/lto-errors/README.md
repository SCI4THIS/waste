# LLVM ThinLTO error ownership

This observer uses the actual `lto::ThinBackendProc` base, its production error
aggregation helper, `DefaultThreadPool`, LLVM Error payloads and owning
callbacks. It links the real Support/Demangle and C++ archives, CRT and shared
libc. The real `lto::Config` constructors come from upstream MC/Passes/Scalar
objects, Option and generated options; there are no replacement constructors. C++
records observations and returns 29. All 171 functional expectations are in
`lto-errors.wast`.

Only `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS` selects LLVM's counted mutex.
Both production backend implementations call the same protected error helper;
native and threaded profiles retain `std::mutex`. The pool is LLVM's existing
queued single-thread executor. This compiler profile does not change engine
concurrency or add a scheduler.

WAST checks empty/repeated waits, deferred move-only captures, retained index
callbacks, nested FIFO submissions, both joined-error list shapes, errors
outliving their backend, reentrant payload cleanup and owner reuse. A queued
callback holds captures and errors across an interrupted POSIX sleep and a
second resumed sleep using the existing clock/signal controls. Exact log files
include embedded NULs; real error-code conversion consumes its payload once.

Compiling the real pipeline constructor also required two upstream sample
profile context formatters. Their disabled-thread guest branches use LLVM's
existing `raw_string_ostream`; native/threaded stringstreams are unchanged.
WAST compares exact frame/context output for empty names, zero/full-width line
and discriminator values, numeric function IDs, embedded NULs, leaf-location
selection and returned-string ownership after input mutation.

The real generated scalar-options switch has 452 nested blocks. The shared
`EXECUTION_MAX_CONTROL_DEPTH` in `src/config.h` supplies the engine's bounded
512-slot decoder, validator, execution, continuation and clone capacity. The
portable `tests/engine-regressions/control-depth.wast` exercises 512 live blocks,
outer/deep table branches, early return and independent caller/callee stacks.
Its standard binary spelling avoids the text parser's separate nesting bound.
Both WAST handlers encode resolved module/raw-source views directly, avoiding
the old 3 MiB temporary AST copy that overflowed the native driver's stack after
the capacity increase. Normal host stack limits remain in use.

Run serially from the repository root, with one LLVM build owner:

```sh
make -C src/system-tests lto-errors-fixture lto-errors-native-source lto-errors-guest-source
make -C src/system-tests test-lto-errors
make -C src/system-tests test-lto-errors WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install vfs-tests-check bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html src/system-tests/clang/lto-errors/browser.json > build/engine/logs/clang-browser-lto-errors.json
```

Finish VFS installations before starting sessions. The same artifact, WAST and
controlled-clock events run in native, ASan/UBSan and packaged production-worker
sessions without a GUI. The suite reports 172/172 including launch and exits
zero. Native results/transcripts are retained in fresh directories under
`build/system-tests/clang/lto-errors`; `lto-errors-native.json` and
`lto-errors-sanitize.json` are latest-run copies. Sanitizer sessions use the
established profile with leak detection disabled.

Source gates compile complete upstream `LTO.cpp`, including both aggregation
callers, in native, forced threaded-guest and actual production guest profiles.
Guest object checks use generated CMake/PCH rules and refresh dependencies
before compiling. Initializer objects likewise use actual source rules and
real generated option headers; section collection retains the required
constructors rather than linking the whole optimizer into this observer.
`lld-wasm-guest-source` enumerates the 14 actual Wasm linker rules;
`lto-component-guest-source` attempts every LTO source rule and may report the
next port boundary rather than final linker acceptance.

The observer queues callbacks directly. Its required virtual `start` method
rejects bitcode compilation and is never called. Full ThinLTO code generation,
index-file writing, full ProfileData behavior and final compiler/linker
execution remain separate acceptance work. This fixture installs no compiler
or linker. The installed `/root/test/system/clang/lto-errors/lto-errors.wast`
snapshot needs the observer's generated files; isolated batch execution skips
it.
