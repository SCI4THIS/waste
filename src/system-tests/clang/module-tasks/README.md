# Clang module compilation task ownership

This fixture includes the actual staged private `ModuleCompileTasks.h` used by
`DependencyScanningWorker.cpp`. The repository patch moves the original native
owner into that header without changing its standard mutex/thread operations.
Only `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS` selects LLVM's existing
`parallel::TaskGroup`. Its callbacks run inline in that profile, including
recursive module discoveries, before owner shutdown begins.

The move-only compilation callable owns its compiler, diagnostic, controller
and advisory-lock captures. A shared owning wrapper adapts that callable to
TaskGroup's copyable function interface; it supplies neither a queue nor a new
scheduler. Captures finish and release before `add` returns. Native/threaded
branches retain asynchronous work, submission locking and joining on shutdown.
This compiler profile does not restrict the engine's concurrent processes or
threads.

The observer links actual LLVM TaskGroup/CrashRecoveryContext from Support,
the complete Clang cache sources, current libc++/libc++abi, ordinary static
registration, production CRT and shared libc. All 104 functional assertions
are in `module-tasks.wast`; C++ only records observations. They check inline
order, recursive discovery, move-only capture lifetime and reentrant submission
from capture cleanup. Actual service-owned advisory locks survive destruction
of their temporary cache, remain held through nested callbacks and an
interrupted POSIX sleep, then release exactly once. The same injected clocks,
signals and yielding sleep used by the existing runtime drive that wait.
Empty teardown and a fresh owner after completed callbacks remain usable.

Run serially from the repository root:

```sh
make -C src/system-tests module-tasks-fixture module-tasks-native-source module-tasks-guest-source
make -C src/system-tests test-module-tasks
make -C src/system-tests test-module-tasks WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install vfs-tests-check bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html src/system-tests/clang/module-tasks/browser.json > build/engine/logs/clang-browser-module-tasks.json
```

Keep one LLVM/CMake build owner and one compile job. Finish VFS installation
before running sessions. Each native run keeps its original result/transcript
under a fresh `build/system-tests/clang/module-tasks/` directory; the
`module-tasks-native.json` and `module-tasks-sanitize.json` files refer to the
latest runs. `browser.json` transports the same artifact, WAST and events to the
packaged production worker. No manual browser or GUI launch is required.

The source gates compile the complete worker in native/threaded profiles and
with the exact guest CMake/PCH rule. They do not establish full prescan/module
compilation, diagnostics or asynchronous dependency-scanning acceptance. Those
require the complete guest Clang/LLD executables and remain in the active plan.
