# Actual LLVM crash recovery

The ordinary C++ observer links the complete staged `LLVMSupport` and Demangle
archives, current libc++/libc++abi, production CRT and installed shared libc.
It records observations; `crash-recovery.wast` owns all 125 assertions. No
extracted or substitute LLVM recovery methods are linked.

Coverage includes disabled/enabled and repeated setup, safe completion, nested
contexts, explicit `HandleExit` and actual `Process::Exit`, zero exit codes,
context survival across yielding sleep, all six caught guest crash signals,
mask cleanup, crash classification, unregister/reverse resource cleanup,
real Signals file removal/callbacks, restored previous actions and ignored
utility dispositions. A forked child raises a signal outside a current context;
only that child terminates and its parent reaps the raw signal status.

Guest utility setup records successful handler installations so teardown leaves
ignored dispositions unchanged, even after an earlier ordinary setup cycle.
Native LLVM retains its original implementation. The public
[checkpoint fixture](../checkpoint/README.md) separately covers nonzero
`sigsetjmp` mask saving, copied tokens and C/C++ fork/yield/handler interactions.

```sh
make -C src/aux test-crash-recovery CLANG_JOBS=1
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-crash-recovery CLANG_JOBS=1 \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect 125 passes and status zero. Results stay under
`build/system-tests/clang/crash-recovery/run-*`. Finish builds/installations
before starting guest readers; use one LLVM build owner/job. The fixture does
not verify C++ static exit registration/shutdown, recover arbitrary Wasm traps
or memory faults, produce host core dumps, or symbolize machine backtraces.

Refresh the installed WAST snapshot, build the offline page, and run the same
fixture through its production worker:

```sh
make -C src/html-rt vfs-tests-install vfs-tests-check bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/crash-recovery/browser.json \
  > build/engine/logs/clang-browser-crash-recovery-results.json \
  2> build/engine/logs/clang-browser-crash-recovery.log
```

Expect 126 passes including Bash launch. The shared native/browser API checker
also accepts `--scenario clang-crash-recovery` and `--scenario clang-checkpoint`.
No manual browser check is required. For optional Firefox evidence, open the
fresh `bash.html`, upload the built `probe.wasm` as `/tmp/crash-recovery.wasm`,
and run:

```bash
chmod +x /tmp/crash-recovery.wasm
/tmp/crash-recovery.wasm > /tmp/crash-recovery.bin
printf '%s\n' "$?" > /tmp/crash-recovery-status.txt
wast --verbose /root/test/system/clang/crash-recovery/crash-recovery.wast > /tmp/crash-recovery-results.txt
echo "$?"
download /tmp/crash-recovery-results.txt
```

Expect 125 passes and status zero; save the download under `build/html-rt`.
