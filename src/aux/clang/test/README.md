# Clang SDK and execution boundaries

Clang is still being ported; see `docs/active-clang-plan.md`. These checks
must not be reported as compiler acceptance until real guest binaries exist.

`make -C src/aux test-clang-lld-link` runs the actual guest LLD artifact on a
host-compiled object and archive, executes the emitted module and checks ten
WAST assertions in installed Bash. This is native linker acceptance for that
small input, not compiler/SDK/LTO or browser linker acceptance. See
[real linker execution](lld-link/README.md).

`make -C src/aux test-startup` runs the production CRT with ordinary
C++ constructors and current archives, checking streams, ordering, environment,
yielding sleep, owned/aligned allocation and local destruction. Both CRT main
signatures pass 86 WAST assertions each. See [startup](startup/README.md);
static-exit callbacks are covered by [shutdown](shutdown/README.md).
Stream buffering and full compiler startup remain pending.

`make -C src/aux test-raw-stream` checks the retained LLVM memory-output
core with 51 WAST assertions for buffering, positions, binary output, formatting
and ownership. The unused standard-stream adapter is excluded only from the
guest compiler graph. The fixture isolates unchanged upstream ANSI helpers;
full POSIX Process and descriptor output remain separate. See
[the raw-stream boundary](raw-stream/README.md).

`make -C src/aux test-time-profiler` checks actual LLVM profiler entry
storage and the registry's selected mutex field on a local instance. Its 74
WAST assertions cover timing conversion, heap-backed metadata and copied/moved
instant storage. Native/threaded full-source gates preserve the standard mutex.
The archive-linked public lifecycle fixture adds 62 WAST assertions for global
initialization, nested/async events, aggregation, exact JSON/file output,
cleanup/reuse and real static registry destruction. See
[the profiler boundary](time-profiler/README.md).

`make -C src/aux test-module-cache` links actual Clang cache sources,
Support and current C++ archives. Its 108 WAST assertions cover buffer and
invalidation ownership, stable reads, module states, advisory generations,
timed predicates and child rejection. Native and browser control adapters reuse
shared clock/signal/sleep mechanisms. Real guest CMake/PCH and native/threaded
source gates cover dependency filesystem, preamble and module-driver owners.
See [the module-cache boundary](module-cache/README.md); complete dependency
scanning and Clang/LLD acceptance remain separate.

`make -C src/aux test-module-tasks` checks the actual private module
compilation owner used by DependencyScanningWorker. Its 104 WAST assertions
cover recursive callbacks, move-only captures and real service-owned cache
locks across interrupted shared sleep. The disabled-thread guest reuses LLVM
TaskGroup; complete native/threaded and guest source gates preserve the other
profiles. See [module task ownership](module-tasks/README.md); full scanning
and compiler acceptance remain separate.

`make -C src/aux test-parallel` checks LLVM's actual disabled-thread
TaskGroup and scalar algorithms with current C++ archives. Its 124 WAST
assertions cover latch counts, nested/recursive callbacks, capture lifetime,
range fences, reductions and sorting. Release-build rejection paths terminate
real guest children while their parent survives. Native/threaded source gates
retain blocking synchronization. See the [parallel boundary](parallel/README.md)
for native, sanitizer and packaged browser steps.

`make -C src/aux test-mustache` checks the guest numeric helper used
by actual LLVM Mustache.cpp against explicit WAST text expectations and native
classic-locale streams. Its 96 assertions cover 37 binary64 inputs, errno and
heap-backed string ownership using current C++ archives. Full guest/native
source compiles retain the native stream path. See the
[numeric interpolation boundary](mustache/README.md); full template rendering,
narrow floating printf and C++ startup/shutdown remain pending.

`make -C src/aux test-managed-static` checks the actual LLVM managed
registry's lazy nested construction, reverse destruction, repeated shutdown,
reuse, custom deleters and claimed-object lifetime. Its 91 WAST assertions run
with real C++ archives and ordinary static-registration flags. Native/threaded
source gates preserve the standard mutex. See the
[managed registry boundary](managed-static/README.md); C++ static-exit callbacks
remain separate pending work.

`make -C src/aux test-backoff` runs the actual LLVM backoff with shared
jitter and yielding sleep. Its 12 live WAST assertions and 20 controlled-clock
assertions cover minimum waits, exact deadline clamping and signal interruption.
Use `test-backoff-controlled` for clock injection; see the
[backoff boundary](backoff/README.md). The current randomness provider supplies
compatibility PRNG bytes, not operating-system entropy.

`make -C src/aux test-checkpoint` checks the public C/C++ non-local
jump ABI with compiled callers and the actual guest stack pointer. Its 218 WAST
assertions cover copied checkpoints, nested jumps, C++ signal linkage, saved
masks, yielding continuations and fork; 12 evaluator assertions additionally
cover bounds, inactive tokens and save flags. See the
[checkpoint boundary](checkpoint/README.md). The separate
[archive-linked recovery fixture](crash-recovery/README.md) accepts actual LLVM
guest signal recovery and cleanup; arbitrary Wasm trap/fault recovery is outside
that boundary.

`make -C src/aux test-thread-pool` checks actual LLVM single-thread
task execution with shared deferred value/void/reference results. Its 79 WAST
assertions cover ordering, move-only captures/results, alignment, nested tasks,
group waits and destruction. Native/threaded declarations retain standard
futures; the guest implementation lives in LLVM's namespace. See the
[thread-pool boundary](thread-pool/README.md). Static shutdown remains pending.

`make -C src/aux test-synchronization` checks the guest compiler's
reader/writer locks and generic PerThreadAllocator ownership with the actual
C++ archives and LLVM ID function. Its 97 WAST assertions cover counted locks,
cache reuse, alignment, moves, forwarding and local destruction. This diagnostic
fixture disables static destructor registration because `__cxa_atexit` is still
unprovided; production compiler flags stay unchanged. See the
[synchronization boundary](synchronization/README.md) for the separate declaration
gates and the remaining static-exit acceptance work.

`make -C src/aux test-logb` checks public C/C++ exponent sentinel
constants and the actual LLVM private exponent helpers under APFloat's
configuration. Its 131 WAST assertions cover all three guest floating widths,
including subnormals and special values. See the
[exponent boundary](logb/README.md); no public `ilogb` provider is added.

`make -C src/aux test-mutex` checks the guest compiler's thread-disabled
LLVM recursive locks and actual archive-backed `llvm::call_once`. Its 40 WAST
assertions run natively, with sanitizers and through the production worker.
See the [mutex boundary](mutex/README.md); other synchronization classes and
general C++ runtime acceptance remain pending.

`make -C src/aux test-filesystem` checks the initial guest LLVM POSIX
profile, metadata widths and compiled guest file-status calls. Its 63 WAST
assertions run natively, with sanitizers and through the production worker.
`test-filesystem-controlled` adds five exact injected-clock assertions, verifying
real file metadata beyond 2038 without using unsupported timestamp mutation.
See the [filesystem boundary](filesystem/README.md); full LLVM POSIX source
and out-of-line filesystem acceptance remain pending.

`make -C src/aux test-wide-format` checks bounded wide formatting
through compiled C and C++ calls, including full binary128 values and mixed
variadic alignment. See the [wide-format boundary](wide-format/README.md) for
native, sanitizer and production-worker steps; assertions remain in WAST.

`make -C src/aux test-stdio` checks compiled C and C++ `va_list`
calls against shared `vsnprintf`, including public `PRIX8/16/32/64` integer
formats and mixed-width alignment, with all 35 assertions in WAST. See
[formatter boundary and browser instructions](stdio/README.md). It needs
the host cross-compiler and generated libc++ headers, not guest Clang.

`make -C src/aux test-classification` checks actual compiled C and
`<cmath>` C++ classification, including binary128 subnormal/normal boundaries.
See [classification and browser instructions](classification/README.md).

`make -C src/aux test-endian` exercises the actual LLVM bare-Wasm
header path, byte swapping and stored byte order. Its compiled C++ observer
and WAST assertions use the private Clang compatibility view of the public
SDK's endian definitions. See [endian and browser instructions](endian/README.md).

`make -C src/aux clang-sdk-check` generates the configured libc++ headers and
compiles `cxx-sdk.cpp` with the actual guest C headers. It checks declarations,
Wasm32 aggregate layouts and string/vector use. It does not link libc++ or
execute a compiler.

`make -C src/aux test-wide` checks generated wide-enabled libc++
`wstring_view`/character traits and real shared wide-memory/search, integer
and floating conversion providers. Its 64 functional assertions live in WAST.
It does not use the stale bootstrap archives or establish owning-string
runtime support. See
[wide-string boundary and browser instructions](wide/README.md).

`integer.json` runs the 77 shared-libc integer-conversion assertions through
the production page's worker, plus Bash bootstrap:

```sh
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/vfs/root/test/system/clang/integer.json \
  > build/engine/logs/clang-browser-integer-results.json
```

The WAST assertions are owned by `src/vfs/root/test/aux/libc/integer-conversion.wast`
and run with `test-libc`. The SDK probe separately checks `<cstdlib>`, `<cwchar>` and
`<cinttypes>` C linkage, namespace aliases and their 32/64-bit return types.
For an optional manual check after refreshing snapshots and rebuilding:

```sh
wast --verbose /root/test/aux/libc/integer-conversion.wast > /tmp/integer-results.txt
echo "$?"
download /tmp/integer-results.txt
```

Expect 77 passing assertions and exit status zero. Keep downloaded evidence
under `build/html-rt`; a GUI check is not required for this libc prerequisite.

`floating.json` runs the 174 package floating-conversion assertions through
the production worker (175 results including Bash bootstrap):

```sh
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/vfs/root/test/system/clang/floating.json \
  > build/engine/logs/clang-browser-floating-results.json
```

The tests call all six narrow/wide providers and check exact result bits,
end pointers, errno and input/output guards. They include decimal/hex input,
rounding midpoints, subnormals, signed zeros, infinities/NaNs, exponent
saturation, full binary128 range and nonzero tails after 20,000 zeros.
For optional manual evidence after refreshing snapshots and rebuilding:

```sh
wast --verbose /root/test/aux/libc/floating-conversion.wast > /tmp/floating-results.txt
echo "$?"
download /tmp/floating-results.txt
```

Expect 174 passes and status zero; keep downloads under `build/html-rt`.
The compiled three-width C++ return checks run with `test-wide` above.

`alignment.json` runs the shared-libc aligned-allocation WAST prerequisites
through the packaged browser worker:

```sh
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/vfs/root/test/system/clang/alignment.json \
  > build/engine/logs/clang-browser-alignment-results.json
```

The assertions belong to `src/vfs/root/test/aux/libc/aligned-allocation.wast` and also
run with `test-libc`. For an optional manual browser check after refreshing
snapshots and rebuilding the page:

```sh
wast --verbose /root/test/aux/libc/aligned-allocation.wast > /tmp/alignment-results.txt
echo "$?"
download /tmp/alignment-results.txt
```

Expect eleven passing assertions and exit status zero.

`fenv.json` similarly runs the fixed floating-point environment prerequisites:

```sh
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/vfs/root/test/system/clang/fenv.json \
  > build/engine/logs/clang-browser-fenv-results.json
```

Its nine assertions live in `src/vfs/root/test/aux/libc/fenv.wast`. For an optional
manual check in the refreshed/rebuilt page:

```sh
wast --verbose /root/test/aux/libc/fenv.wast > /tmp/fenv-results.txt
echo "$?"
download /tmp/fenv-results.txt
```

Expect nine passing assertions and exit status zero. The SDK probe also checks
`<cfenv>` C linkage, four-byte saved environment/flag types and that unsupported
rounding/exception macros are not advertised.

`clocks.json` runs eight shared-libc clock assertions through the offline
page's worker. `clocks-controlled.json` runs five exact timestamp assertions
with injected clocks: realtime remains fixed beyond 2038 while monotonic time
advances by 123,456 nanoseconds. The fixtures in `clocks/` require that scenario;
they must not run with ordinary live clocks. Large nanosecond inputs use JSON
strings to retain every bit across the JavaScript boundary.

```sh
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/vfs/root/test/system/clang/clocks.json \
  > build/engine/logs/clang-browser-clock-results.json
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  python3 src/engine/test/guest-session/guest-session-check.py --scenario clang-clocks \
  --page build/html-rt/bash.html
```

The latter command compares the native private session, browser API and
packaged worker. Select `--native build/cli-rt/private/guest-session-sanitize`
for the ASan/UBSan session. Build the transports with
`make -C src/cli-rt guest-session guest-session-sanitize`. WAST owns the timestamp
assertions; the transports stage inputs, set/advance clocks and check completion.

For an optional manual check of the eight live-clock assertions in a refreshed
page (no injected clocks required):

```sh
wast --verbose /root/test/aux/libc/clocks.wast > /tmp/clock-results.txt
echo "$?"
download /tmp/clock-results.txt
```

Expect eight passing assertions and status zero. Keep downloaded evidence under
`build/html-rt`. GUI evidence is not required for the compile-time blocker.

The compiled [lock-owner boundary](lock-owner/README.md) independently verifies
LLVM's guest process-liveness adaptation and the engine's null-signal queries.
Run `test-lock-owner` and `lock-owner-native-source`; its 83 WAST assertions cover
blocked children, unreaped/reaped owners, conservative host identity and the
public/libc++/kernel errno boundary. Complete file-lock execution remains pending.

The [TargetParser host identity boundary](target-host/README.md) compiles the
real archive and checks its guest triple/CPU fallback before that full install.
Run `make -C src/aux test-target-host`; the authored WAST also runs in
the packaged production worker.

The [DWARF diagnostic owner](dwarf-verifier/README.md) links the actual
verifier source and checks owned category/subcategory aggregation, callback
controls and shared sleep with a live diagnostic guard. Run
`make -C src/aux test-dwarf-verifier`; complete guest/native/threaded
source gates are separate targets documented beside the WAST fixture.

The [DWARF context cache](dwarf-context/README.md) includes the full production
context source and uses the actual guest DWARF/Object/MC archives. It checks
normal and split compile-unit bytes, cache identity, recursive access across
shared sleep, eager/lazy DWO selection, type maps and destruction/reuse. Run
`make -C src/aux test-dwarf-context`; functional assertions belong to
its WAST, with complete native/threaded/guest source checks kept alongside it.

The [GSYM conversion boundary](gsym/README.md) links the production GSYM archive
to convert actual DWARF units, encode both format versions and byte orders, and
decode function names and address ranges. It checks creator interning and
reader ownership in WAST. Run `make -C src/aux test-gsym`; complete
source gates preserve native/threaded mutex types alongside the guest wrapper.

The [CFG formatting boundary](cfg-format/README.md) uses the actual node
formatter, production Analysis objects and real IR owners. WAST checks exact
attributes, copied and embedded-NUL identifiers, unprofiled heat colors and
yielding callbacks. Run `make -C src/aux test-cfg-format`; source gates
preserve standard string streams in native and threaded builds.

The [inlining boundary](inlining/README.md) includes the complete production
statistics source and exercises its private four-significant-digit formatter
and public graph API. WAST checks exact helper text, compiled mixed variadic
arguments and the graph's retained function names. Run
`make -C src/aux test-inlining`; the guest uses LLVM's raw sink and
shared floating printf while native/threaded sources preserve standard streams.

Once `install-clang` succeeds, run:

```sh
make -C src/aux test-clang-system
```

The shared Bash driver supplies `setup.sh` inputs, runs the guest compiler and
linker, and executes `compiled.wast`. All assertions are in WAST. The probe
requires actual Wasm magic bytes and generated-program exit status 42.
Package version and invalid-source checks are owned by `test-clang` in aux.
The current freestanding C probe avoids pretending that resource-header,
CRT/shared-libc and driver-default integration have already been completed.

For browser acceptance, first install refreshed system snapshots with
`vfs-tests-install` and rebuild `bash.html`. Paste the contents of `setup.sh`
into browser Bash, then run:

```sh
wast --verbose /root/test/system/clang/compiled.wast > /tmp/clang-results.txt
download /tmp/clang-results.txt
```

Keep downloaded evidence under `build/html-rt`. Browser execution is pending
until the compiler/linker port completes.

Record-lock provider and actual LLVM blocking method checks live under
[`locks`](locks/README.md); run `make -C src/aux test-locks`.
The [timed-lock boundary](timed-locks/README.md) checks unchanged tryLockFile
retries and public usleep through the existing sleep scheduler. Run
`test-timed-locks` for live process contention and `test-usleep-controlled` for
precise deadlines and interruption. Full LLVM Path acceptance remains pending.

The [optional folding boundary](folding/README.md) links complete production
ConstantFolding and TargetLibraryInfo objects. Run `test-folding` for scalar,
vector, poison, target-library and retained-call outcomes; its source checks
preserve native host math and check the guest policy independently of threads.

The [code-layout boundary](code-layout/README.md) checks production ExtTSP
orders, scores and configuration. The selected Wasm guest retains ExtTSP and
excludes unused ELF/BOLT CDSort; native profiles retain both. Run
`make -C src/aux test-code-layout` and re-audit callers if the guest
target/driver graph expands.

The [assignment formatter](assignment-format/README.md) checks the complete
source's private interval formatting with real IR values. Its guest raw sink
preserves exact bytes, unsigned bounds and returned-string ownership; complete
native/threaded sources retain standard streams. Run
`make -C src/aux test-assignment-format`.

The [shadow callback initializer](shadow-callbacks/README.md) uses real LLVM
modules to check all callback suffixes, 32/64-bit signatures, declaration reuse
and captured-name ownership. The guest selects LLVM's raw formatter while
native/threaded source checks retain standard streams. Run
`make -C src/aux test-shadow-callbacks`.

The [MemProf graph formatter](memprof-format/README.md) checks actual node IDs
for regular/ThinLTO graph types and retained real-node attributes. Guest
lowercase hex uses LLVM's existing helper; native/threaded streams remain
standard. Run `make -C src/aux test-memprof-format` through cli-rt.

The portable [type-capacity regression](../../vfs/root/test/engine-regressions/type-capacity.wast)
checks the shared 256-type bound, high reference indexes, recursive group growth
and rejection, forward references and cross-module structural equivalence.
Run `build/cli-rt/wast --verbose src/vfs/root/test/engine-regressions/type-capacity.wast`.
The same assertions run in Bash as
`/bin/wast --verbose /root/test/engine-regressions/type-capacity.wast` after
`vfs-tests-install`. Automate the production browser worker with
`node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html
src/vfs/root/test/guest-session/type-capacity.json`. Use `wast-sanitize` with
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`
for native memory/undefined-behavior checks. This accepts module type capacity,
not large-function decoding or compiler/linker startup.

The portable [local-storage regression](../../vfs/root/test/engine-regressions/local-storage.wast)
checks exact-sized declared-local metadata, empty/mixed groups, frame limits,
malformed loads and recovery. Run
`build/cli-rt/wast --verbose src/vfs/root/test/engine-regressions/local-storage.wast`, or
`/bin/wast --verbose /root/test/engine-regressions/local-storage.wast` in Bash
following `vfs-tests-install`. The production worker scenario is
`src/vfs/root/test/guest-session/local-storage.json`, using the same command shown above
for the type-capacity worker scenario. This reduces decoded function metadata;
large instruction streams and compiler/linker startup remain pending.

The portable [instruction-storage regression](../../vfs/root/test/engine-regressions/instruction-storage.wast)
checks decoded-buffer growth, control-target relocation, branch/catch payload
ownership, wide immediates and recovery after failed loads. Run
`build/cli-rt/wast --verbose src/vfs/root/test/engine-regressions/instruction-storage.wast`,
or `/bin/wast --verbose /root/test/engine-regressions/instruction-storage.wast`
in Bash after `vfs-tests-install`. Automate the production browser worker with
`node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html src/vfs/root/test/guest-session/instruction-storage.json`.
This removes byte-based over-allocation; large-module memory acceptance and
compiler/linker startup remain pending.

The portable [instruction-layout regression](../../vfs/root/test/engine-regressions/instruction-layout.wast)
checks the compact opcode-selected immediate payloads: block signatures, call
metadata, scalar/vector constants, memory offsets, SIMD lanes/shuffle, bulk
copy and GC casts/branches. Run
`build/cli-rt/wast --verbose src/vfs/root/test/engine-regressions/instruction-layout.wast`,
or `/bin/wast --verbose /root/test/engine-regressions/instruction-layout.wast`
in Bash after `vfs-tests-install`. Use
`src/vfs/root/test/guest-session/instruction-layout.json` with the production worker harness,
and rerun the instruction-storage scenario for exception/growth coverage.
This accepts the smaller record layout, not LLD startup or its total memory use.

The portable [function-metadata regression](../../vfs/root/test/engine-regressions/function-metadata.wast)
checks exact-sized import/declaration storage with mixed import kinds,
import-only modules, and binary modules using combined indexes through 16,383.
It exercises export/global/element/ref.func paths, undeclared reference rejection,
partial import failure and recovery. Run
`build/cli-rt/wast --verbose src/vfs/root/test/engine-regressions/function-metadata.wast`,
or `/bin/wast --verbose /root/test/engine-regressions/function-metadata.wast`
in Bash after `vfs-tests-install`. The production worker scenario is
`src/vfs/root/test/guest-session/function-metadata.json`. Existing per-kind resource limits
remain unchanged; this does not accept large LLD startup or its full footprint.

The private native footprint diagnostic builds with
`make -C src/cli-rt module-footprint` (or `module-footprint-sanitize`) and runs as
`build/cli-rt/module-footprint build/aux/clang/wasm-ld.wasm > build/engine/logs/clang-module-footprint-lld.json`.
It decodes and releases one body at a time; no imports or start function run,
and semantic validation is skipped. The JSON scopes its storage counts and
omissions explicitly. Normal loading keeps its definition cap and validation.
The portable [function-envelope regression](../../vfs/root/test/engine-regressions/function-envelope.wast)
checks exact body termination plus ordinary semantic/start behavior. Run it
with cli-rt or `/bin/wast --verbose /root/test/engine-regressions/function-envelope.wast`
in Bash; `src/vfs/root/test/guest-session/function-envelope.json` runs the browser worker.

For semantic validation with one decoded body live at a time, use:

```sh
build/cli-rt/module-footprint --validate-functions build/aux/clang/wasm-ld.wasm > build/engine/logs/clang-stream-validation-lld.json
build/cli-rt/wast --verbose src/vfs/root/test/engine-regressions/stream-validation.wast
```

This mode uses inert function bindings and private memory/table/global/tag
placeholders. It never runs a start function or returns a runnable instance.
`validated` describes function/loader checks, not real linking or successful
instantiation. Failed bodies are excluded from measured counts. Ordinary
loading still rejects invalid unused bodies and executes starts. In Bash run
`/bin/wast --verbose /root/test/engine-regressions/stream-validation.wast` after
`vfs-tests-install`; `src/vfs/root/test/guest-session/stream-validation.json` drives the same
six assertions through the production browser worker. Full LLD validation is a
prerequisite for bounded execution-code retention, which remains pending.
