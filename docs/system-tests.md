# System tests

`make -C src/aux test-child-deadline` checks shared absolute-deadline
child waits, output validation, interruptions and concurrent nested parents.
`test-child-deadline-controlled` checks exact deadlines, original-deadline retries,
alarm independence and cleanup with native sanitizer/browser API parity. All
runtime expectations live in WAST; see the
[child deadline fixture](../src/aux/clang/test/child-deadline/README.md).

`make -C src/aux test-finite-wait` exercises actual archive-linked LLVM
Wait/ExecuteAndWait/ExecuteNoWait with positive timeouts in 217 WAST assertions.
`test-finite-wait-controlled` checks exact expiry, original-deadline retries,
alarm independence, cleanup and UINT64_MAX saturation with native sanitizer/
browser API parity. See the [finite-wait fixture](../src/aux/clang/test/finite-wait/README.md).

`make -C src/aux test-crash-recovery` checks actual archive-linked LLVM
guest signal recovery, nested/explicit exits, context/resource cleanup and real
Signals file/callback cleanup. `test-checkpoint` verifies compiled C/C++ mask
restoration across copied tokens, sleep, handler jumps and fork. Expectations
live entirely in WAST; see the [recovery fixture](../src/aux/clang/test/crash-recovery/README.md)
and [checkpoint fixture](../src/aux/clang/test/checkpoint/README.md) for runtime parity and scope.

`make -C src/aux test-watchdog` checks public alarms and the complete
upstream LLVM Watchdog source: replacement, cancellation, unsigned remaining
time, fork/exec ownership, READ/waitpid interruption, CPU-bound delivery and
concurrent child deadlines. `test-watchdog-controlled` checks exact injected
clock boundaries and masked pending signals. Runtime assertions live in WAST;
see the [watchdog fixture](../src/aux/clang/test/watchdog/README.md) for reproduction steps.

`make -C src/aux test-signals` checks public signal registration and
actual LLVM ordinary callbacks/file cleanup. Its 136 WAST assertions include
atomic error handling, ignored utility dispositions, legacy/versioned layouts,
mask-restoration delivery and signal statuses in forked applications. See the
[signal fixture](../src/aux/clang/test/signals/README.md) for source scope and native/browser steps.

`make -C src/aux test-session` checks shared process session identity,
terminal detachment, nested fork/exec and preserved parent ownership. All 86
runtime assertions live in WAST; see the
[session fixture](../src/aux/clang/test/session/README.md) for native/sanitizer/worker steps,
private terminal-ownership guards and the remaining job-control scope.

`make -C src/aux test-argument-limits` checks both actual staged LLVM
argument-fit overloads, engine argv/environment budgets, rejected-exec state
and interpreter expansion. The 186 WAST assertions derive boundary inputs
from the shared kernel's limits. See the
[argument-limit fixture](../src/aux/clang/test/argument-limits/README.md) for native, sanitizer
and packaged-worker/manual steps and its source-extraction scope.

`make -C src/aux test-program-wait` checks the actual staged LLVM
child-wait method through shared waitpid, including polling, exit/error status,
signals, interruption and rejection of unavailable statistics.
All 254 runtime assertions live in WAST; see the
[LLVM wait fixture](../src/aux/clang/test/program-wait/README.md) for its extraction scope and
native/sanitizer/browser steps. Actual LLVM execution methods have separate
archive-linked execution and finite-wait gates.

Stage tests here when they exercise interactions among packages, the engine
kernel, runtimes or compiler SDK. Package-only sources, WAST assertions and
test instructions belong under `src/aux/NAME/`.

`make -C src/aux test-libc-system` runs the authored `libc/*.wast` against
the installed shared library inside native Bash. These cover filesystem and
loader errors, readiness, wire layouts, terminal callbacks, resource limits
and temporary files. The shared transport only stages inputs and runs Bash;
all runtime assertions are in the WAST sources. `WASM_RUNNER` selects the
`wasm-sanitize` frontend. Results go under `build/system-tests/libc/run-*/`.

Install snapshots with `make -C src/html-rt vfs-tests-install`, rebuild the page,
then run from `bash.html`:

```sh
status=0
for test in /root/test/system/libc/*.wast; do
  /bin/wast --verbose "$test" || status=1
done
printf 'system test status: %s\n' "$status"
```

These tests also run with `build/cli-rt/wast --verbose FILE` or `wast --suite`
using an isolated production runtime context. They use the C kernel/library providers; the OCaml reference
implementation remains a WAT/WAST language reference only.

See [Bash WAST reporting](../src/vfs/root/test/system/wast/README.md) for passing/failing progress controls,
quiet behavior, redirection and download steps.

`make -C src/aux test-abort` checks shared-libc abort, compiled signal
handlers, child wait statuses and parent survival. See
[abort scenarios and browser instructions](../src/engine/test/abort/README.md).

`make -C src/aux test-stdio` checks the public stdio/compiler boundary:
real C and C++ clients capture variadic formatter observations, and WAST
checks counts, truncation, argument preservation and output canaries. See
[formatter probes](../src/aux/clang/test/stdio/README.md) for native/sanitizer/browser steps.

`make -C src/aux test-wide-format` checks compiled C/C++ bounded wide
formatting, including mixed double/binary128 arguments, full Unicode elements,
UTF-8 precision, repeated variadic calls, return counts and guarded buffers.
The [wide-format probes](../src/aux/clang/test/wide-format/README.md) keep all expectations in
WAST and document native, sanitizer and production-worker execution.

`make -C src/aux test-classification` checks compiled C and `<cmath>`
C++ classification for float, double and binary128 long double. WAST verifies
the actual classifications, signs, single evaluation and errno. See
[classification probes](../src/aux/clang/test/classification/README.md) for reproduction.

`make -C src/aux test-wide` checks generated libc++ wide-string
headers and the real shared wide-memory/search, integer and floating
conversion providers. Its compiled C++ observer captures values; all 64 functional
assertions belong to WAST. See
[wide-string probes](../src/aux/clang/test/wide/README.md) for native/sanitizer/browser steps
and the limits of this check while full runtime archives remain unfinished.

`guest-sdk/` holds compiler-boundary probes, the SDK check and the sysroot
copier. `make -C src/aux check-sdk` checks header preprocessing,
include order, ABI/declaration signatures and negative compilation cases.
These checks necessarily compile C: WAST cannot test C header declarations.
`sysroot` stages installed headers and CRT into
`build/system-tests/guest-sdk/sysroot`; `crt-fixture` compiles/audits the startup
probe under `build/system-tests/guest-sdk`. `SYSROOT_DIR` and `BUILD_DIR` override these build destinations.
The HTML Makefile's old coreutils sysroot/startup targets delegate here; libc
neither builds nor owns these SDK fixtures.

`clang/` holds the C++ SDK probe and the pending guest compiler/linker/executor
checks. See [Clang boundaries](../src/aux/clang/test/README.md) and
[the active port plan](active-clang-plan.md). The browser worker can
already run the new libc prerequisites with
`node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html
src/aux/libc/test/system/compiler-support.json`; the WAST files own its assertions.

`make -C src/aux test-target-host` links LLVM's real guest
`LLVMTargetParser` archive and checks its configured Wasm target identity in
WAST. See [the host identity boundary](../src/aux/clang/test/target-host/README.md) for native,
sanitizer and packaged-worker steps; full `lld` linking is still pending.

`make -C src/aux test-dwarf-verifier` checks LLVM's actual diagnostic
aggregation, owned category keys and callbacks across shared yielding sleep.
The [DWARF owner fixture](../src/aux/clang/test/dwarf-verifier/README.md) also checks complete
guest/native/threaded source builds.

`make -C src/aux test-dwarf-context` checks LLVM's actual recursive
context cache, normal/split unit parsing and eager/lazy DWO behavior. See
[the context cache boundary](../src/aux/clang/test/dwarf-context/README.md) for native,
sanitizer, production source and packaged-worker checks. These focused tests
do not establish full DWARF conformance or a linked compiler/linker.

`make -C src/aux test-gsym` checks actual DWARF-to-GSYM conversion,
creator interning, both GSYM versions and byte orders, decoded address ranges
and reader ownership. See [the GSYM boundary](../src/aux/clang/test/gsym/README.md) for source,
sanitizer, worker and manual browser checks. Its task-count-3 cases exercise
LLVM's existing disabled-thread task-pool branch.

`make -C src/aux test-cfg-format` checks the production CFG node
formatter against exact attribute bytes, copied identifiers, unprofiled heat
colors and a callback that yields through shared sleep. See
[the CFG formatting boundary](../src/aux/clang/test/cfg-format/README.md) for complete source,
sanitizer, worker and manual browser checks.

`make -C src/aux test-inlining` checks the actual LLVM statistics
formatter, mixed compiled floating arguments and an imported-function graph
dump. See [the inlining boundary](../src/aux/clang/test/inlining/README.md). Shared floating
printf assertions remain in the libc package's `narrow-floating-format.wast`.

`make -C src/aux test-folding` checks production LLVM optional host
math folding. Unavailable guest computations decline, while exact APFloat
folds, supported shared providers and retained IR calls remain verifiable.
See [the optional folding boundary](../src/aux/clang/test/folding/README.md).

`make -C src/aux test-code-layout` checks required LLVM ExtTSP layout
and scoring through the production CodeLayout object, including real option
parsing and retained results. See [the code-layout boundary](../src/aux/clang/test/code-layout/README.md)
for the guest CDSort exclusion audit and native/browser reproduction.

`make -C src/aux test-assignment-format` checks the actual private LLVM
assignment-tracking interval formatter, including full-width bounds, binary
names and returned-string ownership. See
[the assignment formatter](../src/aux/clang/test/assignment-format/README.md) for source,
sanitizer and browser checks.

`make -C src/aux test-shadow-callbacks` checks the actual LLVM stack
callback initializer, including lowercase hexadecimal names, pointer-width
signatures, declaration reuse and retained names. See
[the callback declaration fixture](../src/aux/clang/test/shadow-callbacks/README.md) for source,
sanitizer and browser checks.

`make -C src/aux test-memprof-format` checks actual regular/ThinLTO
graph identifiers, pointer boundaries and retained real-node attributes through
cli-rt. See [the MemProf formatter](../src/aux/clang/test/memprof-format/README.md) for complete
source, sanitizer and packaged-worker checks.

`make -C src/aux test-managed-static` checks LLVM's actual lazy managed
registry and explicit shutdown lifecycle using real guest C++ archives. See
[the managed registry probe](../src/aux/clang/test/managed-static/README.md) for WAST,
sanitizer and packaged browser execution. C++ static-exit callbacks remain a
separate pending compiler boundary.

`make -C src/aux test-mustache` checks guest LLVM numeric interpolation
and heap-backed string ownership against explicit WAST and a native stream
reference. See [the numeric probe](../src/aux/clang/test/mustache/README.md) for reproduction
and the boundary between the helper and pending full template rendering.

`make -C src/aux test-parallel` checks LLVM's actual TaskGroup and
scalar algorithms, plus checked latch counts and release-build child rejection
paths. See [the parallel probe](../src/aux/clang/test/parallel/README.md) for WAST, sanitizer
and packaged browser execution. The compiler's disabled-thread profile is
separate from WASTE's process/thread scheduling.

`make -C src/aux test-time-profiler` checks LLVM's actual entry timing,
owning metadata and instant storage, with normal static-destructor flags. See
[the profiler storage probe](../src/aux/clang/test/time-profiler/README.md) for native,
sanitizer and packaged browser steps. A separate archive-linked public-API fixture
checks initialization, nested/async events, aggregation, exact JSON, file writers,
granularity, cleanup/reuse and static registry destruction. Its native and
browser drivers share existing injected clocks and sleep continuations.

`make -C src/aux test-module-cache` links actual Clang cache sources
against LLVM Support and current C++ archives. WAST checks buffer/invalidation
ownership, module states, advisory lock generations, timed waits and child
rejection. The [module-cache probe](../src/aux/clang/test/module-cache/README.md) includes real
guest CMake/PCH and native/threaded source gates. It reuses shared clock, signal
and sleep controls; full dependency scanning and Clang acceptance remain separate.

`make -C src/aux test-module-tasks` checks the production Clang module
compilation owner with LLVM's existing disabled-thread TaskGroup and actual
cache locks. Its WAST assertions cover recursive work, capture cleanup and
ownership across interrupted shared sleep. See
[module task ownership](../src/aux/clang/test/module-tasks/README.md) for the source gates and
native/sanitizer/packaged-worker reproduction steps.

`make -C src/aux test-lld-diagnostics` checks actual LLD diagnostics,
context initialization, fatal recovery and process cleanup. All expectations
live in WAST; the [LLD fixture](../src/aux/clang/test/lld-diagnostics/README.md) includes native,
sanitizer and production-worker steps and the Wasm-only driver selection scope.

`make -C src/aux test-lld-filesystem` checks actual LLD filesystem
helpers and LLVM output ownership, including replacement with a retained open
descriptor, temporary-file cleanup, ordinary/LTO streams and pointer formatting.
The [filesystem fixture](../src/aux/clang/test/lld-filesystem/README.md) keeps all expectations
in WAST and includes native, sanitizer, packaged-worker and source-build steps.

`make -C src/aux test-lto-errors` checks LLVM's actual ThinLTO error
owner and queued executor, including joined/reentrant cleanup and captures
surviving interrupted shared sleep. The [LTO fixture](../src/aux/clang/test/lto-errors/README.md)
also checks actual profile context formatting and uses real configuration
constructors. It documents native, sanitizer, packaged-worker and complete
source gates; full bitcode compilation remains separate acceptance work.

`make -C src/aux test-raw-stream` checks LLVM's retained memory-output
core, including buffering, flushes, binary data and integer formatting. See
[the raw-stream probe](../src/aux/clang/test/raw-stream/README.md) for native, sanitizer and
packaged browser steps. The standard-stream adapter has no caller in the guest
Clang/LLD graph; full LLVM descriptor/Process support remains pending.

`make -C src/aux test-platform` checks compiled C++ mapping and dynamic
loading calls against existing providers, plus actual LLVM unsupported capacity
and locality queries. See [the platform probe](../src/aux/clang/test/platform/README.md) for
native, sanitizer and packaged browser steps. Its error-category object now
retains normal static registration through shared libc. Full Path/Memory/DynamicLibrary acceptance remains pending.

`make -C src/aux test-executable-path` checks the engine-owned guest
image pathname and actual LLVM getMainExecutable calls through shared libc.
See [the executable-path probe](../src/aux/clang/test/executable-path/README.md) for native,
sanitizer and packaged browser steps. It covers relative invocation, fabricated
argv, cwd/rename/unlink independence, fork/exec identity, bounded copies and
open-description lifetime through unlink and fork. Complete Path support and
cross-fork shared-file offsets remain separate acceptance work.

`make -C src/aux test-symlink` checks compiled C++ symlink/symlinkat
providers and shared component resolution, including dirfds, dangling/looping
links, no-follow behavior, mutation and Bash executable loading. See
[the symbolic-link probe](../src/aux/clang/test/symlink/README.md) for native, sanitizer and
packaged worker steps. Complete LLVM Path execution remains pending.

The [Clang hard-link boundary](../src/aux/clang/test/hardlink/README.md) uses public shared-libc
`link`/`linkat` and compiled C++ fork/wait observations. `test-hardlink` runs its
269 WAST checks for inode data/metadata sharing, namespace counts, unlink lifetime,
symlink flags, dirfds, errno and stat ABI preservation.

`make -C src/aux test-accounts` checks the public C++ reentrant account
lookup declarations, caller-owned record/string storage and sysconf contracts.
See [the account boundary](../src/aux/clang/test/accounts/README.md) for its 47 WAST checks and
native, sanitizer and packaged worker commands. Full LLVM Path execution
remains pending.

## Mapping advice

`make -C src/aux test-advice` verifies the shared advisory provider and
unchanged LLVM mapping methods in ordinary guest processes, with all assertions
in WAST. See [clang/advice/README.md](../src/aux/clang/test/advice/README.md) for sanitizer,
production worker and optional manual browser steps.

`make -C src/aux test-realpath` verifies canonical guest namespace
lookup, libc-owned output, bounded copies and the public C++ declaration in
213 WAST checks. See [the canonical-path fixture](../src/aux/clang/test/realpath/README.md) for
native, sanitizer, production-worker and optional manual browser steps. Private
credential injection checks search permissions independently; complete LLVM
Path execution remains pending.

The [Clang record-lock fixture](../src/aux/clang/test/locks/README.md) checks process locks,
blocking/signal scheduling and unchanged LLVM blocking lock methods with
`make -C src/aux test-locks`.

The [timed-lock and usleep fixture](../src/aux/clang/test/timed-locks/README.md) verifies
unchanged LLVM timed retries with `test-timed-locks`, plus precise sleep
deadlines and interruption with `test-usleep-controlled`. Both use the
existing engine sleep/readiness path and keep runtime expectations in WAST.

The [LLVM process fixture](../src/aux/clang/test/process/README.md) uses the complete staged
Process.cpp to check real PID/wall-time calls, the public resource ABI and
immediate child termination with `test-clang-process`. Guest CPU accounting
returns an unsupported error; LLVM selects its existing wall-time fallback.
Full crash-context handling and C++ shutdown remain separate acceptance work.

The [public exec/wait fixture](../src/aux/clang/test/exec-wait/README.md) uses ordinary C++
children and `test-exec-wait` to verify replacement, environment/descriptors,
any-child waits, interruption/restart and atomic status validation. Runtime
expectations remain in WAST; actual LLVM Program execution has a separate gate.

The [archive-linked LLVM execution fixture](../src/aux/clang/test/program-execution/README.md)
uses `test-program-execution` to exercise actual ExecuteAndWait/ExecuteNoWait,
argument/environment ownership, redirects, polling, concurrent children and
detachment through the real Support library. Unsupported optional requests fail
before fork. Empty redirect paths use the shared null character device in both
synchronous and detached children. Positive waits have the separate archive-linked
finite-wait gate; complete compiler startup remains acceptance work.

The [shared device fixture](../src/engine/test/devices/README.md) runs with `test-devices` and
checks `/dev/null` metadata, descriptor/path operations and immediate readiness
entirely in WAST. Both runtimes synthesize the device independently of packaging.

The [C++ startup fixture](../src/aux/clang/test/startup/README.md) uses `test-startup` to verify
stream binding before real constructors, initialization order, yielding sleep,
argv/environment and actual C++ archive ownership/alignment in both production
CRT main-signature variants. Its 172 runtime assertions live entirely in WAST;
static exit registration and shutdown are checked separately below.

The [C++ shutdown fixture](../src/aux/clang/test/shutdown/README.md) uses `test-shutdown` to
verify compiler-generated globals/local statics, C callbacks, selective and
recursive finalization, registration during cleanup and actual LLVM cache
allocation release. Normal and legacy imports share one libc path. WAST also
checks yielding cleanup, fork isolation, exec replacement/rejection and
immediate/abort bypass: 305 assertions per artifact, 610 total.

`test-timestamp` checks Clang calendar formatting and its debug sleep through
shared clock/sleep support; see [timestamp](../src/aux/clang/test/timestamp/README.md).
`test-coverage-format` checks actual LLVM MC/DC record text and owned string
lifetimes; see [coverage formatting](../src/aux/clang/test/coverage-format/README.md). Both
keep functional assertions in WAST and support native/sanitizer/browser-worker
execution. They do not substitute for actual guest compiler acceptance.

`test-dylib-uuid` checks the real LLVM Mach-O UUID header reader through
actual parsed objects and WAST output assertions; see the
[UUID fixture](../src/aux/clang/test/dylib-uuid/README.md) for native/sanitizer/worker steps.

`test-issue-hash` checks real Clang line normalization and MD5 through actual
source-manager/lexer inputs; see the
[issue-line fixture](../src/aux/clang/test/issue-hash/README.md) for native/sanitizer/worker steps.

`thread-safety-source` checks all four real thread-safety analyzer owners and
preserves their native stream adapter; this is a private source-compatibility
gate, with scope in [the source notes](../src/aux/clang/test/thread-safety/README.md).

`unsafe-buffer-source` checks all real unsafe-buffer string-stream owners.
After compiler installation, `test-unsafe-buffer` invokes actual Clang and
checks fix-it ranges/text in WAST; see
[unsafe-buffer acceptance](../src/aux/clang/test/unsafe-buffer/README.md).

`test-seed-file` compares the real compiler seed-line adapter with the original
native stream reader; see [seed-file input](../src/aux/clang/test/seed-file/README.md). The
complete invocation owner also has native/threaded source gates.
