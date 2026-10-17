# Actual LLVM child execution

The observer links the built `libLLVMSupport.a` and `libLLVMDemangle.a`, current
libc++/libc++abi archives, production CRT and installed shared libc. The archive
supplies the complete Program.cpp implementation; no extracted or substitute
execution method supplies its behavior. Section collection retains the methods
used by this probe. This establishes ExecuteAndWait, ExecuteNoWait and their
supported waits, rather than every Support method or full C++ startup/shutdown.
The actual compiler/linker executables remain unfinished.

All 394 runtime assertions live in `program-execution.wast`. They cover exact
exit/error results, copied non-NUL-terminated argument/environment views, empty
arguments, inherited current libc environment, stdin/stdout/stderr redirection,
merged descriptors, failed setup and exec, zero-time
polling, later reaping, concurrent sleeping children, and actual session
detachment while preserving the parent. Optional statistics,
memory limits and affinity requests fail before fork. Malformed redirect vectors
return EINVAL before indexing; unsupported capabilities return ENOSYS. The
observer only records results and bytes; it has no runtime assertions.

The long `start.wat` module initializes a counter before its entrypoint. Its
observed exit checks that the complete start transaction ran. Executable loading
defers cooperative pump yields just like DSO loading, retaining timeout and
cancellation polling and restoring the caller's quantum on every return.
Without this boundary, ordinary true/false execution could intermittently
fail with an execution-format error when instantiation reached a pump checkpoint.

Empty redirect paths open the shared engine-owned `/dev/null` character device.
Actual synchronous and detached asynchronous children observe EOF on stdin,
successful discarded stdout/stderr writes, character metadata and zero size.
Detached children retain their own SID while the parent keeps its original SID.
No regular file, compiler-local device or host `/dev/null` supplies behavior.
The [device fixture](../../devices/README.md) checks its descriptor, path,
readiness and metadata boundaries independently.

The guest adaptation terminates a child with status 126 after failed redirection
or detachment setup. Returning false from the child's Execute branch would
return into the caller twice and let the parent report successful child exit.
The parent uses LLVM's ordinary Wait interpretation and reports execution error.
ExecutionFailed distinguishes failure to create a child from an executed child's
126/127 failure; the latter still has ExecutionFailed false. Native LLVM branches
retain their original behavior. Resource-limit storage does not enforce LLVM's
requested child memory budget, so that request is explicitly unsupported.

Build and test with one LLVM owner and one build job:

```sh
make -C src/aux clang-support CLANG_JOBS=1
make -C src/system-tests test-program-execution CLANG_JOBS=1
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-program-execution CLANG_JOBS=1 \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect 394 passes and status zero. Generated objects, observer and result JSON
live under `build/system-tests/clang/program-execution`. Logs belong under
`build/engine/logs`. `clang-support` builds the real library without continuing
into the complete Clang/LLD graph. Repeated fixture builds compile nothing.
Finish VFS installations before starting read-only guest sessions.

Refresh and audit snapshots, build the offline page, then use its production
worker without launching a GUI browser:

```sh
make -C src/html-rt vfs-tests-install vfs-tests-check bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/program-execution/browser.json \
  > build/engine/logs/clang-browser-program-execution-results.json
```

Expect 411 worker passes including Bash launch. No manual browser test is needed
when this passes. For an optional Firefox check, open the refreshed bash.html,
run `upload /tmp/program-execution.wasm`, select the generated `probe.wasm`. Also upload `start.wat` from this fixture
with `upload /tmp/program-execution-start.wat`, then:

```sh
chmod +x /tmp/program-execution.wasm /tmp/program-execution-start.wat
/tmp/program-execution.wasm > /tmp/program-execution.bin
printf '%s\n' "$?" > /tmp/program-execution-status.txt
wast --verbose /root/test/system/clang/program-execution/program-execution.wast > /tmp/program-execution-results.txt
printf '%s\n' "$?"
download /tmp/program-execution-results.txt
```

Expect 394 passes and status zero; save downloads under build/html-rt. Finite
child waits use shared process, signal, clock and readiness state; the
[finite-wait fixture](../finite-wait/README.md) checks their archive-linked methods.
requested child statistics remain unavailable. Shared open-description offsets
across fork and general cross-process filesystem visibility remain separate
kernel work. This fixture uses fresh child output paths and inherited pipes.
