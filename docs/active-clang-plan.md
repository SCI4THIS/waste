# Guest Clang and WebAssembly linker

## Completed acceptance status

Completed on 2026-10-17. The normal LLVM 24 guest compiler and selected
non-LTO Wasm linker are built and installed. Actual installed Clang compiles C, invokes installed LLD and
executes the emitted command through `/bin/wasm` in native Bash and the
production browser worker. Native and ASan/UBSan package acceptance each pass 7/7 assertions;
the worker passes 8/8 results including Bash launch. Real unsafe-buffer C++
fix-it acceptance passes 13/13 assertions natively and under ASan/UBSan, and
14/14 worker results. The full installed native corpus passes 327 files and
65,432 assertions, with 103 context-dependent skips and no failures.

Generic lazy function decoding is implemented. Every function is validated at
load; owned encoded bodies become decoded instructions on first call and stay
with their shared code owner. The measured native compile/link invocation now
uses 515 MiB peak RSS under the 2 GiB bound. This replaces the eager second-image
allocation that exhausted the bound. There is no compiler-specific execution
cache or eviction policy.

Installed artifacts are Clang 65,916,960 bytes and wasm-ld 6,975,173 bytes.
The compiler SHA-256 is
`6050671d43d940a1fb627c1aba3001ccef283b109c3b03d2bf6c91ac49ab6ec0`.
The final installation audit matches both executables, permissions, 44 SDK
files and the upstream license to their build outputs. SDK/provider audits,
VFS capacity/discovery and private continuation/exec ownership gates pass.
An unchanged compiler build reports nothing to do.

Scope remains WebAssembly object/archive compilation and linking against the
shared guest SDK. The installed linker explicitly rejects LLVM bitcode/LTO;
the original LTO-enabled artifact is preserved for comparison. Compiler-internal
threads, exceptions and optional host facilities remain disabled or explicitly
unsupported. WASTE's shared process/thread scheduler remains the execution
backend. Browser results here use the production C worker in the offline page;
they do not claim a new Firefox/Chromium GUI run.

The earlier source-port notes below record intermediate states and diagnostic
artifacts. Their references to pending installation or eager execution are
historical; this status and the final acceptance section take precedence.
Current operating instructions are in
[`src/aux/clang/README.md`](../src/aux/clang/README.md).

## Earlier source-port notes

Build Clang and LLD from the read-only `submodules/llvm-project` checkout as
guest Wasm commands. Run `/usr/bin/clang` from the installed Bash in both
runtimes; compile a guest C file, link it with `/usr/bin/wasm-ld`, and execute
the result with `/bin/wasm`. Reuse the existing guest libc and kernel.

This remains an active port until the final acceptance gates pass. The normal
Clang and non-LTO wasm-ld commands are now installed in the source VFS. The checked-out source is LLVM
24.0.0git at `318bc72539e370171877d293da2691475a24a052`.

A separate non-LTO LLD trial now links ordinary Wasm objects and archives in
native Bash, with 16 passing assertions in both normal and sanitizer runs.
Its isolated production-worker link/execution check passes all 11 results.
The subsequent shared-provider SDK link also passes nine assertions in native
and sanitizer runs and ten production-worker results, without rewriting its
output on the host. Actual guest Clang now links. A diagnostic relink starts
from native Bash and the packaged production worker, and native object
compilation succeeds. The normal package is installed. Lazy function decoding now lets combined
compilation/linking complete under 2 GiB; the final native/sanitizer/worker gates
are recorded at the end of this document.
Timestamp formatting and the debug-sleep pragma now pass source and
native/sanitizer/production-worker checks. The subsequent MC/DC coverage
formatter fix also passes native, sanitizer and packaged-worker checks.
TextAPI UUID reading now reuses LLVM's existing UUID writer and passes the
same source/runtime boundaries. Clang issue-line normalization now also
passes complete-source and native/sanitizer/worker checks. See the final
continuation sections. The unused thread-safety ostream adapter also has
complete guest-owner and native/threaded source verification. Unsafe-buffer
fix-it stream owners pass release/debug source gates; their new real-compiler
runtime scenario remains pending installation.
The layout-override and seed readers now share a private libc-backed line
reader and pass native, sanitizer and production-worker checks. The larger
layout fixture also exposed and verified fixes for the shared control bound,
native WAST stack ownership and browser engine allocation cost. See the final
continuation section for evidence and the current full-build handle.
The next HLSL Driver regex blocker is also resolved using LLVM's existing
backend, with full-source and native/sanitizer/worker parity checks. The ahead
source pass now covers all 93 previously unbuilt Frontend/Driver/FrontendTool
owners; executable linkage has since succeeded, while actual compiler
acceptance remains pending.
Removing the linker's LLVM bitcode optimizer/code generator reduces decoded
instruction arrays from 655.39 MiB to 75.87 MiB. The original LTO-enabled
artifact remains unchanged. See the final non-LTO section for measurements,
browser evidence and the remaining compiler acceptance boundary.

The complete guest `LLVMSupport` target now compiles and produces its archive.
Shared alarms and the whole upstream LLVM Watchdog pass native, sanitizer,
browser API and packaged-worker checks. Actual ExecuteAndWait/ExecuteNoWait now
link against this archive and pass native, sanitizer and packaged-worker WAST.
Executable starts defer pump yields through the existing synchronous loader
boundary. The shared kernel now supplies /dev/null, and actual LLVM empty
stdin/stdout/stderr redirects pass in synchronous and detached asynchronous
children. Shared absolute-deadline child waits now pass live/concurrent,
controlled-clock, sanitizer and packaged-worker WAST. Actual LLVM finite waits
now use that shared API, retaining polling children and killing/reaping only the
target on ordinary expiry; independent alarms and full-width deadlines pass
the same runtime checks. Shared blocking/nonblocking pipe semantics now pass
the restored full i32-smoke and posix-kernel gates. Root Bash can exec a WAST
handler and return its actual exit status. Actual archive-linked LLVM guest
signal recovery, nested/explicit exits and resource/file cleanup now pass in
native, sanitizer and browser runtimes. Nonzero sigsetjmp flags save and restore
the shared kernel mask across copied tokens, yield, handlers and fork.
Production C++ constructor startup, stream ordering and archive-backed
allocation/alignment now pass with both CRT main signatures. Shared libc now
executes real C/C++ static exit registrations, including LLVM cache destruction,
yielding cleanup and fork/exec boundaries. Actual archive-linked TimeProfiler
initialization, aggregation, exact JSON/file output, cleanup/reuse and static
registry destruction now pass native, sanitizer and packaged-worker WAST.
Broader LLVM initialization/formatting and full Clang/LLD acceptance remain.

Clang's in-process module cache now passes native, sanitizer and packaged-worker
WAST using actual cache sources and Support. Compiler-local synchronization
aliases also let the dependency filesystem, preamble and module-driver owners
compile, while native/threaded branches retain standard types. Timed predicates
reuse shared clocks/sleep; unfinished unbounded waits reject explicitly. The
module compile owner now reuses LLVM TaskGroup in that same explicit profile,
preserving recursive submissions, capture cleanup and real cache locks across
interrupted shared sleep. Complete guest/native/threaded worker source gates
pass, as do all seven production dependency-scanning source rules. Actual LLD
diagnostics/context initialization, location matching, error limits and fatal
cleanup now pass native, sanitizer and packaged-worker WAST. The explicit guest
LLD selection builds only Common/Wasm drivers and references only Wasm from its
entrypoint; native selection retains all drivers. Diagnostic/context/entrypoint
source gates pass. LLD's filesystem helper now uses the upstream one-thread
fallback in that explicit guest profile; actual output replacement, retained
descriptors and ordinary/LTO streams pass native, sanitizer and packaged-worker
WAST. Public pointer-format macros resolve the Common DWARF source dependency.
All 13 Common source rules, all 14 Wasm source rules, the entrypoint and all
six LLVM LTO source rules compile with their real PCH and generated headers.
Version metadata identifies the LLVM submodule rather than the containing
WASTE repository. ThinLTO error aggregation and sample-profile context
formatting now pass actual-source and native/sanitizer/browser-worker checks.
The generated scalar-options function also runs within a shared 512-block
control capacity. The real `LLVMTargetParser` guest archive now builds and its
configured Wasm target identity passes native, sanitizer and packaged-worker
WAST. The resumed `lld` graph builds LLVMCore, TextAPI, BitReader, AsmParser,
IRReader and FrontendHLSL archives. The next DWARF source blocker, missing
public `PRIx8`, is resolved; compiled C/C++ formatting and the exact DWARF
object pass. The full graph builds the low-level DWARF archive and its dependent readers. Both DWARFVerifier mutex owners compile using LLVM's existing
disabled-thread guest backend, with native/threaded types preserved. Actual
diagnostic aggregation, owned keys, callbacks and yielding callback cleanup
pass native, sanitizer and packaged-worker WAST. The next checked source
blocker was DWARFContext's recursive mutex owner. It now uses that same
disabled-thread guest backend and its complete context object compiles, with
native/threaded source checks passing. MC, MCParser and Object archives build
after three guest-only raw-stream formatter adaptations. The subsequent DWARF
address-table blocker, missing `PRIu8/16`, is resolved and compiled C/C++ tests
pass native, sanitizer and packaged-worker checks. The complete DWARF archive
and dependencies now build. Actual context cache access, recursive yielding
callbacks, eager DWO parsing and MC inline-context formatting pass native,
sanitizer and packaged-worker WAST. Native/threaded source checks also pass.
The subsequent GSYM creator and transformer mutex owners now use the same
existing guest wrapper with native/threaded types preserved. Actual DWARF-to-GSYM
conversion, both format versions and byte orders, interning, decoded ranges and
copied ownership pass native, sanitizer and packaged-worker WAST. The complete
GSYM, MSF and CodeView archives build. The resumed graph also completes PDB,
BTF, Symbolize and ProfileData, then reaches the CFG printer in Analysis at 45%.
Its omitted string-stream owner now uses LLVM's raw string sink only in the
disabled-thread guest, with native/threaded streams preserved. Actual node
attributes, copied identifiers and a yielding callback pass native, sanitizer
and packaged-worker WAST. Shared libc supplies the heat helper's binary64
`log2` and ties-away `round`. The resumed graph refreshes the affected
dependencies and continues into Analysis. Inlining statistics now formats through
LLVM's raw string sink and shared narrow floating printf, preserving standard
streams in native/threaded profiles. Actual helper output, compiled mixed
variadic arguments and a real inlining graph pass native, sanitizer and
packaged-worker WAST. The complete libc package passes 787 assertions natively
and with sanitizers. Manual offline-browser acceptance for the inlining and
narrow floating printf fixture is recorded from the user-supplied downloads.
ConstantFolding now declines optional host computations without guest math
providers, preserving exact APFloat cases and supported shared computations.
Its production dependency objects pass 63 native/sanitizer WAST assertions and
64 packaged-worker results. Manual offline-browser acceptance for optional
folding is recorded from the user-supplied downloads. Native and threaded
source profiles remain compatible, and SDK availability annotations remain
intact. The resumed full
graph completes Analysis, IRPrinter, BitWriter, CGData and CodeGenTypes, reaching
TransformUtils at the bounded build's 55% marker. The folding fixture now runs
against the completed Analysis archive. Full Clang/LLD linking and execution
remain. TransformUtils then exposes CodeLayout's unavailable `pow` in CDSort.
Its only production callers are excluded ELF/BOLT tools; the Wasm guest now
omits that unused implementation and its options while retaining ExtTSP.
Actual ExtTSP layouts, scores, retained output and real option parsing pass
76 native/sanitizer assertions and 77 packaged-worker results. Manual
offline-browser acceptance is recorded from the user-supplied code-layout
downloads. Complete native/threaded source profiles preserve native CDSort. The
complete TransformUtils, AggressiveInstCombine, InstCombine and ScalarOpts
archives now build. A bounded full graph reaches ScalarOpts at 65%
without a compile error; a targeted follow-up completes that archive. All 100
members of the two new archives match their production objects, and their
generated component rules repeat without compilation or linking. The LLD
dependency graph subsequently builds ObjCARCOpts and Target. CodeGen's assignment
interval formatter now uses LLVM's raw string sink in the disabled-thread guest,
retaining native/threaded standard streams. Actual-source formatting and
returned-string ownership pass 23 native/sanitizer WAST assertions and 24
packaged-worker results.
Manual offline-browser acceptance is recorded from the user-supplied assignment
formatter downloads. The next bounded graph run completes the full CodeGen
archive: all 238 members match their compiled production objects. It reaches
SelectionDAG at 80% without a compile error before its requested time limit.
The following bounded run completes SelectionDAG, AsmPrinter, GlobalISel and
three small LLVM archives, then enters ObjectYAML without a compile
error. All 85 members of those six archives match their compiled production
objects. The next run completes ObjectYAML, FrontendOffloading and
FrontendOpenMP, then exposes Instrumentation's shadow-callback name formatter.
That formatter now uses LLVM's raw string sink and two-digit lowercase hex in
the disabled-thread guest; native/threaded streams remain standard. Actual
callback declarations pass 47 native/sanitizer WAST assertions and 48 packaged
worker results. The full Instrumentation archive builds. All 68 members of
these four new archives match their compiled production objects. SandboxIR and
Vectorize now build as well: all 60 members match their production objects,
and their generated component rules repeat without compilation or linking.
LLVMLinker now builds, and MemProf's graph identifier helper uses LLVM's
existing lowercase-hex helper in the disabled-thread guest. Native/threaded
streams remain standard. Actual regular/ThinLTO IDs and retained real-node
attributes pass 34 cli-rt/sanitizer WAST assertions and 35 packaged-worker
results. The complete IPO archive builds, with all 49 members of the two new
archives matching their production objects. The next bounded run completes
Coroutines, CFGuard, HipStdPar, Passes, Plugins, LTO, MCDisassembler and the
WebAssembly Info, Desc and Utils archives without a compiler error or warning.
All 43 members of these ten archives match their production objects; repeating
their component rules changes no artifacts. All 64 LLVM archives in LLD's
recipe are now present, with none of its 1,641 direct source objects absent.
WebAssembly CodeGen/AsmParser/Disassembler and LLD Common/Wasm build; all 83
members of these five archives match their production objects. The first
nominally successful executable link exposed a mangled C++ `main`, leaving
`env:main` imported and discarding the real driver. The guest driver template
now gives `main` C linkage. The apparent final-link wait is the host Clang
driver's automatic Binaryen pass. Feature-detecting `--no-wasm-opt` keeps `-Os`
source compilation and lets the actual final link finish in 3.70 seconds.
Its 53.34 MiB artifact has no imported `main` or C++ functions. Shared libc now
provides binary32 `log2f` through its existing binary64 logarithm; all 88 libc
imports verify, and no env function is absent from the adapter policy. The
shared executable-byte limit is now 64 MiB; native and
ASan/UBSan application entrypoints each pass 47 checks. The rebuilt packaged
worker passes 35/35 MemProf regression results. The new logarithm passes all
60 native/sanitizer WAST assertions and 61 packaged-worker results including
launch.
The shared module type bound is now 256, coordinated across WAT, binary
references, the executor and structural comparison. Binary type tables reserve
actual entries and grow for recursive groups; parser-only forward references
preserve resolved types and field metadata. The portable regression passes 15
assertions natively, in Bash and under ASan/UBSan, plus 16 packaged-worker
results including launch. Five focused official type/reference files pass 66
assertions normally and under sanitizers. cli-rt now accepts LLD's 230 types
and stops at `too many functions: 90685 exceeds 8192`. Decoded local types now
allocate only declared locals, reducing this artifact's local-type storage from
708.48 MiB to 1.09 MiB. Combined parameter/local bounds are checked before
allocation and validation. Instruction buffers now grow by decoded instruction
count and trim after decoding, replacing the old one-record-per-byte reservation
that projected 2.75 GiB for this artifact. Immediate payloads now share an
opcode-selected union: records are 56 bytes on both native and Wasm32, down
from 104 and 96 respectively. The same native allocation probe now retains
112,056 bytes, versus 208,104 after the growth-only change and the original
1,040,208-byte reservation. Streaming inspection now measures all 90,685 LLD
bodies: 12,271,818 instructions need 687,221,808 bytes (655.39 MiB) of arrays,
which alone exceed the former configured 512 MiB browser ceiling. Function
bindings and declaration maps now allocate actual counts and share immutable ownership across clones. The
fixed native engine structure falls from 373,768 to 168,984 bytes. Combined
indexes through 16,383 pass while the separate defined-function and import
limits were 8,192 each during those measurements. The current defined-function
limit is 131,072 and imports remain capped at 8,192, coordinated in
`src/config.h`. `HTML_RUNTIME_MAX_MEMORY_BYTES` now sets a 2 GiB browser
maximum; the initial allocation stays 64 MiB. The private diagnostic still
validates all 90,685 bodies with about 109 MiB native RSS by releasing each
body. Ordinary eager execution now loads the real LLD artifact and prints its
WebAssembly linker version with exit 0: the bounded native run takes 17.52
seconds and peaks at 919.96 MiB RSS under a 2 GiB address-space cap.
The first real native object/archive link now passes: guest LLD resolves
host-compiled Wasm inputs, writes a module, and Bash executes it with status 37.
Ten WAST assertions check the statuses and emitted header. This required a
shared kernel fix separating mapped files from the allocator's linear heap,
and binding WAST assertions to their process capsule for VM imports. See the
native object/archive slice below; guest Clang and full SDK linking are pending.
Code/local/payload storage plus common metadata has a 662.35 MiB eager lower
bound, excluding source, guest memory, VFS and runtime state. Lazy function
loading remains a useful optimization; eviction of decoded bodies is deferred
until actual working-set measurements justify it. Neither optimization is a
prerequisite to native linker acceptance at the larger ceiling. Browser LLD startup still exceeds the 120-second worker probe;
its load-time performance remains open. See the 2 GiB slice below.
The new six-assertion stream-validation regression covers unused invalid
bodies and ordinary start behavior. Detailed acceptance results are recorded
in the streamed-validation slice below; earlier measurement/import/compaction
acceptance remains in its respective slices.
Full LLVM dependency refresh after
header changes remains incomplete, despite all archives being physically
present. These counts describe
build artifacts, not compiler acceptance. No compiler or linker is installed.

## Build and package wiring

- [x] Add `src/aux/clang` with staged upstream sources, CMake profiles and
  separate native-generator/C++-runtime/guest build directories.
- [x] Expose `clang`, `install-clang`, `test-clang`, `clang-source`,
  `clang-native-tools`, `clang-cxx`, `clang-support`, `clang-dwarf`, `clang-gsym` and
  `clang-sdk-check` through the aux Makefile.
- [x] Register `clang` and `wasm-ld` with the explicit VFS installer and delegate
  the wizard's Clang entry to `install-clang`.
- [x] Default LLVM compilation to one job to constrain desktop memory use.
- [x] Configure libc++ using the actual guest headers, without host C headers.
- [x] Fix C linkage in the initial C++ SDK headers and avoid redeclaring the
  C++ `char16_t`/`char32_t` keywords.
- [x] Add guest `div`, `ldiv`, `lldiv`, `llabs`, and `remove`, including WAST
  regressions for signed aggregate returns and file/directory behavior.
- [x] Build matching native `llvm-tblgen` and `clang-tblgen` from the staged
  LLVM revision, with no writes to the submodule.
- [x] Implement terminating shared-libc `abort()` and the legacy `env.abort`
  adapter using engine-owned SIGABRT termination. Verify blocked/ignored,
  caught/returning and recursive handlers, raw child wait statuses and parent
  survival in native/sanitized Bash and the packaged browser worker.
- [x] Give `unistd.h` C linkage in C++, including its shared `mkstemp` and
  `mkostemp` declarations; extend the C++ SDK probe to cover this include order
  and `std::abort()`.
- [x] Implement shared-libc `aligned_alloc` and general power-of-two
  `posix_memalign`, with ordinary free/realloc ownership and reusable padding.
  Check invalid inputs, output/errno preservation, overflow and mixed live blocks
  in WAST. Add `std::aligned_alloc()` to the C++ SDK probe.
- [x] Build the bootstrap `libc++abi.a`; let its typeinfo implementation use
  RTTI while retaining the libc++/LLVM targets' own no-RTTI profiles.
- [x] Add the fixed Wasm floating-point environment header/shared-libc API:
  nearest rounding, empty exception set, checked snapshots and rejected
  unsupported changes. Verify actual arithmetic and `<cfenv>` declarations.
- [x] Expose realtime/monotonic `clock_gettime` through shared libc and the
  injected kernel clocks. Check timespec encoding, errors, yielding sleep,
  independent controlled clocks and C++ linkage; select libc++'s private
  supported clock backend without advertising the POSIX timer suite.
- [x] Expose the existing shared `vsnprintf`/`vsprintf` providers in the public
  stdio header. Verify bounded output in package WAST and real compiler-generated
  C/C++ `va_list` calls in system WAST, including counts, canaries and reuse.
- [x] Add public `PRIX8/16/32/64` for the actual guest fixed-width types.
  Verify compiled C/C++ uppercase output, promotions, high bits, mixed-width
  alignment, padding and truncation through the existing shared formatter.
- [x] Implement shared `strtoul`, `strtoll` and `strtoull`, and correct the
  common integer parser used by `strtol`/`strtoimax`/`strtoumax`. Verify both
  guest widths, bases, end pointers, errno and overflow; check C++ declarations.
- [x] Add public floating-point classification macros/constants and C linkage
  for existing math providers. Verify compiled C/C++ calls for all three guest
  widths; correct binary128 compiler comparisons without narrowing.
- [x] Build the bootstrap guest C++ runtime archives. Full link/startup and the
  feature set needed by LLVM remain acceptance work below.
- [x] Put generated libc++ wrappers before public C headers in the guest
  toolchain and explicitly select libc++ for LLVM's guest configuration.
- [x] Configure the guest executable/static-component graph with PIC, excluding
  optional shared C API libraries/plugins and using the guest host triple.
- [x] Adapt LLVM's bare-Wasm endian header spelling privately, using public SDK
  byte-order definitions; verify compiled LLVM operations and stored bytes in WAST.
- [x] Enable libc++ wide-character headers; add real shared wide-memory and
  reverse/set/substring search providers, consistent C++ linkage and guest
  wchar/wint limits. Verify direct provider edge cases and compiled
  `wstring_view`/character-traits operations in native/sanitized Bash WAST.
- [x] Implement the six wide integer converters using the same checked parser
  as narrow input; verify full wchar values, typed end pointers and real C++
  namespace calls in package/system WAST.
- [x] Replace the narrow floating parser and implement the matching wide
  providers with bounded integer ratios and direct nearest/ties-to-even
  binary32/64/128 rounding; verify grammar, range, end pointers and real C++
  returns in package/system WAST.
- [x] Implement bounded shared `swprintf`/`vswprintf`, including exact integer
  decimal/hex formatting of binary64/128, full wide elements, UTF-8 conversion,
  variadic alignment, bounds/errors and compiled C/C++ WAST observations.
- [x] Rebuild the complete wide-enabled libc++/libc++abi archives from the
  current profile, including the actual `string.cpp` formatting calls.
- [x] Establish the initial guest LLVM POSIX profile, preserving full-width
  stat metadata and nanoseconds; verify C++ file API linkage and ordinary/
  controlled-clock metadata observations in WAST.
- [x] Resolve the disabled-thread guest `SmartMutex` dependency without adding
  standard-library types; verify recursive/scoped operations and archive-backed
  `llvm::call_once` in native, sanitized and production-worker WAST.
- [x] Build the actual LLVM Support target before the complete Clang/LLD graph
  to report platform-port failures early.
- [x] Define the public integer exponent sentinels without adding an `ilogb`
  provider; verify C/C++ constant expressions and LLVM's actual APFloat-profile
  exponent helpers for binary32/64/128 in native, sanitized and worker WAST.
- [x] Adapt the six audited unguarded synchronization declarations under the
  explicit single-thread guest profile, preserving native/threaded backends;
  verify reader/writer and generic allocator ownership in a diagnostic fixture.
  The fixture now retains normal static registration; the separate shutdown
  fixture verifies cache backing-allocation release.
- [x] Resolve ThreadPoolInterface's disabled-thread futures with an LLVM-owned
  shared deferred result, preserving native/threaded standard futures; verify
  actual executor ordering, value/void/reference ownership, recursive submissions,
  waits, task groups and draining destruction with real C++ archives.
- [x] Expose shared C/C++ checkpoint and exit-code headers, preserve Bash's ABI
  through a forwarding header, and adapt CrashRecoveryContext's mutex for the
  disabled-thread guest profile. Verify compiled jumps, copied/live/stale tokens,
  stack restoration and ordinary C++ signal linkage in WAST.
- [x] Adapt the actual ExponentialBackoff implementation for the disabled-thread
  guest using compatibility jitter and yielding POSIX waits. Verify live sleeps,
  controlled monotonic deadlines, early wakeups and interrupted waits in WAST;
  retain standard random/thread backends for native/threaded profiles.
- [x] Resolve LockFileManager's guest process-liveness check using positive-PID
  null-signal queries. Verify live, blocked, zombie and reaped owners, conservative
  identity errors and public/libc++/kernel errno agreement; retain native getsid.
  Complete file-lock acquisition/cleanup remains part of platform acceptance.
- [x] Resolve ManagedStatic's disabled-thread mutex declaration while preserving
  native/threaded backends. Verify the actual lazy registry, nested construction,
  reverse destruction, repeated shutdown/reuse, custom/array deleters and claimed
  lifetime with normal static-registration flags and real C++ archives.
- [x] Resolve Mustache's numeric stream dependency for the disabled-localization
  guest using the real wide formatter; retain native streams and verify six-digit
  output against explicit WAST/native reference inputs. Check heap-backed string
  concatenation, independent copy/move and substring lifetime with current archives.
- [x] Resolve Parallel.h's private Latch dependency for the disabled-thread guest
  with checked count/completion semantics; retain native/threaded blocking locks.
  Verify actual TaskGroup, nested callbacks and scalar algorithms, plus release-build
  rejection of unfinished waits/destruction and count wrap in real guest children.
- [x] Resolve TimeProfiler's disabled-thread mutex and explicitly typed guards
  without changing registry/static ownership. Verify actual entry timing, owning
  metadata and instant storage with current archives; retain native/threaded locks.
- [x] Audit and exclude the unused std::ostream adapter only from guest Support,
  retaining LLVM raw streams, native builds and LLVM's source inventory check.
  Verify the retained memory-output core in source-backed WAST.
- [x] Audit the socket-stream callers and exclude that unused implementation
  only from guest Support, using the same optional-source mechanism. Keep the
  native implementation; this does not add guest socket or poll capabilities.
- [x] Verify TimeProfiler's global initialization, begin/end aggregation,
  finish/cleanup and exact JSON through actual Support/static-exit providers.
  Public-API WAST uses existing injected clocks/sleep continuations and checks
  file writers, metadata, granularity, reuse and static vector destruction.
  Shared modf resolves the real JSON writer import with package/SDK acceptance.
- [x] Resolve Clang module-cache, dependency-filesystem, preamble and driver
  lock declarations in the explicit disabled-thread guest profile; preserve
  native/threaded standard backends and single-mutex guard ownership. Verify
  actual cache sources/Support, buffer and invalidation ownership, advisory
  generations, module states, directory iteration and timed/rejected predicates
  in WAST with existing clock/signal/sleep controls. Keep full dependency scanning
  in the remaining work.
- [x] Adapt the actual module compile owner only for the explicit disabled-thread
  guest, reusing LLVM TaskGroup while preserving native/threaded mutex/thread
  behavior. Verify recursive submissions, move-only capture cleanup, service-owned
  advisory locks and interrupted shared sleep in WAST. Compile the complete worker
  in all three profiles and all seven guest dependency-scanning source rules;
  keep full prescan/module compilation acceptance separate.
- [x] Adapt the LLD diagnostic owner for the explicit disabled-thread guest
  using LLVM SmartMutex and its existing regex backend, preserving standard
  native/threaded behavior. Verify actual context/CodeGen/MC initialization,
  exact output, location matching, limits, script execution, fatal recovery,
  output discard and yielding cleanup in native/sanitized/worker WAST.
- [x] Narrow guest LLD selection explicitly to Common/Wasm libraries, the
  wasm-ld alias and an immutable Wasm-only driver table. Retain native default
  drivers/aliases; verify generated graphs, actual entrypoint references and
  complete guest/native/threaded source compiles. Final dispatch remains pending.
- [x] Resolve LLD asynchronous removal in the explicit disabled-thread guest
  by retaining its upstream one-thread fallback. Preserve native/threaded
  optimization; verify actual preflight, temporary-file/mapping lifetime,
  executable commit, retained descriptors, ordinary/LTO streams and failure
  diagnostics through existing shared filesystem/sleep providers in WAST.
- [x] Add pointer-format macros matching the compiler's public pointer aliases.
  Verify real C/C++ variadic calls and LLVM DataRef formatting; compile all
  13 actual LLD Common rules. Refresh generated dependencies before direct PCH/
  source checks, generate required version headers and bind staged version
  metadata to the read-only LLVM checkout.
- [x] Adapt the actual ThinLTO error owner and both production aggregation
  callers for the disabled-thread guest. Verify queued callbacks, joined errors,
  payload lifetime and reuse with native, sanitizer and packaged-worker WAST;
  retain standard mutexes in native and threaded builds.
- [x] Adapt the two sample-profile context formatters to LLVM raw streams in
  that guest profile, checking exact frame/context output and string ownership.
- [x] Compile all 14 Wasm and six LTO source rules. Provide a shared 512-block
  execution bound for generated LLVM code, cover it with portable WAST and
  remove the WAST handler's large temporary module copy.
- [x] Route LLVM TargetParser's guest host identity through the configured
  Wasm target triple without a Unix `uname` dependency; retain native Unix
  detection. Build its actual archive and verify guest default/process triple,
  CPU fallback and exit behavior with source-backed WAST.
- [x] Continue the real `lld` graph through LLVMCore and early reader/text
  archives. Add public `PRIx8/16` for promoted fixed-width arguments and verify
  compiled C/C++ formatting plus the former DWARF compile failure.
- [x] Build the low-level DWARF archive and adapt both DWARFVerifier mutex
  owners only in the disabled-thread guest profile. Verify actual diagnostic
  aggregation and yielding callbacks in native/sanitizer/worker WAST, plus
  complete guest/native/threaded verifier source checks.
- [x] Accept DWARFContext's adapted recursive owner with actual cache access,
  eager DWO parsing and native/threaded source compatibility. The complete
  DWARF archive and dependencies build; native/sanitizer WAST passes 51/51
  assertions and the packaged worker passes 52/52 including launch.
- [x] Resume the full `lld` graph after the accepted context and MC adaptations.
  Adapt the five GSYM creator/transformer files using the existing guest mutex
  wrapper, with complete native/threaded and actual guest source checks. Real
  conversion and ownership WAST passes 117/117 native/sanitizer assertions and
  118/118 worker results including launch. GSYM, MSF and CodeView archives build;
  the bounded full graph reaches 40% in PDB sources without a compile error.
- [x] Build PDB, BTF, Symbolize and ProfileData and resolve the actual CFG
  node-attribute stream blocker. Complete native/threaded source checks and
  real guest objects pass; WAST checks actual output and shared math providers.
- [x] Adapt the actual inlining-statistics stream owner using LLVM's existing
  raw string sink in the explicit guest profile. Accept shared narrow floating
  printf, compiled mixed variadic calls and real inlining graph output in
  native/sanitizer/worker WAST and manual offline-browser checks; preserve
  native/threaded source compatibility.
- [x] Resolve ConstantFolding's unavailable host math by declining optional
  computations while preserving exact APFloat cases, supported providers and
  original calls. Complete source compatibility checks and native/sanitizer/
  worker WAST pass; SDK availability remains truthful.
- [x] Build the complete LLVM Analysis archive and accept public optional
  folding through that archive in native/sanitizer/worker WAST. The resumed
  graph also builds IRPrinter, BitWriter, CGData and CodeGenTypes.
- [x] Complete the TransformUtils and AggressiveInstCombine archives after
  resolving the required CodeLayout source boundary.
- [x] Complete the InstCombine and ScalarOpts archives. Verify all 100 archived
  members against production objects and repeat their generated component rules
  without compilation, linking or artifact changes.
- [x] Complete ObjCARCOpts and Target and verify all 13 archived members against
  production objects.
- [x] Resolve the actual AssignmentTrackingAnalysis string-stream blocker using
  LLVM's raw sink only in the disabled-thread guest. Complete guest/native/
  threaded source checks and native/sanitizer/worker WAST verify interval text,
  binary names, unsigned bounds and returned-string ownership; accept the manual
  offline-browser results.
- [x] Complete the full CodeGen archive in the actual `lld` graph and verify
  all 238 archived members against their compiled production objects. Repeat
  the CodeGen target without C/C++ compilation or archive linking.
- [x] Complete SelectionDAG, AsmPrinter, GlobalISel, Extensions,
  FrontendAtomic and FrontendDirective in the actual `lld` graph; verify all
  85 new archive members against their compiled production objects.
- [x] Complete ObjectYAML, FrontendOffloading, FrontendOpenMP and
  Instrumentation. Verify all 68 new archive members against production
  objects and repeat the Instrumentation component rule without compilation
  or linking.
- [x] Resolve the actual stack shadow-callback name formatter using LLVM's
  raw-string/hex helpers in the disabled-thread guest. Complete source checks
  preserve native/threaded streams; native/sanitizer/worker WAST verify exact
  names, 32/64-bit signatures, declaration reuse and captured ownership.
- [x] Complete SandboxIR and Vectorize. Verify all 60 archived members against
  production objects and repeat both generated component rules without
  compilation, linking or artifact changes.
- [x] Resolve MemProfContextDisambiguation's actual graph identifier helper
  using LLVM's existing hex helper in the disabled-thread guest. Complete
  production/native/threaded source checks and cli-rt/sanitizer/worker WAST
  verify pointer boundaries, regular/ThinLTO graph types and retained real-node
  attributes.
- [x] Complete LLVMLinker and IPO; verify all 49 new archive members against
  production objects and repeat both component rules without compilation,
  linking or artifact changes.
- [x] Complete Coroutines, CFGuard, HipStdPar, Passes, Plugins, LTO,
  MCDisassembler and WebAssembly Info/Desc/Utils. Verify all 43 members of
  these ten archives against production objects and repeat their component
  rules without compilation, linking or artifact changes.
- [x] Complete WebAssembly CodeGen, AsmParser and Disassembler plus LLD
  Common/Wasm; verify all 83 archive members against production objects and
  repeat component rules without compilation, linking or artifact changes.
- [x] Give the generated guest LLVM driver C `main` linkage. Verify its
  production symbol and warnings-as-errors native/threaded source profiles;
  record that the previous small executable imported `main` instead of
  retaining the real linker driver.
- [x] Diagnose the apparent final-link wait as Clang's automatic `wasm-opt`
  child. Feature-detect its disable flag, finish the actual final link in a
  measured budget, and verify the generated component rule/cache.
- [x] Raise the shared executable-byte limit to 64 MiB; rebuild native and
  browser artifacts, refresh installed app configuration and verify native
  entrypoint boundaries plus the packaged worker regression.
- [x] Provide shared-libc `log2f` using the existing binary64 logarithm and
  verify binary32 results/errno in WAST, the public C++ float overload and
  actual linker import signatures across native/packaged-worker checks.
- [x] Coordinate a 256-type bound through shared configuration and the
  indexed-reference representation. Allocate actual binary type entries,
  grow safely for recursive groups and keep parser forward placeholders
  separate from real references and packed-field flags. Verify high indexes,
  growth/rejection, mutation and cross-module equivalence on both runtimes.
- [x] Allocate decoded local-type arrays for actual declarations, share their
  immutable ownership across clones and check combined parameter/local bounds
  before allocation and validator bitmap access. Measure actual LLD local and
  instruction-buffer requirements before changing the function cap.
- [x] Replace byte-sized instruction reservations with bounded geometric growth
  by decoded instruction count and best-effort trimming. Verify control indexes,
  branch/catch ownership, wide immediates, failed loads and actual retained
  allocation with native and browser regressions.
- [x] Compact decoded instruction records with opcode-selected immediate
  payloads, separate GC cast fields and opcode-gated catch ownership. Measure
  native/Wasm32 sizes and exercise memory, SIMD, GC and control semantics.
- [x] Allocate actual function-import bindings and combined declaration maps;
  share immutable metadata across clones, preserve import-only/mixed-kind
  modules and audit high export/global/element/ref.func indexes. Move binary
  definition/import bounds to shared configuration without raising them.
- [x] Stream-measure all 90,685 LLD function bodies without raising execution
  limits or retaining all code. Establish that 655.39 MiB of instruction arrays
  alone exceed the browser ceiling; distinguish decoding from startup acceptance.
- [x] Stream semantic validation through the ordinary validator, releasing each
  body after success. Validate all 90,685 actual LLD bodies with bounded native
  memory, private import placeholders and suppressed starts; retain ordinary
  unused-body rejection/start behavior and explicit diagnostic scope.
- [x] Raise the configured browser maximum to 2 GiB, keep the 64 MiB initial
  allocation, and raise defined-function capacity to 131,072. Measure real eager
  LLD startup before requiring a lazy loader or decoded-code eviction policy.
- [x] Run actual guest LLD on a host-compiled Wasm object and archive, execute
  the emitted module in installed Bash, and verify statuses/header through WAST.
  Keep file mappings separate from allocator heap growth and verify native,
  sanitizer and browser VM regressions.
- [x] Build a separate non-LTO linker, preserve the baseline, measure retained
  code and actual startup, and verify ordinary linking plus direct/archived
  bitcode rejection in native and sanitizer Bash.
- [x] Refresh the LLVM graph after resource configuration changes, then complete
  broader native SDK linking, browser link acceptance and compiler startup. Build-progress
  percentages do not measure overall compiler-port completion.
- [x] Link public-header calls and the shared guest CRT directly against the
  installed libc with actual guest LLD, preserve named-library/kernel imports,
  and reject an incompatible provider signature without an output file.
  This uses host-compiled inputs; actual guest Clang remains required below.
- [x] Retain validated encoded bodies and decode functions on first call, now
  required for the measured second-compiler-image memory path. Initially keep
  decoded functions for the shared code owner's lifetime. Add bounded eviction only if measured workloads require it, with
  active/yielded-frame pinning and clone/thread ownership preserved.
- [x] Resolve CodeLayout's unavailable `pow` by auditing CDSort callers and
  excluding only the unused ELF/BOLT algorithm from the Wasm-only guest.
  Preserve required ExtTSP and native CDSort; accept actual layout/scoring and
  real option parsing in native/sanitizer/worker WAST and the manual browser run.
- [x] Add public `PRIu8/16` for the actual promoted guest types after the real
  DWARF address-table source failure. Compiled C/C++ zero/max/mixed-width calls
  pass native, sanitizer and packaged-worker WAST.
- [x] Complete remaining owning-string/formatting operations and broader LLVM
  initialization using current archives. Narrow floating printf now shares the
  exact wide conversion and passes package/compiled statistics WAST.
  Production C++ startup and static exit have separate accepted fixtures.
- [x] Build/link Clang and LLD, resolve every imported symbol against real
  providers, and measure executable size, memory use and engine limits.
- [x] Complete the guest LLVM platform support profile beyond the initial
  POSIX/metadata selection. Resolve its synchronization requirements and route
  file, path, clock, signal and process operations through supported guest APIs;
  do not inherit native host capabilities or fabricate configure successes.
- [x] Resolve LLVM's optional capacity/locality queries with an explicit guest
  unsupported error; preserve native statvfs/statfs and required Path sources.
  Verify actual query definitions, conservative Boolean wrappers, errno and
  output preservation in a diagnostic system fixture. Full Path remains below.
- [x] Correct and verify public C++ linkage for dynamic-loading and memory-mapping
  declarations. Actual DynamicLibrary/Memory objects now import the C names;
  compiled C++ system WAST exercises the existing providers and i64 mmap offset.
- [x] Resolve guest executable discovery through the engine-owned loaded-image
  pathname, inherited by fork and replaced on exec; verify actual LLVM calls
  and bounded copies independently of argv/cwd and later rename/unlink. Derive
  private PATH_MAX and image-path storage from the existing VFS configuration.
- [x] Implement shared guest symlink/symlinkat providers and component
  resolution. Verify relative/intermediate/dangling links, dirfds, no-follow
  behavior, mutation and Bash executable loading in native/sanitized/worker WAST.
- [x] Implement shared guest link/linkat with retained inode bytes/metadata,
  namespace link counts, symlink flags and dirfds. Preserve the v1 metadata ABI
  and verify public C++ fork/wait and unlinked descriptors in both runtimes.
- [x] Provide reentrant account lookup using the existing shared guest account
  record, caller-owned buffers and direct POSIX errors. Expose the public C/C++
  declarations and sysconf query; verify package and compiled C++ WAST.
- [x] Provide POSIX mapping advice through shared libc and engine mapping
  validation, preserving bytes, permissions, backing identity and errno.
  Verify unchanged LLVM mapping/advice methods and public C++ calls in WAST
  across native, sanitized and packaged browser runtimes.
- [x] Provide shared realpath through the existing namespace/component resolver,
  with directory search checks, bounded atomic copies and libc-owned results.
  Verify public C++/WAST calls in native, sanitized and packaged browser runtimes.
- [x] Implement and verify Unix/Path process descriptor locks over retained
  inode identity, with public ABI, contention, fork/exec/close ownership,
  blocking readiness, interruption/restart and bounded deadlock detection.
  Unchanged LLVM blocking lock/unlock methods have guest WAST acceptance.
- [x] Provide yielding usleep through the existing sleep/time path and verify
  unchanged LLVM timed tryLockFile retries. Full Path link/runtime
  acceptance remains pending.
- [x] Correct the public rusage/timeval boundary and review its resource-usage
  provider; provide terminating public `_Exit` through the existing process
  exit path. Verify actual LLVM PID/time methods and retry full Support.
  CPU accounting returns an explicit unsupported error; LLVM selects its
  existing fallback. Full Process::Exit/crash-context handling remains below.
- [x] Provide public execv/environ and wait through existing execve/waitpid;
  validate/prepare wait status destinations before consuming a child. Verify
  public C++ calls, replacement and interrupted waits in native, sanitized
  and packaged browser runtimes. Actual LLVM Program methods remain below.
- [x] Select shared waitpid for actual guest LLVM Wait without fabricated child
  usage. Verify indefinite waits, polling, status/errors, interruption and
  rejected statistics in native, sanitized and packaged-worker
  WAST. ExecuteAndWait preflights unsupported requests before fork; execution
  methods and complete Program.cpp acceptance remain below.
- [x] Query and enforce shared argv/environment count and string-byte budgets;
  select both actual LLVM argument-fit overloads using the running kernel and
  current environment. Verify exact/excess boundaries, partial-copy errors,
  retained old-image/descriptors and interpreter expansion in native, sanitized
  and packaged-worker WAST. Complete Program.cpp remains below.
- [x] Resolve Program.cpp's session/detachment compile boundary through shared
  engine SID/group/terminal ownership and public libc calls. Verify retained
  aliases, nested fork/exec, terminal-control errors, parent preservation and
  private host/queued foreground routing in native/sanitized/worker checks.
  Fresh complete guest/native Program.cpp compilation passes.
- [x] Compile complete guest/native Signals.cpp using ordinary shared signal
  handlers without invented signal-info or alternate-stack capabilities.
  Verify actual info/interrupt/pipe callbacks, file cleanup, ignored utility
  dispositions, public libc registration/errors and forked-child termination
  in native, sanitizer and packaged-worker WAST. Full crash recovery and
  callback insertion/fatal-error dependencies remain below.
- [x] Build the complete guest LLVMSupport archive and compile fresh whole
  guest/native Watchdog.cpp sources. Replace fake alarm success with shared
  process-owned deadlines; verify unsigned remaining values, replacement,
  cancellation, masked pending SIGALRM, fork/exec ownership, READ/waitpid,
  CPU-bound and concurrent native/browser delivery. Pending application signals
  unblocked by sigprocmask now run before it returns.
- [x] Link actual ExecuteAndWait/ExecuteNoWait and Wait against the real Support
  archive with production CRT/C++/libc. Verify argument/environment ownership,
  regular redirects, failed setup/exec, polling, concurrent children and
  detachment in native, sanitizer and packaged-worker WAST. Failed guest child
  setup terminates that child; unsupported memory/affinity requests and malformed
  redirect vectors fail before fork. Executable image preparation now defers
  pump yields while keeping deadline/cancellation polling active.
- [x] Provide a shared /dev/null character device with EOF reads, discarded
  writes, retained inode metadata and immediate readiness. Verify descriptor/path
  semantics and actual LLVM empty redirects, including detached children, in
  native, sanitizer and packaged-worker WAST without substituting a regular file.
- [x] Provide shared absolute-deadline child waits through existing process,
  signal, clock and readiness state. Verify output validation, unchanged live
  children on expiry, concurrent nested parents, original-deadline retries,
  independent alarms and cleanup in native/sanitizer and packaged-worker WAST.
- [x] Integrate positive LLVM child timeouts with waste_waitpid_until. Verify
  polling expiry retains the child and ordinary expiry kills/reaps that exact
  child through actual archive-linked Wait/ExecuteAndWait. Requested
  child statistics remain explicitly unsupported; preserve native branches and
  reuse existing process, signal, clock and readiness state.
- [x] Reconcile the retained standalone raw blocking-pipe smoke probe with
  shared blocking-read readiness. Implement shared F_GETFL/F_SETFL status,
  explicitly request nonblocking EAGAIN in the standalone fixture, and verify
  blocking data/EOF wakeup, VMIN waits and fork/dup ownership. Restore the full
  i32-smoke and posix-kernel gates in native and sanitizer builds, with browser
  API and packaged-worker parity for the guest assertions.
- [x] Verify actual LLVM crash-context setup, cleanup, recovery and supported
  guest signal interactions against the complete Support archive. Preserve
  ignored utility actions on teardown; verify nested/explicit exits and actual
  signal callbacks/file cleanup. Save/restore nonzero sigsetjmp masks across
  copied tokens, yielded continuations, handlers and fork. This accepts guest
  signal recovery; arbitrary Wasm trap/fault recovery is not claimed.
- [x] Verify C++ static initialization and archive-backed allocation/alignment.
  Reproduce null constructor streams with a real startup observer, then bind
  CRT streams before constructors. Verify ordering, argv/environment, redirected
  input/output, local destruction and constructor sleep with both main signatures
  in native, sanitizer and packaged-worker checks. Static shutdown remains below.
- [x] Provide real C/C++ static exit registration/finalization in shared libc.
  Verify compiler-generated globals/local statics, selective/reentrant cleanup,
  new registrations during cleanup, yielding callbacks, fork isolation and
  successful/rejected exec. The actual PerThreadAllocator cache frees its backing
  allocation at normal exit. Legacy env imports reach the same implementation;
  diagnostic static-destructor suppression is removed. Automatic C++ dlclose
  cleanup and concurrent registration remain outside this compiler profile.
- [x] Install required Clang resource headers and guest driver defaults so
  compilation uses guest paths, the guest CRT and shared libc.
- [x] Stage/install the matching portable/Wasm resource files, production CRT,
  generated guest driver configuration and upstream license. Verify exact
  installed bytes and caching; actual compiler use remains the gate above.
- [x] Complete native and browser acceptance; verify unchanged builds cache.

## Earlier build and runtime boundary

The complete wide-enabled libc++ and libc++abi archives now rebuild under
`build/aux/clang/cxx/lib`, including `string.cpp`. Real shared integer/floating
conversion and bounded wide formatting providers resolve the previous source
blockers. The current archives are 905,086 and 423,762 bytes respectively;
retained wide-disabled archives are no longer being used as build evidence.
Broader owning-string/formatting and complete compiler/linker runtime acceptance
remain pending. The numeric fixture checks heap-backed string ownership;
startup verifies constructors and local C++ ownership/alignment with both CRT
main signatures. Shutdown and public TimeProfiler lifecycle now have accepted
archive-linked fixtures with real static registrations.

The incomplete Generic `file_status::getSize` interface is resolved by an
explicit guest POSIX profile. The AST precompiled header and initial source
objects compile. The recorded `llvm/Support/Mutex.h` failure is resolved: the
explicit disabled-thread guest profile reuses LLVM's recursive acquisition
counter, counts successful `try_lock`, and preserves noncopyable ownership.
Native and threaded profiles keep their original recursive mutex. A fresh
isolated compile of `clang/lib/AST/ASTContext.cpp` now produces a 1,199,845-byte
Wasm object using the generated guest target flags and actual SDK, without
reusing its PCH.

The resumed full graph compiled 11 of 116 AST source objects before a deliberate
stop to check required driver dependencies directly. Actual CMake/PCH compiles
now pass for InProcessModuleCache, DependencyScanningFilesystem,
PrecompiledPreamble and ModulesDriver. Their compiler-local lock aliases retain
native/threaded standard backends; timed predicates use existing LLVM backoff
and shared POSIX sleep. The actual cache fixture also verifies ownership, module
states and real directory iteration in both runtimes. DependencyScanningWorker's
six-error mutex/thread blocker is resolved by its production private module
task owner: the explicit disabled-thread guest reuses LLVM TaskGroup while
native/threaded branches retain their original operations. All seven guest
dependency-scanning source rules compile. Actual LLD diagnostic/context and
entrypoint source rules now compile with the guest-only SmartMutex/LLVM regex
adaptation. Native/threaded diagnostic and all-driver entrypoint source gates
also pass. The guest graph excludes COFF/ELF/MachO/MinGW libraries and aliases,
and its actual entrypoint references only the Wasm driver. Filesystem's
five-error synchronization blocker is resolved by the upstream one-thread
fallback, verified against real file/output owners in both runtimes. Pointer
format macros resolve the Common DWARF dependency; all 13 Common source rules
now compile. Direct source/PCH gates refresh actual CMake dependencies and
generate the real options/analysis/version prerequisites. The former Wasm
Driver blocker at `llvm/include/llvm/LTO/LTO.h:259` is resolved for the actual
`ThinBackendProc::ErrMu` owner. All 14 Wasm and six LLVM LTO production source
rules now compile. Full Clang/LLD graph linking and execution are pending.

The public `FP_ILOGB0`/`FP_ILOGBNAN` dependency is resolved. Guest `math.h`
defines the integer constant expressions as `INT_MIN` and `INT_MAX`, with no
new `ilogb` declaration/provider. The compiled C/C++ boundary verifies the
actual staged LLVM exponent helpers using APFloat's no-errno/no-exception
configuration, including exact subnormal exponents for binary32/64/128.
The resumed LLVM Support build compiles `APFloat.cpp` into a 152,977-byte Wasm
object and advances through `APInt.cpp`, `APSInt.cpp` and other Support sources.

The six audited unguarded synchronization declarations are adapted for
`LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`: PerThreadAllocator, FileCollector,
HashingOutputBackend, AtomicLineLogger, SmartRWMutex and the unused
BalancedPartitioning pool fields. The real Support build compiles
`Allocator.cpp` into a 2,562-byte Wasm object. Fresh isolated full object
compiles using generated target flags also pass for FileCollector.cpp (22,598
bytes) and AtomicLineLogger.cpp (8,633 bytes), without their PCHs. Native/threaded
declaration gates retain standard backends; full owner behavior remains pending.

ThreadPoolInterface's unguarded futures are resolved for the explicit guest
single-thread profile by `src/aux/clang/include/llvm/Support/ThreadPoolFuture.h`.
The private LLVM-owned result shares deferred value/void/reference state; native
and threaded profiles keep standard futures and deferred async. Real LLVM
SingleThreadExecutor code and current archives verify submission, sharing,
move-only and aligned results, recursive submission, waits and destruction.
ThreadPool.cpp includes its backoff helper only with the threaded implementation.
The full Support build now compiles BalancedPartitioning.cpp (52,095 bytes) and
advances through CommandLine.cpp. An isolated full compile of ThreadPool.cpp
also succeeds (10,803 bytes), using generated guest flags without its PCH.

The shared SDK now provides BSD-compatible `sysexits.h` and a C/C++ `setjmp.h`
using the existing engine checkpoint ABI. Bash's private header forwards to it;
the real `longjmp` import also supports libc++'s `std::longjmp`. Ordinary signal
declarations have C linkage in C++. CrashRecoveryContext's disabled-thread guest
mutex uses LLVM's existing recursive counter; native/threaded profiles retain
the standard mutex. The actual full Support build compiles
`CrashRecoveryContext.cpp` into a 5,069-byte Wasm object. Compiled checkpoint
and evaluator assertions pass natively, with sanitizers and through the
production browser worker. Archive-linked LLVM recovery and cleanup now have
separate acceptance, and nonzero `sigsetjmp` saves/restores the shared kernel
mask across copied tokens, yields, handlers and fork. Arbitrary Wasm trap/fault
recovery remains outside the supported signal boundary.

The actual ExponentialBackoff implementation now compiles into a 2,634-byte
Wasm object. Its guest-only LLVM helper uses the existing shared `getrandom`
compatibility PRNG for non-security jitter and `nanosleep` for yielding waits.
LLVM's uniform distribution, exponential multiplier, interval cap and timeout
calculation remain upstream code. Interrupted waits recompute the remaining
interval from their monotonic deadline. Native/threaded profiles retain
`std::random_device` and `std::this_thread::sleep_for`. Live and controlled-clock
WAST checks pass natively, with sanitizers and in the production browser worker.
The current random provider supplies process-local xorshift bytes, not OS entropy
or cryptographically secure randomness; libc++ random-device support stays off.

LockFileManager's actual source now compiles into a 14,649-byte Wasm object.
Its guest-only private helper queries positive PIDs with real `kill(pid, 0)`;
native LLVM retains getsid. Failed/different host identity and unrelated errors
conservatively retain the owner. The engine query sends no signal, preserves
blocked reads/pending signals and keeps zombies visible until reaping. The
guest ABI converts negative kernel errors to `-1/errno`. Public `ESRCH` and
libc++'s actual `errc` mapping now agree, and shared strerror supplies its message.
Compiled WAST verifies this boundary; complete LLVM file-lock acquisition,
contention, stale-file cleanup and signal cleanup remain pending.

ManagedStatic's disabled-thread mutex dependency is resolved using LLVM's own
`SmartMutex<true>` only for the explicit guest single-thread profile. Native and
threaded branches retain `std::recursive_mutex`; the upstream lazy registration
and reverse-destruction code remains unchanged. The actual full Support build
and an isolated compile without the PCH each produce a 1,048-byte Wasm object.
The source-backed observer links current C++ archives and ordinary static
registration flags; 91 WAST assertions pass in native/sanitized Bash and the
production browser worker adds its launch assertion (92/92). These verify the
managed registry. The separate shutdown fixture now accepts real C++ static-exit
registrations and destructors with ordinary archive flags.

Mustache's numeric stream dependency is resolved only for the explicit guest
profile. Its private helper uses the implemented exact `swprintf` with six-digit
general formatting, checks bounded ASCII output and returns an owning string.
Native LLVM keeps its original stream. The guest narrow `snprintf` currently
lacks floating conversions, and raw LLVM double streaming selects exponent
formatting; neither is used as an equivalent. All 37 exact binary64 inputs agree
byte-for-byte with native classic-locale streams and explicit WAST expectations.
Heap-backed accumulation, independent copy/move, mutation isolation and substring
lifetime pass using the current C++ archives. The fixture executes the actual
private helper; complete Mustache template rendering still requires Support's
remaining dependencies. Both the real build and an isolated full-source compile
without its PCH produce a 49,877-byte Mustache Wasm object.

Parallel.h's header-owned Latch dependency is resolved only for
`LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`. It retains atomic count ownership;
balanced counts drain normally, while pending sync/destruction and count wrap
terminate through real shared abort even with `NDEBUG`. TaskGroup already runs
callbacks synchronously in this profile. Native/threaded branches keep their
standard mutex and condition variable. The actual full build and an isolated
compile without its PCH each produce a 1,401-byte Parallel.cpp Wasm object.
Source-backed WAST verifies actual TaskGroup/scalar algorithms and release-build
child rejection paths with current C++ archives. The earlier source-only audit
missed this header; included synchronization owners require the same audit.

TimeProfiler's guest-only mutex selects LLVM's counted `SmartMutex<true>`;
write/cleanup/finish-thread guards use their actual field type. Native/threaded
branches retain `std::mutex`, and its function-local vector/static ownership
remains unchanged. The full build and an isolated compile without its PCH each
produce a 61,449-byte Wasm object. Its former missing `__cxa_atexit` now resolves
to the real shared-libc registration provider. Entry storage retains normal flags,
using actual source types for timing conversion, heap-backed metadata and copied/
moved instant storage. The separate public lifecycle fixture links the actual
Support archive and verifies initialization, aggregation, finish/cleanup,
exact JSON/file output and static registry allocation release. It uses existing
injected clocks and real sleep continuations in both runtimes.

The public `PRIX8/16/32/64` blocker is resolved using the actual guest widths:
small unsigned types promote to int, uint32_t uses unsigned int, and uint64_t
uses unsigned long long. The existing shared formatter already implements
uppercase hexadecimal output. Real C/C++ varargs pass 35 system WAST assertions,
including high bits, mixed-width alignment, padding, truncation and case selection.
Both the production YAMLTraits.cpp compile and an isolated full compile without
PCH produce 62,099-byte Wasm objects. YAML floating scalar output still uses
unsupported narrow `%g`; this does not establish complete YAML serialization.

The standard-stream adapter blocker is resolved by omitting raw_os_ostream.cpp
only from guest Support. Generated dependency closures contain 328 Clang-driver
and 99 LLD targets; neither selects clangFormat, its production caller. Native
builds retain the adapter and pass its full-source compile. LLVM's source
inventory check stays enabled with an explicit LLVM_OPTIONAL_SOURCES entry.
Localization remains disabled; no standard-stream type or provider was fabricated.
The actual build now compiles raw_ostream.cpp (37,506 bytes) and its proxy.
Source-backed WAST verifies the memory-output core with actual C++ archives and
shared libc. Its fixture isolates unchanged upstream ANSI helpers to resolve
vtable dependencies. That fixture covers memory output; actual descriptor file
writers, Process execution and C++ startup have separate accepted fixtures.

The socket-stream blocker is resolved by the same guest optional-source
mechanism. A whole-upstream symbol/include audit finds only the implementation,
public header, source list and LLVM Support unit-test caller. Tests are disabled;
the selected 328 Clang-driver and 99 LLD targets contain no socket-stream caller.
Both optional streams remain in native Support, which builds successfully;
a separate full native socket-source compile also passes.

The missing statvfs boundary is resolved without claiming filesystem statistics.
Under LLVM_WASTE_GUEST, actual disk_space and both is_local overloads return
function_not_supported (ENOSYS). Output values and errno are preserved; upstream
Boolean locality wrappers conservatively return false. The packaging budget is
not a filesystem quota. Native backends keep their original statvfs/statfs code.
CachePruning propagates this error for byte/percentage-based pruning policies;
these policies remain unavailable in the guest. RealFileSystem forwards locality
errors. Required Path sources remain selected.

Public dlfcn.h and sys/mman.h now provide C++ C-linkage guards. The production
DynamicLibrary/Memory objects and fresh complete compiles without PCH match
at 8,926/11,696 bytes and import unmangled C provider names. Compiled C++ system
WAST exercises real mapping/dynamic-loading providers. The query fixture's actual
libc++ system_error.cpp and ordinary archives now retain normal static-destructor
registration, backed by the shared provider and separate shutdown acceptance.
Process-wide dlopen(NULL) and complete LLVM Memory/DynamicLibrary execution remain
pending.

Guest getMainExecutable now uses shared-libc `waste_get_executable_path` and the
engine's process-owned loaded pathname. The resolved namespace path is captured
with the executable bytes, copied into the committed capsule, inherited on fork
and replaced on exec. It remains independent of argv, cwd and later rename/unlink.
Private PATH_MAX and image storage derive from VFS_PATH_MAX_BYTES in src/config.h;
the POSIX input-parser bound remains separate. Native LLVM branches are preserved.

The shared libc now provides symlink/symlinkat and link/linkat over the engine's
versioned creation boundaries. Common component resolution covers relative and
intermediate links, processes links before .. and preserves final no-follow
operations. Hard-link aliases share inode contents, attributes and lifetime;
namespace link counts exclude fd, mapping and fork references. A shared monotonic
inode allocator prevents reuse after deletion and concurrent child creation.
The original 48-byte metadata boundary remains intact; fstatat_v2 returns the
existing 128-byte guest stat layout. WAST verifies public C++ calls, mutation,
dirfds, ordinary fork/wait and Bash executable loading. Full LLVM Path execution
remains pending. Fork's cloned namespace still needs immediate cross-process
visibility and deletion reconciliation; shared open-description offsets and
removed-directory lifetime/subtree rename also remain follow-ups.

The actual one-job Support build now succeeds, including Path.cpp, Process.cpp,
Program.cpp, Signals.cpp and Watchdog.cpp. The current archive size and hashes
are recorded in the finite-wait slice below.
Fresh whole guest/native Watchdog compiles have zero diagnostics and produce
514-/1,544-byte objects. Public alarm uses the existing sleep/time, process
signal and minimum-deadline scheduler; it no longer reports fake success.
The guest signal profile uses ordinary one-argument handlers,
restores actual dispositions and explicitly re-raises guest signals. Unsupported
signal-info/alternate-stack capabilities remain unavailable; guest machine
backtraces are disabled without disabling real dlopen support. Fresh complete
guest/native Signals compilations have zero errors and produce 36,213-/83,072-byte
objects. Watchdog's replacement, cancellation, remaining time and process
ownership pass shared runtime checks. Actual LLVM execution methods pass the
archive-linked gates below, including /dev/null and positive child timeouts.
Fresh complete guest/native Program compilations have zero
diagnostics, producing 17,364-
and 32,240-byte objects. Shared setsid/getsid/getpgid/getpgrp and corrected
tcgetpgrp use existing process and terminal state. Child detachment retains
descriptors while preventing control of the parent's terminal/session;
the public system WAST passes 86 checks in native/sanitized Bash and 87 in
the packaged worker. The archive-linked fixture also checks actual LLVM
detachment rather than establishing it from compilation alone.
Argument/environment limits now come from src/config.h and are queried
through the shared kernel. Both actual LLVM argument-fit overloads account for
current environment count and combined string bytes; guest exec and WAT
interpreter expansion enforce the same limits before image commitment.
The 186-check WAST fixture verifies exact/excess counts and bytes, partial-copy
failures and old-image/descriptor preservation in native and sanitized Bash.
The packaged production worker passes 187 checks including Bash launch.
The guest LLVM Wait method selects
existing waitpid for indefinite waits and zero-time polling; requested
statistics are rejected without consuming a child. Positive waits compute one
checked absolute deadline and call waste_waitpid_until. Polling expiry retains
the child; ordinary expiry kills/reaps only that target. ExecuteAndWait checks
statistics/clock availability before fork. Actual staged Wait passes 254 WAST
checks in sanitized Bash and 255 packaged-worker checks; its separately
archive-linked finite-wait gate is recorded below. Public execv uses the current
environ without PATH search; public wait uses waitpid(-1, ..., 0). Invalid exec
vector pointers return EFAULT, and wait validates/prepares its destination
before reaping. The new public C++ fixture passes 234 WAST checks in native
and sanitized Bash, and 235 worker checks including launch. That public-API
fixture does not execute LLVM Program methods.
Public rusage timestamps are time64 timeval records. Both getrusage adapters
return explicit unsupported errors without touching output, and the guest
LLVM profile selects its existing wall-time/zero-CPU fallback. Real Process
PID/time calls and immediate child termination pass 55 WAST checks in native
and sanitized Bash; the production worker passes 98 including 42 package
resource-usage checks and launch. Full Process::Exit/crash-context handling
remains pending. Shared usleep delegates to the existing nanosleep/pselect deadline;
unchanged LLVM timed tryLockFile retries pass 53 WAST checks in native and
sanitized Bash, with 63 live worker checks including package usleep and launch.
Controlled deadlines and interruption pass 26 checks in native, browser and
packaged worker. Shared process locks use retained inode objects, independent
fork owners and the existing SELECT readiness/wait record. The 341-check
record-lock regression still passes natively and the packaged worker passes
342 including launch; its prior sanitizer acceptance remains recorded below.
Full Path execution remains pending. Shared realpath uses the existing namespace/component resolver, checks
directory search permissions and validates complete output before copying.
Its C++/WAST fixture has native/sanitized/packaged worker acceptance; kernel
credentials remain privately injected rather than synchronized with guest
identity setters. It does not execute the full LLVM real_path method.

Shared libc's posix_madvise uses the engine-owned mapping graph and
preserves data/permissions/errno; unchanged LLVM mapping methods and public
C++ calls pass 199 WAST checks in native/sanitized Bash and 200 checks including
Bash launch in the packaged worker. Reentrant account lookup uses the shared
account record, with caller-owned strings and an indeterminate sysconf size
query. Executable discovery, path limits, account lookup, mapping advice and
link/symlink/realpath providers no longer fail. Only implemented declarations lose
unavailable annotations.
Full guest Support and Clang/LLD remain unbuilt. The early Support gate continues
to report platform failures before the complete executable graph.

The compiled generic allocator observer independently exposes missing
`env.__cxa_atexit` for its static cache. Only the diagnostic ownership fixture
uses `-fno-c++-static-destructors`, to verify local ownership and destruction;
production compiler and archive flags retain static registration. This does
not establish C++ shutdown. Provide a real registry and finalization path,
including callback identity/order and process/library ownership. The existing
`env.atexit` no-op is not a replacement. No unavailable annotation was removed.
No compiler/linker executable has been built or installed.

Guest LLVM configuration and Makefile generation succeeded in the preceding
slice; the current full build has completed the wide-enabled runtime above.
The guest-only
`src/aux/clang/guest.cmake` profile excludes shared libLTO, libclang, Remarks
C API outputs and plugins through upstream options. PIC, static LLVM LTO and
Remarks, and LLD's Wasm target remain in the generated graph. The profile sets
LLVM_HOST_TRIPLE to wasm32-unknown-unknown instead of guessing the build
machine's x86-64 Linux triple. LLVM's bare-Wasm endian include now resolves
through `src/aux/clang/include/machine/endian.h`, a private view of the public
SDK's byte-order definitions. Only the guest compiler toolchain adds that path.

The previously missing `std::wstring` and `std::wstring_view` declarations
are available in the wide-enabled headers. The actual staged
`llvm/Support/ConvertUTF.h` passes an isolated syntax check; the compiled SDK
probe covers both types. The guest-only `LLVM_WASTE_GUEST` option now selects
LLVM's POSIX implementations while CMake remains Generic. Its private status
types derive from the actual 64-bit guest stat fields, preserving size/device/
inode data without changing the public 32-bit POSIX typedefs. Configure probes
the separate nanosecond member; the Unix conversion preserves those fields.
The compiled metadata boundary passes, while full out-of-line LLVM filesystem
support and other platform operations remain pending.

The same guest profile explicitly excludes separate pthread/dl/rt/pfm libraries,
which are absent from the mounted SDK. Archive-only `check_library_exists`
probes had falsely reported them as present and added native-style link flags.
This does not disable individual functions in shared libc or establish their
availability. Frame-registration/unwinder helpers and pthread rwlock
initialization are now explicitly excluded after checking their absent or
unavailable providers. Other function-level configure results, C++ startup and
the full LLVM POSIX support profile still require audit and acceptance.

The abort blocker is resolved by `src/aux/libc/abort.c` calling the shared
`waste_kernel.abort_v1` operation. A caught handler runs synchronously; if it
returns, ignores or reblocks SIGABRT, the engine still terminates the guest.
Recursive abort does not invoke the handler again. Both this versioned import
and legacy `env.abort` preserve signal 6 in waitpid without terminating the
parent, native runtime or browser. The unavailable annotation/policy and
Rogue-specific declaration exception have been removed after implementing
and testing the capability. Blocking/yielding signal handlers remain unsupported.

Aligned allocation is resolved in the shared boundary-tag allocator. Padding
becomes reusable free blocks; the aligned pointer retains the normal preceding
header, so free, realloc and coalescing work without hidden original pointers.
`posix_memalign` supports alignments above 16 bytes and preserves errno and its
output on failure. `aligned_alloc` checks power-of-two alignment and size
multiples. Checked 64-bit rounding rejects oversized requests before growth
or narrowing to signed block sizes, fixing an existing malloc overflow too.
The allocator's signed i32 bookkeeping bounds the contiguous heap below 2 GiB;
engine memory limits can be smaller. No shared configuration limit was raised.
libc++'s aligned-new helper rounds its requested size and releases through
ordinary free, matching this provider; executing C++ aligned new remains part
of the pending startup/runtime acceptance.

The RTTI failure in libc++abi's `private_typeinfo.cpp` was a profile conflict:
the toolchain globally supplied `-fno-rtti` although upstream's ABI library
requires dynamic_cast/typeid internally. Target profiles now decide RTTI;
libc++ and LLVM still disable it, while libc++abi compiles its real implementation.

The floating-point environment blocker is resolved by `fenv.h` and the shared
`src/aux/libc/fenv.c` provider. Only FE_TONEAREST and FE_ALL_EXCEPT=0 are
advertised. Default save/restore and empty exception operations reflect an
immutable Wasm environment; alternate modes, nonempty masks, null pointers and
malformed snapshots fail without touching host state. LLVM's helper supplies
its internal comparison tags for other modes; those do not advertise additional
guest capabilities. Native/browser WAST checks verify real ties-to-even,
NaN/infinity/underflow behavior, unchanged errno and output canaries.

The clock blocker is resolved by `src/aux/libc/clock.c` calling
`waste_kernel.clock_gettime_v1`. The kernel selects its injected realtime or
monotonic callback; no host clock calls enter shared engine code. It encodes the
16-byte guest timespec in one bounded write, including zero padding. Invalid
IDs return EINVAL, bad destinations EFAULT, missing callbacks ENOSYS; successful
reads preserve errno. Public `time.h` and `sys/time.h` retain C linkage in C++.
libc++ selects its private `_LIBCPP_HAS_CLOCK_GETTIME` backend through its
additional compile flags. `_POSIX_TIMERS` remains unadvertised.

The stdio blocker is resolved by public C-linkage `vsnprintf`/`vsprintf`
declarations matching the existing shared providers. Ten package WAST checks
cover the implemented integer/string subset. Compiled C and `<cstdio>` C++
clients capture two calls with the same `va_list`, original argument cursor,
return counts, errno and complete buffers; 35 WAST assertions check the
observations, including public uppercase integer-format macros and mixed-width
alignment. The C++ header wrappers precede guest C headers, and the normal
shared-import tool assigns its namespace aliases to real providers. This does
not claim every printf conversion or establish libc++ runtime startup.

The narrow and wide integer conversion prerequisites are resolved by
`src/aux/libc/integer.c`. All twelve converters share checked magnitude parsing;
32-bit long and 64-bit long long/intmax retain their own limits. The reader
keeps complete wchar values, and typed wrappers form byte/wide end pointers
without transcoding or pointer-to-pointer aliasing. Seventy-seven package WAST
assertions reuse every ASCII expectation across narrow and wide input, checking
C whitespace, bases, no conversion, unsigned negation and overflow consumption.
Fifteen wide-only vectors exercise all six APIs against non-ASCII low bytes and
signed wchar extremes. The unavailable `strtoul` annotation/policy is removed
only after implementing its provider. Public `<stdlib.h>`, `<wchar.h>` and
`<inttypes.h>` declarations have C linkage; the C++ SDK probe checks aliases
and return types, while the compiled wide observer checks real calls, values,
end offsets and errno in WAST.

The classification blocker is resolved by public `math.h` constants and
type-preserving, single-evaluation compiler builtin macros. Compiled C and
`<cmath>` C++ probes verify all three guest widths in 83 WAST assertions.
They initially exposed eight failures: binary128 comparisons narrowed through
double, collapsing tiny values to zero. The seven shared comparison helpers
now inspect both words exactly and use compiler-rt's ordered/unordered return
conventions. Twenty-four package WAST assertions also check adjacent values,
signed zeros, infinities and NaN payloads. Binary128 arithmetic/conversion
helpers still narrow through double; this slice does not claim full quad math.

Guest LLVM's first version-probe failure misleadingly reported an old
libstdc++. The configure log actually showed libc++ failing to obtain
`mbstate_t`, because its headers followed the public C headers. An independent
`<iosfwd>` compile passed with the wrapper-first order. The toolchain now uses
that order and the guest recipe explicitly selects LLVM_ENABLE_LIBCXX=ON;
host generators retain their own library choice. No probe was forced to pass.

The next slice should link and exercise actual LLVM child execution using the
built Support archive and existing process/session/terminal state.
Argument-fit queries and exec enforcement now share src/config.h quantities;
there is no host ARG_MAX or separate compiler budget.
Requested statistics remain explicitly unsupported. Positive timeout/alarm
behavior must reuse engine-owned signals, monotonic deadlines and the existing
scheduler. Support archive compilation now passes; linking and runtime acceptance
must establish the remaining dependencies.
Account lookup, mapping advice, realpath and descriptor locks use shared guest
providers; full LLVM Path execution is still pending. Reuse the installed guest
namespace and existing startup/provider APIs; add shared public declarations only
for implemented capabilities. Keep required Path sources, preserve native behavior
and report unsupported operations explicitly. No native OS macros or fabricated
configure successes should substitute for actual guest behavior.

The executable-path fixture also exposed independently cloned regular-file
offsets across fork. Its fixed record positions avoid depending on that pending
capability; a shared open-description offset and cross-process ordering remain
kernel follow-ups. Symbolic-link component resolution now has its own shared
provider/loader acceptance below. Regular unlinked-file metadata and hard-link
shared metadata now have provider acceptance. Immediate namespace visibility,
deletion reconciliation and directory lifetime/subtree rename remain broader work.

Any needed blocking file-lock or retry operation must reuse engine-owned readiness
and yield/resume, including the waits/clocks already used by sleep. In particular,
usleep now uses that existing time path. Real shared C/C++ static exit now
supports ordinary error-category shutdown and actual profiler/cache destruction.
Automatic C++ DynamicLibrary cleanup on dlclose remains outside this profile;
each remaining registry still needs its own lifecycle acceptance. Use the built
Support archive and record actual link/runtime dependencies; re-audit optional stream exclusions
if the selected tool graph expands. Guest socket/poll APIs remain unimplemented.
The public integer macros and actual YAMLTraits source now compile; complete
YAML output still needs its own acceptance; narrow floating formatting now
has shared providers and compiled inlining coverage. TimeProfiler's mutex
now compiles without changing registry ownership. Its actual entry timing and
owning storage pass WAST. Global initialization, aggregation, finish/cleanup,
trace output and static destruction also pass against actual archive providers.
Parallel.h's latch verifies completion and rejects invalid counts even in release
builds; actual synchronous TaskGroup and scalar algorithms pass source-backed
WAST. The earlier source-only audit missed the header-owned latch and profiler
registry, so continue checking included headers and later synchronization owners.
Mustache's numeric helper now preserves six-digit stream formatting with exact
shared wide output and verified heap-backed strings; complete template rendering,
broader template rendering remains pending. Narrow floating printf and C++
startup/static exit now have separate accepted fixtures. ManagedStatic's
actual registry lifecycle is verified separately from C++ static-exit callbacks.
LockFileManager's process-liveness boundary now uses actual guest null-signal
queries; complete file-lock ownership, contention and stale-file
cleanup still require its remaining Support dependencies. ExponentialBackoff
uses real guest jitter and deadline-preserving yielding waits. CrashRecoveryContext
compiles with the shared checkpoint headers, and its compiled C/C++ jump
boundary is verified; actual LLVM signal-handler recovery/cleanup still requires its real out-of-line
dependencies. The `sigsetjmp` save flag remains ignored and needs explicit
engine/kernel semantics and tests before claiming POSIX mask preservation.
The deferred task boundary is now verified with actual LLVM executor code.
Its audited Support/LTO/Clang/Wasm consumers do not explicitly name a standard
future result type or require timed waits. LLD's COFF driver separately uses
`std::future`/`std::async`; the explicit guest Wasm-only selection now excludes
that driver and its dependency. Native builds retain all drivers. Exponential
backoff's real out-of-line implementation is verified through current C++
archives and shared POSIX providers; complete file-lock behavior remains pending.
The six audited lock owners now compile at their checked boundaries; FileCollector,
AtomicLineLogger, HashingOutputBackend and concrete BumpPtrAllocator runtime
behavior remain separate acceptance work. Parallel.cpp's executor fields,
ThreadPool's mutex fields and ConcurrentHashtable guard their standard mutex
state with `LLVM_ENABLE_THREADS`; Parallel.h's Latch was missed by that earlier
audit and is now adapted and verified. TimeProfiler's mutex declaration is
resolved and its public global lifecycle passes. ThreadPool futures use
the verified private deferred result;
LLD's separate COFF futures are outside the selected guest graph. Real static exit
registration/finalization now passes, including actual cache destruction;
diagnostic static-destructor suppression has been removed.
`SmartMutex` and real archive-backed `llvm::call_once` now pass the compiled
boundary; do not treat these compiler-local counters as inter-thread locks for
WASTE's scheduler. Preserve native behavior and continue the Unix backend's header,
layout and provider audit, preserving kernel ownership and unsupported errors.
The initial metadata interface and wide formatting dependency are resolved:
`swprintf`/`vswprintf` count wchar elements, preserve full wide data and format
binary64/128 directly with nearest/ties-to-even integer arithmetic. Package and
compiled C/C++ WAST cover counts, termination, errors, bounds and variadic
alignment. Narrow `snprintf`/`printf` now shares that exact floating conversion,
with full-result byte counts, bounded retained output and error propagation
through allocating/file wrappers. Actual LLVM inlining statistics and compiled
mixed arguments pass their acceptance fixtures. This does not establish every
printf extension or all libc++ `to_string` callers. Verify each remaining owning
string and initialization consumer against the current archives.

Keep wide characters enabled and preserve LLVM's UTF conversions. The initial
file-status representation is verified; out-of-line path/status operations
still need source/link/runtime acceptance. The dependency audit already finds
explicitly unsupported capacity/locality queries and missing `dladdr`
executable-path handling behind the real `dlopen` capability. Timestamp mutation through
`utimensat`/`futimens` still returns ENOSYS; the current positive
`HAVE_FUTIMENS` probe describes function presence, not working mutation; LLVM
propagates the unsupported errno. The emulated-TLS helper has no linked
provider, so the authored guest profile now disables its archive-only probe.
The production cache has now been refreshed to OFF, with the generated
`CLANG_HAVE_EMUTLS_GET_ADDRESS` macro set to 0.
Frame-registration and rwlock false positives are already excluded.
Keep PIC and LLD's static dependencies and
avoid fabricated native platform/dynamic-link capabilities or host-header
fallback. The new floating providers use integer bit construction; existing
quad arithmetic/conversion helpers outside those providers still need their
separate precision audit. Full libc++ startup and compiler acceptance remain
pending after the formatting/runtime build.

The compiled C++ probe also exposed an entry constraint: under the current
`-ffreestanding` profile, an ordinary C++ main has a mangled symbol. Its owned
fixture uses explicit C linkage to match the C CRT. The repository patch now
applies C linkage to the generated LLVM tool driver only for the guest build;
the production LLD driver object defines unmangled `main`. A small successful
link with no C++ imports was misleading because its CRT imported `env:main`
and the actual linker code was discarded. Check both the defined entry symbol
and absence of an imported `main`, alongside constructors/streams in startup
acceptance, before installing either compiler command. Keep every generated
output under `build/`.

The current libc++ profile enables wide characters and disables localization,
filesystem and several other facilities. LLVM uses more C++ facilities than the initial
probe. Audit and enable the necessary subset rather than treating configure
success as proof of a usable compiler. Keep exceptions/RTTI/threads disabled
for the first executable; no Asyncify or alternate C libc is planned.

## Acceptance and evidence

Package version/diagnostic assertions belong under
`src/vfs/root/test/aux/clang/`.
C++ SDK and compiler/linker/executor boundary probes belong under
`src/aux/clang/test/<topic>/` with WAST assertions in
`src/vfs/root/test/system/clang/<topic>/`. Functional assertions remain in WAST and execute
inside Bash, with setup scripts supplying inputs and capturing outputs only.

Run supported slice regressions through cli-rt automatically and save their
logs under `build/engine/logs`. The user should not need to run a CLI test that
the agent can execute locally. Keep native and browser evidence identified by
the runtime actually exercised.

Acceptance must include an actual Clang invocation producing a Wasm binary,
LLD linking it, `/bin/wasm` returning the expected exit status, invalid C
returning nonzero with a diagnostic, guest-header/shared-libc compilation,
and the same checks from offline `bash.html`. Test subprocess scheduling and
verify the parent Bash survives compiler errors. Measure artifact sizes before
changing shared configuration limits. Preserve LLVM's license notice.

Diagnostic logs for this initial port are under `build/aux/clang`:
`sdk-check.log`, `cxx-build.log`, `native-build.log`, `libc-test.log`, and
`system-libc-test.log`. No GUI browser automation is required to diagnose the
current compile-time failure.

Completed verification for the SDK/libc prerequisites:

- `clang-sdk-check` and both host generators pass; repeating it,
  `clang-native-tools` and `clang-source` does no work.
- Guest SDK audit passes: 56 headers, six include orders and ABI/provider/
  availability checks.
- Native Bash package/system libc tests pass: 31 + 46 assertions.
- The same 77 assertions pass with ASan/UBSan.
- Offline-page worker session passes eight new division/removal assertions
  and the bootstrap invocation (9/9), with Bash returning normally.
- The page rebuild, WAST parsing, shell/Python syntax and diff checks pass.
- Installed snapshots are refreshed and `vfs-tests-check` passes.
- The LLVM submodule remains unchanged.

Retained verification evidence is under `build/engine/logs/clang-*`, including
`clang-browser-libc-results.json`. These results verify the prerequisites;
Clang's own package/system tests have not run because the compiler has not built.

### Abort prerequisite slice

- `make -C src/aux test-abort`: eight WAST assertions pass from Bash.
  The C input fixture only configures scenarios and captures observations;
  WAST checks default, ignored, blocked, caught/returning, recursive and legacy
  abort, including raw wait status 6 and continued parent execution.
- The same eight assertions pass with `wasm-sanitize` and ASan/UBSan.
- The rebuilt offline page's production worker passes all eight assertions
  plus the bootstrap invocation (9/9), returning to Bash and exiting normally.
- The existing native libc package/system tests pass (31 + 46 assertions).
- Those existing 77 assertions also pass with ASan/UBSan after the shared
  signal-delivery refactor.
- `clang-sdk-check`, the SDK provider/header audit and unchanged
  staging/native-generator/SDK-probe cache checks pass. The abort fixture also
  does no work when unchanged; its seven shared-libc imports are checked.
- Installed snapshots are refreshed (319 tests, 281 supported), and
  `vfs-tests-check` passes. Shell/Python syntax and diff checks pass.

Current build diagnostics are in `build/aux/clang/cxx-build.log`; abort/native/
sanitizer/SDK/corpus/cache logs are `build/aux/clang/abort-*.log`. Browser
evidence is `build/engine/logs/clang-browser-abort-results.json`. See
[abort test instructions](../src/engine/test/abort/README.md) for reproduction
and an optional manual Firefox check; no manual browser evidence is needed
to diagnose the then-current clock-backend compile failure.

### Aligned-allocation prerequisite slice

- `test-libc` passes 42 assertions, including eleven new aligned-allocation
  assertions. These cover powers of two through 64 KiB, padding/coalescing,
  adjacent live bytes, realloc, POSIX errno/output semantics, overflow before
  growth, zero-size ownership and 256 mixed allocations with live-byte checks.
- The same 42 assertions pass through `wasm-sanitize` with ASan/UBSan, as do
  the 46 existing system libc assertions (88 total). Native system libc also
  passes all 46. Leak detection is disabled for the documented ptrace
  environment; address and undefined-behavior checks remain enabled.
- The rebuilt offline page's production worker passes the eleven new WAST
  assertions and bootstrap invocation (12/12), with normal Bash exit.
- The SDK declaration/provider audit and `clang-sdk-check` pass, including
  `std::aligned_alloc` in the actual generated libc++ headers.
- `libc++abi.a` builds after fixing the global RTTI flag. libc++ then exposed
  the `fenv.h` blocker in `charconv.cpp`, resolved in the following slice;
  neither guest compiler is installed.
- Repeating `libc`, source staging, native generators and SDK probe targets
  does no work. Installed snapshots are refreshed (320 tests, 281 supported),
  and `vfs-tests-check`, shell/Python syntax and diff checks pass. The LLVM
  submodule remains unchanged.

Logs are `build/aux/clang/alignment-*.log`, with retained copies under
`build/engine/logs/clang-*`. The current C++ compile diagnostic remains in
`build/aux/clang/cxx-build.log`; browser evidence is
`build/engine/logs/clang-browser-alignment-results.json`. See
[alignment/browser instructions](../src/aux/clang/test/README.md) for
reproduction and optional manual download steps. No manual browser evidence
is required for this slice's remaining compile-time blocker.

### Floating-point environment prerequisite slice

- Shared libc supplies all eleven `<fenv.h>` entry points for the immutable
  guest environment; `<cfenv>` compiles with C linkage and four-byte snapshots.
  Unsupported rounding/exception macros are not advertised by the public SDK.
- Nine new WAST assertions pass in native Bash and with ASan/UBSan. These
  check real f32/f64 ties-to-even, NaN/infinity/underflow without sticky flags,
  default save/hold/restore, flag snapshots, invalid requests, canaries and errno.
- All package/system libc assertions pass natively and with ASan/UBSan:
  51 package + 46 system = 97. Leak detection remains disabled for ptrace.
  Final runs used stable VFS inputs after completing installation; concurrent
  installation and mounted-tree reads caused a setup race during verification.
- The rebuilt offline page's production worker passes the nine new assertions
  plus bootstrap (10/10), with normal Bash exit. GUI automation was not needed.
- SDK audit passes with 57 headers and six include orders; `clang-sdk-check`
  covers all eleven `std::fe*` APIs and supported macro/type boundaries.
- libc++ now compiles `charconv.cpp`. Its next failure is `chrono.cpp`'s missing
  monotonic clock backend; the complete C++ runtime and guest commands are pending.
- Unchanged libc/source/native-generator/SDK targets do no work. Installed
  snapshots are refreshed (321 tests, 281 supported); corpus, shell/Python
  syntax and diff checks pass. The LLVM submodule remains unchanged.

Logs are `build/aux/clang/fenv-*.log`, with retained copies under
`build/engine/logs/clang-*`. Current compile diagnostics remain in
`build/aux/clang/cxx-build.log`; browser evidence is
`build/engine/logs/clang-browser-fenv-results.json`. See
[fenv/browser instructions](../src/aux/clang/test/README.md) for reproduction
and optional download steps. No manual browser test is required for the then-current
clock-backend compile failure.

### Clock prerequisite slice

- Shared libc now exports `clock_gettime` for CLOCK_REALTIME/CLOCK_MONOTONIC.
  The versioned engine adapter selects injected clocks and encodes the complete
  guest timespec, returning errors instead of trapping on invalid destinations.
- Eight new package WAST assertions pass in native Bash, ASan/UBSan and the
  packaged offline worker. They cover normalized values, padding/canaries,
  monotonic progression across a yielding sleep, preserved errno, invalid IDs,
  null/overflow pointers and rejection without a partial range write.
- Five controlled timestamp assertions pass through native, sanitized native,
  browser API and packaged worker. Realtime seconds beyond 2038 retain exact
  nanoseconds and agree with time/gettimeofday; monotonic advances independently.
  The production worker reports 9/9 live-clock and 6/6 controlled-clock results,
  including their respective Bash bootstrap invocations.
- All libc package/system assertions pass natively and with ASan/UBSan:
  59 package + 46 system = 105. The private kernel sanitizer check passes
  265 checks, including absent callback errors. Leak detection remains disabled
  for ptrace; address/undefined-behavior checks remain enabled.
- Verification corrected two terminal fixture setup errors that also reproduced
  against HEAD: clear observed SIGINT before the VMIN read, and select a caught
  SIGWINCH disposition before checking its pending queue. The native session
  driver's premature-output guard now runs while waiting for a record, so an
  unexpected READ/SELECT change is rejected instead of timing out. The terminal
  readiness gate passes its 90 assertions and both negative controls.
  Terminal timing, process-group and terminal-descriptor dependencies also
  pass. The broader `posix-kernel` Make recipe then stops when it runs
  `pipe-descriptors.wast` standalone: "external input waits require a runtime
  terminal context". That recipe is not claimed as passing; its pipe-probe
  transport needs a separate follow-up. The kernel unit and clock/libc gates
  above pass independently.
- Guest SDK audit and `clang-sdk-check` pass, including C++ clock linkage,
  clockid/timespec ABI and `<chrono>` declarations. libc++ now compiles
  `chrono.cpp`; `filesystem_error.cpp` exposes the next missing `vsnprintf`
  declaration. The complete C++ runtime and Clang/LLD commands remain pending.
- Installed snapshots are refreshed: 324 tests, 281 supported, 527 VFS nodes.
  The installed shared libraries and clock WAST snapshot match authored/build
  bytes. No shared capacity limit was raised and the LLVM submodule is unchanged.
  Corpus audit, shell/Python syntax and diff checks pass. Repeating unchanged
  libc, source staging, native generators and SDK probe targets does no work.

Logs are `build/aux/clang/clock-*.log`, with retained copies under
`build/engine/logs/clang-*`. Current compile diagnostics remain in
`build/aux/clang/cxx-build.log`; browser evidence is
`build/engine/logs/clang-browser-clock-results.json` and
`build/engine/logs/clang-browser-clock-controlled-results.json`. See
[clock reproduction/manual download instructions](../src/aux/clang/test/README.md).
No manual browser test is required for this slice or its then-current stdio
compile blocker.

### Variadic stdio declaration prerequisite slice

- Public `stdio.h` now declares the existing shared `vsnprintf` and `vsprintf`
  with C linkage. The C++ SDK probe resolves both `std::` aliases. No second
  formatter or libc was introduced, and existing formatting limitations remain.
- Ten package WAST assertions pass for full-length reporting, truncation, exact
  fit, one-byte/zero-size output, null sizing, empty formats, mixed promoted/wide
  arguments, repeated argument areas and the public `vsprintf` provider.
- `make -C src/aux test-stdio` builds real C and `<cstdio>` C++ clients
  against guest headers and checks their six shared-libc imports. Both call
  `vsnprintf` twice with the same `va_list`, then capture its original first
  argument, return counts, errno and complete sentinel-filled buffers. Nineteen
  WAST assertions check those observations, including UTF-8 byte counts and
  normal child/Bash exit. Compiled sources carry no formatting assertions.
- Native and ASan/UBSan checks pass: 69 libc package + 46 existing system libc
  + 19 compiled stdio assertions = 134. Leak detection remains disabled for
  ptrace; address and undefined-behavior checks remain enabled.
- The rebuilt offline page's production worker passes all nineteen compiled
  stdio and ten package formatter assertions, plus Bash bootstrap (30/30).
  Both compiled programs return normally and the parent Bash remains usable.
- SDK audit passes with 57 headers/six include orders, and the provider gate
  verifies 778 available functions, 148 unavailable functions and 31 globals.
  `clang-sdk-check` passes with actual generated libc++ headers and public SDK.
- libc++ now compiles the filesystem format/path sources. It stops in
  `string.cpp` at unavailable `strtoul` and missing `strtoll`/`strtoull`, which
  require the next integer-conversion slice. The full C++ runtime and guest
  Clang/LLD remain unbuilt/uninstalled.
- The C++ input requires C linkage for main under the current freestanding
  profile. Namespace aliases also require ordinary provider/import assignment;
  a late attribute redeclaration alone left an unresolved raw `env.vsnprintf`.
  The real driver entry/CRT signature and C++ startup remain acceptance work.
- Installed snapshots are refreshed: 326 tests, 281 supported, 530 VFS nodes.
  Corpus audit, shell/Python syntax and diff checks pass. Unchanged libc, source
  staging, native generators, C++ SDK and stdio fixture targets do no work.
  LLVM remains read-only; no shared capacity limit was changed.

Logs are `build/aux/clang/stdio-*.log`, with retained copies under
`build/engine/logs/clang-*`; the latest compile diagnostic remains in
`build/aux/clang/cxx-build.log`. Browser evidence is
`build/engine/logs/clang-browser-stdio-results.json`. See
[compiled formatter/browser instructions](../src/aux/clang/test/stdio/README.md)
for reproduction and optional upload/download evidence. These compiler-boundary
probes do not link libc++ runtime archives or establish its constructor behavior.
No manual browser check was required for this slice or its then-current integer
conversion compile failure.

### Integer conversion prerequisite slice

- `src/aux/libc/integer.c` supplies real `strtoul`, `strtoll` and `strtoull`
  providers and replaces the old parser for `strtol`, `strtoimax` and
  `strtoumax`. Limits come from guest C types, not native host widths. Checked
  quotient/remainder arithmetic detects overflow before multiplying, retains
  saturation through later digits, and handles signed minima without overflow.
- Sixty-two package WAST assertions pass for 32/64-bit signed/unsigned limits,
  overflow on both signs, negative unsigned wrapping, all six whitespace bytes,
  octal/hex auto-detection, partial/empty prefixes and no conversion. Each API
  exercises every base from 2 through 36 and a null end pointer. Checks also
  cover errno, complete overflow consumption, input bytes and end-pointer guards.
- Public `stdlib.h` declares the three new providers; `inttypes.h` now retains
  C linkage in C++. The unavailable `strtoul` policy is removed. The actual
  libc++ `<cstdlib>`/`<cinttypes>` SDK probe verifies all six namespace aliases,
  return types and linkage. The provider audit passes: 781 available functions,
  147 unavailable functions and 31 globals, with exact Wasm signatures.
- All 131 package libc + 46 system libc + 19 compiled stdio assertions pass
  natively and with ASan/UBSan: 196 in each transport. Leak detection remains
  disabled for ptrace; address and undefined-behavior checks remain enabled.
- The rebuilt offline page's production worker passes the 62 integer assertions
  plus Bash bootstrap (63/63). The compiled stdio/formatter scenario also passes
  again (30/30), including normal child and parent exits. No GUI was launched.
- libc++ now compiles `string.cpp` and stops in `valarray.cpp` because the guest
  math header lacks the five floating-point classification constants. That is
  the next recorded blocker; no upstream source is removed to suppress it.
  The full libc++ archive and guest Clang/LLD remain unbuilt/uninstalled.
- Installed snapshots are refreshed: 327 tests, 281 supported, 531 VFS nodes.
  Both installed shared libraries match the 68,152-byte build and the integer
  snapshot matches its authored WAST. Corpus audit and shell/Python/diff checks
  pass. Unchanged libc, LLVM source staging, native generators, C++ SDK and
  stdio fixtures do no work. The LLVM submodule is unchanged and no capacity
  limit was raised.

Logs are `build/aux/clang/integer-*.log`, with retained copies under
`build/engine/logs/clang-integer-*`; the latest compile diagnostic remains in
`build/aux/clang/cxx-build.log`. Browser evidence is
`build/engine/logs/clang-browser-integer-results.json` and
`build/engine/logs/clang-browser-integer-stdio-results.json`. See
[integer reproduction/manual download instructions](../src/aux/clang/test/README.md).
No manual browser test was required for this slice or its then-current
classification compile failure.

### Floating-point classification and runtime archive slice

- Public `math.h` now provides the five classification codes, `fpclassify`,
  `isfinite` and `isnormal`, preserving operand types and single evaluation.
  Existing math declarations retain C linkage in C++; INFINITY/NAN are float,
  and HUGE_VAL/HUGE_VALF/HUGE_VALL have their corresponding standard types.
  Unimplemented math function annotations remain intact.
- `make -C src/aux test-classification` builds real C and `<cmath>`
  C++ observers and checks their eight shared-libc imports. Eighty-three WAST
  assertions verify report lengths, normal process exits, metadata, all three
  guest widths, thirteen cases per width, six individual argument evaluation
  counts and unchanged errno. Compiled inputs carry no functional assertions.
- Initial execution passed 75 assertions and failed eight binary128 boundary
  cases, revealing narrowing in shared compiler comparison helpers. The seven
  helpers now compare the complete representation, preserving subnormals and
  low fraction bits, signed-zero equality and compiler-rt's NaN conventions.
  Twenty-four package WAST assertions cover adjacent values, full finite range,
  infinities and quiet/signaling NaN payloads on both operands. Other binary128
  arithmetic/conversion helpers still narrow through double.
- All 155 package libc + 46 system libc + 19 compiled stdio + 83 classification
  assertions pass natively and with ASan/UBSan: 303 in each transport. Leak
  detection remains disabled for ptrace; address/undefined-behavior checks
  remain enabled. C/C++ probes compile with warnings as errors.
- The rebuilt offline page's production worker passes all 83 compiled and
  24 package comparison assertions plus Bash bootstrap (108/108). The existing
  compiled stdio/formatter scenario also passes again (30/30). No GUI launched.
- `clang-cxx` now succeeds, producing the bootstrap libc++/libc++abi/
  experimental archives. Their respective sizes are 859,854 / 423,762 / 1,000
  bytes. `clang-sdk-check` includes actual `<cmath>` declarations, constant
  types and provider linkage. Full C++ link/startup acceptance is still pending.
- The first guest LLVM configure attempt reported an old libstdc++ because
  libc++ could not locate `mbstate_t` through `include_next`. An independent
  wrapper-first `<iosfwd>` compile confirmed the diagnosis. The toolchain now
  uses that order and the guest LLVM recipe selects libc++ explicitly. Native
  table generators retain their host library selection and still build.
- Guest LLVM configuration now stops at optional SHARED libLTO generation on
  the Generic platform. The next slice must select the executable/static build
  graph without fabricating dynamic-link support or dropping LLD's dependencies.
  Guest Clang/LLD remain unbuilt and uninstalled; archive construction alone
  does not establish a working compiler.
- Installed snapshots are refreshed: 329 tests, 281 supported, 534 VFS nodes.
  Both installed shared libraries match the 68,917-byte build; both new WAST
  snapshots match authored inputs. Corpus and SDK audits pass (57 headers,
  six include orders, 781 available functions, 147 unavailable, 31 globals).
  Unchanged libc, source staging, native generators, C++ SDK/runtime archives
  and both compiled fixtures do no work. LLVM is unchanged; no capacity limit
  was raised. Shell/Python syntax, JSON and diff checks pass.

Logs are `build/aux/clang/classification-*.log`, with retained copies under
`build/engine/logs/clang-classification-*`. Runtime archive diagnostics are in
`build/aux/clang/cxx-build.log` and `classification-runtime-final-build.log`;
the current configure failure is in `build/aux/clang/guest-build.log` and
`build/aux/clang/guest/CMakeFiles/CMakeConfigureLog.yaml`. Browser evidence is
`build/engine/logs/clang-browser-classification-results.json` and
`build/engine/logs/clang-browser-classification-stdio-results.json`. See
[compiled classification/browser instructions](../src/aux/clang/test/classification/README.md).
No manual browser check was required for that prerequisite slice or its
then-current guest configure failure.

### Guest executable configuration slice

- Guest LLVM configuration and Makefile generation now pass. A new guest-only
  profile excludes optional SHARED/MODULE outputs through upstream options:
  libLTO, libclang, Remarks C API libraries and plugins. PIC remains enabled.
  The generated graph contains Clang, LLD, LLVMLTO, LLVMRemarks and lldWasm;
  LLD's link command still includes all three static LTO/Remarks/Wasm libraries.
- LLVM's guest host triple is explicitly wasm32-unknown-unknown. The native
  generators retain x86-64 Linux, their host standard library and their original
  tool/plugin options. The guest profile is a dependency of guest configuration
  only, so editing it does not invalidate the native/C++ runtime configuration.
- Archive-only library probes falsely reported separate pthread/dl/rt/pfm
  libraries as available. The SDK contains none of those libraries. Explicit
  negative library settings remove `-lpthreads`, `-ldl` and `-lrt` from generated
  LLD link commands without changing shared-libc function availability. Remaining
  function-level probe results still need a provider audit before acceptance.
- The actual one-job Clang build completes its table generation and reaches
  AST source compilation. It fails in `llvm/ADT/bit.h` at the missing
  `machine/endian.h` include. The SDK's existing `endian.h` is not selected by
  LLVM's bare-Wasm branch. This is the next recorded source blocker; no source
  is dropped or native OS macro fabricated to suppress it.
- `clang-source`, `clang-native-tools`, `clang-sdk-check`, `clang-cxx` and
  guest configuration pass. Repeating them does no work. The compiler/linker
  executables remain unbuilt and uninstalled; package and full compiler
  acceptance tests cannot run yet. No runtime semantics or installed VFS files
  changed in this slice, so no manual browser check is needed.
- Diff/shell checks pass and the LLVM submodule remains unchanged. No shared
  capacity limit was changed.

The final build diagnostic is `build/aux/clang/guest-build.log`, retained as
`build/engine/logs/clang-configuration-build.log`. Generated graph/cache evidence
is in `clang-configuration-audit.json`, `clang-configuration-prerequisites.log`
and `clang-configuration-cache.log` under the same log directory. Earlier
plugin/libclang/Remarks configuration failures are retained there too. Resume
with `make -C src/aux clang` after addressing the endian/header compatibility;
then continue the POSIX/C++ capability, startup and compiler acceptance work.

### Bare-Wasm endian compatibility slice

- `src/aux/clang/include/machine/endian.h` supplies LLVM's BSD fallback header
  spelling and aliases its byte-order names to the public SDK's `endian.h`.
  Values remain defined in that public header. No upstream source or public
  SDK header is patched, and no host OS macro/header fallback is introduced.
- A guest-only toolchain composes the existing Wasm toolchain with this private
  include path. A configure-cache audit confirms that native generators and
  bootstrap libc++ retain their original toolchains/header paths. Make tracks
  the private header/toolchain inputs, and the observer tracks included headers.
- `make -C src/aux test-endian` builds a real C++ observer of staged
  `llvm/ADT/bit.h`, verifies its four shared CRT/libc imports, then runs 21 WAST
  assertions in native Bash. Checks cover the 128-byte report, normal exit,
  public/private byte-order tags, LLVM's native order, actual Wasm target,
  absent host OS macros, 16/32/64-bit byte swaps/conversions and stored bytes.
  Volatile inputs prevent constant folding. The observer does not link the
  libc++ runtime archives or establish their startup behavior.
- The same artifact passes all 21 assertions under ASan/UBSan. Leak detection
  remains disabled for ptrace; address/undefined-behavior checks remain enabled.
  The rebuilt offline page's production worker passes all 21 assertions plus
  Bash bootstrap (22/22), then exits normally. No GUI browser was launched.
- The one-job Clang build gets past endian detection and now fails at missing
  `std::wstring`/`std::wstring_view` and `file_status::getSize` in the AST
  precompiled header. The first errors expose disabled libc++ wide-character
  support; the latter exposes Generic's unselected LLVM POSIX/Windows backend.
  These are the next actual feature/platform blockers. Clang/LLD executables
  remain unbuilt and uninstalled; compiler acceptance is still pending.
- Installed snapshots are refreshed: 330 tests, 281 supported, 536 VFS nodes.
  The endian WAST snapshot matches its authored input. Corpus and guest SDK
  audits pass (57 headers, six include orders, 781 available functions,
  147 unavailable functions, 31 globals and exact provider signatures).
- Source staging, native generators, C++ SDK/runtime archives, guest
  configuration and the compiled endian fixture cache without recompilation.
  The final dependency-tracking build produces the identical probe bytes.
  Shell/JSON/diff checks pass; LLVM remains unchanged and no capacity limit
  was raised.

The latest source failure is in `build/aux/clang/endian-build.log`, also copied
to `guest-build.log`. Retained build, native, sanitizer, profile, corpus, SDK
and cache logs are under `build/engine/logs/clang-endian-*`. Browser evidence is
`build/engine/logs/clang-browser-endian-results.json`. See
[compiled endian/browser instructions](../src/aux/clang/test/endian/README.md)
for reproduction and optional upload/download evidence. No manual browser check
is needed for this slice or the remaining compile-time blockers. The next slice
should address real wide-string support, followed by the LLVM guest POSIX
profile and the outstanding startup/compiler acceptance checks above.

### Wide-character SDK and shared-provider slice

- The libc++ profile now enables wide characters. Public `wchar.h` supplies
  the required `FILE`/`struct tm` names and both wchar headers retain C linkage
  beneath libc++ wrappers. Existing comparison/search providers are declared;
  new shared `wmemcmp`, `wmemcpy`, `wmemmove`, `wmemset`, `wcsrchr`, `wcspbrk`
  and `wcsstr` providers implement their actual operations. No unsupported
  classification annotation is removed and no numeric/format API is fabricated.
- Comparisons use the signed four-byte guest wchar ordering, including fixing
  existing `wcscmp`/`wcsncmp`/`wcscoll`. Wide-memory counts are elements; moves
  preserve overlap in both directions and fills write the complete element.
  Sixty-one package WAST assertions cover zero-count invalid pointers,
  embedded NULs, signed extremes, pointer returns, searches, errno and canaries.
  Both installed libc paths match the 70,232-byte build artifact.
- The compiled C++ observer found an existing SDK limit bug: `stdint.h` used
  nonexistent Clang `__WCHAR_MIN__`/`__WINT_MIN__` macros, and the compiler's
  default wint bounds described a signed type while the SDK declares unsigned
  `wint_t`. Limits now match the actual public types, with include-order and
  C++ type/constant checks. `towlower`/`towupper` declarations return `wint_t`.
- `clang-sdk-check` passes with `std::wstring`/`std::wstring_view`, wide API
  aliases, signed wchar layout, unsigned wint limits and C linkage. The real
  staged LLVM `ConvertUTF.h` passes an isolated syntax check using generated
  libc++/guest headers and the existing guest LLVM configuration. Its former
  missing-type errors are resolved without removing UTF conversion declarations.
- `make -C src/aux test-wide` passes 34 WAST assertions against a real
  compiled C++ observer: non-ASCII `wstring_view` searches/comparisons,
  character-traits copy/move/fill, shared API calls, overlap and canaries.
  The observer needs libc++'s diagnostic-abort helper; it compiles the original
  staged `verbose_abort.cpp` into a fresh object and verifies 14 real shared
  libc imports. It does not construct an owning string or link stale archives.
  The same artifact passes all 34 assertions with ASan/UBSan.
- All 216 package libc assertions and 46 libc/kernel interaction assertions
  pass in native and sanitized Bash. The rebuilt offline production worker
  passes the 34 compiled assertions plus bootstrap (35/35), and the 61 direct
  provider assertions plus bootstrap (62/62); both exit zero. No GUI browser
  is launched and no manual browser evidence is required for this slice.
- The resumed one-job `make -C src/aux clang` now stops in libc++ `string.cpp`
  at eight real missing providers: `wcstol`, `wcstoul`, `wcstoll`, `wcstoull`,
  `wcstof`, `wcstod`, `wcstold` and `swprintf` (nine call-site errors).
  The wide-enabled runtime archive build remains incomplete. Older bootstrap
  archives are not used to bypass the failure; Clang/LLD remain unbuilt and
  uninstalled. LLVM's Generic file-status/backend and startup acceptance remain
  pending after these runtime dependencies.
- Installed snapshots now contain 332 tests (281 supported) and 539 VFS nodes.
  The two new snapshots match authored WAST bytes. Corpus and SDK audits pass:
  57 headers, six include orders, 792 available functions, 147 unavailable,
  31 globals and verified provider signatures. Unchanged libc, source staging,
  SDK probe and compiled wide fixture perform no recompilation. Shell/JSON/diff
  checks pass; LLVM remains unchanged and no shared capacity limit is raised.

The current build failure is `build/aux/clang/wide-final-build.log`, copied to
`guest-build.log` and retained as `build/engine/logs/clang-wide-build.log`.
Native, sanitizer, SDK, corpus, cache and packaging logs use the
`build/engine/logs/clang-wide-*` prefix. Browser evidence is
`clang-browser-wide-results.json` and `clang-browser-wide-memory-results.json`
in that directory. See [compiled wide-string/browser instructions](../src/aux/clang/test/wide/README.md)
for reproduction and optional upload/download evidence. Continue with the wide
numeric/formatting providers, then resume `make -C src/aux clang`; retain the
POSIX capability audit and real owning-string/startup/compiler acceptance gates.

### Wide integer conversion prerequisite slice

- Shared `integer.c` now implements `wcstol`, `wcstoul`, `wcstoll`,
  `wcstoull`, `wcstoimax` and `wcstoumax` using the same checked magnitude
  parser as byte input. The input reader preserves full wchar values;
  typed wrappers return wchar-element end pointers. There is no allocation,
  transcoding buffer or pointer-to-pointer type punning. The supported
  whitespace/digit set is ASCII C behavior, not Unicode classification.
- The existing integer package WAST now has 77 assertions. Each of its
  original 62 checks exercises narrow and wide input against shared
  expectations, including every base, limits, overflow consumption, null
  end pointers, errno, unchanged inputs and output guards. Fifteen additional
  fixtures run through all six wide providers: non-ASCII low bytes resembling
  digits/signs/spaces/hex prefixes/NUL, signed wchar extremes and embedded NUL.
- Public `wchar.h`/`inttypes.h` provide C-linkage declarations. The C++ SDK
  probe checks all six namespace aliases and return types. The existing
  compiled wide observer captures six conversion records through actual
  `<cwchar>`/`<cinttypes>` calls; WAST checks values, element offsets and errno.
  Its 224-byte report passes 52 assertions, including the prior view/traits
  checks, with 21 verified shared-libc imports. No owning string or stale
  runtime archive is substituted for the pending startup acceptance.
- Native and ASan/UBSan Bash each pass 231 package libc assertions, 46
  libc/kernel interaction assertions and 52 compiled observer assertions
  (329 each). The rebuilt offline page's production worker passes the integer
  suite plus bootstrap (78/78) and compiled suite plus bootstrap (53/53),
  with both sessions exiting zero. No GUI browser or manual evidence is
  required for this slice.
- The resumed one-job `make -C src/aux clang` rebuilds libc++abi and reaches
  libc++ `string.cpp`. Its former five wide-integer call-site errors are gone.
  Four real missing providers remain: `wcstof`, `wcstod`, `wcstold` and
  `swprintf` (four errors). The complete wide-enabled archive build remains
  incomplete; Clang and LLD remain unbuilt/uninstalled. The LLVM Generic
  backend, owning-string/startup, SDK driver defaults and final acceptance
  gates still follow these runtime dependencies.
- Both installed libc paths match the 71,444-byte build artifact. Refreshed
  snapshots match authored WAST bytes; corpus checks pass with 332 tests
  (281 supported) and 539 VFS nodes. SDK audit passes: 57 headers, six include
  orders, 798 available functions, 147 unavailable, 31 globals and checked
  provider signatures. Unchanged libc, source staging, SDK probe and compiled
  observer perform no recompilation. Shell/JSON/diff checks pass, LLVM remains
  unchanged and no shared capacity limit is raised.

The current failure log is `build/aux/clang/wide-integer-build.log`, copied to
`guest-build.log` and `build/engine/logs/clang-wide-integer-build.log`.
Other native/sanitizer/SDK/corpus/cache/packaging evidence uses the
`build/engine/logs/clang-wide-integer-*` prefix; assertion totals are recorded
in `clang-wide-integer-verification.json`. Browser evidence in that directory
is `clang-browser-wide-integer-results.json` and
`clang-browser-wide-integer-cxx-results.json`. The page is 4,538,804 bytes with
a 3,295,721-byte compressed tar. See the updated
[compiled wide/browser instructions](../src/aux/clang/test/wide/README.md)
and [integer/browser instructions](../src/aux/clang/test/README.md).
Continue with the audited floating/formatting providers above, then resume
the real runtime/compiler build and retain the remaining acceptance gates.

### Floating conversion prerequisite slice

- New shared `floating.c` replaces the narrow `strtof`/`strtod`/`strtold`
  parser and implements `wcstof`/`wcstod`/`wcstold`. One C-locale scanner
  preserves byte/wchar end pointers, rolls back incomplete exponents, accepts
  decimal/hex input and infinity/quiet NaN, and consumes saturated large
  exponents without integer overflow. Wide characters keep their full values;
  accepted NaN payloads produce a canonical quiet representation.
- The providers round bounded integer ratios directly to binary32/64/128,
  nearest with ties to even. They retain full long-double precision/range and
  avoid double intermediates and approximate quad helpers. Overflow returns
  signed infinity with ERANGE; tiny inexact results set ERANGE before rounding,
  while exact subnormals and successful conversions preserve errno. Prefix
  and word-array bounds derive from the widest guest format; a sticky tail
  resolves ties beyond the retained prefix. Trailing prefix zeros remain in
  the scale, avoiding unnecessary large-ratio work. The measured static
  conversion frame is 16,624 bytes; there is no allocation or host FP state.
- The new package WAST has 174 assertions, pairing each ASCII expectation
  across byte/wide input and checking result bits, end pointers, errno,
  unchanged input and canaries. It covers even/odd ties and values just above,
  normal/overflow/subnormal boundaries, signed zeros, invalid prefixes and
  exponents, NaN spellings, non-ASCII low bytes, binary128's low fraction bit
  and decimal values outside double's range. Stress vectors include 20,000
  exponent digits and nonzero tails after 20,000 zeros.
- Public wide declarations and the C++ SDK checks pass. The compiled wide
  observer adds three actual `<cwchar>` floating calls and captures their
  complete returned representations, element end offsets and errno. Its
  296-byte report passes 64 WAST assertions with 25 verified libc imports.
  Runtime/owning-string startup acceptance remains separate.
- Native and ASan/UBSan Bash each pass 405 package libc, 46 libc/kernel and
  64 compiled observer assertions (515 each). An additional 240 seeded
  decimal vectors, checked against exact rational expectations in three WAST
  files under `build/aux/libc/floating-fuzz`, pass in both modes (755 total
  each). Generated probes respect the data-segment limit and keep input data
  away from the live libc errno slot; the corrected probes require no engine
  or capacity changes. Assertions remain in WAST; generation is a development
  artifact rather than a new package build/test dependency.
- The rebuilt offline page's production worker passes the package suite plus
  bootstrap (175/175) and compiled observer plus bootstrap (65/65), both
  exiting zero. No GUI browser or manual evidence is required for this slice.
- The resumed one-job `make -C src/aux clang` reaches libc++ `string.cpp`
  with all three floating call-site errors resolved. One real missing provider
  remains: `swprintf` (one error). The complete wide-enabled runtime still
  cannot build; Clang/LLD remain unbuilt/uninstalled. Continue with the wide
  formatting work described above, then retain the LLVM POSIX backend,
  owning-string/startup, driver defaults and final compiler acceptance gates.
- Both installed libc paths match the 77,779-byte build artifact. Refreshed
  snapshots match authored WAST bytes; corpus checks pass with 333 tests
  (281 supported) and 540 VFS nodes. SDK audit passes: 57 headers, six include
  orders, 801 available functions, 147 unavailable, 31 globals and checked
  provider signatures. Unchanged libc, source staging, SDK probe and compiled
  observer perform no recompilation. Warnings-as-errors compilation,
  shell/JSON/WAST/diff checks pass; LLVM remains unchanged and shared capacity
  limits are unchanged.

The current failure is `build/aux/clang/floating-build.log`, copied to
`guest-build.log` and `build/engine/logs/clang-floating-build.log`.
Native/sanitizer/SDK/corpus/cache/packaging logs use the
`build/engine/logs/clang-floating-*` prefix; totals are recorded in
`clang-floating-verification.json`. Browser evidence in that directory is
`clang-browser-floating-results.json` and
`clang-browser-floating-cxx-results.json`. The page is 4,555,896 bytes with a
3,308,539-byte compressed tar. See the updated
[floating/browser instructions](../src/aux/clang/test/README.md) and
[compiled wide/browser instructions](../src/aux/clang/test/wide/README.md).

### Wide formatting and runtime archive slice

- Audited the staged libc++ `string.cpp`: its floating `to_wstring` overloads
  call `swprintf` with `%f` for promoted float/double and `%Lf` for binary128.
  Added real shared `swprintf`/`vswprintf` declarations and providers in
  `src/aux/libc/wide-format.c`; no declaration-only bypass or stale archive.
- The bounded wide formatter supports integer conversions and lengths,
  sign/alternate/zero/left flags, starred width/precision, pointer/count
  conversions, literal/full wide elements and UTF-8 strings. Capacities and
  precision count output wchar elements. It terminates a retained prefix for
  nonzero capacity, returns -1/EOVERFLOW on insufficient/zero capacity,
  EINVAL on unsupported/invalid format syntax, and EILSEQ on invalid UTF-8.
  Successful output preserves errno; `vswprintf` copies the argument cursor.
- Floating `f/F/e/E/g/G/a/A` output derives directly from binary64/128 bits.
  Fixed private integer decimal limbs generate the exact terminating decimal
  expansion; decimal/hex rounding is nearest/ties-to-even. No binary128
  narrowing, approximate quad arithmetic, allocation or host locale is used.
  Bounds derive from the guest's widest float format and have checked writes.
  Measured floating-format frame size is 23,904 bytes. A review caught `%g`
  fractional-precision overflow at INT_MAX; 64-bit derived precision and two
  regressions now cover bounded output for that request.
- Added 87 package WAST assertions for full elements, integer lengths/flags,
  UTF-8 errors, precision/width, canaries, typed count writes, errno,
  zero/exact/insufficient capacities, variadic alignment, signed zero,
  NaN/infinity, ties, carries, subnormal/normal limits and binary128 low bits.
  Synthetic variadic areas retain the compiler's 4/8/16-byte alignment;
  making only their base eight-byte aligned initially misread all quad values.
- Added `src/aux/clang/test/wide-format` with actual C and libc++ `<cwchar>`
  observers, `wide-format-fixture`/`test-wide-format`, dependency tracking and
  native/sanitizer/worker instructions. Its 21 WAST assertions check nine
  guarded records from each executable, including mixed int/double/binary128/
  long-long arguments, repeated calls, the original cursor and direct
  `swprintf`. Observers contain no formatting assertions or substitute libc.
- An additional 160 seeded binary64/128 vectors (seed 20261010) use independent
  Fraction/Decimal expectations, including full-range finite fixed output.
  All pass native and sanitizer execution. Reference expectations were corrected
  to strip `%g` trailing zeros. These supplementary generated probes remain
  under `build/aux/libc/wide-format-fuzz`; authored tests have no generator.
- Rebuilt the **complete wide-enabled** libc++ and libc++abi archives,
  905,086 and 423,762 bytes. The actual `string.cpp` compiles. Resumed the full
  one-job Clang build; configuration succeeds and compilation reaches the AST
  precompiled header. Its current source failure is
  `llvm/Support/FileSystem.h:701`, missing `file_status::getSize` on Generic.
  No Clang/LLD binary has been installed. Owning strings, C++ startup/shutdown,
  narrow floating formatting, platform/provider auditing and end-to-end compiler
  acceptance remain pending.
- Native and ASan/UBSan runs each pass **719 assertions**: 492 package libc,
  46 libc/kernel interaction, 21 compiled formatter and 160 supplementary
  numeric vectors. The sanitizer observer's original 60-second overall budget
  expired during assertions; its completed 180-second rerun passes without
  sanitizer errors. The new runner accepts `WASTE_TEST_TIMEOUT_MS`.
- Refreshed and audited installed snapshots in sequence: 335 corpus tests
  (281 supported), 80 installed files and 543 VFS nodes. The SDK audit passes
  for 57 headers, six include orders, 803 available functions, 147 unavailable,
  31 globals and provider signatures. Both installed libc paths exactly match
  the 91,057-byte build and have mode 0644. Unchanged libc, SDK, C++ archives
  and compiled observer builds cache. Warnings-as-errors, shell/JSON/WAST/diff
  checks pass; LLVM is untouched and shared capacity limits are unchanged.
- Regenerated the self-contained offline page and ran the same package and
  compiled tests through its production worker: **109/109 results, exit 0**,
  including Bash bootstrap. The page is 4,576,488 bytes with a 3,323,984-byte
  compressed tar. Worker evidence covers execution and packaging; it does not
  establish DOM rendering or final real-browser compiler acceptance.

The current full build log is `build/aux/clang/wide-format-build.log`, copied
to `guest-build.log` and `build/engine/logs/clang-wide-format-build.log`.
Native/sanitizer/SDK/corpus/cache/HTML logs use the latter directory's
`clang-wide-format-*` prefix; aggregate counts and artifact identities are in
`clang-wide-format-verification.json`. Production-worker evidence is
`clang-browser-wide-format-results.json`. See the
[wide-format reproduction and optional browser steps](../src/aux/clang/test/wide-format/README.md).

### Initial LLVM guest POSIX and metadata slice

- Added the owned `src/aux/clang/clang-waste.patch`, applied only to the staged
  LLVM source. `LLVM_WASTE_GUEST` selects the Unix implementation for the
  Generic wasm32 guest toolchain and rejects other toolchains. It does not
  define host OS macros, change CMake's platform, or enable native linker
  version scripts. Native generators and libc++ retain their separate profiles.
- LLVM's private file-status storage and constructor types derive size/device/
  inode widths from the actual guest stat fields. The public `off_t`, `dev_t`
  and `ino_t` remain 32-bit; the stable stat fields retain 64 bits. Configure
  probes `st_mtime_nsec`, and Unix status conversion uses the matching guest
  access/modify nanosecond fields. Full out-of-line filesystem/accessor runtime
  acceptance remains pending.
- Added C linkage to `sys/stat.h` and `fcntl.h`, including their shared
  `fchmodat` declaration, and extended the C++ SDK probe. The rebuilt libc is
  byte-identical to the installed 91,057-byte provider. No guest ABI layout,
  engine kernel semantics or shared capacity limit changed.
- Excluded absent frame-registration/unwinder symbols and unavailable pthread
  rwlock initialization from misleading archive-only configure results.
  The generated guest headers show both platform flags enabled, the real
  nanosecond-member probe enabled, and all four unsupported flags undefined.
  Function/provider auditing beyond these flags remains incomplete.
- Added `src/aux/clang/test/filesystem` with a compiled observer, dependency
  tracking, `filesystem-fixture`, `test-filesystem` and
  `test-filesystem-controlled`. All 63 ordinary functional assertions live in
  WAST: profile flags, public/private widths, inline LLVM size/constructor
  metadata, real C++ stat/lstat/fstat/directory-relative fstatat calls, errno,
  full metadata agreement, clock bounds and output preservation/guards.
  Synthetic large sizes test LLVM storage; they do not claim larger VFS limits.
- The initial timestamp-setting probe exposed the existing ENOSYS
  `utimensat` stub. The controlled fixture instead uses the existing private
  injected kernel clock and real file creation. Five additional WAST assertions
  establish exactly 4,294,967,313 seconds plus 123,456,789 nanoseconds; the
  ordinary interval assertions then verify that same timestamp in all four
  status APIs. `clock.wast` requires this scenario. Implementing timestamp
  mutation and auditing the positive `HAVE_FUTIMENS` result remain follow-ups.
- Rebuilt both native generators, the wide-enabled C++ archives and guest
  configuration. The full guest Clang attempt compiles its AST PCH and
  APValue/ASTConcept/ASTConsumer objects, then fails in `ASTContext.cpp` at
  `llvm/Support/Mutex.h:29`: the disabled-thread libc++ profile has no
  `std::recursive_mutex`. This replaces the missing `getSize` blocker; no
  compiler/linker executable was produced or installed.
- Native Bash passes 601 assertions: 492 package libc, 46 system libc and
  63 compiled metadata assertions. All 601 also pass under ASan/UBSan.
  Controlled native and sanitized sessions each pass 68 assertions, including
  the five exact clock checks. Both documented Make test targets pass.
- The refreshed production page worker passes 69/69 results, including Bash
  bootstrap, and exits zero. No GUI browser run is required for this slice.
  The SDK gate passes 57 headers, six include orders, 803 available functions,
  147 unavailable functions and 31 globals with Wasm signatures checked.
- Snapshot install/audit passes: 337 test files, 281 supported manifest entries,
  82 installed snapshots and 546 VFS nodes. The offline page is 4,580,480 bytes;
  its compressed tar is 3,326,979 bytes. The tested manifest SHA-256 is
  `1f6763c2e096f8874172b7b628c05417af0c7e7f086224cc17b66a7a4aac6b38`.
- Repeating source/runtime/generator/SDK/fixture targets performs no compilation
  or reconfiguration. Shell, WAST, Python, JSON and diff checks pass; the LLVM
  submodule remains unchanged.

Current guest build evidence is `build/aux/clang/platform-build.log`, copied
to `guest-build.log` and `build/engine/logs/clang-platform-build.log`.
Verification logs use `clang-platform-*`; aggregate results and artifact
identities are in `clang-platform-verification.json`. Production-worker
evidence is `clang-browser-filesystem-results.json`. See the
[filesystem reproduction and optional browser steps](../src/aux/clang/test/filesystem/README.md).
No manual browser check is required to reproduce this slice's execution gate.


### Compiler-profile recursive synchronization slice

- Extended the owned LLVM patch for `SmartMutex` only when `LLVM_WASTE_GUEST`
  is selected and `LLVM_ENABLE_THREADS=0`. The existing recursive acquisition
  counter replaces the unavailable `std::recursive_mutex`; successful
  `try_lock` increments it, scoped wrappers release it, and copying/moving stays
  unavailable. Native and threaded branches retain their original implementation.
  No standard-library type, pthread provider, engine synchronization or shared
  capacity change was added. These counters are valid only for the compiler's
  single execution thread, not WASTE's concurrent scheduler.
- Added `src/aux/clang/test/mutex` with a compiled observer, dependency
  tracking, `mutex-fixture`, `test-mutex`, instructions and a worker scenario.
  All 40 functional assertions are WAST. They verify profile flags, ownership,
  both SmartMutex variants, recursive lock/try/unlock, nested scoped guards,
  deferred/adopted standard lock helpers, independent once flags, argument
  forwarding and suppression of repeated initialization. `llvm::call_once`
  executes the actual rebuilt bootstrap archive, rather than a substituted
  function or header-only assumption. Broader C++ runtime acceptance remains
  pending. Negative unowned/over-release diagnostic coverage remains pending:
  the attempted dynamic export fixture did not resolve its functions, and
  indirect-call failures were not retained as mutex-trap evidence.
- The 40 assertions pass in native Bash and under ASan/UBSan. The existing
  compiled metadata boundary also passes all 63 native assertions. The rebuilt
  page's production worker passes 41/41 results, including Bash bootstrap,
  and exits zero; no GUI run is required for this slice.
- An isolated full object compile of the previously failing `ASTContext.cpp`
  passes with the generated guest target flags and actual SDK, without its PCH.
  The output is 1,199,845 bytes. A focused real-source compile exposes
  `AtomicLineLogger.h:67` and its lock guard naming unavailable `std::mutex`.
  Audit finds five other unguarded classes to address together: FileCollector,
  RWMutex, BalancedPartitioning, HashingOutputBackend and
  PerThreadBumpPtrAllocator. Parallel, ThreadPool and ConcurrentHashtable guard
  their standard mutex state, but ThreadPool includes the unadapted RWMutex.
  No complete guest compiler/linker binary has been produced or installed.
- Source staging still applies the complete patch to fresh copied inputs with
  zero fuzz. It now preserves timestamps only for byte-identical patched files,
  avoiding unrelated header rebuilds on subsequent slices. Corrected the
  nanosecond configure-template comment while extending the patch. Native
  header/type-interface compilation passes with warnings as errors.
- Rebuilt both native table generators and the current wide-enabled C++
  archives, refreshed guest configuration, and passed the documented
  `make -C src/aux test-mutex` target. The fixture and archives remain
  byte-identical to those tested through the production worker. Repeating
  source/C++/generator/SDK targets performs no build work; the fixture also caches.
- The aux build now compiles actual `LLVMSupport` before full Clang/LLD targets,
  retaining the dependency graph and one-job limit. The new attempt compiles
  BLAKE3 and Demangle objects, then stops in `APFloat.cpp` at the missing
  `FP_ILOGB0`/`FP_ILOGBNAN` constants required by LLVM's private libc FP helpers.
  This is the immediate build blocker; the independently confirmed standard
  mutex failures remain subsequent work. The first resumed attempt was stopped
  after rebuilding native generators to switch to this early platform gate;
  that interruption is not a successful complete guest build.
- The SDK audit passes 57 headers, six include orders, 803 available functions,
  147 unavailable functions and 31 globals, including provider signatures.
  Snapshot installation/audit passes with 338 test files, 281 supported entries,
  83 installed snapshots and 548 VFS nodes. The page is 4,580,572 bytes and
  its compressed tar is 3,327,046 bytes. Worker manifest SHA-256 is
  `1dd5012d3afce4d2ff5c1d1351af0d48c17f9fbe09cd2336d311cf34aa1d9ab0`.

Build and verification logs use `build/engine/logs/clang-mutex-*`. The current
one-job attempt is `build/aux/clang/mutex-support-build.log`, copied to
`guest-build.log` and `build/engine/logs/clang-mutex-support-build.log`.
The earlier generator-rebuild attempt remains in `mutex-build.log`.
Aggregate counts and artifact identities are in `clang-mutex-verification.json`.
Production-worker results are `clang-browser-mutex-results.json`. See the
[mutex reproduction and optional browser steps](../src/aux/clang/test/mutex/README.md).

### Integer exponent constants and LLVM helper slice

- Added `FP_ILOGB0` and `FP_ILOGBNAN` to public `math.h`, deriving their values
  from `INT_MIN` and `INT_MAX` through the compiler-support limits header.
  They are integer constant expressions of type `int`. No `ilogb` declaration,
  provider, unsupported annotation change or shared capacity change was added.
- Added `src/aux/clang/test/logb` with `logb-fixture`, `test-logb`, dependency
  tracking, compiled C/C++ observations, WAST assertions, a worker scenario and
  reproduction instructions. The C observer uses constant-expression enums
  and `_Generic`; C++ verifies exact types and the real LLVM int/long sentinels.
  All 131 functional assertions remain in WAST.
- The observer executes actual staged LLVM `intlogb<int>` and `intlogb<long>`
  using the same private namespace and no-errno/no-exception configuration as
  APFloat. Volatile binary32/64/128 operands preserve each type. Thirteen cases
  per width cover signed zeros, smallest signed/largest subnormals, smallest
  normals, largest finite values, ordinary signed values, infinities and NaNs.
  The 516-byte report includes preserved shared-libc errno. This establishes
  the selected exponent-helper boundary, not general LLVM math accuracy.
- `make -C src/aux test-logb` passes 131 native assertions; the final
  fixture passes the same 131 under ASan/UBSan. Existing compiled C/C++
  classification passes 83 native assertions. The rebuilt production worker
  passes 132/132 exponent results including Bash bootstrap, and the existing
  classification/quad-comparison scenario passes 108/108. Both exit zero with
  empty error logs. No manual GUI test is required for this slice.
- The real one-job aux build refreshes guest runtime/configuration and compiles
  APFloat into a 152,977-byte Wasm object, then progresses through APInt, APSInt
  and additional Support sources. It stops in `Allocator.cpp` at the
  `PerThreadBumpPtrAllocator` standard mutex and two lock guards. This confirms
  that template is required by the current graph. Synchronization adaptation,
  full Support/Clang/LLD builds and compiler acceptance remain pending.
- The rebuilt wide-enabled C++ archives remain byte-identical at 905,086 and
  423,762 bytes; native table generators remain cached. Shared libc remains
  byte-identical at 91,057 bytes with both installed copies mode 0644. Repeating
  source/runtime/generator/SDK and fixture targets performs no build work.
  The SDK audit passes 57 headers, six include orders, 803 available functions,
  147 unavailable functions and 31 globals, including provider signatures.
- Snapshot installation/audit passes with 339 test files, 281 supported
  entries, 84 installed files and 550 VFS nodes. The page is 4,581,404 bytes;
  its compressed tar is 3,327,671 bytes. Worker manifest SHA-256 is
  `1b4b74a29be3149d758f20689ffcc5b63903e33a8b475bdd18428369b4510a21`.
  Shell syntax, WAST parsing, scenario JSON, exact snapshot bytes and diff
  checks pass. The LLVM submodule remains unchanged.

The current full-build attempt is `build/aux/clang/logb-build.log`, copied to
`guest-build.log` and `build/engine/logs/clang-logb-build.log`. Verification logs
use `build/engine/logs/clang-logb-*`; aggregate counts and artifact identities
are in `clang-logb-verification.json`. The production-worker evidence is
`clang-browser-logb-results.json`. See the
[exponent reproduction and optional browser steps](../src/aux/clang/test/logb/README.md).

### Compiler synchronization declarations and local ownership slice

- Extended the owned staged-only LLVM patch in eight files to adapt six
  audited classes under `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`.
  PerThreadAllocator, FileCollector, HashingOutputBackend and AtomicLineLogger
  use LLVM's counted recursive mutex with guards deriving the member type.
  SmartRWMutex retains counted reader/writer ownership, rejects conflicting
  try-locks and has a noncopyable scoped reader. BalancedPartitioning omits
  mutex/condition-variable fields unused by its existing disabled-thread methods.
  Native and threaded backends remain intact; no `std` type, pthread provider,
  kernel synchronization or shared capacity change was introduced.
- Added `src/aux/clang/test/synchronization` with a compiled observer,
  header/template instantiation and native/threaded declaration gates, Make
  targets, WAST, a worker scenario and instructions. All 97 functional
  assertions remain in WAST. Both reader/writer variants exercise counted
  read acquisitions, exclusive writers, failed conflicts, scoped release and
  ownership restrictions. Negative unowned-release diagnostics remain pending.
- The actual generic PerThreadAllocator uses current C++ archives and the
  out-of-line atomic ID implementation compiled from LLVM's Allocator.cpp.
  An aligned observer allocator records construction, cache reuse, allocation/
  deallocation forwarding, alignment, independent state, moves, reset/red-zone
  forwarding, cache growth and exactly-once local destruction. The report is
  380 bytes. Concrete BumpPtrAllocator and hash-backend templates instantiate
  in a compile gate; their full runtime behavior is not claimed.
- The observer with ordinary compiler flags first failed to launch because of
  missing `env.__cxa_atexit`. Its original failure and a separately rebuilt
  default-flags module are retained. Only the diagnostic observer now uses
  `-fno-c++-static-destructors`, isolating automatic/local destruction from static
  cache finalization. Production compiler/archive flags remain unchanged.
  No substitute exit hook was added, and static C++ shutdown remains pending.
- The final documented native test passes 97 assertions; ASan/UBSan passes
  the same 97. Existing SmartMutex/archive-backed call_once checks pass 40
  native and 40 sanitized assertions. The rebuilt production worker passes
  98/98 synchronization results and 41/41 existing mutex results, including
  Bash bootstrap. Both exit zero with empty error logs. No manual GUI test is
  required for this diagnostic boundary; static shutdown is not accepted.
- Source staging applies all 15 patched files with zero fuzz. Both native table
  generators and the C++ runtime targets refresh successfully. The real guest
  build now compiles Allocator.cpp (2,562-byte Wasm object), AutoConvert.cpp and
  Base64.cpp, then fails in BalancedPartitioning.cpp on ThreadPoolInterface's
  unguarded futures/deferred async API. Fresh isolated full object compiles with
  generated guest flags, without PCHs, pass for FileCollector.cpp (22,598 bytes)
  and AtomicLineLogger.cpp (8,633 bytes). The whole Support/Clang/LLD graph and
  complete filesystem/logger/hash behavior remain pending.
- Native and guest/threaded declaration gates pass with warnings as errors.
  The C++ SDK probe passes; repeating source/runtime/generator/SDK and fixture
  targets performs no build work. The current C++ archives remain byte-identical
  at 905,086 and 423,762 bytes. Shared libc remains byte-identical at 91,057 bytes
  with both installed copies mode 0644; the diagnostic module verifies its 11
  libc imports against that provider.
- Snapshot installation/audit passes with 340 test files, 281 supported entries,
  85 installed files and 552 VFS nodes. The page is 4,583,012 bytes; its compressed
  tar is 3,328,877 bytes. Worker manifest SHA-256 is
  `59885b063662733f08186906b712045f2fea2988e52141203d3f34b33ffb1b12`.
  Shell syntax, WAST parsing, scenario JSON, exact snapshot bytes and diff checks
  pass. The LLVM submodule remains unchanged.

The full-build attempt is `build/aux/clang/synchronization-build.log`, copied
to `guest-build.log` and `build/engine/logs/clang-synchronization-build.log`.
Verification logs use `build/engine/logs/clang-synchronization-*`; aggregate
counts, limitations and artifact identities are in
`clang-synchronization-verification.json`. Production-worker evidence is
`clang-browser-synchronization-results.json`. The default-flags failure is
reproduced by `build/system-tests/clang/synchronization/static-exit/probe.wasm`
and recorded in `clang-synchronization-static-exit-repro.log`; the first failed
run remains in `clang-synchronization-static-exit-blocker.log`. See the
[synchronization reproduction and optional browser steps](../src/aux/clang/test/synchronization/README.md).

### LLVM deferred task result slice

- Resolved ThreadPoolInterface's missing futures only under
  `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`. The private LLVM-owned
  `ThreadPoolFuture` shares a deferred callable and value, void or reference
  result. Submission and handle destruction do not execute work. A future
  wait/get or the actual executor's queued thunk executes it once. Native and
  threaded branches retain standard shared futures and deferred async, verified
  by declaration/instantiation gates with warnings as errors.
- Added `src/aux/clang/test/thread-pool` with 79 WAST assertions over a
  308-byte compiled report. The observer compiles real LLVM ThreadPool.cpp and
  Threading.cpp and links current libc++/libc++abi archives, CRT and shared libc.
  Link-time optimization removes unused diagnostic paths using the actual
  single-thread strategy; no LLVM constructor, executor or provider is mocked.
  Static registration flags remain enabled. The fixture needs no static
  destructor and does not establish the missing C++ static shutdown provider.
- Verified value/void/reference get types, copy/move validity, selective future
  execution before FIFO draining, repeated waits, dropped handle/capture
  ownership, move-only result destruction, non-default/64-byte-aligned storage,
  const references and overloaded-address reference identity. Nested submissions,
  recursive task-group waits and group/executor destruction retain upstream
  semantics. SingleThreadExecutor group waits drain all groups, and later queued
  thunks do not re-execute a task completed through its future.
- The native Bash check passes 79 assertions and ASan/UBSan passes the same 79.
  The rebuilt production Bash worker passes 80/80 results including bootstrap,
  exits zero and has an empty error log. No manual GUI test is needed for this
  compiler boundary; optional upload/run/download steps are documented beside
  the fixture. The final module is 47,570 bytes and verifies 12 libc imports.
- Staging applies all 17 patched files with zero fuzz. ThreadPool.cpp's backoff
  include is restricted to its existing threaded implementation; this avoids
  parsing an unused random-device member in the single-thread source. The
  actual Support build now compiles BalancedPartitioning.cpp (52,095 bytes)
  and subsequent sources through CommandLine.cpp. A fresh isolated full
  ThreadPool.cpp object compile with generated guest flags, without its PCH,
  passes at 10,803 bytes. The full build now fails in CrashRecoveryContext.cpp
  on absent EX_IOERR/public setjmp.h; its mutex/signal handling also needs audit.
- The future audit finds no explicitly typed standard future or timed-future
  requirement in the selected Support/LTO/Clang/Wasm consumers. COFF Driver.cpp
  still independently uses standard futures/async, and the current LLD graph
  includes all drivers. ExponentialBackoff.cpp and file-lock users independently
  need random-device and C++ sleep facilities. Those remain platform-port work.
  No complete Support archive or Clang/LLD guest executable exists yet.
- Source/runtime/generator/SDK targets refresh successfully; their repeat and
  the unchanged fixture build perform no work. C++ archives remain 905,086 and
  423,762 bytes; shared libc remains 91,057 bytes. Snapshot installation/audit
  passes with 341 test files, 281 supported entries, 86 installed files and
  554 VFS nodes. The page is 4,582,936 bytes; its compressed tar is 3,328,821
  bytes. Worker manifest SHA-256 is
  `f0773f39af606a883acac6bb25d4b1d6f27cc623b404243430a922de8273b4c8`.
  Shell syntax, WAST parsing, JSON, exact snapshot bytes and diff checks pass;
  the LLVM submodule stays unchanged.

The final full-build attempt is `build/aux/clang/thread-pool-build.log`, copied
to `guest-build.log` and `build/engine/logs/clang-thread-pool-build.log`.
The first attempt is retained in `clang-thread-pool-first-build.log`.
Verification logs use `build/engine/logs/clang-thread-pool-*`; aggregate counts,
limitations and artifact identities are in `clang-thread-pool-verification.json`.
Production-worker evidence is `clang-browser-thread-pool-results.json`.
See the [task-boundary reproduction and optional browser steps](../src/aux/clang/test/thread-pool/README.md).

### Public checkpoint and crash-context source slice

- Added public C/C++ `setjmp.h` and BSD-compatible `sysexits.h`. The jump buffers
  retain Bash's 64-byte ABI; Bash's private header now forwards to the SDK.
  `setjmp` stays a caller-local returns-twice import, and the real C-linkage
  `longjmp` alias supports libc++'s `std::longjmp`. Both jump spellings use the
  existing engine evaluator transition. Unknown/inactive tokens now trap with
  an explicit diagnostic. C++ signal declarations have C linkage; the SDK
  provider review records the jump signatures and current mask limitation.
- CrashRecoveryContext uses LLVM's recursive counter only in the disabled-thread
  guest profile. Native/threaded profiles retain their standard mutex. The
  zero-fuzz staged patch now touches 18 files. The real full Support build and
  a fresh isolated compile with generated guest flags both produce a 5,069-byte
  CrashRecoveryContext Wasm object; the isolated compile uses no PCH. CMake
  discovers the actual public `sysexits.h` without a forced capability result.
- The compiled C/C++ checkpoint fixture passes 96 WAST assertions. Eight
  evaluator assertions cover direct jump aliases, buffer bounds and stale/
  unknown tokens. Native and ASan/UBSan Bash each pass all 104; the production
  page worker passes 105 including launch, with status zero and empty stderr.
  Checks include nested/copied checkpoints, positive/negative/zero values,
  volatile side effects, actual stack-pointer restoration and ordinary signal
  mask/disposition linkage. The C/C++ probes are 3,313 and 3,357 bytes, each
  passing the six-import libc check. This fixture requires no C++ archive symbols
  and leaves static destructor registration enabled.
- The continuation gate passes 43 private checks, eight buffer-copy assertions
  and 23 wait assertions. Its first attempt exposed an existing manifest-decoder
  assumption: browser fixtures mounted under `html-rt` retain different source
  identities. Mounted paths now validate independently as canonical paths within
  the test tree, preserving identities/groups. Native sanitizer and browser
  batch gates cover relocation, selection, traversal/outside-tree rejection and
  the existing batch contracts. Generated C++ headers have one shared Make rule.
- The full one-job build advances to `ExponentialBackoff.cpp`, failing on absent
  `std::random_device` and `std::this_thread::sleep_for`. Preserve the real
  LockFileManager backoff, jitter, caps, deadlines and yielding waits in the next
  slice. Complete LLVM crash-context execution/cleanup, `sigsetjmp` mask saving,
  C++ static shutdown, COFF futures and complete compiler acceptance remain
  pending. No Support archive or Clang/LLD executable is installed.
- The SDK gate passes 59 headers, six include orders and actual provider
  signatures (806 available functions, 147 unavailable, 31 globals). Source,
  C++ runtime, native-generator, SDK and unchanged fixture targets cache without
  work. Archives remain 905,086/423,762 bytes and libc remains 91,057 bytes.
  Installed corpus refresh/audit passes with 342 tests, 281 supported entries,
  87 installed files and 558 VFS nodes. The final page is 4,587,760 bytes and its
  compressed tar is 3,332,437 bytes. Worker manifest SHA-256 is
  `9468153505d5be35088c06c0152a119f61b5aa67878032595b9326b60ddc86ea`.
  WAST parsing, shell/Python/JSON checks, snapshot bytes and diff checks pass;
  the LLVM submodule remains unchanged. No manual browser check is required.

The full build log is `build/aux/clang/checkpoint-build.log`, copied to
`guest-build.log` and `build/engine/logs/clang-checkpoint-build.log`. The first
attempt is retained in `checkpoint-first-build.log`. Verification logs use
`build/engine/logs/clang-checkpoint-*`; counts, limitations and artifact hashes
are in `clang-checkpoint-verification.json`. Production-worker evidence is
`clang-browser-checkpoint-results.json`. See the
[checkpoint reproduction and optional browser steps](../src/aux/clang/test/checkpoint/README.md).

### LLVM yielding backoff slice

- Adapted the real ExponentialBackoff implementation only for
  `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`. A private LLVM-owned noncopyable
  generator calls the shared `getrandom` provider; the existing uniform
  distribution, exponential multiplier, interval cap and timeout calculation
  remain upstream code. Native/threaded branches retain standard random/thread
  backends. The current provider supplies process-local xorshift compatibility
  bytes for non-security jitter, not OS entropy; libc++ random-device support
  stays disabled. Public `sys/random.h` now has C++ C linkage, covered by the
  SDK declaration check and documented provider capability note.
- The private deadline helper uses actual shared `nanosleep` and engine timed
  readiness. EINTR recomputes the remaining monotonic interval instead of
  trusting libc's inaccurate remainder or restarting the full wait. Other
  errors terminate through real guest abort. The helper is tracked by production
  build dependencies; no standard-library substitute or C++ thread capability
  was introduced.
- The zero-fuzz staged patch now touches 20 files. The real full Support build
  and a fresh isolated compile using generated flags without a PCH both produce
  a 2,634-byte ExponentialBackoff Wasm object. The build advances through
  GraphWriter, InitLLVM and Locale to `LockFileManager.cpp:122`, failing on
  undeclared `getsid`. Resolve its real process-liveness/file-lock boundary next,
  preserving conservative host-identity handling. No complete Support archive
  or Clang/LLD executable is built or installed.
- Twelve live WAST assertions pass in native and ASan/UBSan Bash; the production
  worker passes 13 including launch. Checks cover the guest profile, real
  random range/variation, expired timeouts and an actual minimum sleep. Twenty
  controlled WAST assertions pass in native, sanitized native, raw browser API
  and production-worker sessions (21 including launch). Six controlled clock
  advances exercise early wakeups, two one-second waits, a final half-second
  clamp and exact timeout completion. A caught SIGUSR1 interrupts the second
  wait without changing its deadline or the overall 2.5-second duration.
  Native/threaded declaration gates pass. The observers only record values;
  functional expectations stay in WAST.
- The probes link actual staged ExponentialBackoff source, current C++ archives
  and production CRT/libc, retaining static registration. Live/controlled Wasm
  files are 3,767/4,446 bytes, each passing the ten-import libc check. These
  fixtures do not establish C++ static shutdown, complete file-lock behavior or
  compiler acceptance. Signal-mask checkpoint saving, real crash recovery,
  COFF futures and the other acceptance items remain pending above.
- The SDK gate passes 59 headers, six include orders and provider signatures
  (806 available functions, 147 unavailable, 31 globals). Source, C++ runtime,
  native generators, SDK and unchanged fixture targets cache without work.
  Archives remain 905,086/423,762 bytes and libc remains 91,057 bytes.
  Installed corpus refresh/audit passes with 344 tests, 281 supported entries,
  89 installed files and 561 VFS nodes. The page is 4,591,404 bytes; its compressed
  tar is 3,335,172 bytes. Worker manifest SHA-256 is
  `bb45fe63bf2fe3bd9eabe6c1595b9d1a9fa4d231690c137869d62cf122b44e05`.
  WAST parsing, shell/Python/JSON checks, snapshot bytes and diff checks pass;
  the LLVM submodule remains unchanged. No manual browser check is required.

The full build log is `build/aux/clang/backoff-build.log`, copied to
`guest-build.log` and `build/engine/logs/clang-backoff-build.log`. The first
attempt is retained in `backoff-first-build.log`. Verification logs use
`build/engine/logs/clang-backoff-*`; counts, limitations and artifact hashes
are in `clang-backoff-verification.json`. Production-worker evidence is
`clang-browser-backoff-results.json` and
`clang-browser-backoff-controlled-results.json`. See the
[backoff reproduction and optional browser steps](../src/aux/clang/test/backoff/README.md).

### LLVM lock-owner and null-signal query slice

- Adapted LockFileManager's process-existence check under `LLVM_WASTE_GUEST`
  with a private LLVM-owned helper using the real shared hostname provider and
  positive-PID `kill(pid, 0)`. Native source retains getsid. Different/failed/
  empty host identity and unrelated query errors conservatively retain the
  owner; negative local IDs never become group queries, and zero preserves the
  original current-process interpretation. Guest lock creation propagates a
  hostname-provider failure. No session-ID implementation or host PID access
  was fabricated, and no unavailable declaration was removed.
- Added null-signal queries to the shared engine process table, including
  existing current/explicit-group routes. Queries neither enqueue a signal nor
  service a pending handler, cancel waits, close descriptors or reap children.
  Zombies remain visible until waitpid removes them. Absent PID/group errors
  use ESRCH; invalid signal numbers use EINVAL. The guest kill/killpg boundary
  now converts private negative errno to `-1/errno`, preserving errno on success.
  All-process broadcast selection, cross-user permissions and session semantics
  remain outside this verified boundary.
- The first native fixture caught a missing public `ESRCH`: libc++ supplied its
  placeholder value 9969, so the kernel's actual ESRCH could not identify a
  vanished owner. Added the public value and verified libc++'s actual
  `std::errc::no_such_process` agrees. Shared strerror now returns the associated
  diagnostic. C++ wait declarations have C linkage, covered along with kill
  and gethostname by the SDK declaration probe. The first failed result is
  retained in `build/system-tests/clang/lock-owner/run-TBdei1/results.json`.
- The zero-fuzz staged patch now touches 21 files. The full Support build and
  an isolated compile with generated guest flags, without a PCH, each produce a
  14,649-byte LockFileManager Wasm object. A native full-source compile passes
  and retains its actual getsid symbol. The resumed one-job build advances to
  `ManagedStatic.cpp:22-31`, which names unavailable `std::recursive_mutex`.
  Address its real disabled-thread registry lifecycle next; LLVM managed
  shutdown does not replace C++ static-exit registration/finalization. No complete
  Support archive or Clang/LLD executable is built or installed.
- Eighty-three WAST assertions pass in native and ASan/UBSan Bash; the packaged
  production worker passes 84 including launch, status zero and empty stderr.
  They cover live/missing owners, stale errno, conservative remote/failed
  identity, invalid IDs/signals, group queries, blocked-pending signals and an
  actual child blocked in READ. Null queries leave its read intact; pipe EOF
  proves exit before the zombie query, and reaping retains the original exit
  status before making the PID absent. The C++ observer records values only.
  Its 6,885-byte Wasm artifact passes the thirteen-import libc check and uses
  production CRT/libc, with no C++ archive symbols or static-destructor
  suppression. Complete file-lock acquisition, contention, stale-file/signal
  cleanup, crash recovery, C++ shutdown and compiler acceptance remain pending.
- Broader native sanitizer checks pass: 117 process-lifecycle checks, 71
  process-group checks, 42 signal checks and the installed signal-mask regression
  in both direct and batch execution. The SDK gate passes 59 headers, six include
  orders and provider signatures (806 available functions, 147 unavailable,
  31 globals). Source, C++ runtime, native generators, SDK and unchanged fixture
  targets cache without work. Archives remain 905,086/423,762 bytes; the current
  libc is 91,090 bytes after adding the error message.
- Installed corpus refresh/audit passes with 345 tests, 281 supported entries,
  90 installed files and 563 VFS nodes. The page is 4,591,452 bytes; its compressed
  tar is 3,335,206 bytes. Worker manifest SHA-256 is
  `dee59c45d2ef8d774fad7c45e6032847620eb7d18cdcbfe6384aa2e5d8800909`.
  WAST parsing, shell/Python/JSON checks, snapshot bytes and diff checks pass;
  the LLVM submodule remains unchanged. No manual browser check is required.

The full build log is `build/aux/clang/lock-owner-build.log`, copied to
`guest-build.log` and `build/engine/logs/clang-lock-owner-build.log`. The first
attempt is retained in `lock-owner-first-build.log`. Verification logs use
`build/engine/logs/clang-lock-owner-*`; counts, limitations and artifact hashes
are in `clang-lock-owner-verification.json`. Production-worker evidence is
`clang-browser-lock-owner-results.json`. See the
[lock-owner reproduction and optional browser steps](../src/aux/clang/test/lock-owner/README.md).


### LLVM managed static registry slice

- Adapted only `ManagedStatic.cpp`'s mutex alias for
  `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`, reusing LLVM's counted recursive
  `SmartMutex<true>`. Native/threaded profiles retain `std::recursive_mutex`;
  registration, object publication and reverse shutdown stay upstream code.
- Added the source-backed observer and all 91 assertions under
  `src/aux/clang/test/managed-static`. It records nested lazy construction,
  const/mutable identity, last-registration destruction/recreation, destructor
  dependency lifetime, empty/repeated shutdown, scoped shutdown, custom/array
  deletion and claimed ownership without double destruction. Its 356-byte
  report is checked in WAST; the observer supplies no functional expectations.
- Normal static-registration flags remain enabled. ManagedStatic handles are
  trivially destructible and explicit LLVM shutdown owns their registry. This
  does not provide `__cxa_atexit`, fix the existing no-op `env.atexit`, or establish
  full C++/LLVM startup/shutdown. Claimed handles are reused only after registry
  cleanup. No standard-library type or provider is fabricated.
- The fixture compiles actual staged `ManagedStatic.cpp` without LTO, linking
  current libc++/libc++abi archives, production CRT and installed shared libc.
  The 6,112-byte Wasm probe passes its import audit (nine libc imports). Both
  native and ASan/UBSan Bash sessions pass 91/91 assertions. The packaged
  production worker passes 92/92 including launch, exits zero and has empty
  stderr; no GUI browser or manual check was required.
- Staging applies all 22 patched files with zero fuzz. The actual one-job build
  compiles ManagedStatic and advances through MathExtras, MemAlloc, MemoryBuffer,
  MemoryBufferRef, ModRef, MD5, MSP430Attributes and MSP430AttributeParser. It then
  stops at Mustache.cpp:677 because `std::ostringstream` is omitted by the
  disabled-localization libc++ profile. Both the real Support object and a fresh
  isolated full-source compile with generated target flags are 1,048 bytes.
  Native source/type checks, including the explicitly threaded guest branch,
  pass; the threaded object retains real pthread mutex calls.
- Fixture caching passes. Repeating source staging, native tools, C++ archives
  and the C++ SDK gate does no work. Current archives remain 905,086 and 423,762
  bytes. LLVM's read-only submodule is unchanged. Full Support, Clang/LLD
  linking/installation and compiler acceptance remain pending.
- Refreshed and audited installed snapshots: 346 corpus entries, 281 supported,
  91 installed files and 565 current VFS nodes. The rebuilt offline page is
  4,595,416 bytes with a 3,338,180-byte compressed archive. Its normalized
  packaged VFS manifest digest is
  `c84a2a91713526d43c1ad8a90db95cc455f55ce41bf06327f6e4ca0643088940`.
  WAST parsing, shell/Python syntax, JSON and whitespace checks pass.

The actual build log is `build/aux/clang/managed-static-build.log`, copied to
`guest-build.log` and `build/engine/logs/clang-managed-static-build.log`.
Native/sanitized runs and packaging checks use
`build/engine/logs/clang-managed-static-*`; counts and artifact hashes are in
`clang-managed-static-verification.json`. Production-worker evidence is
`clang-browser-managed-static-results.json`. See the
[managed registry reproduction and optional browser steps](../src/aux/clang/test/managed-static/README.md).


### LLVM Mustache numeric interpolation slice

- Resolved the actual Mustache numeric stream dependency only under
  `LLVM_WASTE_GUEST`, using a private LLVM-owned helper and the implemented
  shared wide formatter. Six-digit general formatting preserves rounding,
  exponent selection and signs; bounded ASCII output becomes an owning string.
  Native LLVM retains its original `std::ostringstream`. The guest narrow
  `snprintf` lacks floating conversions and raw double streaming uses exponent
  formatting, so neither was substituted. Localization stays disabled and no
  unavailable annotation or standard-library/provider type was fabricated.
- Added 37 shared exact IEEE bit-pattern inputs and all 96 WAST assertions in
  `src/aux/clang/test/mustache`. Explicit expected text, lengths and NULs
  cover exact ties, carries at fixed/scientific boundaries, signed zeros,
  binary64 extremes, normal/subnormal minima and signed/payload special values.
  A generated native classic-stream report independently matches every byte;
  both observers only record results and WAST owns all assertions.
- The fixture calls the same private numeric helper used by actual Mustache,
  linking current libc++/libc++abi archives, production CRT and shared libc.
  It also checks a 267-byte heap-backed accumulated string, independent copies,
  move ownership, mutation isolation, clear and substring lifetime. Normal
  static-registration flags remain enabled. The 1,416-byte guest report includes
  a 1,332-byte numeric table; the 5,940-byte probe passes its import audit
  (13 libc imports). Native and ASan/UBSan Bash sessions pass 96/96; the packaged
  production worker passes 97/97 including launch, exits zero and has empty
  stderr. No GUI browser or manual check was required.
- Complete Mustache template rendering remains pending until its real Support
  dependencies link. General narrow floating printf, broader C++ formatting,
  full startup and static-exit callbacks remain pending. The test establishes
  the numeric helper and selected owning-string operations, not compiler
  acceptance or full template/locale support.
- Staging applies all 23 patched files with zero fuzz. The actual one-job build
  compiles Mustache.cpp, NativeFormatting.cpp, OptimizedStructLayout.cpp,
  Optional.cpp, OptionStrCmp.cpp and PGOOptions.cpp, then fails in Parallel.cpp
  because Parallel.h:43-64 has unguarded Latch mutex/condition-variable fields
  and operations. The earlier audit covered the source's guarded executor but
  missed this header. Both the production Mustache object and an isolated full
  compile with generated flags and no PCH are 49,877 bytes. A full native source
  compile passes and retains actual stream references with no swprintf import.
- Fixture/source/runtime/generator/SDK caching passes after warming the SDK
  gate. Current archives remain 905,086 and 423,762 bytes. LLVM's read-only
  submodule is unchanged. The complete Support archive and Clang/LLD builds,
  providers, installation and compiler acceptance remain pending.
- Refreshed/audited snapshots: 347 corpus entries, 281 supported, 92 installed
  files and 567 VFS nodes. The rebuilt offline page is 4,596,448 bytes and its
  compressed archive is 3,338,955 bytes. Exact packaged WAST/libc bytes match
  their sources. The normalized packaged manifest digest is
  `3017ce9de549bdd8c81ff435ac409b7ae2742a0548b7155e4f877e54492b47ee`.
  WAST parsing, shell/Python syntax, JSON and whitespace checks pass.

The actual build log is `build/aux/clang/mustache-build.log`, copied to
`guest-build.log` and `build/engine/logs/clang-mustache-build.log`.
Native/sanitized runs and packaging checks use
`build/engine/logs/clang-mustache-*`; counts and artifact hashes are in
`clang-mustache-verification.json`. Production-worker evidence is
`clang-browser-mustache-results.json`. See the
[numeric interpolation reproduction and optional browser steps](../src/aux/clang/test/mustache/README.md).


### LLVM Parallel latch and scalar algorithm slice

- Resolved Parallel.h's private Latch mutex/condition-variable dependency only
  for `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`. The checked atomic counter
  supports balanced increments/decrements and repeated zero sync. Pending
  waits/destruction, underflow and overflow call actual shared abort even under
  `NDEBUG`. Native/threaded profiles retain standard blocking synchronization;
  no unavailable SDK annotation, standard type or capability was fabricated.
  TaskGroup already runs callbacks immediately in this compiler profile; WASTE's
  concurrent process/thread scheduler is unchanged.
- Added all 124 functional assertions in `src/aux/clang/test/parallel`.
  The observer compiles actual staged Parallel.cpp without LTO, linking current
  C++ archives, production CRT and shared libc. It records 122 values (488 bytes)
  for WAST to check: counts, nested/recursive TaskGroups, shared captures and
  heap-backed function closures, loop range fences, empty/nested loops, in-place
  updates, reductions, owning strings and ascending/descending sorts. The large
  sort checks 4,097 heap-backed values, order, bounds and element conservation.
  Move-only reduction uses upstream's iterator overload; its range wrapper
  copies the accumulator. Error-joining and threaded execution remain separate.
- Four invalid latch operations each run in a real guest child. WAST verifies
  SIGABRT wait statuses, reaping and parent survival. Normal static-registration
  flags stay enabled. Native and ASan/UBSan Bash sessions each pass 124/124,
  with six forks/exits and no handler failures or timeout. The packaged
  production worker passes 125/125 including launch, exits zero and has empty
  stderr. No GUI browser or manual test was required. The 30,958-byte probe
  passes its import audit (10 libc imports).
- Staging applies all 24 patched files with zero fuzz. The actual one-job build
  compiles Parallel.cpp and advances through PluginLoader, PrettyStackTrace,
  regex/rewriting, RISC-V support, hashes, SmallVector, strings, TarWriter,
  TextEncoding and ThreadPool. It then fails at TimeProfiler.cpp:43,218,410,420
  because its trace-instance registry and three lock guards unconditionally
  name `std::mutex`. Both the production Parallel object and an isolated full
  compile with generated flags and no PCH are 1,401 bytes. Full native and
  threaded guest-branch source compiles pass; the threaded object retains real
  condition-variable and pthread mutex references.
- Fixture/source/runtime/generator/SDK caching passes after warming the SDK
  gate. Current archives remain 905,086 and 423,762 bytes. LLVM's read-only
  submodule is unchanged. Full Support, Clang/LLD linking, providers,
  installation and compiler acceptance remain pending, along with actual
  static-exit registration/finalization and full C++ startup.
- Refreshed/audited snapshots: 348 corpus entries, 281 supported, 93 installed
  files and 569 VFS nodes. The rebuilt offline page is 4,599,504 bytes with a
  3,341,245-byte compressed archive. Exact packaged WAST/libc bytes match their
  sources. The normalized packaged manifest digest is
  `4209258124619bbea68338d1d5134b1139d5744dadc1a335901318ec47802b34`.
  WAST parsing, shell syntax, JSON and whitespace checks pass.

The actual build log is `build/aux/clang/parallel-build.log`, copied to
`guest-build.log` and `build/engine/logs/clang-parallel-build.log`.
Native/sanitized runs and packaging checks use
`build/engine/logs/clang-parallel-*`; counts and artifact hashes are in
`clang-parallel-verification.json`. Production-worker evidence is
`clang-browser-parallel-results.json`. See the
[parallel reproduction and optional browser steps](../src/aux/clang/test/parallel/README.md).


### LLVM TimeProfiler mutex and entry-storage slice

- Resolved the trace-instance registry's unguarded mutex only for
  `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`, selecting LLVM's existing counted
  `SmartMutex<true>`. All three scoped guards derive their type from the field.
  Native/threaded profiles retain `std::mutex`. Registry ownership, its static
  vector, event algorithms and JSON emission remain upstream code; no leaked
  allocation, static-destructor suppression, fake standard type or unsupported
  capability was introduced. WASTE's scheduler is unchanged.
- Added all 74 functional assertions in `src/aux/clang/test/time-profiler`.
  The observer includes actual staged TimeProfiler.cpp to inspect private entry
  types, links current C++ archives/CRT/shared libc, and records 72 values
  (288 bytes). WAST checks the actual local registry field/guard type, timing
  conversion, heap-backed names/details, metadata independence, moved buffers
  and 257 instant entries. Snapshots outlive their owning in-progress entry.
  Point-before-subtraction truncation is verified across fractional boundaries,
  negative time and timestamps beyond 32 bits. Const entry fields permit copy
  construction, so the observer constructs snapshots rather than assigning entries.
- The fixture retains normal static-destructor flags. Unused global paths are
  discarded by ordinary section collection; this checks entry storage, not
  global initialization, begin/end aggregation, finish/cleanup or emitted JSON.
  The complete production object still references `__cxa_atexit` and
  `__dso_handle`; real static-exit registration/library identity and remaining
  Support providers are required before full profiler acceptance. This mutex
  adaptation does not hide those dependencies or establish C++ shutdown.
- Native and ASan/UBSan Bash sessions each pass 74/74, exit zero and have no
  timeout or handler failure. The packaged production worker passes 75/75
  including launch, exits zero and has empty stderr. The 26,321-byte Wasm probe
  passes its import audit (10 libc imports); no GUI or manual test was needed.
  Full native and explicitly threaded guest-branch source compiles pass and
  retain the real standard mutex and pthread mutex calls.
- Staging applies all 25 patched files with zero fuzz. The actual one-job build
  compiles TimeProfiler.cpp and advances through Timer, ToolOutputFile, Twine,
  Unicode support, VersionTuple, VirtualFileSystem/output backends, WithColor
  and YAMLParser. It then fails in YAMLTraits.cpp:1132,1146,1160,1174 on missing
  public `PRIX8/16/32/64`. Both the production TimeProfiler object and an isolated
  full compile with generated flags and no PCH are 61,449 bytes. YAML floating
  scalar output uses narrow `%g`, whose provider remains unsupported; fixing
  integer-format macros alone will not establish complete YAML serialization.
- Fixture/source/runtime/generator/SDK caching passes after warming the SDK
  gate. Current archives remain 905,086 and 423,762 bytes; shared libc is
  unchanged at 91,090 bytes. LLVM's read-only submodule is unchanged. Full
  Support and Clang/LLD linking, providers, installation and acceptance remain
  pending, along with full C++ startup and static exit.
- Refreshed/audited snapshots: 349 corpus entries, 281 supported, 94 installed
  files and 571 VFS nodes. The rebuilt offline page is 4,601,332 bytes with a
  3,342,618-byte compressed archive. Exact packaged WAST/libc bytes match their
  sources. The normalized packaged manifest digest is
  `c96ccc5aad7ace93fef0fdee931171e00e9f4e0515668d2c50f0e7c88fa89bca`.
  WAST parsing, shell syntax, JSON and whitespace checks pass.

The actual build log is `build/aux/clang/time-profiler-build.log`, copied to
`guest-build.log` and `build/engine/logs/clang-time-profiler-build.log`.
Native/sanitized runs and packaging checks use
`build/engine/logs/clang-time-profiler-*`; counts and artifact hashes are in
`clang-time-profiler-verification.json`. Production-worker evidence is
`clang-browser-time-profiler-results.json`. See the
[profiler storage reproduction and optional browser steps](../src/aux/clang/test/time-profiler/README.md).

### Public uppercase integer-format macro slice

- Added `PRIX8/16/32/64` to public `inttypes.h`, using `X` for promoted small
  unsigned values/uint32_t and `llX` for the guest uint64_t. The existing shared
  formatter already supports uppercase hexadecimal output; no LLVM-private
  formatter, libc implementation or runtime scheduling path was added.
- Extended the existing compiled C/C++ formatter observer with eight cases per
  language. WAST owns all 35 assertions: zeros/maxima, high bits, small integer
  promotions, mixed 32/64-bit argument alignment, padding, truncated 64-bit
  output and uppercase/lowercase parity, alongside the original bounded-output
  cases. Both clients record sixteen 176-byte observations (2,816 bytes each),
  call the real shared vsnprintf twice and preserve the original va_list cursor,
  errno and unwritten buffer canaries. The C++ input also includes `<cinttypes>`.
- Native and ASan/UBSan Bash sessions each pass 35/35, exit zero and have no
  timeout or handler failures. The packaged production worker passes 46/46:
  35 compiled assertions, ten package formatter assertions and Bash launch.
  It exits zero with empty stderr. The C/C++ probes are 2,493/2,533 bytes and
  pass their actual provider import audits (six libc imports each). No GUI or
  manual browser test was required.
- The actual one-job LLVM build now compiles YAMLTraits.cpp. A separate complete
  compile with the generated target flags and no PCH also passes; both objects
  are 62,099 bytes. Support then fails at raw_os_ostream.cpp:26,29 because
  disabled libc++ localization leaves std::basic_ostream<char> undefined for
  write/tellp. Its header definition is guarded by `_LIBCPP_HAS_LOCALIZATION`.
  The adapter and selected consumers need an explicit guest component/runtime
  audit. This does not establish YAML floating output: narrow `%g` remains
  unsupported. Full Support, Clang/LLD, startup/static exit, installation and
  compiler acceptance remain pending.
- SDK headers/include-order/ABI/provider checks pass (59 headers, six include
  orders; 806 available functions, 147 unavailable functions and 31 globals).
  Repeated fixture/source/runtime/generator/SDK targets do no work. C++ archives
  and shared libc remain byte-identical to the preceding slice at 905,086,
  423,762 and 91,090 bytes. The 25-file LLVM patch and read-only submodule are
  unchanged. Shared sleep/time/scheduler behavior is unchanged.
- Refreshed and audited snapshots retain 349 entries, 281 supported, 94 installed
  files and 571 VFS nodes. The rebuilt offline page is 4,602,608 bytes with a
  3,343,574-byte compressed archive and 492 packaged files. Exact embedded WAST,
  inttypes.h and libc bytes match the authored/installed inputs. The normalized
  packaged manifest digest is
  `5ddcbc938970a3774904a662bf81fd1905fcd03c9b857c14991ddddb9360a16e`.
  WAST parsing, shell syntax, JSON and whitespace checks pass.

The actual build log is `build/aux/clang/inttypes-build.log`, copied to
`guest-build.log` and `build/engine/logs/clang-inttypes-build.log`.
Native/sanitized, SDK, snapshot and packaging evidence uses
`build/engine/logs/clang-inttypes-*`; counts and artifact hashes are in
`clang-inttypes-verification.json`. Production-worker evidence is
`clang-browser-inttypes-results.json`. The isolated YAML compile command/log
and object are under `build/system-tests/clang/stdio/source`. See the updated
[compiled formatter reproduction and optional browser steps](../src/aux/clang/test/stdio/README.md).

### Guest stream-component and memory-output slice

- Audited the standard-stream adapter against actual generated target closures:
  328 Clang-driver targets and 99 LLD targets. Neither selects clangFormat, the
  production caller of raw_os_ostream; other callers are outside these graphs.
  Omitted only raw_os_ostream.cpp from guest Support and declared it optional
  through LLVM_OPTIONAL_SOURCES. LLVM's source inventory check remains enabled.
  Native builds retain the adapter, and a full native source compile passes.
  Localization remains disabled. Re-audit if the selected tool graph expands.
- Added the compiled observer and all 51 assertions under
  src/aux/clang/test/raw-stream. It compiles actual raw_ostream.cpp and
  NativeFormatting.cpp without LTO and links current C++ archives/CRT/shared
  libc. A nonterminal sink records callbacks; LLVM owns buffering, flushes,
  positions, pwrite dispatch, integer/hex formatting and escaping. The real
  raw_string_ostream checks heap-backed output and independent copies. WAST
  verifies 44 observed values, full binary output and untouched output-slot
  tails in a 1,280-byte capture, including destructor flushing after input
  mutation. Generic pwrite does not flush pending trailing bytes in this sink;
  its position/output checks preserve that upstream behavior.
- The first observer link exposed base-vtable dependencies on POSIX color
  helpers. The fixture now isolates their unchanged upstream ANSI definitions
  and constants, preserving the license and recording complete source hashes.
  Extraction fails if its source boundaries change. These generated test inputs
  are under build; they neither replace production providers nor claim a full
  Process.cpp compile. Complete POSIX resource/descriptor/terminal output still
  requires its real translation units and providers. No standard-stream type,
  configure success or kernel capability was fabricated.
- Native and ASan/UBSan Bash sessions each pass 51/51, exit zero and have no
  handler failures or timeout. The packaged production worker passes 52/52
  including Bash launch, exits zero and has empty stderr. The 22,709-byte probe
  passes its import audit (12 libc imports). No GUI or manual browser test was
  required. Ordinary static-destructor flags remain enabled; this does not
  establish static-exit registration or full C++ startup.
- The 26-file patch stages with zero fuzz. The actual one-job build passes the
  adapter boundary and compiles raw_ostream.cpp (37,506 bytes) and its proxy.
  It then fails at raw_socket_stream.cpp:24 because poll.h is missing. Audit
  socket-stream consumers before implementing dependencies or excluding the
  component. Any needed poll/socket implementation must reuse engine-owned
  readiness, waits and yield/resume. Full Support, Clang/LLD linking and
  installation remain pending, as do narrow floating/YAML output and the other
  recorded lifecycle/provider boundaries.
- Repeated fixture/source/runtime/generator/SDK targets do no compilation after
  warming the SDK gate. C++ archives and shared libc remain byte-identical at
  905,086, 423,762 and 91,090 bytes. LLVM's read-only submodule is unchanged.
  There are no engine, scheduler, clock or sleep changes in this slice.
- Refreshed/audited snapshots: 350 entries, 281 supported, 95 installed files
  and 573 VFS nodes. The rebuilt offline page is 4,602,588 bytes, its compressed
  archive is 3,343,558 bytes and it contains 493 files. Exact packaged WAST/libc
  bytes match their inputs. The normalized packaged manifest digest is
  `3aee8e116a4a59c69b276da24b4ed1ffe7ff71ea5c5785bba7caee66a17f85ef`.
  WAST parsing, shell/Python syntax, JSON and whitespace checks pass.

The actual build log is build/aux/clang/raw-stream-build.log, copied to
guest-build.log and build/engine/logs/clang-raw-stream-build.log. The selected
target closures and native-retention checks are in clang-raw-stream-graph-audit.json.
Native/sanitized, cache, snapshot and packaging evidence uses
build/engine/logs/clang-raw-stream-*; counts and artifact hashes are in
clang-raw-stream-verification.json. Production-worker evidence is
clang-browser-raw-stream-results.json. See the
[raw-stream reproduction and optional browser steps](../src/aux/clang/test/raw-stream/README.md).


### Guest socket-stream scope slice

- Audited socket-stream symbols/includes across the read-only LLVM checkout and
  the regenerated executable closures: 328 Clang-driver targets, 99 LLD targets
  and 2,499 selected upstream source files. Its only caller is the disabled LLVM
  Support unit test. Omitted raw_socket_stream.cpp only from guest Support,
  alongside the previously audited std::ostream adapter, and declared both in
  LLVM_OPTIONAL_SOURCES. The inventory check stays enabled. Native builds retain
  both; the full native Support archive and table generators build, and an
  isolated native socket-source compile produces a 26,496-byte object.
- The 26-file patch stages with zero fuzz. The actual one-job guest build retains
  raw_ostream.cpp and its proxy, passes the socket boundary and compiles Atomic,
  DynamicLibrary, Errno and Memory. It then fails in Path.cpp:1254 through
  Unix/Path.inc:77 on missing sys/statvfs.h. Required path support must be ported;
  this is not a justification for another blanket source exclusion.
- Fresh full DynamicLibrary/Memory compiles without PCH match the production
  objects at 8,950/11,719 bytes. Symbol inspection of both production objects
  confirms mangled dynamic-loading and memory-mapping imports from missing
  public C++ linkage guards. Recorded that SDK/provider follow-up explicitly;
  neither source compilation nor this exclusion establishes those runtime APIs.
- Retained memory-stream WAST passes 51/51 in native Bash and ASan/UBSan, with
  zero exit status, no handler failure and no timeout. The rebuilt production
  page's worker passes 52/52 including launch, exits zero and has empty stderr.
  The rebuilt 22,709-byte observer is byte-identical to the exercised artifact
  and verifies its 12 libc imports. No new assertion suite or GUI test was needed.
- Repeated source/runtime/generator/SDK and fixture targets cache after warming
  the SDK check. The C++ archives, shared libc, observer and offline page remain
  byte-identical to the previous slice. Packaging still contains 493 files;
  the page is 4,602,588 bytes and its compressed archive is 3,343,558 bytes.
  LLVM's submodule stays unchanged; engine clocks, sleep and scheduling are
  unchanged. Full guest Support, Clang/LLD, startup/static exit, installation and
  compiler acceptance remain pending. Shell syntax and whitespace checks pass.

The actual build log is `build/aux/clang/socket-stream-build.log`, copied to
`guest-build.log` and `build/engine/logs/clang-socket-stream-build.log`.
Graph/source selection is recorded in `clang-socket-stream-graph-audit.json`;
platform source commands and diagnostics use `clang-socket-stream-*` under
`build/engine/logs`. Native/sanitized and worker evidence is summarized in
`clang-socket-stream-verification.json`, with worker results in
`clang-browser-socket-stream-results.json`. The retained
[raw-stream reproduction instructions](../src/aux/clang/test/raw-stream/README.md)
now describe both optional components.

### Optional filesystem queries and C++ provider slice

- Audited the actual filesystem-query callers: CachePruning uses disk_space
  and propagates errors for byte/percentage-based pruning; RealFileSystem
  forwards is_local errors. The guest namespace has no volume statistics or
  mount-locality interface, and the installed-file packaging budget is not a
  filesystem quota. Only the guest branches of these three optional queries
  now return function_not_supported (ENOSYS), preserving errno and locality
  outputs. Upstream Boolean wrappers return false. Native statvfs/statfs code
  and required Path sources remain selected; capacity-based pruning remains
  unavailable in the guest.
- Added public C++ C-linkage guards to dlfcn.h and sys/mman.h, covering mapping,
  synchronization, shared-memory and dynamic-loading declarations. Extended the
  compile-only C++ SDK probe with matching C redeclarations and the actual i64
  mmap signature. Production DynamicLibrary/Memory objects and complete fresh
  compiles without PCH are byte-identical at 8,926/11,696 bytes and now import
  unmangled dlopen/dlsym/dlclose/dlerror and mmap/munmap/mprotect names.
- Added the compiled platform observer under src/aux/clang/test/platform.
  All 81 functional assertions are in platform.wast. A 512-byte capture records
  78 observations: existing/missing paths, valid/invalid descriptors, error
  category, errno/output preservation, anonymous zero initialization and
  protection changes, shared msync/private-file isolation, rejected mapping
  inputs and an actual 4-GiB offset that would incorrectly succeed if narrowed.
  A freshly compiled 464-byte PIC library verifies C++ function-pointer calls,
  shared state, reference lifetime, missing symbols/libraries and consumed
  dlerror state. Process-wide dlopen(NULL) is explicitly observed as unsupported;
  RTLD enforcement and complete LLVM Memory/DynamicLibrary execution remain
  separate acceptance work.
- The first ordinary-archive link exposed the known missing env.__cxa_atexit
  through libc++ error categories. Only the fixture's actual system_error.cpp
  is compiled without static-destructor registration, using its C++26 source
  profile. Other fixture sources and production C++ archives retain normal
  registration. This is diagnostic query/provider scope, with no no-op registry
  or fabricated category implementation. Real C++ static exit remains pending.
  The generator isolates the unchanged actual query definitions from staged
  Unix/Path.inc, preserving the license and recording its full source hash;
  it rejects changed boundaries and does not establish a full Path compile.
- Native and ASan/UBSan Bash sessions each pass 81/81, exit zero and have no
  timeout or handler failures. The packaged production worker passes 82/82
  including Bash launch, exits zero and has empty stderr. The 7,354-byte observer
  verifies its 19 libc imports; its mapping imports retain the engine ABI,
  including mmap's i64 offset. No GUI or manual browser test was required.
- The 26-file patch stages with zero fuzz. Native Support, including Path,
  rebuilds successfully, as do matching table generators. The actual one-job
  guest build recompiles DynamicLibrary/Memory and advances to Path's executable
  discovery, failing at Unix/Path.inc:331 on Dl_info. A full fresh compile with
  all diagnostics enabled reports 31 errors spanning executable discovery,
  PATH_MAX, link/symlink, reentrant account lookup, madvise, realpath, record
  locks and usleep. These required methods remain in the build. No unavailable
  annotation was removed or native capability advertised.
- SDK checks pass: 59 headers, six include orders, 806 available functions,
  147 unavailable functions and 31 globals. Repeated source/runtime/generator/
  SDK and fixture targets perform no compilation after warming the declaration
  probe. C++ archives and shared libc remain byte-identical at 905,086, 423,762
  and 91,090 bytes. LLVM's submodule remains clean. No engine, scheduler, clock
  or sleep implementation changed. Full Support, Clang/LLD linking, installation
  and compiler acceptance remain pending.
- Refreshed/audited snapshots contain 351 entries, 281 supported, 96 installed
  files and 575 VFS nodes. The rebuilt offline page contains 494 files, is
  4,604,640 bytes and embeds a 3,345,098-byte compressed archive. Exact packaged
  WAST, changed public headers and both libc copies match their inputs. The
  normalized packaged manifest digest is
  dee2c08e37eb25cef946dbf742220a8810192f63caffec105feaf5399af4dd71.
  WAST parsing, shell/Python syntax, JSON and whitespace checks pass.

The actual build log is build/aux/clang/platform-build.log, copied to
guest-build.log and build/engine/logs/clang-platform-build.log. Source/caller
audits, full platform compile commands, symbols and diagnostics use
clang-platform-* under build/engine/logs. Native/sanitized, SDK, snapshot,
cache and packaging evidence is summarized in clang-platform-verification.json;
production-worker results are in clang-browser-platform-results.json. See the
[platform reproduction and optional browser steps](../src/aux/clang/test/platform/README.md).


### Loaded executable identity and shared path-limit slice

- Added public `waste_get_executable_path` in waste/process.h, backed by shared
  libc and the bounded waste_kernel.executable_path_v1 copy ABI. The loader
  captures the resolved namespace pathname with the owned executable snapshot;
  committed capsules own that identity, fork copies it and successful exec
  replaces it. The query is independent of argv[0], cwd, later rename/unlink and
  the calling library. Exact buffers include NUL; short buffers return ERANGE
  untouched, invalid spans return EFAULT, and success preserves errno. No host
  path, allocation, platform call, scheduler or separate compiler kernel is added.
  Requested filename suffixes still control existing WAT/WAST classification.
- Guest LLVM getMainExecutable now uses that real provider instead of entering
  the native dladdr fallback. Native branches remain unchanged. Private limits.h
  derives PATH_MAX from the existing src/config.h VFS_PATH_MAX_BYTES, as does
  engine image-path storage. This bounds resolved namespace names, not the
  separate POSIX input-parser length. Guest include order is libc++ wrappers,
  private adapters, public SDK, then compiler headers; bootstrap/native profiles
  retain their own order. Make tracks both the adapter and shared configuration.
- The new fork/rename/unlink observer exposed a stale open-description pointer:
  unlink compacted the pathname table without rebasing the redirected output
  descriptor, so the next kernel clone rejected it as outside the namespace.
  GDB confirmed that failure. Compaction now rebases surviving entries, detaches
  removed entries and preserves retained file objects, including dup aliases.
  Fork accepts detached regular descriptions; writes avoid absent namespace
  metadata, and fstat retains file size/inode. General unlinked metadata and
  directory lifetime still need broader acceptance.
- Added src/aux/clang/test/executable-path. The generator extracts the
  actual unchanged getMainExecutable definition from staged Unix/Path.inc,
  preserves the license, records the complete source hash and rejects changed
  extraction boundaries. Its diagnostic unit does not replace production Path
  or establish full Path support. Current C++ archives and this fixture retain
  normal static-registration flags; no static-exit shim/exception is added.
- All 164 functional assertions are in executable-path.wast. Four fixed
  640-byte slots capture the original process, fork child, replacement image
  and waiting parent. They verify relative dotted invocation, fabricated argv,
  cwd/rename/unlink independence, shared-library LLVM calls, process identity,
  exact/short/zero capacities, errno, invalid pointers and a memory-tail failure
  without partial writes. An unlinked open executable remains readable through
  a forked alias; separate WAST checks write/read and duplicate lifetime after
  unlink, including retained fstat size/inode. Explicit seeks isolate this evidence from the separately exposed
  cross-fork shared regular-file-offset limitation, which remains pending.
- Native and ASan/UBSan Bash each pass 164/164, exit zero and have no handler
  failures or timeout. The packaged production worker passes 165/165 including
  Bash launch, exits zero and has empty stderr. The 4,823-byte observer verifies
  21 libc imports. No GUI or manual browser test was required.
- The actual one-job guest build gets past executable discovery and path limits,
  then fails at Unix/Path.inc:455 on unavailable symlink. A full fresh compile
  with actual generated target flags, no PCH and all diagnostics reports 28
  errors: link/symlink, reentrant account lookup, madvise, realpath, record locks
  and usleep. Required Path remains selected and unavailable annotations remain
  intact. A complete native Path compile passes, producing a 143,336-byte object.
  The 26-file patch stages with zero fuzz; LLVM's submodule remains clean.
- Existing direct-start (366), path-VFS (64), kernel C (265), terminal/readiness/
  timing/process-group/descriptor guards and the path-VFS, descriptor-flags,
  directory-umask and shared-memory sanitized batch regressions pass. The broader
  posix-kernel target remains red at the retained standalone pipe-descriptors
  empty read: “external input waits require a runtime terminal context.” Its
  batch reproduction has the same failure (12/136 assertions pass). No read or
  wait implementation changed in this slice; this gate is recorded separately
  rather than reported as passing.
- SDK checks pass with 60 headers, six include orders, 807 available functions,
  147 unavailable functions and 31 globals. Repeated source/runtime/generator/
  SDK and fixture targets perform no compilation after warming the declaration
  probe. C++ archives remain byte-identical at 905,086/423,762 bytes. Shared libc
  is now 91,259 bytes with the new public wrapper. Full Support, Clang/LLD linking,
  installation, static exit and compiler acceptance remain pending.
- Refreshed/audited snapshots contain 352 entries, 281 supported, 97 installed
  files and 578 VFS nodes. The offline page contains 496 files, is 4,608,356
  bytes and embeds a 3,347,884-byte compressed archive. Exact packaged WAST,
  waste/process.h and both libc copies match their inputs. The normalized
  packaged manifest digest is
  807f6919a8f1109524ffd9617c4ffe79b2ac1ad9897be3a0d63c9fc203e5119f.
  WAST parsing, shell/Python syntax, JSON and whitespace checks pass.

The actual build log is build/aux/clang/executable-path-build.log, copied to
guest-build.log and build/engine/logs/clang-executable-path-build.log. Complete
Path commands/diagnostics, debugger findings, SDK, snapshots and cache logs use
clang-executable-path-* under build/engine/logs. Native/sanitized, packaging and
compiler evidence is summarized in clang-executable-path-verification.json;
worker results are in clang-browser-executable-path-results.json. See the
[executable-path reproduction and optional browser steps](../src/aux/clang/test/executable-path/README.md).
The next slice should address actual link/symlink providers and resolution,
then the remaining required Path boundaries. Blocking retries must reuse the
engine's existing readiness, clocks and explicit yield/resume path used by sleep.

## Symbolic-link providers and traversal slice

- Added public shared-libc symlink/symlinkat wrappers over
  waste_kernel.symlinkat_v1. The engine bounds and validates both copied
  strings before mutation, owns the target bytes, records the target length,
  rejects duplicate destinations and returns negative errno for libc translation.
  Failed target allocation cannot leave an incomplete node. Only the two
  implemented declarations lose their unavailable annotations; hard links and
  readlinkat remain unavailable. No host syscall, browser link registry,
  compiler filesystem or transport-format change is added.
- Path operations and loader snapshots now use one component resolver.
  Relative targets start at their parent, intermediate links resolve before
  later .. components, absolute inputs ignore dirfd and relative inputs use the
  retained directory description. Missing parents, non-directory traversal,
  loops, trailing slashes and final no-follow operations are checked explicitly.
  Dangling links remain inspectable and open with creation can create their
  final referent. Mount/seeding helpers keep lexical normalization. Bare language
  kernels synthesize /tmp so private guest fixtures have a real parent.
- Rename replacement now removes the destination through the existing retained
  file/descriptor compaction path, re-finds the source and changes its pathname.
  It no longer copies the old source name over the destination or leaves a hole;
  same-entry rename preserves the link. Full directory-subtree rename remains
  pending. Existing empty-span error codes for the at-style interfaces are preserved.
- Strict traversal exposed shared-memory backing names without ordinary parent
  directories. Public paths stay strict; shared-memory objects reuse the common
  validated descriptor constructor directly. Their namespace, credentials,
  flags, retained file ownership and offset-independent mapping behavior remain
  shared kernel facilities.
- Added src/aux/clang/test/symlink with an ordinary-CRT C++ observer and
  two authored WAST files. All 189 functional assertions live in WAST: C++
  declaration/ABI and wrapper calls, errno, configured pathname bounds,
  relative/intermediate/dangling links, loops, null/invalid/unterminated pointers,
  readlink payload/truncation/canaries, stat/lstat, dirfds, O_EXCL/O_NOFOLLOW,
  chmod/chdir, rename/unlink and Bash execution through final and directory links.
  This fixture does not extract or execute LLVM's full Path methods and does
  not establish the pending C++ static-exit registry.
- The actual one-job guest build gets past symlink and fails at
  Unix/Path.inc:475 on unavailable link. Fresh full Path.cpp compilation with
  generated guest target flags, no PCH and all diagnostics reports 27 errors:
  hard links, reentrant account lookup, mapping advice, realpath, record locks
  and usleep. Required Path stays selected; native LLVM sources and the 26-file
  repository patch are unchanged. Full Support, Clang/LLD linking and installation
  remain pending.

- Native and ASan/UBSan Bash each pass 189/189, exit zero and have no timeout
  or handler failures. The rebuilt packaged production worker passes 190/190,
  including Bash launch, with empty stderr. The 2,670-byte C++ observer verifies
  nine libc imports. The existing executable-path/fork/exec observer also passes
  164/164 natively and under sanitizers, and 165/165 in the same packaged worker.
  No GUI or manual browser test was required.
- Retained kernel C checks pass 265/265; private path-VFS checks pass 64/64,
  and its native/sanitized WAST gate passes. Descriptor-flags, directory-umask
  and shared-memory sanitized batch regressions pass. Process-group (71) and
  terminal-descriptor (120) checks pass. The broader posix-kernel target's
  previously recorded standalone pipe-descriptors wait failure remains a
  separate follow-up; this slice does not report that complete target as passing.
- SDK checks pass: 60 headers, six include orders, 809 available functions,
  145 unavailable functions and 31 globals, with Wasm signatures verified.
  Source/native-generator/C++-archive/SDK and fixture targets do no compilation
  after warming their prerequisites. C++ archives remain byte-identical at
  905,086/423,762 bytes; shared libc is 91,453 bytes. The existing static-exit
  limitation remains; no no-op registration shim or new compiler-local wait
  mechanism is added.
- Refreshed/audited snapshots contain 354 entries, 281 supported, 99 installed
  files and 581 VFS nodes. The rebuilt offline page contains 498 files, is
  4,615,460 bytes and embeds a 3,353,212-byte compressed archive. Both new WAST
  snapshots, public unistd.h, both libc copies and the browser engine match
  their packaged bytes. The normalized manifest digest is
  f0ac27d85bdad77a3ca531002e762392bc1f596ae305c80922f54a18fdf059fa.
  WAST parsing, shell/Python syntax, JSON and whitespace checks pass; LLVM's
  submodule remains clean.

The actual build log is build/aux/clang/symlink-build.log, also copied to
guest-build.log and build/engine/logs/clang-symlink-build.log. Full Path commands
and diagnostics, SDK, snapshots, runtime and cache logs use clang-symlink-*
under build/engine/logs. Aggregate evidence is clang-symlink-verification.json;
worker reports use clang-browser-symlink-*. See the
[symbolic-link reproduction and optional browser steps](../src/aux/clang/test/symlink/README.md).
The next slice should implement hard links with shared inode/file lifetime,
metadata and link-count observations. Preserve the current stat ABI deliberately
when extending its boundary. Any eventual blocking locks or retries must reuse
existing engine readiness, clocks and the explicit yield/resume path used by sleep.


## Hard-link inode ownership and stat versioning slice

- Added shared-libc link/linkat over waste_kernel.linkat_v1, validating both
  bounded strings before mutation. Relative inputs use their respective dirfds;
  absolute inputs ignore them. Flags zero link the final symlink itself, while
  AT_SYMLINK_FOLLOW resolves its target. Missing/non-directory parents, existing
  destinations, directory hard links, loops, trailing slashes and unsupported
  flags return explicit errno. Only implemented link loses its unavailable
  annotation/policy; linkat's former unsupported wrapper now has a real provider.
- Regular-file and symlink names retain shared inode objects. Contents, mode,
  ownership, timestamps and target bytes belong to those objects; chmod/write/
  truncate are observed through every alias and by a surviving unlinked fd.
  Rename between names of one inode is a no-op. A kernel-owned inode sequence
  is retained across fork, preventing reuse after deletion or child creation.
  Directory descriptors are rebased into cloned tables so parent teardown cannot
  leave their node pointers dangling. The bounded seeding reader no longer
  reads one byte beyond its configured namespace-name limit.
- Link counts scan directory entries in the current namespace rather than using
  generic object references. The existing clone/reap model still needs immediate
  cross-process namespace visibility and deletion reconciliation. Shared fork
  offsets, removed-directory lifetime/subtree rename and distinct atime/ctime
  mutation rules remain follow-ups; no complete POSIX filesystem claim is made.
- Preserved path_stat_v1/fstatat_v1's 48-byte metadata record. New fstatat_v2
  returns the existing 128-byte public stat layout with link counts, and libc
  stat/lstat/fstatat use it. Legacy env stat/lstat/fstat report the same counts;
  fstat reads retained inode attributes even after all names are removed. Failed
  v2 queries preserve the output, and WAST checks exact sizes and canaries.
- Added src/aux/clang/test/hardlink: ordinary-CRT C++ observations and one
  authored WAST file. All 269 reported checks live in WAST: public ABI/errno,
  inode identity, shared bytes/permissions/truncation, unlink lifetime, rename,
  symbolic-link flags/targets, directory descriptors, invalid pointers and both
  stat versions. A real fork/wait verifies child alias publication at reap,
  shared writes, and distinct parent/child inode allocations. This does not
  execute full LLVM Path methods or accept C++ static-exit registration.
- The actual one-job guest build gets past link and fails at Unix/Path.inc:740
  on _SC_GETPW_R_SIZE_MAX/getpwnam_r. Fresh complete Path.cpp compilation with
  generated guest flags, no PCH and all diagnostics reports 26 errors: reentrant
  accounts, mapping advice, realpath, record locks and usleep. Required Path,
  native LLVM sources and the repository patch remain selected/unchanged.
  Full Support, Clang/LLD linking and installation remain pending.

- Native and ASan/UBSan Bash pass 269/269, exit zero, with no handler failures
  or timeouts. The rebuilt packaged production worker passes 270/270, including
  Bash launch, with empty stderr. The 3,540-byte C++ observer verifies 13 libc
  imports. Existing symlink tests pass 189/189 natively and under sanitizers;
  executable-path/fork/exec passes 164/164 in both. The same packaged worker
  passes 190/190 symlink and 165/165 executable-path checks. No GUI test was
  required.
- Private kernel checks pass 274/274, including nine opaque inode/refcount/clone
  ownership checks. Private path-VFS passes 64/64 and its native/sanitized WAST
  gate passes. Sanitized descriptor-flags, directory-umask and shared-memory
  batch regressions pass; process-group (71) and terminal-descriptor (120)
  gates pass. The previously recorded standalone pipe-descriptors wait failure
  remains a separate follow-up; the complete posix-kernel target is not claimed.
- SDK checks pass: 60 headers, six include orders, 810 available functions,
  144 unavailable functions and 31 globals, with Wasm signatures verified.
  Repeated source/native-tools/C++-archive/SDK and warmed fixture targets do no
  compilation. C++ archives remain byte-identical at 905,086/423,762 bytes;
  shared libc is 91,463 bytes. No static-exit no-op or compiler-local time/wait
  implementation is added.
- Refreshed/audited snapshots contain 355 entries, 281 supported, 100 installed
  files and 583 VFS nodes. The rebuilt offline page contains 499 files, is
  4,620,100 bytes and embeds a 3,356,693-byte compressed archive. New WAST,
  unistd.h/fcntl.h, both libc copies and browser engine match their packaged
  bytes. The normalized manifest digest is
  5c2452572e1d54edc32a57911f1927b9dcdd05006117df9c80cb4cb4f3d815a4.
  WAST parsing, shell/Python syntax, JSON and whitespace checks pass; LLVM's
  submodule remains clean.

The actual build log is build/aux/clang/hardlink-build.log, also copied to
guest-build.log and build/engine/logs/clang-hardlink-build.log. Full Path commands,
SDK, snapshots, runtime and cache logs use clang-hardlink-* under
build/engine/logs. Aggregate evidence is clang-hardlink-verification.json;
worker reports use clang-browser-hardlink-*. See the
[hard-link reproduction and optional browser steps](../src/aux/clang/test/hardlink/README.md).
The next slice should resolve the reentrant account lookup boundary using the
existing guest account data/providers. Mapping advice, realpath and record locks
follow. Eventual blocking locks/retries and usleep must reuse engine readiness,
clocks and explicit yield/resume used by sleep.

## Reentrant account lookup slice

- Added shared-libc `getpwnam_r`/`getpwuid_r` over the existing single guest
  account record. The public `struct passwd` now defines the provider layout,
  and `pwd.h` supplies C linkage and both prototypes. Lookup copies all five
  strings into caller storage without allocation or enumeration-cursor changes.
  Complete sizing precedes writes: ERANGE preserves record/buffer and clears
  the result; missing entries return zero with a null result. These returns
  preserve errno. Configured null optional fields become empty strings; null
  required pointers return EFAULT as a guest extension. Host NSS and new account
  database/enumeration behavior are outside this slice.
- Added public `sysconf` selectors and a separate libc source consuming those
  declarations. Page-size queries reuse `getpagesize`; the account buffer-size
  query returns a process-stable indeterminate limit (-1, unchanged errno),
  allowing LLVM's existing 16,384-byte fallback. Invalid selectors return
  -1/EINVAL instead of the former fabricated 1,024. No kernel/runtime hooks,
  compiler-specific account store or static-exit no-op were added.
- Added 84 package WAST assertions and `src/aux/clang/test/accounts` with
  47 compiled public C++ observations checked entirely in WAST. They cover
  exact/undersized buffers, canaries, positive error returns, independent
  results, account replacement, missing/null entries, enumeration preservation,
  optional strings, a 20,000-byte home directory, errno and the 28-byte ABI.
  This verifies provider behavior and C++ linkage, not full LLVM Path execution.
- The full libc Make runner exposed a previous staging regression: explicit
  files below `/tmp/aux-tests/...` lacked parent directories, so component
  resolution rejected their paths. The shared transport now stages files
  directly under the existing `/tmp` namespace. All 20 package files pass
  576/576 natively and with ASan/UBSan, exit zero and no handler failures or
  timeouts. The sanitizer run uses `WASTE_TEST_TIMEOUT_MS=180000`; the existing
  largest binary128 formatting case exceeded the default 60-second session
  deadline. Leak checking remains disabled in the ptrace sandbox.
- The compiled account fixture passes 47/47 in native and sanitized Bash.
  Packaged production workers pass 48/48 for C++ and 85/85 for the package,
  including Bash launch, with empty stderr. The 4,370-byte observer verifies
  19 libc imports. SDK checks pass: 60 headers, six include orders, 813 available
  functions, 144 unavailable and 31 globals, including Wasm signature checks.
- The actual one-job guest Support build passes the account lookup boundary
  and first fails at `Unix/Path.inc:969` on `madvise`/`MADV_DONTNEED`. A fresh
  complete Path compile with generated guest flags, no PCH and all diagnostics
  reports 22 errors, down from 26: mapping advice, realpath, record locks and
  usleep. Required Path/native sources and production C++ flags remain selected;
  full Support, Clang/LLD linking and installation remain pending.
- Warmed source/native-tools/C++-archive/SDK and account fixture targets perform
  no compilation. C++ archives remain byte-identical at 905,086/423,762 bytes;
  shared libc is 93,049 bytes. Refreshed/audited snapshots contain 357 entries,
  281 supported, 102 installed files and 586 VFS nodes. The offline page contains
  501 files, is 4,626,536 bytes and embeds a 3,361,521-byte compressed archive.
  Both new WAST snapshots, public pwd.h/unistd.h, both libc copies and engine
  match their packaged bytes. The normalized manifest digest is
  `d546fc4eb554acc775da9004c956a1ed524b1816c31aa13d88715f0012cc393f`.
  Parsing, shell/JSON/whitespace checks pass; LLVM's submodule remains clean.

The actual build log is `build/aux/clang/accounts-build.log`, also copied to
`guest-build.log` and `build/engine/logs/clang-accounts-build.log`. Full Path
commands, runtime, SDK, snapshots, package-byte and cache logs use
`clang-accounts-*` under `build/engine/logs`; worker reports use
`clang-browser-accounts-*`. Aggregate evidence is
`build/engine/logs/clang-accounts-verification.json`. See the
[account reproduction and optional browser steps](../src/aux/clang/test/accounts/README.md).
The next slice should resolve mapping advice, followed by realpath and descriptor
locks. Eventual blocking locks/retries and usleep must reuse the existing engine
readiness, clocks and explicit yield/resume path used by sleep.

## POSIX mapping advice slice

- Added the public `posix_madvise` API, five advice values in the shared
  `waste/abi/mman.h` header, and a shared-libc wrapper around the versioned
  engine provider. The process capsule validates alignment, length, advice,
  rounded bounds and every mapped page without reading data. PROT_NONE and
  truncated file mappings remain valid advisory targets. Positive errors leave
  errno unchanged. Hints preserve bytes, permissions, dirty state, VMAs and
  backing identity; resident/demand-zero pages need no advisory optimization.
  Linux's destructive `madvise(MADV_DONTNEED)` is not advertised. Native/browser
  builds depend on the same ABI header; no compiler-local VM implementation,
  platform syscall, time/thread subsystem or static-exit workaround was added.
- Added `src/aux/clang/test/advice` with unchanged licensed LLVM mapping
  methods extracted from staged Unix/Path.inc and Path.cpp, with source hashes.
  WAST checks real construction, three advice methods, accessors, move/unmap,
  empty-object guards and failed construction (38 checks), plus public C++
  mapping observations (161 checks). The latter cover all hints, page rounding,
  errors/errno, inaccessible pages, holes/adjacent mappings, unsigned arguments,
  private modifications, shared aliases, dirty writeback, truncation and retained
  backing after close/unlink. An import audit verifies both the libc wrapper and
  legacy env adapter have the provider's actual signature.
- Mapping operations run in an ordinary CRT guest process: an exec'd WAST
  handler has no application VM engine bound to its capsule. WAST owns every
  functional expectation and reads captured observations. The fixture reuses
  the existing real diagnostic error-category object with scoped static-
  destructor suppression; production C++ archives keep normal registration.
  This does not establish full LLVM Path execution or C++ static shutdown.
- Native and ASan/UBSan Bash pass 199/199, exit zero, with no handler failures
  or timeouts. The rebuilt production worker passes 200/200 including Bash
  launch, with empty stderr. The native lifecycle sanitizer gate passes
  120/120, including three new opaque page/refcount/VMA/protection invariants.
  No GUI browser test is required for this slice.
- The actual one-job guest Support build passes the advice branch and first
  fails at `Unix/Path.inc:1287` on realpath. Fresh full Path compilation with
  generated guest flags, no PCH and all diagnostics reports 16 errors, down
  from 22: realpath, descriptor-lock types/commands and usleep. Required Path
  sources/native branches and production C++ flags remain selected/unchanged.
  Complete Support, Clang/LLD linking and installation remain pending.
- SDK checks pass: 61 headers, six include orders, 814 available functions,
  144 unavailable and 31 globals, including Wasm signature checks. Warmed
  source/native-tools/C++-archive/SDK and mapping fixture targets perform no
  compilation. C++ archives remain byte-identical at 905,086/423,762 bytes;
  shared libc is 93,163 bytes. Refreshed/audited snapshots contain 359 entries,
  281 supported, 104 installed files and 590 VFS nodes. The offline page has
  504 files, is 4,629,292 bytes and embeds a 3,363,588-byte compressed archive.
  Both WAST snapshots, public/ABI mapping headers, both libc copies and browser
  engine match packaged bytes. The normalized manifest digest is
  `7973fcfe4ac20901e9c78acd2e88512bbbf2d5a082e874fea400f579026d8a5e`.
  Parsing, shell/Python/JSON/whitespace checks pass; LLVM remains read-only/clean.

The actual build log is `build/aux/clang/advice-build.log`, also copied to
`guest-build.log` and `build/engine/logs/clang-advice-build.log`. Full Path
commands, SDK, snapshots, runtime, import/package-byte and cache logs use
`clang-advice-*` under `build/engine/logs`; the worker report is
`clang-browser-advice-results.json`. Aggregate evidence is
`build/engine/logs/clang-advice-verification.json`. See the
[mapping reproduction and optional browser steps](../src/aux/clang/test/advice/README.md).
The next slice should implement realpath using the existing guest component
resolver and namespace, then descriptor locks and yielding usleep. Blocking
locks/retries must reuse engine readiness, clocks and explicit yield/resume
used by sleep. Full C++ exit registration remains a separate required boundary.

## Canonical path slice

- Added shared-libc `realpath` with public C/C++ linkage and the versioned
  `waste_kernel.realpath_v1` copy boundary. Resolution uses the existing kernel
  namespace/component walker, including relative cwd paths, symlinks before
  `..`, trailing-directory requirements, missing components and bounded loops.
  Search permission checks use the existing kernel credentials for each
  traversed directory. A final regular file need not be readable. No host
  filesystem query, compiler-local resolver or identity subsystem was added.
- The kernel returns canonical length or negative errno; libc supports both
  caller storage and malloc-owned output, restores errno on success and reports
  allocation failure. Input is copied before resolution/output, and writable
  output is validated as a complete range before any store. Failure leaves
  output untouched, including a copy crossing a protected page. Canonical
  storage and LLVM's private PATH_MAX use `VFS_PATH_MAX_BYTES` in `src/config.h`.
  The separate bounded input-reader limit remains intact. Public pathconf
  limits and GNU canonicalize_file_name remain unsupported; public callers can
  request an allocated result with NULL output.
- Added `src/aux/clang/test/realpath` with 213 WAST assertions covering
  direct libc/kernel calls and ordinary compiled public C++ observations.
  Cases include chained/absolute/intermediate links, errors and errno, empty/
  null/invalid/unterminated inputs, canonical bounds, exact/short/raw buffers,
  canaries, allocated-result independence, free/realloc and protected-page
  copy failures. The C++ observer uses the ordinary CRT and current archives;
  it does not execute LLVM's full real_path method or establish complete Path
  behavior. Runtime expectations live entirely in WAST.
- Native and ASan/UBSan Bash pass 213/213, exit zero, without handler failures
  or timeouts. The rebuilt production worker passes 214/214 including Bash
  launch. The warnings-as-errors native path sanitizer gate passes 90/90 and
  the existing path-vfs regression passes in native/sanitized runners. Private
  checks cover injected owner/group/other credentials, owner-bit priority,
  denied `..`/trailing traversal, root behavior, kernel isolation and invalid
  spans/capacities. Guest identity setters still do not synchronize these
  credentials. Existing symlink checks pass 189/189 natively and 190/190 in the
  worker; hard-link checks pass 269/269 natively. No GUI browser test is needed
  for this slice; optional manual reproduction is documented with the fixture.
- The actual one-job Support build passes both realpath call sites and first
  fails at `Unix/Path.inc:1363` on missing struct flock. Fresh full Path
  compilation with generated guest flags, no PCH and all diagnostics reports
  12 errors, down from 16: record-lock types/commands and usleep. Required Path
  sources, native branches and production C++ flags remain unchanged. Complete
  Support, Clang/LLD linking and installation remain pending.
- SDK checks pass: 61 headers, six include orders, 815 available functions,
  144 unavailable and 31 globals, including actual Wasm signatures. The
  3,368-byte C++ observer verifies 18 libc imports and the versioned realpath
  signature. Warmed source/native-tools/C++-archive/SDK and fixture targets
  perform no compilation. C++ archives remain byte-identical at
  905,086/423,762 bytes; shared libc is 93,393 bytes. Refreshed/audited snapshots
  contain 360 entries, 281 supported, 105 installed files and 592 VFS nodes.
  The offline page has 505 files, is 4,631,492 bytes and embeds a 3,365,236-byte
  compressed archive. The new WAST snapshot, public stdlib.h, both libc copies
  and browser engine match packaged bytes. The normalized manifest digest is
  `271604c563699ffab2da2b5032c144152e7c953b820c47ac9b37642f65c7a54b`.
  Parsing, shell/Python/JSON/whitespace checks pass; LLVM remains read-only/clean.

The actual build log is `build/aux/clang/realpath-build.log`, also copied to
`guest-build.log` and `build/engine/logs/clang-realpath-build.log`. Full Path
commands, SDK, snapshots, runtime, import/package-byte and cache logs use
`clang-realpath-*` under `build/engine/logs`; the worker report is
`clang-browser-realpath-results.json`. Aggregate evidence is
`build/engine/logs/clang-realpath-verification.json`. See the
[canonical-path reproduction and optional browser steps](../src/aux/clang/test/realpath/README.md).
The next slice should implement record-lock ownership/ranges over retained
kernel file identity, including nonblocking contention and blocking readiness,
then yielding usleep through the existing sleep/time path. Blocking locks and
retry delays must reuse engine-owned readiness, clocks and explicit yield/resume.
Full C++ exit registration remains a separate required boundary.


## Process record-lock slice

- Added shared process-owned advisory `F_GETLK`, `F_SETLK` and `F_SETLKW`
  over retained kernel inode identity. Read locks coexist; write locks conflict.
  Ranges support SEEK_SET/CUR/END, negative lengths, zero-length future bytes,
  replacement/downgrade, partial unlock and coalescing. Admission and split
  preparation are atomic; resource exhaustion returns ENOLCK. Per-inode
  records and the bounded deadlock walk use `POSIX_RECORD_LOCK_MAX` in
  `src/config.h`.
- Transactional kernel clones retain their process lock owner; an actual fork
  receives a fresh owner before publication. Exec keeps its owner. Closing any
  descriptor for an inode releases all that process's records, including a
  CLOEXEC alias when another fd survives. Exit/death also releases records.
  Locks follow hard/symbolic aliases, rename/unlink and retained descriptors;
  reuse of a pathname creates an independent inode. Ordinary I/O is advisory.
- Blocking locks retain copied normalized ranges and inode references in the
  existing kernel wait record and yield with SELECT through the existing
  process scheduler. Unlock/close/death makes the interest ready; caught
  signals deliver EINTR or restart, and process wait cycles return EDEADLK.
  A test exposed restart recomputing SEEK_CUR after a handler sought the fd;
  retries now preserve the resolved absolute range. Invalid resumed operands
  cancel retained waits. No runtime-specific lock store or new timer/scheduler
  was added; this uses the readiness/yield/resume path already used by sleep.
- Public wasm32 `struct flock` is a 16-byte record with signed 32-bit off_t
  fields. Commands/types/copy offsets share `waste/abi/fcntl.h`. Libc reads
  record pointers, integer descriptor arguments and argument-free queries
  distinctly. The fixed kernel/legacy env boundary takes a direct pointer;
  libc and the explicit variadic kernel adapter read its argument slot.
  Complete input/output validation precedes copies, including protected-page
  output. Existing import signatures remain unchanged.
- Added `src/aux/clang/test/locks` with 341 WAST assertions over direct
  adapters and ordinary public C++ process observations. The fixture compiles
  unchanged LLVM `lockFile`/`unlockFile` methods extracted from staged source,
  retains its license/hash, and links the ordinary CRT/current C++ archives.
  Cases include conflict holder/range/PID, fork noninheritance, shared readers,
  close/exec/death, inode aliases/path reuse, blocking readiness, EINTR/restart,
  deadlock, erroneous ranges/access/fds/pointers and atomic protected copies.
  Runtime expectations stay in WAST. The existing real diagnostic error-category
  object suppresses static destructor registration only for this fixture;
  production archive flags and the pending C++ shutdown boundary remain intact.
  Timed `tryLockFile` and complete LLVM Path execution remain pending.
- Native and ASan/UBSan Bash pass 341/341 with exit zero, no handler failures
  or timeouts. The production packaged worker passes 342/342 including launch,
  with empty stderr. Native warnings-as-errors kernel checks pass 297/297,
  including 23 new opaque owner/inode/wait/admission checks. Process lifecycle
  passes 120/120, raw wait checks 16/16 and the existing timed wait session
  passes 24 assertions. Private checks cover transactional teardown, copied
  interests/reference balance, configuration-derived admission/split failure,
  injected offset/refcount overflow and closed-fd revalidation. No GUI browser
  test is required; optional manual steps are documented with the fixture.
- The actual one-job guest Support build passes the record-lock declarations
  and first fails at `Unix/Path.inc:1383` on usleep. Fresh full Path compilation
  with generated guest flags, no PCH and all diagnostics reports one error,
  down from twelve. Required Path sources/native branches and production C++
  flags remain unchanged. Complete Support, Clang/LLD linking and installation
  remain pending.
- SDK checks pass: 62 headers, six include orders, 815 available functions,
  144 unavailable and 31 globals, including actual Wasm signatures. The
  15,271-byte compiled observer verifies 27 libc imports and the fcntl boundary.
  Warmed source/native-tools/C++-archive/SDK and fixture targets compile nothing.
  Archives remain byte-identical at 905,086/423,762 bytes; libc is 93,485 bytes.
  Refreshed/audited snapshots contain 361 entries, 281 supported, 106 installed
  files and 595 VFS nodes. Installed frontend configuration was explicitly
  refreshed for the new shared limit before packaging. The offline page has
  507 files, is 4,641,864 bytes and embeds a 3,373,017-byte compressed archive.
  WAST, public/ABI fcntl headers, errno.h, both libc copies, frontend scripts
  and browser engine match packaged bytes. The normalized manifest digest is
  `ee5e45862cd10c95f16edb995b02c5897a9bc22508aa52f154b2e987f95c7271`.
  Parsing, shell/Python/JSON/whitespace checks pass; LLVM remains read-only/clean.

The actual build log is `build/engine/logs/clang-locks-guest-build.log`, also
copied to `build/aux/clang/locks-build.log` and `guest-build.log`. Full Path
commands, SDK, snapshot, runtime, import/package-byte and cache logs use
`clang-locks-*` under `build/engine/logs`; the worker report is
`clang-browser-locks-results.json`. Aggregate evidence is
`build/engine/logs/clang-locks-verification.json`. See the
[record-lock reproduction and optional browser steps](../src/aux/clang/test/locks/README.md).
The next slice should provide yielding usleep through the existing sleep/time
path, verify unchanged LLVM timed tryLockFile retries and retry full Support.
Full Path, Clang/LLD and production C++ exit registration/finalization remain
required boundaries.

## Yielding usleep and timed LLVM locks slice

- Added unsigned wasm32 `useconds_t` and C/C++ public `usleep` through the
  existing shared libc time module. The wrapper converts microseconds into
  the existing timespec and calls nanosleep, which already uses the engine's
  pselect deadline, SELECT continuation and runtime deadline service. Zero
  has no effect; arguments of one million microseconds or more return EINVAL
  before conversion; caught signals propagate EINTR. Successful calls preserve
  errno. Longer intervals use nanosleep. No engine state, wait reason, platform
  import, host timer or separate scheduler was added. The legacy
  [POSIX contract](https://pubs.opengroup.org/onlinepubs/009696899/functions/usleep.html)
  supplies the argument policy.
- Added nine package WAST assertions for the actual shared provider, zero,
  upper-bound/unsigned errors, errno and live 1/1000/20000-microsecond waits.
  The complete libc package passes 585/585 in native and ASan/UBSan Bash.
  The initial 60-second aggregate sanitizer run reached its session deadline
  during the existing binary128 wide formatter; repeating with the existing
  WASTE_TEST_TIMEOUT_MS=180000 override passes all checks. No implementation
  or test expectation was weakened.
- Extended the existing test-only LLVM method extraction to include unchanged
  tryLockFile, preserving source license/hash and its real Errc dependency.
  `src/aux/clang/test/timed-locks` compiles those methods against the current
  CRT and C++ archives. Its 53 WAST assertions check public C++ types/linkage,
  immediate success, invalid fd, concurrent shared readers, zero-timeout
  contention, finite timeout despite a caught signal and successful acquisition
  after the holder releases its lock. Ordinary fork/wait processes observe
  behavior; runtime expectations stay in WAST. Live elapsed checks use minimum
  durations rather than exact wall-clock timing.
- The controlled variant supplies 25 WAST assertions over the production
  usleep provider and the existing injected-clock/session controls. Advancing
  to just before 1/1000/999999-microsecond deadlines cannot resume the guest;
  advancing onto each deadline does. A fourth wait is interrupted by SIGUSR1
  and returns -1/EINTR without advancing the clock. Native sanitizer and browser
  API sessions each pass 26/26 including Bash launch. The production packaged
  worker also passes 26/26 with the same scenario and empty stderr. Sandbox
  restrictions initially blocked Node's local metadata-reader subprocess;
  the authorized rerun outside the sandbox passed.
- Native and sanitized Bash pass 53/53 timed-lock checks with exit zero and no
  timeout or handler failure. The production packaged worker passes 63/63
  including package usleep and launch, with empty stderr. The existing
  record-lock fixture still passes 341/341 natively and 342/342 in the packaged
  worker after extending extraction. The diagnostic error-category object
  alone suppresses static destructor registration; production C++ archives
  retain their normal flags and complete shutdown remains pending.
- Retried the actual one-job guest Support build. Path.cpp now compiles, and
  a fresh complete Path compile using generated guest flags, no PCH and all
  diagnostics succeeds with zero errors. The build first stops in Process.cpp:
  Unix/Process.inc:54 has two conversions requiring timeval-shaped ru_utime
  and ru_stime, while the public rusage currently declares scalar i64 fields;
  line 436 requires public `_Exit`. A fresh complete Process compile confirms
  those three diagnostics. The current getrusage provider writes 144 zero
  bytes and reports success; the next slice must review its layout, bounds and
  accounting semantics together rather than merely changing the declaration.
  Required Path/Process sources, native branches and production C++ flags
  remain selected. Full Support, Path runtime, Clang/LLD linking and installation
  remain pending.
- SDK checks pass: 62 headers, six include orders, 816 available functions,
  144 unavailable and 31 globals. The real usleep provider has i32 -> i32 Wasm
  signature. Both installed libc copies match the 93,630-byte build. C++
  archives remain byte-identical at 905,086 and 423,762 bytes. Warmed
  source/native-tools/C++/SDK and fixture targets compile nothing.
- Refreshed/audited snapshots contain 364 entries, 281 supported, 109 installed
  files and 599 VFS nodes. The offline page has 510 files, is 4,641,984 bytes
  and embeds a 3,373,106-byte compressed archive. Package/system WAST, public
  unistd/sys/types/record-lock headers, both libc copies, browser engine and
  frontend scripts match packaged bytes. The normalized manifest digest is
  `a50664a92dbf839e851af8b48762f3169acf857a7fcfcd0cd56ff4b2e27fa461`.
  Parse, shell/Python/JSON/whitespace checks pass; LLVM remains read-only/clean.
  Manual browser testing is optional; reproduction and download instructions
  are in the fixture README.

The actual build log is `build/engine/logs/clang-usleep-guest-build.log`, also
copied to `build/aux/clang/usleep-build.log` and `guest-build.log`. Fresh complete
Path/Process commands, SDK, snapshots, import/package-byte and cache logs use
`clang-usleep-*` under `build/engine/logs`. Live worker evidence is
`clang-browser-timed-locks-results.json`; controlled worker evidence is
`clang-browser-usleep-controlled-results.json`. Aggregate evidence is
`build/engine/logs/clang-usleep-verification.json`. See the
[timed-lock and sleep reproduction steps](../src/aux/clang/test/timed-locks/README.md).
The next slice should resolve the rusage/timeval and `_Exit` boundaries,
verify actual LLVM Process behavior and retry the one-job Support build.
Production C++ exit registration/finalization and complete Clang/LLD acceptance
remain required boundaries.

## Resource-usage ABI and immediate-exit slice

- Corrected the public wasm32 rusage to two 16-byte time64 timeval timestamps
  and fourteen 32-bit long counters, totaling 88 bytes. Public resource APIs
  have C linkage and typed pointers. The selector values used by libc and
  the legacy env adapter share `waste/abi/resource.h`; runtime header
  dependencies include that ABI. Existing resource-limit storage remains a
  compatibility facade without kernel enforcement.
- Neither runtime supplies guest CPU accounting. Replaced fabricated success
  and a 144-byte zero write with -1/ENOSYS for SELF/CHILDREN and -1/EINVAL for
  invalid selectors. Both adapters leave output untouched, including null and
  overflowing pointers. The guest LLVM profile disables HAVE_GETRUSAGE so its
  unchanged GetTimeUsage fallback supplies wall time and zero CPU durations.
  No host CPU-accounting import or separate clock service was added.
- Added shared-libc `_Exit` through the existing engine `_exit` path, plus the
  legacy env._Exit alias. The public declarations are nonreturning. Existing
  process status, signal and descriptor teardown are reused; no alternate
  scheduler or termination state was introduced.
- Added `src/aux/clang/test/process`. Its public C++ observer compiles the
  complete unchanged LLVM Process.cpp and links the ordinary CRT/current C++
  archives. Static checks establish layouts and typed declarations; 55 WAST
  assertions cover actual PID/time calls, bounded wall time, LLVM's CPU fallback,
  retained limit storage and four immediate-exit children. Checks include
  low-eight-bit statuses, blocked pending signals, pipe EOF and parent survival.
  Only this diagnostic compile allows expected warnings for unavailable CPU
  usage/malloc statistics. Unreferenced crash-context methods are discarded;
  actual Process::Exit, callback/stream shutdown and C++ finalization remain
  pending. Production C++ flags and native LLVM branches are unchanged.
- Added 42 package WAST assertions over both resource-usage adapters, errno,
  canaries and invalid pointers. Refreshed the system resource probe to check
  unsupported accounting. Its broader run exposed a stale sysconf(0) assertion
  expecting the former fake value 1024; it now checks the already implemented
  invalid-selector -1/EINVAL contract. Native and ASan/UBSan Bash each pass
  627 package, 46 system-libc and 55 process assertions, with exit zero and
  no handler failures/timeouts. The aggregate sanitizer package uses the
  established 180-second override for the existing wide formatter.
- The production packaged worker passes 98/98 including package resource
  checks, process checks and Bash launch, with empty stderr. The existing
  sleep/timed-lock worker still passes 63/63 against the same package. A GUI
  browser test is optional; manual upload/run/download steps are documented
  in the fixture README.
- Retried the actual one-job Support build. Process.cpp compiles; a fresh
  complete compile using generated guest flags, no PCH and all diagnostics
  produces an 11,092-byte Wasm object with zero errors. The build first stops
  in Program.cpp. A fresh complete Program compile confirms twelve diagnostics:
  missing setsid, execv, wait4, alarm and argument-limit constants, plus the
  unavailable wait declaration and cascading wait4 diagnostics. Required
  Path/Process/Program sources remain selected. Full Support, full Path runtime,
  Clang/LLD linking and installation remain pending.
- SDK checks pass: 63 headers, six include orders, 817 available functions,
  144 unavailable and 31 globals, including actual Wasm signatures. The
  3,985-byte process observer verifies 13 libc imports. Both installed libc
  copies match the 93,708-byte build. C++ archives remain byte-identical at
  905,086 and 423,762 bytes. Warmed source/native-tools/C++/SDK and process
  fixture targets compile nothing.
- Refreshed/audited snapshots contain 366 entries, 281 supported, 111 installed
  files and 603 VFS nodes. The offline page has 513 files, is 4,645,376 bytes
  and embeds a 3,375,651-byte compressed archive. New/changed WAST, public
  resource/stdlib/unistd headers, the resource ABI, both libc copies, browser
  engine and frontend scripts match packaged bytes. The normalized manifest
  digest is `5e3048afe411dce9ae88006227052c2d1a033c6b970ce95b9ad90244304bd97e`.
  Parse, shell/Python/JSON/
  whitespace checks pass; LLVM remains read-only/clean.

The actual build log is `build/engine/logs/clang-process-guest-build.log`, also
copied to `build/aux/clang/process-build.log` and `guest-build.log`. Fresh
Process/Program commands, SDK, snapshot, import/package-byte and cache logs
use `clang-process-*` under `build/engine/logs`. Worker evidence is
`clang-browser-process-results.json` and
`clang-browser-process-timed-locks-results.json`. Aggregate evidence is
`build/engine/logs/clang-process-verification.json`. See the
[process reproduction and optional browser steps](../src/aux/clang/test/process/README.md).
The next slice should resolve public execv/waiting through the existing
execve/environment and waitpid paths and verify actual LLVM child execution.
Audit optional child statistics/detachment honestly; subsequent timeout and
argument-limit work must reuse shared signals, clocks, readiness and limits.
Full Process/crash-context handling and production C++ shutdown remain
required acceptance boundaries.

## Public exec/wait and safe status-output slice

- Added shared-libc execv and wait with public C-linkage declarations, including
  environ. execv calls existing execve with the current environment and an exact
  path; wait calls the existing waitpid boundary with PID -1 and options zero.
  There is no new process table, scheduler or runtime-specific implementation.
  Provider policy now marks wait as implemented and describes the remaining
  process capabilities explicitly.
- Invalid exec path/vector pointers return -1/EFAULT instead of confusing
  inaccessible vector slots with argument-budget exhaustion. Existing E2BIG
  capacity handling remains separate. Failed replacement preserves the image
  and environment. Successful children verify copied argv, inherited/empty/
  explicit environments, relative paths without PATH search, stable PID,
  inherited descriptors and close-on-exec behavior.
- Fixed waitpid consuming a child before discovering an invalid status output.
  A bounded engine-memory preparation checks the entire write range and its
  permissions, then allocates lazy backing and detaches copy-on-write pages
  before reaping. Bytes and dirty flags remain unchanged during preparation.
  Resumed waits revalidate the integer address; no guest pointer survives a
  yield. NULL status is legal, and write-only fresh pages work. Both invalid
  final-copy paths consistently return -1/EFAULT with guest errno.
- Added `src/aux/clang/test/exec-wait`. Its C++ observer uses public headers,
  the ordinary CRT and current C++ archives; all 234 behavioral assertions live
  in WAST. Besides replacement, it covers any-child ordering, WNOHANG,
  interruption/restart, single consumption, parent survival and invalid,
  protected, cross-page and unmapped status destinations. Pipe EOF establishes
  a zombie before failure checks, followed by a successful wait for that child.
  Signal handling and microsecond waits reuse the existing signal/sleep paths.
  This fixture executes public APIs, not actual LLVM Program methods.
- Native and ASan/UBSan Bash each pass 234 exec/wait, 627 package-libc and
  55 existing Process assertions, with exit zero and no handler failures or
  timeouts. Private sanitizer guards pass 120 process-lifecycle and 62
  exec-transition checks. The aggregate libc sanitizer uses the established
  180-second override for the existing wide formatter.
- The refreshed production packaged worker passes 235/235 exec/wait checks
  including Bash launch. Existing Process/resource-usage acceptance passes
  98/98 with both live time and a scripted submicrosecond clock sample, all
  with empty stderr. Its initial live-time failure exposed an overstrict
  comparison between nanosecond clock samples and microsecond-rounded libc++
  wall time. The WAST lower bound now floors to the statically verified
  system_clock resolution; the upper bound is unchanged. A GUI browser test
  is optional; the fixture README provides upload/run/download instructions.
- Retried the actual one-job Support build and a fresh complete Program.cpp
  compile with generated flags, no PCH and unlimited diagnostics. Missing
  execv and unavailable wait errors are gone: twelve diagnostics become nine
  over setsid, wait4, alarm and argument-limit constants, including cascading
  wait4 errors. Required Path/Process/Program sources remain selected; upstream
  sources and native LLVM branches are unchanged. Full Support, actual LLVM
  Program execution/waiting, Path/Process/crash-context acceptance, C++ shutdown,
  Clang/LLD linking and installation remain pending.
- The wider i32-smoke Make gate passes its standalone sanitizer executable
  but stops in `tests/engine-regressions/pipe-descriptors.wast`: the raw blocking
  read yields without a runtime terminal context. A diagnostic build bypassing
  this slice's exec/wait adapter changes gives the same failure. No production
  source rollback or weaker assertion was used. This gate remains a separate
  readiness/fixture acceptance follow-up and is not recorded as passing.
- SDK checks pass: 63 headers, six include orders, 819 available functions,
  143 unavailable and 31 globals, with actual Wasm signatures. The 10,154-byte
  exec/wait observer verifies 24 libc imports. Both installed libc copies match
  the 93,814-byte build; C++ archives remain byte-identical at 905,086 and
  423,762 bytes. Warmed source/native-tools/C++/SDK and exec/wait fixture targets
  compile nothing.
- Refreshed/audited snapshots contain 367 entries, 281 supported, 112 installed
  files and 605 VFS nodes. The page contains 514 files, is 4,649,804 bytes and
  embeds a 3,378,971-byte compressed archive. New/changed WAST, public wait/
  unistd headers, both libc copies, browser engine and frontend match packaged
  bytes. The normalized manifest digest is
  `64430d5fbdc5ee880d49d686298e9f467dcfb5d2bd1c61cbde3cdc706eab778a`.
  Parse, shell/Python/JSON/whitespace checks pass; LLVM remains read-only/clean.

Aggregate evidence is `build/engine/logs/clang-exec-wait-verification.json`.
Actual build evidence is `clang-exec-wait-guest-build.log`, also copied to
`build/aux/clang/exec-wait-build.log` and `guest-build.log`. Fresh compile,
native/sanitizer, SDK, corpus, cache and package-byte evidence uses
`clang-exec-wait-*` under `build/engine/logs`. Worker reports use
`clang-browser-exec-wait*-results.json`; the initial timing failure is retained
separately. The raw-pipe comparison is `clang-exec-wait-smoke-baseline.json`.
See the [exec/wait reproduction and optional browser steps](../src/aux/clang/test/exec-wait/README.md).

The next slice should resolve LLVM Program's child-wait/statistics selection
through the existing waitpid path and verify actual LLVM calls. Its Unix code
passes rusage even when ProcStat is absent; do not add a successful wait4 that
fabricates CPU usage. Keep requested-but-unavailable statistics explicit and
preserve native behavior. Session/detachment, alarm timeouts and argument-limit
queries remain separate boundaries using shared process, signal, clock,
readiness and configuration state. Full Process/crash-context handling and
production C++ startup/shutdown remain required acceptance work.

## Actual LLVM child-wait selection slice

- Added a guest-only Program adaptation that selects existing waitpid for
  indefinite waits and zero-time polling. LLVM's EINTR retry, exit-status
  interpretation, 126/127 execution errors, signal result and Unix error strings
  remain its actual code. The shared C engine still owns process selection,
  readiness, signal delivery and reaping; no compiler scheduler or new runtime
  import was introduced. Native LLVM retains its wait4/alarm path.
- Audited optional statistics and timeouts at both call boundaries. Requested
  statistics reset the output and return ENOSYS without a fabricated usage
  record. Positive timeouts also return ENOSYS while their shared deadline/
  signal implementation remains pending. Wait rejects both before reaping,
  killing or changing SIGALRM state; ExecuteAndWait checks them before fork so
  a rejected request cannot launch a child. The shared guest-only helper is
  tracked by the compiler build's dependency stamp.
- Added `src/aux/clang/test/program-wait`. Its source extractor preserves
  the actual staged Wait definition, ProcessInfo constructor and Unix error
  helper, with upstream license, checked unique/ordered boundaries and source/
  generated hashes. The full unchanged Errno.cpp supplies real error strings.
  No extracted fragment replaces production Program.cpp. The fixture does not
  compile the whole translation unit or execute ExecuteAndWait/ExecuteNoWait;
  their runtime acceptance remains pending. ExecuteAndWait's preflight was
  source/compile reviewed, not executed by this observer.
- All 282 behavioral expectations live in WAST. Native and ASan/UBSan Bash
  each pass 282/282 with exit zero and no handler failures/timeouts. Checks cover
  five exit statuses, full error strings, ECHILD/single consumption, zero-time
  polling, unchanged SIGALRM disposition, SIGTERM and indefinite interruption
  with/without SA_RESTART. Statistics and finite-wait rejection leave a blocked
  child alive; releasing it and waiting later succeeds. The real signal sender
  uses the existing yielding usleep provider.
- The production packaged worker passes 283/283 including Bash launch, with
  empty stderr. Its page uses the current engine, shared libc and frontend;
  no alternate process backend is supplied. A GUI test is optional, with
  upload/run/download steps in the fixture README.
- Retried the actual one-job Support build. It first stops in Program.cpp on
  setsid and the two argument-limit constants. A fresh whole-source guest
  compile with generated flags, no PCH and unlimited diagnostics confirms
  nine diagnostics become three. This reduction selects a supported status-only
  path; it does not establish positive timeout support. A fresh complete native
  Program.cpp compile has zero errors and produces a 32,240-byte object.
  Required Path/Process/Program sources remain selected. Full Support,
  actual LLVM execution methods, full Path/Process/crash-context handling,
  C++ startup/shutdown and Clang/LLD linking/install remain pending.
- The initial fixture invocation overlapped the initial configure. Both
  completed; subsequent builds used one CMake owner and one LLVM job. Fresh
  guest/native compiles and warm-cache checks verified the resulting build.
  Warmed source/native-tools/C++/SDK and fixture targets compile nothing.
  The C++ archives remain byte-identical at 905,086 and 423,762 bytes; installed
  libc remains byte-identical at 93,814 bytes. The 13,283-byte observer verifies
  18 libc imports. Extraction identity and changed-boundary rejection checks
  pass, and LLVM's read-only checkout remains clean.
- SDK checks remain green: 63 headers, six include orders, 819 available
  functions, 143 unavailable and 31 globals, including actual Wasm signatures.
  Refreshed/audited snapshots contain 368 entries, 281 supported, 113 installed
  files and 607 VFS nodes. The offline page has 515 files, is 4,650,104 bytes
  and embeds a 3,379,196-byte compressed archive. New WAST, existing exec/process
  snapshots, public headers, both libc copies, engine and frontend match
  packaged bytes. The normalized manifest digest is
  `6029f5aa9562aaec318da84866ceea9d696c96e00c2f2282f9fa51c76ff0206a`.
  Parse, shell/Python/JSON/whitespace checks pass.
- The previously recorded standalone raw blocking-pipe smoke failure remains
  a separate readiness/fixture acceptance follow-up. This slice changes no
  engine read behavior and does not claim that wider gate passes.

Aggregate evidence is `build/engine/logs/clang-program-wait-verification.json`.
The actual build log is `clang-program-wait-guest-build.log`, also copied to
`build/aux/clang/program-wait-build.log` and `guest-build.log`. Fresh whole-source
guest/native commands and logs, native/sanitizer sessions, extraction, SDK,
snapshot, package-byte and cache evidence use `clang-program-wait-*` under
`build/engine/logs`. Worker evidence is `clang-browser-program-wait-results.json`.
See the [actual LLVM Wait reproduction and optional browser steps](../src/aux/clang/test/program-wait/README.md).

The next slice should implement real argument-budget queries and LLVM's
argument-fit selection. Derive coordinated quantities from src/config.h,
account for argv/environment counts and bytes, and reject excess before
replacing an image. Session/detachment then remains the other immediate
Program.cpp compile boundary. Positive waits still require existing shared
signal/deadline state; requested child statistics stay explicitly unsupported.
Actual LLVM execution-method and complete compiler/linker acceptance remain
required after the translation unit links.

## Shared exec budgets and actual LLVM argument-fit slice

- Moved argv/environment counts into src/config.h: 127 arguments and 255
  environment entries, excluding NULL sentinels. Added a 128 KiB combined
  string budget including every terminating NUL. Pathname and vector storage
  are separate; the existing 16 MiB executable-file cap is unchanged. Initial
  host-vector copying, guest exec copying, executable preflight and WAT
  interpreter expansion now enforce the same budgets before image commitment.
  Exact host-launch boundaries were not separately probed; the runtime fixture
  checks guest exec and interpreter expansion.
- Removed the accidental 256-byte pathname cap on individual argument and
  environment strings. The guest copier shares a remaining string budget
  across both vectors and records ownership after each element, so later
  EFAULT/E2BIG failures free partial copies. Rejected interpreter expansion
  preserves E2BIG through exec resume rather than replacing it with ENOEXEC.
  Tests verify that rejected exec retains the old image and its close-on-exec
  descriptors; they do not claim leak-detector coverage.
- Added the bounded versioned `waste_kernel.exec_limits_v1` query and public
  `waste_get_exec_limits`. Three little-endian u32 fields expose actual argument,
  environment and byte maxima. Wrong output sizes return EINVAL; null/bad ranges
  return EFAULT without changing canaries. Public `sysconf(_SC_ARG_MAX)` derives
  its value from this query and preserves errno on success. Added real public
  selectors/minimum and C++ declaration/layout checks. The existing invalid
  sysconf test now uses -1 because selector zero is implemented.
- Selected both actual LLVM argument-fit overloads through one guest helper,
  using current environment entries and string lengths rather than a host
  ARG_MAX or arbitrary half-budget. Args already includes argv[0]; Program is
  a separate pathname. The const-char overload also avoids its intermediate
  SmallVector allocation. Native LLVM retains its original calculation.
  A previous successful fit is advisory: an enlarged environment still fails
  at the authoritative exec boundary.
- Added `src/aux/clang/test/argument-limits`. Its extractor preserves both
  actual staged overloads and upstream license, verifies unique/ordered and EOF
  boundaries, and records source/generated hashes. Identity, duplicate-boundary
  and trailing-source rejection checks pass. No extracted fragment replaces
  production sources. The fixture does not establish complete Program.cpp or
  actual LLVM ExecuteAndWait/ExecuteNoWait acceptance.
- All 186 runtime expectations live in WAST. Native and ASan/UBSan Bash pass
  186/186 with exit zero and no handler failures/timeouts. Cases include a
  valid 1024-byte argument, exact/excess argument/environment counts, exact/
  excess combined bytes, empty-string NUL accounting, changed environment,
  partial-copy invalid pointers, query canaries and WAT interpreter expansion.
  Boundary inputs derive from the running kernel rather than copied budgets.
  The production packaged worker passes 187/187 including Bash launch, with
  empty stderr. A GUI test is optional; upload/run/download steps are in the
  fixture README.
- Retried actual Support with one CMake owner and CLANG_JOBS=1. A fresh whole
  Program.cpp guest compile with generated flags, no PCH and unlimited
  diagnostics confirms three diagnostics become one: setsid. A fresh complete
  native compile has zero errors and produces a 32,240-byte object. Required
  Path/Process/Program sources remain selected. Session/detachment, actual LLVM
  execution methods, positive waits, full Path/Process/crash-context handling,
  C++ startup/shutdown and compiler/linker acceptance remain pending.
- Retained regression checks pass: 627 package-libc sanitizer assertions,
  46 system-libc native assertions, 234 public exec/wait sanitizer assertions,
  283 actual LLVM Wait packaged-worker checks, and 120 lifecycle/62 exec-matrix
  private sanitizer checks. The previously recorded standalone raw blocking-pipe
  smoke failure remains a separate readiness/fixture follow-up; the wider
  i32-smoke gate is not claimed green.
- SDK checks pass for 63 headers, six include orders, 820 available functions,
  143 unavailable and 31 globals with actual Wasm signatures. Refreshed/audited
  snapshots contain 369 entries, 281 batch-supported, 114 installed files and
  609 VFS nodes. This fixture requires Bash context, so it joins the installed
  shell tests rather than increasing direct batch support. Explicitly refreshed
  the installed frontend to include the new generated configuration before
  worker verification; packaging alone preserves installed files.
- The offline page has 516 files, is 4,656,824 bytes and embeds a 3,384,235-byte
  compressed archive. New WAST, existing exec/wait/process snapshots, public
  headers, both libc copies, engine and frontend match packaged bytes. The
  normalized manifest digest is
  `45b9c65c1d9a789354fc1224bd29ae6349cc492f80f06a0ab79d65b94f7ecc82`.
  The 7,951-byte observer verifies 24 libc imports. Built/installed libc copies
  match at 94,038 bytes. C++ archives remain byte-identical at 905,086 and
  423,762 bytes; warm source/native/C++/SDK and fixture targets compile nothing.
  LLVM's read-only checkout remains clean. Parse, shell/Python/JSON and
  whitespace checks pass.

Aggregate evidence is `build/engine/logs/clang-argument-limits-verification.json`.
Build, fresh whole-source guest/native commands, sessions, extraction, SDK,
snapshot, package-byte, cache and final-check evidence use
`clang-argument-limits-*` under `build/engine/logs`. Worker evidence is
`clang-browser-argument-limits-results.json`. See the
[argument-budget reproduction and optional browser steps](../src/aux/clang/test/argument-limits/README.md).

The next slice should resolve session/detachment using existing engine-owned
process/session/terminal state, then link and exercise actual LLVM child
execution. Preserve native behavior and reject unavailable options before
fork. Positive waits must use the existing signal/deadline scheduler;
requested child statistics stay explicitly unsupported. Full compiler/linker
startup, execution and shutdown acceptance remains required.

## Shared sessions and controlling-terminal detachment slice

- Added SID and controlling-terminal ownership to the existing process kernel;
  terminal OFDs retain their owning SID alongside the shared foreground group.
  Bootstrap terminal attachment establishes ownership before publication.
  Fork inherits process state and shares terminal aliases; exec retains it.
  No compiler-specific process table, scheduler or runtime syscall path was added.
- Added allocation-free shared setsid validation across allocated process groups.
  Group/session leaders and another allocated group matching the caller's PID
  return EPERM before any mutation. Success sets SID/PGID to the caller's PID
  and clears controlling-terminal ownership without closing descriptors or
  changing the parent's device/session/foreground state.
- Added public libc setsid/getsid/getpgid/getpgrp through versioned shared kernel
  boundaries. PID zero queries the caller; missing/negative PIDs return ESRCH,
  zombies remain queryable until reaped, and successful calls preserve errno.
  Public tcgetpgrp now queries actual controlling-terminal ownership instead of
  returning constant one: invalid/closed descriptors return EBADF, while streams
  and detached terminal aliases return ENOTTY. Physical isatty, ordinary I/O,
  termios and descriptor flags are independent of detachment.
- Preserved legacy raw env group/terminal return conventions while rejecting
  cross-session mutations and joins to another session's existing group.
  Queued terminal interrupts require matching controlling-session ownership.
  Host foreground signals use the terminal's SID even when the active process
  is detached and has closed all its aliases, preventing delivery through an
  artificial group-ID collision. Routing retains integer identity before signal
  delivery, which can destroy the device's final reference.
  Full POSIX setpgid/job-control, terminal acquisition, session-leader exit
  hangups and orphan-group stop/continue are not established by this slice.
- Added `src/aux/clang/test/session` with an ordinary compiled C++ observer,
  public declaration/type checks and all 86 runtime expectations in WAST.
  Native and ASan/UBSan Bash pass 86/86, exit zero and report no handler errors
  or timeouts. Nested fork/exec, retained aliases, repeat rejection, parent
  ownership and zombie/reaped identities pass. The production packaged worker
  passes 87/87 including Bash launch with empty stderr. No manual browser check
  is required; optional upload/run/download instructions are in its README.
- Private sanitizer lifecycle guards now pass 132 checks, including terminal
  refcounts, host/queued signal routing, closed detached aliases, last-device
  destruction and atomic group-collision rejection. The lifecycle gate reuses
  the same cached sanitizer engine objects as the production native commands.
  Native and packaged-worker process-group regressions pass 71 checks each;
  terminal-descriptor regressions pass 120 each. The private kernel gate passes
  297 checks and the exec-transition matrix passes 62. Existing public exec/wait
  and actual LLVM Wait sanitizer fixtures
  pass 234 and 282 assertions; package libc passes 627 native assertions.
  The previously recorded standalone raw blocking-pipe i32-smoke failure remains
  separate and that complete gate is not claimed green.
- Retried actual Support with one CMake owner and CLANG_JOBS=1. Program.cpp now
  compiles in the real target. Fresh complete guest/native compiles, using
  generated flags without PCH, report zero errors and produce 15,999/32,240-byte
  objects. Support first stops at Signals.cpp with fourteen diagnostics over
  siginfo_t, sa_sigaction, advanced SA_* flags, SI_QUEUE and Dl_info. Required
  Program/Path/Process sources remain selected. Object compilation and public
  session fixtures do not establish actual LLVM ExecuteAndWait/ExecuteNoWait,
  full Support linking, finite waits or compiler startup/shutdown.
- SDK checks pass for 63 headers, six include orders, 824 available functions,
  143 unavailable and 31 globals with actual Wasm signatures. Refreshed/audited
  snapshots contain 370 entries, 281 batch-supported, 115 installed files and
  611 VFS nodes. The session fixture requires Bash context. Its 6,284-byte
  observer verifies 19 libc imports; built and both installed libc copies match
  at 94,531 bytes. Current C++ archives remain byte-identical at 905,086 and
  423,762 bytes. Repeated source/native/C++/SDK and fixture builds compile nothing.
- The final offline page contains 517 files, is 4,657,948 bytes and embeds a
  3,385,079-byte compressed archive. New session and retained process tests,
  public headers, libc copies, engine and frontend match packaged bytes.
  The normalized manifest digest is
  `eefc713e24bd1a195ca629eb6a67a77dcc25a1b5e461e97c6fd41adddea15395`.
  WAST parse, shell/JSON/whitespace checks and the clean read-only LLVM checkout
  are verified. No upstream checkout was modified.

Aggregate evidence is `build/engine/logs/clang-session-verification.json`.
Build, whole-source guest/native commands, native/sanitizer sessions, SDK,
snapshot/package bytes, caches and final guards use `clang-session-*` under
`build/engine/logs`; worker evidence is `clang-browser-session-results.json`.
See the [session reproduction and optional browser steps](../src/aux/clang/test/session/README.md).

The next slice should audit LLVM Signals.cpp against the existing shared signal
and dynamic-loading capabilities, preserving native branches and reporting
unsupported signal-info/alternate-stack requests honestly. Then link and exercise
actual LLVM execution methods. Positive waits must reuse the existing signal,
clock and readiness scheduler; requested child statistics remain unsupported.
Full Clang/LLD resource installation, execution and C++ startup/shutdown acceptance
remain required.


## Ordinary LLVM signals and public registration slice

- Adapted complete guest Signals.cpp to the existing engine-owned one-argument
  signal ABI, retaining native siginfo/fault-context branches. Guest handlers use
  flags zero rather than inventing SA_SIGINFO, alternate stacks or fault records.
  Guest machine backtraces are disabled; real dlopen support remains selected.
  The SIGINFO/SIGUSR1 alias is registered once. Utility registration saves only
  successfully installed, nonignored actions so unregister cannot replace an
  ignored disposition with a default action.
- Replaced libc's discarded signal() registration with a persistent SA_RESTART
  action and a returned predecessor. Public sigaction uses the fixed 24-byte
  waste_kernel.sigaction_v1 boundary and translates negative errors into
  -1/errno. Unknown flags and SIG_ERR handlers return EINVAL before output/state
  mutation; invalid ranges return EFAULT. Aliased input/output is decoded first.
  Legacy table-free WAT retains its 20-byte prefix and raw return conventions;
  versioned queries normalize legacy ignored sentinel -2 to public SIG_IGN one.
- Runtime tests exposed two shared signal defects. Forked applications keep
  cloned evaluators without borrowing executable-image descriptors, so delivery
  now uses the existing inherited is_application flag. A self-signal queued while
  its handler masks it must run after mask restoration, before guest code resumes.
  The common delivery path drains pending signals iteratively, propagates exits
  and keeps any nonrestart decision. Raw language probes retain their existing
  one-notification-per-boundary behavior. No compiler-local scheduler, platform
  timer or process table was added.
- Added src/aux/clang/test/signals. The checked extractor preserves actual
  ordinary registration/dispatch, file-list cleanup and RunSignalHandlers source,
  upstream license and hashes; identity and duplicate-boundary rejection checks
  pass without changing output on failure. It links real ManagedStatic.cpp,
  current C++ archives and normal CRT. An observation bridge calls the actual
  private utility-registration path. Callback insertion/fatal-error dependencies,
  crash recovery, symbolication and complete Support linking are outside this
  diagnostic fixture; no substitutes provide them.
- All 136 runtime expectations live in WAST. Native and ASan/UBSan Bash each
  pass 136/136 with exit zero, no handler failures and no timeout. Actual LLVM
  info callbacks persist and preserve errno; interrupt/pipe callbacks are
  one-shot. Cleanup checks erased/reused list slots, retained cancelled names,
  regular-only deletion and direct interrupt cleanup. Utility dispositions,
  explicit termination, pipe exit status, parent survival and reaped child
  identities pass. Table-free tests check both layouts, error atomicity and
  ignored-sentinel normalization. The production packaged worker passes 137/137
  including Bash launch, with empty stderr. No manual browser test is required;
  optional upload/run/download steps are in the fixture README.
- Retried actual Support with one LLVM build job. Complete Signals.cpp now
  compiles in that target. Fresh complete guest/native compiles with generated
  flags and no PCH have zero diagnostics, producing 36,213-/83,072-byte objects.
  Guest ENABLE_BACKTRACES is zero, native remains one and guest HAVE_DLOPEN stays
  one. Support now stops in Watchdog.cpp on two undeclared alarm calls.
  Required Path, Process, Program and Signals sources remain selected. Complete
  Support, actual LLVM child execution and Clang/LLD acceptance remain pending.
- Retained checks pass: 627 package-libc and 46 system-libc sanitizer assertions,
  234 public exec/wait and 282 actual LLVM Wait sanitizer assertions, and 96
  compiled checkpoint plus eight copyable-jump assertions. The existing wide
  formatter reaches the default 60-second aggregate sanitizer limit; the
  established WASTE_TEST_TIMEOUT_MS=180000 override passes all package checks.
  Private guards pass 132 lifecycle, 62 exec-transition and 42 signal tests.
  The signal-mask regression passes in native and sanitizer suite contexts.
  Native and packaged-worker process groups pass 71 each; terminal descriptors
  pass 120 each. The previously recorded raw blocking-pipe i32-smoke issue
  remains separate; the full smoke gate is not claimed green.
- SDK checks pass for 63 headers, six include orders, 824 available functions,
  143 unavailable and 31 globals with actual Wasm signatures. Explicitly
  refreshed/audited snapshots contain 371 entries, 281 batch-supported, 116
  installed files and 613 VFS nodes. This fixture requires Bash context.
  Its 15,059-byte observer verifies 22 libc imports; built and both installed
  libc copies match at 94,803 bytes. C++ archives remain byte-identical at
  905,086 and 423,762 bytes. Repeated source/native/C++/SDK and fixture targets
  compile nothing.
- The offline page contains 518 files, is 4,663,308 bytes and embeds a
  3,389,099-byte compressed archive. New signal and retained process snapshots,
  public headers, both libc copies, engine and frontend match packaged bytes.
  The normalized manifest digest is
  cc5b5e64c8407d55a2e1fde310589fadae7525875bb9fb7ffc23b7d573151759.
  WAST parse, shell/Python/JSON/whitespace checks and the read-only LLVM checkout
  are verified. No upstream source was modified.

Aggregate evidence is build/engine/logs/clang-signals-verification.json.
Actual build, whole-source guest/native commands, fixture/source extraction,
SDK, native/sanitizer sessions, retained guards, snapshots/package bytes, caches
and final checks use clang-signals-* under build/engine/logs. Main worker
results are clang-browser-signals-results.json. See the
[signal reproduction and optional browser steps](../src/aux/clang/test/signals/README.md).

The alarm/watchdog prerequisite is completed in the following slice. Actual
LLVM execution methods, finite child waits and complete compiler acceptance
remain required.

## Shared process alarms and complete Support archive slice

- Replaced the raw alarm no-op with one process-owned monotonic deadline in the
  existing kernel. Public libc uses waste_kernel.alarm_v1 with unsigned i32
  seconds and an i64 remaining/error result, retaining UINT_MAX without confusing
  it with errno. Success preserves errno; clockless private sandboxes return
  ENOSYS. Replacement and cancellation round fractional remaining seconds up.
  Expiry queues SIGALRM once through ordinary masks/dispositions. Cancelling an
  expired timer preserves its pending signal. Deadline addition saturates at
  UINT64_MAX, matching existing finite waits.
- Reused the minimum-deadline scheduler and native poll/browser wake-up boundary,
  including READ, waitpid and alarms belonging to another live process. The
  timer is independent of a syscall wait. Fork clears only the child's alarm;
  generic transactional clones and exec retain it. No compiler-local timer,
  host SIGALRM, extra scheduler or Asyncify path was added.
- Caught signals during CPU work now use bounded opcode checkpoints and the
  existing synchronous handler path with live evaluator frames. Saved blocking
  call chains retain their import's EINTR/restart decision. The callback is
  installed by the process driver, preserving standalone engine link boundaries.
  Controlled tests exposed deferred pending-signal delivery after sigprocmask:
  a successful application mask update now delivers unblocked pending signals
  before returning, preventing an old notification from interrupting a later
  READ. Table-free language probes retain their raw signal conventions.
- Added src/aux/clang/test/watchdog with the complete staged upstream
  Watchdog.cpp/Unix implementation, ordinary C++ observers and production CRT,
  libc and C++ archives. No source extractor or substitute watchdog supplies
  its behavior. All runtime expectations live in WAST: 65 live assertions and
  47 controlled-clock assertions. Tests cover constructor/destructor cancellation,
  unsigned remaining values, below/on-deadline boundaries, retained masked
  pending signals, fork/exec ownership, READ/waitpid interruption, caught/default
  CPU-bound delivery and concurrent child timers. Child observations use pipes
  to avoid the separately recorded forked regular-file offset limitation.
  Existing raw read returns negative EINTR while retaining errno; that boundary
  is recorded explicitly rather than claimed to be normalized public read.
- Native and ASan/UBSan Bash each pass 65/65 with exit zero and no timeout or
  handler failure. Controlled native sanitizer and browser API sessions pass
  48/48 including launch; the production packaged worker passes 66/66 live and
  48/48 controlled with empty stderr. Below-deadline controls confirm no early
  progress, and unmask delivery is observed before the subsequent READ.
  No manual browser test is needed. Optional Firefox upload/run/download steps
  are in the fixture README. The browser API manifest helper required a sandbox
  retry after its Node-to-Python spawn was denied with EPERM; the unrestricted
  parity rerun passed.
- Actual one-job LLVMSupport compilation and archiving now succeed. Required
  Path.cpp, Process.cpp, Program.cpp, Signals.cpp and Watchdog.cpp remain selected;
  the archive is 3,673,552 bytes. Fresh complete guest/native Watchdog compiles
  use generated flags without PCH and have zero diagnostics, producing
  514-/1,544-byte objects. The native implementation is unchanged. Archive
  compilation does not establish complete Support linking, LLVM ExecuteAndWait/
  ExecuteNoWait execution, finite child waits or Clang/LLD startup/shutdown.
- Retained sanitizer checks pass: 627 package-libc and 46 system-libc assertions,
  136 LLVM signal assertions, 234 public exec/wait and 282 actual LLVM Wait
  assertions. The package aggregate uses the established 180-second timeout
  for its wide formatter. Private guards pass 68 signal/clock ownership checks,
  132 process lifecycle checks and 62 exec-transition checks; signal-mask WAST
  passes native and sanitizer suite contexts. Native and packaged-worker process
  groups pass 71 each; terminal descriptors pass 120 each. The previously
  recorded raw blocking-pipe i32-smoke issue remains separate; the full smoke
  gate is not claimed green.
- SDK checks pass for 63 headers, six include orders, 825 available functions,
  143 unavailable and 31 globals with actual Wasm signatures. Refreshed/audited
  snapshots contain 373 entries, 281 batch-supported, 118 installed files and
  616 VFS nodes. Watchdog tests require Bash context; controlled assertions also
  require clock injection. Live/controlled observers are 5,114/4,351 bytes.
  Built and both installed libc copies match at 94,917 bytes. C++ archives remain
  byte-identical at 905,086 and 423,762 bytes. Repeated source/native/C++/SDK and
  fixture targets compile nothing.
- The offline page contains 520 files, is 4,665,260 bytes and embeds a
  3,390,564-byte compressed archive. Both new WAST snapshots, retained process
  tests, public headers, both libc copies, engine and frontend match packaged
  bytes. The normalized manifest digest is
  cf0558e769d574a2c1f834a3a52694d344edc5e278b9611aa510db52386d9872.
  WAST parse, shell/Python/JSON/whitespace checks and the read-only LLVM checkout
  are verified. No upstream source was modified.

Aggregate evidence is build/engine/logs/clang-alarm-verification.json.
Actual Support build, whole guest/native source commands, native/sanitizer
sessions, clock parity, retained guards, SDK, snapshots/package bytes, caches
and final checks use clang-alarm-* under build/engine/logs. Main worker results
are clang-browser-watchdog-results.json and
clang-browser-watchdog-controlled-results.json. See the
[watchdog reproduction and optional browser steps](../src/aux/clang/test/watchdog/README.md).

The archive-linked execution slice below completes that method gate and records
its filesystem and loader boundaries before changing C++ lifecycle behavior. Positive child waits must reuse the
shared clocks, signals and readiness scheduler; requested child statistics
remain unsupported. Crash recovery, sigsetjmp mask saving, C++ startup/shutdown,
static exit registration, Clang resources/driver defaults and actual native/
browser Clang/LLD compilation, linking and execution acceptance remain.


## Archive-linked LLVM execution and executable loader slice

- Added the aux-owned clang-support target so the real Support archive can be
  built independently of the complete Clang/LLD graph. The program-execution
  fixture links libLLVMSupport.a and libLLVMDemangle.a with current C++ archives,
  production CRT and installed shared libc. It uses actual ExecuteAndWait,
  ExecuteNoWait and Wait; no extracted method or substitute process provider
  supplies their behavior. Section collection retains the used methods, so this
  does not establish every Support dependency or full C++ startup/shutdown.
- Reproduced failed child setup returning into both callers: the missing-input
  probe emitted two result records, with the original parent reporting success
  after its child returned into the observer and exited normally. The guest-only
  adaptation now terminates failed redirect/detachment setup with status 126.
  LLVM's ordinary Wait reports the execution error. ExecutionFailed remains false
  when a child was created but later failed setup/exec, preserving its distinction
  from failure to create a child. Native branches retain their original behavior.
- Preflight rejects optional child memory budgets and CPU affinity with ENOSYS
  before allocating/forking. Stored rlimit values cannot enforce LLVM's requested
  budget. Malformed redirect vectors return EINVAL before release-build indexing.
  Requested statistics and positive child waits retain their existing explicit
  unsupported errors before child creation or consumption. Checks include absent
  error destinations, no child to reap and no redirect-created side effects.
- The first complete runtime run exposed intermittent false execution failure:
  executable instantiation returned EXEC_YIELD at a cooperative pump checkpoint,
  which the exec transition translated to ENOEXEC. Executable preparation now
  uses the same synchronous pump deferral as DSO loading, covering module starts
  and startup hooks. Timeout/cancellation polling stays active, and the caller's
  quantum is restored on every return. A long start.wat counter fixture checks
  completion before entry; ordinary application execution retains scheduling.
- All 357 runtime expectations live in program-execution.wast. They verify true/
  false and child exit/error interpretation, owned non-NUL-terminated StringRef
  arguments/environments, literal and empty arguments, explicit/inherited current
  environment, regular stdin/stdout/stderr redirects, merged descriptors, setup/
  exec failure, held-child polling and later reaping, concurrent sleeping children
  and actual detached SID/group ownership with an unchanged parent. Native and
  ASan/UBSan Bash each pass 357/357 with exit zero, no timeout or handler failure.
  The refreshed production worker passes 358/358 including Bash launch, has empty
  stderr and matches the final packaged manifest digest. No manual browser test
  is required; optional Firefox upload/run/download steps are in the README.
- Empty redirect paths exposed a separate filesystem prerequisite: the guest has
  no /dev/null device, and LLVM's corresponding child setup fails. Its dedicated
  case records that error honestly; other success cases use actual regular files
  or inherited descriptors. No regular file or compiler-local fake null device
  supplies success. Shared device semantics and empty-redirect acceptance remain
  open alongside positive child deadlines.
- Actual one-job Support builds succeed with the required Path, Process, Program,
  Signals and Watchdog sources retained, producing a 3,673,850-byte archive.
  Fresh whole guest/native Program compiles with generated flags, no PCH and zero
  diagnostics produce 16,297-/32,240-byte objects. The observer is 53,927 bytes and
  verifies 37 actual libc imports. Retained imports resolve without inventing exit
  registration; C++ static lifecycle remains a separate required boundary.
- Retained sanitizer checks pass 282 actual LLVM Wait, 234 public exec/wait and
  65 alarm/Watchdog assertions. Private sanitizer guards pass 62 exec-transition
  and 132 process lifecycle checks. Native process groups pass 71 checks and
  terminal descriptors 120. These exercise current shared runtime artifacts.
  The previously recorded standalone blocking-pipe smoke issue remains separate;
  the full i32-smoke gate is not claimed green.
- SDK checks pass 63 headers, six include orders, 825 available functions,
  143 unavailable and 31 globals with actual Wasm signatures. Refreshed/audited
  snapshots contain 374 entries, 281 batch-supported, 119 installed files and
  618 VFS nodes. The new snapshot requires Bash context and observer output.
  Built/installed libc remains byte-identical at 94,917 bytes; C++ archives remain
  byte-identical at 905,086 and 423,762 bytes. Repeated Support/fixture and source/
  native/C++/SDK targets compile nothing.
- The offline page contains 521 files, is 4,669,264 bytes and embeds a
  3,393,566-byte compressed archive. The new WAST snapshot, retained process
  tests, public headers, both libc copies, engine and frontend match packaged
  bytes. The normalized manifest digest is
  33068efaff1a7537b6dc7a4545d78a6daa3711ebbf65c0e336120061eba60bfd.
  WAST parse, shell/JSON/whitespace checks and the clean read-only LLVM checkout
  are verified. No upstream source was modified.

Aggregate evidence is build/engine/logs/clang-execution-verification.json.
Actual builds, full source commands, native/sanitizer sessions, retained guards,
SDK, snapshots/package bytes, caches and final checks use clang-execution-* under
build/engine/logs. Main worker results are
clang-browser-program-execution-results.json. See the
[execution reproduction and optional browser steps](../src/aux/clang/test/program-execution/README.md).

The shared device slice below accepts actual LLVM empty redirect paths. Finite
child waits remain next, using existing clocks, process/signal state and
readiness scheduling. Requested child statistics,
memory budgets and affinity remain unsupported until shared capabilities can
supply them. Crash recovery, sigsetjmp mask saving, C++ startup/shutdown and
static exit registration, Clang resources/driver defaults, and actual native/
browser Clang/LLD compilation, linking and execution acceptance remain.


## Shared null device and LLVM empty redirects slice

- The shared C kernel creates /dev and /dev/null when each sandbox starts.
  A dedicated character-device node/OFD returns EOF on reads and discards writes
  without byte backing or a platform output capability. Access modes govern
  nonzero I/O and immediate SELECT readiness. Ordinary seeks return zero;
  unsupported seek origins return EINVAL and regular-file truncation rejects
  the descriptor. General device creation, mappings/ioctls and file-status flag
  mutation are outside this slice.
- Device metadata lives in a retained inode. Dup/fork share its stateless open
  description without namespace pointers; it survives rename/unlink, namespace
  compaction and destruction of the original parent kernel. Synthesized inode
  allocation starts beyond the configured VFS entry range, avoiding collisions
  with automatically numbered transport entries. Shared public node-kind values
  preserve the existing 48-byte path and 128-byte stat layouts; stat reports
  character type and readdir reports DT_CHR. Both runtime Makefiles track this
  shared ABI header.
- Added src/engine/test/devices/null.wast and test-devices. Its 67 assertions
  cover type/size/inode identity, EOF/canaries, discarded and zero-count writes,
  access-mode failures/readiness, seeks, descriptor aliases/flags, openat,
  directory entries, symlinks, hard-link/rename/unlink lifetime and path errors.
  Temporary aliases are removed and /dev/null remains available. Raw read/write
  retain their existing negative-errno contract. Native and ASan/UBSan Bash each
  pass 67/67; the final packaged worker passes 68/68 including launch.
- Expanded the actual archive-linked LLVM execution fixture to 410 WAST
  assertions. Empty output/error paths now succeed. Additional synchronous and
  detached asynchronous children use empty paths for all three streams and
  record EOF input, successful discarded output, character metadata, zero size
  and SID ownership through a separate ordinary output file. Native and
  ASan/UBSan Bash each pass 410/410; the final production worker passes 411/411
  including launch. Both workers have empty stderr and match the final packaged
  manifest. No manual browser test is required; optional Firefox steps are in
  the fixture READMEs.
- Private sanitizer ownership guards pass 313 checks, including 16 new device
  reference/lifetime checks. The metadata codec passes 17 checks, including the
  new wire kind, zero-size validation and invalid-kind rejection. Retained path
  checks pass 90, exec transitions 62 and process lifecycle 132. Native terminal
  readiness/timing pass 90/46, process groups 71 and terminal descriptors 120.
  System-libc WAST passes 46 assertions under ASan/UBSan.
- The broader posix-kernel target reaches its passing private/terminal checks,
  then fails at the previously recorded standalone blocking-pipe expectation:
  pipe-descriptors.wast expects raw EAGAIN on an empty live pipe, while the
  blocking import publishes READ and the standalone runner rejects its external
  wait. No device read waits or schedules. This slice does not claim the full
  posix-kernel or i32-smoke gates green; their shared blocking/nonblocking test
  boundary remains an explicit follow-up.
- Actual one-job Support and fixture targets succeed; Support stays at
  3,673,850 bytes. The observer is 56,480 bytes with 37 verified libc imports;
  runtime expectations remain entirely in WAST. The libc directory-entry update
  produces 94,922-byte matching built/installed libraries. C++ archives remain
  byte-identical at 905,086 and 423,762 bytes. Repeated native/browser/fixture
  targets perform no compilation.
- SDK checks pass 63 headers, six include orders, 825 available functions,
  143 unavailable and 31 globals. Refreshed/audited snapshots contain 375
  entries, 281 batch-supported, 120 installed files and 620 VFS nodes. The null
  device is synthesized rather than transported in the tar archive. Packaging
  verifies both new/current WAST snapshots, retained tests, public ABI headers,
  both libc copies and current engine/frontend bytes. The offline page contains
  522 files, is 4,674,236 bytes and embeds a 3,397,296-byte compressed archive;
  its normalized manifest digest is
  0ada5fb6b523fc60a7fa686e92e63cc1c11f2ebb024888038f10a5b37a02a58e.
  Shell, JSON, WAST and whitespace checks pass, and the read-only LLVM checkout
  remains clean.

Evidence uses clang-null-* under build/engine/logs. Production-worker results
are clang-browser-null-results.json and clang-browser-null-execution-results.json.
The aggregate is clang-null-verification.json. See the
[shared-device reproduction](../src/engine/test/devices/README.md) and
[actual LLVM execution steps](../src/aux/clang/test/program-execution/README.md).

Next implement finite child waits through shared process/signal state,
monotonic clocks and the existing readiness scheduler. Requested statistics,
child memory budgets and affinity remain unsupported. Crash recovery,
sigsetjmp mask saving, C++ startup/shutdown and static exit registration,
Clang resources/driver defaults, and actual native/browser Clang/LLD compile,
link and execution acceptance remain.


## Shared absolute-deadline child wait slice

- Added public shared-libc waste_waitpid_until and the versioned
  waste_kernel.waitpid_until_v1 boundary. They reuse ordinary waitpid's child
  table, complete destination preparation and signal callbacks. Raw failures
  are negative errno; libc returns -1/errno. Options 0/WNOHANG and the existing
  positive/any-child PID model are supported; stopped/continued/group waits
  remain unavailable. Blocking calls without a monotonic clock return ENOSYS
  before consuming status.
- The shared kernel registers the absolute deadline in its existing syscall
  wait record. WAITPID readiness now includes expiry; the driver publishes the
  earliest timer through the same SELECT/native-poll/browser-callback path used
  by sleep and alarms. Ready matching children win over expiry; unrelated zombies
  do not satisfy a positive-PID wait. Expiry returns zero, preserving status,
  errno and the live child. Callers own termination policy.
- Caught signals reuse ordinary handlers and SA_RESTART. Interrupted/restarted
  calls retain their original absolute operand, so signals cannot extend the
  timeout. Completed, erroneous and interrupted calls clear the wait record
  while preserving independent alarms. Retries prepare the entire output again;
  no guest pointers or platform timer state are retained by the engine.
- Added src/aux/clang/test/child-deadline with ordinary C observations,
  a public C++ declaration probe, two WAST files, live/controlled manifests and
  Make targets. All runtime expectations live in WAST. Native and ASan/UBSan
  Bash each pass 107 live assertions: expiry/polling, raw/public errors, status/
  errno preservation, PID selection/later reaping, NULL/protected/unmapped
  destinations, early child completion, signals and independent nested parents.
- Controlled native sanitizer/browser API sessions each pass 62 WAST assertions
  plus Bash launch. Events check just-before/exact expiry, explicit EINTR retries
  and SA_RESTART without deadline extension, active/masked alarm independence,
  UINT64_MAX and a later sleep proving no stale child deadline remains. Fresh
  packaged-worker runs pass 108/108 live and 63/63 controlled, exit zero, have
  empty stderr and match the final packaged manifest. No manual browser check
  is required; optional Firefox upload/run/download steps are in the README.
- Private sanitizer ownership checks pass 324, including 11 new clock/token/
  clone/cancellation/full-width deadline guards. A generic transaction clone
  clears the active wait but preserves the independent alarm; ordinary fork
  still clears the child's alarm at the existing process boundary. Retained
  sanitizer WAST passes 234 public exec/wait, 282 actual staged LLVM Wait,
  410 archive-linked LLVM execution and 65 alarm/Watchdog assertions. Native
  private exec transitions pass 62 and lifecycle 132; process groups pass 71
  and terminal descriptors 120. The separately recorded standalone blocking-
  pipe failure remains open; full posix-kernel/i32-smoke are not claimed green.
- SDK checks pass 63 headers, six include orders, 826 available functions,
  143 unavailable and 31 globals, with actual Wasm signatures. Refreshed/audited
  snapshots contain 377 entries, 281 batch-supported and 122 installed files;
  the VFS has 623 nodes. Both new WAST snapshots, retained tests, public headers,
  both installed libc copies and the current engine/frontend match package bytes.
  The offline page contains 524 files, is 4,673,276 bytes and embeds a
  3,396,576-byte compressed archive. Its normalized manifest digest is
  32f7566e67615e4d8f6b5a00f7371a21bfd4d1380c10d4bf8aa26cb2fff81ba2.
- Repeated native/browser/fixture targets compile nothing. WAST parsing, shell,
  Python bytecode, JSON and whitespace checks pass, and LLVM remains a clean
  read-only checkout. No upstream source or compiler-specific timer was added.

Evidence uses clang-deadline-* under build/engine/logs. Production worker
results are clang-browser-child-deadline-results.json and
clang-browser-child-deadline-controlled-results.json. Aggregate evidence is
clang-deadline-verification.json. See the
[shared wait reproduction and scope](../src/aux/clang/test/child-deadline/README.md).

Next adapt guest LLVM Wait and ExecuteAndWait to this accepted shared API:
compute one checked absolute deadline, retain live children on polling expiry,
and kill/reap the exact target on ordinary expiry. Preserve native branches and
reject requested statistics, memory budgets and affinity until shared providers
can supply them. Actual archive-linked finite-wait acceptance remains open;
the current LLVM adaptation still explicitly rejects positive seconds. Crash
recovery, sigsetjmp mask saving, C++ startup/shutdown and static exit registration,
Clang resources/driver defaults and full native/browser Clang/LLD compile/link/run
acceptance also remain.


## Actual LLVM finite-wait integration slice

- Guest LLVM Wait now computes one absolute monotonic deadline with checked
  UINT64_MAX saturation and calls the accepted waste_waitpid_until API. Caught
  signals retry the same deadline. Polling expiry retains the live child;
  ordinary expiry sends SIGKILL and reaps only the target, preserving an
  unrelated ready sibling's status. ExecuteAndWait checks clock availability
  before fork; requested statistics, memory budgets and affinity remain
  explicitly unsupported. Native LLVM branches retain their implementation.
- Added src/aux/clang/test/finite-wait with actual archive-linked
  Wait/ExecuteAndWait/ExecuteNoWait, current CRT/C++/libc, three authored WAST
  files, live/controlled manifests and Make targets. C++ records observations;
  runtime expectations stay entirely in WAST. The unchanged LLVM error helper
  produces "Child timed out: " after successful timeout kill/reap.
- The full-width fixture exposed a shared scheduler tie: UINT64_MAX was both a
  valid saturated timer and the comparison sentinel for an indefinite pipe
  read. Selection now tracks deadline presence and prefers the finite timer.
  Exact expiry remains owned by the existing wait record and kernel clock.
- Live CPU-bound acceptance also exposed browser integration gaps. A pump
  resume now publishes readiness when it reaches an external wait. Native Bash,
  the production worker and browser suite share
  EXECUTION_DEFAULT_PUMP_QUANTUM_MS from src/config.h, retaining the native
  16 ms default. The live fixture uses ordinary worker defaults, so its CPU-bound
  child requires no test-only scheduling option. No compiler-specific timer,
  JavaScript process scheduler or Asyncify path was added.
- Native and ASan/UBSan Bash pass 217 live WAST assertions covering polling,
  precise target kill/reap, live/absent children, no post-timeout side effects,
  unsigned maximum seconds, early completion, signal retries, sleeping/CPU-bound
  ExecuteAndWait and asynchronous launch followed by finite Wait. Controlled
  native sanitizer/browser API sessions pass 135 assertions plus launch; exact
  pre-expiry/expiry, both signal retry modes, active/masked alarms and cleanup
  use twenty shared clock events. Another 32 assertions plus launch pass at
  UINT64_MAX, using full-precision decimal-string clock events.
- Final production workers pass 218/218 live, 136/136 controlled and 33/33
  saturation, including Bash launch. Retained staged-Wait/archive-execution
  workers pass 255/255 and 395/395. All exit zero, have empty stderr and match
  the final package manifest.
- Retained sanitizer WAST passes 254 actual staged LLVM Wait, 394 archive-linked
  execution, 107 shared child-deadline and 65 Watchdog assertions. Controlled
  shared-child sessions pass 62 plus launch in native sanitizer/browser API.
  Private exec transitions/lifecycle pass 62/22, process lifecycle 132, process
  groups 71 and terminal descriptors 120. The separately recorded standalone
  blocking-pipe expectation remains open; full posix-kernel/i32-smoke gates
  are not claimed green.
- Actual one-job Support builds succeed at 3,674,916 bytes, SHA-256
  e47433e5b02d64aa24654c69e11bf9d381e30d1547cfa62b4f2495c49e921d5f.
  Fresh whole native/guest Program.cpp compiles have zero diagnostic bytes and
  produce 32,240-/17,364-byte objects. Observers are 46,614/46,031 bytes.
  C++ archives remain byte-identical at 905,086 and 423,762 bytes; the matching
  built/installed shared libc remains 95,087 bytes. Repeated build targets
  compile nothing.
- SDK checks pass 63 headers, six include orders, 826 available functions,
  143 unavailable and 31 globals with actual Wasm signatures. Refreshed/audited
  snapshots contain 380 entries, 281 batch-supported, 125 installed files and
  627 VFS nodes. All three new WAST snapshots, retained tests, public
  ABI headers, both libc copies and current engine/frontend bytes match the
  offline package. The page contains 527 files, is 4,682,168 bytes and embeds a
  3,403,243-byte compressed archive. Its normalized manifest digest is
  4ea42e50e3817c99f1459f853eae89e2e634f8f845c760d07901abc69d02473b.
- WAST parsing, shared configuration, shell, Python bytecode, JSON, JavaScript
  syntax and whitespace checks pass. The LLVM checkout remains clean and
  read-only. No manual browser check is required after worker acceptance;
  optional Firefox upload/run/download steps are in the fixture README.

Evidence uses clang-finite-* under build/engine/logs. Production-worker results
are clang-browser-finite-wait-results.json, clang-browser-finite-wait-controlled-results.json,
clang-browser-finite-wait-saturation-results.json, clang-browser-finite-program-wait-results.json
and clang-browser-finite-program-execution-results.json. The aggregate is
clang-finite-verification.json. See the
[actual LLVM finite-wait reproduction](../src/aux/clang/test/finite-wait/README.md).

Next reconcile the retained standalone blocking-pipe regression with shared
blocking/nonblocking readiness and restore its full gates. Crash-context
setup/cleanup/recovery, sigsetjmp mask saving, C++ startup/shutdown and real
static exit registration, Clang resources/driver defaults, and full native/browser
Clang/LLD compile/link/run acceptance remain. No Clang/LLD executable has been
installed by this slice.


## Shared pipe status and readiness reconciliation slice

- Pipes retain their default blocking mode. F_GETFL reports access/status flags;
  F_SETFL changes O_NONBLOCK/O_APPEND through fixed and variadic guest adapters,
  preserving access mode and descriptor-local FD_CLOEXEC. Empty nonblocking
  reads and full nonblocking pipe writes return EAGAIN without registering a
  wait. Small nonblocking writes that cannot fit leave the pipe unchanged;
  larger writes retain short-write behavior. Blocking I/O uses the existing
  READ/SELECT wait and scheduler. No compiler-specific I/O path was added.
- A retained status object shares mutations through dup and fork, including
  regular/directory descriptions rebased into a cloned namespace. Final
  teardown releases it; saturated reference counts reject clone publication.
  Regular append placement reads these shared flags. Regular-file seek cursor
  sharing across fork remains a separate kernel ownership gap.
- Terminal READ now copies its effective VMIN threshold into the wait record.
  Buffered bytes can satisfy ordinary SELECT readiness while a particular read
  remains suspended for more bytes. Nonblocking reads bypass that threshold.
  Native session input resolves a surviving terminal alias when guest fd 0
  has been closed or replaced with a pipe.
- Expanded the authored pipe-descriptors regression to 170 assertions. EAGAIN
  cases explicitly enable O_NONBLOCK; closed-fd checks use fixture-owned
  endpoints. Added scheduling/pipe-modes.wast with four compound assertions
  and three forked children, covering restored blocking data/EOF wakeup and
  regular-file append mutations through aliases/fork. Private status ownership,
  wait-token stability, VMIN readiness and clone-failure guards add 16 checks.
- Public CLI Bash's final-command optimization exposed a root WAST completion
  bug: the driver attempted the child-only reap path for PID 1. Root completion
  now releases the handler context and returns through its invocation driver.
  Native sanitizer/browser API/packaged-worker sessions verify both successful
  root replacement (171/171 including Bash launch, status 0) and the intentional
  report mismatch (3/4 including launch, status 1). Existing child-handler
  recovery retains its expected mismatch and final status 3.
- Full native i32-smoke and posix-kernel targets pass. All 15 standalone
  regressions and their exact installed snapshots pass; sanitizer batch results
  contain 1,338/1,338 assertions, with zero failed/skipped files. The private
  kernel gate passes 340 runtime checks from 200 CHECK sites in 27 helpers;
  process lifecycle passes 132. Retained terminal readiness/timing, process
  groups and terminal descriptors pass 90/46/71/120 checks and their negative
  controls. Warnings-as-errors production compilation and ASan/UBSan pass;
  leak detection remains disabled for the documented ptrace environment.
- Shared native sanitizer/browser API sessions pass pipe descriptors 170/170,
  pipe modes 4/4 and Bash invocation 171/171. Existing CPU/backpressure,
  controlled overlapping timers, pipelines, nested Bash/handlers and linked
  provider isolation remain green. The public wasm-sanitize command runs the
  installed Bash/WAST invocation with 170 assertions and exit 0.
- Final production workers pass descriptors 170/170, modes 4/4, Bash 171/171,
  terminal readiness 90/90 and CPU/backpressure 2/2. Actual archive-linked LLVM
  finite Wait still passes 217/217 in sanitizer Bash and 218/218 in the worker,
  including launch. Every worker result matches the final package manifest;
  the expected failing root control preserves its diagnostic and status 1.
- The Support archive, both C++ archives, actual LLVM observers/Program objects
  and built/installed libc remain byte-identical to the previous slice. The
  LLVM checkout remains clean at 318bc72539e370171877d293da2691475a24a052.
  Refreshed/audited snapshots contain 381 entries, 281 batch-supported,
  126 installed files and 628 VFS nodes. Current tests, engine/frontend,
  public ABI headers and both libc copies match extracted package bytes.
  The offline page contains 528 files, is 4,684,092 bytes and embeds a
  3,404,688-byte compressed archive. Its normalized manifest digest is
  288bfab1bba6d87aeb2e6dec4e1a9c1690c156bfab0a86525e18c22916755414.
- WAST parsing, shared configuration, shell syntax, Python bytecode, JSON and
  whitespace checks pass. No Chromium launch or manual browser check is required
  for this slice; focused browser API/production-worker checks pass. Optional
  Firefox checks remain documented with the scheduling probes.

Evidence uses clang-pipe-* and clang-browser-pipe-* under build/engine/logs.
The aggregate is clang-pipe-verification.json; final native gates are recorded
in clang-pipe-native-gates-final.log. Shared session evidence is in
clang-pipe-session-results.json and clang-pipe-root-session-results.json, with
individual production-worker result JSON alongside them. See the
[scheduler and public CLI reproduction](../src/engine/test/scheduling/README.md).

Next verify actual LLVM crash-context setup, cleanup, recovery and signal
interactions, including nonzero sigsetjmp signal-mask saving/restoration.
C++ startup/shutdown and real static exit registration, Clang resources/driver
defaults, and full native/browser Clang/LLD compile/link/run acceptance remain.
No Clang/LLD executable has been installed by this slice.


## Actual LLVM crash recovery and shared checkpoint mask slice

- Nonzero positive/negative sigsetjmp save flags now copy the active kernel's
  full signal mask into the existing evaluator checkpoint. Copied tokens,
  yielded continuations and fork clones retain that mask by value. Matching
  live-frame restoration releases abandoned wait interests before restoring
  the mask; zero save flags leave changes intact. The legacy one-argument
  checkpoint import remains a zero-save operation. The public 64-byte Bash
  buffer ABI is unchanged; no native jump buffer, host mask or Asyncify is used.
- Expanded checkpoint.wast to 218 assertions over compiled C/C++ observations,
  checking masks after sleep, handler escape, pending delivery and fork alongside
  existing stack/local/token checks. A mask boundary drains a newly unblocked
  pending signal before a fresh sleep, independent of opcode delivery timing.
  The standalone setjmp-buffer-copy regression now has 12 assertions, including
  zero/positive/negative save flags and copied tokens with higher signal bits.
- Added src/aux/clang/test/crash-recovery with a normal C++ observer linked
  against the complete Support/Demangle/C++ archives and production CRT/libc.
  All 125 expectations live in WAST. Actual LLVM methods verify disabled,
  repeated and nested contexts, safe completion, explicit HandleExit and
  Process::Exit, zero exit codes, sleep resume, all six caught guest crash
  signals, mask cleanup and status classification. Unregister/reverse resource
  cleanup and real Signals callbacks/temporary-file cleanup pass. A signal
  outside a current context terminates/reaps only the forked child.
- Actual utility-mode acceptance exposed stale previous actions: an ignored
  signal was skipped at setup but overwritten at teardown. The guest LLVM
  adaptation records successful installations and restores only those entries.
  Previous handler, flags and mask are restored; ignored utility dispositions
  survive repeated setup cycles. Native LLVM retains its original backend.
  Whole native/guest CrashRecoveryContext.cpp recompiles pass.
- Native and ASan/UBSan Bash pass 125 crash-recovery and 218 compiled checkpoint
  assertions, plus the 12 direct-import assertions. Shared native/browser API
  and final packaged workers pass recovery 126/126 and checkpoints 231/231,
  including Bash launch. Root WAST replacement remains 171/171, status zero;
  retained child-handler recovery preserves its intentional mismatch and
  status 3. Linked-provider fork isolation and boundary negative controls pass.
- Full i32-smoke, continuation, posix-signal, posix-kernel and process-lifecycle
  gates pass against stable installed snapshots. All 15 standalone/sanitizer
  batch regressions pass 1,342 assertions, with no failed/skipped files.
  Private continuation/signal/kernel/lifecycle checks pass 43/68/340/132.
  Terminal readiness/timing, process groups and terminal descriptors retain
  90/46/71/120 checks and their negative controls. Production compilation uses
  warnings-as-errors; ASan/UBSan pass with documented leak detection disabled.
- Actual archive-linked LLVM finite waits remain green: 217 sanitizer Bash
  assertions and 218/218 production-worker results including launch. Repeated
  Support/fixture build targets compile nothing. Support is now 3,675,030 bytes,
  SHA-256 bd0c88b572e3097d64de4a43dfb798f5306a23a7928d1fc2c9e77607dd58f65a.
  The recovery observer is 37,755 bytes, imports 40 supported function
  signatures and does not import static exit registration. The C++ archives
  and both installed libc copies remain byte-identical to the previous slice.
- SDK/provider checks pass. Refreshed/audited snapshots contain 382 entries,
  281 batch-supported, 127 installed files and 630 VFS nodes. Current authored
  checkpoints/recovery/retained WAST, public headers, libc and engine/frontend
  match extracted package bytes. The offline page has 529 files, is 4,684,560
  bytes and embeds a 3,405,038-byte compressed archive. The normalized manifest
  digest is a3de5a82f215abae9b7198587792f9c60525aca9cf55274a550784e8992855c9.
- WAST parsing, shared configuration, shell/Python/JavaScript/JSON checks and
  whitespace checks pass. LLVM remains a clean read-only source dependency.
  No manual browser check or Chromium launch is required. Optional Firefox
  upload/run/download steps are documented with the new fixture.

Evidence uses clang-recovery-* and clang-browser-recovery-* under
build/engine/logs. The aggregate is clang-recovery-verification.json; final
native gates are in clang-recovery-native-gates-final.log and shared runtime
reports in clang-recovery-session-final-results.json. See the
[actual LLVM recovery reproduction and limits](../src/aux/clang/test/crash-recovery/README.md)
and [compiled checkpoint checks](../src/aux/clang/test/checkpoint/README.md).

Next establish a real C++ startup/standard-stream ordering baseline, then
provide static exit registration/finalization and verify shutdown. TimeProfiler
lifecycle, broader formatting, Clang resources/driver defaults and full
native/browser Clang/LLD compile/link/run acceptance remain. Recovery from
arbitrary Wasm traps/memory faults and machine stack symbolication are outside
this accepted guest signal slice. No Clang/LLD executable has been installed.

## Accepted C++ constructor startup and stream ordering slice

- Added src/aux/clang/test/startup with an ordinary compiled C++ observer
  linked against the current libc++/libc++abi archives, production CRT and shared
  libc. Runtime expectations are entirely in startup.wast: 86 assertions over
  82 recorded values, exact redirected output and the main return status.
  test-startup runs both the three-argument CRT and the two-argument variant
  used by Clang, for 172 assertions.
- The baseline reproduced 65 passes and 21 failures. Executable stream globals
  were null during constructors, so constructor input/output was skipped.
  waste-crt.c now binds stdin/stdout/stderr through the already initialized libc
  accessors before calling __wasm_call_ctors. Both initial Bash and child images
  continue to use the existing engine bootstrap; no second initializer or
  scheduler was introduced. Rebuilt and explicitly installed source-built Bash.
- Prioritized functions and an ordinary C++ global constructor verify ordering
  and exactly-once execution. Constructor environment and descriptor identity,
  redirected input/output, sleep resume, retained allocations, argv/environment,
  EOF and status 23 all pass. Real heap-backed string/vector operations, local
  destruction, over-aligned arrays and explicit aligned new/delete use the
  existing C++ archives and shared allocator. The constructor's usleep uses the
  existing kernel SELECT/timer continuation; the native interactive startup
  session records four SELECT waits across the two observers and WAST handlers.
- Native and ASan/UBSan Bash pass all 172 assertions. Interactive native and
  production packaged-worker sessions pass 173/173 including launch. Retained
  source-built Bash sessions and root WAST execution pass 9/9 and 171/171 in
  both runtimes. Relinked archive-backed LLVM recovery and checkpoint fixtures
  retain 125 and 218+12 native assertions; packaged workers retain 126/126 and
  231/231 including launch. All final statuses are zero.
- Provider/import checks, WAST parsing, shared configuration, shell syntax and
  whitespace checks pass. SDK checks retain 63 headers, six include orders,
  826 available functions, 143 unavailable functions and 31 globals. Both
  observer modules verify 24 libc imports and are 13,268/13,303 bytes.
  Support/C++ archives and both installed libc copies are byte-identical to
  the previous slice. Repeated Support and startup fixture builds do no work
  after installation timestamps settle; LLVM and Bash submodules stay clean.
- Refreshed/audited snapshots contain 383 entries, 281 batch-supported,
  128 installed files and 632 VFS nodes. Extracted offline bytes match authored
  startup/retained WAST, the rebuilt Bash, current engine/frontend, headers and
  libc. The page has 530 files, is 4,689,500 bytes and embeds a 3,408,743-byte
  compressed archive. Its normalized manifest digest is
  820b19253a2b0e36ce13782a5dbd28c4ce9d7462052bb09a278324e3d61575d5.

Evidence uses clang-startup-* and clang-browser-startup-* under
build/engine/logs. The aggregate is clang-startup-verification.json; native and
sanitizer output is in clang-startup-native.log and clang-startup-sanitize.log.
See the [startup fixture and optional Firefox steps](../src/aux/clang/test/startup/README.md).
No manual browser test or Chromium launch is required.

Next provide real C++ static exit registration/finalization and verify shutdown,
including actual PerThreadAllocator cache destruction and TimeProfiler lifecycle.
The startup observer's global object has a trivial destructor; local objects have
real destructors, and ordinary registration flags stay enabled. It does not
accept __cxa_atexit, C atexit, static destructor ordering or std::cout setup.
Current descriptor streams write directly, so output on return does not prove
buffered flush conformance. Broader LLVM initialization/formatting, resource
headers/driver defaults and full native/browser Clang/LLD compile/link/run
acceptance remain. No Clang/LLD executable has been installed.

## Accepted C/C++ static exit and LLVM cache shutdown slice

- Added an allocation-backed C/C++ registration list in shared guest libc:
  atexit, __cxa_atexit, __cxa_finalize and normal exit. Finalization selects the
  newest matching registration, retires it before calling, and searches again
  afterwards. Selective/repeated/recursive finalization and registrations added
  during cleanup execute exactly once. C and C++ callbacks keep their distinct
  Wasm function signatures; no host callback registry or new scheduler is used.
- Normal exit finalizes through ordinary guest indirect calls, requests fflush,
  then uses the existing immediate kernel exit. Cleanup can yield through the
  same usleep/SELECT/timer continuation used by sleep. _Exit and guest SIGABRT
  bypass callbacks. Legacy env exit/registration imports resolve to the loaded
  process-local libc unless an explicit registered provider overrides them.
  Removed the fake browser/engine atexit acknowledgement and updated SDK policy.
- Added src/aux/clang/test/shutdown, linked against current complete LLVM
  Support/Demangle, libc++/libc++abi, production CRT and shared libc with normal
  static-registration flags. All expectations are WAST: 305 assertions over
  277 observations, exact byte counts and nine statuses per artifact. The
  namespaced and legacy-import variants run 610 assertions. They cover real
  heap-backed global/local-static destructors, C callback ordering, recursive
  finalization, new registrations, 64 retained callbacks and yielding cleanup.
- The actual PerThreadAllocator template's function-local cache now destroys
  its backing allocation at normal exit. Observer wrappers delegate to the real
  allocator and only record allocation/free identity; they do not replace LLVM
  ownership or suppress callbacks. Removed diagnostic static-destructor
  suppression from synchronization and optional-query error-category fixtures;
  their 97/81 assertions pass with normal registration enabled.
- Exec acceptance exposed an ASan use-after-free: the resident libc retained a
  callback pointing into the freed previous executable. The existing allocator
  bootstrap now discards abandoned registrations after successful exec commits,
  without freeing or invoking old-heap nodes. Rejected executable preflight
  preserves the old heap/list; fork uses the existing private process clone.
  Both successful/rejected exec and independent parent/child cleanup pass.
- Rebuilt and explicitly installed source-built Bash and shared libc. Native
  and ASan/UBSan Bash pass 610 shutdown assertions plus 705 retained assertions
  across synchronization, platform, startup, actual LLVM crash recovery and
  checkpoints. Interactive native and final packaged-worker shutdown sessions
  pass 611/611 including launch, status zero; native records 12 SELECT waits.
  Workers retain synchronization 98/98, platform 82/82, startup 173/173, recovery
  126/126, checkpoints 231/231, finite waits 218/218, source-built Bash 9/9 and
  root WAST replacement 171/171. The child-handler negative control retains its
  intentional mismatch (16/17), diagnostic and final status 3.
- Full i32-smoke, continuation and process-lifecycle gates pass with stable
  installed snapshots. All 15 standalone/sanitizer batch regressions pass
  1,342 assertions without failed/skipped files. Private continuation/lifecycle
  checks retain 43/132 checks. Production C compilation uses warnings-as-errors;
  ASan/UBSan pass with the documented leak-detection setting. The 22-file libc
  package (627 assertions) and nine-file libc/kernel interaction set (46)
  also pass under sanitizers. The
  full libc package uses a 180-second suite budget: its first run reached the
  60-second total limit during binary128 formatting; the longer run has no
  failures or sanitizer findings.
- SDK/provider checks retain 63 headers, six include orders, 826 available
  functions, 143 unavailable and 31 globals. Shutdown artifacts verify 21/17
  libc imports and are 12,929/12,925 bytes. Both installed libc copies match the
  built provider (95,688 bytes); rebuilt Bash is 1,043,086 bytes. Support and both
  C++ archives remain byte-identical to the preceding slice. Repeated Support
  and fixture targets compile/link nothing once install timestamps settle. LLVM
  and Bash checkouts remain clean read-only dependencies.
- Refreshed/audited snapshots contain 384 entries, 281 batch-supported,
  129 installed files and 634 VFS nodes. Extracted package bytes match authored
  shutdown/retained WAST, Bash, libc, public headers and engine/frontend. The
  offline page has 531 files, is 4,693,812 bytes and embeds a 3,411,976-byte
  compressed archive. Its normalized manifest digest is
  79af5b04adfc319e2c1e57cc4ea5d0822cef52db1e60ae2d0e83bffe17616c80.

Evidence uses clang-shutdown-* and clang-browser-shutdown-* under
build/engine/logs. The aggregate is clang-shutdown-verification.json; final
native/sanitizer fixture output is in clang-shutdown-retained-native-final.log
and clang-shutdown-retained-sanitize-final.log. See the
[shutdown fixture and optional Firefox steps](../src/aux/clang/test/shutdown/README.md).
No manual browser test or Chromium launch is required.

Next verify actual LLVM TimeProfiler global initialization, begin/end aggregation,
finish/cleanup and emitted JSON with the newly available static-exit provider.
Buffered stdio flush, concurrent callback registration and automatic C++ DSO
finalization on dlclose are outside this accepted single-thread compiler profile;
explicit __cxa_finalize(dso) is covered. Broader formatting/LLVM initialization,
Clang resource headers/driver defaults and full native/browser Clang/LLD
compile/link/run acceptance remain. No Clang/LLD executable has been installed.

## Accepted LLVM TimeProfiler lifecycle and JSON slice

- Added a public-API observer linked against actual LLVMSupport/Demangle,
  current libc++/libc++abi, production CRT and shared libc. It does not include
  TimeProfiler.cpp or inspect private fields. Normal static-registration flags
  and LLVM registry ownership remain intact. Its 51 recorded observations,
  408-byte capture and normal exit status 17 are checked entirely in WAST.
- Six real usleep continuations use the existing injected monotonic clock and
  READ/SELECT control adapters. Native and packaged-worker drivers consume the
  same lifecycle.json. Monotonic time advances from 42 to 42.013 seconds;
  realtime remains 4,294,967,296,123,456,789 ns. Native and sanitizer sessions
  record six clock events and eight SELECT waits, with no new timer/scheduler.
- All 62 lifecycle assertions pass. They verify disabled/lazy calls, verbose
  state, owning metadata after mutation, nested same-name sections, RAII scopes,
  asynchronous begin/end pairs, instants and totals across two finished instances.
  Recursive sections do not double-count totals. Exact JSON bytes check ordered
  events/totals, quote/backslash/newline/tab escaping, process/thread metadata,
  negative cross-instance timestamps and full-width realtime microseconds.
- Actual raw-stream, preferred, fallback and default file-writing paths produce
  the same 1,680-byte trace. Missing parents return ENOENT without files or
  fallback output. Granularity removes short/zero entries and their instants
  while retaining totals; its exact trace is 416 bytes. Repeated cleanup and
  fresh initialization discard previous events/totals/metadata; the empty reuse
  trace is 224 bytes. All six actual JSON files match authored expectations.
- Real finish/cleanup retains vector capacity until the actual function-local
  registry destructor runs. Delegating malloc/free observers show its final
  eight-byte allocation is freed exactly once before an earlier C atexit
  reporter. No registry replacement, destructor suppression or host callbacks
  are used. Sequential finished instances exercise the disabled-thread compiler
  profile; concurrent LLVM thread profiling is not claimed.
- Linking the real JSON writer exposed an unresolved modf import. Added the
  shared binary64 provider using existing exact truncation and sign primitives,
  plus the public C/C++ declaration. Twenty package assertions pass natively,
  under ASan/UBSan and in browser Bash: fractions, signed zeros, integral/precision
  boundaries, subnormals, infinities, NaNs, output canaries and errno preservation.
  The C++ SDK probe verifies std::modf emits the expected C import/signature.
  General narrow floating printf and wider math support remain separate work.
- The Make target now runs both storage and public lifecycle checks; selecting
  wasm-sanitize also selects the sanitized interactive session adapter. Fresh
  evidence directories make repeated runs compatible with native exclusive
  result creation. Final native and sanitizer runs pass 74 storage plus 62
  lifecycle assertions each. Native/threaded full-source compilation passes.
  Retained raw-stream and namespaced/legacy shutdown checks pass 661 assertions
  in each native/sanitizer run, with no sanitizer findings.
- Final offline production-worker checks pass lifecycle 63/63, storage 75/75,
  modf 21/21, static shutdown 611/611 and constructor startup 173/173, including
  launch assertions. All exit zero. SDK/provider checks pass 63 headers, six
  include orders, 827 available functions, 143 unavailable and 31 globals.
  The lifecycle artifact is 162,384 bytes with 33 verified libc imports; shared
  libc is 95,772 bytes and both installed copies match the built provider.
- Complete Support rebuilds with one job against the updated SDK and is
  3,675,044 bytes. Both C++ archives retain their previous bytes (905,086 and
  423,762). Repeated Support, fixture and C++ SDK targets retain artifact hashes
  and mtimes and compile/link nothing. LLVM and Bash checkouts remain clean.
- Refreshed/audited snapshots contain 386 entries, 281 batch-supported,
  131 installed files and 636 VFS nodes. Extracted package bytes match 45 checked
  inputs, including new/authored WAST, Bash, libc, public headers and engine/
  frontend. The offline page has 533 files, is 4,694,288 bytes and embeds a
  3,412,334-byte compressed archive. Its normalized manifest digest is
  5fe7b4fb16e9c0c9ac422d2d46769583a1d4696336c03fef2f65e6227becce92.

Evidence uses clang-profiler-* and clang-browser-profiler-* under
build/engine/logs. The aggregate is clang-profiler-verification.json; native
and sanitizer lifecycle results/transcripts are also retained under
build/system-tests/clang/time-profiler. See the
[profiler reproduction and optional browser steps](../src/aux/clang/test/time-profiler/README.md).
No manual browser test or Chromium launch is required.

Next resume the actual Clang/LLD build graph with one job and resolve its next
concrete compile/link/provider failure. Broader LLVM initialization/formatting,
remaining platform dependencies, Clang resource headers/driver defaults and
full native/browser compile/link/run acceptance remain. TimeProfiler lifecycle
is complete for this compiler profile. No Clang/LLD executable is installed.

## Accepted Clang module cache and synchronization slice

- Resumed the full Clang/LLD graph with one job. Generated tables and 11 of
  116 AST source objects completed before a deliberate stop to check required
  dependency-scanning sources directly. Completed objects were preserved; this
  stop was not a compiler or browser crash.
- Reproduced 13 production errors in InProcessModuleCache.cpp: its header and
  guards unconditionally required mutex/condition-variable types absent from
  the disabled-thread libc++ profile. Repository-owned staging patches now
  select compiler-local aliases in this cache, DependencyScanningFilesystem,
  PrecompiledPreamble's temporary-file owner and ModulesDriver's worker pool/
  scan scheduler. Guest locks reuse LLVM SmartMutex; native/threaded branches
  retain standard types. The driver's two single-mutex scoped_lock guards use
  lock_guard, which this libc++ profile provides.
- Timed predicate waits release the counted lock, use actual LLVM exponential
  backoff through existing shared monotonic clocks and POSIX sleep, then
  reacquire and recheck. They preserve the deadline across signal interruption.
  Notifications need no queue in this one-thread compiler profile. Ready
  unbounded waits succeed; unfinished ones report a fatal error even under
  NDEBUG. No kernel timer, thread or scheduling implementation was added.
- Added a fixture linked against complete actual InProcessModuleCache,
  InMemoryModuleCache, ModuleCache and AtomicLineLogger sources, Support/
  Demangle, current C++ archives, production CRT and shared libc. It retains
  normal static registration. Its 106 observations, 848-byte capture and
  ordinary exit status 17 are checked entirely by 108 WAST assertions.
- WAST verifies owning invalidation keys, advisory contention and generations,
  old-owner destruction preserving a newer lock, owning binary write buffers,
  repeat/shared reads, actual disk flush/read, stable cached reads, written
  buffer precedence, ENOENT and full-width timestamps. Actual in-memory module
  states progress through Unknown/Tentative/ToBuild/Final with metadata intact.
  Controlled clock/signal events verify a one-second interrupted timeout,
  predicate completion during sleep and restored lock ownership. A real child
  rejects an unfinished wait with SIGABRT while its parent continues.
- Loading the linked pruning implementation exposed mangled directory imports.
  Public dirent.h now gives C++ callers C linkage; the C++ SDK probe redeclares
  and emits opendir/fdopendir/dirfd/readdir/closedir calls. Their actual C import
  names/signatures pass. LLVM's real directory iterator also finds both cache
  files and reports ENOENT/end for a missing directory in runtime WAST.
- All four affected production sources compile using their actual generated
  CMake/PCH rules. Complete native/threaded source gates pass and verify standard
  cache mutex/condition-variable types. These compile gates do not establish
  full dependency scanning, preamble building or worker-pool runtime acceptance.
- Final native, ASan/UBSan-runtime and packaged production-worker runs each
  pass 108/108 WAST assertions plus the launch assertion, exit zero and report
  no failures. Native/sanitizer sessions each record two clock events, two
  signals and six SELECT waits. Retained public TimeProfiler lifecycle passes
  62 assertions plus launch in each of the same three runtimes. Sanitizers report
  no findings; browser acceptance uses the packaged worker without a GUI run.
- SDK checks pass 63 headers, six include orders, 827 available functions,
  143 unavailable and 31 globals. The cache artifact is 122,745 bytes with
  46 verified libc imports. Updated Support is 3,675,010 bytes; C++ archives
  retain their previous bytes (905,086 and 423,762), as does shared libc
  (95,772). Repeated fixture/Support/source targets retain all nine checked
  artifact hashes and mtimes and compile/link nothing. LLVM and Bash remain clean.
- Refreshed/audited snapshots contain 387 entries, 281 batch-supported,
  132 installed files and 638 VFS nodes. Extracted package bytes match 47
  authored/built inputs including new WAST, dirent.h, libc, Bash and engine/
  frontend. The offline page has 534 files, is 4,698,660 bytes and embeds a
  3,415,612-byte compressed archive. Its normalized manifest digest is
  179a9e7e92632ca0b52eece8f06c55b83b689a8bf83eff269637428bbbad0fe0.

Evidence uses clang-cache-* and clang-browser-module-cache.json under
build/engine/logs; the aggregate is clang-cache-verification.json. Latest
native/sanitizer results and all original transcripts are retained under
build/system-tests/clang/module-cache. See the
[module-cache reproduction steps](../src/aux/clang/test/module-cache/README.md).

Next resolve the confirmed six-error production failure in
DependencyScanningWorker.cpp: AsyncModuleCompiles still owns unguarded standard
mutex/thread objects. Reuse the accepted LLVM task/executor profile, preserve
native/threaded behavior and verify capture lifetime, nested submissions and
shutdown ownership before resuming the full graph. The current LLD graph also
selects non-Wasm drivers despite the Wasm target goal; audit/narrow that explicit
guest selection before treating COFF/ELF/MachO dependencies as requirements.
Broader LLVM initialization/formatting, remaining platform work, resource
headers/driver defaults and full native/browser Clang/LLD compile/link/run
acceptance remain. No Clang or LLD executable has been installed.

## Accepted Clang module compilation task-owner slice

- Resolved the six-error production `DependencyScanningWorker.cpp` blocker.
  The repository staging patch moves its actual private `AsyncModuleCompiles`
  owner into `ModuleCompileTasks.h`. Native/threaded profiles retain the
  original mutex, thread creation and shutdown joins. Only the explicit
  disabled-thread guest selects LLVM's already accepted TaskGroup.
- TaskGroup runs callbacks inline in that profile, allowing recursive module
  discoveries before shutdown can reject submissions. One shared owning
  callable adapts move-only compiler/diagnostic/controller/cache-lock captures
  to TaskGroup's copyable function interface. No fake standard types, pthread
  capability, kernel timer or compiler-local scheduler was added.
- Added a source-backed observer using that production header, actual LLVM
  TaskGroup/CrashRecoveryContext from Support, complete actual cache sources,
  current C++ archives, ordinary static registration, production CRT and shared
  libc. Its 102 observations and ordinary exit status 19 are checked entirely
  by 104 WAST assertions; the binary capture is 816 bytes.
- WAST checks callback order, recursive submission, move-only capture cleanup
  and reentrant work submitted from capture destruction. Actual service-owned
  advisory locks outlive their temporary cache, remain held during nested work
  and interrupted POSIX sleep, then release once. Existing clock/signal controls
  and the shared sleep continuation drive the wait. Empty teardown and a fresh
  owner after completed callbacks also pass.
- Complete native and forced threaded-guest worker source gates pass. The exact
  production guest CMake/PCH worker rule passes, followed by all seven actual
  dependency-scanning source rules. This establishes compilation and task-owner
  behavior, not full prescan, module compilation or scanning acceptance.
- New native, ASan/UBSan-runtime and packaged production-worker sessions each
  pass 104 WAST assertions plus launch, exit zero and record no failures.
  Native/sanitizer evidence records one injected clock event, one signal and
  four SELECT waits. Retained module-cache and TaskGroup suites pass 108 and
  124 functional assertions respectively in each runtime: 336 functional
  assertions per runtime altogether. The established sanitizer profile disables
  leak detection; an initial retained parallel run enabled it and reached
  LeakSanitizer's sandbox/ptrace limitation after its WAST passed. The final
  ASan/UBSan runs report no findings.
- The module task artifact is 119,960 bytes with 43 verified libc imports,
  SHA-256 `55e1ea265688e0c9d22cc71adc1d822e00cd36d39618450fbafa0ab868dd3f57`.
  Support and both C++ archives retain their previous bytes. After final provider
  installation, repeated fixture and production source targets compile/link
  nothing and preserve all 11 checked artifact hashes and mtimes. LLVM and Bash
  submodules remain clean; shell syntax, Python bytecode and diff checks pass.
- Refreshed/audited snapshots contain 388 entries, 281 batch-supported,
  133 installed files and 640 VFS nodes. Extracted package bytes match 48
  authored/built inputs. The offline page contains 535 files, is 4,699,700 bytes
  and embeds a 3,416,392-byte compressed archive. Its normalized manifest digest
  is `31b08940b878edca3ee7e4b5124af5e863e38e4f34b4c563614cc4acad50b385`.

Evidence uses `clang-module-tasks-*` and `clang-browser-module-tasks*` under
`build/engine/logs`; the aggregate is `clang-module-tasks-verification.json`.
Native/sanitizer results and original transcripts remain under
`build/system-tests/clang/module-tasks`. See the
[module task reproduction steps](../src/aux/clang/test/module-tasks/README.md).
No manual browser test or GUI launch is required for this boundary.

The following diagnostic/driver-selection slice resolves this one-error LLD
entrypoint blocker and narrows the explicit guest driver graph to Wasm.
Broader initialization/formatting, platform work, resource headers/defaults
and full native/browser Clang/LLD compile/link/run acceptance remain.

## Accepted LLD diagnostics and WebAssembly driver selection slice

- Resolved the diagnostic owner's unguarded standard mutex only under
  `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`, reusing LLVM SmartMutex. Its
  five guards deduce their actual mutex type. Native/threaded branches retain
  standard synchronization and their original diagnostic semantics.
- The actual diagnostic source also depends on `std::regex`, which the
  localization-disabled guest libc++ omits. A private guest helper uses the
  existing LLVM POSIX regex backend for all nine Visual Studio location forms
  and duplicate splitting, with explicit capture indices, character classes,
  CR/newline exclusions and full-match anchors. Native/threaded branches retain
  the original ECMAScript expressions. No fake standard types or new regex
  engine were introduced; localization remains disabled.
- Added an observer compiling complete actual LLD ErrorHandler,
  CommonLinkerContext and Memory sources, plus real LLVM CodeGen/MC option
  initialization. It links complete Support/Demangle, current C++ archives,
  ordinary static registrations, production CRT and shared libc. C++ records
  75 observations (600 bytes) and exits 23; all 90 functional expectations
  live in authored WAST.
- WAST checks exact stdout/stderr bytes, embedded NULs, multiline separators,
  warning suppression/promotion, muted output, error counters/limits, all
  location forms, fallback, duplicate splitting and actual SyncStream levels
  and move ownership. The error-handling script runs actual LLVM child execution
  against installed false. An initial three-assertion failure was a test
  expectation error: the owner defaults to the short error-limit message,
  whereas the guessed text included a driver-specific hint. Expectations now
  use the actual upstream owner default; production behavior was unchanged.
- Actual fatal recovery discards a FileOutputBuffer temporary file while
  retaining its live mapping, then returns through the accepted LLVM crash
  context/checkpoint path. Teardown destroys arena owners before its callback;
  that callback yields through shared usleep and resumes with the context
  cleared. A subsequent context reuses the real static options. Fatal and
  error-limit children execute actual managed shutdown, flush buffered output,
  skip ordinary static/context callbacks through `_exit`, and return raw status
  256 to their surviving parent. No engine scheduler or timer was added.
- Added explicit default-off `LLD_WASTE_WASM_ONLY` selection, enabled by the
  guest profile and rejected outside LLVM_WASTE_GUEST. CMake selects Common/Wasm
  libraries and the wasm-ld alias; the entrypoint uses a static immutable
  Wasm-only driver table in both ordinary and reentrant/test paths. Native
  graphs retain all five drivers and four aliases. Generated graph/link/alias
  audits pass; the actual guest entrypoint object references only wasm::link.
  These checks establish selection, not final executable dispatch or linking.
- Complete diagnostic and all-driver entrypoint sources compile with warnings
  as errors in native and forced threaded-guest profiles (four source gates).
  Exact guest CMake/PCH rules pass for ErrorHandler, CommonLinkerContext and
  the entrypoint, including the real vt_gen and CodeGen PCH prerequisites.
  Their production objects are 27,041, 6,708 and 10,749 bytes respectively.
- Native, ASan/UBSan-runtime and packaged production-worker sessions each pass
  90 WAST assertions plus launch (91/91), exit zero and report no failures.
  Native evidence records one clock event, three SELECT waits, five forks,
  four execs and five child exits. The established sanitizer profile disables
  leak detection; ASan/UBSan report no findings. No manual browser test or GUI
  launch is required for this boundary.
- The observer artifact is 340,048 bytes with 53 verified libc imports,
  SHA-256 `0cda186c0b6112ed6f26ab312a0dfd7a3376470d308f03d7c9071652a59fb22c`.
  Support, C++ archives, libc and all 11 previously checked artifacts retain
  their bytes. Repeated fixture/production-source builds compile/link nothing
  and preserve all 22 checked artifact hashes and mtimes. LLVM and Bash
  submodules remain clean; shell syntax, Python bytecode and diff checks pass.
- Refreshed/audited snapshots contain 389 entries, 281 batch-supported,
  134 installed files and 642 VFS nodes. Extracted package bytes match 49
  authored/built inputs. The offline page contains 536 files, is 4,703,368 bytes
  and embeds a 3,419,145-byte compressed archive. Its normalized manifest digest
  is `bf14db352f18ee66c3854ef103ead40edfa6527b0da506ba7f69fe5c89840e1d`.

Evidence uses `clang-lld-diagnostics-*` and `clang-browser-lld-diagnostics*`
under `build/engine/logs`; the aggregate is
`clang-lld-diagnostics-verification.json`. Original native/sanitizer results
and transcripts remain under `build/system-tests/clang/lld-diagnostics`.
See [LLD diagnostic reproduction and scope](../src/aux/clang/test/lld-diagnostics/README.md).

The following filesystem slice resolves this five-error removal-helper blocker
and advances the production Common/Wasm source checks. Full compiler/linker
acceptance remains pending.

## Accepted LLD filesystem and pointer-format SDK slice

- Resolved `unlinkAsync`'s unguarded standard synchronization declarations only
  under `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`. The guest follows upstream's
  one-thread fallback, leaving destination replacement to the real output
  writer even when a strategy requests zero or eight threads. Native/threaded
  branches retain their detached-close optimization. Existing shared filesystem
  ownership and POSIX sleep drive the runtime behavior.
- Added an observer compiling complete actual Filesystem, diagnostics, context
  and memory sources, real CodeGen/MC initialization, production CRT, Support/
  Demangle and current C++ archives with shared libc. C/C++ records 177 values
  (1,416 bytes), saves exact file snapshots and exits 27; all 196 functional
  expectations live in authored WAST.
- WAST checks preflight without output mutation or leftover files, destruction
  of an uncommitted mapping, executable output committed after shared usleep,
  and descriptor/inode lifetime after replacement. The old descriptor retains
  old bytes and an unlinked inode while the new target has a different inode,
  new binary bytes and mode 0755. Explicit/repeated discard removes temporary
  names while mapping data stays live. Real in-memory and zero-size output,
  ordinary truncation/buffering/pwrite, LTO read/seek/write and the write-only
  `/dev/null` fallback also pass, including exact error diagnostics.
- Advancing actual Common compilation exposed missing `PRIxPTR` in LLVM's
  `DataRefImpl` formatter. Public `inttypes.h` now defines all six pointer PRI
  macros from the compiler's matching format definitions. The public pointer
  aliases are guest `long`/`unsigned long`, distinct from fixed-width `int`
  aliases despite both being 32 bits. Compiled C and C++ WAST covers signed
  bounds, unsigned high bits, decimal/octal/hex, a following 64-bit argument and
  exact actual LLVM formatter output through the existing shared formatter.
- Complete native and forced threaded-guest Filesystem sources compile with
  warnings as errors. All 13 actual production Common source rules and the
  entrypoint compile with their real PCH/generated headers. Direct source gates
  now refresh CMake's generated `depend` targets before compiling; otherwise
  the SDK edit left the CodeGen PCH stale. Required version, analysis and Wasm
  options headers come from their actual upstream generation targets.
- Generated version headers initially inherited WASTE's containing Git identity.
  Both native-generator and guest configurations now force LLVM's checked-out
  revision and canonical repository. Cache/header audits confirm revision
  `318bc72539e370171877d293da2691475a24a052` for LLVM and LLD. These source and
  configuration checks do not establish final driver dispatch or executable
  linking. The next real Wasm Driver compile failure is the single unguarded
  `ThinBackendProc::ErrMu` standard mutex declaration at `llvm/include/llvm/LTO/LTO.h:259`.
- Native, ASan/UBSan-runtime and packaged production-worker runs each pass
  196 WAST assertions plus launch (197/197), exit zero and report no failures.
  Native/sanitizer sessions record one clock event, three SELECT waits, two
  forks, three execs and two child exits. Retained LLD diagnostics pass 90
  assertions plus launch in each runtime: 286 functional assertions per runtime
  altogether. ASan/UBSan report no findings under the established profile with
  leak detection disabled. No manual browser test or GUI launch is required.
- The observer is 336,133 bytes with 51 verified libc imports, SHA-256
  `1829b82f0d76a42581b662dd34ff00b7fc3738e98d2d14628c4d573ae45e0ceb`.
  Support, both C++ archives, shared libc and 21 of the previous 22 checked
  artifacts retain their bytes; the real CodeGen PCH rebuilds for the SDK edit.
  After final provider installation, repeated fixture/production Common source
  gates compile/link nothing and retain all 40 checked hashes and mtimes.
  LLVM/Bash submodules remain clean; SDK, shell syntax, Python bytecode and diff
  checks pass.
- Refreshed/audited snapshots contain 390 entries, 281 batch-supported,
  135 installed files and 644 VFS nodes. Extracted package bytes match 51
  authored/built inputs including new WAST and `inttypes.h`. The offline page
  contains 537 files, is 4,703,964 bytes and embeds a 3,419,592-byte compressed
  archive. Its normalized manifest digest is
  `08beea65b2ffe6db315a2685ceda11ef7c2792d66d277a93d1c6006392789e9c`.

Evidence uses `clang-lld-filesystem-*` and `clang-browser-lld-filesystem*` under
`build/engine/logs`; the aggregate is `clang-lld-filesystem-verification.json`.
Original native/sanitizer results and transcripts remain under
`build/system-tests/clang/lld-filesystem`. See
[LLD filesystem reproduction and scope](../src/aux/clang/test/lld-filesystem/README.md).

The following slice resolves the ThinLTO error owner, its two production
aggregation callers and the former Wasm source blocker. Full LLD driver/IR
diagnostics, broader initialization/formatting, remaining platform support,
resource headers/defaults and native/browser Clang/LLD compile/link/run
acceptance remain. No Clang or LLD executable is installed.

## Accepted ThinLTO error ownership and deep-control capacity slice

- The staged LLVM patch selects LLVM's counted `SmartMutex` for the actual
  `lto::ThinBackendProc` error owner only in the disabled-thread guest profile.
  Both production LTO backends call its single protected aggregation helper;
  native and forced threaded-guest profiles retain `std::mutex`. The two
  sample-profile context formatters use LLVM's `raw_string_ostream` only in
  that same guest profile. All changes are in repository-owned staging patches,
  and the LLVM checkout remains read-only.
- The observer uses the actual ThinBackendProc base, `DefaultThreadPool`,
  `Config` constructors and upstream MC/Passes/Scalar and Option data objects.
  It records observations; 171 assertions in its WAST check move-only queued
  captures, retained callbacks, nested FIFO tasks, joined and surviving errors,
  reentrant cleanup, exact context strings and interrupted/resumed callbacks.
  The callback sleep uses existing shared clock, signal and sleep providers.
  Native, ASan/UBSan and packaged production-worker sessions each pass 172/172
  including launch. The real virtual `start` is never invoked, so this proves
  error ownership and scheduling at that boundary, not ThinLTO code generation.
- Direct CMake/PCH gates now compile all 14 LLD Wasm and six LLVM LTO production
  source rules, in addition to the previously accepted 13 LLD Common rules and
  entrypoint. Complete `LTO.cpp` also passes native and forced threaded-guest
  source checks. No Clang or LLD executable has been linked or installed.
- The actual generated scalar-options function nests 452 Wasm control blocks.
  `EXECUTION_MAX_CONTROL_DEPTH` in `src/config.h` now supplies one 512-block
  bound to decoder, validator, executor, clone and continuation state. The new
  portable binary WAST checks 512 live blocks, outer and deep table branches,
  early return and separate active caller/callee stacks: 10/10 native and
  packaged-worker assertions pass. Increasing that capacity exposed a native
  stack overflow from copying a 3.2 MiB `wast_group` inside the WAST handlers.
  Both handlers now pass resolved module/raw-source views directly to the
  encoder, and the normal host stack limit suffices.
- Retained LLD filesystem sessions pass 197/197 in native, sanitizer and the
  packaged worker. Native sanitized `i32-smoke` and all 16 installed engine
  regressions pass. The standalone CLI checks pass all 38 host boundaries in
  native and sanitizer profiles; the sanitizer libc batch uses a 120-second
  per-test budget derived from the shared runtime timeout configuration. These
  checks keep the expanded control bound and handler change covered outside
  the new observer.
- Refreshed and audited snapshots contain 392 corpus entries, 282 supported
  entries, 137 installed files and 647 VFS nodes. The offline page contains 539
  files and is 4,712,072 bytes. It embeds a 3,425,672-byte compressed archive
  and has normalized manifest SHA-256
  `0090900b12396f8f01faba19c9e600aae48aa136c24721ef3fb2a5ecacd56400`.
  Extracted package bytes match 53 authored/build inputs, including the two
  new WAST scripts. The worker runs use this production package without a GUI.
- After the final build, repeated fixture, Wasm driver and LTO source gates
  retain all 79 checked artifact hashes and modification times, with no C++
  compilation or linking. The observer Wasm is 296,903 bytes, SHA-256
  `c6b909858cbfe511abf829d3e8575863ddc9d323989857a82529e5056fff9599`.
  LLVM and Bash submodules are clean; shell syntax, Python bytecode and diff
  checks pass.

Reproduce the focused owner with `make -C src/aux test-lto-errors`;
see [ThinLTO owner reproduction and scope](../src/aux/clang/test/lto-errors/README.md).
Source and result evidence is retained under `build/engine/logs/clang-lto-errors-*`
and `build/system-tests/clang/lto-errors/`. The next step is to continue the
full Clang/LLD guest graph through linking, imported-symbol validation and
native/browser compile-link-run acceptance; the accepted source and owner
checks do not establish those outcomes.

## Accepted guest TargetParser host identity slice

- The next full `lld` graph probe stopped at `llvm/lib/TargetParser/Host.cpp`:
  selecting LLVM's generic Unix implementation included `sys/utsname.h`, which
  the Wasm guest SDK does not provide. The staged-source patch now selects a
  `LLVM_WASTE_GUEST` branch that returns the configured target triple without
  querying a Unix kernel release. It retains LLVM's optional target-override
  environment setting. Native source syntax checking still uses the original
  Unix branch, and the read-only LLVM submodule is untouched.
- The actual `LLVMTargetParser` archive builds with its generated headers and
  `Host.cpp` object. A 74,809-byte observer links that archive, Support,
  Demangle, the C++ archives, CRT and shared libc. Its 18 libc imports resolve
  against the installed provider. It records default/process triples and CPU
  identity, then returns 23; authored WAST checks exact output and exit status.
  Native, ASan/UBSan and the packaged production worker each pass 3/3 including
  launch. The observer's SHA-256 is
  `14a22797cfb1383c85c2ea3f35c4b3dffeec05e13abbd26f81543767de34889f`.
- A bounded, single-job rerun of the full `lld` target passed the former
  `sys/utsname.h` blocker and built BinaryFormat, BitstreamReader and Remarks
  archives plus initial LLVMCore objects. It reached 17% when the 240-second
  probe limit expired, with no new source error. This is build progress, not
  an `lld` link or runtime acceptance result. The next graph run should resume
  incrementally through LLVMCore and record the next actual source/link failure.
- Refreshed and audited snapshots now contain 393 corpus entries, 282
  batch-supported entries, 138 installed files and 649 VFS nodes. The offline
  page contains 540 files, is 4,712,364 bytes and embeds a 3,425,891-byte
  compressed archive. Its normalized manifest SHA-256 is
  `d55a08e8f9899b375822fb6df39d51c432b8e7dd01789434d3b5636b83e00f44`.
  Extracted package bytes match 54 authored/build inputs, including the new
  installed WAST. The production worker runs the observer without a GUI.
- A checked archive stamp keeps repeated focused builds from rerunning LLVM's
  table generators. Repeating the fixture build compiles or links nothing and
  preserves all six checked observer/TargetParser/Support/stamp artifact hashes
  and mtimes.
  Native source syntax, WAST parsing, Python bytecode and diff checks pass;
  LLVM remains clean.

Reproduce the focused boundary with `make -C src/aux test-target-host`;
see [guest host identity reproduction](../src/aux/clang/test/target-host/README.md).
The original and resumed graph probes, native/sanitizer results, worker JSON,
package audit and cache evidence are under `build/engine/logs/clang-target-host-*`
and `build/system-tests/clang/target-host/`. Full Clang/LLD linking, imported-
symbol validation and native/browser compile-link-run acceptance remain pending.
No Clang or LLD executable is installed.

## Accepted early LLVM graph and fixed-width lowercase format slice

- A bounded, single-job `lld` graph run completed the 8.3 MiB LLVMCore and
  709 KiB LLVMTextAPI guest archives, after the previous run stopped at 17%.
  The next pass reused both and completed LLVMBitReader, LLVMAsmParser,
  LLVMIRReader and LLVMFrontendHLSL. At 25%, actual
  `DWARFCFIProgram.cpp` stopped because the guest public `inttypes.h` lacked
  `PRIx8`. These are archive/source milestones; neither driver has linked.
- Added `PRIx8` and `PRIx16` using `x`, matching the guest's promoted
  `uint8_t`/`uint16_t` arguments. Existing `PRIx32` remains `x`, while
  `PRIx64` remains `llx`. A compiled C and C++ observer now records a mixed
  small/32/64-bit lowercase format call; its two new expectations live in
  `src/vfs/root/test/system/clang/stdio/format.wast`.
- `make -C src/aux test-stdio` and the same fixture through the
  ASan/UBSan native runtime each pass 37/37 WAST assertions. After refreshing
  the public-header PCH, the exact formerly failing DWARF production object
  compiles. The refreshed installed snapshot matches its authored WAST. The
  rebuilt offline `bash.html` worker passes 48/48, including ten package
  formatter checks and launch, with Bash exiting zero. `clang-sdk-check`,
  `vfs-tests-check` and `git diff --check` pass; the LLVM submodule is clean.
- Logs are under `build/engine/logs/clang-next-lld-graph*`,
  `clang-llvmtextapi-build.log` and `clang-prix-small-*`. The graph has not
  completed its next archive or linked `lld`; the next slice should resume
  the graph from this checked DWARF object, address its next actual diagnostic,
  and eventually validate imports and native/browser driver behavior. Narrow
  floating `printf` remains a separate formatter dependency; it must preserve
  `snprintf` sizing and truncation semantics when sharing the exact wide
  binary64/128 formatter.

## Accepted DWARF diagnostic ownership slice

- A 900-second, single-job run of the full `lld` target passed the previous
  fixed-width formatting blocker, built the 73,700-byte
  `LLVMDebugInfoDWARFLowLevel` archive and reached 27% in MC. Its time limit
  expired without a new source error; this percentage describes build progress,
  not completed linking or runtime acceptance.
- A direct production source check found four errors in DWARFVerifier's
  unguarded `std::mutex` owners and guards. The repository-owned staging patch
  selects LLVM's counted `SmartMutex<true>` only for
  `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`; both guards infer the actual
  owner's type. Native and threaded profiles retain standard mutexes. Complete
  native and forced threaded-guest syntax checks and the exact guest object
  rule now pass. The read-only LLVM checkout remains clean.
- The 31,261-byte observer links the complete actual verifier source, Support,
  Demangle, the C++ archives, production CRT and shared libc; all 16 libc
  imports resolve. Its observations cover category/subcategory counts, owned
  string keys, sorted enumeration, enabled/disabled callbacks and fresh owner
  state. An enabled callback yields through the existing shared `usleep`
  provider while its guard is live. All functional expectations are in
  `src/vfs/root/test/system/clang/dwarf-verifier/dwarf-verifier.wast`. Native and
  ASan/UBSan Bash sessions pass 35/35 assertions; the packaged production
  worker passes 36/36 including launch and exits zero. These checks accept the
  diagnostic aggregator, not the private compile-unit access map, full DWARF
  validation or thread exclusion.
- Refreshed snapshots contain 394 corpus entries, 282 batch-supported entries,
  139 installed files and 651 VFS nodes. The offline page contains 541 files,
  is 4,714,456 bytes and embeds a 3,427,460-byte compressed archive. Repeating
  the fixture and production verifier object gates compiles/links nothing and
  preserves all four checked artifact hashes and modification times. SDK and
  installed-corpus audits, shell syntax and diff checks pass; the installed
  verifier WAST matches its authored source.
- Checking adjacent `DWARFContext.cpp` exposed the next separate source
  blocker: `ThreadSafeState` declares `std::recursive_mutex`, unavailable in
  the disabled-thread guest C++ profile. That owner is not adapted or accepted
  by this slice. Preserve its recursive cache access and eager DWO parsing,
  retain native/threaded semantics, and add actual context behavior coverage
  before continuing the full build.

Reproduce with `make -C src/aux test-dwarf-verifier` and the source
targets documented in
[DWARF verifier reproduction and scope](../src/aux/clang/test/dwarf-verifier/README.md).
Native/sanitizer/worker, package and cache evidence is under
`build/engine/logs/clang-dwarf-verifier-*`; the full graph log is
`clang-dwarf-next-graph.log`, and the next context failure is retained in
`clang-dwarf-context-before.log`. Full Clang/LLD linking, imported-symbol
validation and native/browser compile-link-run acceptance remain pending.
No Clang or LLD executable is installed.


## Accepted DWARF context cache and MC formatter slice

- Adapted the actual `DWARFContext.cpp` recursive owner to LLVM's existing
  `sys::SmartMutex<true>` only for the disabled-thread guest profile. Lock guards
  infer the selected mutex type. Native and threaded builds retain the upstream
  recursive mutex; eager DWO behavior is preserved. The upstream `isThreadSafe()`
  label selects the eager/locking state and does not establish cross-thread
  exclusion in the disabled-thread guest backend.
- Added `clang-dwarf` and built the complete `LLVMDebugInfoDWARF` archive and
  dependencies, including MC, MCParser and Object. Actual guest objects and
  complete native/threaded syntax checks pass for the context and all three
  adapted formatter sources.
- Replaced unavailable standard stream formatting only in the disabled-thread
  guest branches of MC pseudo-probe inline-context formatting and both assembler
  macro-depth diagnostics. Actual MC archive formatting passes owned-string,
  nested-context and maximum-index assertions. The assembler diagnostics have
  complete source checks; runtime macro-depth acceptance still needs an actual
  assembler driver.
- Added public `PRIu8` and `PRIu16` after the actual DWARF address-table compile
  failure. The compiled C/C++ formatting fixture now checks mixed zero and
  maximum unsigned widths with default argument promotion. Native and current
  ASan/UBSan runners pass 39/39 assertions; the packaged worker passes 50/50
  including package and launch checks.
- Added `src/aux/clang/test/dwarf-context`. Its observer includes the actual
  context implementation and links production archives. Valid DWARF 4 inputs
  exercise normal and DWO units, cached object identities, recursive cache
  entry while a callback yields through the existing `usleep` path, eager versus
  lazy DWO behavior, empty-state reuse and destruction. WAST owns all expected
  values, exit status and exact sidecar file assertions. This accepts the tested
  cache and formatting behavior, not full DWARF conformance or thread exclusion.
- The observer is 2,137,154 bytes and has 37 verified guest-libc imports. Native
  and current ASan/UBSan Bash sessions pass 51/51 assertions; the packaged
  production worker passes 52/52 including launch and exits zero. The first
  sanitizer attempt used an older public runner; rebuilding through the runtime
  Makefile and rerunning both fixtures produced the accepted results. No shared
  engine or scheduling change was needed for this slice.
- Manual browser acceptance also passes: the downloaded
  `build/html-rt/dwarf-context-results.txt` reports 51/51 assertions with zero
  failures. `dwarf-context-status.txt` records the expected observer status 29,
  and `dwarf-context-test-exit.txt` records WAST exit status 0. The separately
  supplied `build/engine/logs/clang-dwarf-context-manual.log` also reports 51/51
  native assertions with zero failures. This completes manual browser
  acceptance for this fixture; full Clang/LLD acceptance remains pending.
- Refreshed snapshots contain 395 corpus entries, 282 batch-supported entries,
  140 installed files and 653 VFS nodes. The offline page contains 542 files,
  is 4,714,164 bytes and embeds a 3,427,240-byte compressed archive. Repeated
  fixture and guest-object checks preserve hashes and modification times for
  all 13 checked artifacts. SDK and installed-corpus audits, shell syntax and
  diff checks pass; installed WAST snapshots match authored sources. The LLVM
  submodule remains unchanged.

Reproduce with `make -C src/aux test-dwarf-context`; native/threaded,
guest-object, sanitizer, worker and manual browser commands are in
[DWARF context reproduction and scope](../src/aux/clang/test/dwarf-context/README.md).
The current sanitizer acceptance log is
`build/engine/logs/clang-dwarf-context-sanitize-final.log`; source, native,
worker, package and cache evidence is under
`build/engine/logs/clang-dwarf-context-*`. The manual browser test writes
`/tmp/dwarf-context-results.txt`, which can be downloaded from `bash.html`.

The next slice resumes the full `lld` graph. Its last full-graph checkpoint
remains 27%; this slice accepted the subsequent archives and focused source
checks without measuring a new overall percentage. Full Clang/LLD linking,
imported-symbol validation and native/browser compile-link-run acceptance
remain pending. No compiler or linker executable is installed.


## Accepted GSYM creator and transformer ownership slice

- Resumed the complete `lld` graph after the accepted DWARF context fixture.
  It exposed standard mutex owners in the actual GSYM creator header and DWARF
  transformer logging path. Adapted these owners to LLVM's existing
  `sys::SmartMutex<true>` only in the disabled-thread guest profile, and inferred
  the lock-guard types in `DwarfTransformer.cpp`, `GsymCreator.cpp`,
  `GsymCreatorV1.cpp` and `GsymCreatorV2.cpp`. Native/threaded owners remain
  standard mutexes. No replacement types were added to `std`, and no engine,
  kernel, scheduling or guest-libc implementation change was needed.
- Added `make -C src/aux clang-gsym`. The complete production
  `LLVMDebugInfoGSYM` archive builds and is 678,046 bytes. Eight complete
  native/threaded translation-unit checks pass, including the changed creator
  header, together with the four actual guest-object rules.
- Added `src/aux/clang/test/gsym`, linked against the real GSYM/DWARF and
  supporting archives. Two authored DWARF 4 units exercise conversion,
  creator string/file interning, copied string ownership, finalization, both
  GSYM versions and byte orders, decoded function names/addresses/ranges,
  gap/end rejection and reader ownership after its input buffer is overwritten.
  The writer selects the creator's actual string-offset width, as the production
  save path does. WAST owns all 114 observations, capture size, exact sidecar
  bytes and the intentional observer exit status 31.
- Task counts 1 and 3 exercise the transformer's direct and task-pool/logging
  branches through LLVM's existing disabled-thread backend. These checks accept
  those conversion and ownership paths; they do not establish concurrent LLVM
  execution, cross-thread exclusion, full DWARF/GSYM conformance or a completed
  compiler/linker.
- The observer is 2,419,853 bytes with 39 verified guest-libc imports. Native
  and the rebuilt/current ASan/UBSan Bash runner pass 117/117 assertions. The
  packaged production worker passes 118/118 results including launch and exits
  zero. The native Make target returns success with the same 117/117 report.
- Refreshed snapshots contain 396 corpus entries, 282 batch-supported entries
  and 141 installed files. The offline page contains 543 files, is 4,714,888
  bytes and embeds a 3,427,783-byte compressed archive. The installed GSYM WAST
  matches its authored source; SDK and installed-corpus audits pass. LLVM and
  Bash submodules remain unchanged.
- A subsequent 240-second bounded full `lld` run starts 94 additional source
  compilations, completes the MSF and CodeView archives, and reaches 40% in
  PDB sources without a compile error. It stops at the requested time limit;
  its last compile need not have finished. The graph refreshed a fixture
  dependency and caused one relink with identical observer bytes. The CMake
  percentage is a dependency-graph checkpoint, not overall port completion.
- Repeating the fixture and guest-object gates after the bounded graph stops
  compiles and links nothing. Hashes and modification times remain unchanged
  for all nine checked artifacts, including the observer, archive stamp, GSYM
  and DWARF archives, and four adapted production objects. Cache evidence is in
  `build/engine/logs/clang-gsym-cache-result.json`.

Reproduce with `make -C src/aux test-gsym` and the source, sanitizer,
worker and manual browser commands in
[GSYM reproduction and scope](../src/aux/clang/test/gsym/README.md).
Accepted runtime evidence is in `build/engine/logs/clang-gsym-native-cache.log`,
`clang-gsym-sanitize.log` and `clang-gsym-browser.json`. Build/source/package/SDK
checks are under `build/engine/logs/clang-gsym-*`; the original full-graph
failure is in `clang-context-next-lld.log`, and the bounded follow-up is in
`clang-gsym-next-lld.log`.

Manual offline-browser acceptance for this new fixture remains pending. Its
README provides upload/run/download steps for `gsym-results.txt`,
`gsym-status.txt` and `gsym-test-exit.txt`. Expect 117 PASS, zero failures,
observer status 31 and WAST exit status 0. The earlier context fixture's
user-supplied manual browser acceptance remains accepted.

The next slice continues the full `lld` graph from the PDB checkpoint. Full
Clang/LLD linking, imported-symbol validation and native/browser compile-link-run
acceptance remain pending. No compiler or linker executable is installed.

## Accepted CFG node formatting and shared math slice

- Continued the full `lld` dependency graph through PDB, BTF, Symbolize and
  ProfileData. Those production archives build. The next compile failure is
  the actual CFG node-attribute method's `std::stringstream` in Analysis,
  reached at the graph's 45% marker. Build percentages remain dependency
  checkpoints, not compiler-port completion estimates.
- Adapted `llvm/include/llvm/Analysis/CFGPrinter.h` only under
  `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`, using LLVM's existing unbuffered
  `raw_string_ostream` and its owned backing string. The insertion and return
  logic stays intact; native/threaded branches retain their standard streams.
  Complete native/threaded CFG printer source checks and four real guest
  Analysis object rules pass. No engine, kernel or scheduling implementation
  change was needed.
- Added `src/aux/clang/test/cfg-format`, linking the complete production
  CFGPrinter, HeatUtils, BlockFrequencyInfo and BlockFrequencyInfoImpl objects,
  real Core/Support dependencies and current C++ libraries. Eleven cases check
  absent/null/empty IDs, copied identifiers after source mutation, embedded
  NULs, a 256-character ID, comma placement and cold heat-color attributes.
  A callback yields through the existing shared `usleep` while the sink is
  live. Retained strings are written after their DOTFuncInfo/callback owners
  are destroyed. All 70 observations, the 560-byte capture, exact 786-byte
  attribute file and intentional observer status 32 are asserted in WAST.
- Runtime loading exposed the heat helper's missing `log2` import. Implemented
  binary64 `log2` and ties-away `round` in shared guest libc, with public math
  declarations and C++ SDK checks. Logarithms normalize normal/subnormal
  inputs, reduce the mantissa around one and evaluate a bounded atanh series.
  Powers of two are exact; 28 selected 100-digit references pass a four-ULP
  bound. This does not establish correctly rounded transcendental math or
  additional precision variants. Zeros return negative infinity with ERANGE;
  negative inputs return NaN with EDOM; other inputs preserve errno.
- Rounding tests cover half-integer neighbors, signed zero, large integral
  values, infinities/NaNs and errno preservation. Two integer-bit signaling-NaN
  probes first fail against the initial providers, then pass after both
  functions quiet those values through ordinary Wasm arithmetic. The complete
  authored `log2-round.wast` now has 89 assertions. Assertions remain in WAST;
  no private compiler math provider or separate timer/scheduler was added.
- The observer is 1,881,438 bytes with 39 verified libc imports. Native Make
  and current ASan/UBSan Bash runs pass 73/73 assertions. The complete libc
  package passes 736/736 across 24 files in both native and sanitizer runs.
  Its first sanitizer run exceeds the original 60-second suite deadline in
  the existing quad-formatting case; the final run uses the driver's existing
  180-second timeout and passes. Leak checking stays disabled for the ptraced
  runner; AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.
- The final packaged production worker passes 163/163 results, covering both
  authored files and launch, and exits zero. Snapshots contain 398 corpus
  entries, 282 batch-supported entries and 143 installed files. The offline
  page stages 545 files, is 4,721,780 bytes and embeds a 3,432,952-byte compressed
  archive. Both new snapshots match their authored inputs. SDK and corpus
  audits, warnings-as-errors math compilation and shell syntax checks pass.
  LLVM and Bash submodules remain unchanged.
- Adding the public math declarations refreshes the C++/LLVM headers and their
  affected production objects. A subsequent 240-second bounded full `lld`
  pass starts 70 compilations, refreshes dependencies through the CodeView
  checkpoint at 37%, and stops at its requested time limit without a compile
  error. Its last source need not have finished. The lower percentage reflects
  dependency rebuilding; it does not reopen the verified CFG source boundary.
- The final link recipe names 64 LLVM archives. At this checkpoint, 25 archive
  files exist and 39 are absent; 927 of its 1,641 named C/C++ objects are absent.
  This is a physical artifact inventory, not proof that every present file is
  current or that the remaining sources will compile. See
  `build/engine/logs/clang-cfg-final-inventory.json`.
- A warm repeat of the fixture and guest-object rules compiles and links
  nothing. All 13 checked hashes and modification times remain unchanged,
  including the observer, source stamp, four Analysis objects, Core/Support
  archives, staged CFG header and shared math/library artifacts. No recipe
  override warnings occur. Cache evidence is in
  `build/engine/logs/clang-cfg-cache-result.json`.

Reproduce with `make -C src/aux test-cfg-format` and the source,
sanitizer, worker and manual steps in
[CFG formatting reproduction and scope](../src/aux/clang/test/cfg-format/README.md).
Accepted reports are `build/engine/logs/clang-cfg-native-final.log`,
`clang-cfg-sanitize-final.log`, `clang-cfg-libc-native-final.log`,
`clang-cfg-libc-sanitize-final.log` and `clang-cfg-browser-final.json`.
Production source/SDK/package checks are under `clang-cfg-*`. The original
full-graph blocker is in `clang-pdb-next-lld-3.log`; the bounded dependency
refresh is in `clang-cfg-next-lld.log`.

Manual offline-browser acceptance for this CFG/math fixture is now recorded
from the three user-supplied files under `build/html-rt`.
`cfg-format-results.txt` reports 73 PASS for CFG and 89 PASS for shared math,
with zero failures in both suites. `cfg-format-status.txt` records the expected
observer status 32. `cfg-format-test-exit.txt` contains `0` and `,0`: the second
zero has a leading comma, so this is not a clean two-line numeric status file.
The complete passing suite reports and expected observer status are accepted;
the original evidence is preserved with that formatting detail documented.
Evidence hashes and the exact status lines are saved in
`build/engine/logs/clang-cfg-manual-evidence.json`. The prior GSYM manual check
remains pending; the user-supplied DWARF context browser acceptance remains
accepted.

The next slice continues the full `lld` graph through the remaining refreshed
CodeView/PDB dependencies and Analysis. Full Clang/LLD linking, imported-symbol
validation and native/browser compile-link-run acceptance remain pending. No
compiler or linker executable is installed.

## Accepted inlining statistics and shared narrow printf slice

- A 600-second bounded full `lld` pass refreshes the affected CodeView/PDB
  dependencies, completes BTF, Symbolize and ProfileData, and reaches Analysis
  at 45% without a compile error before its time limit. A direct production
  source rule then exposes `ImportedFunctionsInliningStatistics.cpp`'s
  unavailable standard string stream and `setprecision`.
- Adapted that complete source only under
  `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`, retaining its owned string,
  labels, fraction calculation and newline behavior. LLVM's existing
  `raw_string_ostream` and `format("%.4g", ...)` provide four significant
  digits. Native/threaded branches retain their standard streams. Both
  complete source compatibility checks and the real generated guest object
  rule pass. No engine, kernel or scheduler change was needed.
- Extended shared libc's narrow floating printf conversions by reusing the
  wide formatter's exact binary64/binary128 decimal and hexadecimal core.
  A private parsed-spec header coordinates both sinks and is an explicit
  Make dependency. Narrow output retains a bounded byte prefix and counts
  the complete result; wide output retains its capacity-error contract.
  Checked field parsing and result counts reject overflow. Requested zero
  runs are counted directly after narrow storage fills, so sizing enormous
  precision does not require a precision-sized loop. No compiler-local
  numeric implementation or host locale was added.
- Allocating and file printf wrappers now propagate formatter failure before
  allocation or writing, and free a temporary buffer if materialization
  fails. The 51-assertion package WAST covers f/e/g/a variants, nearest/even
  ties, subnormal/largest values, infinities/NaNs, signs, padding, default and
  explicit precision, mixed aligned variadic arguments, truncation, null/zero
  sizing, maximum count and overflow. Real file output and allocating calls
  check successful bytes and errors. This accepts the tested floating
  boundary, not every printf extension.
- Added `src/aux/clang/test/inlining`. Its observer includes the complete
  production statistics source to call the private formatter and public
  graph API, and links current Core/Support and C++ archives. Eight helper
  outputs cover zero denominators, scientific notation, negative/large
  fractions, omitted newlines and copied labels. A real C++ `snprintf` call
  mixes width/precision, double, integer, binary128 and string arguments.
  Four real IR functions form an imported/transitive graph; renaming them
  after recording verifies retained names. All 12 observations, the 96-byte
  capture, exact 263-byte helper/compiled text, 710-byte verbose graph dump
  and observer status 33 are asserted in WAST.
- The observer is 1,872,086 bytes with 35 verified libc imports. Native Make
  and ASan/UBSan Bash pass 16/16 system assertions. The complete libc package
  passes 787/787 assertions across 25 files in both native and sanitizer runs.
  Sanitizer runs use the existing 180-second suite limit; leak checking remains disabled
  for the ptraced runner while ASan and UBSan remain enabled.
- The final packaged production worker passes 68/68 results, including both
  authored WAST files and launch, and exits zero. The retained CFG/math worker
  scenario also passes 163/163 against the refreshed provider. Installed
  snapshots contain 400 corpus entries, 282 batch-supported entries and 145 installed files;
  the VFS audit checks 661 nodes. The offline page stages 547 files, is
  4,729,620 bytes and embeds a 3,438,834-byte compressed archive. Corpus,
  provider/SDK, source warnings-as-errors and shell checks pass. LLVM and Bash
  submodules remain unchanged.
- A warm repeat of the fixture and guest source gates compiles and links
  nothing. All 11 tracked hashes and modification times remain unchanged,
  including the observer, source stamp, actual statistics object,
  Core/Support archives, staged source and shared libc artifacts. No recipe
  override warnings occur. See
  `build/engine/logs/clang-inlining-cache-results.json`.
- Resuming the full graph now stops at ConstantFolding.cpp at 45%, with ten
  diagnostics for unavailable `exp`, `log` and `pow`. The verified statistics
  object was built through its direct production rule; the full graph has
  not progressed past ConstantFolding or produced the Analysis archive.
  Availability annotations remain intact. The next slice should determine
  which optional folds can faithfully decline unsupported operations and
  which require real shared math providers.
- The final link recipe still names 64 LLVM archives: 25 archive files exist
  and 39 are absent. Of 1,641 direct archive-source objects, 917 are absent;
  four additional BLAKE3 object-library inputs exist. This is a physical
  inventory, not proof that present artifacts are current or that remaining
  sources compile. See `build/engine/logs/clang-inlining-final-inventory.json`.

Reproduce with `make -C src/aux test-inlining`; source, sanitizer,
worker and manual steps are in
[Inlining statistics reproduction and scope](../src/aux/clang/test/inlining/README.md).
Accepted reports are `build/engine/logs/clang-inlining-native-final.log`,
`clang-inlining-sanitize-final.log`, `clang-inlining-libc-native-final.log`,
`clang-inlining-libc-sanitize-final.log` and `clang-inlining-browser-final.json`.
The source compatibility checks are in `clang-inlining-source-fixture-final.log`.
Build checkpoints are `clang-analysis-next-lld.log`,
`clang-inlining-source-before.log` and `clang-inlining-next-lld.log`.

Manual offline-browser acceptance for this fixture is now recorded from the
three user-supplied downloads under `build/html-rt`. `inlining-results.txt`
reports 16 PASS for the system test and 51 PASS for the package test, with zero
failures. `inlining-status.txt` contains the expected observer status 33, and
`inlining-test-exit.txt` contains two clean zero WAST exit statuses. File hashes,
suite counts and exact status text are saved in
`build/engine/logs/clang-inlining-manual-evidence.json`. Prior CFG/math and DWARF
context manual acceptance remains recorded; GSYM manual acceptance remains pending. Full
Clang/LLD linking, import validation and compile-link-run acceptance remain
unfinished. No compiler or linker executable is installed.

## Accepted optional host math folding slice

- Audited the actual ConstantFolding.cpp blocker and its public folding/query
  callers. LLVM can return no fold without changing the original operation.
  The explicit guest profile now declines 31 helper-mediated calls across
  17 math functions that have no shared libc provider, including `exp`, `log`
  and `pow`. Native builds retain ordinary host function pointers. Both unary
  and binary helpers check a missing pointer before touching errno or floating
  exception state. No SDK annotations, configure capabilities, engine/kernel
  code or scheduler behavior were changed.
- The import audit also exposed two direct host sine/cosine evaluations in
  AMDGPU handling. The guest now declines those general evaluations while
  preserving upstream's exact quarter-integer table and range checks. Exact
  logarithm special values, signed zero, poison and supported sqrt/log2 paths
  remain intact. The math-call side-effect query also declines an unevaluated
  power rather than treating a fabricated result as permission to delete it.
- Added `src/aux/clang/test/folding`, initially linking complete production
  ConstantFolding, TargetLibraryInfo, ValueTracking, VectorUtils,
  AssumptionCache, AssumeBundleQueries, GuardUtils,
  FloatingPointPredicateUtils and TargetTransformInfo objects against real
  LLVM/C++ archives. The dependency audit first exposed unresolved C++
  functions; adding their actual owners resolves all of them. The final
  fixture now links the completed 9,518,868-byte LLVM Analysis archive and real
  Core/Support dependencies. Its observer is 2,683,756 bytes with 42 verified
  libc imports and only existing kernel/POSIX imports outside libc. It
  introduces no replacement helpers.
- The 63-assertion WAST checks all 119 observed words, a 952-byte capture and
  intentional observer status 34. It covers double/float/half declines,
  unary/binary library calls, retained power operands and 18 retained call
  instructions. Positive controls verify exact logarithms, signed zero,
  complete vector folds, poison, APFloat fabs/floor/powi/fmod, shared sqrt/log2
  and AMDGPU quarter integers. Other controls verify partial-vector decline,
  no-builtin/nondeterminism restrictions, target-library availability and errno
  preservation. All expected outcomes remain authored in WAST.
- The archive-linked fixture passes 63/63 assertions through the public native
  Make target and ASan/UBSan Bash; no sanitizer report occurs.
  Complete production source checks pass in native, native threaded and guest
  threaded profiles, independently of the guest's disabled-thread selection.
  The nine real generated guest object rules pass. This accepts the tested
  optional folding boundary, not every Analysis operation or missing math
  provider. Leak checking remains disabled for the ptraced runner while ASan
  and UBSan remain enabled.
- The packaged production worker passes 64/64 results including launch and
  exits zero. The retained inlining/shared floating printf worker scenario
  passes 68/68. Installed snapshots contain 401 corpus entries, 282
  batch-supported entries and 146 installed files; the VFS audit checks 663
  nodes. The offline page stages 548 files, is 4,732,212 bytes and embeds a
  3,440,778-byte compressed archive. The new installed WAST matches its
  authored bytes. SDK, corpus, source and shell checks pass.
- The 600-second bounded full `lld` pass starts 123 C++ compilations, produces
  complete Analysis, IRPrinter, BitWriter, CGData and CodeGenTypes archives,
  and reaches 55% in TransformUtils without a compile error. It stops at its
  requested time limit. Its last started source is CloneFunction.cpp; that
  object has not finished. Build percentages describe the dependency graph,
  not overall compiler-port completion. See
  `build/engine/logs/clang-folding-next-lld.log`.
- The final link recipe names 64 LLVM archives: 30 files exist and 34 are
  absent. All 124 direct Analysis objects exist. Across the archive closure,
  788 of 1,641 direct source objects are absent; four additional BLAKE3 inputs
  exist. This is a physical inventory, not proof of currentness or successful
  compilation for remaining sources. See
  `build/engine/logs/clang-folding-final-inventory.json`.
- A warm repeat of the fixture and guest source gates compiles and links
  nothing. All 26 checked hashes and modification times remain unchanged,
  including the observer, source/archive stamps, nine production source
  objects, staged ConstantFolding source, completed Analysis/dependency
  archives, C++ archives and shared libc artifacts. No recipe override
  warnings occur. See `build/engine/logs/clang-folding-cache-results.json`.

Reproduce with `make -C src/aux test-folding`; source, sanitizer,
worker and manual steps are in
[Optional folding reproduction and scope](../src/aux/clang/test/folding/README.md).
Accepted reports are `build/engine/logs/clang-folding-native-make-final.log`,
`clang-folding-sanitize-final.log`, `clang-folding-browser-final.json` and
`clang-folding-retained-inlining-browser.json`. Source compatibility and real
guest-object checks are in `clang-folding-source-fixture-final.log`;
`clang-folding-link-dependencies.json` records final imports. Verification
summaries are in `clang-folding-verification.json`.

Manual offline-browser acceptance for this fixture is now recorded from the
three user-supplied downloads under `build/html-rt`. `folding-results.txt`
reports 63 PASS and zero failures. `folding-status.txt` records the expected
observer status 34, and `folding-test-exit.txt` records WAST exit status zero.
File hashes, suite counts and exact status text are saved in
`build/engine/logs/clang-folding-manual-evidence.json`. Prior inlining, CFG/math
and DWARF context manual acceptance remains recorded; GSYM manual acceptance
remains pending. Full Clang/LLD linking, import validation and compile-link-run
acceptance remain unfinished. No compiler or linker executable is installed.

## Accepted ExtTSP code-layout and guest algorithm selection slice

- Resumed the full `lld` graph from TransformUtils. CloneFunction,
  CloneModule and CodeExtractor compile; CodeLayout.cpp then fails at two
  unavailable `pow` calls in CDSort. The graph's marker is 52%; percentages
  are dependency checkpoints, not overall compiler-port completion.
- Audited `computeCacheDirectedLayout` callers across the dependency source
  tree. Production consumers are LLD ELF and BOLT; remaining references are
  declarations, definitions and unit tests. The configured guest selects
  WebAssembly, Clang/Common/Wasm LLD and no unit tests. MachineBlockPlacement
  requires the separate ExtTSP APIs. Source hashes and target settings are
  recorded in `build/engine/logs/clang-code-layout-graph-audit.json`.
- Guarded only CDSort's options, implementation class and public entrypoint
  definitions under the guest profile. ExtTSP and the shared source file stay
  in their normal CMake target. Native profiles retain both algorithms. No
  math provider, configure success, SDK availability, kernel or scheduler
  behavior was fabricated or changed. Enabling additional guest targets or
  linker drivers requires re-auditing this source selection.
- Added `src/aux/clang/test/code-layout`, linking the complete production
  CodeLayout object, actual Support/Demangle and current C++/libc providers.
  Its observer is 414,285 bytes with 49 verified libc imports, no unresolved
  C++ functions and no `pow` import. Other imports use existing kernel/POSIX
  interfaces. The complete real guest object rule passes.
- The 76-assertion WAST checks two 512-byte captures and both intentional
  observer statuses of 35. Seven graph cases cover reversed/forward chains,
  cold/disconnected nodes, a hot/cold diamond, backward loops and zero-count
  edges that still affect conditional scoring. Returned vectors remain live
  after input destruction. Both score APIs and computed layouts are checked
  against independently specified values with a 2e-12 absolute bound.
  Forward/backward cutoff neighbors, zero-count and self-loop controls pass.
- The second real process invocation parses
  `--ext-tsp-fallthrough-weight-uncond=2`. WAST verifies its expected score
  changes while conditional fallthrough remains unchanged. Required ExtTSP
  options remain registered and excluded CDSort options are absent. Expected
  outcomes and functional assertions remain entirely in WAST.
- The public native Make target and ASan/UBSan Bash each pass 76/76 assertions,
  with no sanitizer report. Complete source checks pass native, native
  threaded and guest threaded profiles. Additional native object checks find
  all three ExtTSP entrypoints in every profile, plus both CDSort entrypoints
  in native profiles and none in the guest. See
  `build/engine/logs/clang-code-layout-native-symbol-check.json`. Leak checking
  remains disabled for the ptraced runner; ASan and UBSan stay enabled.
- The packaged production worker passes 77/77 results including launch and
  exits zero; the retained optional-folding scenario passes 64/64. Installed
  snapshots contain 402 corpus entries, 282 batch-supported entries and 147
  installed files; the VFS audit checks 665 nodes. The offline page stages
  549 files, is 4,735,660 bytes and embeds a 3,443,363-byte compressed archive.
  The installed WAST matches its authored bytes. SDK, corpus and shell audits
  pass; the LLVM submodule remains unchanged.
- The resumed 600-second full `lld` pass starts 99 C++ compilations, builds
  the complete 5,581,824-byte TransformUtils and 249,080-byte
  AggressiveInstCombine archives, and reaches InstCombine at 60% without a
  compile error. It stops at its requested time limit. The last started
  InstCombineVectorOps.cpp object has not finished. See
  `build/engine/logs/clang-code-layout-checkpoint.json` and
  `clang-code-layout-next-lld.log`.
- The final link recipe names 64 LLVM archives: 32 files exist and 32 are
  absent. Across that closure, 686 of 1,641 direct source objects are absent;
  four additional BLAKE3 inputs exist. This physical inventory does not prove
  every present artifact is current or that remaining sources compile. See
  `build/engine/logs/clang-code-layout-final-inventory.json`.
- A warm repeat of the fixture and actual guest source gates compiles and
  links nothing. All 13 tracked hashes and modification times remain unchanged,
  including the observer, source stamp/object, staged source, Support/Demangle,
  C++ archives, CRT and shared libc artifacts. No recipe override warnings
  occur. See `build/engine/logs/clang-code-layout-cache-results.json`.

Reproduce with `make -C src/aux test-code-layout`; source, sanitizer,
worker and manual steps are in
[Code-layout reproduction and scope](../src/aux/clang/test/code-layout/README.md).
Accepted reports are `build/engine/logs/clang-code-layout-native-final.log`,
`clang-code-layout-sanitize-final.log`, `clang-code-layout-browser-final.json`
and `clang-code-layout-retained-folding-browser.json`. Source/import/symbol
checks are recorded in `clang-code-layout-source-fixture.log`,
`clang-code-layout-link-audit.json` and `clang-code-layout-native-symbol-check.json`.
The original blocker is in `clang-transform-next-lld.log`; the resumed full
graph is in `clang-code-layout-next-lld.log`.

Manual offline-browser acceptance is recorded from the user-supplied
`build/html-rt/code-layout-results.txt`, `code-layout-status.txt` and
`code-layout-test-exit.txt`: 76 PASS, 0 FAIL, both intentional observer statuses
35 and WAST exit status zero. File hashes and exact status text are saved in
`build/engine/logs/clang-code-layout-manual-evidence.json`. Prior folding,
inlining, CFG/math and DWARF context manual acceptance remains recorded; GSYM
manual acceptance remains pending. Full Clang/LLD linking, import validation and
compile-link-run acceptance remain unfinished. No compiler or linker executable
is installed.

## Accepted InstCombine and ScalarOpts archive build checkpoint

- Resumed the full `lld` graph with one compilation job and all output saved
  in `build/engine/logs/clang-instcombine-next-lld.log`. The 600-second pass
  starts 80 compilations, completes InstCombine and reaches ScalarOpts at the
  graph's 65% marker. It stops at its requested time limit with no compile
  error; the last started Sink.cpp object remains unfinished. See
  `build/engine/logs/clang-instcombine-checkpoint.json`.
- A targeted `LLVMScalarOpts` follow-up completes the remaining six objects
  and its archive in 126 seconds, exiting zero with no compile errors. See
  `build/engine/logs/clang-scalar-completion.log` and
  `clang-scalar-completion.json`. No source adaptation or provider change is
  required for these two components.
- InstCombine's archive is 3,479,892 bytes with 16 members; ScalarOpts is
  6,942,604 bytes with 84 members. Every member matches the corresponding
  production object byte for byte. Archive and member hashes are recorded in
  `build/engine/logs/clang-scalar-archive-verification.json`.
- Repeated each component's actual generated `depend` and `build` rules in
  separate Make invocations. All 102 tracked objects/archives retain their
  hashes and modification times; no compilation or archive linking occurs.
  This checks the component rules, rather than timing another full graph
  traversal. See `build/engine/logs/clang-scalar-cache.log` and
  `clang-scalar-cache-results.json`.
- The final LLD link recipe names 64 distinct LLVM archives: 34 files now
  exist and 30 are absent. Missing direct source objects fall from 686 to 604
  of 1,641; the four additional BLAKE3 inputs remain present. This physical
  inventory does not establish freshness of every older artifact, successful
  compilation of remaining sources or resolved imports. See
  `build/engine/logs/clang-scalar-final-inventory.json`. The LLVM submodule
  remains unchanged.

Continue the full `lld` graph after ScalarOpts; Vectorize, CodeGen and the
remaining target/optimization dependencies still need compilation. Build graph
percentages do not measure overall port completion. No new runtime behavior or
WAST assertions were added in this compilation-only checkpoint, so no new
manual browser test is required. Prior recorded browser acceptance is retained;
GSYM manual acceptance, full Clang/LLD linking, import validation and
compile-link-run acceptance remain unfinished. No compiler or linker executable
is installed.

## Accepted assignment interval formatter and CodeGen build slice

- Resumed the full `lld` graph after ScalarOpts with one compilation job. The
  actual ObjCARCOpts and Target archives complete. CodeGen then stops at
  AssignmentTrackingAnalysis.cpp because its private interval formatter names
  `std::stringstream`, which the guest C++ profile omits. The original failure
  and dependency checkpoint are in `build/engine/logs/clang-post-scalar-lld.log`
  and `clang-post-scalar-lld.json`.
- The owned patch selects LLVM's existing `raw_string_ostream` only under
  `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`. Native/threaded streams, interval
  analysis and option handling remain unchanged. No SDK capability, provider,
  kernel or scheduling change is needed. The complete patch applies to a fresh
  staged source with zero fuzz; the submodule stays read-only.
- Added `src/aux/clang/test/assignment-format`. The observer includes the
  complete production source and uses test-only `-fno-access-control` to call
  the private formatter with real IR arguments and interval maps. Production
  compilation retains normal access checks and has no test hooks. The observer
  links actual Core/Support dependencies and current C++/libc; it is 1,863,986
  bytes with 35 checked libc imports and no unresolved C++ function imports.
- All 23 WAST assertions pass through native Bash and ASan/UBSan Bash, with
  no sanitizer report. Exact lengths and bytes cover optional newlines, empty
  and exhausted iterators, no-location and unnamed values, UINT32_MAX interval
  bounds, heap-backed names and embedded NUL/control bytes. Captured strings
  survive name changes and destruction of every IR/map/formatter owner. WAST
  also checks the intentional observer status 36. These assertions accept the
  formatter boundary, rather than the entire assignment analysis or CodeGen.
- The complete actual guest object compiles through its generated CMake rule.
  Complete native, native threaded and guest threaded source checks also pass.
  The packaged production worker passes 24/24 results including launch and exits
  zero. Installed snapshots contain 403 corpus entries, 282 batch-supported
  entries and 148 installed files; the VFS audit checks 667 nodes. The offline
  page stages 550 files and is 4,734,884 bytes. The installed WAST matches its
  authored bytes, and corpus/package checks pass.
- The resumed 600-second full `lld` pass starts 134 C++ compilations and reaches
  CodeGen at the graph's 75% marker without a new compile error. It stops at its
  requested time limit, with MLRegAllocEvictAdvisor.cpp still unfinished. See
  `build/engine/logs/clang-assignment-format-next-lld.log` and its JSON summary.
  Build graph percentages do not measure overall port completion.
- The final link recipe names 64 distinct LLVM archives: 36 files now exist
  and 28 are absent. Missing direct source objects fall from 604 to 454 of
  1,641. CodeGen has 101 of 238 objects absent and no completed archive. All 13
  members of the new ObjCARCOpts/Target archives match their production objects.
  See `clang-assignment-format-final-inventory.json` and
  `clang-assignment-format-new-archives.json` under `build/engine/logs`. Physical
  counts do not establish freshness of every older artifact or resolved imports.
- A warm fixture and guest-source repeat compiles and links nothing. All 17
  tracked hashes and modification times remain unchanged, including the observer,
  source object/stamp, staged source, dependency archives, C++ archives, CRT and
  shared libc providers. No recipe override warnings occur. See
  `build/engine/logs/clang-assignment-format-cache-results.json`.

Reproduce with `make -C src/aux test-assignment-format`; complete source,
sanitizer, worker and manual steps are in
[Assignment formatter reproduction and scope](../src/aux/clang/test/assignment-format/README.md).
Accepted reports are `build/engine/logs/clang-assignment-format-native.log`,
`clang-assignment-format-sanitize.log` and `clang-assignment-format-browser.json`.
Import/source checks are in `clang-assignment-format-link-audit.json` and
`clang-assignment-format-source-verification.json`.

Manual offline-browser evidence is accepted from the user-supplied
`assignment-format-{results,status,test-exit}.txt` downloads under
`build/html-rt`: 23 PASS, zero failures, observer status 36 and WAST exit status
zero. Exact bytes and SHA-256 hashes are recorded in
`build/engine/logs/clang-assignment-format-manual-evidence.json`. GSYM manual
acceptance remains pending.
At this checkpoint, the next build work was to continue the full `lld` graph
from CodeGen. Full Clang/LLD linking, import validation and compile-link-run
acceptance were unfinished; no compiler or linker executable was installed.

## Accepted CodeGen archive build checkpoint

- Resumed the actual `lld` dependency graph with one compilation job and a
  600-second limit. The complete `libLLVMCodeGen.a` archive linked with no
  compile error. The run then entered SelectionDAG and stopped at its time
  limit while compiling `LegalizeTypesGeneric.cpp`; the log's last progress
  marker was 80%. See `build/engine/logs/clang-post-assignment-lld.{log,json}`.
- The CodeGen archive is 16,765,640 bytes. All 238 unique archive members
  match the 238 compiled objects named by its production link recipe byte for
  byte. Its SHA-256 and audit are in
  `build/engine/logs/clang-post-assignment-codegen-archive.json`. This proves
  archive assembly, not successful LLVM code generation.
- The current `lld` link recipe names 64 LLVM archives: 37 are present and 27
  absent. It names 1,641 direct source objects, of which 345 are absent.
  SelectionDAG has 18 of 26 direct objects absent. These are physical artifact
  counts and do not establish freshness of every older archive or resolved
  imports. See `build/engine/logs/clang-post-assignment-inventory.json`.
- A repeat `LLVMCodeGen` target finishes with zero C/C++ compilation and zero
  archive links; the CodeGen archive hash remains unchanged. The repeated
  generated-header targets still take time. See
  `build/engine/logs/clang-codegen-warm.{log,json}` and
  `clang-codegen-checkpoint-verification.json`. The read-only LLVM submodule
  remains clean.

At this checkpoint, the next build work was to continue the full `lld` graph
from SelectionDAG. Full Clang/LLD linking, import validation and
compile-link-run acceptance were unfinished. GSYM manual browser acceptance
remained pending; no compiler or linker executable was installed.

## Accepted SelectionDAG, AsmPrinter and GlobalISel archive checkpoint

- Resumed the production `lld` dependency graph with one compilation job and
  a 600-second limit. SelectionDAG, AsmPrinter, GlobalISel, Extensions,
  FrontendAtomic and FrontendDirective completed without a compile error. The
  run then entered ObjectYAML and stopped at its time limit while compiling
  `CodeViewYAMLTypes.cpp`; the last graph marker was 82%. See
  `build/engine/logs/clang-post-codegen-lld.{log,json}`. That marker is a
  build graph position, not overall compiler-port completion.
- All 85 unique members of the six new archives match the 85 compiled objects
  named by their production link recipes byte for byte. Archive sizes, SHA-256
  hashes and recipe checks are in
  `build/engine/logs/clang-post-codegen-archive-audit.json`. This proves
  archive assembly; it does not prove code generation or linker execution.
- The current `lld` link recipe names 64 LLVM archives: 43 are present and 21
  absent. Of its 1,641 direct source objects, 262 are absent. ObjectYAML has
  25 of 31 direct objects absent. These physical counts do not establish
  freshness of every earlier artifact or resolved imports. See
  `build/engine/logs/clang-post-codegen-inventory.json`. The read-only LLVM
  submodule remains clean.

At this checkpoint, the next build work was to continue the full `lld` graph
from ObjectYAML. Full Clang/LLD linking, import validation and compile-link-run
acceptance were unfinished. GSYM manual browser acceptance remained pending;
no compiler or linker executable was installed.

## Accepted shadow callback formatting and Instrumentation archive slice

- Resumed the full `lld` graph with one compilation job. ObjectYAML,
  FrontendOffloading and FrontendOpenMP completed; the next real source failure
  was AddressSanitizer's stack callback initializer using unavailable guest
  standard string-stream formatting. See
  `build/engine/logs/clang-post-selection-lld.{log,json}`.
- The repository-owned patch uses LLVM's existing `raw_string_ostream` and
  `format_hex_no_prefix(Val, 2)` only when `LLVM_WASTE_GUEST` is selected and
  threads are disabled. Native/threaded profiles retain the original standard
  streams. Function insertion, selected callbacks and signatures stay in the
  actual initializer.
- The observer includes the complete production source and invokes that
  initializer with real LLVM modules. No production test hook or relaxed access
  check is needed. All 47 functional assertions live in WAST: all 13 lowercase
  suffixes and zero padding, void/two-argument 32/64-bit signatures, declaration
  counts and reuse, repeated initialization, unpopulated slots, and copied names
  after all module/context/formatter owners are destroyed. The three cases
  cover Never, Runtime and Always stack-return modes and enabled scope
  poisoning. This accepts the declaration boundary, not the complete
  instrumentation pass or a guest sanitizer runtime.
- Complete production guest compilation and native/native-threaded/
  guest-threaded source checks pass. The observer is 2,336,093 bytes and has
  36 checked shared-libc function imports and no unresolved C++ function
  imports. Native Bash and ASan/UBSan each pass 47 assertions; the packaged
  worker passes 48/48 results including launch, with exit status zero. The
  installed WAST matches the authored source and corpus/package checks pass.
  See `clang-shadow-callbacks-{build,native,sanitize}.log`,
  `clang-shadow-callbacks-browser.json`, `clang-shadow-callbacks-link-audit.json`
  and `clang-shadow-callbacks-source-verification.json` under
  `build/engine/logs`.
- A further 300-second full graph run reaches Instrumentation at the 87%
  build marker with no compile error, then stops at its time limit. Its log
  includes one unused-variable warning in NumericalStabilitySanitizer. The
  generated component rule completes the remaining seven objects and links
  the Instrumentation archive. See
  `build/engine/logs/clang-shadow-callbacks-next-lld.{log,json}` and
  `clang-shadow-callbacks-instrumentation.{log,json}`. Build graph percentages
  do not measure overall compiler-port completion.
- All 68 members of the four new archives match their compiled production
  objects byte for byte. Archive sizes and SHA-256 hashes are recorded in
  `build/engine/logs/clang-shadow-callbacks-archive-audit.json`. A repeated
  Instrumentation component build compiles and links nothing; hashes and
  modification times of all 72 tracked objects/archives remain unchanged.
  See `clang-shadow-callbacks-cache-results.json` under the same directory.
- The `lld` link recipe names 64 LLVM archives: 47 are present and 17 absent.
  Of its 1,641 direct source objects, 200 are absent. Physical counts do not
  establish freshness of every earlier artifact or resolved imports. See
  `build/engine/logs/clang-shadow-callbacks-final-inventory.json`.

Reproduce with `make -C src/aux test-shadow-callbacks`. Source,
sanitizer, worker and manual steps are in
[Shadow callback declarations](../src/aux/clang/test/shadow-callbacks/README.md).
The user-supplied `build/engine/logs/shadow-callback-cli.txt` confirms the full
`test-shadow-callbacks` Make target: 36 libc imports verified, 47 PASS and zero
failures. The user reports exit status zero. Its hash and review are recorded
in `build/engine/logs/clang-shadow-callbacks-user-cli-evidence.json`.
Manual offline-browser acceptance for this fixture remains pending. Save
`shadow-callbacks-{results,status,test-exit}.txt` under `build/html-rt`; expect
47 PASS, zero failures, observer status 37 and WAST exit zero. GSYM manual
acceptance remains pending. At this checkpoint, the next build work was to
continue the full `lld` graph after Instrumentation. Full Clang/LLD linking,
import validation and compile-link-run acceptance were unfinished; no compiler
or linker executable was installed.

## Accepted SandboxIR and Vectorize archive checkpoint

- Resumed the actual `lld` graph with one compilation job and a 600-second
  limit. SandboxIR completed, and Vectorize reached the VPlan sources without
  a compiler error or warning. The main SLP vectorizer source completed during
  this run. The graph then stopped at its time limit while compiling
  `VPlanPredicator.cpp`; its last progress marker was 90%. See
  `build/engine/logs/clang-post-instrumentation-lld.{log,json}`. Build graph
  percentages do not measure overall compiler-port completion.
- The generated Vectorize component rule completed the remaining six objects
  and linked the archive in 131.55 seconds. See
  `build/engine/logs/clang-vectorize-component.{log,json}`. This checkpoint
  required no source adaptation.
- SandboxIR is 621,742 bytes with 15 archive members; Vectorize is 9,142,528
  bytes with 45 members. All 60 members match the compiled production objects
  named by their generated link recipes byte for byte. Sizes, SHA-256 hashes
  and recipe audits are in
  `build/engine/logs/clang-vectorize-archive-audit.json`. This accepts archive
  assembly, not execution of LLVM's optimization passes.
- Repeating both generated component rules compiles and links nothing. All 62
  tracked object/archive hashes and modification times remain unchanged, with
  no recipe override warnings. See
  `build/engine/logs/clang-vectorize-cache-results.json`.
- The `lld` link recipe names 64 LLVM archives: 49 are present and 15 absent.
  Of its 1,641 direct source objects, 140 are absent. Physical counts do not
  establish freshness of every earlier artifact or resolved imports. See
  `build/engine/logs/clang-vectorize-final-inventory.json`. The read-only LLVM
  submodule remains clean.

At this checkpoint, the next build work was to continue the full `lld` graph
after Vectorize. Full Clang/LLD linking, import validation and compile-link-run
acceptance were unfinished. Existing GSYM and shadow-callback manual browser
checks remained pending; this build-only checkpoint added no manual browser
check. No compiler or linker executable was installed.

## Accepted MemProf graph identifiers and IPO archive slice

- Resumed the full `lld` graph with one compilation job and a 600-second
  limit. LLVMLinker completed. IPO reached MemProfContextDisambiguation at
  the graph's 92% marker, where the node-ID helper used unavailable guest
  standard string streams. See
  `build/engine/logs/clang-post-vectorize-lld.{log,json}`. Graph percentages
  do not measure overall compiler-port completion.
- The repository-owned patch returns an owned `N0x...` identifier using
  LLVM's existing `utohexstr` and Twine helpers in the disabled-thread guest.
  Native/threaded source profiles retain the original standard streams.
  Production compilation keeps normal access checks; only the observer opens
  private access to call the actual helper from the complete source.
- All 34 functional assertions live in WAST. Both regular-LTO and ThinLTO
  graph specializations cover zero, small hexadecimal values, lowercase
  digits, high-bit and maximum 32-bit pointer values. Numeric boundary
  pointers are never dereferenced. Additional cases use real nodes, shared
  edges, sorted context IDs and clone attributes. WAST independently derives
  expected hex from captured addresses and checks copied names/attributes
  after all graph, node, edge and formatter owners are destroyed. This accepts
  graph formatting, not the MemProf optimization pass.
- The full production guest object and native/native-threaded/guest-threaded
  source checks pass. The observer is 273,443 bytes, with 33 checked shared-libc
  function imports and no unresolved C++ function imports. The agent ran
  `make -C src/aux test-memprof-format` through cli-rt automatically:
  34 PASS, zero failures and Make exit zero. ASan/UBSan also pass 34 assertions;
  the packaged worker passes 35/35 results including launch, with exit zero.
  The installed WAST matches its authored input and corpus/package checks pass.
  See `clang-memprof-format-build-cli.log`, `clang-memprof-format-sanitize.log`,
  `clang-memprof-format-browser.json`, `clang-memprof-format-link-audit.json`
  and `clang-memprof-format-source-verification.json` under
  `build/engine/logs`.
- The generated IPO component rule completes its remaining objects and links
  the full archive in 153.58 seconds. It reports one unused-variable warning
  for `OldName` in ThinLTOBitcodeWriter. See
  `build/engine/logs/clang-memprof-format-ipo.{log,json}`.
- All 49 members of the two new archives match their production objects byte
  for byte. Sizes and SHA-256 hashes are in
  `build/engine/logs/clang-memprof-format-archive-audit.json`. Repeating both
  generated component rules compiles and links nothing; all 51 tracked
  object/archive hashes and modification times remain unchanged. See
  `clang-memprof-format-cache-results.json` under the same directory.
- The `lld` recipe names 64 LLVM archives: 51 are present and 13 absent. Of
  its 1,641 direct source objects, 91 are absent. Physical counts do not
  establish freshness of every earlier artifact or resolved imports. See
  `build/engine/logs/clang-memprof-format-final-inventory.json`.

Reproduce through cli-rt with `make -C src/aux test-memprof-format`.
Complete source, sanitizer and worker commands are in
[MemProf graph identifiers](../src/aux/clang/test/memprof-format/README.md).
This diagnostic boundary is accepted through cli-rt and the production worker;
it adds no manual browser check. Existing GSYM and shadow-callback manual
browser checks remain pending. At this checkpoint, the next build work was to
continue the full `lld` graph after IPO. Full Clang/LLD linking, import
validation and compile-link-run acceptance were unfinished. No compiler or
linker executable was installed.

## Accepted LTO and WebAssembly support archive checkpoint

- Resumed the full `lld` graph with one compilation job and a 600-second
  limit. It completes ten LLVM archives without a compiler error or warning:
  Coroutines, CFGuard, HipStdPar, Passes, Plugins, LTO, MCDisassembler,
  WebAssemblyInfo, WebAssemblyDesc and WebAssemblyUtils. This checkpoint
  requires no source adaptation. See
  `build/engine/logs/clang-post-ipo-lld.{log,json}`.
- The run starts 56 source compilations and stops at its requested time limit
  while compiling `WebAssemblyCoalesceFeaturesAndStripAtomics.cpp`. Its
  object is absent, so this source will be retried. The last graph marker is
  95%; graph percentages do not measure overall compiler-port completion.
- All 43 members of the ten new archives match the compiled production
  objects named by their generated link recipes byte for byte. The audit
  records member counts, sizes and SHA-256 hashes in
  `build/engine/logs/clang-post-ipo-archive-audit.json`. This accepts archive
  assembly, not execution of LLVM passes, LTO or code generation.
- Repeating all ten generated component rules compiles and links nothing.
  All 53 tracked object/archive hashes and modification times remain
  unchanged, with no recipe override warnings. See
  `build/engine/logs/clang-post-ipo-cache-results.json`.
- The final `lld` recipe names 64 LLVM archives: 61 are present and three
  absent. Of its 1,641 direct source objects, 44 are absent: 41 of 53
  WebAssemblyCodeGen objects, both WebAssemblyAsmParser objects and the
  WebAssemblyDisassembler object. Physical counts do not establish freshness
  of every earlier artifact or resolved imports. See
  `build/engine/logs/clang-post-ipo-final-inventory.json`. The read-only LLVM
  submodule remains clean.

At this checkpoint, the next build work was to continue the full `lld` graph
at WebAssembly CodeGen, then complete the assembler/disassembler and final
link. This build-only checkpoint added no
manual browser check or new runtime regression; no runtime semantics changed.
Existing GSYM and shadow-callback manual browser checks remain pending.
Full Clang/LLD linking, import validation and native/browser compile-link-run
acceptance were unfinished. No compiler or linker executable was installed.

## WebAssembly archives complete; generated driver entry corrected

- The one-job full `lld` graph completes in 548.44 seconds without a compiler
  error or warning. All 64 LLVM archives named in its recipe are present, with
  none of its 1,641 direct source objects absent. This count excludes the four
  BLAKE3 object-library inputs and does not establish freshness of every older
  artifact. See `build/engine/logs/clang-post-webassembly-support-lld.{log,json}`
  and `clang-lld-complete-final-inventory.json` under the same directory.
- WebAssemblyCodeGen has 53 members, AsmParser two and Disassembler one.
  Together with LLD Common and Wasm, all 83 members of the five newly completed
  archives match the production objects named by their generated link recipes
  byte for byte. Repeating all five component rules compiles and links nothing;
  all 88 tracked object/archive hashes and modification times remain unchanged.
  See `clang-lld-complete-archive-audit.json` and
  `clang-lld-complete-cache-results.json` under `build/engine/logs`.
- The first nominally successful executable link produces only 29,642 bytes
  after libc rewriting. It imports `env:main`: the generated freestanding C++
  driver defines `_Z4mainiPPc`, while the shared C CRT references `main`.
  Dynamic unresolved-symbol linking retains that import and discards the real
  linker driver. Its zero C++ import count and 31 valid libc imports therefore
  do not establish startup or linker acceptance. The diagnostic record is
  `build/engine/logs/clang-lld-first-link-imports.json`.
- The repository-owned patch adds C linkage to the generated LLVM tool
  template's `main` only for `LLVM_WASTE_GUEST`. It covers the generated Clang
  and LLD entrypoints while preserving the native template. Zero-fuzz source
  staging succeeds, the real production LLD driver object defines `main`, and
  complete generated-driver source checks pass with warnings-as-errors for
  native, native-threaded and guest-threaded profiles. See
  `clang-driver-entry-stage.log` and `clang-driver-entry-source-checks.json`
  under `build/engine/logs`. This is a build/entry-symbol check; constructor
  behavior and execution still require runtime evidence.
- The corrected one-job graph rebuilds only the generated driver object, then
  reaches its 600-second limit during the final executable link. Make removes
  the unfinished output. See
  `build/engine/logs/clang-driver-entry-lld.{log,json}`. Before termination, a
  diagnostic copy is 55,930,935 bytes after libc rewriting: 87 libc function
  signatures verify, and it imports neither `main` nor C++ functions. These
  are provisional byte/import observations, not a completed-link claim.
- Retrying only the final compiler-driver link command with
  `-Wl,--threads=1` also reaches a 300-second limit. It uses 485,192 KiB peak
  RSS, 2.41 seconds of user CPU and 0.67 seconds of system CPU. This does not
  establish a one-thread workaround or a slow CPU-bound link; investigate the
  waiting child/process before extending the budget. See
  `build/engine/logs/clang-driver-entry-single-thread-link.{log,json}`.
  The one-thread option is diagnostic only and is not added to production
  build flags. The provisional rewritten file is retained at
  `build/aux/clang/diagnostics/lld-entry-provisional.wasm`, with provider and
  entrypoint observations in
  `build/engine/logs/clang-driver-entry-provisional-audit.json`.
- The agent attempts the version command through cli-rt automatically. It
  exits 126 before decoding because the input exceeds `NATIVE_EXEC_BYTES_MAX`,
  currently 16 MiB in `src/config.h`, shared by native and browser executable
  loading. No guest result JSON is created. See
  `build/engine/logs/clang-lld-version-cli.log`. The raw file has 28,753,712
  bytes in its code section and 24,511,714 in the custom name section; removing
  names alone will not fit the current limit. See
  `build/engine/logs/clang-driver-entry-sections.json`.

At this checkpoint, the next work was to diagnose the final-link wait, then
resolve artifact sizing/imports before startup acceptance. No runtime semantics
or resource limits changed in that slice. Full Clang building, compiler/linker
startup and native/browser compile-link-run acceptance were unfinished. No
compiler or linker executable was installed.

## Automatic optimizer wait resolved; executable budget and import frontier

- A bounded verbose link records the real process tree: Clang first waits on
  `wasm-ld`, then on `/usr/bin/wasm-opt`. At 30 and 60 seconds the optimizer is
  sleeping in `futex_do_wait`. The previous `--threads=1` option applied to
  wasm-ld, not this separate child. See
  `build/engine/logs/clang-lld-link-wait.{log,json}`. This identifies the waiting
  stage; it does not diagnose Binaryen's internal wait.
- The aux Makefile feature-detects `--no-wasm-opt` in the host C++ driver's
  help and adds it only to guest executable link flags when available. Hosts
  without the option get no extra flag. Existing `-Os` source compilation is
  retained; whole-module optimization is not an implicit prerequisite for
  producing the compiler tools. The configured final link exits zero in
  3.70 seconds, with 483,992 KiB peak RSS. See
  `clang-no-auto-opt-make-flag.log`, `clang-no-auto-opt-configure.json` and
  `clang-lld-no-auto-opt-direct.{log,json}` under `build/engine/logs`.
- The real generated LLD component rule exits zero. Repeating it compiles and
  links nothing; the two driver objects and raw executable retain identical
  hashes and modification times. This checks the final component, not the
  freshness of every transitive archive. See
  `build/engine/logs/clang-no-auto-opt-component{,-cache}.{log,json}`.
- The rewritten artifact is 55,930,935 bytes, with 87 verified shared-libc
  function imports, no imported `main` and no C++ function imports. All
  remaining env functions match the browser adapter policy except `log2f`;
  there are no adapter signature mismatches. See
  `build/engine/logs/clang-no-auto-opt-import-audit.json` and
  `clang-lld-no-auto-opt-import-check.log` under the same directory.
- `NATIVE_EXEC_BYTES_MAX` is now 64 MiB in `src/config.h`, consumed by both
  runtime loaders and generated frontend configuration. The overall VFS byte
  budget remains 128 MiB. Native applications and the browser engine rebuild;
  installed app configuration is explicitly refreshed before packaging.
  Native and ASan/UBSan application entrypoints each pass all 47 checks,
  including rejection of an input larger than the configured cap. The worker
  passes 35/35 MemProf regression results with exit zero. See
  `clang-exec-budget-{native-build,browser-build,entrypoints,package-refresh}.log`
  plus `clang-exec-budget-entrypoints-sanitize.log` and
  `clang-exec-budget-worker.json` under `build/engine/logs`. These checks
  do not establish large-linker execution in the browser.
- The agent runs the actual linker version command through cli-rt. It passes
  the byte guard and reaches import resolution, then exits 126 with
  `unresolved function import env.log2f`. The result JSON records this error
  without a timeout. Archive symbol inspection traces the import to
  `libLLVMTransformUtils.a:SimplifyLibCalls.cpp.obj`; the source's float
  `std::log2` overload is used for power simplification. See
  `clang-lld-no-auto-opt-cli.log`, `clang-lld-no-auto-opt-cli-results.json` and
  `clang-lld-log2f-callers.log` under `build/engine/logs`. This is a startup
  failure, not an accepted version/diagnostic test.
- The private LLVM `limits.h` includes `src/config.h` for the shared guest
  path bound. Updating the executable cap therefore invalidates LLVM source
  dependencies. A one-job 300-second graph refresh starts 66 compilations,
  completes four small archives and reaches LLVMCore without a compile error
  before its requested time limit. The full refresh remains incomplete; the
  presence of all 64 archives does not prove current dependencies are fresh.
  See `build/engine/logs/clang-no-auto-opt-lld.{log,json}`.

At this checkpoint, the next work was shared-libc `log2f` support and the
bounded dependency refresh before startup acceptance. Full Clang building and
native/browser compile-link-run acceptance were unfinished. No compiler or
linker executable was installed.

## Accepted shared binary32 logarithm; module capacity frontier

- Shared guest libc adds `float log2f(float)` to its public C/C++ header and
  PIC provider. It promotes the binary32 input exactly, calls the existing
  binary64 logarithm, then narrows once with Wasm nearest-even conversion.
  Domain/pole handling and errno behavior stay in the same implementation;
  no host math import or LLVM-private approximation is added. This is not a
  general correctly rounded transcendental guarantee.
- All functional assertions live in `src/aux/libc/tests/log2f.wast`: 60
  assertions include an independent WAST sweep of all 277 positive binary32
  powers of two, 32 references computed with 100-digit precision and checked
  within one binary32 ULP, normal/subnormal and range-reduction boundaries,
  both neighbors of one, signed-zero poles, negative-domain inputs,
  infinities and signaling/quiet NaNs. errno is checked independently of
  value results. See `build/engine/logs/clang-log2f-reference-cases.json`.
- The agent runs the full package through cli-rt Bash, both normally and
  with ASan/UBSan: 847 PASS, zero failures across 26 files in each run,
  including all 60 new assertions. The production packaged
  worker passes 61/61 results including launch, with exit zero. Installed WAST
  bytes match the authored source; corpus and packaging checks pass. The
  library's 38 compiled artifacts retain their hashes and modification
  times on a repeated build. See `clang-log2f-libc-native.log`,
  `clang-log2f-libc-sanitize.log`, `clang-log2f-browser.json`,
  `clang-log2f-package.log` and
  `clang-log2f-libc-cache.json` under `build/engine/logs`.
- The sanitizer package uses `WASTE_TEST_TIMEOUT_MS=180000` and disables only
  LeakSanitizer, which is unavailable under this environment's ptrace setup.
  ASan and UBSan halt on errors. Its earlier 60-second run passes the new
  math assertions but expires later in the existing binary128 wide-format
  case; the retained attempt is `clang-log2f-libc-sanitize-60s.log` under
  `build/engine/logs`. No production deadline changes.
- The C++ SDK probe verifies both the public `float (*)(float)` declaration
  and libc++'s actual `std::log2(float)` overload. It compiles against mounted
  headers with warnings-as-errors. Rewriting the existing linked LLD artifact
  now assigns 88 libc function imports; all provider and remaining adapter
  signatures match. There are no missing env function providers or C++
  function imports. See `clang-log2f-cxx-sdk.log`,
  `clang-log2f-link-rewrite.log` and `clang-log2f-import-audit.json` under
  `build/engine/logs`. This accepts the provider/ABI, not execution of LLVM's
  power simplification or refreshed compiler artifacts.
- The automatic cli-rt version attempt now passes import resolution and
  reaches engine decoding, then exits 126 with `too many types`. The artifact
  contains 230 ordinary type entries versus `EXEC_MAX_TYPES = 128` in
  `src/engine/runtime_internal.h`. It also has 90,685 defined and 111 imported
  functions (90,796 total), exceeding `EXEC_MAX_FUNCS = 8192`. These are
  measured module counts, not startup success. See
  `clang-log2f-lld-cli.log`, `clang-log2f-lld-cli-results.json` and
  `clang-log2f-lld-section-counts.json` under `build/engine/logs`.
- A one-job 600-second dependency refresh starts 268 compilations without a
  compiler error. Demangle, Support, TargetParser, BinaryFormat,
  BitstreamReader and Remarks complete; all 228 archive members match their
  production objects. Repeating the six component rules compiles and links
  nothing; all 234 tracked hashes and modification times remain unchanged.
  The graph stops at its requested time limit in LLVMCore's
  `Instructions.cpp`, at the 17% graph marker. The full refresh remains
  incomplete even though all 64 archives and 1,641 direct source objects are
  physically present. See `clang-log2f-refresh-lld.{log,json}`,
  `clang-log2f-refresh-{archive-audit,cache-results,final-inventory}.json`
  under `build/engine/logs`. Graph percentages do not measure compiler-port
  completion.

Reproduce the provider checks with `make -C src/aux test-libc`, or run
`wast --verbose /root/test/aux/libc/log2f.wast` in Bash. Browser-worker and
sanitizer commands are in [the libc package README](../src/aux/libc/README.md).
At this checkpoint, the next work was coordinated type/function capacity and
allocation review, followed by the dependency refresh and startup acceptance.
Full Clang building and native/browser compile-link-run acceptance were
unfinished. No compiler or linker was installed.

## Accepted module type capacity and recursive allocation slice

- `WASM_MAX_TYPES = 256` in `src/config.h` now supplies the WAT table,
  binary indexed-reference decoder, executor type cap and structural comparison
  workspace. This covers LLD's 230 types while fitting the existing eight-bit
  type-index payload. A compile-time representation guard prevents configuring
  a larger bound without changing that encoding. Public value-type codes stay
  unchanged.
- Binary type loading allocates the declared entry count rather than the full
  configured table. Recursive groups can expand that count; growth checks the
  expanded total against the shared bound, zero-initializes a new allocation
  and retains the old one until allocation succeeds. Ordinary LLD type storage
  is 230 slots, or 2,009,280 bytes on the measured layout. Source-independent
  decoded tables retain their existing immutable clone/reference ownership.
- Recursive WAT forward placeholders now use parser-only values outside all
  real reference ranges. They still fit the low 16 storage-type bits, leaving
  mutability and packing flags intact. Group close resolves only placeholders,
  preserving previously resolved references. This avoids aliasing a real type
  as the configured type table grows.
- All 15 portable assertions live in
  `tests/engine-regressions/type-capacity.wast`. They exercise high-index calls
  and null references, a full 256-entry ordinary table, single and cumulative
  recursive growth, count overflow, truncated groups, recovery after failures,
  nullable/non-null forward references, mutable fields and an earlier resolved
  reference in the same group. Cross-module equivalence traverses 230 separate
  dependent type groups, beyond the former comparison workspace bound.
- Native `wast` and `/bin/wast` inside native Bash both pass all 15 assertions;
  both also pass with ASan/UBSan. Leak detection is disabled for this environment's
  ptrace restriction; address/undefined-behavior checks remain active. The
  packaged production worker passes 16/16 results including launch, exit zero.
  Installed WAST bytes match the authored source and packaging/corpus checks
  pass. See `clang-type-capacity-{native,native-bash,sanitize,sanitize-bash}.log`,
  `clang-type-capacity-worker.json` and
  `clang-type-capacity-browser-build.log` under `build/engine/logs`.
- The focused official `type`, `type-rec`, `type-equivalence`, `call_ref` and
  `ref_func` files pass all 66 assertions normally and under ASan/UBSan. See
  `build/engine/logs/clang-type-capacity-core.log` and
  `clang-type-capacity-sanitize.log` under the same directory. These are language
  regressions, not compiler acceptance.
- The agent reruns the actual linked LLD version input through native and
  sanitized cli-rt. Its 230 types now load, then both attempts stop cleanly at
  `too many functions: 90685 exceeds 8192`, exit 126, without a timeout or
  sanitizer report. See `clang-type-capacity-lld-final-cli.log` and its
  `clang-type-capacity-lld-final-cli-results.json`, plus
  `clang-type-capacity-lld-sanitize.log` and
  `clang-type-capacity-lld-sanitize-results.json` under
  `build/engine/logs`. This uses the existing linked artifact as decoder input;
  it does not accept a freshly rebuilt compiler/linker.
- The function cap remains unchanged. Its fixed 2,048-entry local array uses
  8,192 bytes per defined function; 90,685 definitions require 742,891,520
  bytes (708.48 MiB) for those arrays alone. That exceeds the browser engine's
  512 MiB maximum before decoded instructions or other process state. The
  native metadata record is 8,216 bytes, and per-engine import/declaration
  arrays are also tied to the function cap. Measured layout is in
  `build/engine/logs/clang-type-capacity-sizes.json`.

Reproduce with `build/cli-rt/wast --verbose
tests/engine-regressions/type-capacity.wast`, or run
`wast --verbose /root/test/engine-regressions/type-capacity.wast` inside Bash.
The automated worker scenario is `src/vfs/root/test/guest-session/type-capacity.json`;
commands are in [the Clang system-test README](../src/aux/clang/test/README.md).
Next, scale per-function local/metadata and import/declaration storage, audit
combined function indexes, and measure decoded-instruction memory before
raising the function cap. Keep coordinated limits in shared configuration.
The full LLVM dependency refresh remains pending after the header changes;
resume it after the resource profile settles to avoid repeated invalidation.
Full Clang building and compiler/linker native/browser compile-link-run
acceptance remain unfinished. No new manual browser check is required for this
slice; existing manual checks remain pending. No compiler or linker is installed.


## Decoded function local storage slice

The next function-capacity prerequisite is implemented without raising the
8,192-function bound. `exec_func` now owns an exact-sized local-type array
rather than embedding 2,048 slots. The loader first scans local declaration
groups through a copied bounded reader, validating types even for zero-count
groups and checking cumulative counts before allocation. It then replays those
validated declarations into the array. A function with no declared locals
allocates none. The shared decoded function table owns these immutable arrays;
cloned engines retain them through the existing static reference count, and
only the last owner frees them, including after failed body loads.

The audit also found that separately bounded parameters and locals could exceed
the validator's initialization bitmap. Both the loader and the validator now
check their combined total before allocation or bitmap access. Live evaluator
frames, yield/resume state and the existing process scheduler remain unchanged.

Measured against the current diagnostic `build/aux/clang/wasm-ld.wasm`:

- Its 90,685 defined functions declare 286,336 locals in total; 17,689 functions
  declare none, and the largest declaration has 100 locals.
- Local-type arrays require 1,145,344 bytes (1.09 MiB), replacing 742,891,520
  bytes (708.48 MiB). On the measured native layout, each function record falls
  from 8,216 to 40 bytes; records plus local types total 4,772,744 bytes
  (4.55 MiB), excluding allocator overhead and instructions.
- The instruction decoder still reserves one instruction record for each
  remaining body byte plus one per function. This artifact would reserve
  28,411,480 records. At 104 bytes per native record, that is 2,954,793,920
  bytes (2.75 GiB), so raising the function cap remains unsafe. Browser pointer
  sizes differ, but the current strategy still needs replacement before loading
  this module under the 512 MiB browser ceiling.

The portable `tests/engine-regressions/local-storage.wast` adds 16 assertions
covering zero and mixed local groups, maximum declarations, the combined frame
boundary, cumulative and u32 overflow, malformed zero-count types, truncated
loads after allocation, invalid local access and recovery after failures.
Native cli-rt and native Bash pass all 16. Four official local-variable files
(`local_get`, `local_set`, `local_tee`, `local_init`) pass all 192 assertions;
the prior type-capacity regression still passes all 15. The rebuilt production
browser worker passes 17/17 results including launch using
`src/vfs/root/test/guest-session/local-storage.json`. Installed snapshots and provenance
checks pass. ASan/UBSan passes all 223 assertions across the new regression,
the four official files and the type-capacity regression. Native and sanitizer
Bash each also run two regression children: 32/32 assertions, two forks, two
execs and two reaped children, exit 0. This exercises decoded metadata shared
through the existing fork graph. Leak detection is disabled for the restricted
native test environment; no leak-checking claim is made.

Evidence is under `build/engine/logs/clang-local-storage-*`, including the actual
LLD declaration inventory, native structure sizes and worker results. The
aggregate `clang-local-storage-verification.json` records the accepted checks.
The real LLD diagnostic still returns exit 126 at
`too many functions: 90685 exceeds 8192` in both normal and sanitizer runs,
without a timeout or sanitizer report; no linker startup is claimed.
`git diff --check`, `bash -n start.sh` and authored/installed byte comparison
pass; the LLVM and wasm-spec submodules remain clean.

Next, replace excessive decoded-instruction storage, scale import/declaration
metadata and check combined imported/defined function indexes before raising
the shared function bound. The full LLVM header dependency refresh remains
pending until the resource profile settles; this slice does not rebuild LLVM
or install a compiler/linker. No additional manual browser run is needed here;
older outstanding manual acceptance checks remain pending.


## Decoded instruction buffer growth slice

Instruction decoding now allocates by decoded count rather than reserving one
104-byte native record for each byte in the function body. The buffer starts
small, grows geometrically within the original instruction-byte upper bound,
and checks allocation-size arithmetic. Growth occurs before decoding the next
instruction, so allocation failure frees all completed instructions and cannot
strand the current instruction's heap payload. Open-control references remain
indexes through relocation. Each appended record is initialized explicitly.

After decoding, the loader attempts to shrink to the actual count before
validation. If shrinking fails, it retains the valid allocation; capacity is
already bounded by geometric growth. Branch-table arrays and exception catch
arrays keep their existing ownership through relocation, failed body loads,
validation and the shared immutable function table used by cloned engines.
Execution, yield/resume and scheduling use the existing decoded representation.

A native allocator probe loaded a module with 1,000 `f64.const`/`drop` pairs
and a final `end`: 10,001 instruction bytes decode to 2,001 records. The
production loader retains 208,104 allocator-usable bytes for those records,
compared with the old 1,040,208-byte reservation (10,002 records). This measures
retained instruction storage on the native allocator; it excludes transient
reallocation peaks, module declarations and allocator overhead elsewhere.
Shrinking is best effort, and no exact-allocation guarantee is imposed on all
allocators.

Validation:

- `tests/engine-regressions/instruction-storage.wast`: 26 portable assertions
  covering counts around growth boundaries, if/else target relocation,
  branch-table and catch payload ownership, wide integer/float/SIMD immediates,
  truncated bodies after growth, empty instruction streams and recovery.
- Four official files (`if`, `br_table`, `exceptions/try_table`,
  `simd/simd_const`) pass 931 assertions. Existing control-depth, local-storage
  and type-capacity regressions pass 41 more. With the new regression, all
  998 assertions pass normally and under ASan/UBSan.
- Native and sanitizer Bash each run two copies of the new regression:
  52/52 assertions, two forks, two execs and two reaped children, exit 0.
- The rebuilt packaged production worker passes 27/27 results including launch
  using `src/vfs/root/test/guest-session/instruction-storage.json`. Installed snapshot
  bytes, corpus/provenance checks, `git diff --check` and `bash -n start.sh`
  pass. LLVM and wasm-spec submodules remain clean. Leak detection is disabled
  in the restricted sanitizer environment.

Evidence is under `build/engine/logs/clang-instruction-storage-*`:
`verification.json` records aggregate results and artifact hashes, `measure.c`
and `measure.json` document the native allocation probe, and the remaining
logs hold native, sanitizer, fork, worker and build results.

The real LLD diagnostic still stops at
`too many functions: 90685 exceeds 8192`, exit 126, in both normal and
sanitizer runs without a timeout or sanitizer report. The previous 2.75 GiB
projection described byte-based reservation, which this slice removes; it is
not a measurement of the new LLD decoded footprint. Records remain 104 bytes
on the native layout. Compact their representation and establish total decoded
storage, scale import/declaration metadata, and check combined function indexes
before raising the function cap. No claim is made that LLD fits the browser's
512 MiB ceiling or starts successfully yet.

The full LLVM dependency refresh remains deferred until the resource profile
settles. No LLVM rebuild or compiler/linker installation occurs in this slice.
No new manual browser check is needed; existing outstanding manual checks
remain pending.


## Compact decoded instruction payload slice

Decoded instructions now use an opcode-selected union for mutually exclusive
immediate payloads. Memory offsets/indexes/alignment/lanes, block/catch metadata,
GC heap types/cast flags and scalar/vector bit patterns share storage. Common
opcode/subopcode/index fields and all validation caches remain separate. The
measured record size falls from 104 to 56 bytes on native (46.15%) and from 96
to 56 bytes on Wasm32 (41.67%). The Wasm32 size probe uses the production
compiler target, include paths and runtime profile and executes its exported
size functions in WebAssembly.

The ownership and initialization changes are part of this representation:

- Initialize the block-type sentinel only for block/loop/if/try_table, and keep
  block parameter/result counts in explicit control fields rather than vector
  bytes. Indexed and single-result block metadata stays together with catches.
- Give GC casts and cast branches dedicated heap/target/flag fields; they no
  longer borrow block-type and memory/lane fields across union payloads.
- Gate catch-pointer cleanup on opcode `0x1f` in both failed-body cleanup and
  final instance destruction. Scalar/vector/memory operands must never be read
  as an owned pointer. Branch-table ownership remains selected by `br_table`.
- Pass a lane to the generic SIMD helper only for lane operations, and a vector
  immediate only for shuffle. The decoder and validator select the same
  payload as execution; validation caches remain outside the union.

The existing 2,001-instruction allocation benchmark now retains 112,056
allocator-usable bytes, down from 208,104 with the previous 104-byte record.
Its original byte-based reservation was 1,040,208 bytes. This is retained native
instruction storage only; allocator overhead elsewhere and reallocation peaks
are excluded. There is no change to instruction semantics, live evaluator
frames, explicit yields, process scheduling or shared decoded-code ownership.

`tests/engine-regressions/instruction-layout.wast` adds 16 portable assertions:
block signatures and cached calls, scalar/vector constants, memory offsets,
SIMD extraction/shuffle and load/store lanes, bulk copying, GC tests/casts and
cast branches including nullable references. Repeated modules use high-bit
vector/scalar patterns to catch accidental pointer interpretation on teardown.

All 10,631 assertions across this test, four retained regressions and 17
focused official files pass natively. The official coverage includes control
flow/exceptions, GC structs/arrays/casts, bulk and multi-memory copying,
memory64, and SIMD constants/lanes/load/store. Native Bash runs two copies of
the new regression with 32/32 assertions, two forks/execs and two reaped
children, exit 0. The rebuilt production worker passes 17/17 new-layout results
and 27/27 instruction-growth/exception results, including both launches.
Installed snapshots and corpus/provenance checks pass. ASan/UBSan passes the
same 10,631 assertions with identical per-file counts, and the sanitizer Bash
fork run passes all 32 assertions with both children reaped. The retained
private `wasm-validation` sanitizer gate also passes. Leak detection is disabled
for the restricted native environment; no leak-checking claim is made.

Evidence lives under `build/engine/logs/clang-instruction-layout-*`, including
native/Wasm32 size probes, the allocation benchmark, the exact selected-file
list and per-file counts, build logs, fork results and both worker scenarios.
`verification.json` records the accepted checks and source/artifact hashes.
`git diff --check`, `bash -n start.sh` and installed byte comparisons pass;
LLVM and wasm-spec submodules remain clean. The real LLD input still returns
exit 126 at `too many functions: 90685 exceeds 8192` in both normal and
sanitizer runs, without a timeout or sanitizer report; the function cap is
deliberately unchanged. This accepts the compact record representation, not
LLD startup or its complete decoded memory footprint.

Next, scale import/declaration storage and audit combined imported/defined
function indexes, then establish total decoded LLD memory before raising the
function cap. Further compaction can be evaluated against that measurement.
The full LLVM dependency refresh remains deferred until the resource profile
settles; no LLVM rebuild or compiler/linker installation occurs here. No new
manual browser run is required; earlier manual acceptance items remain pending.


## Function import and declaration metadata slice

Function bindings and declaration flags now allocate actual module counts
instead of embedding four 8,192-slot import arrays and a fixed 8,192-byte
declaration map in every engine. `exec_function_import` groups the host
function, host-data pointer, type index and control kind in a 24-byte native
record. Pre-count only function imports; table, memory, global and tag imports
retain their separate index spaces. Empty modules allocate neither table.

The declaration map covers imported plus defined function indexes. Allocate
its imported prefix even when the module has no function section, grow it with
checked addition when definitions arrive, and zero the added suffix. Export,
global-initializer, element and instruction-validation paths already check
combined indexes; their storage now matches those checks. The old fixed map
could be indexed beyond 8,191 even with each category under its existing limit.
Undeclared references still fail validation after growth.

Import bindings and the declaration map are immutable after load and share the
existing decoded-table reference count across cloned engines. Free them only
with the last static owner, including partially resolved/failed loads. Host-data
pointees keep their prior external ownership. A native diagnostic loads 8,192
imports plus 8,192 definitions, clones the engine, frees the original first,
and still obtains 42 from direct and indirect calls through the clone. The
clone shares both metadata allocations; an empty module keeps both pointers
null. The fixed native engine structure is 168,984 bytes instead of 373,768
(204,784 fewer bytes); actual binding/declaration allocations are additional.

`src/config.h` now defines `WASM_MAX_FUNCTIONS` (binary definitions) and
`WASM_MAX_IMPORTS` (all import declarations). The executor and binary import
decoder consume those shared values. Both remain 8,192; this slice preserves
the separate limits rather than introducing a combined 8,192-function limit.
The WAT parser's construction bound remains separate, so high-index regressions
use literal binary modules inside WAST.

The portable `tests/engine-regressions/function-metadata.wast` adds 13
assertions covering mixed import kinds, import-only modules with absent/empty
function sections, 8,193 and 16,384 combined functions, and indexes through
16,383 in exports/globals/elements/ref.func. It also checks undeclared high
references, partial unresolved imports, invalid types/out-of-range indexes and
recovery. No runtime fixture generation is required.

Native acceptance passes all 728 assertions across the new test, three
retained regressions and eight official import/export/reference/element/global/
call/start files. Native Bash runs two copies: 26/26 assertions, two forks,
two execs and two reaped children, exit 0. The rebuilt packaged production
worker passes 14/14 results including launch using
`src/vfs/root/test/guest-session/function-metadata.json`. Installed snapshots and
corpus/provenance checks pass. ASan/UBSan passes the same 728 assertions with
identical per-file counts, the 26-assertion Bash fork case and the native
metadata/clone ownership probe. The probe confirms calls through shared
bindings remain valid after freeing the original engine first. Leak detection
is disabled in the restricted environment; no leak-checking claim is made.

Evidence is under `build/engine/logs/clang-function-metadata-*`, including the
selected files/counts, native sizing/ownership probe, large valid diagnostic
binary, build logs, fork results and browser worker results. The aggregate
`verification.json` records accepted checks and source/artifact hashes.
`git diff --check`, `bash -n start.sh` and installed snapshot byte comparison
pass, and LLVM/wasm-spec submodules remain clean. The real LLD input still stops
at `too many functions: 90685 exceeds 8192`, exit 126, in both normal and
sanitizer runs without a timeout or sanitizer report.

Next, establish the full decoded LLD footprint with controlled memory use
before raising the definition limit. Function binding/declaration storage and
the combined index audit are now complete prerequisites, rather than pending
fixed-array conversions. The full LLVM dependency refresh remains deferred
until the resource profile settles. No LLVM rebuild or compiler/linker install
occurs here. No additional manual browser test is needed; earlier manual
acceptance items remain pending.


## Streaming decoded-footprint measurement slice

The private native `module-footprint` diagnostic measures function storage with
controlled live memory. Build it with `make -C src/cli-rt module-footprint`;
`module-footprint-sanitize` builds the instrumented version. It uses the shared
body decoder, accumulates allocation requests and releases each completed
function immediately through the same disposal helper as final instance
teardown. It does not resolve imports, validate function semantics, instantiate
memories/tables/globals or execute a start function, and returns no runnable
engine. Other section contents are skipped or read only for counts. JSON keeps
`validated`, `instantiated` and `executed` explicitly false: `complete` means
all function bodies decoded, not compiler/linker startup acceptance.

Inspection can count definitions beyond the execution cap. Before allocating
function metadata, the shared reader now checks that the count fits the
available type-index bytes. Ordinary loading still uses the configured 8,192
definition limit. The diagnostic reads at most the shared executable-byte limit
and was run with an external 512 MiB address-space limit and 90-second timeout.
No LLVM rebuild or compiler/linker installation was involved.

The current 55,930,936-byte `build/aux/clang/wasm-ld.wasm` measured:

| Component | Result |
| --- | ---: |
| Defined/imported functions | 90,685 / 111 |
| Completed function bodies | 90,685 |
| Decoded instructions | 12,271,818 |
| Encoded instruction bytes (excluding local declarations) | 28,320,795 |
| Retained instruction array requests at 56 bytes/record | 687,221,808 bytes (655.39 MiB) |
| Declared local-type arrays | 1,145,344 bytes |
| Branch-table/catch payloads | 258,244 bytes |
| Common metadata lower bound | 5,899,124 bytes |
| Combined decoded-storage lower bound | 694,524,520 bytes (662.35 MiB) |
| Largest single-function buffer-growth bound | 6,111,448 bytes, definition index 11,018 |
| Native wall time / peak RSS | 1.08 seconds / 111,720 KiB (109.10 MiB) |

The array/payload figures are allocation requests, not allocator-usable sizes.
The single-function bound conservatively counts old and new buffers during
reallocation. Common metadata omits allocator overhead/slack, exports, guest
memories/tables/data, source bytes, VFS and runtime state. It projects ordinary
retained metadata even though inspection does not allocate imported bindings
or instantiate objects. The native RSS belongs to the streaming diagnostic;
it is not an estimate of an eager runtime load. Native and sanitizer inspection
produce identical counts and byte figures.

The instruction arrays alone exceed the browser's 512 MiB ceiling. A 48-byte
record would still need 561.76 MiB; 40-byte records need 468.13 MiB before all
other allocations, and 32-byte records need 374.51 MiB. These are arithmetic
scenarios, not implemented layouts or browser acceptance. Raising the function
limit or removing only the redundant validation-cache fields is insufficient
at the current memory budget.

The shared body decoder also now requires an actual outer function terminator
and exact body-reader consumption. It rejects trailing instructions/extra ends,
a missing outer end even after an inner block closes, and open blocks. The
portable `tests/engine-regressions/function-envelope.wast` adds eight assertions
for those cases, recovery, ordinary semantic stack rejection and start traps.
It confirms that decode-only inspection does not weaken normal loading.

Acceptance:

- All 924 assertions across six portable regressions and seven official binary,
  LEB, start/function and control/exception files pass normally and under
  ASan/UBSan with identical per-file counts.
- Native and sanitizer Bash each run two copies of the new regression:
  16/16 assertions, two forks, two execs and two reaped children, exit 0.
- The rebuilt production worker passes 9/9 results including launch using
  `src/vfs/root/test/guest-session/function-envelope.json`. Installed bytes and the
  corpus/provenance audit pass.
- Eight diagnostic scope/payload inputs agree normally and under sanitizers:
  a trapping start is not executed, a semantic stack error is deliberately not
  validated, incomplete/trailing bodies and truncated function types fail, and
  branch/catch/local storage is counted correctly. All real LLD body counts
  and storage figures match across normal and sanitized inspection.
- Ordinary LLD execution still returns exit 126 at
  `too many functions: 90685 exceeds 8192` in both builds, without timeout or
  sanitizer report. `git diff --check`, `bash -n start.sh`, snapshot comparison
  and clean LLVM/wasm-spec submodule checks pass. Leak detection is disabled in
  the restricted sanitizer environment; no leak-checking claim is made.

Evidence is under `build/engine/logs/clang-module-footprint-*`:
`verification.json` records accepted checks and source/artifact hashes,
`lld.json` and `lld-summary.json` hold measured figures/scenarios, and
`lld-resources.json` records the bounded native run. The remaining files cover
scope inputs, native/sanitizer runs, builds, fork results and browser evidence.
See `docs/techniques.md` for diagnostic usage and limits.

Next, reduce retained decoded code against this measured budget before changing
the definition limit. Evaluate streaming full validation while retaining
immutable encoded bodies and decoding on demand with bounded code retention.
Keep code referenced by active or yielded frames alive across fork and thread
scheduling; preserve shared immutable ownership and imported-memory semantics.
An unbounded lazy cache would merely postpone the same memory failure. Revisit
record compaction against the measured totals, not an assumed fit. The full
LLVM dependency refresh remains deferred until resource configuration settles,
and full native/browser compile-link-run acceptance remains unfinished. No new
manual browser test is required; earlier manual acceptance items remain pending.

## Streamed function-validation slice

The private footprint tool now accepts `--validate-functions`. It uses the
ordinary loader and per-body semantic validator, retains all function signatures
and declarations, then disposes each successfully checked body's instructions,
locals and opcode-owned payloads before reading the next. A failure identifies
the defined-function index and leaves counters at the completed prefix. Ordinary
execution remains eager and keeps the configured 8,192-definition limit.

Analysis binds function imports to inert callbacks. Imported memories, tables,
globals and tags receive private objects matching their declaration types and
limits; no real runtime provider or external state is used. This handles LLD's
111 functions, one memory, one table and seven mutable globals. Private memory
uses normal ownership flags; private tables stay in the owned cleanup range.
Imported non-nullable reference globals return unsupported. Start signatures
are checked but start functions never run, and analysis returns no engine.

The diagnostic now reports `validationRequested` and `validated` separately.
`runnableInstance` replaces the earlier `instantiated` field because validation
can initialize private module objects; `runnableInstance` and `executed` are
always false. `validated: true` describes function/loader validation. Placeholder
values and ignored instantiation traps do not establish real import
compatibility, successful instantiation or linker startup.

On the existing 55,930,936-byte rewritten LLD artifact, all 90,685 defined bodies
pass the ordinary validator. The bounded native run takes 11.16 seconds with
111,540 KiB (108.93 MiB) peak RSS, a 512 MiB address-space limit and a 90-second
timeout. Counts exactly match the earlier decode-only measurement: 12,271,818
instructions, 687,221,808 instruction bytes, 1,145,344 local bytes and 258,244
payload bytes. Its conservative single-body allocation bound remains 6,111,448
bytes. This demonstrates streamed body validation with limited live code;
it does not establish a runnable browser memory budget.

The next implementation step remains bounded execution-code retention over
immutable encoded bodies. Preserve full unused-body validation, pin code for
active and yielded frames, and define eviction and clone/thread ownership before
raising the definition cap. An unbounded lazy cache still accumulates the
662.35 MiB eager lower bound. The LLVM dependency refresh, real linker startup,
link acceptance and compiler installation remain pending.

Validation for this slice:

- All 930 assertions across seven portable regressions and seven official
  binary/control/exception files pass natively and with ASan/UBSan.
- Foreground/background WAST children in installed Bash pass 12 assertions in
  each native mode, with two forks, execs and child exits each.
- The packaged production worker passes 7/7 results: six portable assertions
  plus launch. Installed regression bytes match the authored source, and corpus
  refresh/audit passes.
- Full LLD streamed validation passes normally and with ASan/UBSan. Storage
  counts agree exactly between modes and with the previous decode-only probe.
- Nine diagnostic checks cover valid bodies, first/later invalid unused bodies,
  trapping/import-calling starts, a memory import, all five import kinds with
  active element/data initialization, partial-failure cleanup and decode-only
  acceptance of an invalid stack. No diagnostic executes a guest function.
- Ordinary LLD execution still exits 126 with the configured definition-limit
  error in both native modes. No limit increase, LLVM rebuild or compiler/linker
  install occurs. ASan leak detection is disabled; no leak-check claim is made.
- Both relevant submodules remain clean; `git diff --check` and `bash -n start.sh`
  pass. No manual browser run is needed for this slice.

Evidence lives under `build/engine/logs/clang-stream-validation-*`.
`verification.json` records acceptance and hashes; `lld.json` and
`lld-resources.json` capture the isolated bounded native probe. The native and
sanitizer logs cover all assertions, Bash children, diagnostic inputs and the
unchanged execution cap; `worker.json` records packaged-worker results.

## 2 GiB runtime budget and eager LLD startup

The 512 MiB browser maximum was a repository-selected linker setting. It is
now 2 GiB through `HTML_RUNTIME_MAX_MEMORY_BYTES` in `src/config.h`, read by
both production and direct-start-probe linker rules. Initial memory stays
64 MiB. The separate `WASM_MAX_FUNCTIONS` bound is now 131,072 so the already
validated 90,685-definition LLD artifact can reach ordinary execution; the
8,192-import bound is unchanged. Earlier slices describing a mandatory
512 MiB budget and requiring eviction before raising the function cap are
superseded by this decision.

Ordinary cli-rt eager loading now runs the actual linker artifact with
`-flavor wasm --version`, prints LLD 24.0.0 and exits 0. The isolated native
run takes 17.52 seconds, with 942,036 KiB (919.96 MiB) peak RSS under a 2 GiB
address-space cap and without a timeout. The `.wasm`-suffixed build filename
requires explicit driver selection; the eventual installed `wasm-ld` name
will supply it through `argv[0]`. This accepts startup of the existing artifact;
it does not establish rebuilt dependency freshness or real link acceptance.

The rebuilt browser module declares a 2,147,483,648-byte maximum. A private
production-module allocation probe starts at 67,108,864 bytes, allocates a
629,145,600-byte block, grows memory to 656,801,792 bytes and checks writes at
both ends before freeing it. The packaged regression worker passes 7/7 results.
However, the actual LLD session in the production worker exceeds an external
120-second timeout with no completed result or stderr diagnostic. Raising the
memory ceiling is verified; browser LLD startup is not accepted. Investigate
load/allocation time separately from native peak RSS before drawing a browser
working-set conclusion. No GUI browser or Chromium is launched.

Lazy function loading means retaining validated encoded bodies and decoding a
function only when it is first called. The simplest policy retains that decoded
code until the shared code owner is released. A bounded cache adds eviction of
previously decoded bodies, requiring active/yielded-frame pinning and explicit
clone/thread ownership. Defer eviction until real working-set measurements
justify it. The earlier 662.35 MiB lower bound describes decoded functions in
the linker executable; guest archive-member selection is a separate linker
responsibility. LLVM code generation is already configured for WebAssembly.
No lazy loader or eviction mechanism is implemented in this slice.

Continue native link acceptance and investigate browser load-time performance
with the 2 GiB budget. The full LLVM dependency refresh remains necessary after
configuration changes; no compiler/linker is installed by this slice.

Checks and evidence:

- Native and ASan/UBSan each pass all 930 focused WAST assertions. The actual
  LLD version command exits 0 in both modes. The instrumented run needs a
  120-second guest deadline and completes in 66.10 seconds; its initial
  60-second attempt expires normally. ASan shadow mappings require omitting
  the native address-space cap; leak detection is disabled.
- The production-module growth probe, 7/7 packaged-worker regression results,
  corpus refresh/audit, shared-config parser checks, `bash -n start.sh`,
  `git diff --check` and clean LLVM/wasm-spec submodule checks pass.
- `build/engine/logs/clang-2gib-verification.json` records the accepted
  configuration/native-startup scope, hashes and the separate browser timeout.
  Adjacent `native-lld-*`, `sanitize-lld-*`, `memory-check.json`,
  `regression-worker.json` and `lld-worker-status.json` retain the evidence.

## Native object/archive link and heap/mapping separation

`make -C src/aux test-clang-lld-link` now exercises the real guest LLD rather
than a helper fixture or version command. Host Clang emits a freestanding entry
object and an answer object, and host `llvm-ar` puts the answer object in an
archive. Installed Bash stages the existing guest `wasm-ld`, links the object
against the archive, marks the emitted module executable, and runs it. The
result calls shared libc exit with status 37. All ten runtime expectations live
in `src/vfs/root/test/system/clang/lld-link/link.wast`: link/chmod statuses, execution
status and the exact Wasm magic/version bytes. Sources, orchestration and Make
rules live beside that WAST file; each run keeps its evidence under a fresh
`build/system-tests/clang/lld-link/run-*` directory. The build-only fixture is
cached on repetition and does not install or rebuild LLVM.

The first real link exposed an allocator fault hidden behind process exit 127.
`--trace-process` identifies an unmapped write through `libc.malloc`. Automatic
file mappings previously advanced `linear_pages`, the Wasm-visible
`memory.size`, although they did not commit a contiguous allocator heap. A later
allocation could therefore skip growth and write into a mapped file or a hole
left by unmapping it. Increasing the interpreter's own memory ceiling does not
repair this guest address-space bookkeeping bug.

Automatic mappings now first reuse explicit holes inside the current linear
extent, then search high free pages below the existing signed-pointer mmap ABI
ceiling. They leave `memory.size` unchanged. Linear growth checks for overlap
with existing virtual mappings before mutating state and fails cleanly rather
than replacing mapped data. The nine-assertion `mmap-heap.wast` regression
checks unchanged heap extent, mapping bytes across growth, rejection without
mutation, unmapping and subsequent heap writes. It uses imported process memory
and derives expectations from the actual starting page count.

The packaged Bash probe also exposed missing process-engine context in WAST
handlers: committing a WAST exec clears its executable engine, so mapping
imports had no active memory target. Each assertion now binds its engine to the
active capsule for the invocation and restores the previous binding afterward.
Owned memory uses the existing process-memory binding helper. Handler cursors,
explicit evaluator continuation and nested-process restrictions remain governed
by their existing paths; no recursive process driver or new runtime ABI is
introduced.

The native object/archive link and emitted-module execution pass with the
existing LLD artifact. This is not guest Clang, full SDK/CRT linking, LTO,
negative-link diagnostics, refreshed LLVM dependencies or browser linker
acceptance. The earlier real browser-worker startup timeout remains open; this
slice checks the shared VM fix through browser WAST instead of repeating that
large startup probe. No compiler/linker is installed and no submodule is edited.

Acceptance for this slice:

- The real object/archive link and emitted-module execution pass all 10 WAST
  assertions in installed native Bash, both normally and under ASan/UBSan.
  Each run reports four forks, execs and child exits, no handler failure and
  exit 0. Repeating `lld-link-fixture` recompiles nothing.
- All 939 focused assertions across 15 portable/official files pass in native
  and sanitizer builds. Parallel Bash WAST children pass 18 assertions in each
  mode, with independent process mapping state and both children reaped.
- The packaged production worker passes 10/10 results for the nine-assertion
  shared-memory regression plus launch. The older file/shared/protection mmap
  fixture passes all 23 assertions through the browser worker and both native
  corpus modes with its required assets. The DIY group has four passing files
  and its existing browser-backend-only skip in both native modes.
- The existing packaged handler session completes all 17 results, including
  its one intentional mismatch, and preserves read/select continuation,
  expected failure reporting, recovery and the final exit status.
- Private sanitizer gates pass 132 process-lifecycle and 32 checkpoint checks.
  Corpus refresh/audit, authored/snapshot byte comparison, shell syntax and
  whitespace checks pass. Relevant submodules remain clean. ASan leak checking
  is disabled; no additional manual browser run is required for the VM fix.

Evidence is under `build/engine/logs/clang-lld-link-*`; `verification.json`
records the accepted scope, source/artifact hashes and real linker results.
`native-final.log` and `sanitize-final.log` name the per-run directories with
assertion-aware results. The process trace records the original allocator
fault; early standalone mmap failures without file assets and the initial
handler-context failures are diagnostic history, not accepted results.

## Separate non-LTO linker trial

LTO is link-time optimization: a compiler emits LLVM bitcode with `-flto`,
and the linker runs LLVM optimization and code generation before ordinary
object linking. Per-file `-O1`, `-O2` and `-Os` compilation still works without
LTO. This experiment removes LTO *support from the linker executable*; it does
not merely turn off optimization while building LLD itself.

`make -C src/aux clang-lld-no-lto` builds the separate
`build/aux/clang/wasm-ld-no-lto.wasm`. Its private source overlay applies
`src/aux/clang/lld-no-lto.patch` to the existing staged LLD Wasm sources. It
omits `LTO.cpp`, backend initialization and bitcode compilation, then links the
remaining 13 translation units against the existing driver/common objects and
LLVM archives. All private compilations explicitly target Wasm. Normal archive
extraction and section collection discard the now-unreferenced optimizer and
backend code; no lazy loader, plugin ABI or engine change is needed. Direct
and archived bitcode fail with a useful request to compile without `-flto`.

| Measurement | Original LTO-enabled LLD | Non-LTO LLD |
| --- | ---: | ---: |
| Wasm bytes | 55,930,936 | 6,974,801 |
| Defined functions | 90,685 | 12,186 |
| Decoded instructions | 12,271,818 | 1,420,572 |
| Instruction-array bytes | 687,221,808 | 79,552,032 |
| Instruction arrays (MiB) | 655.39 | 75.87 |
| Native version-command peak RSS | about 920 MiB | 144.19 MiB |

Instruction storage falls by 88.4%. The non-LTO version command completes in
2.60 seconds with exit 0 under a 512 MiB native address-space cap. The footprint
probe decodes every function but does not itself instantiate or execute the
linker. The startup RSS measurement covers the actual version command, not
full SDK compilation or a browser heap budget. Generic object readers still
retain IR/Core and BitReader code; this does not remove every LLVM IR utility.
The map contains no SelectionDAG, Vectorize, LTO or Passes functions.

`make -C src/aux test-lld-link-no-lto` reuses the real object/archive
link fixture. Its original ten WAST assertions verify successful linking, the
Wasm header, executable permissions and execution with status 37. Six new
assertions in `no-lto.wast` verify exit status, explanatory diagnostics and no
output file for direct and archived bitcode. All 16 pass in native and
ASan/UBSan Bash; leak detection is disabled. The preserved original linker
still passes the ten positive assertions. Source assertions and orchestration
stay under `src/aux/clang/test/lld-link`; snapshots are refreshed and
audited through the ordinary corpus workflow.

The target reuses the existing configured archives and is deliberately separate
from the install targets. It does not refresh the full LLVM dependency graph,
install a compiler/linker, implement successful LTO, or establish guest Clang
startup and SDK/CRT acceptance. Checked-out submodules and the original staged
LLD sources remain unchanged. The baseline artifact retains SHA-256
`73062608e38d64d8d44bca979da99638c1ccee10de98ae52f1760a11c4bc3875`.
The non-LTO artifact is
`b1d2a1a3c52baeb36ac84e3b2d73d175b8f095d1446f35063ef99b9eab09852b`.
Repeated `clang-lld-no-lto` builds compile nothing. Evidence is under
`build/engine/logs/clang-lld-no-lto-*`; the build log with `wasm-build` in its
name records the corrected explicit-target build. The earlier host-target trial
and short worker timeout are diagnostic history, not accepted linker results.

The combined production-worker scenario passed all ten positive link and
emitted-module assertions before the additional bitcode checks reached the
180-second execution deadline. Its diagnostic is retained in
`clang-lld-no-lto-worker-extended-stderr.log`; it is partial evidence, not a
passing combined scenario. The authored browser smoke scenario now isolates
the positive object/archive link and expects 11 results including launch,
with a 240-second host wait and 180-second guest deadline. The harness permits
a scenario-specific host wait and otherwise uses the shared config default.
The isolated smoke scenario passes all 11 results with exit 0, recorded in
`clang-lld-no-lto-worker-link.json`. It loads the production engine and worker
from the offline page without launching a graphical browser. Native and
sanitizer runs retain the full 16-assertion positive/negative coverage.

This is a promising lower-memory baseline for ordinary linking. Next, carry
the selected non-LTO profile through a fresh dependency build and broader SDK
link acceptance, then finish the actual Clang executable and end-to-end C
compile/link/run checks. Clang itself still needs optimization/code generation;
these savings apply to LLD, not automatically to the compiler. Lazy decoding
remains an independent optional improvement. LTO could later be a separate
command or module if a concrete workload requires it; that separation is not
implemented here.

`clang-lld-no-lto-verification.json` records baseline/new artifact and source
hashes, instruction measurements, startup resources, native/sanitizer results,
the successful isolated worker result and the combined-run deadline separately.
Corpus refresh/audit, snapshot comparison, shell/Node syntax, whitespace checks,
cached rebuilding and clean LLVM/wasm-spec submodule checks pass.

## Compiler dependency refresh resumed (2026-10-16)

The full `make -C src/aux clang` build is running again against the current
SDK and shared resource configuration, with `CLANG_JOBS=1`. It has refreshed
the C++ runtime configuration, built the runtime and both native table
generators, and regenerated the guest LLVM configuration. The full LLVMSupport rebuild has completed, and the Clang/LLD graph is now
generating Clang diagnostic tables. No compiler source failure has been
reported at this checkpoint; the dependency refresh is not yet
accepted as complete.

At this checkpoint the command session was `46720`; it subsequently exited
with a timestamp-formatting source error. See the final continuation section
for current build state; do not try to resume this closed session. Its output is in
`build/engine/logs/clang-completion-build.log`;
`clang-completion-build-state.json` records the command and observed scope.
A session handle and a running-status record are only continuation aids:
re-poll the actual session before treating it as still live. Both LLVM and
wasm-spec submodule worktrees remain clean.

After this build, resolve any actual compiler source/link/import failure,
select the tested non-LTO linker for installation, and install matching
resource headers, the guest CRT and driver defaults. Then run the actual
installed compiler through native and production-worker Bash for valid C,
guest headers/shared libc, link/run results, invalid C diagnostics and parent
shell recovery. No new compiler or installation acceptance is claimed by
this build checkpoint.

## Shared-provider SDK linking boundary

The non-LTO linker now has a separate `LLD_WASTE_NAMED_IMPORTS` profile in
`src/aux/clang/lld-shared-imports.patch`, applied only to its private overlay.
Upstream LLD rejects a shared provider in a fixed-address executable and emits
shared function imports under `env` when using PIE. WASTE's existing command
ABI imports process memory/table and registers each library under its basename
without `.so.wasm`. The guest profile permits these `.so.wasm` providers for
commands that import memory and assigns that registered namespace to shared
function symbols. It keeps upstream type/symbol resolution and leaves local
definitions and unresolved kernel imports alone. Native/upstream LLD and the
checked-out sources are unchanged.

`make -C src/aux test-lld-sdk` links actual public-header input objects
and the shared guest CRT against `/lib/libc.so.wasm` with guest LLD. The emitted
file is executed directly; no host import-rewrite helper processes it. Its C
program calls memcpy, strlen, printf and fflush through shared libc and write
through the existing runtime. Nine assertions in `sdk.wast` check exact output,
exit 37, statuses and emitted import namespaces, plus the signature diagnostic,
nonzero status and absent output for an incompatible declaration. Native and
ASan/UBSan each pass all nine. The earlier object/archive and direct/archived
bitcode rejection tests still pass all 16 assertions in both modes. Leak
detection is disabled in sanitizer runs.

The same SDK link fixture passes all ten production-worker results including
launch, with exit 0, through the rebuilt offline page. It uses a 240-second
guest deadline and 300-second host wait for the two real linker invocations.
`clang-lld-sdk-worker.json` retains the assertion-aware result. All 12,186
functions in the updated linker also pass streamed semantic validation; its
instruction arrays occupy 79,560,376 bytes (75.87 MiB). This probe does not
claim instantiation; the native/worker sessions supply execution evidence.

The build rule now lists its actual object targets explicitly: Make must not
try to regenerate an included `.d` file through an imaginary `NAME.d.cpp`
source. The final build succeeds and a repeat compiles nothing. Initial
dependency-remake errors in `clang-lld-shared-build.log` are retained diagnostic
history; `clang-lld-shared-build-final.log` is the corrected successful build.

At this checkpoint session `46720` was compiling Clang AST/bytecode sources;
it has since exited. The final continuation section records its successors. Its build percentage is not port acceptance. This SDK
fixture uses host Clang solely to supply objects; it does not establish guest
Clang compilation, installed driver/resource defaults or complete SDK coverage.
No compiler/linker is installed by this change, and the full refreshed LLVM
graph is not yet accepted. The source checks and runtime evidence are under
`build/engine/logs/clang-lld-sdk-*` and `clang-lld-shared-*`.
`clang-lld-sdk-verification.json` records source/artifact hashes and the exact
accepted runtime results. Corpus refresh/audit, snapshot comparison, shell
syntax, whitespace checks and clean LLVM/wasm-spec submodule checks pass.

## Guest driver defaults and resource installation

`clang-sdk-files` stages upstream CMake's portable and WebAssembly resource
components using the native generator tree. Header contents are independent of
the generator host, and its generated `Version.inc` supplies the resource
version. The stage is under `build/aux/clang/sdk`; checked-out submodules stay
read-only. `install-clang-sdk` explicitly publishes the staged files into the
source VFS under the existing install lock. Both targets work while the actual
guest compiler build continues in its separate directory.

The installed `/usr/bin/clang.cfg` selects the guest target/root, public C
headers and `/usr/lib/clang/24` resources. The version is generated, not a
constant in the authored profile. Its link-only tail inputs select the shared
production CRT at `/usr/lib/waste/cc/crt1.o` and actual `/lib/libc.so.wasm`,
with the fixed-address imported-memory/table command ABI. Compile-only and
syntax-only actions omit these inputs. The existing bootstrap compiler-header
snapshot remains separate. The main `install-clang` recipe now builds and
checks the named-import non-LTO linker and selects that artifact for
`/usr/bin/wasm-ld`; the larger LTO-enabled build artifact remains available for
comparison. No new wizard or VFS command registration is required.

All 44 staged files (231,291 bytes), including LLVM's license, match the
installed bytes and mode 0644. The installed CRT matches the package build.
The SDK repeats without rebuilding; VFS discovery/audit accepts the resulting
731 entries. A host-driver `-###` preflight, with guest paths rebased solely
for that host check, accepts the config and shows the expected compile/link
arguments. That preflight is not guest Clang acceptance; the first unrebased
host link preflight correctly rejected the guest-only input paths.

The SDK link fixture now consumes the installed production CRT instead of
compiling its own copy. Ordinary linker fixtures no longer stage or build the
SDK-specific files. The package setup authors valid C using stdio/string
headers, runs `/usr/bin/clang` without manual SDK/link flags, invokes
`/bin/wasm` on the emitted file and records return 37, exact output and parent
shell recovery after invalid C. The package WAST now has seven assertions.
Its parsing is accepted; its actual execution remains pending until the guest
compiler is built and installed. The driver-default runtime gate stays open.

Using the actual installed CRT, the existing SDK linker test passes all nine
assertions natively and under ASan/UBSan, and all ten production-worker results.
The earlier 16-assertion linker regression also passes with the SDK-specific
staging removed from that ordinary path. Corpus refresh/audit, exact snapshot
comparison, SDK caching, shell syntax and whitespace checks pass; LLVM and
wasm-spec submodules remain clean. Leak checking is disabled for the sanitizer
run. These validate the installed CRT/linker boundary, not the unbuilt compiler.

`src/aux/clang/tests/browser.json` is ready to exercise the installed compiler
and packaged seven-assertion snapshot, expecting eight results with launch.
Only its setup script is staged. That scenario is deliberately not reported as
run before an actual guest Clang binary exists. The native package target and
this worker scenario must both pass before compiler acceptance can close.

Evidence is in `build/engine/logs/clang-sdk-*`, including exact installed file
hashes in `clang-sdk-files-verification.json`. At that checkpoint session `46720` had completed Clang AST, refreshed
LLVMCore/BitReader and MC/MCParser/Object, and reached DWARF; it has since exited. No guest compiler binary has been
installed or accepted yet.

## Remaining configure capability audit

The guest's archive-only `CLANG_HAVE_EMUTLS_GET_ADDRESS` probe was a false
positive. The actual shared libc and adapter expose no such helper, and symbol
audits of the linked libc++, libc++abi and LLVMSupport archives find no
definition. Its upstream consumer is Clang's optional incremental interpreter.
The authored guest profile now presets this probe OFF; the native generator
profile retains upstream detection. An isolated CMake profile read confirms
OFF. The subsequent production refresh also records OFF in CMakeCache.txt
and 0 in the generated Clang config header. This does not implement TLS.

Session `46720` originally used the old positive result. It exited with a
source error before installation. After that terminal result, the ordinary
package configuration was refreshed with emulated TLS OFF. Subsequent builds
use the corrected real cache and generated header. The dated platform audit
records the earlier pending change; the continuation evidence records its
application.

Timestamp mutation is a separate unsupported capability, rather than an absent
function: the shared provider exports correctly typed `utimensat` and
`futimens`, but both return -1/ENOSYS. LLVM's Unix/Path setter propagates that
errno as `std::error_code`. Consequently `HAVE_FUTIMENS` is accurate as a
function-presence flag and must not be cited as successful mutation support.
No setter or timestamp semantics are changed here. The compact metadata owns
mtime only, with stat expansion currently aliasing access/change time. The SDK
policy now annotates both setters' unsupported behavior explicitly. Timestamp
mutation remains unsupported; this audit does not close the broader platform
gate or establish that actual compiler operations can tolerate that limitation.

`build/engine/logs/clang-platform-audit.json` records provider/source scope and
the required later refresh; the isolated profile read is in
`clang-platform-profile.txt`. The ordinary SDK check before the annotation
passes 63 headers, six include orders, 830 available functions, 143 unavailable
functions and 31 globals with checked Wasm signatures. The same gate passes
after the annotation as well. The compiler build has
continued through DWARF, GSYM, CodeView/PDB and into ProfileData. Later source
failures and their verification are recorded below. Compiler build/startup
acceptance remains open.


## Compiler continuation: timestamp and pragma sleep (2026-10-16)

The original full build (`46720`, exit 2) stopped at `PPMacroExpansion.cpp`:
the disabled-localization guest C++ profile has no stringstream, locale or
put_time implementation. Its guest-only `WasteTimestamp.h` adapter now uses
shared libc calendar/strftime components and appends the full signed year,
preserving classic spacing and avoiding unsupported `%T` or four-digit year
truncation. The original native/threaded formatting branches remain intact.
Source-date-epoch selection, source mtime selection, literal-one mode and the
missing-time fallback remain in the production macro owner.

The first resumed build (`35064`, exit 2) exposed `Pragma.cpp`'s 100 ms
`#pragma clang __debug sleep` use of unavailable std::this_thread. Its private
guest adapter calls the existing LLVM backoff deadline helper, shared monotonic
clock and yielding nanosleep. It adds no kernel, timer or thread ABI. The
original native/threaded sleep branch remains intact.

Both complete guest source objects compile, and each full source passes native,
native-threaded and guest-threaded syntax gates. The timestamp/pragma fixture
passes 15 WAST assertions in native Bash and ASan/UBSan, plus 16 production
browser-worker results including launch. Leak detection was disabled. The
fixture covers exact formatting and owned results across shared sleep, negative
time, 2038 boundaries, leap day, years 1 and 9999, absent calendar data, exact
comparison against the original native stream formatter, and at least 100 ms of
monotonic elapsed pragma time. See `src/aux/clang/test/timestamp/README.md`.
Evidence: `build/engine/logs/clang-timestamp-pragma-*`.

The actual package acceptance input now requests that pragma and expands
`__TIMESTAMP__`, `__DATE__` and `__TIME__` with SOURCE_DATE_EPOCH=946684800, in
addition to the existing public-header, libc link and execution check. The
seven-assertion package WAST parses, but its runtime acceptance remains pending
until the real compiler is built and installed.

The next full build (`17143`, exit 2) advanced to LLVM coverage formatting and
stopped on four std::ostringstream owners in CoverageMapping.h. The current
continuation adapts these guest-only integer/text formatters to the existing
LLVM raw string sink. Exact coverage output and complete source checks are
being verified before resuming the full build. No Clang binary has yet been
linked or installed; actual compiler/package acceptance remains open.


## MC/DC coverage string owners (2026-10-16)

The verified `17143` failure identified four locale-stream owners in
`llvm/ProfileData/Coverage/CoverageMapping.h`: condition headers, vector
headers, vector rows and condition coverage labels. Only the disabled-thread
guest branch now uses LLVM's existing raw_string_ostream with a local owned
string. All insertion logic, condition/vector selection, independence-pair
computation and public interfaces remain upstream. Native/threaded branches
retain std::ostringstream. The patch applies to staged sources only.

The complete CoverageMapping.cpp guest object compiles, and its entire source
passes native, native-threaded and guest-threaded syntax gates. The observer
links that actual object and the existing Support/Demangle archives against the
installed production CRT and shared libc. All 25 WAST assertions pass in native
Bash and ASan/UBSan with leak detection disabled. Exact text includes an actual
independence pair, uncovered and folded conditions, permuted condition IDs,
true/false/don't-care rows, unexecuted numbering, empty and twelve-condition
headers, UINT32_MAX coordinates, and strings retained after record destruction
and shared sleep. See `src/aux/clang/test/coverage-format/README.md`.

The first fixture link exposed its own main/CRT signature mismatch; the
observer now uses the installed CRT's two-argument main. The corrected build
is `clang-coverage-format-build-final.log`; the earlier log is diagnostic
history. A shared Demangle producer also accidentally depended on the DWARF
fixture. Demangle now reuses the existing Support producer, so these small
Support-only observers do not build DWARF. The corrected fixture repeat is
recorded separately.

The rebuilt offline production worker passes all 26 results, including launch.
Corpus refresh/audit and exact installed snapshot comparison pass. The repeated
fixture build issues no compile/link commands and does not request DWARF.
Shell syntax and whitespace checks pass; LLVM and wasm-spec worktrees remain
clean. Evidence is under `build/engine/logs/clang-coverage-format-*`, including
`clang-coverage-format-verification.json`.

The full `make -C src/aux clang` build resumed, with one job, in session
`93486`; its log is `build/engine/logs/clang-completion-build-resume-coverage.log`.
It subsequently reached 75%, built the coverage archive, then exited 2 in
DylibReader.cpp UUID formatting. That session is closed; see the continuation
below. This is not successful compiler linkage, installation or acceptance
of the seven package assertions. `clang-completion-build-state.json` records
the current continuation and the previous verified exit-2 failure.


## TextAPI Mach-O UUID reader continuation (2026-10-16)

The verified terminal result from session `93486` identifies unavailable
std::stringstream/hex manipulators in TextAPI BinaryReader's LC_UUID owner.
The guest-only branch now calls LLVM's existing raw_ostream::write_uuid,
retaining uppercase padded bytes and the same separators. The native/threaded
stream branch and final RecordsSlice::copyString ownership stay unchanged.
No duplicate formatter, parser, libc or kernel implementation is introduced.

The new `src/aux/clang/test/dylib-uuid` observer includes the production
reader to reach its private header reader and supplies real Mach-O objects
through MachOObjectFile::create. Its WAST expectations cover zero, all-ones,
mixed high-bit and absent UUIDs in both byte orders and word sizes, plus owned
strings retained after every source/object/slice has been destroyed and a
shared sleep yield. The standalone guest object and all three full-source native/threaded syntax
gates pass. Native Bash and ASan/UBSan each pass all 35 WAST assertions, with
leak detection disabled; the rebuilt production browser worker passes all 36
results including launch. The public native test target performs no new
compile/link commands. Corpus refresh/audit, installed snapshots, shell syntax
and whitespace checks pass. Both LLVM and wasm-spec submodules remain clean.
See `src/aux/clang/test/dylib-uuid/README.md` and
`build/engine/logs/clang-dylib-uuid-verification.json` for scope and evidence.

The complete compiler build is running again with one job in session `2160`,
logging to `build/engine/logs/clang-completion-build-resume-uuid.log`. It has
since completed the TextAPI BinaryReader, WindowsDriver, APINotes and Clang
Support targets, the complete Clang parser object target and ASTMatchers.
The complete Clang semantic-analysis object target has now built: all 87
required objects are present and nonempty, including template, coroutine,
OpenMP and WebAssembly owners. The same session has advanced to 89% and is
compiling Clang Analysis/CFG. It subsequently exited 2 at IssueHash.cpp
(normalization stream); session `2160` is now closed. These are build
checkpoints, not compiler linkage or runtime acceptance. See the next section. This is a
verified wait on the live command, not compiler acceptance. Poll that
actual handle before deciding whether it is still live; do not start a
competing build from a status file alone. The prior `93486` session is closed
with exit 2. The current build, final import/memory measurements, installation
and actual seven-assertion compiler package acceptance remain open.


## Clang issue-line normalization continuation (2026-10-16)

The verified terminal result from session `2160` is an unavailable
std::ostringstream in IssueHash.cpp's NormalizeLine. Its guest-only branch
now uses LLVM's existing raw string sink; native/threaded streams and the
entire token-selection loop remain unchanged. The source patch applies only
to the staged LLVM checkout.

The new `src/aux/clang/test/issue-hash` fixture uses actual SourceManager,
FileManager, diagnostics, Lexer, normalization and MD5 code. WAST expectations
cover comments, whitespace, source-line selection, quoted spaces/escapes, blank
lines, a long identifier, EOF behavior, copied strings and hashes across source
manager destruction and shared sleep. The complete production guest object
compiles; all three full-source native/threaded syntax profiles pass. All 31
WAST assertions pass in native Bash and ASan/UBSan, with leak detection disabled;
the rebuilt production browser worker passes all 32 results including launch.
The fixture builds ClangLex (which brings in ClangBasic) explicitly, uses the
installed CRT and shared libc, and verifies 53 actual provider imports. Its
final link uses the required TargetParser/Support/Demangle archives without
unrelated reader archives and produces byte-identical output to the artifact
used by the sanitizer run. The public native test repeats without compile or
link commands. Corpus refresh/audit, installed snapshots, shell syntax and
whitespace checks pass; LLVM and wasm-spec submodules remain unchanged.

This accepts the changed normalizer and actual MD5 helper, not all public
issue-signature paths or a guest compiler invocation. See
`src/aux/clang/test/issue-hash/README.md` and
`build/engine/logs/clang-issue-hash-verification.json` for scope and evidence.

The full build resumed with one job in session `47655`, logging to
`build/engine/logs/clang-completion-build-resume-issue-hash.log`. Poll that
actual session before starting any replacement; `2160` is closed with exit 2.
That session subsequently exited 2 at ThreadSafetyUtil.h's unused
std::ostream adapter; it is closed. The next continuation records the fix.
Actual compiler linkage, import/footprint measurements, installation and the
seven-assertion compiler package acceptance remain open.


## Thread-safety utility source compatibility (2026-10-16)

The terminal result from session `47655` identifies an unused
std::ostream/StringRef insertion adapter in ThreadSafetyUtil.h. Its inline
body requires a complete std::ostream, unavailable in the localization-free
guest C++ profile. The actual TIL StdPrinter, sx::toString and diagnostic paths
already use LLVM raw streams. Only the unavailable adapter is excluded in the
disabled-thread guest branch; native/threaded adapters stay intact. No analyzer,
thread, lock or runtime behavior is removed or replaced.

The new private `thread-safety-source` gate compiles all four production owners
and explicitly calls the original adapter in each full-source native/threaded
syntax profile. All four guest objects and twelve native/threaded full-source
checks pass. The initial native gate found a harness include-path gap: missing
generated OpenMP enums fell through to an older installed LLVM. Keeping native
configuration headers first and adding the matching target-independent generated
headers fixes that gate without changing production code. Evidence is under
`build/engine/logs/clang-thread-safety-*`, including the final source log and
verification JSON. There is no new functional WAST test for this unused-header
compatibility exclusion; actual compiler package/runtime acceptance remains
open. See `src/aux/clang/test/thread-safety/README.md` for reproduction.

A dated inventory of the real Clang CMake dependency closure found 328 reachable
targets, 2,376 required object files and 357 missing objects before this failure.
The largest pending groups were CodeGen (104), Driver (77), scalable-analysis
owners (54) and Frontend (31). Existing objects can still need dependency
rebuilds; these counts do not measure port acceptance or include linkage,
installation or runtime checks. Exact per-target inputs are recorded in
`build/engine/logs/clang-required-object-inventory.json`.


The full build resumed with one job in session `82881`, logging to
`build/engine/logs/clang-completion-build-resume-thread-safety.log`. It passed the thread-safety owners, then exited 2 at six UnsafeBufferUsage
string-stream owners. That session is closed; the next continuation records
the source adaptation and pending real-compiler acceptance. `47655` is also
closed with exit 2.
The real compiler still needs linkage, provider/footprint measurements,
installation and native/sanitizer/offline-page package acceptance.


## Unsafe-buffer fix-it stream owners (2026-10-16)

The verified `82881` result identifies six unavailable std::stringstream
owners in UnsafeBufferUsage.cpp. The guest-only branch now uses LLVM raw
string sinks for those six release owners and the seventh debug-only ancestor
formatter. All analysis, source selection, fix-it spelling, control flow and
native/threaded streams stay unchanged. No diagnostic or compiler feature is
disabled. The complete production object, a full guest-debug syntax check
and all three full native/threaded source profiles now pass. This verifies all
seven source owners, not actual compiler-generated fix-its at runtime.

The new `src/aux/clang/test/unsafe-buffer` acceptance scenario deliberately
invokes the actual installed compiler instead of substituting a copied
formatter. Its thirteen WAST assertions inspect ten exact parseable fix-it
ranges/texts, compiler status and parent-shell survival. Inputs exercise local
span declaration/initialization, zero/nonzero array addresses, preincrement,
parameter replacement, new overload declarations and old overload wrappers.
These runtime gates remain pending until guest Clang is built and installed:
`make -C src/aux test-unsafe-buffer`, the same target with
`WASM_RUNNER=.../wasm-sanitize`, and the production-worker browser.json scenario
(expect fourteen results including launch).

The WAST file parses and the scripts pass shell syntax checks. A development
host Clang 22 preflight targeting Wasm accepts the inputs/options and produces
the expected ten fix-it forms. This is only test-input preparation; it does not
close guest runtime acceptance or the broader owning-string/formatting gate.
The debug-only owner is source-checked, not executed by the release compiler
runtime scenario. See `src/aux/clang/test/unsafe-buffer/README.md`.


Source evidence is in `build/engine/logs/clang-unsafe-buffer-source.log` and
`clang-unsafe-buffer-verification.json`. Corpus refresh/audit and exact installed
snapshot comparison pass; shell syntax and whitespace checks pass. The runtime
scenario is prepared and parsed but deliberately not reported as executed
before an installed guest compiler exists.

The full build resumed with one job in session `12910`, logging to
`build/engine/logs/clang-completion-build-resume-unsafe-buffer.log`. It passed
Analysis/LifetimeSafety, built all 104 CodeGen objects and linked Serialization,
then exited 2 at CompilerInvocation.cpp's seed-file ifstream. That session is
closed, as is `82881`. The next continuation records the reader fix. The full
compiler and both package/fix-it runtime acceptance gates remain open.


## Layout seed-file reader and frontend continuation (2026-10-16)

The terminal `12910` failure identifies std::ifstream/std::getline in the
layout-randomization seed-file option. The guest-only private WasteSeedFile.h
adapter uses existing shared libc fopen/getline/close APIs, with explicit
ownership of the allocated line. It removes only LF, preserving spaces, CR and
embedded NUL; failed opens preserve the previous seed, while opened empty
files clear it. The original native/threaded reader, open-failure diagnostic
and later explicit-seed override stay in the production option parser.

The complete CompilerInvocation.cpp guest object and all three full-source
native/threaded profiles pass. The reader's 20 WAST assertions pass in native
Bash and ASan/UBSan (leak detection disabled), and all 21 production-worker
results pass including launch. A complete byte packet is compared with the
original native stream reader over separate generated files. Cases include
missing/empty files, first-line selection, spaces/tabs/CR, embedded NUL, no
final LF, an empty first line and a 4097-byte line. Retained strings outlive
closed files, freed getline buffers and a shared sleep yield. The public
native test repeats without compiling or linking. Corpus/snapshot audits,
shell syntax and whitespace checks pass; submodule sources stay untouched.
See `src/aux/clang/test/seed-file/README.md` and
`build/engine/logs/clang-seed-file-verification.json` for scope and evidence.

This accepts the reader boundary, not an execution of guest Clang itself.
The pending unsafe-buffer compiler setup now passes a real two-line seed file
to this option in the same compiler invocation; a host preflight accepts that
input/option combination, but actual guest integration remains pending.

The full build resumed with one job in session `19315`, logging to
`build/engine/logs/clang-completion-build-resume-seed-file.log`. It passed the
invocation owner, then terminated with exit 2 at LayoutOverrideSource.cpp's
unavailable std::ifstream. That handle is closed; use the later continuation's
current build handle. Compiler linkage, final imports/footprint, installation and the
seven-assertion package plus thirteen-assertion
fix-it runtime gates remain open. The earlier 104-object CodeGen checkpoint
and Serialization archive are built outputs, not overall compiler acceptance.


### Continuation: layout file input and large browser loader ownership

The terminal `19315` failure is addressed with private `WasteFileInput.h`,
shared by LayoutOverrideSource and the existing seed-file wrapper. Both use
installed shared libc fopen/getline/close APIs with owned files and buffers.
Native/threaded branches retain their C++ streams. Production record parsing
and option diagnostics stay in their original source owners; no independent
filesystem or clock backend is introduced.

The complete LayoutOverrideSource.cpp guest object and three full-source
native/threaded profiles pass (`clang-layout-override-build.log`). The observer
links the actual production object, using private access only in the test to
inspect parsed records. All 43 WAST assertions pass in native Bash and ASan/UBSan,
and all 44 production-worker results pass including launch. Cases include
missing/empty files, legacy and byte-size layouts, CRLF, long lines/names,
64-bit offsets, anonymous filtering, duplicate replacement, field/base/virtual
base offsets and the final record without LF. Values survive input/parser
cleanup and a shared sleep. This accepts constructor parsing; the public AST
layout callback and actual compiler execution remain separate boundaries.
The refactored seed reader repeats all 20 native/sanitizer assertions and 21
worker results, including comparison against the original native stream reader.

The fixture's LLVM dependency closure now explicitly includes
LLVMFrontendDirective: the first direct startup attempt identified its missing
FindName definition. The corrected artifact is 8,937,424 bytes, with 16,541
defined functions and 61 imported functions, including 55 signature-checked
shared libc imports. Streaming validation covers every defined function and
measures 127,327,312 bytes of decoded instruction arrays. This is a parser
fixture measurement, not the size of the still-unlinked Clang command.

LLVM disassembly measures a maximum explicit block depth of 528 in
BuiltinType::getName, exceeding the prior 512 bound. Shared
EXECUTION_MAX_CONTROL_DEPTH is now 1024; decoder, validator, executor, cloning
and continuation storage continue to derive it from src/config.h. Portable
binary WAST checks cover 512 and 1024 active blocks, mixed br_table depths,
early returns and independent caller/callee stacks: 18 native/sanitizer
assertions and 19 worker results pass. A 1025-block module is rejected cleanly
as exceeding the configured resource bound. The per-function validation
control array is 951,200 bytes; the existing browser C stack size is unchanged.

The capacity change exposed a native WAST startup stack overflow: its inline
driver context is 8,933,624 bytes, exceeding the ordinary 8 MiB native stack.
The WAST owner now allocates and cleans up that context on the heap. The native
session/checkpoint owners were already heap allocated and the browser driver
has static storage. Three private continuation observers likewise move their
large snapshots to the heap; sanitizer gates pass 12 two-wait, 24 exec-lifecycle
and 63 exec-matrix checks. The full i32-smoke gate passes, including all 23
portable corpus files. The mmap-heap fixture belongs under
src/system-tests/libc because it imports process-runtime memory; its nine
native/sanitizer assertions and ten worker results pass there. Installed paths
and its shell scenario are refreshed accordingly.

The initial large-fixture worker stalled during command startup. A bounded
CPU profile attributes about 88% of sampled time to the browser engine's
malloc/realloc, whose list searches visited live allocations repeatedly.
Its private allocator now uses size bins for free blocks, physical neighbour
links for coalescing, constant-time append and in-place tail growth. Failed
growth preserves existing storage; a free tail can grow instead of wasting
its span. This allocator is separate from guest shared libc.
`make -C src/html-rt browser-allocator` checks the actual implementation with
native ASan/UBSan and bounded memory: alignment, zeroing/overflow, fragmented
peers, splitting/coalescing, in-place/moving realloc and failure preservation.
With the corrected allocator, the final packaged worker passes all 44 layout
results within its external 60-second bound, plus the seed/control/mmap worker
gates above. Sanitizer runs disable leak detection. Corpus/provenance, VFS and
offline packaging audits, shell syntax and whitespace checks pass.

See src/aux/clang/test/layout-override/README.md and
build/engine/logs/clang-layout-input-verification.json for exact artifacts and
log paths. The verified link is retained under
build/system-tests/clang/layout-override-verify. The public fixture's regenerated
AST/Core dependency build was deliberately interrupted (session `26072`, exit
130) to return to the full compiler target; retain its compiled objects. Rerun
its ordinary public Make target after the full dependency build settles.

The full compiler build resumed with one job in session `60780`, logging
to build/engine/logs/clang-completion-build-resume-layout-input.log. It was later
stopped deliberately (exit 130) after the direct source pass found HLSL.cpp's
unavailable std::regex. Its compiled objects are retained; the next continuation
records the replacement full-build handle. Clang remains unlinked and
uninstalled. Final imports/footprint, SDK compilation/link/execution, seven
package assertions and thirteen unsafe-buffer compiler assertions in native,
sanitizer and production-worker runtimes remain open. Keep the original
LTO-enabled linker artifact and select the already-tested non-LTO linker for
ordinary SDK acceptance.


### Continuation: HLSL extension names and remaining Driver source checks

The direct guest source pass checked 15 remaining Frontend owners and 48
Driver owners successfully, then found unavailable std::regex/regex_match in
HLSL.cpp. This is a required Driver source despite the Wasm-only backend
selection. Session `60780` was stopped deliberately at the LLVM dependency
refresh so this known blocker could be fixed before the full build reached it.

The disabled-thread guest branch now uses the existing LLVM POSIX regex
backend, already used by LLD diagnostics. Anchors and explicit full-match
length preserve standard regex_match behavior, including embedded NUL and
newline inputs. Native and threaded branches retain their original std::regex.
No runtime, kernel, locale, clock or shader backend is added.

The actual CMake HLSL.cpp object and all three native/threaded full-source
profiles pass. The source-backed observer calls the anonymous helper by
including the complete production owner; section GC removes unrelated Driver
methods. All 34 WAST assertions pass in native Bash and ASan/UBSan, with leak
detection disabled, and all 35 production-worker results pass including
launch. Thirty fixed cases cover aliases, case-sensitive prefixes, valid and
long names, delimiters, whitespace, UTF-8 and embedded NUL. An exhaustive
256-byte suffix bitmap and a complete comparison with the original native
production std::regex implementation also pass. Retained observations survive
temporary regex/string cleanup and a shared sleep yield. The public Make test
repeats without compilation or linking. Installed snapshots, corpus/provenance,
offline packaging, shell syntax and whitespace checks pass.

The resumed direct source pass checks the remaining 28 Driver owners and
ExecuteCompilerInvocation.cpp successfully. Together with the 63 earlier
passes and the actual HLSL object gate, this covers all 93 previously unbuilt
Frontend/Driver/FrontendTool owners in that checkpoint. The direct syntax checks use real guest flags/headers without PCH and a
2 GiB native compiler address-space bound; they establish syntax compatibility,
not full executable linkage or runtime acceptance. Other LLVM/Clang components still require the real build.

See src/aux/clang/test/spirv-name/README.md and
build/engine/logs/clang-spirv-name-verification.json for commands, exact
artifacts and evidence. The full compiler build is running with one job in
session `6804`, logging to
build/engine/logs/clang-completion-build-resume-spirv-name.log. Poll that actual
handle before starting another LLVM build. Clang remains unlinked and
uninstalled. Final imports/footprint, real SDK compile/link/execute, package
seven-assertion and unsafe-buffer thirteen-assertion native/sanitizer/worker
gates, plus final unchanged-build caching remain open.


### Prior required-source preflight

The current generated Clang dependency graph has 328 reachable targets and
2,376 required object files. At this checkpoint 2,198 object files are present
and 178 are absent; presence alone does not prove dependency freshness.
The previously checked 93 source owners include the now-compiled HLSL fix.
The other 86 unchecked owners now pass direct guest syntax checks: five actual
compiler entrypoints (including generated main), six ExtractAPI, eight frontend
rewrite, eight InstallAPI, three rewrite, 54 scalable-analysis and two Tooling
Core sources. Every source that was unbuilt at the checkpoint is now covered,
without dropping a required component. Current source and flags hashes still
match the checked inputs. See
build/engine/logs/clang-complete-source-preflight.json and its per-source records.

These are no-PCH syntax checks with real guest headers/flags and a 2 GiB native
compiler address-space bound. They do not establish code-generation success,
object freshness, final imports or compiler execution. The generated main
source retains the required guest C linkage; inspect the actual linked artifact
too once available. That full build has now completed both Clang and LLD.
Session `6804` closed after the subsequent packaging step hit the signature
inspector's old hard-coded input-size check. It is no longer a live build.
The linked-artifact continuation below supersedes this source-only checkpoint.

### Linked Clang: measured capacities and remaining imports

The full guest build has now linked Clang and LLD. The raw Clang command is
105,583,594 bytes, including a 39,666,722-byte optional function-name section.
Packaging removes only that section with `llvm-objcopy`, retaining the named
raw artifact for diagnostics. Namespace rewriting then produces a 65,916,977-byte
command (62.86 MiB), within the existing 64 MiB executable-byte bound. The
signature inspector now reads that bound from `src/config.h`. There is no
executable-file or browser-memory-limit increase in this continuation.

The actual compiler contains 265 types, 167,451 defined functions and a maximum
explicit block nesting of 2,205. Its largest nesting belongs to the retained
RISC-V builtin frontend dispatcher, even with Wasm as the selected code target.
Shared configuration now permits 512 types, 262,144 functions and 4,096 control
frames. Indexed references and parser-only recursive placeholders use disjoint
ranges, preserving high indices and nullability. New authored WAST covers high
GC/function references, cross-instance imports, direct/table/ref calls above
function 131,072, and 4,096-deep branches, calls and returns.

The larger bound initially exposed a browser C-stack overflow during ordinary
Bash startup. The validator now owns a bounded heap workspace, sized to the
smaller of the function instruction count and configured depth, and frees it
on every outcome. Native and ASan/UBSan checks pass all 58 capacity assertions;
the production worker passes 27/27 control-depth, 13/13 high-type and 9/9
high-function results, including Bash exit. The earlier 256-type regression
now accepts its three valid modules above the old resource bound instead of
misclassifying them as malformed. A separate valid 4,097-deep diagnostic is
rejected with `control nesting limit exceeded`.

Streamed validation passed every actual Clang function. Eager decoded storage
would require 1,374,778,440 instruction bytes, plus 2,128,132 local bytes and
922,856 payload bytes. This is a storage measurement, not instantiated memory,
startup RSS or execution evidence. Keep the current 2 GiB browser budget and
measure real startup before deciding whether lazy decoded-function loading is
required. Lazy decoded-function loading remains a candidate for reducing
retained code; this continuation adds no execution cache.

The linked import audit identified three concrete provider issues. Public
`sys/ioctl.h` needs C linkage when included from C++; the existing libc/kernel
operation remains its provider. Clang's sanitizer cutoff calculation retains
`lround`, now implemented in shared libc with ties away from zero and checked
range/domain errors before Wasm conversion. Its 22 package WAST assertions pass
natively, under ASan/UBSan and through the production worker (23/23 with Bash
exit). LLVM's fixed-point builder only needs powers of two; the guest adaptation
uses existing `ldexp(1.0, exponent)` instead of unsupported general `pow`, while
native/threaded paths keep upstream behavior. The source-backed
`src/aux/clang/test/compiler-imports` fixture exercises both fixed-point
conversion directions and the public C++ ioctl declaration; its build and
verification are the current pending boundary.

Evidence is under `build/engine/logs/clang-linked-*`, including structure,
control-depth, math-callers, footprint, capacity and worker reports. At this checkpoint the package
was still **uninstalled and unexecuted**. Finish the source-backed import checks,
rebuild the actual compiler with those fixes, check all resulting imports, then
run real SDK compile/link/execute and unsafe-buffer acceptance under native,
sanitizer and packaged-worker runtimes. Final build caching and licensing gates
remain open; this plan is not complete.

The follow-up native `i32-smoke` gate passes its sanitizer probe and all 25
installed engine regression files. Upstream GC struct, type-subtyping,
ref-test and ref-cast suites pass 209 assertions natively. The prior high-type
production-worker scenario also passes 13/13 with the corrected resource-bound
expectations. The compiler-imports build is rebuilding its ordinary LLVM
Support prerequisites in one job; observe that build rather than starting a
second competing CMake graph.

The full shared-libc package gate also passes from native Bash: 27 authored
WAST files, 869 assertions, zero failures, including the new rounding provider.
The enlarged continuation capacity also passes the existing private sanitizer
ownership guards: two-wait 12 checks, exec lifecycle 24 checks and exec matrix
63 checks, all without failures. Installed-corpus refresh and provenance/byte
audits pass after the authored fixture changes.

The initial compiler-imports fixture build unexpectedly entered the DWARF
producer through shared Core archive filenames. That extra build was stopped
before continuing the fixture. Shared Core/Remarks/BitstreamReader/BinaryFormat
archives now have a bounded `clang-core` producer, and the DWARF fixture owns
only its additional archives. Resume the compiler-imports build recorded in
`build/engine/logs/clang-compiler-imports-core-build.log`; the earlier
`clang-compiler-imports-build.log` ended with that intentional interruption.

The actual `CGExprScalar.cpp` owner now also code-generates successfully with
its configured guest flags, textually including the current PCH header instead
of using a stale precompiled header. Its object imports `ldexp`, with no `pow`
import. The separate object and copied CodeGen archive under
`build/engine/clang-import-trial` leave the production artifacts unchanged.
After the Core producer finishes, a diagnostic relink can check actual compiler
startup while the normal full-build freshness/installation gate stays open.
This is the real compiler source, not a replacement compiler or provider shim.

### Real compiler diagnostic startup

The source-backed compiler-imports fixture now passes all 12 WAST assertions
from native Bash and ASan/UBSan Bash, plus 13/13 packaged-worker results. Native,
native threaded and guest threaded source profiles also compile. Its entrypoint
now matches the installed two-argument CRT, with explicit C linkage only for
the Wasm build. Core dependencies complete through the bounded shared producer.

The separate diagnostic relink replaces the actual fixed-point owning object
in a copied CodeGen archive and uses the refreshed Core/Support archives. It
links the real compiler driver and remaining full-build archives; production
archive/package outputs and the source VFS remain unchanged. The 65,916,938-byte
command has 112 verified libc imports, including real `ioctl`, `ldexp` and
`lround`; the unsupported `pow` and mangled ioctl imports are absent.

Session-staged `/usr/bin/clang --version` now succeeds from native Bash, reports
LLVM 24.0.0git, reads `/usr/bin/clang.cfg`, and exits 0. The bounded run takes
35.89 seconds and peaks at 1,705,412 KiB RSS (1.63 GiB) under a 2 GiB native
address-space cap. Evidence is in
`build/engine/clang-import-trial/latest-native-version.json` and its run directory.
This proves actual startup, not SDK compilation or normal-package freshness.

The authored seven-assertion package WAST is now running against session-staged
diagnostic Clang and the accepted non-LTO linker, with the same installed guest
headers/CRT/config. A normal one-job full compiler build is also live in
`build/engine/logs/clang-import-fixes-full-build.log`; complete that producer
before installing or accepting the normal package. The full package and
unsafe-buffer native/sanitizer/worker gates remain required.

The first multi-command staging attempt stopped before guest execution: native
staging incorrectly charged compiler plus linker bytes against the 64 MiB
WAT/WAST source-text bound. Aggregate explicit staging now uses the existing
`VFS_MAX_BYTES` budget, retaining the per-file executable bound and checking the
subtraction before adding a file. Native/sanitizer applications rebuild with
warnings as errors; no LLVM, VFS or browser limit was increased. The diagnostic
package retry is running with both real commands staged in the same session.

### Actual compilation: optional child statistics frontier

The diagnostic package retry stages both commands and completes without a
resource-limit failure, but passes only four of seven assertions: version,
invalid-C rejection/diagnostics and parent survival. Combined compilation/linking
fails because `clang::driver::Command::Execute` unconditionally requests child
statistics, which the guest Program backend correctly rejects. A separate
actual `clang -c` invocation succeeds through the integrated frontend/backend;
the diagnostic `-v` output confirms that in-process path. This establishes
object compilation, not linked-program execution.

The driver adaptation now passes no statistics output for ordinary jobs in the
single-threaded guest profile. Explicit process-statistics requests still reach
the existing unsupported-capability error; native/threaded profiles preserve
upstream accounting. The complete `Job.cpp` owner code-generates with its guest
flags, and the compiler-imports source gate now checks that owner in all three
native/threaded profiles. The normal full build was intentionally stopped
before staging this source change; its completed AST objects are retained.
`build/engine/logs/clang-job-native-profiles.log` records the current producer.

Next, relink the diagnostic compiler with the corrected job owner and rerun
combined compilation/linking under the 2 GiB bound. Upstream disables integrated
cc1 for multiple jobs, so this may expose the cost of a second eager compiler
image. Measure that path before choosing a memory fix. Normal full-build,
installation, package/unsafe-buffer sanitizer and worker acceptance, and cache
verification remain open. Reports are under `build/engine/clang-import-trial`;
all functional package expectations remain in the authored seven-assertion WAST.

The corrected job owner passes native, native-threaded and guest-threaded source
checks; the new diagnostic relink verifies its libc imports. Combined
compilation now reaches child startup instead of failing on statistics. The
2 GiB bounded run reaches roughly 2 GiB RSS and fails while loading the second
compiler image. `--trace-process` records rejection at defined function 51,780
with an unset instruction index: the validator's control-workspace allocation
failed before instruction validation began. The parent driver reports a generic
execution error; improve the allocation diagnostic with the memory fix. A
subsequent external `cat` also returned EBUSY, while Bash builtins still worked;
retain this failure-cleanup observation when checking resource rejection.

Lazy decoded-function loading is therefore a required memory step for the
current normal compile/link path under 2 GiB, following the earlier user
preference. Keep eager validation of every body, own the retained encoded bytes,
materialize each function before execution, and preserve shared static owner
lifetime across clone/fork and active/yielded frames. Keep decoded bodies for
the owner's lifetime initially; eviction remains optional. Do not skip unused
invalid functions, special-case Clang by name, or change the normal driver
pipeline to conceal a second-image failure.

The normal full compiler producer remains live in
`build/engine/logs/clang-job-fix-full-build.log`. A separate packaged-worker
version-only diagnostic is running from the same real compiler artifact; it
is startup evidence, not the remaining full compiler acceptance.

The version-only production-worker diagnostic now passes 1/1 with exit 0,
including the real Clang version text and installed guest config path. This
uses the packaged browser C engine and session-staged compiler, without a GUI
browser. Evidence is `build/engine/logs/clang-import-trial-browser-version.txt`
and `build/engine/clang-import-trial/browser-version.json`. Both runtime
artifacts therefore reach real compiler startup; full compiler workflow
acceptance still waits on the second-image memory fix and normal installation.

### Lazy decoding and normal installation

The loader now validates every body, owns its retained encoding, and releases
the initial decoded representation. First use decodes and revalidates into a
temporary row, then publishes the complete instruction array and ordinary
resolution metadata. Failed materialization leaves the cold row intact. Static
reference counting owns cold and decoded bodies across clones; decoded code is
retained until the last owner is released. No image-name special case, global
execution cache or eviction policy was added.

All 25 portable native engine regression files pass 1,485 assertions, also
passing all 1,485 under ASan/UBSan. The enhanced private i32 smoke passes source
disposal, cold clone lifetime, shared materialization, reuse and independent
instance checks under sanitizers, and its installed 25-file regression gate
passes. Validation workspace allocation failure now has its own internal status
and an explicit diagnostic instead of an invalid instruction index.

The unchanged combined Clang compile/link invocation now exits 0 under the
2 GiB bound, taking 90.19 seconds and peaking at 527,772 KiB RSS (515 MiB).
Its earlier eager run exhausted that bound while loading the second compiler.
The subsequent package test reaches five of seven assertions: execution still
called a missing `/bin/wasm` handler. The shared kernel now supplies that binary
interpreter, with ordinary image startup and argv forwarding; native discovery
and HTML virtual-node metadata expose the same path. Its four authored WAST
checks pass natively and under sanitizers, including readable non-executable
input, argv, missing operands and text rejection. Browser parity is pending.

The normal full compiler build has completed. Its 65,916,960-byte compiler is
byte-identical to the diagnostic artifact (SHA-256
`6050671d43d940a1fb627c1aba3001ccef283b109c3b03d2bf6c91ac49ab6ec0`).
`install-clang` has now published that compiler, the selected 6,975,173-byte
non-LTO linker, matching resource headers/config/CRT and upstream license into
the source VFS. Production package acceptance now uses those installed files.

The canonical native package target is running with its package-owned long-test
driver; the browser page is rebuilding from the installed VFS and new engine.
The package driver allows ten minutes by default (WASTE_TEST_TIMEOUT_MS remains
overridable), and its browser harness allows fifteen minutes. These are test
deadlines; no runtime resource capacity changed. Complete the seven package
assertions and thirteen unsafe-buffer assertions under native, sanitizer and
production-worker runtimes, plus final installation/provider/license/caching
audits before closing the plan.


## Final installed-tool acceptance (2026-10-17)

The normal `clang` build and `install-clang` have completed from the staged
read-only LLVM revision. This is acceptance of actual installed guest commands,
not a host-compiled substitute or isolated archive observer. The package setup
checks invalid C diagnostics, SDK headers, shared printf/string/memcpy, fixed
source timestamps, yielding debug sleep, linker execution, the emitted command's
61 output bytes and exit 37, and parent-shell recovery.

| Gate | Result | Evidence under `build/engine/logs/` |
| --- | --- | --- |
| Native installed package | 7/7, exit 0 | `clang-installed-package-native.log` |
| Sanitizer installed package | 7/7, exit 0 | `clang-installed-package-sanitize-recheck.log` |
| Production-worker package | 8/8, exit 0 | `clang-installed-package-worker.txt` |
| Native unsafe-buffer fix-its | 13/13, exit 0 | `clang-installed-unsafe-native.txt` |
| Sanitizer unsafe-buffer fix-its | 13/13, exit 0 | `clang-installed-unsafe-sanitize.txt` |
| Production-worker unsafe-buffer fix-its | 14/14, exit 0 | `clang-installed-unsafe-worker.txt` |
| Engine regressions | 1,485 native and 1,485 sanitizer assertions | `clang-lazy-regressions-{native,sanitize}.txt` |
| Upstream GC regression subset | 209 native and 209 sanitizer assertions | `clang-lazy-gc-{native,sanitize}.txt` |
| Lazy-function worker regressions | 69/69 including launch | `clang-lazy-worker-regressions.txt` |
| Binary handler | 4 native, 4 sanitizer, 5 worker results | `clang-binary-handler-{native,sanitize,worker}.txt` |
| Exec/continuation ownership | all five Make gates pass | `clang-final-ownership-gates.log` |
| Full installed native corpus | 327 PASS / 65,432 assertions, 0 FAIL, 103 SKIP | `clang-final-corpus-recheck.log` |
| Current VFS and reserve/overflow | pass | `clang-final-vfs-check.log` |
| SDK/provider signatures | pass: 831 available functions, 31 globals | `clang-final-sdk-check.log` |
| Installed bytes, modes, SDK and license | pass | `clang-final-install-audit.json` |
| Unchanged compiler build cache | no work | `clang-final-cache-check.log` |

The VFS capacity guard now discovers the slots left after mounting, because
built-in runtime nodes also consume the configured reserve. It still verifies
that filling those slots succeeds and the next creation fails, without copying
the kernel's boot-node inventory into the test.

The package's native test deadline defaults to ten minutes; sanitizer runs may
need the documented `WASTE_TEST_TIMEOUT_MS=1800000` override. That changes a
test deadline, not engine memory capacity. Leak detection is disabled for the
ASan/UBSan runs; no leak-check claim is made.

The full corpus passes with `--jobs=1 --timeout-ms=60000`; the first four-worker
run had six deadline expirations under the shorter defaults while sanitizer
compilation was active. All six pass in the sequential run. The 103 skips retain
the installed manifest's context requirements: 101 need Bash, one needs the
interactive renderer and one needs the browser compatibility backend. No
failures were added to the expected-failure list. Full assertion JSON is in
`build/cli-rt/clang-final-corpus-results.json`.

Final sanitizer package acceptance passes all seven assertions with exit 0.
The earlier ten-minute attempt expired before assertions; the longer run
completed in approximately fifteen minutes with no sanitizer diagnostic.
The refreshed offline page's static packaging check passes; its source VFS metadata is
regenerated after explicit installations. `clang-final-page-audit.json` records
the exact engine and compiler/linker hashes: 757 VFS entries, 101,203,972 file
bytes and a 32,629,160-byte self-contained HTML page.


The consolidated machine-readable evidence is
`build/engine/logs/clang-final-acceptance.json`. This plan is complete within
the documented guest WebAssembly compiler/linker profile. It has no remaining
required slices. Additional target backends, linker LTO, compiler-internal
threads, general C++ exceptions/localization and optional decoded-code eviction
are separate future work, not acceptance blockers for this profile.
