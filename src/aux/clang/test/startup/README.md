# C++ startup through the production CRT

The ordinary C++ observer links the current staged libc++/libc++abi archives,
production CRT and installed shared libc. `startup.wast` owns all 86 assertions;
the observer only records values and writes real redirected stream output.
`test-startup` runs it with both the three-argument CRT and the two-argument
variant used by the Clang driver: 172 assertions in total.

Prioritized constructors and an ordinary C++ global constructor check stream
binding before initialization, descriptor identity, input and output, environment
availability, constructor ordering and exactly-once execution. A constructor
uses the existing yielding `usleep` path. Heap-backed strings/vectors, local
destruction, over-aligned arrays and explicit aligned new/delete exercise the
actual C++ archives and shared allocator. Main verifies retained allocations,
argv/environment, remaining input, EOF and stream output. Its return value is
23; Bash captures that status, and the WAST checks require it.

The baseline CRT produced 65 passes and 21 failures: constructor stream slots
were null, so constructor I/O was skipped. Binding the three stream slots before
`__wasm_call_ctors` corrects those failures without another initialization path.

```sh
make -C src/aux test-startup CLANG_JOBS=1
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-startup CLANG_JOBS=1 \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect two `WAST: 86 PASS, 0 FAIL, 86 total` summaries and command status zero.
Results stay under `build/system-tests/clang/startup/run-*`. Finish builds and
installations before starting guest readers; use one LLVM build owner/job.

```sh
make -C src/html-rt vfs-tests-install vfs-tests-check bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/startup/browser.json \
  > build/engine/logs/clang-browser-startup-results.json \
  2> build/engine/logs/clang-browser-startup.log
```

Expect 173 passes including Bash launch. No manual browser test is required.
For optional Firefox evidence, open the freshly built `bash.html` and upload
`probe.wasm`, `probe-two.wasm`, `input.txt` and `commands.sh` from this fixture
and its build directory as `/tmp/startup.wasm`, `/tmp/startup-two.wasm`,
`/tmp/startup-input.txt` and `/tmp/startup-commands.sh`. Then run:

```bash
chmod +x /tmp/startup.wasm /tmp/startup-two.wasm
cat /root/test/system/clang/startup/startup.wast > /tmp/startup.wast
. /tmp/startup-commands.sh > /tmp/startup-results.txt
echo "$?"
download /tmp/startup-results.txt
```

Save the downloaded evidence under `build/html-rt`.

The global observer object has a trivial destructor; ordinary local objects
have real destructors. Normal static-registration flags remain enabled. This
fixture does not supply or verify `__cxa_atexit`, C `atexit`, static destructor
ordering, `std::cout` initialization or the LLVM profiler lifecycle. Current
descriptor streams write directly; output on return does not establish buffered
flush conformance. Shutdown and the full Clang/LLD driver remain separate work.
