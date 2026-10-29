# Embeddable command runtime and ABI plan

Status: completed; implementation, acceptance and distribution audit passed.
Created: 2026-10-28.
Completed: 2026-10-29.

## Implementation and evidence

The public C session contract and Wasm host ABI are documented in
[embedding.md](embedding.md). `src/engine/session/` owns the shared store/driver,
copied inputs, raw streams, direct launch, bounded execution, cancellation,
resource budgets and retained file/result inspection. Native CLI and browser
WAST/TTY consumers use private compatibility adapters over that same ownership.
Normal browser Bash startup directly launches the installed executable.

The standalone profile, JS host, worker and selected-resource packager live in
`src/html-rt/command/`. `./start.sh --embedding` builds the runtime and offline
AWK/M4/DOT example; `--embedding-check` runs native and Wasm/worker/package gates;
`--embedding-browser` also runs actual Chromium over `file://`. The native
`embedding-library`, `embedding-native-package` and `embedding-example` targets
provide a linkable C archive, public header and separate consumer.

Verified evidence is retained under `build/engine/logs/`:

| Evidence | Result |
| --- | --- |
| `embedding-check.log` and `embedding-browser-results.json` | Public C API and standalone Wasm/worker parity, raw streams, finite/async stdin, environment/cwd, isolation, signal/exit/trap distinctions, cancellation, quotas and subsequent reuse. Exact native/browser DOT SVG bytes. |
| `embedding-native-final.log` | Retained shared-page/file accounting checks and allocation failure injection at all 90 allocations on the small command's success path; cleanup is verified at each failure point under ASan/UBSan. Includes cancellation after a child process exists. |
| `embedding-native-applications-final.log`, `embedding-native-adapter.log` | All 47 native application boundary checks pass in both ordinary and sanitizer builds; the direct-start gate passes 366 checks. |
| `embedding-engine-regressions.log` | i32 and continuation sanitizer gates, process-group/terminal-descriptor sessions, and all 26 installed executor regression files pass. |
| `embedding-bash-offline.log` | Actual offline Bash DOM/worker boot and direct startup; 26 executor regressions with 1,486 assertions agree with native results; wait/clock/signal/terminal sessions, batch cancellation and return to Bash pass. |
| `embedding-frontend-packaging.log` | Authored/installed frontend paths, embedded current VFS bytes/metadata and offline references pass. `./start.sh --html-bash` and `--html-check` also pass. |
| `embedding-offline-results.json` | The small standalone page runs AWK/M4/DOT, streamed input, raw separate outputs, SVG retrieval/download availability, CPU cancellation and reuse. Page and worker requests are monitored; only the page and Blob are requested. |
| `embedding-fresh-release.log`, `embedding-repeat-release.log` | Separate fresh build/generator directories work; unchanged repetition needs no rebuild. The binary-command profile requires no Flex/Bison generation. |
| `embedding-native-package.log` | Native archive/example build and mounted-VFS confinement/ownership checks pass, including 1,105 current nodes. A separately linked public-header-only consumer runs all three commands. |
| `embedding-source-results.json` | AWK, M4 and DOT corresponding-source archives retain revision/digest records, the public header, upstream sources and WASTE's MIT notice. Missing M4 source objects can be supplied from an explicit build-directory cache without changing submodules. |
| `embedding-selection-check.log`, `embedding-native-consumer-results.json` | Shell dependencies are explicit; reducing a package to DOT removes prior tool resources. A consumer linked only against the distributed C header/archive runs AWK, M4 and byte-identical DOT output. |

The measured command artifact is about 405 kB raw / 130 kB gzip with three host
imports (monotonic clock, realtime clock and entropy), 16 MiB initial outer
memory, and approximately 46.5/46.7/52.1 MB observed outer memory after the
AWK/M4/DOT examples. Checked budgets remain 1 MiB raw, 256 KiB gzip and 128 MiB
outer memory. Resource quotas measure the guest backing objects/work documented
in the guide, separately from these packaging budgets.

Packaging reads current `src/vfs` bytes and never compiles or reinstalls guests.
Default packages omit Bash; explicit shell profiles support the documented
subprocess operations. Notices and versioned distribution metadata accompany
selected resources. No external release publication or new text-command profile
is part of this completion boundary.

## Objective

Let another project execute the installed WASTE guest commands directly, without
starting interactive Bash, constructing a WAST launcher, or including the shell
and diagnostics frontend. The first supported examples are AWK input processing,
M4 expansion, and Graphviz DOT rendering with SVG retrieval.

Deliver a reusable C command-session API, a browser Wasm command profile named
`wasm.wasm`, and a small JavaScript host with optional worker execution. Native
and browser adapters must run the same guest executables and shared libc using
the existing engine-owned process, kernel, linking, and continuation semantics.
Guest instructions continue to be interpreted by WASTE.

This plan develops the proposal in `embed-waste.txt`. Existing design and test
boundaries are recorded in [architecture.md](architecture.md),
[techniques.md](techniques.md), and [guest-sdk.md](guest-sdk.md).

## Existing implementation and gaps

| Area | Starting point | Required change |
| --- | --- | --- |
| Public C API | `src/engine/include/waste.h` and `src/engine/api.c` expose modules, instances, and function invocation. | Add an opaque command-session interface with process resources and host services. |
| Direct command startup | `src/engine/process_driver.{c,h}` already accepts an executable, argv, and environment. | Wrap the shared driver rather than creating a second loader or scheduler. |
| Native application | `src/cli-rt/application.c` and `native_runtime.{c,h}` implement direct launch and host I/O. | Separate result inspection from destruction; adopt the public session API. |
| Browser execution | `src/html-rt/browser_api.c` exposes WAST-oriented exports and keeps execution state in runtime globals. | Add a command adapter with explicit session ownership, independent of script parsing and assertion buffers. |
| Filesystem | `src/engine/vfs.{c,h}` and the engine kernel own mounted files. | Support bounded in-memory inputs and output inspection until explicit destruction. |
| Ownership verification | `src/cli-rt/test/direct-start.c` checks startup, failure recovery, and teardown natively and as Wasm. | Extend retained host-boundary checks for the public session contract. |

## Interfaces and ownership

Document three distinct contracts before naming the first stable release:

| Contract | Scope |
| --- | --- |
| Guest ABI | Guest imports, CRT startup layout, library namespaces, memory/table sharing, fixed-width POSIX layouts, and errno conventions. Preserve existing binaries while introducing embedding. |
| C session API and Wasm host ABI | Session lifecycle, configuration, filesystem and streams, execution/resume, errors, limits, and ownership. Use opaque C objects and integer handles across JavaScript. |
| JavaScript API and worker protocol | Marshal bytes and strings, schedule resumes, deliver input and cancellation, collect results, and dispose resources. Keep POSIX semantics in C. |

The host ABI must declare its major/minor version, supported feature set, and
record sizes. Reject incompatible versions, unknown required features, invalid
handles, and out-of-bounds buffers with structured errors. Specify integer
widths, string encoding, lengths, enum values, and overflow handling. Avoid
serializing native C pointers, `size_t`, compiler padding, or internal structs.
Distinguish host ABI compatibility from guest import compatibility and artifact
release versions; check providers against actual guest imports.

All public input ownership must be explicit. The initial API copies arguments,
environment, paths, and file bytes before returning. Output copy operations
produce caller-owned bytes. Any borrowed view must state its invalidation rules;
JavaScript must reacquire linear-memory views after calls that may grow memory.
Keep mutable execution data per session and immutable decoded assets separate
from process state. Do not expose `native_store` or kernel internals publicly.

## Session behavior

Use this lifecycle, with exact symbols settled in stage 1:

```text
create -> populate/configure -> start -> step/resume -> completed -> inspect -> destroy
                                           ^    |
                                           | wait/input/timer/cancel
                                           +----+
```

- A session launches one initial executable and owns its process tree, kernel,
  descriptors, memory, providers, output buffers, and error state. A subsequent
  high-level `run()` gets a fresh session; input-file templates may be reused.
- Accept an executable path, literal arguments, environment, working directory,
  input files/directories and modes, stdin, requested output paths, and limits.
  Define argv[0] construction; JavaScript `args` excludes argv[0]. Validate NULs,
  path components, counts, lengths, and missing parents before execution.
- Batch descriptors are non-TTY streams. Preserve arbitrary bytes, including
  NULs, and separate stdout/stderr without newline or terminal transformations.
  Finite stdin closes after its final byte. Streaming input supports explicit
  EOF, bounded queues, and an explicit input-needed state.
- Preserve `EXEC_YIELD` through ordinary C returns and existing engine-owned
  continuations. Publish wait kind and next deadline; resume saved execution
  without replaying startup or guest side effects. Do not add Asyncify.
- Distinguish guest exit, signal termination, trap, setup/ABI failure, timeout,
  cancellation, and resource-limit failure. An ordinary nonzero guest exit is
  a command result. Define when `exitCode` is meaningful and retain diagnostics
  and captured streams for every completed outcome.
- Retain the final session filesystem until destruction. Expose bounded stat,
  listing, and file-copy operations; the high-level API copies only explicitly
  requested output files. Define missing-output and partial-output behavior.
  Never implement collection by invoking a guest shell or `download` command.
- Define limits for guest memory, processes, filesystem bytes, input/output
  buffering, execution work and elapsed time. Report exceeded limits explicitly
  rather than truncating output silently. Host adapters supply monotonic time;
  bounded execution steps let a worker receive cancellation even during CPU work.
- Define invalid transitions, repeated cancellation/destruction, and startup
  rollback. A terminal outcome stops remaining child work before inspection;
  destruction releases every process, provider, pending operation, and buffer.

Host access is explicit: the default embedder has only supplied guest files and
configured stream/time services. There is no implicit host filesystem or network
access. Optional callbacks provide capabilities; unavailable operations return
the documented unsupported error. Browser-specific callbacks and native syscalls
stay in their runtime directories.

## Implementation stages

### 1. Specify the command-session contract

- [x] Inventory current guest imports, startup requirements, host services, and
  cleanup paths used by the three commands. Record required and optional features.
- [x] Draft the C additions in the existing public header, the Wasm export/import
  table, status records, version handshake, and JS `createWaste`/`run` contract.
- [x] Specify stream/EOF behavior, file modes, working-directory defaults,
  result ownership, limits, wait transitions, and cancellation semantics.
- [x] Audit singleton state and WAT/WAST dependencies that prevent a standalone
  binary-command build. Record necessary extraction work and compatibility tests.

Acceptance: every lifecycle transition and externally visible buffer has an
owner; a consumer can describe a complete AWK run and DOT output retrieval
without internal headers or a script launcher. Proposed symbols and targets
are identified as proposed until implemented.

### 2. Implement the shared session and native adapter

- [x] Add session implementation under `src/engine/`, preferably a dedicated
  `session/` directory, with the existing driver/store/kernel behind the API.
- [x] Implement copied in-memory files, batch streams, direct launch,
  step/resume, result inspection, cancellation, and explicit destruction.
- [x] Keep platform allocation and host services behind the existing runtime
  boundaries. Keep hot execution paths allocation-free except existing bounded
  guest operations; any optional profiling remains behind `WASTE_PROFILE`.
- [x] Complete per-session resource accounting at memory allocation/growth,
  copy-on-write, shared mappings, file growth/truncation, fork, and instruction
  dispatch boundaries. Charge retained backing objects once and release charges
  on rollback and final destruction. Document what each quota measures and
  distinguish guest quotas from outer-engine memory and distribution budgets.
- [x] Adapt native application execution to the new lifecycle while preserving
  public CLI behavior, inherited host descriptors, and opt-in report handling.
- [x] Complete retained C ownership/callback checks and native sanitizer coverage.
  Inject allocation failures through startup and execution; keep host allocation
  failures distinct from guest traps, and verify cleanup at every failure point.

Acceptance: direct AWK execution works with only its executable and required
libc/resources. Inputs and outputs remain byte exact; exit and failure statuses
are distinct; file inspection precedes destruction; repeated sessions are isolated.

### 3. Build the standalone Wasm command profile

- [x] Add thin command exports in `src/html-rt/` over the shared session API,
  with checked integer handles and bounded copies at the JavaScript boundary.
- [x] Compose `wasm.wasm` through `src/html-rt/Makefile`; reuse shared engine
  objects and the browser allocation/platform backend. Separate optional script
  handlers from binary startup rather than duplicating process semantics.
- [x] Exclude shell UI, terminal renderer, installed corpus, parser generators,
  and WAST assertion storage from the command artifact where no selected feature
  needs them. Audit transitive parser/virtual-interpreter dependencies explicitly;
  report unsupported optional child commands rather than routing to another runtime.
- [x] Record raw/compressed artifact size, minimum host imports, initial memory,
  and measured peak memory for each example. Establish budgets from measurements.

Acceptance: a minimal host instantiates the artifact, supplies selected guest
bytes, runs AWK directly, handles a yield, and copies results. No WAST text,
Bash bootstrap, full VFS distribution, or frontend globals are required.

### 4. Provide JavaScript and worker adapters

- [x] Add authored host/worker modules under `src/html-rt/`, separate from the
  shell UI. Proposed distribution names are `waste.mjs` and `waste.worker.mjs`.
- [x] Accept engine and guest assets as byte buffers; loading URLs is optional.
  Implement literal argument marshalling, stream delivery, limits, result copies,
  and cleanup through the checked command exports.
- [x] Make `run()` asynchronous and provide cancellation through `AbortSignal`.
  Preserve structured runtime failures separately from ordinary guest exit codes.
- [x] Version worker messages and associate requests with run IDs. Ignore stale
  replies, finish each run once, and release resources after cancellation/error.
- [x] Initially isolate concurrent jobs with separate Wasm instances/workers.
  Verify sequential reuse explicitly; do not promise concurrent calls or
  reentrant callbacks within one session.
- [x] Provide a minimal standalone `file://` example using embedded assets and
  bundled/Blob worker code. Require no external requests, server, external module
  imports, SharedArrayBuffer, or cross-origin-isolation configuration.

Acceptance: the same high-level invocation works through the direct host and
worker adapters; CPU-bound cancellation reaches a terminal outcome; a second
run after success, trap, or cancellation starts with clean state.

### 5. Demonstrate selected command packages

- [x] Add AWK, M4, and DOT examples using the existing installed guest binaries.
  Cover arguments, stdin, separate streams, file inputs, explicit exit codes,
  and DOT `-o` SVG retrieval after guest exit.
- [x] Define explicit package resource lists and required library providers.
  Build minimal packages from the current `src/vfs` tree; do not compile or
  reinstall commands during embedding or HTML packaging.
- [x] Include Bash only for requested subprocess features: AWK `system()` and
  command pipes, and M4 `syscmd`/`esyscmd`. Exercise both available and missing
  shell dependencies. Preserve documented package profiles and kernel limits,
  including the inherited regular-file-offset limitation across fork.
- [x] Generate release metadata containing host ABI version, guest ABI/profile,
  selected features, resource paths, and artifact digests. This is distribution
  metadata, not a source-hash approval requirement for local VFS edits.
- [x] Preserve package notices and existing corresponding-source workflows.

Acceptance: a consumer can embed just the engine, libc, and a chosen command's
resources. AWK/M4 output and DOT SVG bytes are available without shell UI or
test assets. Subprocess examples declare their additional dependencies.

### 6. Consolidate consumers and document the release

- [x] Migrate browser shell startup and suitable test-host plumbing onto the
  shared session lifecycle, retaining WAST assertions and terminal behavior in
  their own consumers. Preserve existing exports through compatibility adapters
  where current workers/tests still depend on them.
- [x] Keep the existing native `wasm`, `wat`, and `wast` commands operational.
  Add a focused build/test option to `start.sh` for embedding and document the
  implemented targets, outputs, minimum tools, and reproducible examples.
- [x] Publish an embedding guide and ABI reference; update architecture and
  techniques only with implemented boundaries and practices. Include a native
  C example, the JS API, offline embedding, dependency selection, and limitations.
- [x] Run the acceptance matrix below and record results, sizes, and remaining
  limitations under `build/engine/logs/`. Mark stages complete only with evidence.

Acceptance: existing CLI and offline Bash workflows pass, and a separate small
consumer runs all three examples using only documented public interfaces.

## Delivery order

1. Close shared-session ownership, allocation-failure, and resource-accounting
   gaps. Update the draft ABI reference alongside any changed record or feature.
2. Establish a minimal AWK consumer using the public API, then exercise the same
   lifecycle with M4 and DOT, including retained SVG output. Keep native/browser
   comparisons on identical installed guest bytes.
3. Package selected resources and build the standalone offline example. Verify
   actual browser workers, cancellation, output retrieval, and absence of external
   runtime requests before advertising offline embedding support.
4. Migrate existing native and browser consumers behind compatibility adapters,
   add the focused `start.sh` workflow, and run the final acceptance matrix.

The first reviewable milestone is a bounded AWK invocation with copied inputs,
byte-exact streams, explicit status, and deterministic cleanup through both C and
JavaScript. It establishes the reusable boundary; completion still requires all
six stages and the three-command offline consumer described above.

## Verification and test ownership

Keep guest behavior assertions in canonical WAST under
`src/vfs/root/test/system/embedding/` where new cross-runtime coverage is needed;
reuse existing `src/vfs/root/test/aux/{awk,m4,dot}/` assertions for package behavior.
Direct execution tests must launch commands through the embedding API. WAST can
inspect resulting bytes in test builds, but production embedding must not depend
on WAST parsing. Keep API ownership, callback, handle-validation and teardown
checks in retained C/host probes, as described in the test-boundary guidance.
Host runners belong under the owning engine, CLI, or HTML runtime test directory.

| Gate | Required observations |
| --- | --- |
| Native C API, warnings-as-errors and ASan/UBSan | Startup rollback; copied input lifetime; invalid handles/ranges; result ownership; isolation; cleanup after exit, trap, cancellation, and allocation failure. |
| Native/browser command parity | Same guest bytes, argv/environment/cwd, finite and streaming stdin/EOF, stdout/stderr bytes, statuses, and retrieved files. Include NULs and output beyond one chunk. |
| Scheduling and limits | Read/timer waits, CPU-bound cooperative pumping, cancellation of child work, late worker replies, memory/output/file limits, and clean subsequent runs. |
| Package fixtures | AWK processing; M4 expansion/includes; DOT SVG structure/geometry and output-file retrieval; optional shell operations and missing providers. |
| Offline consumer | Actual `file://` page and production worker boot, execute, cancel, and retrieve results without external runtime requests. Node worker checks alone do not establish this browser gate. |
| Existing runtime acceptance | Relevant direct-start, continuation, descriptor and process-group sanitizer gates; native application checks; `./start.sh --html-bash`, `./start.sh --html-check`, packaging verification, and focused offline-browser acceptance. |
| Build/distribution | Fresh and unchanged repeat builds, outputs only under `build/`, explicit installs, unchanged submodule contents, selected resource closure, licenses and corresponding-source artifacts. |

Run checks appropriate to each stage. For changed shell/Python tooling include
`bash -n start.sh`, bytecode checks for changed Python files, and
`git diff --check`. Do not require a full conformance sweep for documentation
changes or treat upstream tool output as a substitute for API ownership tests.

## Follow-on work and completion boundary

`wat.wasm` and `wast.wasm` are later build profiles over the same session and
engine semantics. Add them after the binary-command interface and consumers
are stable; retain their text parsing, assertion reporting, and language-context
isolation in those profiles. They do not block the initial embedding release.

Initial completion requires the public C and Wasm contracts, JS/worker adapters,
three command examples, bounded result/file retrieval, cancellation and cleanup,
an offline consumer, and preserved native/Bash acceptance. No new POSIX broker,
JIT, broad POSIX conformance claim, or additional Graphviz renderer is required.
Update this plan as implementation establishes measured limits and resolves
dependencies; move durable implemented contracts into the regular documentation.
