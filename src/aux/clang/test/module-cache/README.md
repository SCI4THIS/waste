# Clang module cache and compiler synchronization

This is a compiler boundary check, not complete Clang acceptance. The observer
links the actual staged `InProcessModuleCache.cpp`, `InMemoryModuleCache.cpp`,
`ModuleCache.cpp` and `AtomicLineLogger.cpp` with complete LLVM Support/Demangle,
current C++ archives, ordinary static registration, production CRT and shared
libc. It records observations; all 108 functional assertions live in
`module-cache.wast`.

Linking the actual pruning implementation retains LLVM directory iteration.
The public `dirent.h` now preserves C linkage; the C++ SDK probe emits directory
calls and verifies their declarations. Shared-libc import checks alone would
not reject unrelated mangled imports, so actual loading/execution is required.

The explicit disabled-thread compiler profile selects LLVM's counted mutex.
Native/threaded profiles keep standard mutexes and condition variables. Timed
predicate waits release the lock, use the existing monotonic LLVM backoff and
shared POSIX sleep, then reacquire and recheck. Notifications have no queue in
this one-thread profile. An unfinished unbounded wait cannot obtain a producer
and reports a fatal error even under `NDEBUG`. This does not change WASTE's
concurrent kernel/process/thread scheduling.

The fixture checks owning invalidation keys, advisory lock contention and
generation changes, an old owner's destructor preserving a newer lock, buffer
copies and repeat/shared reads, actual disk flush/read, cached-read stability,
written-buffer precedence, missing-file errors, module state transitions and
full-width timestamps. Existing clock/signal controls check a one-second
deadline across interruption and a predicate becoming ready during sleep.
LLVM's actual directory iterator finds the flushed/read files and reports
ENOENT for a missing directory, exercising the C++ directory linkage at runtime.
WAST also checks an unfinished wait terminates a real child with SIGABRT while
its parent continues. No provider, cache method, clock or scheduler is mocked.

Run serially from the repository root:

```sh
make -C src/aux module-cache-fixture module-cache-native-source module-cache-guest-source
make -C src/aux test-module-cache
make -C src/aux test-module-cache WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install vfs-tests-check bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html src/aux/clang/test/module-cache/browser.json > build/engine/logs/clang-browser-module-cache.json
```

Finish VFS installation before running sessions. Each native run retains its
results and transcript in a fresh `build/system-tests/clang/module-cache/`
directory; `module-cache-native.json` and `module-cache-sanitize.json` refer to
the latest results. The worker uses the same artifact, WAST and control events
from `browser.json`, and loads the production engine/worker from the offline
page. A GUI/manual browser run is not required for these checks.

The guest source target uses the actual CMake/PCH rules for the module cache,
dependency filesystem, precompiled preamble and module driver. Native/threaded
gates compile those complete sources and verify standard cache lock types.
Those compile gates alone do not accept dependency scanning, preamble building
or worker-pool execution. Complete Clang/LLD linking and compile/link/run tests
remain in `docs/active-clang-plan.md`.
