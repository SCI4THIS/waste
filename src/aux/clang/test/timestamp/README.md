# Clang timestamp and debug-sleep boundaries

The guest C++ profile omits locale streams and native C++ threads. Its Lex
owners use two private adapters, under `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`:

- `WasteTimestamp.h` uses the existing shared libc calendar functions and
  `strftime` component directives, then appends a full signed year. It preserves
  classic C-locale spacing without `%T` or four-digit year truncation.
- `WastePragma.h` implements the 100 ms `#pragma clang __debug sleep` operation
  through the already established `wasteBackoffSleepUntil` absolute deadline,
  shared monotonic clock and yielding nanosleep. No new timer/thread ABI is added.

The native and threaded source branches keep their upstream stream and sleep
implementations. Source patches apply only to `build/aux/clang/source`.

With the guest prerequisites already configured, run:

```sh
make -C src/aux timestamp-source timestamp-fixture
make -C src/aux test-timestamp
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-timestamp \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

`timestamp-source` compiles the complete production `PPMacroExpansion.cpp` and
`Pragma.cpp` with generated guest rules and checks both full sources in native,
native-threaded and guest-threaded configurations using host C++ headers.
These syntax checks do not establish a threaded guest runtime.

All 15 runtime assertions live in `timestamp.wast`. The compiled observer calls
the same private helpers used by production and retains owned strings across
reused calendar buffers and real shared sleep. It covers negative/pre-1970
time, both sides of the 2038 boundary, leap day, years 1 and 9999, missing
calendar data, and the UTC localtime facade. WAST compares each result and the
complete observation packet against the original native C-locale stream
formatter (`reference.cpp`, run with `TZ=UTC`). It also checks monotonic elapsed
time of at least 100 ms for the actual pragma helper, without a flaky upper
bound. The fixture loads the installed production CRT and shared libc.

After refreshing snapshots and rebuilding the offline page:

```sh
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/timestamp/browser.json
```

The worker expects 16 results including launch. This validates the two helpers,
not a complete Clang invocation. Package `tests/setup.sh` additionally requests
the debug-sleep pragma and expands `__TIMESTAMP__`, `__DATE__` and `__TIME__`
with `SOURCE_DATE_EPOCH=946684800`. That end-to-end check remains pending until
the actual guest compiler is built and installed.
