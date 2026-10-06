# Regression coverage migration

Stages 6B.22–6B.38 audit executor, caller-memory, continuation, path,
kernel descriptor, shared-memory, signal, wait, SELECT and terminal gates.
Stages 6B.39–6B.48 account for libc/DIY drivers, migrate the DIY language
and host-memory compatibility fixtures and expose ordinary setup failures.
Stage 6B.22 audits the executor smoke gate in `tests/c-engine-i32-smoke.c`.
Its 32 language-only assertions now live in
`tests/engine-regressions/i32-smoke.wast` and are removed from the C driver's
`main`. The installed `engine-regressions` group now contains twelve authored WAST
files and 1,230 assertions. They run through native `waste-test` and the production
browser worker. OCaml comparisons are required only for Wasm/WAT/WAST
language semantics and standard spec-test imports. POSIX-dependent fixtures
use C native/browser parity, private sanitizer gates and guest ABI/session
contracts; missing OCaml POSIX imports do not block their acceptance.

The dated OCaml results below record earlier experiments, not a backlog to
extend its kernel. No further OCaml kernel development is planned; the existing
kernel and application-runtime integration will be removed in deferred cleanup
under [the OCaml retirement plan](active-ocaml-language-oracle-plan.md).

Six shared event-driven sessions add 358 checks outside the ordinary batch.

These staged audits do not complete the wider C/CJS coverage audit.
The current `posix-kernel.c` inventory is complete: 21 helpers, 121 retained
CHECK sites and 261 runtime checks. Every retained ordinal and its guest
complement are classified in [the retained-kernel review](posix-kernel-retained-coverage.md).
The repository has 28 `.c` and 28 `.cjs` files under `tests` at this landing;
these include helper programs and driver modules, not 56 independent suites.
No CJS harness is removed. Host callbacks, private state, browser events and
sanitizer checks need separate coverage accounting before consolidation.

### Source-file inventory index

The inventory is **28 C + 28 CJS = 56 files**. A filename cited somewhere in
this document is not automatically a completed assertion-level audit; this
index distinguishes citations from files that still need explicit disposition.
As of 2026-10-05, **28/28 C files and 28/28 CJS files (56/56 total)** are named
in coverage entries outside this inventory index. Every source file now has
an explicit disposition (audited, helper/no runtime assertions, compiled
guest client, or pending/legacy coverage). “Named” remains a citation measure,
not proof that every assertion-level boundary is closed. The 56 cited files
still need their documented audit boundaries checked before being counted as
fully closed.

## Shared-library and coreutils CRT fixture audit (Stage 6B.89)

| File | Counted checks / role | Coverage and disposition |
| --- | --- | --- |
| `tests/c-engine-shared-lib-dylink.c` | **6 runtime checks** | Legacy manual parser probe: the test contains its own copy of `native_parse_dylink`, so it does not call the production function. It passes 6/6 on the checked-in PIC Wasm, but rebuilding the adjacent source with its documented compile/link command produces `dylink.0 memorysize=0/alignment=0` and the test passes only 4/6 (it expects 4/2). No maintained Make target exists. Treat as historical fixture evidence, not production parser coverage; a future shared-library work tranche should link-test the real parser and explain/rebuild the binary fixture. |
| `tests/c-engine-shared-lib-trivial.c` | PIC library input; no assertions | The three exported arithmetic/counter functions are represented by a checked-in `.so.wasm`, but that binary has no consumer outside the legacy parser probe. The documented build command succeeds but does not recreate the metadata expected by that probe. Keep classified as an unmaintained shared-library fixture until the dedicated loader plan resumes. |
| `tests/coreutils-sysroot-hello.c` | Startup fixture; no CHECK macro | Compiles through the generated freestanding sysroot and CRT. Its CRT now requires waste-libc stdio accessors even though this fixture writes via `env.write`; added fixture-local no-op accessors and `fflush` so it links without embedding full libc. Main rejects missing argv/envp and returns 7 when given an extra argument. The current target builds/audits imports but does not execute `_start`; keep the executable startup path as an acceptance follow-up. |

The shared-library probe was compiled with warnings-as-errors ASan/UBSan and
passed **6/6** against the checked-in binary; the source-built PIC module
produced the documented metadata mismatch and **4/6**. The
`coreutils-crt-fixture` target initially failed on missing `waste_stdin`,
`waste_stdout`, `waste_stderr` and `fflush`; after adding the test-only stubs,
it passes the sysroot build, Wasm import audit (only `env.exit`, `env.write`,
`waste_kernel.startup_v1`), and WAT generation. No production library behavior
was removed or translated to WAST.

## Guest SDK, SELECT ABI and source-loader audit (Stage 6B.90)

| File | Counted checks / role | Coverage and disposition |
| --- | --- | --- |
| `tests/guest-sdk-abi.c` | Compile-time ABI assertions and one guest-Wasm varargs check | Must remain a compiled guest C probe: it checks wasm32 type widths, public POSIX structure sizes/offsets/alignment, limits, and compiler `va_list`/default-promotion behavior. The encompassing `guest-sdk-check` also audits header order/dependencies, unavailable APIs, provider signatures and install mutations. That target currently fails in the negative-test phase: `inspect_sdk` reports `browser SDK binding review is stale` before the test's expected unprovided/signature-mismatch error. Do not count this ABI gate as passing until that review/test-ordering issue is resolved. |
| `tests/posix-select-abi.c` | **1,097 runtime checks** | Native ASan/UBSan gate passed. It checks fd-set bit operations and bounds, guest byte encoding/decoding, null handling, timeval/timespec conversions and validation, and `nfds` bounds. WAST/session tests cover select behavior, but do not replace these direct helper and guest-memory-layout checks. |
| `tests/source-loader.c` | **10 runtime checks** | Native ASan/UBSan gate passed. It checks CRLF and unterminated shebang handling, interpreter/argument extraction, body and line offset, invalid forms, and the bounded overlong case. Keep as direct parser-view coverage; end-to-end launch behavior remains separately covered by runtime tests. |

Commands: `make -C src/cli-rt BUILD_DIR=../../build/cli-rt posix-select-abi source-loader`
and `make -C src/html-rt BUILD_DIR=../../build/html-rt guest-sdk-check`.
The first passed; the second is blocked as described above.

## Parser reentrancy and legacy Bash driver audit (Stage 6B.91)

| File | Counted checks / role | Coverage and disposition |
| --- | --- | --- |
| `tests/wast-parser-reentrant.c` | 14 focused probes, repeated across four threads; 80 additional valid/invalid parse pairs | Native ASan/UBSan `parser-reentrant` target passed. Exercises concurrent parser-context isolation, nested comments, inline fields, raw binary, stream recovery and positions, shebang offsets, process-handler resume/completion/failure, VFS snapshots, strict WAT and diagnostics. Keep as private concurrency/stream/process integration coverage; WAST language tests cannot assert independent parser contexts or native handler cursor ownership. |
| `tests/bash-runtime.cjs` | Three outer smoke conditions | Legacy OCaml runtime driver; runs `build/ocaml/bash-runtime.wast` under sequential or threaded OCaml artifacts, checks process status/result marker and (for its default command) guest output. It depends on Node and the OCaml application runtime, so it is not evidence for the browser C runtime and should retire with that legacy runtime after equivalent C-runtime coverage is established. |
| `tests/bash-interactive-runtime.cjs` | Three outer smoke conditions | Legacy OCaml threaded-control-page driver; feeds newline and `exit`, then checks process status/result marker and an interactive prompt. Keep accounted as legacy behavior until the C-runtime interactive Bash test is accepted; the browser counterpart has its own harness. This is event/control-page coverage, not a portable WAST language test. |

Command: `make -C src/cli-rt BUILD_DIR=../../build/cli-rt parser-reentrant`
passed, including the four-thread sanitizer run. The two OCaml Bash scripts
were audited but not run: they require the legacy OCaml build and Node launcher.

## Legacy DIY drivers and native allocator audit (Stage 6B.92)

| File | Counted checks / role | Coverage and disposition |
| --- | --- | --- |
| `tests/diy-posix-test/posix-control-runtime.cjs` | One runtime completion check; embeds a Wasm module and external control worker | Legacy OCaml control-page integration: pauses the interpreter, resumes it, sends SIGINT and waits for the guest handler. The supported C session in `guest-session-diy-control.wast` covers start-installed handler plus SIGINT at SELECT yield (7 checks), but not pause/resume of a runnable busy loop. Retain this as a known legacy-only profile gap until OCaml retirement or an equivalent C-runtime control contract is decided; do not extend the OCaml kernel. |
| `tests/diy-posix-test/posix-kernel-runtime.cjs` | Awaits runtime completion and checks interpreter exit status; executes seven WAST kernel expectations in sequential/threaded modes | The guest assertions are accounted for in the DIY harness inventory; native kernel sanitizer checks (261) and browser/native C sessions cover the supported subset. Stop/continue and concurrent sleeping-job scenarios remain profile gaps. This driver only checks interpreter completion and remains legacy OCaml integration evidence pending retirement. |
| `tests/libc-test/allocator-native.cjs` | 5,000 allocation cycles plus bounds, alignment, overlap, canary, import-isolation and rejection guards | Real Wasm artifact stress via Node's WebAssembly API. It verifies no overlap/corruption as allocations grow and recycle, no kernel imports during the allocator workload, and that an actual `isatty` wrapper call trips the throwing import guard. `node tests/libc-test/allocator-native.cjs` passed: 16 pages, 12 one-page grows. Keep as native artifact/host-import coverage; ordinary WAST semantics do not assert JS import invocation counts. |

The CJS DIY drivers were audited but not run in this slice because they invoke
the legacy OCaml runtime through Node. Their source-level completion checks
are not substitutes for the native/browser C gates. `make -C src/cli-rt
BUILD_DIR=../../build/cli-rt diy-posix-control` passed seven checks, including
timeout and mismatch rejection. `make -C src/cli-rt
BUILD_DIR=../../build/cli-rt posix-kernel` passed 261 kernel checks and its
shared-memory regression; the allocator stress passed separately.

## Stage 6B acceptance reconciliation (Stage 6B.93)

The refreshed installed native corpus passes **289 / 2 XFAIL / 5 SKIP**, with
no unexpected failures. The strict OCaml language comparison passes all **261
supported official inputs**; four official inputs remain explicitly excluded
by policy, and repository POSIX/libc fixtures stay outside oracle scope. The
strict guest SDK gate now passes after refreshing the reviewed source hashes
for the shared POSIX-provider extraction and explicitly reinstalling the SDK:
56 public headers, six include orders, Wasm ABI and provider-negative checks,
and strict compiled signatures.

The C-engine worker harness runs **290 supported corpus entries** with two
expected XFAILs and no failures. Its extra POSIX-kernel PASS is intentional:
the WAST requires the browser-compatibility backend, so native marks it SKIP.
The offline-browser harness now expects the browser-specific summary **290
PASS / 2 XFAIL / 4 SKIP** rather than duplicating the native summary. `node
--check tests/c-engine-offline-browser.cjs`, the browser mounted-suite
controller test, HTML generation, and the C-engine Bash embedded VM matrix
pass.

The automated `file://` production-browser acceptance remains blocked on this host.
Both the normal Chromium launcher and direct binary terminate with SIGTRAP
before the DevTools protocol handshake; the harness sees `ECONNRESET`. The
direct-binary dump reproduces the same unsymbolized thread-9 offset without
Omarchy's injected extensions/Wayland flags; systemd reports `SI_KERNEL`, and
available memory was about 5 GiB. This establishes a Chromium startup failure,
not a page or worker failure; the exact Chromium trigger remains unresolved.
Firefox 155.0.1 was also tried as the requested alternative. The current
offline harness speaks Chromium's DevTools pipe protocol and this environment
has no Firefox driver; Firefox startup itself is also broken here: a headless
`file://` screenshot exits 139, and systemd-coredump records SIGSEGV in
`libxul.so` before page navigation. Thus neither browser produced page-level
evidence through the local automation attempts; the later user-run Bash-page
report below confirms the generated page and worker run successfully in the
user's browser.
The test expectation was corrected from the native backend's 289/2/5 counts to
the browser backend's 290/2/4 counts. Manual Bash-page evidence is now
available, but the automated dual-page/full-suite gate still needs a browser
that can be launched by its harness. External-executable inherited cwd and legacy
stop/continue, concurrent-sleep and runnable-loop pause/resume profiles remain
separate Stage 6B follow-ups.

### Manual production Bash-page batch (Stage 6B.94)

A user ran the complete `/bin/waste-test` batch from the generated `bash.html`
and saved [the report](../build/html-rt/stage6b-browser-results.json) and
[captured stdout](../build/html-rt/stage6b-browser-output.txt). Both files are
2,189,683 bytes and byte-identical: with `--json`, stdout is the JSON report,
and `--results` writes that same report into the guest VFS. The report contains
all 296 records and has **285 PASS / 4 TIMEOUT / 2 XFAIL / 5 SKIP**, `exitCode`
1, consistent with the user's subsequent `echo $?` result.

All four failures timed out at approximately 10 seconds; they have no
assertion-level report and therefore do not establish semantic failures:

- `core/bulk-memory/memory_copy.wast`
- `core/memory64/memory_copy64.wast`
- `core/simd/simd_f32x4_pmin_pmax.wast`
- `core/simd/simd_f64x2_pmin_pmax.wast`

They belong to groups with a 10-second default deadline. A focused rerun of
the same four files with `--jobs=1 --timeout-ms=60000` produced:

```text
PASS core/bulk-memory/memory_copy.wast
PASS core/memory64/memory_copy64.wast
PASS core/simd/simd_f32x4_pmin_pmax.wast
PASS core/simd/simd_f64x2_pmin_pmax.wast
Suite: 4 PASS, 0 FAIL, 0 XFAIL, 0 XPASS, 0 SKIP
```

This confirms the 10-second full-batch failures were deadline timeouts rather
than assertion mismatches. The focused JSON report was not present in the
copied artifacts, so assertion totals for those four files are not recorded.
The next check is the full mounted corpus with `--jobs=4 --timeout-ms=60000`.
The manual batch still records five skips, including
`diy-posix-test/posix-kernel.wast`, so it does not replace the dedicated
compatibility-backend worker acceptance run. The following slice records the
complete full corpus at the longer deadline.

### Full mounted corpus from the Bash page (Stage 6B.95)

The subsequent full run used `--jobs=4 --timeout-ms=60000` from `bash.html`.
The saved [JSON report](../build/html-rt/stage6b-browser-full-60s.json)
contains all 296 records and reports **289 PASS / 0 FAIL / 2 XFAIL / 0 XPASS
/ 5 SKIP**, `exitCode: 0`. This matches the native corpus summary. The four
legacy exception files remain unsupported; `diy-posix-test/posix-kernel.wast`
is skipped by the guest launcher because it requires the browser compatibility
backend. All four tests that hit the 10-second deadline in the first run pass
under the 60-second budget, both individually and in this full batch.

This confirms the packaged Bash page and full mounted guest test launcher in
the user's browser. The downloaded Bash Diagnostics report (from before the
compatibility path was ported) repeated the 289/2/5 counts and skipped
`posix-kernel.wast` with the explicit "requires browser compatibility backend"
reason. Stage 6B.96 ports this behavior into the Bash worker, removing the
need for a separate dashboard to run that compatibility fixture; the fixture
now runs from the unified Bash page.

### Browser-native fixture in Bash Installed tests (Stage 6B.96)

The Bash controller now reads browser-native execution specs from the mounted
`/tests/manifest.json`. It clears the C runner's compatibility skip only when
a packaged module/step spec is present, then runs that spec in an isolated Bash
worker with the compatibility imports and the standard deadline/cancellation
lifecycle. Results use the same per-assertion `done` record as WAST-stream
tests. Invalid or absent specs retain their explicit skip.

The regression loads the installed VFS and runs
`diy-posix-test/posix-kernel.wast` through the Bash controller: **7/7 checks,
1/1 module setup, zero skips**. The installed `diy-posix-test` group passes
through the packaged VFS (5 tests, 55 assertions). The full mounted-corpus VM
run reports **290 PASS / 0 FAIL / 2 XFAIL / 0 XPASS / 4 SKIP**. `./start.sh
--html-bash` rebuilt the offline page; its Bash browser VM smoke and frontend
archive guards pass. The offline-browser summary now expects four unsupported
legacy exception skips because the browser-native fixture is executable
through Bash. The user's full actual-browser report is available at
[`stage6b-browser-bash-compat.json`](../build/html-rt/stage6b-browser-bash-compat.json):
296 records, **290 PASS / 0 FAIL / 2 XFAIL / 0 XPASS / 4 SKIP**, exit code 0.
Its `diy-posix-test/posix-kernel.wast` record runs in `browser-native` mode and
passes all 7 checks with 1/1 module setup. Local direct Chromium and Firefox
launches still fail before navigation, but the refreshed Bash page has now
passed this full corpus in the user's browser. To reproduce, run from
`bash.html`:

```sh
/bin/waste-test --jobs=4 --timeout-ms=60000 --results=/tmp/stage6b-browser-bash-compat.json
echo __BATCH_EXIT__$?
```

Expect **290 PASS / 0 FAIL / 2 XFAIL / 0 XPASS / 4 SKIP** and exit 0. Then
download `/tmp/stage6b-browser-bash-compat.json` with `/bin/download` and place
it in `build/html-rt` for review. Reload the page first if its guest `/tmp` is
near capacity; do not redirect the large report to a second output file.

## Corpus scheduling and offline package helper audit (Stage 6B.88)

| File | Explicit assertions / role | Coverage and disposition |
| --- | --- | --- |
| `tests/corpus-schedule.cjs` | Pure scheduling helper; no local assertions | Validates schedule/timing flags, JSON shape, identity uniqueness and nonnegative finite durations; sorts by descending duration while preserving manifest order on ties and placing new tests last. Retain as shared native/browser orchestration policy. |
| `tests/corpus-schedule-check.cjs` | **27 explicit assertion sites**, including data-driven malformed-history cases and spawned integration runs | Covers manifest and longest-first order, timing lookup for filtered/new tests, stable ties, invalid CLI combinations and history records, native parallel dispatch/result order, file filtering, timeout reporting, worker replacement and process reuse in both runtimes. Retain as Node host orchestration coverage. |
| `tests/offline-html-package.cjs` | Package reader utility; no local assertions | Extracts concatenated embedded base64 gzip/tar members; checks checksum, member bounds, duplicate names, traversal/absolute paths and regular-file/directory-only types. Retain for host harness inspection of the actual generated archive, distinct from browser decompression/DOM boot checks. Its valid-archive path is exercised by the packaged Bash probe and VFS packaging suite; dedicated malformed embedded-archive fixtures are not present. |

`node tests/corpus-schedule-check.cjs` passes the synthetic native/browser
integration matrix. The packaged Bash executable probe passes **7/7** through
`readOfflinePackage`. `node tests/c-engine-vfs-packaging.cjs
build/html-rt/bash.html` passes archive-to-installed-file byte/metadata parity,
mounted native paths, malformed inventory/install rejection and atomic update
guards. Syntax checks pass. No Chromium was launched. The scheduler test had
to run outside the sandbox because it spawns a nested Node executable; its
first sandbox attempt was rejected with `EPERM`.

## Binary reader, import decoder and validator C gate audit (Stage 6B.87)

| File | Explicit C check sites | Behavior and disposition |
| --- | ---: | --- |
| `tests/wasm-binary-primitives.c` | **80** | LEB128 u32/u64/i32/i64/s33 round trips and canonical bytes; overlong, truncated, overflow and too-long inputs with cursor rollback; bounded reader/subreader behavior; writer encode/take ownership and roundtrip. Loops multiply runtime checks beyond static sites. Retain as private parser/encoder boundary coverage. |
| `tests/wasm-import-decode.c` | **49** | Hand-built binary import section covering function, table, memory, global and tag descriptors; import-only decode, no-import section, copied-source lifetime after caller bytes are overwritten, disposal/reset, malformed UTF-8/trailing bytes/truncation. Retain as decoded-structure, ownership and malformed-input coverage. |
| `tests/wasm-validation.c` | **11** | Directly constructed internal instruction/type records check invalid versus unsupported status and source location, branch/call/local resolved metadata, and one-time validation. Retain as private validator-result and metadata coverage. |

Official WAST tests and the OCaml language oracle cover accepted/rejected module
behavior and language semantics, but do not replace direct assertions about
reader cursor rollback, decoder ownership/reset, exact import descriptor
records, or internal validation metadata. These are C implementation
boundaries; no language assertion was migrated or removed. All three
warnings-as-errors ASan/UBSan gates pass:
`wasm-binary-primitives`, `wasm-import-decode`, and `wasm-validation`.

## External probe and session/worker host audit (Stage 6B.86)

| File | Counted checks / role | Coverage relationship and disposition |
| --- | --- | --- |
| `tests/c-engine-waste-probe.c` | Guest C executable compiled to Wasm; checks write/startup imports, descriptor `F_GETFD`/`F_SETFD` transition `0,0,0,0,1,0,0`, startup pointers, argv/env/cwd/PID output, success and explicit exit 7 | Retain as a minimal guest ABI probe. Bash's staged and packaged runtime test runs it twice, verifies argc/argv[0], env count, positive PID and inherited `/root` cwd, checks status 0 and recovery after missing-command status 127. Native startup-block pointer semantics are checked directly by exec-matrix. It complements rather than duplicates the private exec-transition C gates. |
| `tests/guest-session-browser.cjs` | **36 explicit assertion sites**, dynamically exercised across event lists; direct Wasm adapter for staging VFS/files/cwd, input, EOF, resize, signals, clocks, process ids, waits, host upload/download, result records and exit status | Retain the direct-Wasm browser counterpart. The `io` scenario passes native/browser parity **7/7**, output `FIRST\nSECOND\nEOF\n`, exit 7; the same driver is invoked by `guest-session-check.py` for the session matrix. Production-worker checks are a separate layer. No assertions moved or removed. |
| `tests/c-engine-worker-host.cjs` | Test infrastructure, not an assertion suite: adapts `worker_threads` messages to the browser Worker shape, injects engine bytes, and serializes missing-handler/worker exceptions as error messages | Retain as the isolated worker host used by `c-engine-browser-runtime.cjs`. Its normal-path smoke through `engine-regressions/i32-smoke.wast` passes **1/1**; Stage 6B.97 adds isolated missing-handler and thrown-handler forwarding checks. Timeout and worker replacement remain owned by the parent runtime harness. |

Validation for this tranche: Bash full-package executable probe **7/7**;
shared native/direct-Wasm `io` session **7/7** plus linked-provider isolation
**3/3**; legacy worker-host `i32-smoke.wast` **1/1**. All used Node automation;
no Chromium was launched. Stage 6B.97 found that startup-block offset 20
pointed at the beginning of the argv/envp string region (and therefore
`argv[0]`) instead of the cwd string. See the Stage 6B.97 landing record for
the correction and passing native/browser regressions.

## Final source-inventory citation audit (Stage 6B.98)

The exact-path scan found four authored files whose detailed coverage was
already described under short names or another subsection but which lacked an
exact `tests/...` citation. Their ledgers and dispositions are:

| File | Counted boundary and relationship | Verification/disposition |
| --- | --- | --- |
| `tests/c-engine-caller-instance.c` | **32 C CHECKs** for manual host callbacks reading the correct caller instance, nested cross-module trampolines, decode-once/two-instance independence, memory object/backing identity and bounded reads. `engine-regressions/caller-memory.wast` supplies 12 guest-visible counterparts using real pipe/read/write imports; it cannot replace callback-pointer or ownership checks. | `make -C src/cli-rt caller-instance` passes ASan/UBSan, authored/installed snapshot comparison, 12/12 authored WAST assertions and the installed sanitizer batch. Retain private callback and memory-identity checks. |
| `tests/c-engine-continuation.c` | **43 C CHECKs** for synthetic yield status/reason, callback counts, continuation capture/restore/replay, direct/indirect/nonlocal returns and provider/consumer evaluator frames. `engine-regressions/continuation-waits.wast` contributes 23 guest-visible wait/result/canary assertions. | `make -C src/cli-rt continuation` passes ASan/UBSan, authored/installed snapshot comparison, 23/23 authored WAST assertions and the installed sanitizer batch. Retain private evaluator snapshot and replay checks. |
| `tests/c-engine-frontend-packaging.cjs` | Assertion coverage across embedded tar extraction, Bash page asset bytes, offline references, VFS/corpus source metadata, generator staging protection, and rejection of the retired test-page target. These check packaged artifacts and filesystem bytes, so WAST cannot provide equivalent coverage. | `node tests/c-engine-frontend-packaging.cjs` checks the single Bash/test page and retained focused-worker corpus metadata. |
| `tests/c-engine-offline-browser.cjs` | Real `file://` navigation, DOM/worker startup, WebGL terminal output/input, external-request absence, installed-test selection/results, session waits/cancellation, and optional full corpus. These are browser/page integration checks and remain host coverage. | User-confirmed Firefox run passes page boot, Bash redirection, and the 296-record installed corpus with 290 PASS / 0 FAIL / 2 XFAIL / 0 XPASS / 4 SKIP. Retain the focused offline-browser gate for a working Chromium host. |

The exact-path inventory now cites all **56/56 authored C/CJS files**. This
closes the uncited-source queue. Stage 6B.99 records the consistency check of
the other 52 references against their assertion ledgers and current gates.

### Stage 6B.99 audit-ledger consistency closure

Rechecked the 52 citations that predated the Stage 6B.98 additions. Each points
to an assertion table or a dedicated audit section containing the relevant
counts, guest complements, and retained host/private-engine boundaries. No
unsupported citation or undisposed authored test source was found. Combined
with the four explicit audits above and the production-browser result recorded
in Stage 6B.96, this closes the Stage 6B coverage-accounting checklist. Stage
6C is complete; Stage 7 dashboard retirement is recorded in the active browser
VFS plan.

## Process lifecycle C gate audit (Stage 6B.53)

`tests/c-engine-process-lifecycle.c` retains **117 runtime CHECKs**. Its
assertions divide into eight contiguous ranges so every check has an explicit
guest-coverage disposition:

| CHECK ordinals | Behavior | Guest/session complement | Disposition |
| --- | --- | --- | --- |
| 1–6 | Borrowed-memory clone ownership; process address-space regions, overlap, allocation and clone metadata | None for pointer/region identity | Retain as private process-capsule invariants |
| 7–13 | Pipe/handler setup, fork identity, PID/PPID and runnable capsule selection | `guest-session-boundaries.wast`, `guest-session-linked-fork.wast` | Keep end-to-end checks; native state assertions remain complementary |
| 14–19 | Entry validation, widened function-count arithmetic, handler size/kind rejection | No guest API exposes these parser/host bounds | Retain as native input-validation checks |
| 20–21 | Cloning a blocked handler resets the child without mutating the parent's wait state | Handler READ/SELECT behavior in `guest-session-handlers.json` | Keep direct clone-state invariants |
| 22–31 | Attached handler context, store cursor/result bridges, suspend/resume and yield-reason validation | Resumed WAST assertions in `guest-session-handlers.json` | Retain bridge contracts; guest checks cover observable resume behavior |
| 32–46 | Exec transition preparation/abort, child selection, exit-code bounds, queued-result and parent-wake rejection | `guest-session-handler-start.wast`, `guest-session-boundaries.wast` | Keep atomic transition-state checks; session tests cover successful outcomes |
| 47–96 | Handler cursor state machine, context lifetime, inherited descriptor, completion, wake consumption, zombie/reap, wait errors and group signal status | `guest-session-handlers.json`, `guest-session-boundaries.wast`, `guest-session-process-groups.wast`, `guest-session-signal-pgid-fork.wast` | Retain state/ownership checks; session fixtures cover guest-visible results and waits |
| 97–117 | mmap page translation/protection, file-object and VMA lifetime, shared-file cache/page identity, forked MAP_SHARED writes and reaping | `tests/diy-posix-test/mmap.wast`, `guest-session-linked-fork.wast` | Retain exact page-pointer, mapping-record and fork-root ownership checks; guest tests cover mmap calls and private-memory isolation |

This is a coverage audit, not an assertion deletion. The session/WAST fixtures
do not expose capsule cursor fields, transition labels, cached-page identity,
mapping-record splits or root-clone ownership, so translating those checks
would weaken their contract. The audit did expose a compiler warning promoted
to an error: loaded-library names used the 256-byte path bound despite accepting
the 512-byte WAST name bound. `native_loaded_library.name` now uses
`WAST_MAX_EXPORT_NAME`; the corresponding path remains independently bounded.
The `process-lifecycle` Make target is the warnings-as-errors ASan/UBSan gate.

## Exec-transition matrix C gate audit (Stage 6B.54)

`tests/c-engine-exec-matrix.c` retains **58 runtime CHECKs**, grouped below so
every assertion has a guest-coverage disposition. The `CHECK` macro definition
is not counted.

| CHECK ordinals | Behavior | Guest/session complement | Disposition |
| --- | --- | --- | --- |
| 1–16 | Executable registration visibility, VFS permissions, terminal-kernel replacement, missing/non-executable/symlink metadata, and duplicate/invalid executable rejection | `guest-session-matrix.json` runs installed `/bin` commands and checks their observable results | Keep metadata and registration-boundary checks; sessions cannot assert manifest/kernel records or registration error codes |
| 17–27 | Missing/inactive/directory/symlink instantiation rejection; candidate startup PID, cwd, argv, environment and startup-block pointer | Session matrix checks shell-visible command output and statuses | Retain pre-commit image construction and startup ABI checks |
| 28–33 | Image reference/checkpoint pins, continuation table restoration, and atomic image-commit validation | No session-level view of continuation/reference counters or commit labels | Retain private lifetime and transition invariants |
| 34–44 | Handler payload ownership transfer, size limits, invalid/oversized/queued request rejection, successful handler commit, and rejection of image replacement or duplicate handler commit | `guest-session-matrix.json` covers command return and prompt recovery, not native handler ownership | Retain transfer/atomicity and rejected-context lifetime checks |
| 45–49 | Handler completion validation and forked process graph engine-table independence/reaping | Guest sessions exercise process execution and exit; linked-fork fixtures exercise guest fork behavior | Keep engine-table and owned-root checks as native complements |
| 50–58 | Trap/signal shell status, child selection, exit, parent restoration and wait/reap | `guest-session-matrix.json` checks `/bin/true` and `/bin/false` status; process/session fixtures cover waits | Retain direct status encoding and native lifecycle checks; shared sessions verify observable outcomes |

The `exec-matrix` warnings-as-errors ASan/UBSan target passes **58/58**. This
is an audit only: no C assertions were removed and no WAST duplication was
introduced. The user-visible command contract belongs in guest sessions; native
metadata, candidate ownership, continuation pins, handler payload lifetime and
process-engine graph checks remain private-state gates.

## Exec lifecycle continuation C gate audit (Stage 6B.55)

`tests/c-engine-exec-lifecycle.c` retains **22 runtime CHECKs** across two
controlled child-exec cases (failed exec and successful exec). The same
assertion sequence is run for each case, with one additional exit-127 assertion
in the failed-exec case.

| Check group | Behavior | Guest/session complement | Disposition |
| --- | --- | --- | --- |
| Shared setup in both cases | Fixture bytes/load/export, fork yield, parent continuation capture, wrong-engine resume rejection | `guest-session-handler-start.wast`, `guest-session-control-exec.wat`, and shared command sessions exercise real guest exec/process behavior | Keep the direct scheduler/API checks; portable guest tests do not control this host-yield boundary |
| Child path | Exec yield; failed-exec retry returns `EXEC_ERROR_EXIT` with 127 (failed case only) | Guest sessions verify shell-visible command completion and status | Retain deterministic host-controlled failed-exec branch |
| Parent path | Resume captured fork continuation, parent result, wait/write activity, and rejection of a second resume | Guest sessions verify resumed shell/process behavior end to end | Retain continuation state and exactly-once resume checks |

The `exec-lifecycle` warnings-as-errors ASan/UBSan target passes **22/22**.
The audit also found that the test host-write callback used removed
`exec_memory.data` storage. It now copies through `exec_memory_read`, matching
the engine's sparse page-backed memory and retaining bounds/error behavior.
This is a native scheduler gate, not a candidate for WAST-only translation: the
host deliberately controls the yields that WAST describes but cannot generate.

## Process-contract C gate audit (Stage 6B.56)

`tests/c-engine-process-continuation.c` retains **12 runtime CHECKs**. The
ordinals include the explicit memory-reset assertion added during this audit.

| CHECK ordinals | Behavior | Guest/session complement | Disposition |
| --- | --- | --- | --- |
| 1–2 | Fixture read and process-control module load | Shared session fixtures execute the installed guest process stack | Retain minimal harness setup checks |
| 3–7 | Child exit code 127, child/parent fork counts, no child wait, and child memory marker | `guest-session-linked-fork.wast` and `guest-session-waits.wast` cover observable fork/wait outcomes | Keep ABI and phase-selection assertions in the native contract gate |
| 8 | Clear memory marker before the separate parent invocation | No matching host-harness operation is visible to guest WAST | Retain to prove the parent marker is newly written |
| 9–12 | Parent invocation/result, encoded wait status, reap PID/status and parent memory marker | Shared fork/wait sessions check guest-visible process results | Retain direct import ABI and memory-effect checks; later continuation/session fixtures provide end-to-end coverage |

The `process-continuation` warnings-as-errors ASan/UBSan gate passes **12/12**.
The legacy callback had direct references to removed `exec_memory.data`; it now
uses `exec_memory_read` and `exec_memory_write` against sparse page-backed
memory. The guest-memory clear is separately checked before starting the parent
phase. This focused contract probe complements newer continuation tests and is
not a WAST-only translation target.

## Repeated wait continuation C gate audit (Stage 6B.57)

`tests/c-engine-two-wait.c` retains **11 runtime CHECKs**.

| CHECK ordinals | Behavior | Guest/session complement | Disposition |
| --- | --- | --- | --- |
| 1–4 | Fixture open/read, module load and `run` export lookup | Shared WAST/session harnesses load and invoke guest modules | Keep minimal native fixture setup checks |
| 5–7 | First host READ yield, continuation capture and resume | `guest-session-waits.wast` exercises real wait/read behavior | Retain the explicit engine continuation transition |
| 8–10 | Second READ yield followed by a fresh capture and resume | Session readiness tests validate externally observable wakeups | Retain recapture-after-resume coverage, not exposed by a session contract |
| 11 | Final result is 3 after exactly four host read calls | Guest sessions assert completion and output | Retain replay/call-count check to prove neither read step was skipped or repeated |

The `two-wait` warnings-as-errors ASan/UBSan gate passes **11/11**. This is a
small evaluator state-machine probe using host-generated yields; real guest wait
semantics remain covered by the session fixtures. No checks were migrated or
removed.

## Store checkpoint C gate audit (Stage 6B.58)

`tests/c-engine-store-checkpoint.c` retains **32 runtime checks**. The ranges
below account for checkpoint behavior that is private to native store state.

| CHECK ordinals | Behavior | Guest/session complement | Disposition |
| --- | --- | --- | --- |
| 1–2 | Sparse memory and explicitly shared-memory fixture setup through the public access API | Guest memory and isolation fixtures exercise observable memory behavior | Retain host fixture setup and shared-page identity prerequisites |
| 3–9 | Handler snapshot rejection without publishing state; process kernel/file-mapping topology setup; descriptor close; initial checkpoint capture | No WAST can inspect native handler snapshots, file-object references or mapping records | Retain unsupported-state and topology invariants |
| 10–17 | Mutate/grow/fill memory; nested checkpoint capture/restore; shared-page alias identity; table64 growth/function owner restore; follow-up mutation; outer restore | Guest state fixtures cover operations and results, but not nested snapshot boundaries | Retain nested snapshot semantics and host table ownership |
| 18–27 | Restore file mapping topology and imported memory/table/global identity; read restored memory; restore growth/bytes, table contents, unimported spectest table64 defaults, global and evaluator yield frames | WAST/session isolation tests are externally visible complements only | Retain native store-identity, restore and evaluator-frame assertions |
| 28–32 | Clone creation/binding, cloned shared-memory writes visible through provider, local clone-provider resolution | Guest tests exercise shared memory results but not clone binding graph identity | Retain clone ownership/binding checks |

The `store-checkpoint` warnings-as-errors ASan/UBSan target passes **32/32**.
The portable language/session layer checks guest-visible effects; these C checks
remain necessary for snapshot publication/rejection, imported-object identity,
mapping topology, sparse-page aliases, evaluator state and clone ownership.
This audit did not move or remove assertions.

## Installed VFS inventory C gate audit (Stage 6B.59)

`tests/c-engine-vfs-inventory.c` contains **27 runtime `assert` sites** plus
one compile-time capacity assertion. The runtime loops execute **4,380 checks**
for the current installed snapshot. The count is derived from 497 entries, 448
regular files, 960 maximum installed entries, and the 64-node runtime reserve:

| Source group | Site count | Current executions | Behavior / complement | Disposition |
| --- | ---: | ---: | --- | --- |
| `capacity_check` | 9 | 1,032 | Build/parse capacity fixture, preserve old inventory on failed replacement, mount 960 entries, create exactly 64 runtime nodes, reject the next node | Retain capacity and overflow contract |
| `main` setup and current inventory walk | 8 | 2,341 | CLI argument/load/mount; for every installed entry compare kernel metadata and for each regular file compare snapshot length and bytes | Mounted-path WAST covers guest-visible file access; retain host/kernel metadata and exact byte checks |
| Mutation/isolation and source reread | 8 | 1,000 | Truncate one kernel's `/usr/bin/echo`, verify second kernel unchanged, reload source and compare every entry's metadata/bytes | Retain per-kernel isolation and host snapshot immutability |
| Malformed inventory inputs | 2 | 7 | Reject six malformed/version/duplicate/empty manifests and an over-limit input | Retain parser-boundary checks |

The current inventory total is 497 entries (448 regular files, 49 directories).
`make -C src/cli-rt vfs-check` passes the ASan/UBSan C gate,
`tests/vfs-mounted-paths.wast` (**8/8**), `tests/guest-sdk-mounted.wast`
(**12/12**) and `tests/vfs-directory-check.py`. Guest checks establish access
and modes through real imports; the C gate retains exact host inventory bytes,
metadata, kernel isolation, parser preservation and capacity. No assertions
were moved or removed.

## Browser VFS staging CJS gate audit (Stage 6B.60)

The browser staging gate consists of `tests/c-engine-vfs-browser.cjs` and the
shared `tests/vfs-package.cjs`; together they contain **20 assertion sites**
and execute **6,294 assertions** with the current package:

| File / group | Sites | Executions | Behavior and disposition |
| --- | ---: | ---: | --- |
| `vfs-package.cjs`: extracted byte length and SHA-256 | 2 | 892 | One size/hash check per 446 non-directory/non-interpreter files; retain package-integrity validation |
| `vfs-package.cjs`: allocation and staging return | 2 | 5,364 | Six complete staging cycles × 447 buffers (inventory + 446 files) × two checks; retain Wasm pointer and staging boundary checks |
| `vfs-package.cjs`: ready state | 1 | 6 | Verify every complete package transfer reaches ready state |
| `c-engine-vfs-browser.cjs`: staging allocation; malformed inventory/file rejection; incomplete readiness; repeated mounted-path scripts; compiled SDK probe; generated corpus probe; offset bound and result counts | 15 | 32 | Retain host orchestration/results checks; guest assertions themselves run through the embedded WAST engine |

The gate verifies malformed manifest and file rejection, two mounted-path WAST
fixtures twice each in freshly staged stores, compiled SDK ABI/stat/signal
canaries, and all **296** packaged test-corpus paths by opening and reading each
to EOF with exact length and endpoint bytes.
`node tests/c-engine-vfs-browser.cjs build/html-rt/bash.html` passes. This Node
use is limited to browser/package-host testing; ordinary CLI/browser guest WAST
execution remains direct in the runtimes. No assertion was removed or migrated.

## Tarballjs loader CJS gate audit (Stage 6B.61)

`tests/vfs-tar-loader.cjs` contains **5 assertion sites / 10 executions**:

| Assertion site | Executions | Behavior | Disposition |
| --- | ---: | --- | --- |
| `loadBinary(fullName)` byte equality | 1 | Preserve a full-width 100-byte member name | Keep archive path compatibility check |
| `loadBinary(prefix + leaf)` byte equality | 1 | Join ustar prefix and 100-byte leaf correctly | Keep ustar path-prefix behavior |
| `loadBinary("__proto__")` byte equality and prototype check | 2 | Preserve content and use a prototype-safe archive map | Keep adversarial key handling |
| `assert.rejects` over malformed archives | 6 | Reject corrupt header checksum, traversal, absolute path, duplicate normalized path, unsupported entry type and truncated archive | Keep all malformed-package boundaries |

`node tests/vfs-tar-loader.cjs` passes using the production
`submodules/tarballjs/tarball.js` and `src/html-rt/src/loader.js`. This is an
archive/host parser test rather than guest semantics, so it remains a focused
Node gate while guest WAST continues to run directly in cli-rt and html-rt.

## VFS packaging CJS gate audit (Stage 6B.62)

`tests/c-engine-vfs-packaging.cjs` has **19 assertion sites / 488 executions**
for the current installed package (446 non-directory/non-interpreter files):

| Assertion group | Sites | Executions | Behavior and disposition |
| --- | ---: | ---: | --- |
| Package exclusion and offline/source byte parity | 2 | 447 | Reject retired `vfs-image.bin`; compare each packaged file byte-for-byte with `src/vfs` | Keep package content contract |
| Tar archive and native CLI | 4 | 4 | Create archive, audit archive metadata/content, run mounted-path WAST natively and require 8 passes | Host packaging plus native runtime parity |
| Copied-directory audit | 1 | 1 | Audit copied VFS while removing empty runtime directories and changing host mtime | Verify host mtimes are not guest metadata |
| Malformed inventory mutations | 2 | 10 | Five cases reject duplicate/conflicting destination, traversal, absolute path, wrong root ordering, and missing mandatory/alias entries with expected diagnostics | Keep fail-before-accept diagnostics |
| Exact copy and invalid compiler/component batches | 5 | 21 | Preserve POSIX metadata through `cp -a`; reject five missing/partial/unknown-import/conflicting-component batches and assert inventory plus upload binary stay unchanged | Keep atomicity and ABI review guards |
| Successful refresh and edited snapshot defense | 5 | 5 | Refresh echo alias atomically, re-audit, then reject modified installed output | Keep successful publish and post-install drift checks |

The accounting includes the dynamic per-file parity loop: the package contains
446 files, and the one top-level retired-image check makes 447 executions in the
first group. `node tests/c-engine-vfs-packaging.cjs build/html-rt/bash.html`
passes. This host packaging gate complements the native inventory contract and
browser staging harness; guest behavior is covered by the 8 mounted-path WAST
assertions. No implementation change or assertion migration was needed.

## Browser VFS transfer CJS gate audit (Stage 6B.63)

`tests/c-engine-vfs-transfer.cjs` has **6 explicit assertion sites / executions**
and **7 protocol waits** (initial prompt, five command prompt completions, and
worker exit):

| Check | Count | Behavior | Guest/session complement | Disposition |
| --- | ---: | --- | --- | --- |
| Download object count, name and bytes | 3 | Successful upload/download roundtrip preserves binary payload `[00 01 7f 80 ff 0a 41 00]` and filename `roundtrip.bin` | `guest-session-transfer.json` checks upload/download success outcomes | Retain browser event payload and filename assertions |
| Download count after cancellation/missing file | 1 | Cancelled upload creates no file that can be downloaded | Guest session also checks cancelled upload/download return values | Retain worker-backed filesystem side-effect check |
| Final `done.ok` and upload count | 2 | Worker exits cleanly after another shell command; host sees exactly two upload requests | Guest session covers transfer calls, not terminal/worker lifecycle | Retain asynchronous host callback and recovery checks |
| Protocol prompt/event waits | 7 | Initial ready prompt, successful upload, successful download, cancelled upload, missing download, follow-up echo and final exit | No direct WAST equivalent for terminal prompts or worker event ordering | Retain event-sequencing checks |

The browser-worker harness uses delayed host replies: the first upload returns
opaque binary bytes and the second returns cancellation. It verifies shell status
and prompt recovery around both flows. `node tests/c-engine-vfs-transfer.cjs
build/html-rt/bash.html` passes. The harness had unused `treeVfs` and
`stageVfs` imports, which were removed.

## Frontend packaging crash diagnostics (Stage 6B.64)

The frontend packaging harness writes synchronous checkpoints to
`build/html-rt/frontend-packaging.log` (override with
`WASTE_FRONTEND_PACKAGING_LOG`). It records entry into each page/package check,
per-VFS-file and corpus-source comparisons, each generator/package guard, an
uncaught exception stack, and normal exit status. Synchronous append preserves
the last recorded step if a later comparison or child process causes
termination; subsequent runs append instead of erasing earlier crash history.

The first full run logged its last checkpoint at the start of the Bash page
generic assertions, then ended without an uncaught-exception or normal-exit
record. A read-only host check found no coredump or kernel OOM/kill event in the
run window; this does not identify why the process stopped. The harness source
contains no Chromium launch or desktop keyboard/window automation. Generic page
assertions now have individual pre-checkpoints to narrow the next trace. The
existing `readOfflinePackage` helper already extracts the gzip tar from the
static HTML data URLs and checks its members without starting a browser. Large
Buffer `assert.deepEqual` calls now use exact `Buffer.equals` comparisons with
concise path/length diagnostics. A large failure diff is a plausible resource
trigger, but this crash trace did not identify which assertion was reached.

After the Stage 6B.65 package rebuild, `node tests/c-engine-frontend-packaging.cjs`
passes against both refreshed pages. A separate real-page attempt is recorded
in that stage: Chromium dumped core before returning page results, so static
packaging success does not establish actual browser startup.

## Terminal JavaScript fixtures (Stage 6B.66)

`tests/c-engine-terminal-model.cjs` has **21 direct check sites / 21
executions**. They cover printable cells, SGR color, erase/home/cursor
positioning, alternate-screen restore, Rogue REP and character-set behavior,
application cursor/keypad modes, cursor visibility, split CSI parsing, ECH
count/clipping/background semantics, REP state isolation, and resize dimensions.

`tests/c-engine-terminal-glf.cjs` has **3 condition sites / 13 executions**:
one initialized-asset check, eleven expected Unicode glyph-range lookups, and
one geometry shape check over **135,687 vertices / 63,123 triangles**. The
fixture validates selected cmap ranges and asset structure rather than visual
pixel output.

Both gates pass. They run the production terminal model/renderer source in a
Node VM without Chromium, DOM, WebGL, Bash, or the Wasm engine. These are
JavaScript implementation tests, so WAST cannot preserve their coverage. Keep
them as small Node-only checks; separate them from the Node-free native guest
test path. Actual browser rendering and resize remain distinct Chromium/DOM
coverage.

## Production-worker pump cancellation CJS gate (Stage 6B.67)

`tests/guest-session-pump-cancel.cjs` has **4 assertion sites** and **2
protocol wait gates**. It starts `guest-session-pump-cancel.wast`, waits for the
guest's `P` marker, sends an external `cancel` message during the configured
10 ms dispatch pump, then waits for worker completion. Assertions require
`done.ok === false`, `done.cancelled === true`, cancellation within 2 seconds,
and `done.exited === false`.

`node tests/guest-session-pump-cancel.cjs` passes; this run cancelled in 12 ms.
The WAST fixture supplies the runnable guest loop, but cannot originate the
host cancellation event or verify the production worker's completion message.
Keep this focused production-worker boundary check until a native host driver
exercises the same asynchronous pump/event contract. Removed unused VFS helper
imports and the unused worker-start counter; test behavior is unchanged. This
gate uses Node VM only and does not launch Chromium.

## Installed browser-worker batch CJS gate (Stage 6B.68)

The `--installed-group=engine-regressions` branch of
`tests/browser-test-suite-runtime.cjs` has **2 explicit harness assertions**:
the report must have exit code zero, and it must contain a nonempty set of tests
all assigned to the requested group. The underlying run executed **12 WAST
files / 1,230 assertions** through production `worker.js` in isolated Node
worker threads.

The same installed group was run with `build/cli-rt/waste-test`. Saved native and
worker reports under `build/cli-rt/native-engine-regressions.json` and
`build/html-rt/browser-engine-regressions.json` compare equal for the summary,
ordered test identities/status/counts, and **all 1,230 ordered assertion
function/outcome pairs**. Both runners report 12 PASS, zero failures, and no
skips. This run uses Node worker threads but no Chromium or DOM. It validates
the installed browser-worker batch path against the native runner.

The synthetic controller branch has **98 static assertion call sites**. Its
coverage spans manifest/catalogue rejection, expected-failure/skip policy,
selection and concurrency order, host-memory isolation, guest-command wire
validation, early cancellation, missing assets, timeouts and worker recovery,
Unicode result identities, yield/trap and command-stream results, setup-phase
diagnostics, segment/table limits and operations, and worker teardown. The
fixtures intentionally include expected FAIL/TIMEOUT/CANCELLED outcomes as
well as successful cases; these are harness assertions, not corpus failures.
`node tests/browser-test-suite-runtime.cjs` passes the full synthetic branch.
It uses Node worker threads without Chromium and preserves host scheduling,
event, and teardown boundaries that ordinary WAST cannot drive. Removed its
unused `packageVfs` and `stageVfs` imports.

## Execution-control worker CJS gate (Stage 6B.70)

`tests/guest-session-control-browser.cjs` has **17 static assertion sites**.
Its Wasm branch expands them across six runnable fixtures under both deadline
and cancellation (12 interrupts), checks five stop/result fields per run, then
reuses the same instance for a three-assertion recovery session. The packaged
worker branch uses four scenarios: loop timeout/cancel and blocked-I/O
timeout/cancel. It checks that each completes with the matching stop reason and
does not report a guest exit. This gate includes host scheduling and the worker
completion message, which WAST cannot trigger or inspect.

The packaged I/O cases need three WAST assertions to complete before stdin
blocks. A 100 ms limit was too short in the Node VM run; the harness now grants
that fixture 1 s for both timeout and cancellation, while preserving the loop
probe's 100 ms timeout budget. `node --check` and the direct harness invocation
with `build/html-rt/bash.html` pass. That path extracts `worker.js` and Wasm from
the embedded tar archive and runs the production worker in a Node VM; it does
not launch Chromium.

The companion native ASan/UBSan check, invoked with
`python3 tests/guest-session-control-check.py --page build/html-rt/bash.html`,
was attempted but did not pass in this environment: its sanitizer executable
returned 1 during the first deadline case after reporting the expected timeout,
with LeakSanitizer stating it cannot operate under the runner's ptrace
environment. Native sanitizer status remains unverified; this does not affect
the passing Wasm/worker VM checks.

## Packaged guest-session worker CJS gate (Stage 6B.71)

`tests/guest-session-worker.cjs` has **19 static assertion sites** covering the
production `worker.js` and suite-controller bytes from the embedded package,
inventory identity, external wait/event delivery, output and result parity,
diagnostic payloads, and guest exit status. Direct packaged-worker runs pass
linked-fork (3/3), terminal-control (9/9), and the WAST-handler contract
(22/23; its one mismatch is expected by that shared contract). The handler
scenario was also run through `tests/guest-session-check.py --scenario handlers
--page build/html-rt/bash-next.html`, comparing native, direct Wasm, and
packaged-worker results and transcripts. The C-engine Bash worker smoke passes
7/7. These checks use Node workers/VMs and do not launch Chromium.

The audit fixed two invalid assumptions. First, the production worker queried
kernel paths after `waste_wast_run_script` returned, but that API tears down the
per-session kernel before returning. Removed the post-completion queries while
preserving the VFS path announcement; inventory staging and install-result
checks remain the validity gates. Second, the CJS harness now checks `done.exited`
against `scenario.exited ?? true`, so the shared linked-fork `exited: false`
expectation is represented correctly. The app snapshot in `src/vfs` was
refreshed with `vfs-install-app` before rebuilding the package.

## Terminal control browser CJS gate (Stage 6B.72)

`tests/guest-session-terminal-browser.cjs` has **27 static assertion sites**
across direct Wasm exports and the packaged production worker. The direct path
rejects nine invalid/unavailable clock, resize, and signal values; applies the
shared fixture's resize/signal/input events; checks all nine WAST assertions;
then runs linked-fork in the same Wasm instance to verify fresh-store recovery.
The worker path queues 17 signals and a resize before startup, checks malformed
signal/resize/clock messages and overflow, proves rejected messages do not
advance the blocked guest, then delivers the fixture events. It records **57
control-error rejections** and completes all nine guest assertions.

`node --check tests/guest-session-terminal-browser.cjs` and
`node tests/guest-session-terminal-browser.cjs build/html-rt/bash-next.html`
pass. The page embeds the refreshed production worker and VFS package. The test
uses Node VM only; it does not launch Chromium. Removed an unused `treeVfs`
import. Native sanitizer and malformed control-fd framing checks in
`guest-session-terminal-check.py` remain complementary gates and were not rerun
in this slice.

## Legacy corpus runner CJS audit (Stage 6B.73)

`tests/c-engine-native-runtime.cjs` launched `build/cli-rt/waste-wast` by
default, although the current Makefile target `wast-native` produces
`build/cli-rt/waste-cli`. Its default and server-mode documentation now name
the maintained executable. With that correction, the native runner and
`tests/c-engine-browser-runtime.cjs`'s legacy `tests-worker.js` path agree on the
`engine-regressions` subset excluding only `continuation-waits.wast`: **11
ordered tests / 1,207 assertion results**, all PASS. The browser records have
complete setup and stream completion, and the native/browser summaries and
ordered identities/statuses/counts match.

The wait fixture exposes a real legacy-worker limitation: setup passes 3/3,
then the guest yields at `select`; `tests-worker.js` does not resume the host
wait, so the stream is incomplete with zero results. Stage 6B.68 separately
passes all 12 regressions, including this fixture, through the production suite
worker that schedules and resumes waits. Keep the limitation visible until the
legacy dashboard path is retired or upgraded. No Chromium was launched.

## Legacy dashboard SELECT continuation (Stage 6B.74)

`src/html-rt/src/tests-worker.js` now handles a yielded `SELECT` by waiting
10 ms and calling `waste_wast_resume`, allowing the kernel's finite timeout to
expire. It reports any other host wait kind as an explicit worker error; this
adapter has no source for interactive READ events. `node
tests/c-engine-browser-runtime.cjs build/html-rt/tests/payload.json
--group=engine-regressions --jobs=2` and the same group through
`tests/c-engine-native-runtime.cjs` both pass **12 tests / 1,230 results**.
Reports match on summaries, ordered identities/statuses/counts, and successful
setup/completion; every result passes. This closes the Stage 6B.73 continuation
fixture gap for the legacy test dashboard without launching Chromium.

The self-contained `test-next.html` dashboard package was rebuilt from the
updated worker source, current Wasm and generated payload; the focused run
passed against the worker extracted from that archive. `node --check` and
whitespace checks pass.

## Packaged Bash Readline/VFS completion (Stage 6B.75)

`tests/c-engine-bash-browser-runtime.cjs`'s `--readline-completion
--full-package` branch reads `vfs-manifest.json` and VFS files from the offline
HTML archive, then mounts that package for the production Bash worker. It
types `/bin/pw` one byte at a time, sends Tab, checks that `/bin/pwd` is
completed and submitted, observes `/root`, then requires explicit exit and
successful worker completion. The shared VFS announcement is also required.
These checks are accumulated boolean gates rather than calls to Node's
assertion API; the harness reports **7/7**. The focused run against
`build/html-rt/bash-next.html` passed, as did `node --check` and the installed
VFS audit. It uses the Node VM harness and does not launch Chromium. Exact
full-package directory listings are checked by the separate
`--coreutils-ls --full-package` branch.

## Full-package Bash directory listing (Stage 6B.76)

The `--coreutils-ls` branch in `tests/c-engine-bash-browser-runtime.cjs`
checks listings from the worker's mounted VFS: exact sorted names at `/` and
`/bin`, empty and hidden directories, `.` and `..`, long symlink metadata,
TTY column formatting versus redirected one-name-per-line output, multiple
directory operands, missing-path errno/status, and continued shell execution.
For `--full-package`, expected root and `/bin` names come from the embedded
VFS manifest. The staged variant uses its reduced installed-tree contract.
The old harness assumptions named only a subset of commands and included
echoed prompt text in listing comparisons; both contracts now pass after
extracting the expected number of listing lines after each marker. Full and
staged variants each report **7/7**. `node --check` and the VFS audit pass; no
Chromium was launched.

## Bash coreutils matrix staging parity (Stage 6B.77)

The 13-command `--coreutils-matrix` branch covers true/false status semantics,
pwd, echo, printf, basename, dirname, cat, wc, ls, date, wat, and wast. It
checks every command status, selected output (including file bytes/counts and
the UTC year), setup's mounted-path announcement, the after-command marker,
and clean worker exit. The embedded-package mode already had all command
files; the separately staged mode omitted the eleven coreutils binaries and
therefore observed status 126. The harness now explicitly stages those
snapshots, including date, alongside the generated WAT/WAST and text fixtures.
Both `--coreutils-matrix` and `--coreutils-matrix --full-package` pass **7/7**
against the archive-backed worker using Node VM, without Chromium.

## Bash pipeline and redirection parity (Stage 6B.78)

The `--pipeline-probe` path pipes `/bin/ls /bin` to `wc -l`, redirects the
sorted listing to a file, reads it back with `cat`, and requires a later shell
command and clean exit. It now verifies the pipe result equals the expected
`/bin` entry count (six in the staged tree; manifest-derived for the full
package) and that the redirected file exactly matches the sorted expected
names. This replaces weaker checks that accepted any digits after the pipe
marker and only required cat delimiters. Staged and
`--pipeline-probe --full-package` runs both pass **7/7** in the archive-backed
Node VM harness; no Chromium was launched.

## Bash here-document VFS coverage (Stage 6B.79)

The `--heredoc-builtin` branch checks a here-document value passed to Bash's
`read`, printed with `printf`, and followed by clean exit. It passes **7/7** in
both staged and embedded-package modes. The cat-backed `--heredoc` branch
writes a file, reads its content back, checks mode `0644` and rejects epoch
timestamps; `--heredoc-stdout` checks direct output and absence of a temp-file
error. Both pass **7/7** against the embedded full package. Staged stdout
parity was added in Stage 6B.81 and staged file-write/readback/mode parity in
Stage 6B.82. Verification uses Node VM, not Chromium.

## Repeated WAT/WAST command execution (Stage 6B.80)

`--wast-repeat-probe` runs the `wat` wrapper three times, then runs WAST via
the `wast` command, a direct executable file, and a shebang file. It places
status prints after each invocation, runs a recovery command, and exits. The
acceptance gate now requires all six expected zero statuses and verifies the
VFS path announcement contains both wrappers and all three fixtures. Staged
and `--full-package` runs both pass **7/7** in the packaged-page Node VM
harness; no Chromium was launched.

## Staged Bash stdout here-document (Stage 6B.81)

The staged `--heredoc-stdout` setup includes `/usr/bin/cat` and `/bin/cat`, and
requires both paths in the worker VFS announcement. Both stdout variants pass
**7/7**, checking that `Hello world!` reaches stdout and Bash exits without a
temporary-file error. Stage 6B.82 extends staged dependencies for the separate
file-writing case. No Chromium was launched.

## Staged Bash file here-document dependencies (Stage 6B.82)

The staged `--heredoc` path includes both `cat` and `ls`. `cat` writes
`hello.txt` from the here-document and reads it back; `ls -l` verifies mode
`0644`. The earlier stall occurred at that final mode check because the staged
fixture had no `ls`. The harness requires both commands' VFS paths. File,
stdout, and builtin `read` here-document variants pass **7/7** in both staged
and full-package modes.

## Language smoke assertions moved out of C

Assertion ordinals are one-based within `i32-smoke.wast`. The module body
matches `tests/c-engine-i32-smoke.wat`; the latter remains the build input for
C embedding/ownership probes. Each `run` case keeps its argument values,
expected result or trap, and order. `run_pair` becomes a two-result assertion;
`run_scalar_globals` becomes three assertions with the original scalar types.
Result type/count comparisons now belong to the ordinary WAST assertion runner.

| WAST ordinal | Previous C call | Expected result or trap |
| --- | --- | --- |
| 1 | `add(2147483647, 1)` | `-2147483648` |
| 2 | `rotl(1, 31)` | `-2147483648` |
| 3 | `locals(11, 22)` | `22` |
| 4 | `choose(7, 9)` | `7` |
| 5 | `call(20, 22)` | `42` |
| 6 | `early(7, 9)` | `7` |
| 7 | `ifelse(1, 9)` | `1` |
| 8 | `ifelse(0, 9)` | `9` |
| 9 | `branch(7, 9)` | `7` |
| 10 | `branch-if(7, 1)` | `7` |
| 11 | `branch-if(7, 0)` | `0` |
| 12 | `countdown(5, 0)` | `0` |
| 13 | `branch-table(33, 0)` | `33` |
| 14 | `branch-table(44, 9)` | `44` |
| 15 | `multi-call(44, 9)` | `35` |
| 16 | `multi-block(44, 9)` | `35` |
| 17 | `pair(11, 22)` | `(11, 22)` |
| 18 | `global-i64(0, 0)` | `i64 0x1122334455667788` |
| 19 | `global-f32(0, 0)` | `f32 -3.5` |
| 20 | `global-f64(0, 0)` | `f64 9.25` |
| 21 | `global-null(0, 0)` | `1` |
| 22 | `load-data(8, 0)` | `305419896` |
| 23 | `load8-s(12, 0)` | `-128` |
| 24 | `store-load(16, 1985229328)` | `1985229328` |
| 25 | `global(91, 0)` | `91` |
| 26 | `size(0, 0)` | `1` |
| 27 | `grow(1, 0)` | `1` |
| 28 | `size(0, 0)` | `2` |
| 29 | `grow(1, 0)` | `-1` |
| 30 | `load-data(131071, 0)` | `out of bounds memory access` |
| 31 | `div_s(-2147483648, -1)` | `integer overflow` |
| 32 | `div_u(1, 0)` | `integer divide by zero` |

## Import alias assertions

`extern-aliases.wast` complements `test_extern_aliases`. C export pointer
equality and manual `exec_imports` bindings remain under ASan/UBSan. WAST tests
observable sharing through normal registrations, including table mutation and
re-export chains that were previously absent from the smoke gate.

| WAST ordinal | Covered behavior | Previous coverage |
| --- | --- | --- |
| 1 | Consumer writes global 73 and reads it back | `run(consumer, "set-global", 73, 0, 73)` |
| 2 | Provider sees global 73 | `run(provider, "read-global", 0, 0, 73)` |
| 3 | Consumer stores/loads `0x12345678` at 24 | `run(consumer, "store", 24, 0x12345678, ...)` |
| 4 | Provider sees the stored bytes | `run(provider, "read-memory", 24, 0, ...)` |
| 5 | Consumer grows memory; old size is 1 | `run(consumer, "grow", 1, 0, 1)` |
| 6 | Provider memory size becomes 2 | Private C `memory->pages == 2` remains |
| 7 | Third module mutates re-exported global/memory | Added |
| 8 | Original provider sees global 99 | Added |
| 9 | Original provider sees memory value 123 | Added |
| 10 | Consumer calls provider's table function | Added |
| 11 | Re-export consumer calls the same function | Added |
| 12 | Consumer grows shared table; old size is 2 | Added |
| 13 | Provider sees table size 3 | Added |
| 14 | Consumer clears shared table slot | Added |
| 15 | Provider's indirect call traps after clearing | Added |
| 16 | Re-export consumer's indirect call also traps | Added |

## Independent instance assertions

`instance-isolation.wast` complements `test_decoded_module_instances`. Two
separately declared modules have identical bodies and independent mutable state.
This covers guest-visible isolation; it cannot establish the C decode-once,
instantiate-twice lifetime contract, so that C sequence remains intact.

| WAST ordinal | Covered behavior | Previous coverage |
| --- | --- | --- |
| 1 | First instance stores/loads `0x12345678` at 16 | First `store-load` call |
| 2 | Second instance still reads zero at 16 | Second `load-data` call |
| 3 | First instance changes global to 91 | First `global` call |
| 4 | Second instance global remains 5 | Second `read-global` call |
| 5 | First instance grows memory; old size is 1 | First `grow` call |
| 6 | First instance size becomes 2 | First `size` call |
| 7 | Second instance size remains 1 | Second `size` call |
| 8 | First instance retains initialized data at 8 | First data assertion |
| 9 | Second instance retains initialized data at 8 | Second data assertion |
| 10 | Second instance now stores 22 at 16 | Added reverse-writer case |
| 11 | First instance retains its earlier bytes | Added |
| 12 | Second instance changes global to 33 | Added reverse-writer case |
| 13 | First instance global remains 91 | Added |
| 14 | Second instance grows independently | Added |
| 15 | First instance size remains 2 | Added |
| 16 | Second instance size becomes 2 | Added |

## Retained C and host boundaries

The following functions remain in `c-engine-i32-smoke.c`. Their continued use
is deliberate; the WAST fixtures do not replace these failure modes.

| Retained gate | Boundary it verifies |
| --- | --- |
| `test_public_api` | Opaque module/instance API decode/create/find/invoke, output value/type/count and deletion |
| `test_imports` | Actual C `host_add` callback, manually supplied import binding, direct/re-export and wrapped invocation with typed host results |
| `test_extern_aliases` | Global/memory/table export pointer identity, manual import bindings, growth through the C API and consumer-before-provider teardown |
| `test_decoded_module_instances` | Source-buffer ownership, decode once/instantiate twice, decoded-module disposal while instances live, clone page sharing/copy-on-write, explicit page aliasing, clone use after source-instance deletion |
| `test_memory_access_contract` | Lazy zero pages, cross-page read/write/fill/overlapping copy, allocation observations, protection splits and atomic failures, unmapped fault category/address, remapping, virtual reservation/limits and linear-page promotion |

The nine C instance behavior checks and C alias calls stay next to their
ownership/API setup, so ASan/UBSan still observes those lifetimes. Their WAST
counterparts add normal guest-runner coverage rather than claiming pointer or
allocation equivalence. No private header becomes a guest SDK header.

`tests/browser-test-suite-runtime.cjs --installed-group=engine-regressions`
uses the production controller/worker to run installed inputs; its assertions
check host report/status/selection boundaries. Language results are produced by
WAST. The offline Chromium gate selects the same group from Diagnostics and
checks all seven identities and their 12/23/16/32/16/71/99 results. The existing full
corpus drivers retain their orchestration, compatibility modes and legacy
classifications until their own assertion-level audits are complete.

## Caller memory tranche (Stage 6B.23)

`caller-memory.wast` adds twelve guest counterparts using real
`env.pipe`, `env.write`, `env.read` and `env.close`. Each round trip transfers
four bytes from the executing module's memory into its own destination buffer.
A nested B→A→host call must read and update A's memory. A and a second identical
module retain independent bytes. A historical OCaml run accepted this
POSIX-based fixture; ongoing kernel parity is outside language-oracle scope.

All 32 successful-path C `CHECK`s remain in `c-engine-caller-instance.c`,
including the twelve value observations complemented by WAST. The real
`host_read_caller_mem` callback and manual C trampoline remain separate from the
production guest imports. Four bounded-read status checks replace unchecked
host `memcpy` observations. The callback uses `exec_memory_read`, and private
backing checks compare initialized sparse pages; the removed `memory->data`
field no longer exists. The following ledger covers every normal-path `CHECK`.
Failure-only file-read/decode checks remain driver prerequisite diagnostics.

| C function / CHECK ordinal | C assertion | WAST counterpart / disposition |
| --- | --- | --- |
| `test_direct_and_nested` 1 | Load A with C callback | Retain loader/binding status |
| 2–3 | Invoke A/read initial `0xefbeadde` | `caller-memory` 1; retain C callback path |
| 4–5 | A store/read `0x42424242` | WAST 2; retain C callback path |
| 6 | Load B with host callback and trampoline | Retain manual binding status |
| 7–8 | B reads its own `0xbebafeca` | WAST 3; retain C callback path |
| 9 | A/B memory objects differ | Retain pointer identity |
| 10 | A/B initialized backing pages differ | Retain sparse-page identity |
| 11–12 | B→A→host returns `0x99887766` | WAST 4; retain C trampoline path |
| 13–14 | Bounded reads of A/B succeed | Retain C memory API statuses |
| 15 | A memory[16] equals `0x99887766` | WAST 5 |
| 16 | B memory[16] remains zero | WAST 6 |
| `test_two_instances` 1–2 | Instantiate the same decoded module twice | Retain decode-once ownership/lifetime gate |
| 3–4 | Invoke both instances' initial readers | WAST 7–8; retain C callback path |
| 5–6 | Both initial values are `0xefbeadde` | WAST 7–8 |
| 7–8 | Store/read through both C callbacks | WAST 9–10; retain C callback path |
| 9–10 | Values are `0x11111111` / `0x22222222` | WAST 9–10 |
| 11 | Memory objects differ | Retain pointer identity |
| 12 | Initialized backing pages differ | Retain sparse-page identity |
| 13–14 | Bounded reads of both instances succeed | Retain C memory API statuses |
| 15–16 | Stored bytes remain independent | WAST 11–12 |

`make -C src/cli-rt caller-instance` runs the C gate, compares authored and
installed bytes, runs authored WAST through the CLI and runs the installed
fixture through the ASan/UBSan batch runner. No C assertion is removed here:
passing production imports cannot establish that a user-supplied C callback
received the correct engine pointer.

## Continuation tranche (Stage 6B.23)

`continuation-waits.wast` adds 23 guest assertions. A positive 30-ms select
replaces the synthetic `host.pause` in the guest counterpart. Global entry and
completion counters check that prefix/suffix side effects run once per call
across waits. Direct, indirect, nonlocal-jump and cross-module returns retain
42/43/7/45; repeat calls exercise reuse. A consumer with different memory and a
consumer without memory both reach the provider's timed import. An invalid
consumer timeout canary stays unchanged. Jump values also check zero→one and
negative-value propagation.

| WAST ordinal | Assertion | C correspondence / added boundary |
| --- | --- | --- |
| 1, 3 | Direct results 42, repeated | `run_replay_case("run", 42)` results |
| 2, 4 | Entry/completion counts 1/1, 2/2 | Added guest side-effect observations |
| 5, 7 | Indirect results 43, repeated | `run_replay_case("run_indirect", 43)` results |
| 6, 8 | Counts 3/3, 4/4 | Added |
| 9, 11 | Nonlocal-jump results 7, repeated | `run_replay_case("run_jump", 7)` results |
| 10, 12 | Counts 5/5, 6/6 | Added |
| 13, 15 | Cross-module results 45, repeated | `run_cross_instance_case` results |
| 14, 16 | Counts 7/7, 8/8 | Added |
| 17 | Memoryless consumer result 45 | Original C consumer has no memory; production-import counterpart |
| 18 | Counts 9/9 | Added |
| 19 | Consumer's invalid-timeout canary remains -1 | Added caller-memory boundary |
| 20–21 | Jump value 0 becomes 1; counts 10/10 | Added guest nonlocal-jump boundary |
| 22–23 | Jump value -9 stays -9; counts 11/11 | Added |

All 43 successful-path C `CHECK`s in `c-engine-continuation.c` remain. Repeating
a guest call does not replay an explicit snapshot. The C gate still captures,
resumes, restores and replays interpreter-owned frames, checks precise yield
status/reason and observes host callback counters. Its assertion ledger is:

| C function / CHECK ordinal | Assertion | Disposition |
| --- | --- | --- |
| `main` 1–2 | Read and load fixture with typed control imports | Retain setup/binding statuses |
| `run_replay_case` 1 | Allocate continuation | Retain allocation status |
| 2 | Initial `EXEC_YIELD` / `EXEC_YIELD_FORK` | Retain exact synthetic control boundary |
| 3 | Initial host callback count is 1 | Retain host counter |
| 4 | Capture continuation succeeds | Retain private snapshot API |
| 5 | Live resume succeeds with 42/43/7 | WAST counterparts above; retain snapshot-adjacent result |
| 6 | Live successful callback count is 1 | Retain host counter |
| 7 | Restore continuation succeeds | Retain private snapshot API |
| 8 | Replayed resume succeeds with 42/43/7 | Retain explicit replay; repeated WAST call is complementary |
| 9 | Successful callback count becomes 2 | Retain replay counter |
| `run_cross_instance_case` 1–5 | Allocate snapshots, read consumer, find provider export/type, load trampoline consumer | Retain private setup/binding statuses |
| 6 | Cross-module yield has expected status/reason | Retain exact synthetic control boundary |
| 7–8 | Capture provider and consumer snapshots | Retain private APIs |
| 9 | Live resume returns 45 | WAST 13/15/17 complement it |
| 10 | Live successful callback count is 1 | Retain host counter |
| 11–12 | Restore provider and consumer | Retain private APIs |
| 13 | Replayed resume returns 45 | Retain explicit cross-instance replay |
| 14 | Successful callback count becomes 2 | Retain replay counter |

The nine replay-case checks run for three exports: 2 main + 3×9 + 14 cross-module
checks = 43. `make -C src/cli-rt continuation` retains this sanitizer gate and
adds authored/installed WAST plus a sanitizer batch with byte-staleness checks.

The new fixture exposed the native batch adapter's lack of finite SELECT
resume handling. `native_wast.c` now attaches a native monotonic clock and
waits in at most 5-ms host sleep intervals until the copied kernel deadline.
It checks execution interruption between sleeps and reinvokes through the same
process driver while retaining the pending assertion's arguments/results.
The engine retains evaluator/process state; C returns ordinarily on yields.
The adapter services finite SELECT waits only. Terminal-input, indefinite and
other host-I/O waits still need the full session driver. Yielding module starts
retain their existing session-driver requirement.

The native host gate tests ordinary select/pselect completion, a 30-second
finite wait interrupted by a 200-ms host deadline, cancellation of runnable and
waiting guests, and continued execution/reporting of a following test. The full
native session driver also passes all 23 assertions and records actual SELECT
waits. Native OCaml cannot execute the timed fixture because `env.select` is
unavailable; that historical rejection is outside language-oracle scope,
not an accepted oracle pass or
an added expected failure in the installed C suites.

## Path VFS assertions moved out of C

Stage 6B.24 audits all 72 original CHECKs in `tests/posix-path-vfs.c` and
all 11 codec CHECKs in `tests/posix-path-abi.c`. Fifteen path assertions move
out of C into `tests/engine-regressions/path-vfs.wast`. The remaining 57 path
checks and all 11 codec checks retain direct API/sanitizer coverage. No CJS
harness is removed and no engine or runtime behavior is changed.

The fixture uses private `/tmp/path-vfs` files rather than C-seeded `/bin/tool`
and `/data` nodes. Corresponding `tool`, `readme`, `secret`, `new` and
`work/file` nodes preserve the operation, permission, content and traversal
expectations. Absolute/relative normalization uses equivalent paths beneath
that private root. `open` stores the returned descriptor and reports zero for
success, so assertions do not depend on descriptor allocation order. Expected
failures check both the return value and the guest `__errno_location` slot.
Each failing pathname call clears that slot first, so repeated ENOTDIR cases
cannot pass by observing a preceding error.
The versioned `waste_kernel.readlink_v1` returns a negative errno; its guest
wrapper performs the same return translation as guest libc's `env_result`.

Original C ordinals below count successful-path CHECKs in source order before
this tranche. WAST ordinals count `assert_return` forms, starting at one.

| Removed C ordinal | Operation and original expectation | WAST ordinals |
| --- | --- | --- |
| 47 | Stat through regular `tool/child`: ENOTDIR | 44–45 |
| 48 | Create/open through `tool/child`: ENOTDIR | 46–47 |
| 49 | Read-only open through `tool/child`: ENOTDIR | 48–49 |
| 50 | Mkdir through `tool/sub`: ENOTDIR | 50–51 |
| 51 | Stat after rejected creations: ENOTDIR | 52–53 |
| 52 | Unlink through `tool/child`: ENOTDIR | 54–55 |
| 53 | Rename from `tool/child`: ENOTDIR | 56–57 |
| 55 | Rename `readme` into `tool/child`: ENOTDIR | 58–59 |
| 56 | Readlink through `tool/child`: ENOTDIR | 60–61 |
| 62 | Resolve `././readme` | 64–65 |
| 63 | Resolve parent/dot components to `tool` | 66 |
| 64 | Clamp traversal above root and resolve `tool` | 67 |
| 65 | Resolve repeated separators to `tool` | 68 |
| 66 | Reject chdir to regular `readme`: ENOTDIR | 69–71; also verify cwd unchanged |
| 68 | Reject access mode 8: EINVAL | 23–24 |

| Retained C ordinals | Boundary retained and complementary guest coverage |
| --- | --- |
| 1–7 | Kernel allocation and node/data/symlink installation APIs; guest mkdir/open setup is complementary |
| 8–15 | Direct file read/write/close and size API checks; WAST 4–12 and 31–35 complement them |
| 16–18 | C-seeded symlink stat/follow/readlink; guest symlink creation has no supported provider |
| 19–21 | Direct i64 lseek and close status; WAST 10–12 complement them |
| 22–24 | Directory descriptor and five-entry enumeration over seeded symlink fixtures |
| 25–30 | Direct mkdir/rename/stat/unlink/rmdir statuses; WAST 36–43 complement them |
| 31–32 | Seeded metadata-only file has empty backing content and regular kind; WAST 25–27 observes public stat |
| 33–36 | Exact nanosecond mtime, missing path and invalid nsec; guest utimensat remains unsupported |
| 37 | Direct executable access; WAST 20 complements it |
| 38–43 | Owned executable snapshots, inode identity, permission/size/directory/missing errors and cleanup |
| 44–46 | Seeded read-only/zero-mode/missing access API; WAST 13–22 complements it |
| 54 | Mtime through regular-file prefix: ENOTDIR; no equivalent supported guest import |
| 57–59 | Open/read/close through a seeded symlink, including complete byte comparison |
| 60–61 | Direct cwd setter/getter API; WAST 62–63 complements it |
| 67, 72 | Empty and overlong explicit byte spans, distinct from guest NUL-terminated strings |
| 69–71 | Two separately allocated kernels and independent node installation |

All eleven `posix-path-abi.c` checks remain: metadata validation/encode/decode
and complete round trip (1–4), NULL output and invalid kind (5–6), public stat
encode/decode and complete round trip (7–9), NULL decode output (10), and exact
little-endian mode bytes (11). WAST 25–30 reads the public stat mode at offset
16 and i64 size at offset 40 after real imports. It complements these codecs;
it does not establish a C structure round trip or private metadata layout.

The 71 WAST assertions include the moved expectations and real-import setup,
file I/O, complete `hello` bytes, absolute/relative seek, permissions, stat
mode/size, mutation sequence and full cwd bytes. The new errno observations
also test the guest import adapters that direct kernel calls bypassed.
`make -C src/cli-rt posix-path-vfs` runs the 57 retained C checks, verifies the
installed fixture bytes, runs authored WAST in the CLI, and runs the installed
fixture with ASan/UBSan. The ordinary executor regression gate covers all nine
fixtures. The native OCaml oracle rejects this fixture at `env.chmod` import
resolution; that historical POSIX rejection is outside oracle scope,
not an oracle pass or a new
expected failure in either installed C runner.

## Kernel pipe and descriptor assertions

Stage 6B.25 inventories all 208 CHECK sites across the 23 original functions
in `tests/posix-kernel.c`. Loops execute some sites repeatedly: the original
native gate reports 348 checks. Ten guest-observable checks move out of C;
the retained 198 sites execute 338 checks. `test_dup2` is removed, while
`test_edge_cases` retains five checks for C NULL arguments and pipe allocation.
No other kernel function is removed.

`tests/engine-regressions/pipe-descriptors.wast` has 99 assertions. Real
`env.pipe/read/write/close/dup/dup2/fcntl/select` imports observe private pipes.
Allocated endpoints are selected through stored descriptors, not fixed fd
numbers. The explicit dup2 target remains fd 10, matching the C test. All
SELECT calls use a zero timeout: they expose readiness and output bits without
requiring a host input event or an indefinite wait. Raw HUP/ERR masks, kernel
wait state and reference ownership remain direct C observations.

| Removed C function and original ordinal | Original expectation | WAST ordinal |
| --- | --- | --- |
| `test_dup2` 1 | Dup2 to fd 10 returns 10 | 47 |
| `test_dup2` 2 | Target descriptor is open | 48; later actual data transfers at 57–59 |
| `test_dup2` 3 | Dup2 to the same fd returns that fd | 49; also 50/55 and flags unchanged at 56 |
| `test_dup2` 4 | Overwritten original reader is open as writer | 62–67; fd 10 retains the reader |
| `test_edge_cases` 1 | Read closed fd 0: EBADF | 84; all pipe endpoints have been closed |
| `test_edge_cases` 2 | Write closed fd 0: EBADF | 85 |
| `test_edge_cases` 5 | Read zero bytes from reader: 0 | 14; buffer unchanged at 16 |
| `test_edge_cases` 6 | Write zero bytes to writer: 0 | 15 |
| `test_edge_cases` 7 | Read from writer: EBADF | 18 |
| `test_edge_cases` 8 | Write to reader: EBADF | 19 |

Read/write imports currently return raw kernel errors; the fixture retains
that actual ABI (`-9`, `-11`, `-32`) rather than assuming POSIX libc return
translation. Close/dup translate errors to `-1` and set guest errno; their
wrappers clear the slot before each call so repeated errors cannot pass on
stale values. Fcntl retains its direct status contract. This tranche does not
claim to standardize all legacy import error conventions.

The two zero-count counterparts exposed an adapter bug: the import allocated
no host buffer, then passed NULL to a kernel API that requires a non-NULL
buffer even for count zero. The shared adapter now supplies a stack byte for
zero-count calls, preserving guest range validation and kernel descriptor
bounds without allocation. Only the separately owned heap buffer is freed;
no stack pointer is retained across a yield. Invalid fd -1 and fd 64 still
return EINVAL with zero counts (WAST 86–89). Guest buffer contents remain
unchanged (16). The kernel's existing zero-count ordering and C NULL-pointer
contract remain intact.

All original kernel functions are accounted for below. Ordinals are local to
each function; numbers count source CHECK sites, not loop iterations.

| Original function | Original sites | Retained coverage or migration |
| --- | --- | --- |
| `test_lifecycle` | 1–5 | Kernel allocation, default descriptor setup and teardown |
| `test_invalid_fd` | 1–6 | Direct readiness query bounds/closed/NULL-kernel errors |
| `test_file_read_at` | 1–13 | Direct offset I/O, OFD position, retained objects, clone sharing and truncation |
| `test_fork_path_publication` | 1–5 | Clone namespace publication and shared backing identity |
| `test_directory_dot_entries` | 1–10 | Direct enumeration, dot entries and size-limited name buffers |
| `test_creation_mask` | 1–5 | Creation masks, exact realtime mtimes and clone-private mask state |
| `test_terminal_readiness` | 1–17 | Host-enqueued terminal bytes, readiness flags, EOF and direct statuses |
| `test_terminal_modes` | 1–21 | Canonical editing, signal flags, minimum bytes, window-size state |
| `test_terminal_eof_and_output` | 1–10 | VEOF and direct ONLCR/raw output transformation |
| `test_terminal_vtime` | 1–10 | Fake clock, registered waits and exact terminal timeout boundaries |
| `test_pipe_readiness` | 1–14 | Endpoint bounds, raw IN/OUT/HUP/ERR flags, direct byte I/O; WAST 1–13 complements |
| `test_pipe_close_transitions` | 1–9 | Raw hangup/error bits and direct EOF/EPIPE; WAST 22–35 complements |
| `test_pipe_full` | 1–5 | Every positive partial fill, exact 4096-byte capacity and raw readiness; WAST 36–45 complements |
| `test_dup` | 1–9 | Terminal OFD sharing, host enqueue and direct error statuses; pipe duplication is complementary |
| `test_dup2` | 1–4 | Moved to WAST as mapped above; direct dup2 remains in `test_close_on_exec` |
| `test_pipe_dup_readiness` | 1–3 | Raw HUP after last writer reference; WAST 71–83 observes liveness/EOF |
| `test_close` | 1–7 | Terminal references and direct close/error statuses; WAST 90–99 complements errors |
| `test_isolation` | 1–6 | Two independently allocated kernels and private terminal/descriptor state |
| `test_edge_cases` | 1–11 | 1–2/5–8 moved; 3–4/9–11 retain NULL buffers, higher second pair and NULL pipe/enqueue args |
| `test_fd_exhaustion` | 1–4 | Complete descriptor capacity and lowest-slot reuse, including dup failures |
| `test_close_on_exec` | 1–9 | Actual close-on-exec and clone lifecycle; WAST 51–63/72–74 complements descriptor flags |
| `test_shared_memory_names` | 1–15 | Explicit namespace attachment, credential changes, clone/unlink backing ownership |
| `test_foreground_process_group_routing` | 1–10 | Host-enqueued terminal interrupts and foreground group routing |

The ordinary `posix-kernel` Make gate combines the retained C sanitizer probe,
authored/installed byte comparison, authored CLI execution and installed WAST
sanitizer execution. The native OCaml oracle rejects the fixture at its missing
`env.select` import. A zero-timeout SELECT still needs that provider; its
availability cannot be inferred from language-only oracle tests.

## Signal masks and blocked pending assertions

Stage 6B.26 accounts for all 47 original checks in `tests/posix-signal.c`.
Five guest-observable checks move to real imports in
`tests/engine-regressions/signal-masks.wast`; 42 private/API checks remain.
The five set/mask imports previously returned zero without doing work. The
shared adapter now implements bounded 128-bit sets and process masks, plus
`sigfillset` and blocked `sigpending` queries. These assertions would fail
against the old stubs. All polls use zero timeouts, avoiding external events.

| Removed `test_blocked_signal` ordinal | Original expectation | WAST ordinal |
| --- | --- | --- |
| 1 | Raising blocked SIGINT succeeds | 126 |
| 2 | Pselect with original blocked mask returns 0 | 127 |
| 3 | Blocked SIGINT remains pending | 128–129: real query and bit |
| 4 | Unblocking makes SELECT return EINTR | 130–131 |
| 5 | SELECT consumed the pending signal | 132–134: reblock, query and absent bit |

Setup 122–125 supplies the mask and real handler disposition. SELECT consumes
pending state at its existing boundary; it does not invoke the handler. WAST
82–99 separately invokes the actual handler through pselect and observes
signal number, call count, active self/action mask, restored original mask,
consumed pending state and no duplicate invocation. This complements the
retained five direct handler enter/leave checks, rather than removing their
private kernel contract.

| Original function | Checks retained | Boundary and complementary guest coverage |
| --- | --- | --- |
| `test_pending_before_call` | 4 | Direct raise/interruption/consumption/mask API; WAST 82–97 complements it |
| `test_signal_after_yield` | 7 | Active wait, host-raised wakeup, wait polling and teardown/restoration |
| `test_blocked_signal` | 0 of 5 | Mapped to real imports above |
| `test_simultaneous_and_restore` | 6 | Host input plus signal precedence and original mask; WAST 113–121 complements ready-mask restoration |
| `test_cancel_and_error_restore` | 4 | Private wait cancellation and error restoration |
| `test_signal_dispositions` | 16 | Direct disposition, pending/getter and handler slot state; WAST 77–81/100–112 complements it |
| `test_handler_mask_round_trip` | 5 | Explicit kernel handler entry/leave and self/action masks; WAST 89–95 observes a real guest callback |

The other assertions cover all four set words/boundary bits (1–30),
block/unblock/setmask/query and aliased input/output (31–53), and rejected
operations with fresh errno plus output/mask canaries (54–71). Ignoring a
queued signal and rejecting uncatchable dispositions are 100–112. Output
bounds are checked before writing, and mask updates commit after oldset writes.
SIGKILL/SIGSTOP bits are excluded from process-mask changes.

The existing handler ABI uses an engine function index (14 in this fixture),
not a demonstrated compiled C function pointer. Sigaction flags, general
on-unblock delivery, default-action signal queuing and SELECT handler dispatch
remain separate work. Existing sigaction/raise/pselect raw-negative errors
are preserved. No fixture-only implementation replaces kernel signal state.
The native OCaml oracle rejects the missing `env.sigfillset` provider.

`tests/guest-sdk-stat.c` remains an authored C client. Its new
`sdk_signal_check` compiles against the mounted-only sysroot, verifies public
16-byte sigset layout and cross-word bits, exercises real mask/pending imports
and checks an adjacent buffer canary. The browser VFS harness saves the compiled
probe as `build/engine/guest-sdk/abi.wast`; its four assertions also run with
`build/cli-rt/waste-cli --vfs-root src/vfs build/engine/guest-sdk/abi.wast`.
It deliberately does not test C handler pointers. The `posix-signal` Make gate
combines retained C sanitizers with authored/installed comparison and native
WAST execution, including the installed fixture under ASan/UBSan.

## Wait wakeups and exact deadlines

Stage 6B.27 inventories all 21 original checks in `tests/posix-wait.c`.
Five guest count/bit expectations move to `tests/guest-session-waits.wast`;
16 direct C checks remain. All four original functions remain. Its JSON
contract supplies delayed input, clock and signal events at real SELECT yields.
This is an authored **24-check session**, separate from the eight-fixture,
403-assertion installed batch group. A plain batch cannot supply its events.

| Removed original function/ordinal | Guest expectation | Session WAST ordinal |
| --- | --- | --- |
| `test_ready_before_wait` 1 | Ready writer returns 1 | 2; output bit/high word at 3–4 |
| `test_ready_after_yield` 6 | Resumed SELECT returns 1 | 5 |
| `test_ready_after_yield` 7 | Resumed set contains fd 0 | 6; actual input byte at 7 |
| `test_timeout` 3 | Expired SELECT returns 0 | 8 |
| `test_timeout` 5 | Timed-out output clears fd 0 | 9; final set word at 10 |

| Original function/ordinals retained | Boundary |
| --- | --- |
| `test_ready_before_wait` 2 | Ready call leaves no registered wait |
| `test_ready_after_yield` 1–5, 8–9 | Direct EAGAIN, registration, BLOCKED/READY polling, host enqueue result, teardown and unchanged generation |
| `test_timeout` 1–2, 4 | Direct EAGAIN, exact fake-clock TIMEOUT polling and cleared wait state |
| `test_cancel_and_repeat` 1–5 | Repeated EAGAIN preserves generation; explicit cancellation clears ownership and polling result |

The shared session begins at 4,294,967,290 ns (near the u32 boundary), remains
blocked after advancing 499,999 ns, expires at 500,000 ns, then starts a second
500,000-ns wait (11–12). This tests split-word encoding, microsecond conversion
and independent deadlines without relying on elapsed browser time. Native
records show six actual SELECT yields, two input bytes, one signal and three
clock events. Signal checks 13–19 observe a real post-yield pselect callback,
its self/action mask and restored original mask; they complement the retained
seven `test_signal_after_yield` C checks. Checks 20–23 observe a second input
wake and no duplicate handler call; the final invocation exits successfully.

The native clock event is WSC1 operation 4, low/high u32 absolute nanoseconds.
Only an existing nonzero frozen override can advance, with no backward/reset
update. The browser exposes the same operation at READ/SELECT safe points and
its worker validates all u32 fields before forwarding. Initial clock setters
stay distinct. Negative tests reject absent/zero overrides, backward/reset
updates, pre-wait/completed-store calls and malformed worker values. A frozen
guest still times out against the native host policy clock. Retain direct
cancellation/generation checks: guest-visible completion cannot prove them.

`posix-wait` runs retained C sanitizers and the native-only Python event driver;
it does not start Node or require HTML. The shared session checker exercises
native/browser exports and the packaged production worker with `--scenario
waits --page build/html-rt/bash.html`. The actual offline Chromium gate also
runs the same authored fixture/contract in an independent worker and records
its individual results. The OCaml oracle lacks `env.select` and cannot execute
this fixture; the missing provider is explicit rather than a new installed skip.

## SELECT argument and pipe polling assertions

Stage 6B.28 inventories all 86 original checks in 22 functions in
`tests/posix-select.c`. There are no CHECK loops in this gate. The new
`tests/engine-regressions/select-polling.wast` has 247 assertions through real
SELECT/pselect and pipe imports; 38 C checks move there and 48 remain in 15
functions. All successful polls are bounded by a zero timeout. The fixture
verifies the isolated noninteractive store's lowest-free pipe pair before
using fd 0/1; duplicate targets are explicitly allocated with `dup2`.

| Original function/ordinal | Original expectation | New WAST ordinal |
| --- | --- | --- |
| `test_einval` 3–4 | Negative/too-large nfds | 2–3 |
| `test_einval` 5–7 | Negative sec/usec or usec = 1,000,000 | 5/7/9 |
| `test_einval` 8–10 | Negative sec/nsec or nsec = 1,000,000,000 | 11/13/15 |
| `test_null_sets` 2/4 | All-null zero-timeout SELECT/pselect returns 0 | 17–18 |
| `test_empty_sets` 1 | Empty read set with nfds 3 returns 0 | 20 |
| `test_ebadf` 1–4 | Closed read/write/except descriptors and pselect | 23/27/31/35 |
| `test_ebadf_high_fd` 1–3 | fd 65 exceeds the 64-descriptor kernel table | 39/43/47 |
| `test_pipe` 1–3 | Initial writer-only readiness and both bits | 123–125 |
| `test_pipe` 4–5 | Data makes reader ready | 129–130 |
| `test_pipe` 6–7 | Last-writer close makes reader ready | 233–234; real EOF read at 235 |
| `test_output_sets` 1–5 | Count 2 and correct endpoint filtering in both sets | 139–143 |
| `test_dup_fd` 1–3 | Original/duplicate readers both ready, count 2 | 150–152 |
| `test_pipe_full` 1–3 | Full writer not ready; partial drain restores readiness | 158–159/164 |
| `test_pipe_except` 1–2 | Reader close makes writer exceptional | 242–243 |

The seven functions removed entirely are `test_ebadf`, `test_ebadf_high_fd`,
`test_pipe`, `test_pipe_except`, `test_pipe_full`, `test_output_sets` and
`test_dup_fd`. Every other original function is accounted for below. Ordinals
refer to each function's original CHECKs, before this migration.

| Retained original function/ordinals | Boundary and complementary guest coverage |
| --- | --- |
| `test_einval` 1–2 | NULL kernel with valid C set pointer; no guest equivalent |
| `test_ebadf_mixed` 1 | Live terminal fd plus closed fd; pipe input errors are complementary |
| `test_null_sets` 1/3 | Internal EAGAIN from indefinite SELECT/pselect; guest import yields |
| `test_terminal_write` 1–3 | Terminal write count/bits and shared terminal descriptors |
| `test_terminal_read` 1–7 | Host-enqueued input/drain/EOF and terminal count/output bits |
| `test_multiple_sets` 1–6 | Same terminal fd in read/write sets before/after host input |
| `test_dup_interest` 1–4 | Same terminal fd in all three interest sets |
| `test_zero_timeout` 1–4 | Terminal-specific SELECT/pselect no-input polling and clear bits |
| `test_would_block` 1–3 | Internal EAGAIN on indefinite/positive SELECT/pselect waits |
| `test_count` 1–6 | Terminal descriptor contributes separate read/write interest bits |
| `test_nfds_limit` 1–3 | Terminal nfds filtering; WAST word-boundary pipe cases complement it |
| `test_high_nfds` 1 | Terminal low bit with high nfds; WAST pselect low writer case complements it |
| `test_isolation` 1–2 | Two independently allocated kernels with different descriptor setup |
| `test_pselect` 1–3 | Terminal write count/bit and temporary-mask readiness |
| `test_empty_sets` 2 | Internal EAGAIN with empty sets and no timeout |

Extra WAST coverage rejects partial guest fd-set/timeval/timespec/sigset spans
before output mutation, checking three valid output words and three adjacent
canaries after each error. Closed-descriptor errors retain input bits. This
preserves the import's actual raw-negative error contract, including EINVAL
for rejected input spans. It does not standardize libc errno translation.
Real duplicate writers at 31/32/63 exercise signed word bits and cross-word
encoding, ignored high bits are cleared on success, and selected closed high
bits return EBADF. nfds 0 and high nfds exercise complete output replacement.
Terminal multi-interest counts cannot be inferred from pipe readiness; those
C checks stay intact. Codec ownership/NULL/layout tests remain independent.

`posix-select` now runs the retained sanitizer C gate, compares authored and
installed bytes, runs authored WAST in the CLI and installed WAST with
ASan/UBSan. The ordinary executor group also requires all nine snapshots to
match. Native/browser reports agree on 650 ordered regression assertions.
The native OCaml oracle lacks `env.select`; it cannot execute the new fixture.
No expected-failure or skip entry is added for this supported C fixture.

## Terminal SELECT and canonical/raw session assertions

Stage 6B.29 audits the 48 SELECT checks remaining after Stage 6B.28 and all
21 checks in `test_terminal_modes` in `tests/posix-kernel.c`. The authored
`tests/guest-session-terminal-readiness.wast` and JSON contract supply
76 checks through real guest imports and post-yield host input. These session
checks are separate from the nine-fixture/650-assertion installed batch group.
Earlier tables record their landing-stage C ordinals; the current retained
SELECT gate has 10 checks, and the kernel gate has 328.

| Removed SELECT function/original ordinal | Guest expectation | Session WAST ordinal |
| --- | --- | --- |
| `test_ebadf_mixed` 1 | Live terminal fd 0 plus closed fd 5 returns EBADF | 3; unchanged input word at 4 |
| `test_terminal_write` 1–3 | fd 1 write count/bit; all three terminal fds writable | 5–7; complete word at 8 |
| `test_terminal_read` 1–2 | No input gives count 0 and clears read bit | 9–10 |
| `test_terminal_read` 3–4 | Delayed input gives count 1 and sets read bit | 29–30 |
| `test_terminal_read` 5 | Drained terminal returns count 0 | 45; cleared word at 46 |
| `test_terminal_read` 6–7 | Terminal EOF gives count 1 and sets read bit | 71–72; real zero read at 73 |
| `test_multiple_sets` 1–6 | Same fd read/write count and bits before/after input | 13–15/31–33 |
| `test_dup_interest` 1–4 | Same fd in all three sets; only write interest ready | 16–19 |
| `test_zero_timeout` 1–4 | SELECT/pselect empty terminal polling and cleared bits | 9–12; first pair shares the identical `test_terminal_read` expectations |
| `test_count` 1–6 | fd 0 counts once per read/write interest, plus stdout/stderr | 34–39 |
| `test_nfds_limit` 1–3 | nfds 2 excludes fd 2 and clears its output bit | 20–22 |
| `test_high_nfds` 1 | nfds 100 with only low live fd gives no false EBADF | 23; high set word at 24 |
| `test_pselect` 1–3 | Basic count/bit and empty temporary-mask readiness | 25–27; complete word at 28 |

These ten complete guest test functions are removed from C, accounting for
38 checks. The remaining five functions retain all their original checks:
`test_einval` 1–2 (NULL kernel with valid set pointer), `test_null_sets` 1/3
(private indefinite EAGAIN), `test_would_block` 1–3 (terminal indefinite/positive
wait EAGAIN), `test_isolation` 1–2 (two independently allocated kernels) and
`test_empty_sets` original 2 (private empty-set EAGAIN). These cannot be replaced
by an ordinary WAST return or a single-kernel terminal session.

| Removed `test_terminal_modes` original ordinal | Guest expectation | Session WAST ordinal |
| --- | --- | --- |
| 1–2 | Get attributes and enable canonical mode | 1/47 |
| 7–8 | Canonical read returns four bytes, `abc\n` | 49–50 |
| 9–10 | Erase read returns three bytes, `xZ\n` | 53–54 |
| 13 | Enable noncanonical minimum-byte mode | 59 |
| 15 | Raw read releases three bytes at VMIN | 60; actual `xyz` at 61 |
| 16–17 | tcflow output-on succeeds; unknown action returns EINVAL | 62–63 |

The 11 retained original kernel ordinals are 3/5 (host enqueue returns),
4/6 (exact canonical readiness masks), 11–12 (private pending SIGINT and input
discard readiness), 14 (internal raw EAGAIN), 18–21 (host winsize acceptance,
unchanged no-signal and changed SIGWINCH). Setup and drain calls stay for those
checks; only their guest count/content/attribute assertions are removed.
Other terminal readiness, EOF/output and VTIME C functions are unchanged.

Extra session coverage proves all three standard descriptors share queued
input (40–41), actual raw `hello` bytes (42–44), canonical partial-line blocking
(48) and drain polling (51). Erase uses backspace; a later event also exercises
configured DEL, VKILL and ICRNL, returning `R\n` (55–58). VMIN input is split
into `xy` and `z` after two actual READ yields. Canonical VEOF input releases
`partial` without its control byte (64–70), then zero reads and pselect remain
read-ready (71–75). Final invocation 76 exits successfully. VEOF input establishes
the same guest EOF expectations as the original host EOF setup; the host API
and raw POLL_HUP are still tested directly in `test_terminal_readiness`.

The shared contract marks partial line and partial VMIN events `stillWaiting`.
Each adapter verifies no continuation/output progress before the completing
event. Native counts are six SELECT yields, two READ yields and 32 input bytes.
Two native negative controls send complete input at the partial boundary and
require the guard to reject the premature continuation. The native-only
`terminal-readiness` Make target is also a prerequisite of `posix-select` and
`posix-kernel`; no Node/browser package is needed for those gates. The standalone
Python checker defaults to full ASan/UBSan/LeakSanitizer. Shared exports/worker
and actual offline Chromium also pass 76/76, with exact transcript and ordered
browser assertion parity. The oracle lacks `env.select`; this fixture adds no
installed skip or XFAIL. No runtime or engine semantics changed.

## Terminal EOF/output and VTIME session assertions

Stage 6B.30 inventories all 10 original checks in `test_terminal_eof_and_output`
and all 10 in `test_terminal_vtime` in `tests/posix-kernel.c`. Fourteen checks
move to `tests/guest-session-terminal-timing.wast` and its JSON contract. Twelve
map to ordinary guest WAST assertions; two output-byte comparisons become
exact shared transcript expectations. The session has 46 checks, separate
from the installed batch. The Stage 6B.30 kernel gate had 314 runtime checks;
Stage 6B.31 reduces it to 300 with the descriptor mapping below.

| Removed original function/ordinal | Expectation | Shared session mapping |
| --- | --- | --- |
| `test_terminal_eof_and_output` 1–2 | Get attributes; enable canonical EOF/output mode | WAST 1–2 |
| `test_terminal_eof_and_output` 3–4 | VEOF releases seven bytes with contents `partial` | WAST 37–41: count, word and final three bytes |
| `test_terminal_eof_and_output` 5 | Next read returns zero | WAST 42; unchanged buffer at 43 |
| `test_terminal_eof_and_output` 7 | OPOST+ONLCR emits `a\r\nb` | JSON `outputSegments` 1 and exact `output`; WAST 3 verifies write consumed 3 |
| `test_terminal_eof_and_output` 8 | Disable output processing | WAST 4 |
| `test_terminal_eof_and_output` 10 | Raw output emits `a\nb` | JSON `outputSegments` 2 and exact `output`; WAST 5 verifies write consumed 3 |
| `test_terminal_vtime` 1–2 | Get attributes; set VMIN=0/VTIME=2 | WAST 1/12; attribute query shares the identical EOF setup check |
| `test_terminal_vtime` 6 | Empty read returns 0 at the deadline | WAST 14; unchanged sentinel/canaries at 15–18 |
| `test_terminal_vtime` 7 | Set VMIN=3/VTIME=2 | WAST 19 |
| `test_terminal_vtime` 9–10 | Timeout returns one partial byte, `x` | WAST 20–21; untouched remainder/canaries at 22–25 |

Original EOF/output ordinals 6 and 9 remain in renamed
`test_terminal_output_lengths`: the private output helper returns four
transformed bytes for three input bytes with ONLCR and three in raw mode.
Guest write reports source bytes consumed, so its result has a different
contract. Original VTIME ordinals 3–5 and 8 retain direct EAGAIN, active wait
registration and exact fake-clock TIMEOUT polling. Their setup/drain calls stay;
all other C functions and private ownership/cancellation checks are unchanged.

The JSON transcript observes actual terminal output rather than manufacturing
an output buffer inside WAST. Extra ONLCR-only and OPOST-only cases preserve
LF bytes (6–9). A 257-byte write emits 259 transformed bytes across the guest
adapter's 256-byte chunk boundary (10–11); its two LF expansions and final `Z`
are recorded in `outputSegments` 5. Every adapter compares the complete
unmodified transcript, including stdout/stderr terminal aliases.

The frozen monotonic clock starts at 4,294,967,290 ns. Three READ deadlines
remain pending one nanosecond before expiry and complete exactly at expiry.
VMIN=3/VTIME=2 also stays pending after real input `x`; the original deadline
survives that resume. A fresh VMIN=0 read returns `Q` before its deadline
(26–29), then another timed read returns zero at an independent deadline
(30–34). Empty reads preserve the sentinel, partial reads preserve all other
bytes, and canaries bound the guest buffer. Canonical VEOF returns `partial`
once, then zero, with preserved buffer/canaries (35–45); invocation 46 exits.

Native boundary counts are eight READ yields, one SELECT yield, six clock
events and ten input bytes. Three negative controls replace subdeadline clock
events with the exact deadline and require the `stillWaiting` guard to reject
progress. The native-only `terminal-timing` Make target, also required by
`posix-kernel`, uses ASan/UBSan without Node/HTML; the standalone checker defaults
to full LeakSanitizer. Native/exports/packaged worker/actual offline Chromium
pass 46/46 with exact transcript; all browser paths retain ordered names and
outcomes. The prior 24/76-check sessions still pass.

This audit preserves the implementation's first-read-wait deadline. Explicit
clock events wake READ; autonomous READ timer wakeups and broader VMIN/VTIME
timing semantics remain acceptance work. No engine/runtime semantics changed.
The oracle rejects `env.select`, without a new installed skip or XFAIL. The
manifest/inventory bytes and full 293-test/63,704-assertion native/browser
results remain unchanged.

## Descriptor flags, allocation and exhaustion assertions

Stage 6B.31 audits all 30 original checks in five kernel C functions. Fourteen
move to `tests/engine-regressions/descriptor-flags.wast`; sixteen stay in four
C functions and the exhaustion function is removed. The native kernel gate
passed 300 runtime checks at Stage 6B.31; Stage 6B.32 reduces it to 294.
The fixture adds 93 installed assertions.

| Original function | Original ordinals moved | WAST ordinals | Original ordinals retained in C |
| --- | --- | --- | --- |
| `test_fd_exhaustion` | 1; 2; 3; 4 | 65–66; 57; 69–70; 77 | None |
| `test_close_on_exec` | 1; 2; 3; 4; 5; 6 | 1; 8; 9; 10–11; 12; 14–15 | 7–9: direct close-on-exec and clone |
| `test_dup` | 8; 9 | 41–42; 43–44 | 1–7: terminal aliasing/readiness/drain/close |
| `test_edge_cases` | 3 | 4–7 | 1–2/4–5: C NULL buffers/arguments |
| `test_pipe_dup_readiness` | 1 | 10 | 2–3: raw HUP before/after last writer close |

The real setup calls for retained private checks remain. Compound duplicate
checks map to both the returned descriptor and the subsequent flag query.
Guest pipe checks do not substitute for terminal readiness or private ownership.

The noninteractive batch kernel starts with all 64 descriptors closed. The
fixture verifies first/second pipe pairs, independent FD_CLOEXEC flags, dup and
dup2 replacement clearing flags, self-dup2 preserving them, and minimum/hole
allocation through F_DUPFD and F_DUPFD_CLOEXEC. Three writer aliases transfer
real `test` bytes to shared reader aliases (27–35). Invalid descriptors, flag
bits and commands preserve the current adapter's conventions (36–47):
pipe/dup/dup2/close/fcntl return -1 plus guest errno. F_DUPFD minimum 64
currently returns EMFILE. These are audited implementation
expectations, not complete POSIX conformance claims.

A bounded loop makes exactly 32 real pipe calls (57); all 64 returned
numbers must match the ordered allocation vector (58). Recycled reader/writer
slots have clear flags (59–60/84). Full-table pipe/dup/fcntl operations fail,
self-dup2 succeeds without allocation, and a pipe with only one free slot fails
without consuming it (65–76). Dup then reuses that lowest slot (77–78). Two
free slots admit a pipe with working read/write (79–87); closing all 64 and
creating a fresh pipe verifies cleanup (88–93).

This fixture exposed stale FD_CLOEXEC after a closed slot was reused by pipe.
The shared kernel now clears both endpoint flags when publishing a pipe.
A historical focused 12-assertion differential probe independent of initial
descriptor numbers passed in C and the native OCaml interpreter, whose pipe implementation
already creates both endpoints with clear flags. The complete installed
fixture stops in the oracle at its first allocation (3 versus C batch 0);
the oracle's 1,024-slot table also differs from C's 64 slots. That runtime-profile
difference is outside language-oracle scope and adds no installed skip or
expected failure. No OCaml descriptor/profile implementation work is required.

The native-only `posix-kernel` target now requires matching authored/installed
pipe and descriptor snapshots, executes both authored fixtures, and checks both
mounted fixtures with ASan/UBSan. Full standalone LeakSanitizer passes. All ten
regression fixtures/743 ordered assertions agree across native, sanitizer,
production worker and actual offline Chromium. Full native/browser results
agree on 294 identities/63,797 assertions, preserving all prior 293 records and
63,704 assertions. The 24/76/46-check shared sessions remain unchanged.
Evidence and the complete 14-mapping ledger are under
`build/engine/refactor-stage6b-descriptor-audit/`.

## Directory iteration and file creation-mask assertions

Stage 6B.32 audits all 15 checks in two kernel helpers. Six complete checks
move to `tests/engine-regressions/directory-umask.wast`. Six compound checks
split: guest-visible names or mode move, while exact private inode/timestamp
expectations stay. Three private checks are untouched. Nine C checks remain,
and the complete kernel gate now passes 294 runtime checks. The fixture adds
219 installed assertions; a partial migration is not a removed C check.

| Original function/ordinal | WAST mapping | Retained C boundary |
| --- | --- | --- |
| `test_directory_dot_entries` 1 | Guest mkdir/open setup at 1–4 complements it | Private path installation with seeded metadata |
| `test_directory_dot_entries` 2 | 5: open directory succeeds | Setup call only |
| `test_directory_dot_entries` 3 | 10–23: short dot buffer fails, next dot remains available | Removed |
| `test_directory_dot_entries` 4 | 15–23: dot name, kind, stat identity/mode and bounds | Seeded child inode 41 |
| `test_directory_dot_entries` 5 | 29–37: parent dot name, kind, stat identity/mode and bounds | Seeded parent inode 40 |
| `test_directory_dot_entries` 6 | 43–51: file name, kind, stat identity/mode and bounds | Seeded file inode 42 |
| `test_directory_dot_entries` 7 | 52–59: repeated EOF with unchanged outputs | Removed |
| `test_directory_dot_entries` 8 | 60: close succeeds | Setup call only |
| `test_directory_dot_entries` 9–10 | 63–80: root dot and parent dot names/identities | Both fixed root inode values 1 |
| `test_creation_mask` 1 | 168–172: new file's default permissions are 0644 | Exact injected creation timestamp |
| `test_creation_mask` 2 | No replacement claimed | Exact injected write timestamp |
| `test_creation_mask` 3 | 173: umask returns prior 0022 | Setup call only |
| `test_creation_mask` 4 | 174–177: new file under mask 0077 has mode 0600 | Removed |
| `test_creation_mask` 5 | No replacement claimed | Clone inherits an independent creation mask |

The directory C helper becomes `test_directory_metadata`; all five literal
inode expectations and real readdir calls remain. The mask helper retains
creation time 1700000000/123456789 and write time 1700000001/987654321,
plus clone ownership. WAST compares the actual guest stat and versioned
readdir records, which complements rather than replaces host-seeded metadata
and C ABI codec/layout checks. All other kernel C functions are unchanged.

Guest traversal rejects name capacities 1/2/4 before consuming dot/parent/file;
dot/parent fit exactly in 2/3 bytes. A metadata record crossing the guest memory
boundary is rejected before consuming dot (6–9). Failure and EOF preserve all
32 name bytes and 48 metadata bytes, with surrounding canaries. Bytes beyond
short requested capacities remain unchanged. Names are compared through their
NUL terminators. Kind, inode and mode come from real imports, not a fabricated
metadata record. A dup shares cursor progress while a separate open starts at
dot; wrong-kind, closed and negative descriptors preserve outputs and report
current raw readdir errno. Spare bytes inside a successful name buffer are not
claimed to have a defined value.

File masks test default 0022, private 0077, group 0002, all-masked 0777 and zero.
Only the low nine mask bits count; reopening a file with a new mode leaves its
old permissions intact. This audits file creation, not mkdir's mask semantics.

The fixture exposed ordinary open accepting an existing O_CREAT|O_EXCL path.
The shared kernel now rejects it before following links or truncating bytes.
Assertions 194–210 cover first creation, file/directory collisions and real
`keep` bytes surviving an exclusive O_TRUNC attempt. Shared-memory exclusivity
already belongs to its namespace check; delegation to the materialized backing
path now clears O_EXCL to avoid rejecting first creation. Assertions 212–219
check exclusive shm create, collision/errno, close/unlink and recreation.
Existing shared-memory C checks remain for the next audit.

The complete fixture stops at the native oracle's missing `env.readdir_v1`.
A separate 25-check mask/exclusive comparison passes using explicit stat-mode
offsets (C 16, OCaml 8); it excludes guest errno queries because the oracle
stores its error internally. Installed C assertions still verify EEXIST errno.
That comparison is not full fixture parity and adds no installed skip/XFAIL.

Native, full sanitizer, production worker and actual offline Chromium agree on
all eleven fixtures/962 ordered assertions. Full native/browser results agree
on 295 identities/64,016 assertions, preserving all previous 294 records and
63,797 assertions. The 24/76/46-check shared sessions remain unchanged.
The full/partial mapping and parity evidence are under
`build/engine/refactor-stage6b-directory-audit/`.

## Named shared-memory assertions

Stage 6B.33 audits all 15 checks in `test_shared_memory_names`. Four complete
checks move to `tests/engine-regressions/shared-memory.wast`; one compound
check splits its guest unlink/name-lookup expectations from retained clone
allocation. Ten private/API checks remain unchanged. Eleven C checks remain,
and the complete kernel gate passes 290 runtime checks. The installed fixture
adds 231 assertions through real guest imports.

| Original ordinal | WAST mapping | Retained C boundary |
| --- | --- | --- |
| 1 | No replacement claimed | Both private kernels allocate |
| 2 | No replacement claimed | Independent kernel attaches to the same namespace |
| 3 | No replacement claimed | Non-root credentials reject creation atomically |
| 4 | No replacement claimed | Root restoration verifies failed creation published no name |
| 5 | 23–33: open, truncate to four bytes, stat size/mode | Removed; real create/size setup remains |
| 6 | 127–128: existing exclusive creation returns EEXIST/errno | Removed |
| 7 | 129–133: first exclusive creation succeeds with empty size | Removed |
| 8 | 134–135: second exclusive creation returns EEXIST/errno | Removed |
| 9 | Guest writes complement it | Direct offset-independent object write API |
| 10 | No replacement claimed | Credential access denial for an unrelated user |
| 11 | No replacement claimed | Independent kernel restores root credentials |
| 12 | Guest separately opened descriptor reads complement it | Independent kernel uses direct offset-independent object read API |
| 13 | 138–140: unlink succeeds, name lookup returns ENOENT/errno | Clone allocation; real unlink setup remains |
| 14 | 141–157: live guest descriptors read after unlink | Original kernel's direct offset-independent object read |
| 15 | Guest dup lifetime complements it | Cloned kernel's retained object read after unlink |

The ledger distinguishes four removed checks from one partial migration.
Guest lseek/read and dup cannot establish independent-kernel namespace
attachment, credentials, clone ownership or direct file_read_at/file_write_at
semantics. All other kernel C helpers remain unchanged. Retained C calls still
create/size the object and unlink it before the original/clone lifetime checks.

Missing names report ENOENT; slash-only, relative and nested names report
EINVAL. Empty guest strings and an out-of-memory name pointer report EFAULT
at the adapter. Assertions deliberately preserve the current mixed ABI:
kernel shm_open/ftruncate failures return raw negative errno, while malformed
adapter input and legacy unlink return -1 plus guest errno. These are current
regressions, not complete POSIX conformance. The fixture checks initial mode
0600, but does not replace credential checks or audit every flag/mask profile.

New and extended storage reads as zero. Shrinking retains the prefix; growing
again zeros the discarded region. Read-only, invalid descriptor and negative
length truncation failures preserve size. Real read/write operations verify
actual bytes. Separate opens share content with independent positions; dup
shares position. Read outputs check every returned byte, the complete remaining
32-byte tail and adjacent canaries. Public 128-byte stat outputs retain
adjacent canaries; no fabricated stat record is supplied by the client.

Unlink removes the name while existing descriptors still access their object.
Recreating a name while those handles are live yields a fresh empty object.
Old/new writes remain independent; both generations remain usable after a
second unlink and closing other handles. Final recreation begins empty and
zero-filled. All created names/handles are released. This does not claim
mmap coverage or inode uniqueness across simultaneously live generations.

The recorded OCaml attempt stops at missing `env.shm_open`. This fixture is
outside the language oracle scope; no provider development or kernel parity
is required, and no new installed skip/XFAIL or differential pass is claimed.
Engine, adapter, runtime, guest headers and
frontend behavior are unchanged in this slice.

Native-only `posix-kernel` now checks authored/installed bytes and executes the
new fixture directly and under ASan/UBSan alongside path, pipe and directory/
mask fixtures. The full native regression group also passes standalone
LeakSanitizer. Native, sanitizer, production worker and actual offline Chromium
agree on twelve fixtures/1,193 ordered assertions. Full native/browser records
agree on 296 identities/64,247 assertions: 286 PASS, 2 XFAIL, 8 SKIP. All previous
295 records/64,016 assertions and manifest test/file records remain unchanged.
The 24/76/46-check shared sessions remain unchanged. The five-mapping ledger,
private-check preservation and ordered parity evidence are under
`build/engine/refactor-stage6b-shared-memory-audit/`.

## Process-group and foreground-terminal assertions

Stage 6B.34 audits all ten checks in `test_foreground_process_group_routing`.
Six complete checks move to `tests/guest-session-process-groups.wast`, driven
by its shared JSON event contract. Four private/API checks stay unchanged;
real setup calls stay in C. The complete kernel gate now passes 284 runtime
checks. The session has 70 assertions plus successful exit, totaling 71 checks.

| Original ordinal | WAST/session mapping | Retained C boundary |
| --- | --- | --- |
| 1 | 1: initial getpgrp is 1 | Removed |
| 2 | 2–3: setpgid returns 2 and getpgrp becomes 2 | Removed; real setup remains |
| 3 | 4: terminal foreground starts at 1 | Removed |
| 4 | 7: tcgetattr succeeds | Removed; real setup remains |
| 5 | 8: raw ISIG/VINTR attributes install | Removed; real setup remains |
| 6 | First post-yield VINTR event complements it | Direct host terminal enqueue result |
| 7 | 21–23: background wait remains pending, no handler call | Private kernel pending bit is absent |
| 8 | 27: tcsetpgrp succeeds | Removed; real foreground change remains |
| 9 | 31–33: queued interrupt returns EINTR and invokes SIGINT handler | Raw kernel pselect EINTR result |
| 10 | 34–36: guest pending query, empty poll and no handler replay | Private kernel pending bit consumed |

The public sigpending query exposes blocked signals, not the entire private
kernel/terminal queue. Handler counts and completed guest imports cannot prove
host enqueue or raw kernel API behavior. All other kernel helpers remain
unchanged; the four retained CHECK expressions remain identical.

Initial process group 1 changes to 2 while terminal foreground stays 1. Invalid
PID/group/descriptor changes fail with the current raw errors and preserve
both identities (10–20). All three real standard terminal descriptors observe
the same foreground state. An explicit current-PID change to group 42 again
leaves terminal foreground unchanged; a change through descriptor 2 is visible
through descriptor 0. env.setpgid currently returns the positive group, not
POSIX's zero. The fixture preserves that ABI and does not claim complete
POSIX job control, forked membership, setsid or background-I/O enforcement.

Background VINTR arrives at an actual SELECT yield. The contract requires the
wait to remain pending with no output progress, then supplies real x/Y bytes.
Those bytes read back exactly, with all fifteen tail bytes and both canaries
unchanged. Changing foreground routes the queued terminal interrupt once;
repeated zero-time pselect polls return zero without repeating the handler.
Foreground VINTR immediately interrupts a real wait. With ISIG disabled, ETX
reads as ordinary data and handler count stays unchanged. Six input bytes
produce six SELECT yields and no host signal events. Two native negative
controls add a data byte after background VINTR; the queued signal is preserved
but the required lack-of-progress guard rejects the prematurely satisfied wait.

`make -C src/cli-rt process-groups` is native-only and required by `posix-kernel`.
It uses ASan/UBSan, records the native report/transcript and needs no Node or
HTML. The standalone checker passes LeakSanitizer outside the ptrace
exception. `guest-session-check.py --scenario process-groups --page ...`
compares native, browser exports and packaged worker; the actual offline-page
gate runs the identical contract. All pass 71/71 with the exact transcript;
browser exports/worker/Chromium preserve ordered assertion names/outcomes.
The previous 24/76/46-check sessions remain unchanged; four sessions total 217.

The installed batch remains twelve fixtures/1,193 assertions in 296 identities.
All prior 296 native/actual-browser records and 64,247 assertions agree:
286 PASS, 2 XFAIL, 8 SKIP. A reviewed source-provenance refresh accounts for the
recent DIY/libc README scope edits without changing test/file records or
fixture bytes. Kernel/runtime/frontend semantics are unchanged. These POSIX
contracts do not require an OCaml comparison or additional kernel providers.
The six-mapping ledger and parity proof are under
`build/engine/refactor-stage6b-process-group-audit/`.

## Terminal descriptor duplication and close assertions

Stage 6B.35 audits the fourteen remaining checks in `test_dup` and `test_close`.
Six complete checks move to `tests/guest-session-terminal-descriptors.wast`;
eight raw readiness/lifetime checks remain identical, with real setup calls.
The full kernel gate passes 278 runtime checks. The shared session has 119
assertions plus successful exit, totaling 120 checks.

Ordinals below refer to the pre-6B.35 helpers, each with seven CHECK sites.
The historical `test_dup` ordinals 8–9 already moved in Stage 6B.31 and are
excluded from this slice's migration count.

| Original check | WAST/session mapping | Retained C boundary |
| --- | --- | --- |
| `test_dup` 1 | 2: dup returns descriptor 3, satisfying the original >=3 bound | Removed; real dup setup remains |
| `test_dup` 2 | 58–59/63–64: SELECT observes all three ready aliases | Exact equality of raw readiness masks |
| `test_dup` 3 | 8–11: duplicate reads real post-yield hi input | Raw POLL_IN after direct host enqueue |
| `test_dup` 4 | 8: duplicate read returns 2 | Removed; real read setup remains |
| `test_dup` 5 | 12–13: both aliases cease to be readable after drain | Raw POLL_IN absent on original |
| `test_dup` 6 | 14–18: closed duplicate is not a terminal; read returns EBADF without writing memory | Direct readiness query returns EBADF |
| `test_dup` 7 | 21–28: original retains termios and reads later x input | Direct readiness query still succeeds |
| `test_close` 1 | 72: close stdin succeeds | Removed; real close setup remains |
| `test_close` 2 | 73/76–78: stdin is closed; failed read leaves buffer intact | Direct readiness query returns EBADF |
| `test_close` 3 | 79/103/105–109: stdout remains a terminal and reads final R after stdin/duplicates close | Direct readiness query still succeeds |
| `test_close` 4 | 80/104/113–114: stderr retains terminal state until its close | Direct readiness query still succeeds |
| `test_close` 5 | 74–75: double close returns -1/errno EBADF | Removed |
| `test_close` 6 | 54–55: close(-1) returns -1/errno EINVAL | Removed |
| `test_close` 7 | 56–57: close(64) returns -1/errno EINVAL | Removed |

Real dup2 also verifies same-descriptor success, target aliasing, and failed
source/target changes that preserve the live target. Terminal settings changed
through fd 3/5 are visible through fd 0/3. A VMIN=2 read through the duplicate
remains pending after h; i satisfies it. Later SELECT exposes all ready aliases,
and partial reads through fd 3/5 drain one shared queue. Closing fd 0 preserves
queued Z; restoring it via real dup2 permits later Q input. Final R arrives
before closing fd 0/3; fd 1 drains it and fd 2 retains termios until its close.
Every read checks both canaries and the entire untouched tail. Failed reads
check the complete sixteen-byte buffer.

The fixture records the current mixed import ABI: read returns raw negative
EBADF without setting errno, while dup/dup2/close return -1 and set errno.
No engine error convention or zero-count read behavior changes. Host input
targets fd 0, so events precede its close and it is restored before later input;
the fixture does not add arbitrary-descriptor host injection.

Seven input events deliver eight bytes with four READ/three SELECT yields.
One native negative control delivers hi at the first boundary: its value
checks remain valid, but the driver's required lack-of-progress guard rejects
the prematurely satisfied read. `make -C src/cli-rt terminal-descriptors` is
native-only, required by `posix-kernel`, and saves ASan/UBSan results. The
standalone checker passes LeakSanitizer outside sandbox ptrace. Native,
sanitizer, browser exports, packaged worker and offline Chromium pass 120/120
with identical transcripts; browser paths preserve all ordered assertion names
and outcomes. Previous 24/76/46/71-check sessions are unchanged; five total 337.

The installed manifest/inventory and twelve batch fixtures remain unchanged.
Full native/browser parity preserves all 296 previous records and 64,247
assertions: 286 PASS, 2 XFAIL, 8 SKIP. All eight retained expressions and other
kernel helpers are unchanged. No CJS harness is retired. OCaml kernel work
and comparisons are outside this C POSIX contract's scope. The six full
mappings and parity evidence are under
`build/engine/refactor-stage6b-terminal-descriptor-audit/`.

## Pipe creation, byte I/O and close/EOF assertions

Stage 6B.36 audits all 23 checks in `test_pipe_readiness` and
`test_pipe_close_transitions`. Twelve complete checks already have equivalent
guest assertions in `pipe-descriptors.wast` and are now removed from C. Eleven
raw readiness/direct-kernel checks retain identical expressions and real setup
calls. Every other kernel helper is unchanged; the full gate passes 266
runtime checks. Ordinals below refer to the pre-6B.36 helpers.

| Original check | WAST mapping | Retained C boundary |
| --- | --- | --- |
| `test_pipe_readiness` 1 | 1: pipe creation succeeds | Removed; real creation remains |
| `test_pipe_readiness` 2 | 2: unsigned reader fd <64 excludes negative descriptors | Removed |
| `test_pipe_readiness` 3 | 2: unsigned writer fd <64 excludes negative descriptors | Removed |
| `test_pipe_readiness` 4 | 2: reader and writer differ | Removed |
| `test_pipe_readiness` 5 | 3–4: empty reader is not SELECT-ready | Raw POLL_IN absent |
| `test_pipe_readiness` 6 | 3–4/13: guest absence of data and EAGAIN complement | Raw POLL_HUP absent |
| `test_pipe_readiness` 7 | 5–6: writer is SELECT-ready | Raw POLL_OUT present |
| `test_pipe_readiness` 8 | 7: initial write succeeds | Raw POLL_ERR absent |
| `test_pipe_readiness` 9 | 7: write returns 4 | Removed; real write remains |
| `test_pipe_readiness` 10 | 8–9: reader is SELECT-ready after write | Raw POLL_IN present |
| `test_pipe_readiness` 11 | 10: read returns 4 | Removed; real drain remains |
| `test_pipe_readiness` 12 | 11: exact test bytes match | Removed |
| `test_pipe_readiness` 13 | 12/17: reader not ready after drain | Raw POLL_IN absent |
| `test_pipe_readiness` 14 | 13: batch read returns raw -11 | Direct kernel EAGAIN result |
| `test_pipe_close_transitions` 1 | 22: pipe creation succeeds | Removed; real creation remains |
| `test_pipe_close_transitions` 2 | 25/28: buffered data and EOF are SELECT-ready | Raw POLL_IN present |
| `test_pipe_close_transitions` 3 | 24/26–29: writer close, buffered drain and EOF | Raw POLL_HUP present |
| `test_pipe_close_transitions` 4 | 26: buffered read returns 3 | Removed; real drain remains |
| `test_pipe_close_transitions` 5 | 29: next read returns EOF | Removed; real EOF read remains |
| `test_pipe_close_transitions` 6 | 31: broken-pipe test creation succeeds | Removed; real creation remains |
| `test_pipe_close_transitions` 7 | 34: write returns raw EPIPE | Raw POLL_ERR present |
| `test_pipe_close_transitions` 8 | 33: writer is not SELECT-ready | Raw POLL_OUT absent |
| `test_pipe_close_transitions` 9 | 34: write returns -32 | Removed; real write remains |

The three endpoint checks map to one compound `valid_pair` assertion. Its
unsigned bounds prove both original nonnegative/range checks, and it also
checks distinctness. Guest SELECT, EOF and EPIPE observations complement raw
POLL flags; they do not expose those flags or prove private ownership. Empty
batch reads expose raw EAGAIN, while interactive imports may yield instead.
The direct kernel EAGAIN check therefore remains. Capacity, partial-fill,
duplicate-reference and independent-kernel checks are unchanged.

All original 99 assertion commands and ordered outcomes are preserved.
Assertions 100–136 add separate guarded-read checks through real imports.
They verify exact test/abc payloads, both buffer canaries and every untouched
byte of a 64-byte buffer. EAGAIN, wrong-end/closed EBADF and repeated EOF leave
the complete buffer untouched; a failed EPIPE write preserves its three source
bytes. Buffered data survives writer close before repeated EOF. The original
read/zero-count wrappers remain unchanged. The fixture now has 136 assertions;
the twelve-file regression group totals 1,230.

Explicit installation updates only the pipe test/file record and snapshot,
plus the inventory manifest hash. All 295 other test records, 317 other file
records, source-input provenance and mounted modes/mtimes/inodes are unchanged;
the distribution still has 296 identities and 504 nodes. Both offline pages
were rebuilt. Native, sanitizer, LeakSanitizer, production worker and actual
Chromium preserve 1,230 ordered regression outcomes. The full corpus agrees on
64,284 assertions: 286 PASS, 2 XFAIL, 8 SKIP. Other 295 ordered records and the
original pipe prefix remain unchanged. Five shared sessions still total 337
checks. No CJS harness is removed, and these C POSIX contracts require no
OCaml kernel work or comparison. Twelve full mappings and parity evidence are
under `build/engine/refactor-stage6b-pipe-io-audit/`.

## Remaining terminal byte I/O and EOF assertions

Stage 6B.37 audits all seventeen checks in `test_terminal_readiness`. Four
guest-visible count/content checks move to the shared terminal-readiness
contract. Thirteen host/raw readiness expressions stay identical, with real
read/EOF/write setup; all other C helpers remain unchanged. The full kernel
gate passes 262 runtime checks. Ordinals below refer to the pre-6B.37 helper;
WAST ordinals refer to the extended 90-check session.

| Original ordinal | WAST/session mapping | Retained C boundary |
| --- | --- | --- |
| 1 | 5–8: standard descriptors writable initially | Raw POLL_OUT present |
| 2 | 9–12: empty terminal not read-ready | Raw POLL_IN absent |
| 3 | 9–12: absence of initial readable data complements | Raw POLL_HUP absent |
| 4–5 | 7–8/40–41: all standard descriptors share guest readiness | Exact equality of raw masks for fd 1/2 |
| 6 | 29: shared host event delivers delayed hello | Direct host enqueue result |
| 7 | 30–32/40–41: input makes aliases read-ready | Raw POLL_IN present |
| 8 | 31–39: terminal remains writable with input queued | Raw POLL_OUT present |
| 9 | 42: read returns 5; bounds at 43 | Removed; real drain remains |
| 10 | 44–45: exact hello word and final byte | Removed |
| 11 | 46–47: no readability after drain | Raw POLL_IN absent |
| 12 | 70: canonical VEOF event sets guest EOF | Direct host EOF signaling result |
| 13 | 77–78/81–82: EOF remains read-ready | Raw POLL_IN present |
| 14 | 79/83: repeated zero reads complement EOF state | Raw POLL_HUP present |
| 15 | 79: read at EOF returns zero; bounds at 80 | Removed; real EOF read remains |
| 16 | No guest equivalent for invoking the host enqueue API on a closed fd | Direct host enqueue rejection |
| 17 | 88–89: write consumes five unchanged source bytes; exact hello transcript suffix | Removed; real write remains |

All 76 original WAST commands remain unchanged in relative order, including
successful exit; fourteen assertions are interleaved/appended. Read wrappers
seed a 64-byte sentinel buffer and two canaries before real imports. Eight
explicit bounds assertions cover raw, canonical, edited, minimum-byte and EOF
reads, checking both canaries and every untouched tail byte. EOF reads check
the entire buffer. Repeated EOF and pselect expose shared readiness on all
three terminal descriptors; read and write interests count separately. The
five-byte write appends exactly hello after the original marker transcript,
and its source bytes remain unchanged. The session now has 89 assertions plus
successful exit, totaling 90 checks.

The eight input events and six SELECT/two READ yields remain unchanged, with
32 bytes and two premature-input controls. EOF is supplied through canonical
VEOF input. Direct host EOF/enqueue results and raw readiness are retained C
checks; this session does not claim to expose their private API contracts.

`terminal-readiness` is native-only and required by `posix-kernel` and
`posix-select`. It now saves ASan/UBSan results/transcript; the standalone
checker accepts `--results PATH` and passes LeakSanitizer outside sandbox
ptrace. Native, sanitizer, browser exports, packaged worker and actual offline
Chromium pass 90 checks with identical transcripts. Browser paths preserve all
ordered names/outcomes and the original 76-result subsequence. The other
24/46/71/120-check sessions remain unchanged; five sessions total 351.

Manifest, inventory and installed fixture bytes are unchanged; no reinstall or
page rebuild is needed. Full native/browser parity preserves all 296 records
and 64,284 assertions: 286 PASS, 2 XFAIL, 8 SKIP. The twelve batch fixtures
still have 1,230 assertions. Engine/runtime/frontend semantics are unchanged;
no CJS harness is removed, and no OCaml kernel work or comparison is required.
The four full mappings, original-command indices and parity evidence are under
`build/engine/refactor-stage6b-terminal-io-audit/`.

## Complete current retained-kernel inventory

Stage 6B.38 reviews all 122 remaining source CHECK sites in 21 helpers.
`test_pipe_full` ordinal 1, creation success, moves to the existing
`pipe-descriptors.wast` assertion 36. Its real setup call remains. Original
ordinals 2–5 become current 1–4 with identical expressions: every positive
partial fill, exact capacity and raw writability before/after drain. All other
helper bodies remain unchanged. This is one full migration, with no partial
compound check counted as removed and no new guest assertions.

[The retained-kernel review](posix-kernel-retained-coverage.md) classifies every
current ordinal exactly once: 121 sites, 261 successful runtime checks. The
five lifecycle sites execute 130 times, with descriptor loops of 64/3/61 and
two allocation guards. Four pipe-capacity sites execute 19 times, including
sixteen positive partial writes. All other 112 sites execute once each.

Guest-facing operations remain as supporting guards where the complete C
probe establishes host source installation, explicit cloning/publication,
mapping cursor independence or retained-object ownership. A successful guest
open/read/write does not exercise those private setup APIs. Direct mapping
reads reject ranges past EOF, whereas POSIX reads can return partial bytes;
private transformed-output lengths also differ from guest source-byte counts.
The review records these distinctions along with exact host-seeded metadata,
injected clock/credentials, NULL pointers, raw readiness, independent kernels,
private signal/wait state and direct exec teardown. Guest complements do not
replace these contracts or justify partial-expression removal.

The native warnings-as-errors ASan/UBSan gate and standalone LeakSanitizer pass
261 checks. Existing authored/installed byte checks, WAST execution and native
shared-session guards remain required; no Node or HTML is needed by
`posix-kernel`. No harness is removed. All 296 native/browser corpus records
and 64,284 assertions remain unchanged: 286 PASS, 2 XFAIL, 8 SKIP. Twelve
regression fixtures still total 1,230 assertions, and five unchanged shared
sessions total 351 checks. Manifest/inventory and installed bytes are unchanged;
no reinstall or page rebuild is needed.

Classification is complete for the current helpers in this C file. Other C
and libc/CJS audits, supported language-oracle comparison and consolidation
remain open. These C kernel contracts require no OCaml kernel development or
comparison. The one-mapping ledger, complete per-site expressions/hashes,
execution multiplicities and parity proof are under
`build/engine/refactor-stage6b-retained-kernel-audit/`.

## Libc selection and completion audit

Stage 6B.39 audits the legacy `tests/libc-test/libc-runtime.cjs` driver and
all fourteen installed libc clients: **56 ordered assertions**, **51 passing
and five existing mismatches**, yielding **12 PASS / 2 XFAIL**. Guest value
expectations already reside in WAST; no additional C/Node assertion removal
is counted. [The complete harness inventory](libc-harness-coverage.md) maps
selection, export filtering, scheduling, sixteen injected stub functions,
completion and reporting to C coverage or explicit legacy dispositions.

`make -C src/cli-rt libc-native` and `libc-sanitize` now require the nonempty
authored/installed selection, matching client bytes, parser-derived names,
ordered results/counts and the exact five existing failed expectations.
They reject further failures inside either XFAIL fixture and native setup
diagnostics. Sixteen report rejection controls and the existing native/browser
host-boundary gates cover incomplete selection, wrong outcomes and completion.
Use `python3 tests/libc-corpus-check.py --report PATH` to verify an existing
native/browser libc group report. Single-file execution uses the ordinary
installed batch identity; no fragile assertion-deletion filter is introduced.

Retain the actual-Wasm 5,000-allocation stress gate, compiled guest C
header/layout clients, SDK/provider audits and private codec/ownership probes.
The allocator Node driver has no OCaml dependency. Its stale import-free
instantiation is fixed with throwing guards for the merged artifact's 24
function imports; the entire stress workload must make zero kernel calls,
and a real wrapper call verifies guard rejection. The libc README and allocator
harness provenance are explicitly refreshed and both offline pages rebuilt; test bytes, selection,
guest semantics and file-level expected failures remain unchanged. Full
native/browser ordered corpus parity remains **296 identities / 64,284
assertions**, with **286 PASS / 2 XFAIL / 8 SKIP**. Ordinary setup-module
acceptance remains a separate general-runner audit, and no OCaml kernel work
or driver retirement is included. Evidence is under
`build/engine/refactor-stage6b-libc-harness-audit/`.

## Legacy DIY POSIX kernel and control audit

Stage 6B.40 accounts for all seven authored kernel expectations and the one
embedded external-control expectation in the legacy DIY CJS drivers. The
[complete DIY harness inventory](diy-posix-harness-coverage.md) maps each guest
expectation and loader/control boundary. Original kernel assertions 1–5 pass
through native sessions and browser C exports, preserving identity, foreground
group, VFS/pipe byte transfer and parent/fork/wait memory isolation. Original
stop/continue and concurrent sleeping-job scenarios remain explicit profile
gaps; the compatibility-mode installed entry remains a batch SKIP. No scheduler
feature, OCaml provider or kernel implementation is added, and no driver is
retired or removed C assertion counted.

The new shared `guest-session-diy-control.wast`/JSON contract checks handler
installation during module start, no early delivery, SIGINT after a confirmed
pselect yield, return value 2, one handler call and one installation after
resume, then successful exit: **six assertions plus exit / seven checks**.
It adapts the C function-index ABI and uses the supported pselect boundary;
it does not establish host pause/resume of a runnable busy loop. Deadline and
cancellation are separate abort controls, not resumable pauses.

`make -C src/cli-rt diy-posix-control` runs native ASan/UBSan without Node/HTML,
plus a missing-event deadline and deliberate expected-return mismatch. The
same supported scenario runs through browser exports, the packaged production
worker and real offline Chromium; standalone LeakSanitizer verifies positive
and negative teardown. Existing five session results/transcripts remain
unchanged, bringing the shared total to **six sessions / 358 checks**.
The installed corpus remains **296 identities / 64,284 assertions**, with
**286 PASS / 2 XFAIL / 8 SKIP**. Only DIY README source provenance changes;
both pages are rebuilt explicitly. Evidence and ordered parity are under
`build/engine/refactor-stage6b-diy-posix-audit/`.

## DIY language and imported-memory cutover

Stage 6B.41 migrates `bulk-operations.wast` and
`spectest-isolation-{a,b}.wast` from assembled compatibility modules to shared
native/browser command streams, preserving authored bytes and all **20/4/1**
ordered checks. The [DIY ledger](diy-posix-harness-coverage.md) accounts for
every original command, including six ordinary bulk-operation invokes.
The same memory pair runs in both manifest orders with one and three jobs
through native sanitizer and browser host-boundary gates, preserving aliases
within a script and fresh host stores between scripts. All three scripts pass
the OCaml language oracle individually; no kernel/provider development is added.

Explicitly reviewed installation removes four assembled support modules and
three directories, leaving **497 VFS nodes / 314 manifest files**. Source
identities, bytes and policies remain unchanged; kernel compatibility remains
SKIP. The DIY group has **4 PASS / 1 SKIP / 48 checks**, with native
ASan/UBSan/LeakSanitizer and production-worker parity. Full native/actual offline
Chromium records agree on **289 PASS / 2 XFAIL / 5 SKIP / 64,309 assertions**.
All 293 other ordered records, twelve regression fixtures / 1,230 assertions
and six shared sessions / 358 checks remain unchanged. No C assertions are
removed; cumulative moved C coverage remains **188**, with the retained kernel
inventory still **121 sites / 261 runtime checks**. Evidence is under
`build/engine/refactor-stage6b-diy-language-cutover/`.

## Ordinary setup commands and completion

Stage 6B.42 adds independent setup counts, ordered line/status/phase diagnostics
and EOF completion to both native and browser reports. Invalid, unlinkable,
trapping or unencodable ordinary modules fail even with zero assertions; later
passing results do not repair acceptance. The [setup ledger](wast-setup-coverage.md)
maps all eight focused fixtures and ten exposed official setup errors. C and
OCaml agree on the focused acceptance/rejection cases, while OCaml accepts all
seven affected official files. Those language gaps are now explicit XFAILs.

Full native/actual offline Chromium parity retains **296 identities / 64,309
assertion/action results**, with **282 PASS / 9 XFAIL / 5 SKIP** and a separate
**2,313 setup attempts / 2,303 successes**. The browser defers failed-command
parse disposal until after dispatch finishes. Native ASan/UBSan/LeakSanitizer,
worker, zero-assertion XFAIL/XPASS and truncated-tail gates preserve failure
diagnostics and completion. The twelve regression fixtures / 1,230 results and
six shared sessions / 358 checks remain unchanged. No C assertions are removed;
the cumulative 188 and retained 121-site / 261-check kernel inventory stay intact.
Manifest/inventory and fixture bytes are unchanged; both offline pages rebuild
explicitly. Evidence: `build/engine/refactor-stage6b-setup-audit/`.

Stage 6B.43 repairs the six high segment-index setup errors in five official
files: the parser had silently retained only 32 segments despite correctly
encoding unsigned indices. Data/element capacity is now 128; every append form
rejects overflow and releases unretained payloads. Four remaining setup errors
are the three active-element cases and the standard `spectest.table64` import.
Both current baselines now retain four entries including the two libc XFAILs;
full corpus parity is **287 PASS / 4 XFAIL / 5 SKIP**, with **2,313 setup attempts /
2,309 successes** and the same **64,309** ordered assertion/action results.

`tests/test-suite-segment-indices.wast` adds **64** portable host-staged checks
outside installed selection: indices 31/32/63/64/127, numeric/deferred names,
32/64-bit memory/table initialization, distinct values, post-drop traps,
zero-length operations and unknown index rejection. Two 129-segment capacity
fixtures fail ordinary and `assert_invalid` host acceptance explicitly.
Native ASan/UBSan/LeakSanitizer, production worker and language-only OCaml checks
pass. No C assertion or retained session coverage is removed. Separate flat
bulk-instruction syntax gaps remain documented in the [setup ledger](wast-setup-coverage.md).
Evidence: `build/engine/refactor-stage6b-segment-indices/`.

Stage 6B.44 resolves the three active-element setup errors in `core/elem.wast`
by preserving the declared non-null `(ref func)` type of bare/`func` index
vectors in WAT and binary modes 0–3. Mode 4 and explicit nullable types remain
nullable; synthesized table-shorthand segments use the table's declared type.
The loader applies the same declared type to active initialization and passive/
declarative `table.init` validation. The official file leaves both XFAIL lists.
Stage 6B.45 then supplies standard `spectest.table64`: a store-owned, nullable
funcref table with 64-bit indices and limits 10–20, distinct from `spectest.table`.
Current parity is **289 PASS / 2 XFAIL / 5 SKIP**, **2,313 setup attempts / 2,313
successes / zero failures**, preserving every **64,309** ordered result. All seven
official setup XFAILs are resolved; the two libc XFAILs are retained.

`tests/test-suite-spectest-table64{,-isolation}.wast` adds **39 portable assertions /
four setups** outside installed selection. Native/browser host gates check both
orders at jobs 1/3; OCaml checks the authored language fixtures and nine C-encoded
modules in source order. Width/type/limit rejection, null entries, high indices,
aliases, cross-module calls, bounded growth and separation from table32 all pass.
Store teardown frees the host table; checkpoint capture includes it before any
imports. Two private checkpoint checks retain nested function ownership/growth
and restore original null slots and bounds. ASan/UBSan and LeakSanitizer pass.
Actual offline DOM verification stages an authored setup-recovery row, retaining
visible setup errors despite passing assertions while the official row passes.
Evidence: `build/engine/refactor-stage6b-spectest-table64/`.

`tests/test-suite-element-types.wast` adds **82 portable assertions / eleven
setups** outside installed selection. It covers bare/explicit/empty active/passive/declarative vectors,
nullable shorthand and null slots, table64, all eight binary element modes,
passive initialization, active/declarative drop state, repeated drops and
nullable-to-non-null rejection despite non-null contents. Native C, browser and
OCaml language comparisons, ASan/UBSan and LeakSanitizer pass. The twelve
installed executor fixtures / 1,230 checks, six sessions / 358 checks, cumulative
188 moved C assertions and 121-site / 261-check retained kernel inventory remain
unchanged. Evidence: `build/engine/refactor-stage6b-active-elements/`.

Stage 6B.46 adds `tests/test-suite-flat-bulk.wast`: **190 portable assertions /
three setups** outside installed selection. It implements plain `table.init`,
`data.drop` and `elem.drop`, repairs omitted folded explicit-index cases, and
retains operand validation for named folded abbreviations. Distinct table values
7/11/29 check destinations and segments in both index spellings and address
widths; real drops, repeat/zero/trap behavior and index 127 preserve runtime
semantics. Fourteen invalid and six quoted malformed assertions protect missing
operands/immediates, unknown indices/names and address types. Native/browser C,
OCaml authored/C-encoded language comparisons and ASan/UBSan/LeakSanitizer pass.
All installed corpus/setup/session records and moved/retained C counts remain
unchanged. Evidence: `build/engine/refactor-stage6b-flat-bulk/`.

Stage 6B.47 adds `tests/test-suite-table-copy-fill.wast`: **611 portable
assertions / seven setups** outside installed selection. Plain copy/fill and
folded numeric/mixed index forms preserve destination/source order and deferred
forward table names. Distinct entries check overlap in both directions, bounds
failure without partial writes, zero-length/null operations, externref identity,
non-null tables and cross-module function ownership. Table32/table64 and both
mixed-width copy directions retain count/address typing. Twenty-six invalid
modules and three quoted malformed assertions protect operand/reference/address
mismatches and missing/unknown indices. Native/browser C, authored/33 C-encoded
module OCaml language comparisons, seven ordinary acceptance/rejection probes
and ASan/UBSan/LeakSanitizer pass. Corpus/setup/session records and moved/retained
C counts remain unchanged. Evidence: `build/engine/refactor-stage6b-table-copy-fill/`.

Stage 6B.48 completes [the finite official comparison inventory](wasm-language-coverage.md):
265 identities, 261 compared/accepted, four legacy exclusions and 31 repository
identities outside official scope. Ordered check/setup comparison agrees for
259 inputs; long-name truncation and inline-module instantiation policy remain
explicit gaps. All 62,975 official checks are accounted for by C metadata,
actual native/browser results and independent OCaml traces; ordinary setup
counts are 2,261 C / 2,260 OCaml. Source-hash/issue-kind-pinned gap policy rejects
new/changed gaps and repaired baselines; strict mode rejects known gaps too.
Negative controls protect assertion omission/order/kinds, escaped and distinct
Unicode names, incomplete setup/EOF and stale/duplicate report identities.
Full corpus records, installed/runtime bytes and moved/retained C counts remain
unchanged. Evidence: `build/engine/refactor-stage6b-language-ledger/`.

## Run and refresh

```sh
make -C src/cli-rt i32-smoke
make -C src/cli-rt libc-native libc-sanitize
make -C src/cli-rt diy-posix-control
make -C src/cli-rt posix-path-abi posix-path-vfs posix-kernel posix-signal posix-wait posix-select
build/cli-rt/waste-test --vfs-root=src/vfs --group=engine-regressions --json
build/cli-rt/waste-wast-ocaml tests/engine-regressions/i32-smoke.wast \
  tests/engine-regressions/extern-aliases.wast \
  tests/engine-regressions/instance-isolation.wast
python3 tests/guest-session-process-group-check.py
python3 tests/guest-session-check.py --scenario process-groups --page build/html-rt/bash.html
python3 tests/guest-session-terminal-descriptor-check.py
python3 tests/guest-session-check.py --scenario terminal-descriptors --page build/html-rt/bash.html
python3 tests/guest-session-timing-check.py
python3 tests/guest-session-check.py --scenario terminal-timing --page build/html-rt/bash.html
python3 tests/guest-session-readiness-check.py
python3 tests/guest-session-check.py --scenario terminal-readiness --page build/html-rt/bash.html
node tests/browser-test-suite-runtime.cjs --installed-group=engine-regressions
node tests/c-engine-offline-browser.cjs
```

The OCaml command compares the three language-only fixtures. The POSIX-based
caller, continuation, path, signal and kernel fixtures run through C gates;
they do not require OCaml providers or differential kernel acceptance.

The first command runs the retained warnings-as-errors ASan/UBSan C gate,
the authored WAST files, and the installed group with ASan/UBSan, without Node.
It requires all twelve installed snapshots to match the authored files, so an
edited source cannot silently keep sanitizer coverage on older bytes. The
batch command uses distribution snapshots. Refresh those snapshots explicitly
with `vfs-tests-install`, then run `vfs-tests-check` and rebuild the browser pages.
The initial three-file, subsequent two-file, path, pipe, signal, SELECT,
descriptor, directory/mask and shared-memory selection additions were
explicitly installed with `--review-selection`;
normal refreshes retain the selection guard.

## Next audit tranches

Stages 6B.49–6B.52 close full export/action-name handling, bare-inline-module
profile parity and exported table32/table64 element-list shorthand. The strict
official language ledger now agrees for all 261 supported inputs; POSIX/kernel
behavior remains outside OCaml oracle scope. Stage 6B.53 audits all 117 runtime
checks in `tests/c-engine-process-lifecycle.c`, preserving capsule/transition/
mapping ownership invariants and mapping their guest-visible complements. Stage
6B.54 audits all 58 runtime checks in `tests/c-engine-exec-matrix.c`; the
installed guest-session matrix covers command outcomes while the C gate retains
private executable, startup, handler-ownership and process-engine invariants.
Stage 6B.55 audits all 22 checks in `tests/c-engine-exec-lifecycle.c` and fixes
a stale direct-memory access in its host callback. Stage 6B.56 audits the 12
process-contract checks and replaces its flat-memory assumptions with the
sparse-memory API. Stage 6B.57 retains and accounts for all 11 repeated-wait
continuation checks. Stage 6B.58 accounts for the 32 private store-checkpoint
and clone-binding checks. Stage 6B.59 closes the installed inventory gate at 27
assertion sites / 4,380 current executions, with mounted-path guest and directory
checks recorded as complements. Stage 6B.60 maps 6,294 browser VFS staging CJS
assertions at 20 assertion sites and verifies all 296 packaged corpus paths.
Stage 6B.61 accounts for the 10 tarballjs loader assertions. Stage 6B.62 maps
the VFS packaging gate at 19 sites / 488 executions for this package snapshot.
Stage 6B.63 retains the browser-worker binary transfer, cancellation and prompt
recovery contract alongside its guest-session outcomes.
Stages 6B.70–6B.82 audit execution-control, packaged guest-session worker,
terminal-control adapter, and legacy corpus-runner boundaries, including
session-kernel cleanup lifetime, malformed/overflowing worker controls, the
current native executable, finite SELECT resumption in the old dashboard
worker, archive-backed Bash Readline completion and directory listings,
coreutils-matrix parity, exact pipeline/redirection contents, staged/full
here-document parity, and repeated WAT/WAST command status and fixture-path
checks.

### Bash executable probe (Stage 6B.83)

`env.fcntl` now prefers the engine-owned kernel adapter over a linked provider
module, preserving per-process descriptor flag updates. The probe verifies the
sequence `F_GETFD(0)`, `F_GETFD(1)`, `F_GETFD(2)`, `F_SETFD(2, FD_CLOEXEC)`,
`F_GETFD(2)`, clear, and final get as `0,0,0,0,1,0,0`. The Bash controller also
records immediate and second-launch statuses so future failures terminate with
transition evidence instead of waiting indefinitely.

Staged and refreshed embedded-full-package executable probes pass **7/7**,
including assertions for initial argc 3 and repeated argc 2, argv[0], nonempty
environment, positive PID, and inherited `/root` cwd, plus missing-command status 127
and a successful second launch. Explicit exit-status-7 probes pass **7/7** in
both modes. The rebuilt probe is installed in the VFS and the inventory audit
passes for all 507 nodes. Node VM harnesses were used; no Chromium was launched.
The observed cwd `/bin/waste-probe` was the startup pointer accidentally
aliasing `argv[0]`. Stage 6B.97 fixes the pointer and tightens the regression to
require inherited `/root` in the Bash shell and a valid cwd target in the native
startup-block test. It also tests the worker host's missing-handler and
throwing-handler error forwarding paths.

The first native attempt used a stale `waste-session` binary. After rebuilding
it against the current engine, the native probe reports the same descriptor
sequence and passes the startup, missing-command/status-127, second-launch, and
exit checks (**7/7**). Native and both browser VFS paths now agree on this
external executable contract; the earlier all-zero result was not reproducible
with the rebuilt runner.

Continue assertion-level coverage accounting across the remaining native C
probes and CJS host/browser harnesses. Preserve callback identity, replay
counters, page ownership and teardown gates even where WAST/session fixtures
cover observable behavior. Trace guest C header/layout tests to compiled Wasm
exports rather than translating away their ABI. Browser DOM, terminal,
packaging, transfer and cancellation checks remain host checks; share guest
expectations through the portable session contract where possible. Do not
extend the OCaml kernel. Stage 6C compatibility-mode consolidation and
dashboard retirement remain separate acceptance work.

Stage 6C.2 keeps the source inventory's 56-file citation audit distinct from
assertion disposition: no harness has been removed by the command split. The
current ownership map is: native installed-corpus semantics and JSON results
(`make -C src/cli-rt corpus-native`); focused Bash worker/process/readline/
package scenarios (`tests/c-engine-bash-browser-runtime.cjs` plus the
terminal-model fixture); compiled-engine mounted-byte and inventory checks
(`tests/c-engine-vfs-browser.cjs`); and actual file-page/browser acceptance
(`tests/c-engine-offline-browser.cjs --suite-full`). Retain those separate
boundaries. Stage 6C.3 closed the shell-redirection failure: Bash's variadic
`fcntl` call passed a guest stack-area pointer where the fixed-width adapter
expected the integer `F_DUPFD` minimum. The new
`waste_kernel.fcntl_varargs_v1` adapter reads that integer from guest memory;
the fixed-width `env.fcntl` API remains unchanged. The focused reproduction and
the complete `tests/test-corpus-bash.py` sweep pass, opening all **296/296**
installed WAST files through guest redirection. Use `--limit N` to bound any
future diagnostic run. Manual `file://` browser boot, DOM/worker, and batch
result-contract evidence remains a distinct acceptance boundary. The user has
now completed that Firefox acceptance: prompt startup and redirection passed,
and `build/html-rt/stage6c-browser-results.json` reports 290 PASS, 0 FAIL,
2 XFAIL, 0 XPASS, and 4 SKIP over 296 records with exit code 0. Browser/native
reports share the same normalized top-level and summary result contract.
