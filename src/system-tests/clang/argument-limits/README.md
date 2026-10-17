# Guest exec limits and LLVM argument fitting

This fixture runs both actual staged LLVM `commandLineFitsWithinSystemLimits`
overloads against the installed shared libc and engine. The guest-only
selection queries `waste_get_exec_limits`, counts the actual current environment,
and includes terminating NULs. `Args` already contains argv[0]; the executable
pathname is separate. Native LLVM keeps its original host calculation.

`src/config.h` owns `PROCESS_EXEC_MAX_ARGS`, `PROCESS_EXEC_MAX_ENV`, and
`PROCESS_EXEC_MAX_BYTES`. Counts exclude NULL sentinels. The byte budget covers
argv and environment strings, excluding pointer-vector storage and pathname.
`sysconf(_SC_ARG_MAX)` queries that actual budget. Interpreter expansion is
checked again before committing the image. Excess returns E2BIG and retains
the old image and its close-on-exec descriptors.

The C++ observer only captures results. All runtime expectations live in
`argument-limits.wast`, including exact counts, exact/excess mixed string budgets,
long arguments, environment changes after a fit, invalid pointers, unchanged
query canaries, both LLVM overloads, and WAT shebang expansion. Limits used to
construct boundary cases come from the running kernel, not copied test constants.
`prepare-arguments.py` extracts the two staged definitions with upstream license,
boundary checks and source/generated hashes. It does not replace production
sources or establish whole Program.cpp/LLVM execution-method acceptance.

Run one CMake owner with one LLVM job; complete VFS installation before readers.

```sh
make -C src/system-tests test-argument-limits CLANG_JOBS=1
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-argument-limits CLANG_JOBS=1 \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-install-app vfs-tests-install vfs-tests-check bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/argument-limits/browser.json \
  > build/engine/logs/clang-browser-argument-limits-results.json
```

Native and sanitizer runs expect 186 passes; the packaged worker expects 187
including Bash launch. Generated observations and session JSON stay under
`build/system-tests/clang/argument-limits`; logs belong under `build/engine/logs`.
A GUI test is optional. In a fresh bash.html, upload the observer and script:

```sh
upload /tmp/argument-limits.wasm
# Select build/system-tests/clang/argument-limits/probe.wasm.
upload /tmp/argument-script.wat
# Select src/system-tests/clang/argument-limits/script.wat.
chmod +x /tmp/argument-limits.wasm /tmp/argument-script.wat
/tmp/argument-limits.wasm > /tmp/argument-limits.bin
printf '%s\n' "$?" > /tmp/argument-limits-status.txt
wast --verbose /root/test/system/clang/argument-limits/argument-limits.wast > /tmp/argument-limits-results.txt
printf '%s\n' "$?"
download /tmp/argument-limits-results.txt
```

Expect 186 passes and exit zero; save downloaded evidence under build/html-rt.
