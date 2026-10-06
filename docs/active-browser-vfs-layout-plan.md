# Shared VFS, browser source, and native test runtime refactor

Status: complete — Stages 1–7 are complete. Native and production-browser batch runners consume the installed manifest/tree through command-stream execution with assertion reporting, isolation, deadlines and explicit skips; ordinary execution needs no Node process. Portable coverage includes twelve batch fixtures totaling 1,230 assertions and six shared sessions totaling 358 checks. Stages 6B.22–6B.38 move 188 path/kernel/signal/wait/SELECT/terminal assertions out of C and retain private/API sanitizer gates. The current posix-kernel.c inventory is complete: 21 helpers, 121 retained sites and 261 runtime checks; Stage 6B.39 accounts for all fourteen libc clients/56 assertions and adds strict native/sanitizer completion gates. Stage 6B.40 accounts for the legacy DIY kernel/control drivers and adds a seven-check module-start/SIGINT session, retaining two kernel scheduling gaps and the runnable-loop pause gap. Stage 6B.41 promotes the three DIY language/host-memory compatibility fixtures to shared WAST streams, preserving 25 checks and memory isolation; Stage 6B.42 reports setup and EOF completion independently; Stage 6B.43 fixes silent 32-segment truncation and bounds data/element capacity at 128; Stage 6B.44 preserves non-null function-index element types; Stage 6B.45 adds the standard spectest.table64 binding; Stage 6B.46–6B.47 add bulk table/segment syntax with 801 portable checks; Stage 6B.49–6B.50 complete byte-preserving names; Stage 6B.51 aligns bare-inline-module definition behavior and closes the official language ledger at 261/261; Stage 6B.52 adds exported table32/table64 element-list shorthand; Stage 6B.53 audits 117 process-lifecycle C checks and widens loaded-library name storage to the WAST name bound; Stage 6B.54 audits all 58 exec-transition matrix checks and confirms guest-session coverage complements without replacing private transition/ownership checks; Stage 6B.55 updates the legacy exec-lifecycle harness to read sparse guest memory through the supported API and audits all 22 continuation checks; Stage 6B.56 repairs the older process-contract harness memory accesses and audits its child/parent process checks; Stage 6B.57 audits the repeated READ-yield continuation probe, retaining its host-controlled resume sequence as a native scheduler gate; Stage 6B.58 audits all 32 store-checkpoint invariants across nested restores, shared/imported objects, process mapping topology and clone binding; Stage 6B.59 accounts for all 27 native VFS inventory assertion sites plus mounted-path WAST and directory checks; Stage 6B.60 audits the browser VFS staging harness, including six complete installed-package staging cycles; Stage 6B.61 audits the tarballjs/loader contract; Stage 6B.62 audits VFS packager guards, including directory/archive parity and atomic installer rejection; Stage 6B.63 audits the upload/download browser-worker session; Stage 6B.64's static packaging harness passes; Stage 6B.65 moves Bash runtime assets into the compressed VFS package and starts them after zlib-wasm extraction. Full native corpus parity is 289 PASS / 2 XFAIL / 5 SKIP over 64,309 assertion/action results. All 56 test sources are named and dispositioned, and Stage 6B acceptance is complete. Stage 6C is complete: its variadic fcntl fix passes the focused check and 296-file guest-redirection sweep, and the user-verified Firefox run passes page boot, redirection and the full assertion-aware browser batch. Stage 7 is complete: the standalone test dashboard and its build workflow are retired; Bash-page diagnostics and direct worker payload tests preserve their roles. The shared import adapter preserves zero-count read/write behavior and provides real signal-set, process-mask and blocked-pending queries. Native batches resume finite SELECT waits. Stage 6B.16 duration-guided Node dispatch remains a focused optimization only.


Current loading contract (Stage 6B.21): cli-rt reads `src/vfs` directly with
`--vfs-root DIRECTORY`; html-rt reuses tarballjs-extracted files. Both use the
installed inventory for metadata and content validation. WVFS generation,
mount options and browser payloads are retired. Earlier dated landing notes
below retain WVFS descriptions as historical evidence, not current commands.

## Bash webapp package (Stage 6B.65)

The Bash page keeps a small loading screen, tarballjs reader, loader, and
zlib-wasm bytes in the standalone HTML. Startup compiles zlib-wasm first, uses
it to inflate the compressed tar archive, extracts the installed VFS into
memory, then loads the Bash theme, terminal renderer/font, suite controller,
shell app, and worker from `/waste/app`. The shell loop starts only after those
steps complete. Refresh these distribution snapshots with
`make -C src/html-rt vfs-install-app`; the installer/auditor requires the
complete app set. The Bash page owns the installed-test diagnostics UI; focused
worker conformance tests consume generated JSON payloads directly.

## OCaml scope decision (2026-10-04)

The OCaml interpreter is retained only as an oracle for Wasm/WAT/WAST language
semantics and standard spec-test scaffolding. The application-engine experiment
was not practical. No additional OCaml kernel, POSIX, VFS, process, scheduler,
signal, terminal, libc-host or broker development will be undertaken.

The existing OCaml kernel and application-runtime integration are planned for
removal. Implementation pruning is deferred to
[the OCaml language-oracle and retirement plan](active-ocaml-language-oracle-plan.md);
it is not an added Stage 6B kernel acceptance gate. Missing OCaml POSIX imports
and kernel profile differences are outside oracle scope, not development gaps
or blockers. Verify C kernel/libc/session behavior through native/browser C
parity, private sanitizer gates, compiled guest ABI checks and applicable POSIX
contract fixtures. WAST syntax alone does not make a POSIX test a language test.

Dated landing reports below retain historical OCaml comparisons and import
rejections as evidence. Their earlier kernel-parity/probe requirements are
superseded by this decision. Official WebAssembly-language comparisons remain
required; additional OCaml kernel coverage does not.

## Objective

Flatten the browser frontend into `src/html-rt/src/`, establish
`src/vfs/` as the explicit installed filesystem tree mounted by both runtimes,
install a guest C development header set into its `/usr/include` tree, and make
the existing test corpus runnable primarily through an expanded `cli-rt`.
Keep browser execution available through the offline shell page and retain
focused checks of the compiled browser artifact and its integration boundaries.
Retire `test.html` and its build options after the replacement preserves the
dashboard's execution, reporting, isolation, and differential coverage.

This document plans the refactor; it does not authorize unrelated engine or
POSIX feature work. Each implementation stage should be independently
reviewable and leave a working native runtime and offline browser page.

## Requested outcomes

1. Move the frontend files from `src/html-rt/src/{bash,shared,tests}/` into
   `src/html-rt/src/`, resolving duplicate filenames explicitly.
2. Create `src/vfs/` as the common installed filesystem tree for native and
   browser runtimes, with one inventory and installation path.
3. Relocate guest Wasm executables and libraries into `vfs/bin`,
   `vfs/usr/bin`, and `vfs/lib` according to their runtime roles.
4. Make the headers needed by a future guest C parser/compiler available at
   `/usr/include`. Select guest-compatible declarations rather than moving
   every implementation header; update Makefiles and sysroot tools to use the
   same guest development interface.
5. Place every WAST input previously packaged by the dashboard under
   `vfs/tests`, including required companion files and generated fixtures.
6. Replace dashboard-only execution with shell and automated batch execution;
   then remove the standalone test page and its build option.
7. Expand `cli-rt` to execute the same guest Wasm programs, runtime namespace,
   VFS, and scripted interactions as the browser wherever practical.
8. Consolidate redundant tests and separate page construction from expensive
   verification; retain evidence for distinct engine, adapter, artifact, and
   frontend failure modes.

## Target layout

```text
src/
  html-rt/src/
    index.html             single shell frontend
    app.js                 shell UI and browser interactions
    worker.js              common engine worker and batch-test protocol
    loader.js              offline bootstrap
    style.css
    terminal/              model, renderer, GLF data, and notices
    test-runner.js         batch orchestration/results, if needed
  cli-rt/                  native runner, platform adapters, scripted I/O
  vfs/
    bin/                   shell-facing commands and interpreter launchers
    usr/
      bin/                 installed application executables
      include/
        stdio.h, sys/...   guest-libc application headers
        waste/
          abi/             shared guest ABI declarations, where needed
          waste.h          optional engine-embedding SDK, if linkable
      lib/
        waste/cc/include/  selected target compiler-support headers
                           plus compatibility library aliases as required
      share/               package provenance and source/license mappings
    lib/                   guest shared libraries, including ncurses
    tests/
      core/                includes existing proposal subdivisions
      custom/              existing annotation groups
      legacy/              explicitly unsupported entries remain visible
      diy-posix-test/
      libc-test/
      manifest.json        suite/runtime/source identities and dependencies
build/
  engine/                  generated parsers, toolchain, and logs
  cli-rt/                  native executables
  html-rt/                 browser engine, launch input, archives, bash.html
  ocaml/                   oracle artifacts
```

The directory names under `/tests` preserve the existing group-qualified
identities frozen in [the Stage 1 inventory](browser-vfs-refactor-inventory.md).
The manifest must retain those identities and every proposal subdivision.

## Ownership and packaging decisions

- C implementation ownership and private headers remain in `src/engine`,
  `src/cli-rt`, and `src/html-rt`. Guest application headers are maintained
  under the VFS include tree; native engine/backend builds retain their own
  include roots. Native execution must not depend on a browser build.
- All compiler objects, generated Flex/Bison headers, generated fixtures,
  logs, sysroots, and intermediate archives stay under root `build/`.
- `src/vfs` contains authored filesystem inputs and explicitly
  installed distribution snapshots. A build produces artifacts under
  `build/`; a named install/sync step refreshes the VFS snapshots. HTML
  packaging reads the VFS rather than silently compiling into it.
- Record source revision, build provenance, hashes, destination, mode, and
  meaningful mtimes for installed binaries and test snapshots. Do not replace
  guest file mtimes with the archive's reproducibility timestamp.
- Use one VFS inventory for native mounting, HTML packaging, browser boot,
  audits, sysroots, and tests. The guest view and initial metadata must agree.
  The packaging layer preserves directory hierarchy instead of discovering
  flat `*.wasm` files and inventing guest paths in JavaScript.
- The browser host's `waste-wast.wasm` is a bootstrap engine artifact under
  `build/html-rt`, not an ordinary guest executable. Keep it outside the
  mounted `/bin` inventory. Classify `waste-probe.wasm` separately as a test
  executable rather than treating every Wasm file alike.
- Preserve `/bin` and `/usr/bin` compatibility, the current shell `PATH`, and
  both library lookup paths. Prefer a canonical binary with verified aliases;
  use identical installed copies until guest alias execution is tested. Do
  not rely on tar symlink/hardlink support without verification.
- Keep notices and corresponding-source access for Coreutils, ncurses,
  Rogue, GLF/font assets, and copied test corpora. Preserve upstream sources
  and repository-owned patch transactions.
- Mount a fresh engine-owned filesystem/kernel for each independent test
  sandbox. Sharing immutable package bytes must not share mutable test state.
  The optional POSIX broker remains optional; the page works via `file://`.

## Primary native testing and browser parity

The shared C engine makes native execution the primary verification path for
language semantics, kernel/process behavior, and guest applications. Native
tests do not establish correctness of browser JS adapters, worker messages,
Wasm toolchain output, file dialogs, or rendering. Retain a focused browser
integration gate and an explicit full browser parity mode.

### Expand the native runtime

- Keep the existing `waste-cli FILE.wast` execution path and assertion JSON.
  Extend its runner or add a CLI-owned companion entry point for manifest-based
  suites, installed VFS directories, runtime bootstrap, and scripted terminal I/O.
  The exact flags are decided during implementation and recorded in help.
- Compile the same engine C sources natively. Load the same guest libc/runtime
  modules, Bash launch input, Coreutils/Rogue Wasm executables, and ncurses DSO
  used by the browser. Do not replace interpreted guest programs with native
  Linux binaries or assume guest calls have the host ABI.
- Mount `src/vfs` through a common inventory/metadata contract. Also support
  mounting the generated browser package snapshot for parity tests, so both
  drivers see identical bytes, paths, permissions, aliases, and mtimes. Verify
  that direct-tree and archive mounts agree before selecting the faster path
  for routine native tests.
- Expand native host-import resolution beyond the current select/path subset
  to the imports needed by the guest runtime: descriptors, terminal modes,
  signals, clocks, process startup, exec, dynamic loading, and continuations.
- Extract duplicated platform-neutral guest ABI decoding, import validation,
  errno handling, and kernel operations into `src/engine` where appropriate.
  Native syscalls, browser imports, and browser-specific memory/export APIs
  stay in their platform directories. Do not copy `browser_api.c` into the CLI
  or create a second independent POSIX implementation.
- Drive execution, yields, readiness, resume, process exit/reaping, and
  cancellation through ordinary engine-owned state. Preserve delayed and
  fragmented input scenarios instead of preloading all input and bypassing
  wait transitions. No Asyncify transform is introduced.
- Provide configurable initial terminal dimensions, scripted resize/signals,
  environment/startup values, captured output bytes, and deterministic test
  clocks where needed. Timeouts and cleanup apply to blocked as well as
  runnable guest processes.
- Guest file operations use the engine VFS/kernel even under the native
  driver. Host filesystem access is limited to explicit package/input/result
  staging; it must not mask missing guest paths or bypass guest permissions.
- Simulate upload/download completion and cancellation at the native adapter
  boundary to test guest behavior. Keep separate checks of actual browser
  picker/download interactions; simulation cannot validate browser UI events.
- Keep every independent test's mutable store/kernel/process tree isolated.
  Reuse immutable decoded assets where supported, but retain intentional
  multi-command and multi-child lifetimes within one scenario.

### Test classification and consolidation

| Coverage | Primary gate | Additional retained coverage |
| --- | --- | --- |
| Official core/proposal/annotation WAST semantics | Native manifest runner and OCaml differential oracle | Focused Wasm-artifact parity; full browser corpus at cutover/releases and relevant changes |
| Kernel, memory, linking, process and descriptor lifetime | Native functional plus ASan/UBSan gates | Browser adapter/continuation boundary scenarios |
| Guest Bash, Coreutils, pipelines, libc, ncurses | Native engine running the actual packaged guest modules | Small browser command/input/Rogue lifecycle matrix |
| VT parsing/model and GLF lookup | Existing pure Node fixtures | Actual browser rendering, resize/zoom checks |
| Worker protocol, archive boot, browser clock/input/file transfer | Browser Wasm/worker integration harness | Native checks only for shared guest-side behavior |
| Pixel rendering and DOM/file-dialog interactions | Actual browser/manual or automated browser checks | Not replaced by native execution or Node worker tests |

- Inventory each existing assertion, fixture, and scenario with a stable test
  identity, behavior covered, runtime boundary, prerequisites, and cost.
- Consolidate repeated shell startup and duplicate fixture setup. Use one
  aggregate command matrix and intentional multi-child lifecycle scenarios
  instead of independently booting Bash for every overlapping utility check.
- Keep focused tests selectable for diagnosis without rerunning all of them
  on every page build. Share scenario inputs/expected outcomes between native
  and browser drivers rather than maintaining diverging copies.
- Remove a test only when its unique assertions and failure mode are covered
  by another retained test. Record old-to-new coverage mappings and explain
  intentionally retained duplication, such as sanitizer versus release builds,
  native versus Wasm execution, and repeated waits or process teardown.
- Keep terminal JS fixtures in Node; moving their ownership into the test
  orchestration does not justify translating the production model into C.
- Preserve the full official corpus and oracle comparisons. Consolidating
  harnesses does not mean discarding distinct upstream conformance cases.
- Record baseline and resulting suite durations and engine/Bash startup counts.
  Confirm actual improvement; do not claim speedups from an assumed equivalence
  or native OCaml timings.

### Build and verification commands

Current command contract after Stage 6C.1:

- `./start.sh --html-bash` builds the offline page only. It does not start test
  harnesses and does not require Node.
- `make -C src/cli-rt corpus-native` is the primary installed-manifest C-engine
  corpus run; it emits assertion-aware JSON. `./start.sh --cli-test` remains
  the official core-spec convenience run.
- `./start.sh --html-check` runs focused worker and terminal-model checks
  against an already generated page. It requires Node but does not start
  Chromium.
- `./start.sh --html-browser-full` explicitly runs the full offline browser
  suite against `bash.html`, including actual Chromium/file-page acceptance.

The native batch runner supports group, identity, exclusion, and results-file
filters. Full browser parity remains required at migration acceptance and
releases, and when shared engine behavior, guest ABI, browser adapters, or the
Wasm toolchain change. See [test-corpus.md](test-corpus.md) for the measured
developer and release workflows and current open shell-redirection probe.

## Guest C development headers

The purpose of mounted `/usr/include` is to let a future C parser resolve
application includes from the engine VFS and eventually let a guest compiler
build programs for WASTE. This supersedes the original blanket relocation of
all engine/backend headers. A guest compiler needs the WASTE wasm32 application
ABI, not the headers used to compile the host engine on Linux.

### Header selection

| Existing family | Decision | Destination or ownership |
| --- | --- | --- |
| Guest libc standard/POSIX headers in `src/html-rt/lib/include` | Install the application-facing set after dependency/ABI audit | `/usr/include`, preserving public `sys/`, `arpa/`, and other namespaces |
| Guest `helper.h` | Split public ABI types from libc implementation helpers; do not install the complete helper | Public pieces in `sys/types.h`, relevant public headers, or `/usr/include/waste/abi`; private helpers stay beside libc |
| Engine `lib/include` standard-name headers | Keep separate; do not overwrite or merge them into guest libc headers | Existing engine/native support include roots |
| Engine kernel/store/parser/executor internals and `posix_stubs.h` | Keep private | Their existing engine/backend source locations |
| `src/cli-rt/lib/include/syscall.h` | Keep native-only; includes Linux x86-64 syscall assembly and host layouts | Existing CLI include root |
| `src/engine/include/waste.h` | Optional public engine-embedding SDK, not a default guest libc dependency | Install as `/usr/include/waste/waste.h` only with a usable guest link/import provider; otherwise retain the source API header |
| ncurses public headers | Install pinned wasm32 configuration matching the installed library | `/usr/include/curses.h`, `ncurses.h`, `term.h`, and required public dependencies |
| Compiler-provided fundamental headers | Supply a selected, versioned wasm32 compiler-support set | `/usr/lib/waste/cc/include`, included by the compiler's standard search path |
| Coreutils/gnulib compatibility headers and SELinux build shims | Keep build-specific pieces out of the default SDK; publish useful extensions only with an explicit supported/stubbed ABI | Generated package sysroots or named optional development packages |

The guest application-facing starting set includes `stdio.h`, `stdlib.h`,
`string.h`, `stdint.h`, `inttypes.h`, `assert.h`, `ctype.h`, `errno.h`,
`math.h`, `time.h`, locale/wide-character headers, `unistd.h`, `fcntl.h`,
`dirent.h`, `termios.h`, `signal.h`, `dlfcn.h`, and their supported `sys/*`
dependencies. Also classify existing networking, process, pthread, scheduling,
resource, search, and utility extension headers individually. A header may
describe operations that return an unsupported error, but the SDK must record
that capability honestly and its declared functions must resolve to the
documented implementation or compatibility stub.

Add a normal public `sys/ioctl.h` interface from the existing `sys_ioctl.h`
declarations, with a compatibility wrapper only where current clients need it.
Add guest `sys/mman.h` declarations for the existing guest mapping ABI after
checking the libc signatures, `off_t`, and kernel constants; do not install the
engine's freestanding header merely because its basename matches.

### What to combine

- Maintain one canonical **guest** declaration for each supported function,
  public typedef, structure layout, and constant. Public headers should remain
  separate standard includes, not become a monolithic WASTE header.
- Extract reusable fixed-width guest ABI definitions from `helper.h`. Public
  headers currently including it, such as `termios.h`, `sys_ioctl.h`, and
  `sys/stat.h`, must no longer import private inline helpers and unrelated
  libc externs. Use ordinary public C types in function prototypes.
- Share ABI constants/layout definitions between public guest headers and
  runtime adapters only when they describe the same wire interface. Keep
  native host structures and guest structures distinct; do not use a single
  conditional `stdio.h` or `sys/stat.h` to hide different platform ABIs.
- Audit duplicate declarations and actual layouts before consolidating. The
  engine support `stdio.h` exposes stream objects through address macros,
  whereas guest `stdio.h` declares stream pointers. Engine `sys/stat.h`
  describes x86-64 Linux; guest declarations describe the WASTE interface.
  Neither pair is safely combined by concatenating declarations.
- Preserve the current guest ABI during this refactor. Audit public/private
  `FILE`, `stat`, `dirent`, termios, timestamp, signal, and descriptor types
  against the actual implementation and prebuilt Bash. Any mismatch gets a
  focused fix and regression evidence rather than an unrecorded ABI change.

### Compiler and parser support

The current sysroot wrapper uses both guest headers and the host Clang resource
include directory. Mounting only `src/html-rt/lib/include` would therefore
leave an incomplete SDK: its `stdint.h` uses `#include_next`, and other headers
require compiler-supplied `stddef.h`, `stdarg.h`, `stdbool.h`, and `limits.h`.

- Define an initial C11/wasm32 target profile matching the existing Clang-built
  applications. Record integer/pointer widths, alignment, signedness, 64-bit
  `time_t`, structure layout rules, and the variadic-call ABI. Derive remaining
  details from target probes and `docs/wasm32-abi.md`; do not assume the native
  host's data model.
- Provide fundamental headers including the required transitive dependencies
  of `stddef.h`, `stdarg.h`, `stdbool.h`, `stdint.h`, `limits.h`, and `float.h`;
  include `stdalign.h` and `stdnoreturn.h` for the declared language profile.
  Install only reviewed target-relevant compiler headers with licensing and
  provenance, not an unrestricted host compiler resource tree.
- Make search order explicit: user quote/`-I` paths, guest public SDK, then
  compiler-support headers. Resolve all system includes from mounted paths,
  with no host `/usr/include` fallback. Resolve `#include_next` deterministically
  or remove that dependency from the canonical guest fixed-width header.
- Provide target-specific builtin types/operations for `va_list`, `va_start`,
  `va_arg`, `offsetof`, and the builtins used by `math.h`, `alloca.h`, and
  `stdbit.h`. A parser needs explicit handling of these constructs; headers
  alone do not implement their compiler semantics.
- Permit compiler-specific intrinsic implementations behind a small support
  layer while keeping application declarations and layouts canonical. A future
  non-Clang compiler may need different support headers, but must not silently
  use a different libc ABI. Predefined macros, attributes, and extension syntax
  are part of its documented compatibility profile.
- Move forced Coreutils/gnulib configuration shims such as
  `waste-gnulib-compat.h` into that package's build profile. Default application
  compilation must not require its global forced include.
- Install ncurses's configured bool and structure declarations as built.
  Lock down include-order consistency with standard `stdbool.h`; retain a
  clearly scoped Rogue compatibility profile until its declarations are safe.
- Track CRT/startup, import providers, libc, and linker requirements alongside
  headers in the SDK manifest. Header availability enables parsing; runnable C
  also requires compatible code generation and linkage. Building the future
  parser/compiler itself is outside this layout refactor.

Gate: a hermetic include/declaration probe can preprocess representative guest
programs using only the public SDK and installed compiler-support headers, with
no private backend dependency. Unsupported compiler constructs are reported
explicitly rather than substituted with host definitions.

## Stage 1: Inventory and freeze the path contract

Status: complete (2026-09-30)

Implemented the reproducible inventory/baseline collector at
`src/html-rt/tools/inventory-runtime-refactor.py`. The repository
[inventory and path contract](browser-vfs-refactor-inventory.md) records the
35 frontend/staging files, 92 source-header classifications, all 284 dashboard
inputs (280 supported), collision/alias decisions, consumer/dependency map,
guest paths, result schemas, native import gaps, and consolidation boundaries.
Generated hashes, packaged specifications, measurements, and logs are under
`build/engine/refactor-baseline/`; rerun instructions are in the inventory.

Fresh native/browser builds succeeded. All eleven Bash scenarios and terminal
model/GLF tests pass. Native core has one existing import-test failure; the full
Node dashboard has three import-test failures and fourteen stale libc payload
length failures. These are explicitly recorded, not a claim of a green parity
gate. No files were relocated and no tests removed. Oracle artifacts/pin and
real-browser verification boundaries are recorded without claiming new runs.

- Enumerate all authored frontend files, installed binaries, symlinks,
  generated staging files, C headers, dashboard groups, and test dependencies.
- Record every consumer of the old directories: all three runtime Makefiles,
  `start.sh`, sysroot/application/libc builders, HTML generators,
  `amalgamate.py`, package audits, Node harnesses, and documentation.
- Define a source-to-destination mapping with no filename collisions. Both
  old frontends have `index.html`, `app.js`, `worker.js`, and `style.css`;
  move shell files to those names and temporarily rename dashboard files to
  `tests-index.html`, `tests-app.js`, `tests-worker.js`, and `tests-style.css`.
  Retain them only until the unified test runner is accepted.
- Move `bash/terminal/` to `src/terminal/`; do not flatten its asset namespace.
- Capture the current complete test inventory and baseline result schema,
  group counts, runtime requirements, and configured oracle comparison.
- Capture suite durations, startup counts, duplicate assertions, host-import
  coverage gaps, and which checks currently run in Node versus a real browser.
- Document an explicit mapping for Bash startup input, `/bin/wat`,
  `/bin/wast`, Coreutils, auxiliary commands, Rogue, and shared libraries.

Gate: every existing file and packaged guest path has a documented owner and
destination; baseline inventories and test results can be compared after each
stage. No destructive overwrite of dirty or conflicting files is permitted.

## Stage 2: Flatten frontend sources

Status: complete (2026-09-30)

Moved authored shell assets to `src/html-rt/src/{index.html,app.js,worker.js,
style.css}`, the terminal namespace to `src/html-rt/src/terminal`, shared loader
and repaired vendor link to the source root, and temporary dashboard assets to
`tests-{index.html,app.js,worker.js,style.css}`. Guest `.wasm` snapshots remain
in `src/html-rt/src/bash` until Stage 3; that directory no longer owns frontend
JS, HTML, terminal assets, launch input, or a host-engine link.

Generated test metadata now lives in `build/html-rt/tests/payload.json`.
Bootstrap inputs come directly from `build/html-rt/{waste-wast.wasm,
bash-runtime.wast}`; optional Bash staging copies go under `build/html-rt/bash`.
Removed the source-staged payload and three generated bootstrap links; generated
data remains recoverable by rebuilding and the original links/payload remain in
Git history. Packaging work directories now stay under root `build/html-rt`.

Updated `start.sh`, loader staging paths, the amalgamator, generators, inventory
collector, and Node fixtures. Both generators retain `--output` and
`--output-dir`, but HTML generation now delegates to the common packager and
authored frontend instead of maintaining duplicate inline JS/worker/UI templates.
Generated staging destinations outside repository `build/` are rejected.
Dashboard packaging now includes `tests-worker.js`; archive-mode Node harnesses
read actual embedded engine, worker, payload, WAST and application bytes instead
of falling back to live staging copies. Coreutils package auditing remains a
shell-package check, not a requirement that the test dashboard contain utilities.
Packaging rejects stale WAST length metadata and unsafe/duplicate test paths
before replacing an output page.

Verification:

- All 284 identities, groups, source hashes and bootstrap bytes match Stage 1;
  280 remain supported. Terminal sources and both workers are byte-identical
  after relocation. Only the fourteen stale libc `sourceBytes` fields changed
  in the complete regenerated payload; all other packaged spec fields match.
- Native core remains 96/97 files and 20,063/20,066 assertions passing, with the
  same three failing assertions in `core/imports.wast`.
- All eleven Bash scenarios pass using the actual rebuilt HTML archive,
  including command matrix, readline, heredoc, clocks/mtimes and two Rogue
  children. Terminal model/GLF fixtures and frontend/package checks pass.
- Full archive-backed dashboard execution reports 267/280 passing files and
  retains the three import-failure files. Refreshing
  stale libc metadata allows four libc fixtures to pass and exposes ten
  previously masked failures (`unknown module id` diagnostics); this is not a
  green libc/parity claim. Engine, workers, fixture bytes and all other spec
  fields are unchanged. Logs/results are under `build/engine/refactor-stage2`.
- Both normal packaging targets and the generators' direct HTML output modes
  build successfully. Actual headless Chromium `file://` checks verify archive
  boot/decompression, DOM initialization, Bash input/output, WebGL initialization,
  dashboard worker execution and absence of external runtime requests.
  Screenshots are under `build/html-rt/offline-browser-check`.
- Shell syntax, changed Python bytecode, JS syntax and whitespace checks pass.
  No engine/header semantics, guest installation paths, test assertions or
  oracle sources were changed. Manual upload/download and cross-browser visual
  regression checks remain separate future gates.

Reproduce the focused layout checks with:

```sh
node tests/c-engine-frontend-packaging.cjs
node tests/c-engine-offline-browser.cjs
```

The latter requires Chromium (override its executable with `WASTE_CHROMIUM`),
uses a temporary private profile and debugging pipe, and does not start a web
server or require a browser-driver package. The complete inventory/baseline
collector now exercises packaged pages when measurements are requested.

- Move authored frontend assets according to Stage 1, updating relative
  script/style references, worker bootstrap paths, and GLF notice paths.
- Update staging constants in `start.sh` and source paths in both browser
  harnesses, generators, and the amalgamator.
- Establish the authored frontend as the source of truth. Resolve duplicated
  inline JS templates in generators so regenerated output cannot restore an
  obsolete worker, UI, or directory layout.
- Move generated payloads and launch inputs out of source staging into
  `build/html-rt`; do not overwrite authored assets during generation.
- Keep both entry points functional during migration using distinct temporary
  dashboard names in the single source directory.

Gate: shell and terminal-model/GLF fixtures pass with the new paths; the
existing dashboard inventory and assertion outcomes have no new failures
against the recorded baseline. Refresh stale payload metadata and separately
triage existing import failures before claiming full parity. The browser pages
remain self-contained offline files and require actual packaging/boot checks,
not only the Node worker harness.

## Stage 3: Install and mount the explicit VFS tree

Status: complete (2026-09-30)

Installed `src/vfs/.inventory.json` and its distribution tree: 57 declared
nodes (41 physical files, 14 directories, and two engine-owned interpreter
launchers). Sixteen commands have canonical `/usr/bin` executables and verified
identical `/bin` copies. Ncurses lives under `/lib` with its `/usr/lib`
compatibility copy. The tree also contains the guest-visible launch input,
Coreutils build/source-package mappings, interpreter metadata, and Coreutils,
ncurses and Rogue license notices. GLF notices remain with the authored frontend.
The probe uses the same built test executable already selected by the Stage 2
packager, rather than the smaller obsolete source-staged probe.

`src/html-rt/tools/vfs.py` provides explicit install, audit, image, package and
archive-image operations. Installation validates the complete update batch
before atomically publishing a replacement tree. It protects edited snapshots,
rejects missing/partial compiler results, duplicate destinations, escaping
paths, omitted nodes and divergent aliases, and checks Wasm compilation,
required function exports, import namespaces and the recorded import contract.
Additional imports require an explicit ABI review; Asyncify names are rejected.
Hashes, source input paths, installation revision, uid/gid, inode identities,
modes and meaningful source mtimes are recorded. The installation revision is
not a claim that imported historical snapshots were rebuilt in this stage;
Coreutils' existing build provenance and source mapping remain packaged.

Inventory timestamps and empty directories are authoritative: Git cannot
preserve either on checkout. A checkout with missing empty directories or
different host file mtimes still produces the same guest image. Packaging
materializes every declared directory and audits tar permissions. Tar ownership
and reproducibility timestamps remain normalized; guest metadata comes from
the inventory rather than those archive timestamps.

The common, bounded `src/engine/vfs.c` decoder validates WVFS v1 records before
mutation and mounts copies into a fresh engine-owned kernel. Browser boot now
submits that image instead of inventing paths from flat Wasm filenames. Host
engine, worker, image and inventory metadata are not mounted guest files.
The archive contains both the explicit directory tree and its serialized mount
image; auditing verifies that they encode identical bytes and metadata and that
host/guest launch inputs agree. The extra serialized representation is an
intentional parity check, not a packaging-size optimization.

`waste-cli --vfs-image IMAGE FILE.wast` uses the same decoder; `--help` documents
the option. Directory and embedded-archive images mount with identical bytes,
permissions, aliases, ownership, inode values and mtimes. This is native
**mount parity**, not native Bash/Coreutils runtime parity: the broader native
host-import/continuation driver remains Stage 6. Both direct-tree and packaged
browser command matrices execute the actual installed guest binaries.

The four auxiliary builders now support explicit `--install` operations.
`start.sh` installs auxiliaries and regenerated launch snapshots through the
inventory tool; Coreutils has an explicit `coreutils-install` target. Compilation
still writes only to root `build/`, and HTML construction does not compile guest
programs. The Bash generator accepts `--vfs-root` rather than a flat binary
directory. The old seventeen frontend-staged binaries were retired only after
parity checks; recoverable originals are under
`build/engine/refactor-stage3/legacy-binaries-EBSLGB` and in Git history.

Verification:

- Native warnings-as-errors ASan/UBSan image tests check all 57 nodes' exact
  bytes/metadata, independent mutable mounts, truncated/oversized records and
  malformed-image rejection before mutation. The CLI and compiled browser
  artifact pass the same eight mounted-path assertions; the browser repeats
  them in fresh stores.
- Directory/archive equivalence, omission/collision/escape guards, partial
  batch protection, reviewed-import rejection, edited snapshot protection,
  successful atomic refresh, and checkout-independent metadata tests pass.
- All eleven archive-backed Bash scenarios pass: aggregate utilities,
  readline, heredoc, clock/file/directory mtimes, `ldd`, two Rogue children,
  alternate-screen/cursor restoration, subsequent input and exit. The command
  matrix also passes directly from the installed tree.
- Installed upload/download utilities preserve binary bytes, including NUL
  and high bytes, through a worker-adapter roundtrip. Upload cancellation leaves
  no file; a subsequent download fails as expected, and Bash still accepts
  input and exits. This simulates picker responses; it does not claim manual
  browser file-picker or cross-browser UI acceptance.
- Actual headless Chromium verifies both pages via `file://`: archive
  decompression, DOM, WebGL initialization, worker execution, shell input/output,
  and no external runtime requests. Terminal model/GLF and frontend package
  checks pass. Screenshots remain under `build/html-rt/offline-browser-check`.
- Native core remains 96/97 files and 20,063/20,066 assertions. Full dashboard
  remains 267/280 files; both its pass/fail log and failure diagnostics are
  byte-identical to Stage 2. Existing import and ten libc fixture failures are
  not fixed or hidden. Measurements/logs are under
  `build/engine/refactor-stage3`; no suite-speedup claim is made.
- Shell/Python/JS syntax and whitespace checks pass. Oracle sources and patch
  transactions, guest ABI semantics, headers and test-corpus placement are
  unchanged.

Focused reproduction:

```sh
make -C src/cli-rt BUILD_DIR=../../build/cli-rt vfs-image
node tests/c-engine-vfs-packaging.cjs
node tests/c-engine-vfs-browser.cjs
node tests/c-engine-vfs-transfer.cjs
node tests/c-engine-frontend-packaging.cjs
node tests/c-engine-offline-browser.cjs
```

At Stage 3 acceptance the image reserved at most 96 installed nodes within the
kernel's 128-node table and limited images to 64 MiB. Stage 4 expands the bounded
node limits as recorded below; Stage 5 must reassess capacity for the full
corpus. Audits reject excess entries rather than omitting them. `test.html`
remains until the later replacement gates pass.

- Create the VFS inventory and install/sync tool. Reject conflicting
  destinations, escaping paths, missing mandatory binaries, and partial
  compiler results. Validate Wasm imports and required exports before install.
- Place shell launchers and utility aliases under `/bin`, installed application
  binaries under `/usr/bin`, and shared libraries under `/lib`. Preserve
  compatible `/usr/lib` resolution until consumers have migrated.
- Update `build-rogue.py`, `build-ncurses.py`, `build-ldd.py`,
  `build-upload-download.py`, Coreutils staging, and `start.sh` auxiliary
  installation to target the inventory rather than `src/bash/*.wasm`.
- Change `build.sh` and the Bash generator to package the directory tree with
  explicit roles for host bootstrap artifacts versus mounted guest files.
- Mount archive members at their declared absolute guest paths. Remove the
  old flat-file auto-discovery and duplicate host-to-guest path tables.
- Provide the same mount/metadata contract to `cli-rt`; test directory/archive
  inventory equivalence before retiring old native or browser staging paths.
- Preserve directory metadata, executable permissions, binary bytes, mtimes,
  library aliases, licensing files, and startup environment.
- Verify upload/download round trips and cancellation; verify `ldd`, two
  consecutive Rogue runs, terminal restoration, and subsequent Bash input.

Gate: commands run under the same guest paths from both an installed VFS tree
and the packaged offline page; package audits reject omissions and collisions.
For this stage native acceptance establishes identical mounted paths, bytes and
metadata; native execution of the full guest application matrix is Stage 6.

## Stage 4: Install a guest compiler-ready header tree

Status: complete (2026-10-01). Header layout, hermetic build wiring and the
provider/signature/capability gate pass for the documented partial guest ABI.
This does not claim full ISO C/POSIX conformance or an implemented guest compiler.

### Implemented header-layout and build pass

- Authored guest public headers now live in `src/vfs/usr/include`. Engine,
  CLI, browser API/configuration headers, and libc `helper.h` remain private.
  Coreutils/gnulib/SELinux shims moved to a separate named package profile.
- Extracted fixed-width wire layouts into `waste/abi/posix.h`; public termios,
  stat and ioctl no longer include private libc helpers. Normal `sys/ioctl.h`
  and `sys/mman.h` exist, with the old ioctl spelling as a wrapper. Mapping
  explicitly uses the existing i64 offset, not legacy 32-bit libc `off_t`.
- Corrected public `struct stat` from 112 to the actual 128-byte adapter write
  size; made guest `FILE` opaque. Consolidated `inttypes.h` onto the canonical
  wasm32 `stdint.h`, eliminating host `include_next` dependencies.
- Installed the explicit Clang 22.1.8 fundamental-header dependency closure,
  including the checked-arithmetic/count-of headers required by Coreutils,
  under `/usr/lib/waste/cc/include`; installed its license and file provenance.
- Installed configured ncurses public headers tied to its revision,
  configuration hash and DSO hash. The public wrapper preserves application
  C11 `bool`; raw ncurses definitions retain their four-byte unsigned ABI.
  Rogue intentionally retains its explicit legacy Boolean profile.
- Added `guest_sdk.py`, `build-guest-sdk.py`, `guest-sdk-install` and
  `guest-sdk-check`. Header/DSO refreshes publish an audited complete staged
  tree atomically, preserving existing inode IDs and refusing edited upstream
  snapshots or unrelated binary changes. `/usr/share/waste/sdk.json` records
  target assumptions, origins, hashes and verified partial-profile status.
- Sysroot, libc, generated helper/application templates and auxiliary builders
  consume the mounted public/compiler tree with `-nostdinc` and no host resource
  include fallback. The default wrapper no longer force-includes gnulib shims;
  Coreutils uses its own wrapper/profile. Make dependencies include SDK inputs
  and import-audit changes; corresponding-source bundles include SDK inputs
  and compiler licensing. Auxiliary builders refresh unchecked old sysroots.
- Ncurses now refuses to link/publish after any object compilation failure.
  Its implementation consumes `stdbool.h` before its private raw curses ABI.
  Corrected the import audit's false Asyncify match on ordinary libc `rewind`.
  Rebuilt Coreutils retains libc's existing versioned dlopen/dlsym/dlclose
  wrappers; those three already implemented imports received an explicit,
  recorded install-contract review, not new engine capability implementation.
- Increased the bounded kernel table to 256 nodes and image limit to 224,
  retaining 32 slots for runtime-created files and the 64 MiB image limit.
  The installed tree contains 155 nodes, including 88 SDK distribution files.
  Stage 5 must increase/review capacity again for the complete test corpus.

### Provider, signature and capability completion

- Classified the original 171 unresolved names. Nineteen gnulib-only functions
  and locale/UTF-32 helper types moved into the named Coreutils profile; default
  `locale_t` is opaque. Remaining unprovided functions and six package globals
  reject calls/address-taking through explicit unavailable attributes.
- Adapted configured ncurses declarations, including macro-generated SP APIs,
  to reject missing optional/debug functions. Recorded upstream and adapted
  hashes; no DSO ABI or upstream source was changed by those annotations.
- Audited browser binding precedence, not just symbol names. Default `lseek`
  rejects the Bash i64 override of the legacy i32 declaration; `__fpurge`
  rejects its incompatible i32-returning override. `abort` rejects the returning
  browser stub. Default `assert` uses a terminating compiler trap, without
  diagnostics/SIGABRT. Existing Coreutils/Rogue package exceptions are explicit;
  their inherited bindings are not advertised as default guest capabilities.
- The strict audit accounts for 740 available functions (21 header definitions,
  256 libc exports, 429 ncurses exports and 34 reviewed browser bindings), 155
  unavailable functions and 31 globals. Compiler-generated reference imports
  must match actual Wasm provider signatures; ncurses imports also must match.
  Browser-only signatures/precedence are manually reviewed and source-hash-pinned.
- Mounted `sdk.json` now includes declaration types/headers, providers/signatures,
  unavailable reasons, capability limits, known no-op/error stubs and compiled
  provider/implementation hashes. Availability is not semantic conformance:
  fixed locale, partial stdio, unsupported networking, no-op environment/mutex/
  sleep/signal-set behavior and other limits remain documented. Data addresses
  require real link relocations and stream pointers require CRT startup.
- SDK preparation runs this gate before atomic publication; `guest-sdk-check`
  includes `audit-guest-providers.py --strict` and rejects stale mounted metadata.
  Make dependencies cover the policy and signature inspector. No unrelated
  engine/POSIX capability was implemented to satisfy inherited declarations.

### Verification

Implementation details and commands are in `docs/guest-sdk.md`. Evidence is
under `build/engine/refactor-stage4/` and completion evidence under
`build/engine/refactor-stage4-providers/`. The previous sysroot is retained at
`refactor-stage4/old-sysroot`, and Stage 1–3 measurements are untouched.

- Standalone preprocessing passes for 56 public headers. Six reversed/forward
  include orders pass with warnings as errors, including curses/string/bool.
  Dependency traces stay entirely inside the installed SDK; attempts to use
  host Linux, private implementation or package-only headers fail.
- ABI size/alignment/offset checks pass for stat, directory entries, termios,
  times, signals and descriptor sets. Variadic execution passes in real Wasm
  and the compiled C browser engine. The latter also passes stat-buffer
  canary and assertion-trap probes compiled only against the installed tree.
  Every unavailable function/global rejects address-taking. Added negative
  cases for missing providers, incompatible Wasm signatures, stale browser
  binding reviews and malformed signature inputs.
- Native ASan/UBSan mounting verifies every installed file's bytes/metadata,
  isolation and malformed-input bounds. The shared 8 path assertions and 12
  new SDK presence/private-exclusion assertions pass natively and through the
  compiled browser artifact. SDK provenance, unlisted-header and mismatched
  DSO negative cases reject; failed installation leaves the inventory intact.
- Ncurses (all 142 selected objects), Rogue (34), libc fixtures, ldd,
  upload/download and all eleven Coreutils utilities rebuild with the new
  tree. No guest compile depends on host system headers. Existing kernel
  adapters and application behavior remain the runtime, not host substitutes.
- All eleven Bash scenario/clock/metadata checks pass against the final installed
  package, including two Rogue runs, terminal restoration and later input.
  Binary upload/download roundtrip/cancellation, directory/archive equivalence,
  package rejection guards, frontend/terminal-model/GLF checks pass. Actual
  Chromium `file://` boot passes for both pages with no external requests.
  Static shell/Python/JavaScript checks and `git diff --check` pass.
- Native core retains the Stage 3 result, 96/97 files. The full browser run
  retains 267/280 and the same 13 known failures (three imports suites and ten
  libc fixtures with unknown module IDs). These are pre-existing baselines,
  not evidence that the full conformance gates pass.

At Stage 4 acceptance, test-corpus installation and capacity review were next;
Stage 5 completion is recorded below. Native guest application execution remains
Stage 6, not a claim of this SDK stage. Keep `test.html` until execution,
reporting, isolation and differential coverage have replacement gates.

Use the selection and consolidation rules in **Guest C development headers**.

- Inventory each header's public/private role, dependencies, function providers,
  and ABI assumptions; give every installed header a manifest entry.
- Split `helper.h` dependencies, reconcile guest declarations, and move the
  selected public application headers to `vfs/usr/include`. Preserve private
  engine/native/browser headers beside their implementations.
- Add the missing compiler-support header closure and normal public ioctl and
  mapping interfaces; install ncurses development headers matching its DSO.
- Update sysroot generation to consume the same public SDK and compiler
  support tree mounted in the browser. Update libc/application builds, generated
  C templates, include roots, and Makefile dependency rules accordingly.
- Keep native/engine builds on their existing private headers. Change their
  Makefiles only where public ABI extraction creates a real shared dependency;
  do not add guest standard include directories to native header search paths.
- Keep upstream sources and generated parser/config headers in their current
  ownership locations. Install selected upstream public headers through the
  provenance-aware SDK process, recording versions and licenses.
- Add hermetic preprocessing/include-order checks for standard/POSIX headers,
  ncurses, and guest extensions. Exercise guest ABI size/alignment/offset probes,
  variadic calls, and basic file/terminal/dynamic-library operations.
- Compare existing Clang/sysroot builds with builds using only the installed
  header closure. Rebuild Coreutils, ncurses, Rogue, and guest libc clients.
- Verify incremental rebuilds and header capability metadata. Missing public
  providers, accidental private includes, and host-only declarations fail the
  SDK audit rather than becoming future parser surprises.

Gate: public mounted headers and compiler support are a complete reproducible
guest SDK for the documented target profile. Existing native sanitizer and
browser/application gates remain green, and no guest compile relies on host
system headers. This does not require implementing the future C parser.

## Stage 5: Install the complete test corpus under `/tests`

Status: complete

Installed all 284 previous dashboard inputs under stable, group-qualified
`src/vfs/tests` paths. The 14 groups retain 280 supported entries and four
explicitly unsupported legacy entries. `test_corpus.py` now supplies the common
selection/grouping rules to both dashboard generators and the distribution
installer; execution semantics and dashboard frontend remain unchanged.

Implementation:

- `/tests/manifest.json` preserves every source hash, original execution
  specification, known parsed assertion counts, expected-failure/skip policy,
  group-derived feature labels and runtime profile. It records the pinned spec
  revision and oracle patch scope. Host source paths are provenance, not a
  future execution fallback.
- Official sources remain in the spec submodule, authored probes in top-level
  `tests/`, and generated libc fixtures under `build/html-rt/waste-libc/tests`.
  Snapshot bytes match the retained baseline; no upstream test source changed.
  Fourteen stale libc staging `sourceBytes` values were corrected explicitly in
  `baselineRepairs`; their retained source hashes and execution policy matched.
- `/tests/.support` preserves five assembled Wasm modules for the four existing
  browser-native DIY cases, two mmap file assets, and fourteen libc client
  includes. Host-only harness/build inputs remain in their source locations
  with provenance hashes. The upstream Apache-2.0 test license is installed at
  `/usr/share/licenses/wasm-spec-tests/LICENSE`.
- Backend distinctions remain visible: four DIY cases use browser-native Wasm
  compatibility imports, not native CLI/C-engine equivalents; mmap uses the C
  engine. OCaml direct/threaded variants, threaded dashboard default, quantum
  and timeout are preserved. The C dashboard's absent per-test deadline is
  recorded explicitly, not replaced with an invented timeout.
- `vfs-tests-install` prepares fresh metadata under `build/engine` and atomically
  publishes the test subtree/license. `vfs-tests-check` verifies source hashes,
  identities, companions, selection and provenance. Edited snapshots, malformed
  staged inputs and unreviewed selection changes cannot replace the installed
  tree; explicit `--review-selection` is required after reviewing a selection
  change. Component installs now preserve corpus inventory metadata.
- Capacity is reviewed at 1,024 kernel nodes and 960 image nodes, retaining 64
  runtime slots beyond a maximum image in the installed namespace. The 64 MiB
  image byte limit remains. The installed tree has 489 nodes; its WVFS image is
  26,566,300 bytes. Maximum-size decode records use approximately 300 KiB of the
  browser engine's existing 8 MiB stack. Capacity is bounded and overflow
  rejected, not silently truncated.

Verification:

- Native warnings-as-errors ASan/UBSan image checks compare all 489 nodes'
  complete bytes/metadata, isolated mutable mounts and malformed rejection.
  A synthetic 960-node image plus 64 runtime creations fills the 1,024-node
  table; the next creation and a 961-entry image reject. Executor smoke and
  the 60-check native pathname gate pass.
- Bash guest redirection opens all 284 mounted WAST paths. The actual compiled
  browser engine reads every file through EOF, verifies its length and endpoint
  bytes, and closes it; existing eight path and twelve SDK assertions repeat in
  fresh stores. Complete-content equivalence is checked natively and by the
  directory/archive audits, not inferred from endpoint checks alone.
- Corpus tests verify common C/OCaml identities and policy, exact sources/assets,
  duplicate/missing/stale metadata rejection, edited snapshot protection,
  fail-before-publish behavior, unchanged atomic refresh, and component-refresh
  metadata preservation. HTML packaging remains read-only toward `src/vfs`.
- All eleven archive-backed Bash scenarios pass, including utilities,
  readline/heredoc, clock/metadata, dynamic loading and two Rogue children.
  Binary upload/download/cancellation, frontend/terminal-model/GLF, package
  guards and the strict SDK provider gate pass. Actual Chromium boots both
  pages via `file://` without external requests.
- Native core remains 96/97 files and 20,063/20,066 assertions. The full browser
  dashboard remains 267/280 files with the same thirteen known failures (three
  import suites and ten libc fixtures). These conformance failures remain
  visible; this stage does not fix or hide them.
- An exploratory Bash `IFS= read -r first < /tests/core/address.wast` probe
  exposed an indirect-call type mismatch. Redirection itself opens every input,
  and compiled-engine file reads pass. This builtin-specific issue is not fixed
  by distribution installation; retain it for Stage 6 runtime-parity diagnosis.
  Evidence is in `build/engine/refactor-stage5/bash-read-diagnostic.log`.
- Shell/Python/JavaScript syntax checks and `git diff --check` pass. Logs,
  retained baseline, corpus checks and scenario measurements are under
  `build/engine/refactor-stage5`; reproduction is documented in
  `docs/test-corpus.md`.

Stage 5 handed off to Stage 6A, whose native session and application-parity
gate is now complete. Current work is Stage 6B batch-runner parity; see its
latest slice and the Node-independent testing direction below.
Do not remove `test.html` or its build options yet.

- Reuse the test selection/grouping logic in `generate-c-engine-tests.py` and
  `generate-browser-tests.py` to enumerate the full previous dashboard corpus.
- Install every selected `.wast` under `vfs/tests` with stable group-qualified
  paths. Preserve supporting binaries, included files, expected outcomes,
  runtime selection, timeouts, skips, and feature requirements in the manifest.
- Official upstream test files remain maintained in the pinned spec submodule;
  repository regression sources remain in top-level `tests/`. VFS copies are
  distribution snapshots with refresh/provenance checks, not independently
  edited upstream forks.
- Generate libc fixtures under `build/html-rt/waste-libc/tests` first, then
  explicitly install their packaged snapshots. Keep `.wast.inc` clients and
  required DIY POSIX support inputs in their existing source locations.
- Include currently configured core, proposal, annotation, DIY POSIX, and
  libc groups. Account for existing direct/threaded OCaml POSIX coverage when
  migrating useful assertions to C; no OCaml kernel parity or expansion is
  required. Do not classify unsupported C execution as an equivalent pass.
- Audit installed inventory against the baseline: no missing or duplicated
  suites, name collisions, broken companion paths, or silent corpus changes.

Gate: every previous dashboard input has a mounted `/tests/...` identity and
can be opened from Bash; manifest counts and source hashes match the baseline.

## Stage 6: Expand native execution and consolidate test runners

Status: Stage 6A complete; Stage 6B.1–6B.15 and 6B.17–6B.20 landed; 6B.16 has
focused verification only. Stage 6B acceptance and 6C remain pending.

Merely mounting WAST files does not make `test.html` redundant. Its harness
currently supplies execution policy, suite grouping, result reporting, and
runtime selection. Preserve those capabilities before removing it.

### Stage 6A: Native guest-runtime parity

Status: complete — all five acceptance bullets below have landing slices
and verification evidence; the Stage 6A.2 full-matrix sweep and the
Stage 6A full browser-corpus baseline (recorded in the slices at the
end of this stage) close the gate.

#### Stage 6A.1: Shared guest adapter and native session foundation

Status: complete

- Extracted portable guest import/ABI decoding, errno handling, kernel
  operations and control-import classification from the browser adapter into
  `src/engine/guest_posix.{c,h}`. Both runtime Makefiles compile it. Borrowed
  immutable capability tables and per-store contexts keep native syscalls,
  browser imports, output and tracing outside the engine. Browser upload/
  download dialogs and wall-clock conversion remain in `html-rt`.
- Added `src/cli-rt/guest_session.c` and `guest-session` /
  `guest-session-sanitize` Make targets, producing `build/cli-rt/waste-session`
  and its sanitizer companion. They use system libc only for runtime polling,
  clocks and transcript output; the freestanding `waste-cli` path and its
  conformance result format remain unchanged.
- The companion mounts the same validated WVFS bytes, loads mounted
  `/usr/share/waste/launch.wast` by default, and runs the actual guest libc
  and Bash. READ/SELECT yields resume saved evaluator state after delayed
  host input or a kernel SELECT deadline. Initial cwd/dimensions, explicit
  script staging, EOF, wait timeout, assertion counts, input/wait counts and
  guest exit status are supported. Guest paths have no host-filesystem fallback.
  Result files are exclusively created, never truncated.
- Added shared interaction contracts in `tests/guest-session-{io,bash}.json`,
  a guest ABI fixture and native/browser boundary drivers. Exact output and
  assertion counts agree for fragmented commands, later prompts, mounted
  file input, NUL/`0xff`, EOF, file round-trip and errno. Native exit status
  is checked independently because the current browser C exports do not
  expose it. This small browser driver is an artifact boundary probe, not a
  replacement for production worker, DOM, file-dialog or rendering tests.
- Reproduced the Stage 5 `read` trap natively with the same function/table/type
  diagnostic. Table slot 145 refers to Bash's `pop_scope`: its void callback
  was being invoked by an int-returning unwind-protect dispatcher. The launch
  builder now applies the existing typed-cleanup-adapter scheme to that slot.
  The shared regression verifies successful file input and a subsequent prompt;
  no engine type check was relaxed. Refreshed the installed launch snapshot.
- Preserved the strict SDK signature/source-review gate across the extraction:
  it now checks the shared adapter, private capability header and browser
  wrapper. Added a stale-secondary-source negative, refreshed SDK provenance
  and corpus metadata, and kept the historical provider labels for compatibility.
  The installed dashboard selection remains 284 inputs in 14 groups.
- Native session ASan/UBSan/leak checks pass, including SELECT deadline,
  timeout, explicit unsupported FORK and result-file collision negatives.
  Existing i32, path-VFS (60 checks), continuation (43 checks) and process
  lifecycle (114 checks) sanitizer gates pass. The lifecycle gate now links
  the real store/library cleanup implementation instead of an incomplete
  engine source subset. Bounded assertion diagnostics also compile cleanly
  with the sanitizer target's warnings-as-errors settings.
- Native core remains 96/97 files; the full browser corpus remains 267/280
  with the same thirteen known failures. These failures are still visible,
  not counted as new passes. All eleven packaged Bash worker scenarios pass,
  including two Rogue children; binary transfer/cancellation and terminal
  model/GLF checks pass. Actual Chromium boots both rebuilt pages via `file://`
  without external requests. The compiled browser probe reads all 284 corpus
  inputs and passes the installed-only SDK ABI checks. Direct-tree and archive
  WVFS bytes are identical; both pass the shared session regressions, including
  the native default mounted bootstrap. Reproduction and explicit limits are in
  `docs/native-guest-session.md`; evidence is under
  `build/engine/refactor-stage6`.

#### Stage 6A.2: Shared process driver and full application parity

Status: in progress; shared process-driver/application, WAST-handler, bounded execution-policy and scripted terminal-control slices complete and verified

Implemented in this slice:

- Extracted the browser's existing fork/exec/child-selection and parent/provider
  continuation-restoration policy into `src/engine/process_driver.{c,h}`.
  Both Makefiles compile it; browser and native sessions use the same driver.
  Mutable scheduling state belongs to a per-session record. Native polling,
  browser event delivery, platform imports and WAST command-stream adapters
  stay outside the driver. The kernel/store still owns wait/reaping and memory.
  This is the existing bounded child-first policy, not a concurrent scheduler;
  nested child-first fork remains explicitly unsupported.
- Native Bash now executes actual installed Coreutils, pipelines/redirection,
  short-path external/builtin/stdout heredocs, dynamic ncurses loading and two
  Rogue children. Failed command/exec attempts resume Bash with shell statuses
  127/126; later successful commands and fragmented input remain usable.
- Added shared `guest-session-{matrix,pipeline,heredoc,exec-fail,rogue}.json`
  contracts alongside the foundation probes. The native driver checks real
  READ/SELECT waits before sending delayed input; `--trace-waits` reports PID
  and wait kind without mixing diagnostics into the guest transcript. Rogue
  checks both child PIDs, arrow input, a guaranteed game turn, quit/continue,
  cursor restoration, later fragmented input and nonzero shell exit status.
- `guest-session-worker.cjs` uses the same contracts with the packaged
  production worker. Native/export/worker probes compare exact deterministic
  transcripts, assertion counts and guest exit codes. Rogue's documented
  time-seeded map/redisplay differences use semantic markers and process/wait
  boundaries; raw transcripts remain evidence. Worker comparisons verify exact
  WVFS bytes and canonical inventory metadata, including modes, mtimes and
  provenance—not just file-open success.
- Browser `waste_wast_guest_exited`/`waste_wast_guest_exit_status` preserve
  explicit guest exit results through cleanup and reset on the next script.
  The worker publishes them in `done`; traps are not reported as guest exits.
- LeakSanitizer exposed missing ownership of fork root-engine clones across
  exec replacement. Capsules now retain `owned_fork_engine` until reaping or
  teardown, independently of the current image. Lifecycle tests no longer
  free those clones in the harness. The shared session gate checks normal,
  failed-exec and trapped-parent cleanup with ASan/UBSan/leak detection.
  The two-Rogue leak gate also exposed unowned loader-created GOT globals;
  their storage now has the same store lifetime as linked-call bindings,
  including import-resolution/instantiation failure cleanup.
- Retained a newly exposed **known failure**, not an acceptance pass:
  `/bin/cat > /tmp/session-heredoc.txt <<EOF` with `Hello shared heredoc!`
  traps in guest `env.sh_free` (6/7 invocation/startup results). It reproduces
  in the pre-extraction packaged browser engine as well as both current
  runtimes. `guest-session-heredoc-known-failure.json` preserves the exact
  failing command and diagnostic. Short-path heredocs pass. Guest lifetime
  corruption still needs diagnosis before full heredoc acceptance.

Verification for this slice:

- All seven shared native/browser-export scenarios pass; the six interactive
  Bash contracts also pass through the packaged production worker. Guest exit
  status, deterministic output, selected Rogue child PIDs and package metadata
  agree. The native matrix also passes with ASan/UBSan and leak detection
  enabled, including failed-exec and trapped-parent cleanup. The long-path
  heredoc remains separately labeled **KNOWN FAILURE** (6/7), not a passed suite.
- Native process lifecycle (115 checks) and exec-transition matrix (58 checks)
  pass with warnings-as-errors and ASan/UBSan. Native core remains 96/97 files;
  the full packaged browser corpus remains 267/280, with the same thirteen
  known failures, not new passes or suppressed failures.
- All eleven existing packaged Bash worker probes pass, including valid WAT,
  repeated WAST handlers and two Rogue children. Fixed the legacy builtin/
  stdout heredoc probe's false failure: it now recognizes Bash's optional exact
  bracketed-paste-disable sequence before output. Shared parity checks still
  compare unmodified deterministic transcripts.
- Installed binary transfer/cancellation, terminal model and GLF checks pass.
  Actual Chromium boots both final pages through `file://` with no external
  runtime requests. The mounted-only compiled SDK probe passes and all 284
  corpus inputs read through EOF with exact lengths/endpoint bytes.
- Refreshed the corresponding-source bundle and explicitly reinstalled its
  Coreutils distribution metadata, then rebuilt the WVFS image and both pages.
  The inventory remains 489 nodes; shared worker checks verify identical image
  bytes and canonical inventory metadata. Syntax/bytecode checks and
  `git diff --check` pass.

WAST-handler continuation slice (2026-10-01):

- Added `src/engine/wast/handler.{c,h}` and composed it into both runtimes.
  The native companion now commits WAST process handlers through the same
  fork/exec/reaping driver as the browser; there is no copied browser API or
  second POSIX implementation. The shared handler covers module definitions/
  instances, registration, module assertions and invocation assertions.
- READ/SELECT pauses retain a value-copy of the entire pending assertion and
  its selected engine, independently of the enclosing Bash continuation.
  Completion rechecks expected values/traps once. The previous browser handler
  discarded expectations when its scanner freed the command, then counted
  resumed invocation completion as an unconditional `main` success. Both
  runtimes now preserve assertion-aware completion in child WAST handlers.
- Failed child assertions are visible results and produce child exit status 1;
  Bash can resume, run a successful command and exit with its own status.
  Native JSON separates root errors from `handlerFailures`/`handlerError`.
  The worker keeps its existing aggregate failed-result reporting, so `ok` is
  false for the intentionally failing contract even though Bash recovered.
  Unasserted invalid setup modules in executable handlers now fail visibly;
  the legacy dashboard's setup-command policy is unchanged.
- Handler command modules/failed-start orphans and their parsed metadata are
  released before child provider-clone reaping. They do not enter a later
  fork's module namespace. Handler waits deliberately publish no ordinary
  engine-entry descriptor; resume no longer binds Bash's engine to that child.
- Added explicit bounded native `--stage-file GUEST_PATH OCTAL_MODE HOST_FILE`
  input, matching the existing browser file staging capability. The same JSON
  contract drives identical staged bytes/modes across native, browser exports
  and the production worker. No guest operation falls back to host paths and
  executable fixtures retain normal permission checks.
- `guest-session-handlers.json` covers valid WAT, direct/shebang execution,
  repeated WAST, definitions/registration/module assertions, multiple READ
  pauses in one assertion, a resumed expected trap, SELECT readiness, a
  resumed result mismatch and parent recovery. Expected score: 22/23 with
  exactly one intentional negative check, mismatch child status 1, eight
  forked/reaped children and final Bash status 3. Tests save/restore terminal
  state through the kernel ABI; private-memory fixtures do not pass pointers
  into libc's different imported memory. The non-POSIX command fixture also
  passes the OCaml oracle.
- This remains bounded child-first execution, not a full batch runner.
  Nested process transitions/host-I/O from WAST invocations and yielding module
  starts fail explicitly with child status 126; a paused start cannot satisfy
  a module assertion.  Runnable deadlines and native host I/O have since
  been closed by the dedicated slices below.
  `guest-session-handler-start.json` verifies the explicit unsupported-start
  diagnostic, child status 126 and parent recovery (7/8, one intended negative
  result); it is not a passed `assert_invalid` or start-resume parity claim.
- The start guard exposed a loader status bug: when instantiation discarded
  a yielded partial engine, cleanup could return a still-zero `error.status`
  as `EXEC_OK`. The shared loader now preserves the returned instantiation
  status before cleanup. A yielding valid module therefore cannot become an
  apparent successful instance or satisfy the wrong module assertion.

Verification for the WAST-handler slice:

- All nine shared contracts meet their expected native/browser-export results;
  the eight Bash contracts also agree through the final packaged worker on
  deterministic output, explicit guest exit status, selected wait/PID boundaries
  and identical WVFS/canonical inventory hashes. The two negative contracts
  intentionally retain their failed assertions (22/23 and 7/8). Worker failure
  diagnostics are validated and retained separately from the unmodified guest
  transcript; they are not hidden by marking those assertions as passing.
- The full native shared-session gate passes with warnings-as-errors,
  ASan/UBSan and leak detection, including both negative handlers, normal
  teardown, failed exec and the separately labeled long-path heredoc trap.
  Native process lifecycle (115 checks) and exec-transition matrix (58) pass.
  The non-POSIX handler fixture passes the native OCaml oracle.
- Native core remains 96/97 (the known imports suite); the final packaged
  browser corpus remains 267/280 with the same thirteen known failures.
  The legacy repeated-WAST worker probe passes. Terminal model/GLF,
  installed binary transfer/cancellation and frontend packaging guards pass.
- Both final pages boot in actual Chromium via `file://` without external
  runtime requests. Mounted SDK ABI probes and exact-length/EOF reads of all
  284 corpus inputs pass. Refreshed and explicitly reinstalled corresponding
  source metadata, rebuilt the audited WVFS image and both pages; the inventory
  remains 489 nodes. Shell/Node syntax, Python bytecode and diff checks pass.

Next slice / remaining Stage 6A.2 work:

- The long-path heredoc corruption is fixed; see the isolation slice below.
- Runnable deadlines and scheduled cancellation are implemented in the slice
  below. Immediate external cancellation during a running worker is closed
  by the cooperative dispatch-pump slice below: the engine's dispatch-loop
  safepoint can now yield at a configurable wall-clock quantum
  (`EXEC_YIELD_PUMP`), the worker drains its event loop between resumes, and
  a new `cancel` message calls `waste_wast_request_cancel` so the next
  safepoint returns `EXEC_ERROR_INTERRUPTED` with `EXEC_STOP_CANCELLED`. The
  dedicated worker pump+cancel fixture
  (`tests/guest-session-pump-cancel.{wast,cjs}`) exercises this through the
  production `src/html-rt/src/worker.js`; existing synchronous safepoints
  are unchanged when `pumpQuantumMs` is absent.
- Scripted resize/signals are implemented and verified in the terminal-control
  slice below. Deterministic guest test clocks are implemented and verified in
  the clock slice below. Native upload/download completion/cancellation is
  implemented and verified in the host-io slice below. Arbitrary-PID signal
  routing is implemented and verified in the signal-pid slice below;
  by-PID routing to a backgrounded child while the parent is
  bounded-child-first-blocked is implemented and verified in the
  signal-pid-backgrounded slice below; single-member process-group fan-out
  is implemented and verified in the
  signal-pgid slice below, multi-member fan-out across a forked group is
  implemented and verified in the signal-pgid-fork slice below, and
  fan-out that targets a backgrounded group while the active parent is
  bounded-child-first-blocked is implemented and verified in the
  signal-pgid-backgrounded slice below. Unavailable transitions remain
  visible and cannot be counted as parity passes.

Linked-provider fork isolation / heredoc slice (2026-10-01):

- Traced the original long-path heredoc at the guest heap boundary. Its saved
  command pointer (`0xa2320`) had a valid allocated header at fork. Child Bash
  then freed it through the canonical parent libc; a later parent process
  record reused the block and overwrote that live string's header. Parent
  cleanup eventually interpreted body bytes as a free-list pointer
  (`0x65480000`) and trapped in `env.sh_free`. This was shared process/provider
  isolation corruption, not a terminal glyph or allocator-size bug.
- Linked-call trampolines now use the caller's clone graph when selecting a
  provider. Fork import rebinding only selects the defining memory/table/global
  owner, not an importer whose alias still points at the parent. The cached
  default-memory pointer follows the rebound import. Importer cloning also
  defers child access-check binding until its private graph exists, so it
  cannot mutate the borrowed parent's memory-access context.
- Added an independent three-module regression for transitive calls, mutable
  globals, table callbacks and process-private memory. Three sequential forks
  each mutate the child's provider and reap it while preserving parent state.
  Exact provider host output additionally checks the default-memory fast path.
  A native lifecycle regression covers borrowed memory-access ownership.
- Replaced the expected-trap heredoc contract with a positive contract retaining
  the exact original command. It verifies actual file output/status, repeated
  quoted bodies, closed-descriptor redirection failure/recovery, independently
  delayed heredoc lines and subsequent builtin input. The short-path contract
  remains; deterministic transcripts are not normalized.
- A separate diagnostic redirection through a regular-file path component
  unexpectedly succeeded instead of returning `ENOTDIR`. This kernel
  path-traversal gap is closed by the path-traversal slice below; it is not
  a passed failure-cleanup check for this slice.
  The heredoc failure-cleanup contract deliberately uses a closed descriptor.
- Retrying the full matrix exposed stale child libc activations across exec:
  bootstrap calls and preflight ncurses constructors resumed the abandoned
  old-image `execve` call. This broke `ldd` and also Rogue in a fresh session,
  rather than being solely an `ldd`-poisoned-session failure. The shared driver
  now snapshots/detaches old root/provider activations before loading,
  restores them on failed preflight/commit, and discards them (including stale
  jump environments) after successful replacement. Parent providers are not
  reset. Clearing state only after commit was insufficient because dependency
  constructors run during preflight.
- Added a fresh-session Rogue interaction alongside the retained `ldd` plus
  two-Rogue sequence. Keep failed-exec recovery and the heredoc/provider
  isolation contracts in the same native/browser matrix. Diagnostic details
  remain in local logs; browser assertion failures now include transition
  evidence to distinguish loading, bootstrap and guest execution failures.
- Verification: all eleven shared interaction contracts pass with native
  ASan/UBSan/LeakSanitizer, compiled browser exports, and the packaged worker
  (the native-only I/O fixture uses browser exports). Intentional handler
  negatives retain their expected failures/statuses; they are not relabeled as
  successful assertions. The independent linked-provider fixture passes 3/3,
  and native deadline/isolation/reaping/result-file boundaries pass. Native
  lifecycle and exec-transition unit gates pass 117 and 58 checks respectively.
- Refreshed corresponding-source metadata, explicitly reinstalled it into the
  VFS, and rebuilt the image plus both offline pages. Packaging/transfer,
  terminal model/GLF, real Chromium `file://`/no-external-request checks,
  mounted SDK ABI checks and exact-length/EOF reads of all 284 corpus files
  pass. Native core remains 96/97 (`imports.wast`), and the browser corpus
  remains 267/280 with the same thirteen pre-existing failures: no new corpus
  failures. Evidence is in the `provider-*` logs under
  `build/engine/refactor-stage6a2-heredoc`. At that verification point, runnable
  deadlines/cancellation, resize/signals, deterministic clocks and host-I/O
  parity remained open. The bounded execution-policy slice follows.

Bounded runnable execution-policy slice (2026-10-01):

- Added a borrowed, per-session execution policy to the shared engine. Every
  provider, fork clone, loaded library, replacement image and WAST handler
  inherits it; module starts receive it before running. The allocation-free
  evaluator polls at most every 4,096 dispatched instructions, including
  constant-stack tail calls. No engine-owned platform clock, host syscall,
  Asyncify transform or native stack manipulation was added.
- Native `--timeout-ms` now covers interpreted runnable execution and blocked
  READ/SELECT waits. Added `--cancel-after-ms` for scheduled cancellation.
  Monotonic policy time and sticky stop state survive process changes. Timeout
  and cancellation exit the runtime with 124/125, report separate flags, and
  are not fabricated guest traps, invalidity, successful assertions or child
  exits. The shared process driver checks stop state before converting a guest
  failure into a child exit or failed-exec recovery.
- Added browser limit configuration/stop-reason exports and optional worker
  start-message `executionLimits`. Browser policy uses the existing clock
  (clamped against backward steps); worker timers wake blocked execution.
  Successful assertions before interruption remain successful. The default
  browser behavior is unchanged until limits are explicitly configured.
- Added eighteen native timeout/cancellation probes for runnable and blocked
  execution, transitive providers, fork, tail calls, starts, replacement WAT
  images and WAST-handler invocation/start assertions. Minimal guest launchers
  avoid redundant Bash startup while the existing full Bash contracts retain
  delayed input and application coverage. Browser export probes check twelve
  interrupts and fresh-store recovery in the same Wasm instance; four worker
  probes exercise runnable/blocked timeout/cancellation.
- Scope: limits are cooperative, not hard host deadlines. Parsing/decoding,
  a long individual instruction and blocking host callbacks are not preempted.
  This does not implement immediate worker-message cancellation, general
  concurrent scheduling or configurable deterministic guest clocks. Preserve
  the dashboard and keep the remaining Stage 6A.2 acceptance work open.
- Verification: the combined execution-control gate passes all eighteen native
  cases with ASan/UBSan/LeakSanitizer, all twelve browser-export cases with
  same-instance fresh-store recovery, and all four packaged-worker cases.
  All eleven existing shared interaction contracts plus independent provider
  isolation and native wait/result-file boundaries pass against the final
  native/browser/worker assets; intentional handler negatives remain failures
  with their expected statuses. Native i32 smoke, lifecycle (117 checks) and
  exec-transition (58 checks) gates pass.
- Refreshed the corresponding-source package and explicitly installed its
  metadata into the shared VFS, regenerated the native image and both offline
  pages. Packaging/transfer, terminal model/GLF, mounted SDK and all 284 corpus
  file-read probes, and real Chromium `file://`/no-external-request checks pass.
  Native core remains 96/97 and browser corpus 267/280 with the same known
  failures, not new regressions. Shell/Node syntax, Python bytecode and diff
  checks pass. Evidence: `build/engine/refactor-stage6a2-control`.

Scripted terminal-control slice (2026-10-02):

- Added native `--control-fd` as an explicit borrowed host capability, separate
  from terminal bytes and guest descriptors. READ/SELECT polling accepts fixed
  16-byte, little-endian `WSC1` version-1 resize/signal records. A bounded
  per-session buffer retains fragmented records; coalesced records preserve
  boundaries. Clean EOF disables only that channel. Invalid records/versions,
  truncated EOF and invalid descriptor arguments fail visibly, without a
  fabricated guest trap, passing assertion or child exit.
- Reused existing shared-kernel terminal/signal operations, applied to the
  selected waiting process. Changed dimensions generate SIGWINCH; unchanged
  resize and ignored signals do not falsely complete SELECT. A resize alone
  cannot complete READ. Native reports count applied resize/signal records
  separately from terminal input. No shared engine signal semantics changed.
- Hardened production-worker control messages before Wasm integer conversion;
  invalid values and pending-signal overflow now post `control-error`. Unified
  wakeups preserve events arriving before the IO resolver exists, including
  queued startup resize/signals. Pre-start resize coalescing and the sixteen
  pending-signal bound remain explicit, rather than claiming an unbounded queue.
- Expanded the shared JSON interaction contract/drivers with resize/signal
  events. The new nine-assertion `terminal-control` scenario checks actual guest
  handlers/EINTR, dimensions, unchanged resize, ignored signals, READ followed
  by exactly one input byte, child delivery and parent recovery. The native
  adapter deliberately receives fragmented controls; browser exports and the
  packaged worker use the same scenario and exact transcript. Host regression
  fixtures do not change the frozen installed corpus.
- Scope: controls require a genuine external wait/event-loop turn. This slice
  does not implement immediate runnable cancellation, process-group fan-out,
  deterministic guest clocks or native upload/download simulation; the
  deterministic clocks, host upload/download simulation and arbitrary-PID
  signal routing requirements are addressed in later slices below. Immediate
  cancellation and process-group fan-out keep Stage 6A.2 and the full Stage 6A
  gate open; retain `test.html` and its build options.
- Verification: all nine terminal-control assertions pass natively with
  ASan/UBSan/LeakSanitizer, through browser exports and through the rebuilt
  production worker. Native boundary checks reject 28 malformed/truncated
  records and seven invalid descriptor arguments; clean EOF, coalesced records
  and malformed child delivery pass their expected outcomes. Browser checks
  reject eight invalid export values and verify same-instance fresh-store
  recovery. Queued worker resize wakes SELECT without input; 35 invalid or
  overflow messages are rejected without changing guest state.
- All twelve shared interaction contracts, independent provider isolation and
  native wait/result-file boundaries pass against final native/export/worker
  assets, including both Rogue paths. The intentional handler negatives retain
  their expected failures. Execution-limit regression remains eighteen native,
  twelve browser and four worker passes. Native POSIX kernel/select, i32 smoke,
  process lifecycle (117 checks) and exec-transition (58 checks) gates pass.
- Refreshed and explicitly installed corresponding-source metadata, rebuilt
  the audited native WVFS image and both offline pages; inventory remains 489
  nodes. Frontend packaging, binary transfer/cancellation, terminal model/GLF,
  Bash runtime, mounted SDK ABI/all 284 corpus file-read probes, and actual
  Chromium `file://`/no-external-request checks pass. Browser corpus remains
  267/280 with the same thirteen known failures. No shared engine changes were
  made in this slice. Shell/Node syntax, Python bytecode and diff checks pass.
  Evidence: `build/engine/refactor-stage6a2-terminal`.

Deterministic guest clock slice (2026-10-02):

- Separated host-side session deadline math (real monotonic time) from
  kernel-visible guest clocks in the native adapter. Added
  `--clock-realtime-ns` and `--clock-monotonic-ns` flags; the shared adapter
  installs opaque session callbacks that return the frozen value when fixed,
  and the real host clock otherwise. SELECT host-timeout math still uses the
  real monotonic clock, so a slow runner cannot ride a frozen guest clock.
- Browser exports `waste_wast_set_clock_realtime_ns` and
  `waste_wast_set_clock_monotonic_ns` capture low/high 32-bit halves of a
  u64 nanosecond value; the engine kernel reads them through callbacks that
  fall back to `waste_host.wall_clock_ms` when unset. Setting a frozen
  monotonic clock installs the callback; realtime is always installed so the
  override path and the default path share one call site.
- The production worker gained a `set-clock` control message validated before
  Wasm integer conversion. Values are stored and applied inside `run()` after
  instantiate so the subsequent engine init picks them up, before terminal
  activation and the VFS mount. Values arriving after `start` are rejected
  via the same `g_yield_active` guard as other exports.
- Added a shared four-assertion `clock` scenario pinning realtime to
  1 234 567 890 123 456 789 ns via `time`, `gettimeofday` tv_sec and tv_usec.
  Native, browser exports and the production worker consume the same JSON
  contract. The worker check skips bash.html for the same reason the `io`
  scenario does: the fixture does not go through the Bash shell and the
  bash.html worker's post-script VFS check expects shell-populated paths.
- Verification: the clock scenario passes natively with
  ASan/UBSan/LeakSanitizer and through browser exports; all thirteen shared
  interaction contracts still pass against native/export assets. The browser
  wasm is rebuilt; bash.html is regenerated so its embedded worker.js has
  the new `set-clock` handler. Evidence:
  `build/engine/refactor-stage6a2-clock` (to be populated).

Native host-io yield slice (2026-10-02):

- Moved the `waste_kernel.host_upload_v1`/`host_download_v1` handlers out of
  the browser-only `posix_stubs.c` and into the shared engine
  (`src/engine/guest_posix.c`), so the native adapter, compiled browser
  exports and the production worker all share one host-io yield path. Added
  a store-scoped `native_host_io_state` (kind/path/data/data_len/result) in
  `src/engine/store.h` with free-at-destroy ownership in `store.c`.
  `browser_api.c` now reads/writes the same per-store buffer through the
  existing yield context; the former `g_host_io` global and duplicated
  per-exporter handlers are gone.
- Added shared-engine resolver registration for both imports and chained
  `browser_host_resolver` to call `guest_posix_host_resolver` so there is no
  separate browser dispatch table. Trace output goes through
  `guest_platform_trace()`, keeping the native/browser code paths identical.
- Native CLI gained scripted FIFO replies for host-io yields:
  `--host-upload-reply FILE`, `--host-upload-cancel`,
  `--host-download-complete` and `--host-download-cancel`. The adapter
  queues the replies before `native_store_init`, pops the FIFO at each
  `EXEC_YIELD_HOST_IO` and completes the yield with `result=1` (bytes for
  upload) or `result=-1` (cancel). `EXEC_YIELD_HOST_IO` does not consume
  the read/select wait counters or trace them as waits, and the result JSON
  reports new `uploadEvents`/`downloadEvents` counters.
- Added a shared five-assertion `transfer` scenario that exercises
  upload-ok, upload-cancel, download-ok and download-cancel in a single
  module with no shell. Native, browser exports and the Python driver
  consume the same `tests/guest-session-transfer.json`; the browser driver
  drains host-io yields using the same scripted replies and verifies
  download bytes against the contract. The worker check skips this
  scenario for the same reason as `io`/`clock`: the fixture does not go
  through the Bash shell.
- Verification: the transfer scenario passes natively with
  ASan/UBSan/LeakSanitizer (5/5 assertions, 2 uploads and 2 downloads
  reported) and through browser exports (5/5). All thirteen existing shared
  interaction contracts still pass against native/export assets, and the
  browser wasm has been rebuilt against the refactored resolver. Evidence:
  `build/engine/refactor-stage6a2-host-io` (to be populated).

Arbitrary-PID signal routing slice (2026-10-02):

- Extended the WSC1 signal record to carry a target PID in the previously
  reserved `second` word. `second == 0` keeps the legacy active-kernel path
  (`posix_kernel_signal_raise`); a positive value routes through the shared
  `native_store_signal_process` so backgrounded processes and process-group
  members receive signals without stealing the active continuation and without
  silently masking unknown-PID or zombie-target errors. Native, browser
  exports and the production worker share one routing path.
- Added `waste_wast_raise_signal_pid(signal, pid)` to `browser_api.c`
  alongside the existing `waste_wast_raise_signal`. The export validates the
  `kernel_terminal` guard, dispatches to the kernel path when `pid == 0` and
  to `native_store_signal_process` otherwise. Browser store errors remain
  visible as negative POSIX errno values rather than being coerced into a
  passed event.
- Extended the production worker's signal message to accept an optional
  positive `pid` field, validated as a non-negative 31-bit integer before any
  Wasm integer conversion. `pendingSignals` now holds `{signal, pid}` entries
  (legacy numeric entries still flush correctly); the flush loop routes
  through `waste_wast_raise_signal_pid`. The sixteen pending-signal bound and
  the `control-error` surfaces are unchanged.
- Added a shared four-assertion `signal-pid` scenario (`setup`, `by_pid`,
  `by_active`, `done`) that first signals the active process via `signalPid`
  (through the by-PID route) and then via the legacy active-kernel route in
  the same module. Native, browser exports and the Python driver consume the
  same `tests/guest-session-signal-pid.json`; the worker check skips this
  scenario for the same reason as `io`/`clock`/`transfer`. Scope: this slice
  demonstrates the explicit routing path on the active process; by-PID
  routing to a backgrounded child while the parent is bounded-child-first
  blocked, process-group fan-out and multi-member/backgrounded-group fan-out
  are covered by the signal-pid-backgrounded, signal-pgid, signal-pgid-fork
  and signal-pgid-backgrounded slices below.
- Verification: the signal-pid scenario passes natively with
  ASan/UBSan/LeakSanitizer (4/4 assertions, 2 signal events reported) and
  through browser exports (4/4). All fourteen existing shared interaction
  contracts still pass against the final native/export/worker assets after
  rebuilding `waste-wast.wasm` and regenerating `bash.html`; the resynced
  `installed-vfs.wvfs` matches the embedded image byte-for-byte. Evidence:
  `build/engine/refactor-stage6a2-signal-pid/signal-pid.json`.

Backgrounded-child PID signal slice (2026-10-02):

- Added a shared three-assertion `signal-pid-backgrounded` scenario
  (`tests/guest-session-signal-pid-backgrounded.wast`) that forks a child
  which pauses in `pselect` and has the parent block in `waitpid`. A
  single WSC1 operation=2 record targets the child's PID (`signalPid=2`)
  while the parent is bounded-child-first-blocked. Only the child
  receives `SIGUSR1`; the parent's own handler must not fire on resume
  because by-PID routing dispatches to a single kernel.
- The fixture asserts both halves of the semantics in one invocation:
  after `waitpid` the parent runs a zero-timeout `pselect` to drain any
  signal that might have been queued on its kernel. If the by-PID route
  had leaked to the parent kernel, that `pselect` boundary would
  dispatch the parent's handler and set `caught != 0` (returned as
  `-1`). The child's status must be `10` (the delivered signal number);
  otherwise `-2` is returned. This complements the signal-pid slice
  (which signalled only the active process) by observing that the shared
  `native_store_signal_process` path does not touch non-target kernels
  even when the target kernel is scheduled behind a suspended parent.
- Driver scope: `tests/guest-session-signal-pid-backgrounded.json` ships
  one WSC1 op=2 record with an explicit `signalPid`. Native and
  browser-export checks consume the same fixture; the worker check skips
  this scenario for the same reason as the other signal and host-only
  scenarios (host-only mediated event). This closes the "signals
  delivered to a backgrounded child while the active parent is
  bounded-child-first-blocked" item left as future work in the signal-pid
  slice.
- Verification: the signal-pid-backgrounded scenario passes natively
  with ASan/UBSan/LeakSanitizer (3/3 assertions, 1 fork, 1 child exit, 1
  signal event reported) and through browser exports (3/3). The
  signal-pid, signal-pgid, signal-pgid-fork, signal-pgid-backgrounded
  and terminal-control scenarios still pass natively and through
  browser exports after the fixture addition. Evidence:
  `build/engine/refactor-stage6a2-signal-pid-backgrounded/signal-pid-backgrounded.json`.

Process-group signal fan-out slice (2026-10-02):

- Added a shared engine helper `native_store_signal_process_group(store, pgid,
  signal)` in `src/engine/process.c` / `src/engine/store.h`. It iterates the
  bounded `processes` table, skips used/zombie slots whose kernel pgid does not
  match, and routes each live member through the existing
  `native_store_signal_process` path so terminating members zombify cleanly
  without blocking later deliveries. Returns the delivered count or
  `-POSIX_EINVAL` on invalid arguments; empty groups return zero rather than a
  success so host-boundary callers can surface the "no members" case
  explicitly. The existing guest `killpg` syscall in `guest_posix.c` now calls
  the helper instead of open-coding the same loop, so the guest syscall and
  every host-boundary adapter share one fan-out implementation.
- Introduced WSC1 operation=3 for process-group routing: `first` is the signal
  (1..`POSIX_SIGNAL_MAX`), `second` is a required positive pgid. Zero pgid is
  rejected so callers never conflate "deliver to active" (operation=2 with
  `second == 0`) with the group fan-out code path. A zero-member delivery
  surfaces as an invalid control record rather than being hidden as a passed
  event.
- Added `waste_wast_raise_signal_pgid(signal, pgid)` to `browser_api.c`
  alongside the existing `_pid` export. Semantics match the CLI: a positive
  pgid is required; the export dispatches through
  `native_store_signal_process_group` and returns delivered-count or negative
  errno.
- Extended the production worker's signal message to accept an optional
  positive `pgid` field (mutually exclusive with `pid`), validated as a
  non-negative 31-bit integer. `pendingSignals` entries now carry an optional
  `pgid`; the engine-ready and pending flushes dispatch to
  `waste_wast_raise_signal_pgid` when `pgid` is set and treat any positive
  return as success.
- Added a shared three-assertion `signal-pgid` scenario (`setup`, `pause`,
  `done`) where the active process joins `pgid=42` via `setpgid(0, 42)` and
  then receives `SIGUSR1` via the by-pgid fan-out. Native, browser exports and
  the Python driver consume the same `tests/guest-session-signal-pgid.json`;
  the worker check skips this scenario for the same reason as
  `io`/`clock`/`transfer`/`signal-pid` (host-only mediated event). Scope: this
  slice demonstrates the explicit group-fan-out path with one member;
  multi-member fan-out and backgrounded-group fan-out while the active parent
  is bounded-child-first-blocked are covered by the follow-up signal-pgid-fork
  and signal-pgid-backgrounded slices below.
- Verification: the signal-pgid scenario passes natively with
  ASan/UBSan/LeakSanitizer (3/3 assertions, 1 signal event reported) and
  through browser exports (3/3). The signal-pid regression still passes (4/4
  native and 4/4 browser) after the WSC1 operation=3 branch was added, and the
  worker still passes a representative scenario (heredoc, 7/7) through the
  regenerated `bash.html` with the resynced `installed-vfs.wvfs`. Evidence:
  `build/engine/refactor-stage6a2-signal-pgid/signal-pgid.json`.

Multi-member process-group signal fan-out slice (2026-10-02):

- Added a shared three-assertion `signal-pgid-fork` scenario
  (`tests/guest-session-signal-pgid-fork.wast`) that joins `pgid=42` in setup
  and then forks a child. The clone inherits the parent kernel's pgid through
  the existing `memcpy` in `posix_kernel_clone`, so a single WSC1 operation=3
  record delivers `SIGUSR1` to both members: the child wakes from `pselect`,
  exits with the signal number as its status, and the parent's handler fires
  on resume. The parent calls `waitpid` and then invokes a zero-timeout
  `pselect` so its queued signal is dispatched at the pselect boundary before
  the fixture compares both `caught` and the child exit status; this makes
  explicit that signal handlers only run at pselect boundaries today, not at
  the waitpid return edge.
- Driver scope: `tests/guest-session-signal-pgid-fork.json` ships the same
  single-event WSC1 op=3 record used by the single-member slice. Native and
  browser-export checks consume the same fixture; the worker check skips this
  scenario for the same reason as `io`/`clock`/`transfer`/`signal-pid`/
  `signal-pgid` (host-only mediated event). This closes the multi-member
  item called out in the signal-pgid slice. The engine helper itself was
  already multi-member capable; this slice adds the driver scaffolding and
  the forked-fixture that exercises the fan-out path end-to-end. The
  companion backgrounded-group slice below closes the remaining
  future-work item from signal-pgid (fan-out that excludes the parent).
- Verification: the signal-pgid-fork scenario passes natively with
  ASan/UBSan/LeakSanitizer (3/3 assertions, 1 fork, 1 child exit, 1 signal
  event reported) and through browser exports (3/3). The signal-pid,
  signal-pgid, and terminal-control scenarios still pass natively and
  through browser exports after the fixture addition. Evidence:
  `build/engine/refactor-stage6a2-signal-pgid-fork/signal-pgid-fork.json`.

Backgrounded-group signal fan-out slice (2026-10-02):

- Added a shared three-assertion `signal-pgid-backgrounded` scenario
  (`tests/guest-session-signal-pgid-backgrounded.wast`) that keeps the
  parent in `pgid=42` and has the forked child move itself to `pgid=99`
  via `setpgid(0, 99)` before entering `pselect`. A single WSC1
  operation=3 record targets `pgid=99` while the parent is
  bounded-child-first-blocked in `waitpid`. Only the backgrounded child
  receives `SIGUSR1`; the parent's own handler must not fire on resume
  because its pgid is not a member of the signalled group.
- The fixture asserts both halves of the semantics in one invocation:
  after `waitpid` the parent runs a zero-timeout `pselect` to drain any
  signal that might have been queued on its kernel. If the fan-out had
  leaked outside `pgid=99`, that `pselect` boundary would dispatch the
  parent's handler and set `caught != 0` (returned as `-1`). The child's
  status must be `10` (the delivered signal number); otherwise `-2` is
  returned. The engine helper's existing by-pgid membership check in
  `native_store_signal_process_group` is now covered by a scenario that
  can observe a false positive (parent would wake) rather than only
  verifying the positive-match path.
- Driver scope: `tests/guest-session-signal-pgid-backgrounded.json` ships
  one WSC1 op=3 record. Native and browser-export checks consume the same
  fixture; the worker check skips this scenario for the same reason as
  the other signal and host-only scenarios (host-only mediated event).
  This closes the "signals delivered to a backgrounded group while the
  active parent is bounded-child-first-blocked" item left as future work
  in the signal-pgid and signal-pgid-fork slices.
- Verification: the signal-pgid-backgrounded scenario passes natively
  with ASan/UBSan/LeakSanitizer (3/3 assertions, 1 fork, 1 child exit, 1
  signal event reported) and through browser exports (3/3). The
  signal-pid, signal-pgid, signal-pgid-fork and terminal-control
  scenarios still pass natively and through browser exports after the
  fixture addition. Evidence:
  `build/engine/refactor-stage6a2-signal-pgid-backgrounded/signal-pgid-backgrounded.json`.

Cooperative dispatch-pump cancellation slice (2026-10-02):

- Closed the "immediate external cancellation during a running worker" gate
  left open by the bounded execution-policy slice.  The underlying problem
  was that the browser worker's single-threaded JavaScript cannot process a
  posted `cancel` message while synchronously waiting on
  `waste_wast_resume()`; the dispatch-loop safepoint ran but no onmessage
  callback could set `g_execution_stop`.
- Added a new `EXEC_YIELD_PUMP` yield reason (`src/engine/engine_internal.h`)
  and cooperative pump fields (`pump_quantum_ns`, `last_pump_ns`,
  `pump_clock_now`, `pump_clock_context`) on `exec_execution_control`.  The
  dispatch-loop safepoint at `src/engine/op/execute.c` now, after the
  existing 4,096-opcode stop poll, consults the embedder's wall clock and —
  when the configured quantum has elapsed — saves a yield frame and returns
  `EXEC_YIELD` with reason `EXEC_YIELD_PUMP`.  No value-type checkpoint or
  operand-stack rewriting is required because the yield happens between
  opcodes; resume reuses the existing `yield_frames` path at
  `exec_invoke_managed`.  The engine never consumes host I/O for pump; the
  process driver's generic yield path forwards it unchanged.
- Added `waste_wast_set_pump_quantum_ms(ms)` and `waste_wast_request_cancel()`
  browser exports (`src/html-rt/browser_api.c`).  The pump quantum is
  installed before `run_script` and clamped to [0, 60000] ms.  A no-op
  installer sets a poll callback when only the pump is configured so the
  dispatch-loop safepoint still runs.  Pump yields reuse the shared
  `browser_driver_publish_wait` save/restore path (the saved engine entry
  and args are the same selection that caused the yield); the worker
  distinguishes them from ordinary I/O yields by polling
  `waste_wast_wait_kind() == EXEC_YIELD_PUMP` (6).  The cancel export sets
  both `g_execution_stop` and the live store's
  `execution_control.stopped` so the next safepoint fast-returns
  `EXEC_ERROR_INTERRUPTED` with `EXEC_STOP_CANCELLED`.
- Updated `src/html-rt/src/worker.js`: the start message's `executionLimits`
  accepts a new `pumpQuantumMs` field (validated and forwarded through the
  new export); the main resume loop inspects `waste_wast_wait_kind()` and,
  when it reads `6` (`EXEC_YIELD_PUMP`), treats the yield as a cooperative
  timeslice, draining the event loop via
  `await new Promise(r => setTimeout(r, 0))` so any pending onmessage runs
  before the next `waste_wast_resume()`.  A new `cancel` message type calls
  `waste_wast_request_cancel()` and wakes a pending I/O resolver, mirroring
  the existing scheduled-cancel path.  Without `pumpQuantumMs` the engine
  behaves identically to the pre-slice contract; the new message is a no-op
  outside the pump window.
- Scope: this slice is cooperative, not preemptive.  Parsing/decoding, a
  single long guest instruction and host-blocking callbacks are not
  interrupted.  A guest process with no `pumpQuantumMs` continues to behave
  as before, so existing fixtures (`signal-pid`, `signal-pid-backgrounded`,
  `signal-pgid`, `signal-pgid-fork`, `signal-pgid-backgrounded` and
  terminal-control) are unaffected.
- Verification: engine, browser-API and native CLI builds succeed with no
  new warnings or errors.  Shared guest-session matrix (including the
  signal-pid-backgrounded scenario) still passes natively and through
  browser exports with the pump off.  Added
  `tests/guest-session-pump-cancel.wast` (a WAT fixture that writes `P\n`
  then enters an unbounded `(loop ...)`) and
  `tests/guest-session-pump-cancel.cjs` (a Node harness that boots the
  production `src/html-rt/src/worker.js` in a VM with
  `executionLimits: {pumpQuantumMs: 10}`, waits for the wake marker, posts
  `{type: "cancel"}` and asserts the worker reports
  `ok=false, cancelled=true, exited=false`).  The fixture completes in
  ~10–25 ms, confirming the pump handshake delivers the external cancel
  through `onmessage` and the next safepoint reports `EXEC_STOP_CANCELLED`.
  Evidence: `build/engine/refactor-stage6a2-pump-cancel/result.json`.

Path-traversal `ENOTDIR` slice (2026-10-02):

- Closed the "diagnostic redirection through a regular-file path component
  unexpectedly succeeded instead of returning `ENOTDIR`" gap tracked by the
  heredoc slice.  `posix_kernel_open` already consulted
  `path_prefix_is_file` on the `!O_CREAT` branch, but the `O_CREAT` branch
  reached `posix_kernel_path_add_data` unconditionally — and
  `path_add_data` is intentionally permissive for packaged installs, so it
  created a parallel node at `/some/file/leaf` even when `/some/file` was
  already a regular file.  `posix_kernel_path_mkdir` had the same latent
  bug for new directories.
- `src/engine/lib/kernel.c`: `posix_kernel_open` (both the `!O_CREAT` and
  `O_CREAT` branches now share a single `path_prefix_is_file` check before
  the create fallback) and `posix_kernel_path_mkdir` reject the traversal
  with `-POSIX_ENOTDIR` before touching the node table.  Extended the same
  guard to the mutation and query functions that previously surfaced
  `ENOENT` on a file-prefix traversal: `posix_kernel_path_unlink`,
  `posix_kernel_path_set_mtime`, `posix_kernel_path_rename` (both the
  missing-source branch and the create-at-destination branch) and
  `posix_kernel_path_readlink` now distinguish traversal (`ENOTDIR`) from
  missing leaf (`ENOENT`) so POSIX
  `utimensat`/`unlink`/`rename`/`readlink` callers can surface the
  correct errno.
  The permissive `path_add_data` installer path is unchanged so packaged
  metadata installs continue to short-circuit the prefix check.
- Scope: `stat`, `access`, `set_cwd`, `chmod` and `snapshot` retain their
  existing prefix checks; `open`, `mkdir`, `unlink`, `rename`, `set_mtime`
  and `readlink` are now covered too.  `link` and `symlink` remain out of
  scope because they go through the intentionally-permissive
  `path_add_data`/`path_add_symlink` installers, matching the packaged-
  install contract.
- Verification: extended `tests/posix-path-vfs.c` with explicit regressions
  for `open(O_WRONLY|O_CREAT)`, `open` with no `O_CREAT`, `mkdir` through a
  regular-file prefix, a follow-up `stat`, `unlink` and `set_mtime` through
  a file prefix, `rename` with the file prefix on both the source and
  destination sides, and `readlink` through a file-prefix path.  The
  ASan/UBSan unit now reports
  `POSIX path VFS: 69 checks, 0 failures` (up from 60).  Rebuilt the
  native CLI (`src/cli-rt/wast-native`) and the browser Wasm
  (`src/html-rt/wast-browser`); the production worker pump+cancel fixture
  and the browser signal-pid-backgrounded scenario still pass unchanged
  through the rebuilt artifacts.  Evidence:
  `build/engine/refactor-stage6a2-path-traversal/posix-path-vfs.log`.

Symlink-leaf `open` slice (2026-10-02):

- Closed a latent kernel gap: `posix_kernel_open` rejected a symlink leaf
  with `-POSIX_ENOENT` because `path_find` returns the raw node without
  following `POSIX_NODE_SYMLINK`, and the subsequent kind check at
  `src/engine/lib/kernel.c:1315` only admitted `POSIX_NODE_REGULAR` or
  `POSIX_NODE_DIRECTORY`.  Guest programs opening a packaged alias (for
  example `/usr/bin/foo` installed as a symlink to the canonical binary
  in `/bin`) therefore received a bogus `ENOENT` instead of the target's
  file descriptor; `stat`/`chmod`/`snapshot` already followed symlinks on
  the same traversal, so this was a localized `open` divergence.
- `src/engine/lib/kernel.c`: `posix_kernel_open` now resolves the leaf
  with a bounded loop (`link_depth < 8`, matching `path_stat`/
  `path_chmod`/`path_snapshot`) before the `path_prefix_is_file`,
  `O_CREAT`, directory and regular-file branches.  If the walk exceeds
  the depth cap the call fast-returns `-POSIX_ELOOP`.  If the target
  resolves to a nonexistent path and `O_CREAT` is set, the create branch
  materializes the regular file at the resolved target (not the alias),
  so later lookups through the alias and the target observe the same
  `posix_file_object`.  Dangling symlinks without `O_CREAT` fall through
  the existing `path_prefix_is_file`/`ENOENT` guard unchanged.
- Scope: `O_NOFOLLOW` is still not accepted by the flags mask (it is
  deliberately out of scope for this slice because no scenario reports a
  concrete need); `openat`-style directory-fd relative resolution is
  likewise unchanged.  Only the leaf is resolved — intermediate symlink
  components keep their current (non-following) semantics shared with
  `path_find`, which is consistent with the surrounding kernel API.
- Verification: extended `tests/posix-path-vfs.c` to open the existing
  `/data/alias -> /data/readme` symlink, read its five bytes through the
  returned fd and close it.  The ASan/UBSan unit now reports
  `POSIX path VFS: 72 checks, 0 failures` (up from 69).  Rebuilt the
  native CLI (`src/cli-rt/wast-native`) and the browser Wasm
  (`src/html-rt/wast-browser`); the production worker pump+cancel
  fixture (`elapsedMs=9, cancelled=true`), the `signal-pid-backgrounded`,
  `bash`, `heredoc` and `matrix` guest-session scenarios still pass
  unchanged through the rebuilt artifacts.  Evidence:
  `build/engine/refactor-stage6a2-symlink-open/posix-path-vfs.log`.

Shared-memory `O_EXCL` slice (2026-10-02):

- Closed a latent `posix_kernel_shm_open` bug: when a caller passed the
  canonical POSIX atomic-create pattern `O_CREAT | O_EXCL` on a *new*
  name, the kernel incorrectly returned `-POSIX_EEXIST` instead of
  creating the object.  The existing-name branch (lines ~1375-1377)
  already distinguished `O_CREAT | O_EXCL` from `O_CREAT` and only
  surfaced `EEXIST` when the name was present, so the regression was
  isolated to the new-name path.
- `src/engine/lib/kernel.c`: removed the stray
  `if (flags & POSIX_O_EXCL) return -POSIX_EEXIST;` in the `!object`
  branch of `posix_kernel_shm_open`.  The surrounding
  `POSIX_SHM_OBJECT_MAX`, credential, mode and `file_object_create`
  paths are unchanged, so the existing `/object` and `/denied-create`
  tests continue to pass.  POSIX semantics now hold: `O_EXCL` fails
  only when the name already exists.
- Scope: this is a one-line kernel bug fix; it does not alter the shm
  namespace policy, `shm_unlink` ordering, descriptor retention under
  `fork`/`unlink` or credential checks, which already match POSIX.
- Verification: extended `tests/posix-kernel.c` with two new checks —
  a successful `O_CREAT|O_EXCL` creation of a previously-unknown name
  `/exclusive-create` returning a valid fd, and a follow-up
  `O_CREAT|O_EXCL` that still fails with `EEXIST` after the name is
  established.  `posix-kernel` now reports `348 tests passed` (up from
  346).  Rebuilt the native CLI (`src/cli-rt/wast-native`) and the
  browser Wasm (`src/html-rt/wast-browser`); the production worker
  pump+cancel fixture (`elapsedMs=10, cancelled=true`) and the shared
  guest-session `bash` scenario still pass unchanged through the
  rebuilt artifacts.  `posix-path-vfs` remains at
  `72 checks, 0 failures`.  Evidence:
  `build/engine/refactor-stage6a2-shm-exclusive/posix-kernel.log`.

Stage 6A.2 full-matrix verification sweep (2026-10-02):

- After the recent kernel/VFS slices (path-traversal `ENOTDIR`,
  symlink-leaf `open`, `shm_open` `O_EXCL`), re-ran the entire
  guest-session parity matrix together with the ancillary unit and
  worker contracts to confirm that none of the small fixes regressed a
  previously-passing scenario and that every slice referenced in this
  document still holds end-to-end against the current native CLI and
  packaged production worker.
- Nineteen shared-adapter scenarios through
  `tests/guest-session-check.py` (native + packaged worker where the
  harness exercises both): `io`, `terminal-control`, `clock`,
  `transfer`, `signal-pid`, `signal-pid-backgrounded`, `signal-pgid`,
  `signal-pgid-fork`, `signal-pgid-backgrounded`, `bash`, `matrix`,
  `pipeline`, `heredoc`, `heredoc-long`, `exec-fail`, `rogue-fresh`,
  `rogue`, `handlers` and `handler-start` all PASS, each with
  intentional handler-negatives retaining their expected failures (not
  relabeled as successful assertions).
- Ancillary ASan/UBSan unit gates: `posix-path-vfs` reports
  `72 checks, 0 failures` and `posix-kernel` reports `348 tests passed`
  on top of the sanitize-built kernel source, so the latent-bug slices
  are covered by regression tests.  The production-worker
  `pump-cancel.cjs` contract still completes in ~9 ms with
  `ok=false, cancelled=true, exited=false`.
- Scope: this slice adds no code; it is a bundled verification sweep
  that enumerates the current Stage 6A.2 gate against the installed
  VFS, the shared engine and the production worker.  It does not run
  the full browser corpus (267/280 remains the baseline with thirteen
  pre-existing failures) or the official OCaml oracle comparison; both
  remain explicit full-browser verification steps for a cutover slice.
- Evidence: `build/engine/refactor-stage6a2-full-matrix/summary.txt`
  (totals: 19 shared-adapter scenarios, 420 unit checks, 1 worker
  contract — all pass) with per-scenario logs
  `build/engine/refactor-stage6a2-full-matrix/{scenario}.log`,
  `posix-path-vfs.log`, `posix-kernel.log` and `pump-cancel.log`.
  This closes the Stage 6A.2 regression-verification bullet; the
  remaining Stage 6A acceptance work is the full-browser corpus
  comparison and the plan-status cutover slice.

Full browser-corpus baseline (2026-10-02):

- Ran `tests/c-engine-browser-runtime.cjs` end-to-end against the
  current engine and packaged worker (`build/html-rt/waste-wast.wasm`
  and `build/html-rt/tests/payload.json`) to establish that the Stage
  6A.2 kernel/VFS slices did not perturb the official spec/libc
  browser corpus.  The run reports **267 PASS, 13 FAIL** — identical
  to the documented pre-existing baseline referenced throughout this
  plan.  No new corpus failure appeared and no previously-failing
  entry started passing by accident.
- The thirteen failing files remain the pre-existing set documented
  in the heredoc slice: `core/imports.wast`,
  `core/memory64/memory64-imports.wast`,
  `core/multi-memory/imports2.wast`,
  `libc-test/entropy-messages.wast`,
  `libc-test/environment-boundaries.wast`,
  `libc-test/matching-sort.wast`,
  `libc-test/memory-conversion.wast`,
  `libc-test/path-runtime.wast`,
  `libc-test/select-abi.wast`,
  `libc-test/select-runtime.wast`,
  `libc-test/stat-abi.wast`,
  `libc-test/terminal.wast` and `libc-test/time-resource.wast`.  All
  thirteen are documented engine/runtime shortfalls (unknown module
  id, abi probe gaps and libc capability holes); none are regressions
  introduced by the current slice series.
- Scope: this slice does not fix the pre-existing failures — those
  remain in their own tracked backlog — and does not re-run the OCaml
  oracle comparison for the official core suite, which continues to
  be a separate differential gate (`native core 96/97`) retained for
  the Stage 6B/6C/7 cutover work.  It also does not exercise an
  actual Chromium `file://` boot of the dashboard; the harness runs
  the packaged worker inside Node's VM.
- Evidence:
  `build/engine/refactor-stage6a2-browser-corpus/summary.txt` with
  the full `corpus.log` enumerating every PASS/FAIL line.  Together
  with the full-matrix slice above this closes the Stage 6A.2
  verification bullets; only the plan-status cutover (flipping the
  Stage 6A gate line and updating `AGENTS.md`/`docs/architecture.md`
  cross-references) remains before Stage 6A can be marked complete.

Verification evidence and reproduction: `docs/native-guest-session.md` and
`build/engine/refactor-stage6a2`, `build/engine/refactor-stage6a2-handlers` and
`build/engine/refactor-stage6a2-heredoc` and
`build/engine/refactor-stage6a2-control` and
`build/engine/refactor-stage6a2-terminal` and
`build/engine/refactor-stage6a2-clock` and
`build/engine/refactor-stage6a2-host-io` and
`build/engine/refactor-stage6a2-signal-pid` and
`build/engine/refactor-stage6a2-signal-pid-backgrounded` and
`build/engine/refactor-stage6a2-signal-pgid` and
`build/engine/refactor-stage6a2-signal-pgid-fork` and
`build/engine/refactor-stage6a2-signal-pgid-backgrounded` and
`build/engine/refactor-stage6a2-pump-cancel` and
`build/engine/refactor-stage6a2-path-traversal` and
`build/engine/refactor-stage6a2-symlink-open` and
`build/engine/refactor-stage6a2-shm-exclusive` and
`build/engine/refactor-stage6a2-full-matrix` and
`build/engine/refactor-stage6a2-browser-corpus` and
`build/engine/refactor-stage6b-expected-failures` and
`build/engine/refactor-stage6b-results-contract` and
`build/engine/refactor-stage6b-group-filter` and
`build/engine/refactor-stage6b-exclude-timing`.
The Stage 6A gate is passed as of 2026-10-02 — all five acceptance
bullets land in the slices above and are verified by the full-matrix
sweep and the full browser-corpus baseline.  `test.html` and its
build options must still be retained because Stage 6B/6C/7 have not
started and the dashboard remains the harness for Stage 7 cutover.

The original full Stage 6A acceptance requirements, now all met by the
slices above (retained here for traceability):

- Implement the native runtime expansion described above. Preserve the fast
  single-file WAST command and add installed-VFS/runtime/session support.
- Run guest Bash, command/status matrices, heredoc/pipelines, dynamic loading,
  two Rogue children, and subsequent fragmented input with the same assets and
  scenario definitions used by the browser driver.
- Match native/browser outcomes for startup, errno, exit status, output,
  resume/wait behavior, process isolation, and package metadata. Normalize only
  documented nondeterministic fields, not semantic differences.
- Retain the Stage 6A.1 file-input regression and typed callback diagnosis.
  Do not classify a mounted-open probe alone as `read` builtin coverage.
- Add native sanitizer cases for new adapter/lifetime ownership and retain
  actual browser coverage of the boundaries those cases cannot exercise.

Gate (passed 2026-10-02): the primary native runtime executes the
installed guest application scenarios without browser dependencies or
substitution of host programs; the full-matrix sweep and browser-corpus
baseline above are the authoritative evidence.

### Stage 6B: Common suite manifest and reporting

Status: complete (2026-10-05). Native and browser corpus reporting, supported
language-oracle comparison, assertion-ledger inventory, Bash-page browser-native
compatibility, and production-browser full-corpus acceptance are verified.

Stage 6B.1 browser-corpus expected-failure classification slice (2026-10-02):

- Added `tests/browser-corpus-expected-failures.txt` listing the thirteen
  pre-existing failing files from the Stage 6A baseline (three core
  import-order gaps and ten libc-test capability holes).  The file uses
  shell-style comments and the harness's `test.path || test.group/test.file`
  identity so each entry matches a single corpus test exactly.
- Extended `tests/c-engine-browser-runtime.cjs` to load the expected-failures
  list once at startup and classify every test as `PASS` (not expected to
  fail, passed), `FAIL` (not expected to fail, failed — a regression),
  `XFAIL` (expected to fail, failed — the documented baseline), or `XPASS`
  (expected to fail, passed — an unexpected improvement).  The harness
  exits non-zero only on `FAIL` or `XPASS`; a run that reproduces the
  recorded baseline exits 0.  Added a `--json` flag that emits a final
  `{pass, fail, xfail, xpass, failures[], unexpectedPasses[]}` summary for
  CI consumers.  Positional-argument parsing was reworked so `--json` and
  `--shared-file-page` can appear in any position without being treated as
  a payload path or requested-file filter.
- Verified end-to-end with
  `node tests/c-engine-browser-runtime.cjs --json`.  The run reports
  **267 PASS, 13 XFAIL, 0 FAIL, 0 XPASS** and exits 0 — the thirteen
  XFAIL entries are byte-for-byte the Stage 6A baseline set
  (`core/imports.wast`, `core/memory64/memory64-imports.wast`,
  `core/multi-memory/imports2.wast`, and the ten `libc-test/*` holes).
- Scope: this slice only wires classification and the JSON summary; it
  does not fix any of the thirteen expected failures, does not change
  the engine or worker, and does not alter the shared-adapter guest
  matrix.  The expected-failures file is normative — any XPASS must be
  resolved by removing the entry (and documenting the fix in the plan)
  in the same change, and any new FAIL must be either fixed or
  explicitly added to the file with a plan reference.
- Evidence:
  `build/engine/refactor-stage6b-expected-failures/summary.txt` with the
  full `corpus.log` enumerating every PASS/XFAIL line and the trailing
  JSON summary; `tests/browser-corpus-expected-failures.txt` is the
  checked-in list.

Stage 6B.2 shared result-record contract slice (2026-10-02):

- Extended `tests/c-engine-browser-runtime.cjs` with two new flags that
  turn the stdout classifier into a reusable CI-grade contract: `--list`
  enumerates the tests that would be run as `PASS? <identity>` or
  `XFAIL? <identity>` without invoking the worker (and honors `--json`
  to print `{count, expectedFailures}`); `--results=PATH` writes a
  machine-readable JSON file after the run containing the summary
  object plus one record per test with
  `{identity, status, expectedFailure, group, file, mode}` and, for
  non-passing tests, `{failureCount, resultCount}`.  Parent directories
  are created on demand.  The results file is written on both green and
  regression outcomes so CI can diff against a baseline without needing
  the run to pass.
- Reworked the argument split so `positional` only collects non-`--`
  arguments and `--list`, `--json`, `--shared-file-page`,
  `--results=PATH` can appear in any order without being mistaken for a
  payload path or a requested-file filter.
- Verified end-to-end:
    `node tests/c-engine-browser-runtime.cjs --list` prints 280 lines;
    adding `--json` prints `{count: 280, expectedFailures: 13}`;
    `node tests/c-engine-browser-runtime.cjs --json
      --results=build/engine/refactor-stage6b-results-contract/results.json`
    exits 0, prints the Stage 6B.1 summary unchanged (267 PASS, 13
    XFAIL, 0 FAIL, 0 XPASS), and emits a 2278-line `results.json` with
    280 `"status"` records covering every classified test.
- Scope: this slice only wires enumeration and the per-test result
  artifact; it does not change engine, worker, or classification
  semantics, and does not alter the expected-failures list.  The
  native-side wast binary (`src/cli-rt`) still has no corpus/batch
  mode — adding a parallel native driver that consumes the same
  manifest and emits the same result contract is a later Stage 6B
  slice.
- Evidence:
  `build/engine/refactor-stage6b-results-contract/summary.txt`,
  `build/engine/refactor-stage6b-results-contract/corpus.log`, and
  `build/engine/refactor-stage6b-results-contract/results.json`.

Stage 6B.3 group-filter slice (2026-10-02):

- Added a repeatable `--group=NAME` flag to
  `tests/c-engine-browser-runtime.cjs`.  Each occurrence contributes
  one name to a `requestedGroups` set; the test filter now unions
  positional file identities, positional `group/file` identities, and
  the group set so a request to run anything matching any filter is
  honored.  Running with no filters still runs every non-unsupported
  test (default behaviour unchanged).  This directly closes the Stage
  6B "list/group/file selection" bullet for the browser corpus — file
  selection landed in the original harness, `--list` arrived in Stage
  6B.2, and group selection ships here.
- Discovered groups in the current payload (fourteen total): `core`,
  `core/bulk-memory`, `core/exceptions`, `core/gc`, `core/memory64`,
  `core/multi-memory`, `core/relaxed-simd`, `core/simd`,
  `custom/custom`, `custom/metadata.code.branch_hint`, `custom/name`,
  `diy-posix-test`, `legacy/exceptions/core`, and `libc-test`.  Each
  is addressable through `--group=`.
- Verifications: `--list --group=diy-posix-test` returned the five
  POSIX-regression identities; `--list --group=libc-test --json`
  reported `{count: 14, expectedFailures: 10}` matching the baseline;
  `--list --group=diy-posix-test --group=custom/name --json` reported
  `{count: 6, expectedFailures: 0}` confirming multi-group union; and
  `--group=diy-posix-test --json --results=…/results.json` executed
  the five diy-posix-test files, exited 0, emitted a 5/0/0/0 summary,
  and wrote five records to `results.json`.
- Scope: this slice is a one-flag addition.  It does not change the
  classifier, the record contract, the expected-failures file, or
  the engine/worker.  Negative selection (`--exclude=`) and
  bounded-timeout cancellation remain open Stage 6B bullets.
- Evidence:
  `build/engine/refactor-stage6b-group-filter/summary.txt`,
  `build/engine/refactor-stage6b-group-filter/run.log`, and
  `build/engine/refactor-stage6b-group-filter/results.json`.

Stage 6B.4 exclude-and-timing slice (2026-10-02):

- Added two negative-selection flags to
  `tests/c-engine-browser-runtime.cjs`: `--exclude=FILE` and
  `--exclude-group=NAME`, both repeatable, applied after the include
  set so the final test list is `(include ∪ default) \ exclude`.
  File exclusions match either the bare `file` field or the full
  `path || group/file` identity used throughout the harness.  Combined
  with the existing `--group=`, positional filters, and `--list`, this
  closes the Stage 6B "list/group/file selection" bullet with both
  positive and negative sides.
- Added an `elapsedMs` field to every record emitted into
  `--results=PATH`.  The clock is wall-clock between dispatching the
  worker's `onmessage` and the final `postMessage` return, taken from
  `process.hrtime.bigint()` and rounded to three decimals.  Every
  record carries it — PASS, FAIL, XFAIL, XPASS — so CI can set
  per-group wall-clock budgets based on observed data.
- Verifications: `--list --exclude-group=libc-test
  --exclude-group=legacy/exceptions/core
  --exclude-group=core/exceptions --json` returned
  `{count: 262, expectedFailures: 3}` (all libc-test XFAILs and the
  two exception groups removed); `--list --group=libc-test
  --exclude=libc-test/terminal.wast --exclude=libc-test/stat-abi.wast
  --json` returned `{count: 12, expectedFailures: 8}` (libc-test minus
  two files); `--group=diy-posix-test --json --results=…/results.json`
  exited 0 with 5/0/0/0 and every record populated with `elapsedMs`;
  and the unfiltered full corpus still returned `267/13/0/0` exit 0,
  confirming the include/exclude arithmetic default-passes when no
  flags are given.  Captured timing shows total wall-clock ≈ 219s with
  the five slowest tests (SIMD min/max/cmp and bulk memory copies)
  each burning more than 7s of guest execution.
- Scope: exclusion arithmetic + timing capture only.  No change to
  classification, the record-contract keys other than appending
  `elapsedMs`, or the expected-failures list.
- Deferred: real per-test cancellation.  The current harness calls the
  worker's `onmessage` synchronously in the Node event loop, and the
  worker does not yield between quanta during Node runs, so a
  `Promise.race` against `setTimeout` would measure wall-clock without
  being able to preempt a runaway test.  Preemptive cancellation
  requires isolating the worker in `node:worker_threads` (plus a
  terminate/cancellation token on the postMessage boundary) and will
  land in a later Stage 6B slice.
- Evidence:
  `build/engine/refactor-stage6b-exclude-timing/summary.txt`,
  `build/engine/refactor-stage6b-exclude-timing/run.log`,
  `build/engine/refactor-stage6b-exclude-timing/results.json`,
  `build/engine/refactor-stage6b-exclude-timing/full-run.log`, and
  `build/engine/refactor-stage6b-exclude-timing/full-results.json`.

Stage 6B.5 browser-corpus XFAIL remediation (2026-10-02):

- Diagnosis and remediation of the thirteen pre-existing browser-corpus
  expected failures tracked in `tests/browser-corpus-expected-failures.txt`.
  Family A (three memory/table import-limit tests), Family B silent-drop
  (seven libc-test fixtures), and the `time-resource` libc-overlay export
  gap were all cleared.  Baseline now **278 PASS / 0 FAIL / 2 XFAIL / 0 XPASS**;
  the two remaining XFAILs (`libc-test/environment-boundaries`,
  `libc-test/terminal`) are POSIX-surface conformance gaps intentionally
  deferred to the shared-library libc roadmap.
- Full narrative, root causes, and verification history:
  `docs/completed-browser-corpus-xfail-diagnosis.md`.
- Evidence: `build/engine/refactor-stage6b-xfail-cleared/` and
  `build/engine/refactor-stage6b-close-export/`.

Stage 6B.6 preemptive per-test cancellation (2026-10-02):

- Lands the Stage 6B.4 deferred bullet.  `tests/c-engine-browser-runtime.cjs`
  previously ran the worker script in the Node event loop via
  `vm.createContext` and awaited `self.onmessage` directly, so a
  runaway or hung test could not be preempted — only observed with
  `elapsedMs` after the fact.
- Each test now runs inside a dedicated `node:worker_threads` Worker
  spawned from a tiny bridge (`tests/c-engine-worker-host.cjs`) that
  maps the Node worker scope onto the browser Worker API
  (`self.onmessage`, `self.postMessage`) so the frontend worker source
  (`src/html-rt/src/tests-worker.js`) is used unchanged.  The main
  runtime races the worker reply against a `setTimeout` deadline;
  on timeout it calls `worker.terminate()`, records a `TIMEOUT`
  status (counted into `summary.fail` with the identity appended to
  `summary.failures`), and advances to the next test.
- New flag `--timeout-ms=MS` (default `60000`) governs the deadline.
  Captured timing from Stage 6B.4 (slowest tests ≈15s) sits
  comfortably below the default; adaptive per-group budgets remain
  deferred.
- Verifications: the full corpus (`node
  tests/c-engine-browser-runtime.cjs
  build/html-rt/tests/payload.json --json
  --results=build/engine/refactor-stage6b-cancellation/results.json`)
  returned the same **278 PASS / 0 FAIL / 2 XFAIL / 0 XPASS** as the
  pre-cancellation baseline, no regressions.  Wall-clock totalled
  328.3s across 280 tests (up from ≈219s because every test now
  pays a worker-thread spawn + wasm-decode).  A preemption probe
  (`--group=diy-posix-test --timeout-ms=50`) reported `TIMEOUT` on
  all five fixtures with the harness exiting cleanly — confirming
  the harness can preempt rather than observe-after-the-fact.
- Scope: harness cancellation only.  No change to the worker
  script, the record contract (beyond adding `TIMEOUT` as a status
  value), the expected-failures list, or any guest code.
- Deferred: dynamic per-group timeout budgets; worker-pool reuse to
  recover the per-test spawn overhead.
- Evidence: `build/engine/refactor-stage6b-cancellation/summary.txt`,
  `build/engine/refactor-stage6b-cancellation/run.log`, and
  `build/engine/refactor-stage6b-cancellation/results.json`.

Stage 6B.7 native-side corpus harness (2026-10-03):
- Lands the native half of the shared batch contract: a Node harness
  that drives `build/cli-rt/waste-wast` across every wast-stream fixture
  in `build/html-rt/tests/payload.json` and emits the same {PASS, FAIL,
  XFAIL, XPASS} record contract the browser runtime already produces.
- New file: `tests/c-engine-native-runtime.cjs`. Spawns the native
  binary once per fixture with stdio pipes, parses stdout as the JSON
  `{file, assertions, passed, total}` block, and classifies against a
  native-specific expected-failures baseline. The runner respects the
  `--group=`, `--exclude=`, `--exclude-group=`, `--timeout-ms=`,
  `--runner=`, `--json`, `--list`, and `--results=` flags that the
  browser runtime already defines. Only fixtures with
  `spec.mode === "wast-stream"` AND `spec.sourcePath` are candidates —
  browser-native and inline-wast fixtures are deliberately skipped.
- New baseline file: `tests/native-corpus-expected-failures.txt`.
  Contains 14 libc-test fixtures whose clients import
  `env.read / env.write / env.open / env.close / …` and fail at native
  link time ("unresolved function import"). Those same fixtures PASS
  under the browser worker because it registers the POSIX stub
  resolver; the gap is strictly a native capability one. The baseline
  reader strips `#` comments and trims whitespace, matching the
  browser-side baseline format.
- Classification quirk resolved: `core/inline-module.wast` and
  `core/type-canon.wast` are module-only fixtures that exit 0 with
  `{passed: 0, total: 0}`. An initial `ok = … && parsed.total > 0`
  check misclassified both as FAIL. The runner now treats `0/0` with
  exit 0 as PASS, matching intent and the browser-side result.
- Verification: `node tests/c-engine-native-runtime.cjs --json
  --results=build/engine/refactor-stage6b-native-runner/results.json`
  reports **261 PASS / 0 FAIL / 14 XFAIL / 0 XPASS**. The initial
  pre-baseline run (recorded in `run-initial.log`) showed 261 PASS /
  14 FAIL — identical shape, confirming the baseline captures the full
  native-capability gap and nothing else.
- Deferred: a `native_host_resolver` wiring `env.read / env.write /
  env.open / env.close / env.ioctl / …` to freestanding libc syscalls
  (or returning ENOSYS where appropriate) would clear every entry in
  the current baseline in one slice. Diy-posix fixtures remain
  skipped because they are either `browser-native` mode or inline
  `wastText` with no `sourcePath` — exposing them natively requires
  either materialising `.wast` files into payload.json or teaching the
  native binary to consume inline-wast payloads directly.
- Evidence: `build/engine/refactor-stage6b-native-runner/summary.txt`,
  `build/engine/refactor-stage6b-native-runner/run-initial.log`,
  `build/engine/refactor-stage6b-native-runner/run-baseline.log`, and
  `build/engine/refactor-stage6b-native-runner/results.json`.

Stage 6B.8 native cli-rt POSIX host resolver parity (2026-10-03):
- Lands the Stage 6B.7 deferred bullet: chains the cli-rt host resolver
  to `guest_posix_host_resolver` (the engine-owned POSIX stub table the
  browser worker already uses), so `env.*` imports and the full
  versioned `waste_kernel` ABI resolve at native link time. 12 of the
  14 libc-test fixtures previously listed as native XFAIL now PASS
  natively; the remaining 2 (`libc-test/environment-boundaries.wast`,
  `libc-test/terminal.wast`) are the identical pair the browser corpus
  still lists as XFAIL, bringing native and browser capability
  baselines to exact parity.
- `src/cli-rt/main.c` previously had a `cli_host_resolver` that only
  handled four `waste_kernel.*` names and short-circuited on everything
  else. The resolver now:
    1. Tries the four direct `waste_kernel.*` bindings (`select_v1`,
       `pselect_v1`, path-access, path-stat) — on a match it returns the
       direct binding, keeping the native wast-driven descriptor
       readiness path intact.
    2. Otherwise installs a `native_platform` (five callbacks matching
       the `guest_posix_platform` contract) and delegates to
       `guest_posix_host_resolver`, mirroring the browser's
       `browser_host_resolver` in `src/html-rt/posix_stubs.c`.
    3. The `native_platform` deliberately refuses host filesystem I/O —
       `open / close / read / write` return `-POSIX_ENOSYS` and `trace`
       is a no-op. The engine kernel/VFS still owns descriptor, path,
       and process semantics; only the platform fall-through is
       sandbox-locked, so the native runner can't accidentally touch
       real files.
- `tests/native-corpus-expected-failures.txt` shrank from 14 libc-test
  entries to 2, matching the browser-corpus baseline verbatim. Header
  rewritten to describe the parity and point at the Stage 6B.8 evidence
  directory. The baseline reader still strips `#` comments and trims
  whitespace, matching the browser-side format.
- Verification: a full-corpus run with the old 14-entry baseline
  reported 261 PASS / 0 FAIL / 2 XFAIL / **12 XPASS**, correctly
  surfacing every fixture that flipped to PASS so the baseline update
  is a reflection of reality. A follow-up full-corpus run with the
  trimmed 2-entry baseline reports **273 PASS / 0 FAIL / 2 XFAIL /
  0 XPASS** — clean exit, 12-fixture net PASS gain, no regressions. The
  5-fixture gap vs the browser's 278 PASS is the diy-posix-test group
  which the native harness deliberately skips.
- Deferred: diy-posix native coverage still needs either inline-wast
  consumption in the native binary or on-disk materialisation of those
  fixtures. A broker-backed or opt-in VFS-backed `native_platform`
  could later replace `-ENOSYS` with actual host-file bytes without
  compromising the sandbox default. The remaining 2 XFAIL fixtures
  need the capability work tracked under
  `docs/completed-browser-corpus-xfail-diagnosis.md`.
- Evidence: `build/engine/refactor-stage6b-native-resolver/summary.txt`,
  `build/engine/refactor-stage6b-native-resolver/run-baseline.log`, and
  `build/engine/refactor-stage6b-native-resolver/results.json`.

Stage 6B.9 browser-corpus worker-pool reuse (2026-10-03):
- Lands the Stage 6B.6 deferred worker-pool bullet: the browser corpus
  harness previously spawned a fresh `node:worker_threads` Worker per
  test, spending ~400ms of amortised Worker + wasm-decode cost on every
  one of the 280 fixtures. The harness now spawns ONE worker at
  startup, reuses it across every message, and only respawns on
  timeout.
- `src/html-rt/src/tests-worker.js` is already stateless across
  messages — each `self.onmessage` call re-invokes
  `WebAssembly.instantiate(wasmBytes, imports)` and builds a fresh
  engine instance from scratch — so a single long-lived worker
  preserves test isolation without any change to the worker code
  itself.
- `tests/c-engine-browser-runtime.cjs` was reworked to:
    1. Spawn one `Worker(workerHostPath, {workerData: {workerSrc}})`
       before the test loop.
    2. Attach `message` and `error` listeners per test (with a paired
       `cleanup` closure so leftover listeners don't accumulate), race
       against the `setTimeout` deadline, and detach on reply or
       timeout.
    3. On timeout, terminate the worker and spawn a replacement before
       the next iteration. On a normal reply, keep the worker alive —
       no `worker.terminate()` per test.
    4. Terminate the worker once after the loop exits.
- Stage 6B.6 timeout preemption semantics are preserved unchanged:
  `--timeout-ms=MS` is still honoured, timed-out tests still report
  TIMEOUT and count into `summary.fail`, and the main process still
  exits cleanly without hung threads or leaked workers.
- Verification: `node tests/c-engine-browser-runtime.cjs
  build/html-rt/tests/payload.json --json
  --results=build/engine/refactor-stage6b-worker-pool/results.json`
  reports **278 PASS / 0 FAIL / 2 XFAIL / 0 XPASS** — identical to the
  Stage 6B.6 baseline. Wall time **235.6s** (user 242.5s, sys 7.1s)
  vs the Stage 6B.6 run of 328.3s on the same host: a **92.7s / 28.2%
  reduction**, right in line with the deferred item's prediction that
  pool reuse would recover "most of" the ~109s amortised spawn
  overhead. SIMD and bulk-memory fixtures still dominate per-test time
  (10–15s each); pool reuse chips away at fixed per-test overhead, not
  the guest-code bound.
- Deferred: multi-worker parallelism (currently size 1 for
  deterministic output; a `--jobs=N` flag would make the trade-off
  explicit); resident engine bytes (the main thread still
  structured-clones the engine `Uint8Array` into every `postMessage`;
  caching a compiled `WebAssembly.Module` in the worker on first
  message would eliminate that transfer); dynamic timeout budgets per
  group (still open from Stage 6B.6).
- Evidence: `build/engine/refactor-stage6b-worker-pool/summary.txt`,
  `build/engine/refactor-stage6b-worker-pool/run-baseline.log`, and
  `build/engine/refactor-stage6b-worker-pool/results.json`.

Stage 6B.10 browser-corpus multi-worker parallelism (2026-10-03):
- Lands the Stage 6B.9 deferred multi-worker-parallelism bullet. The
  corpus harness now accepts `--jobs=N` (default 1) and dispatches
  tests across N long-lived workers in parallel while keeping console
  output in deterministic submission order.
- `tests/c-engine-browser-runtime.cjs` was reworked so each pool slot
  owns a long-lived Worker driven by an async `slotLoop()`:
    1. `nextIndex` is a shared cursor into `tests`; each loop atomically
       claims an index with `nextIndex++`, so N loops race through the
       queue without duplicate dispatch or coordination.
    2. Each slot runs its claimed test via
       `runOnWorker(worker, spec)` which races the worker reply against
       the `--timeout-ms=` deadline (default 60s). On timeout the slot's
       worker is terminated and respawned; on normal reply the worker is
       kept alive — Stage 6B.9's single-worker reuse semantics are
       preserved within each slot.
    3. Status lines and FAIL/TIMEOUT diagnostics are stashed into an
       `outputs[]` array keyed by submission index; a `flush()` helper
       drains contiguous completed indices from `nextToFlush` upward.
       N>1 therefore never reorders console output — a slot that
       finishes test 12 before test 7 waits for test 7's output to be
       emitted first.
    4. Per-test records are stored in `slotResults[index]` and copied
       into `records[]` in order after `Promise.all(...slotLoop())`
       resolves, so `--results=path` output is also deterministic.
    5. The pool is capped at `min(jobs, tests.length)` so filtered runs
       don't spawn idle slots.
- Verification (smoke): libc-test group with `--jobs=4` reported
  12 PASS / 0 FAIL / 2 XFAIL / 0 XPASS, output lines in exact
  submission order despite out-of-order slot completion.
- Verification (full corpus): `node tests/c-engine-browser-runtime.cjs
  build/html-rt/tests/payload.json --jobs=4 --json
  --results=build/engine/refactor-stage6b-worker-parallel/results.json`
  reports **278 PASS / 0 FAIL / 2 XFAIL / 0 XPASS** — identical to the
  Stage 6B.9 sequential baseline, 280 fixtures reported in submission
  order. Wall time **118.6s** (user 450.5s, sys 14.6s) vs Stage 6B.9's
  235.6s sequential wall on the same host: a **117.0s / 49.6%
  reduction**, a **1.99× speedup** with 4 slots. The user-time
  increase (242.5s → 450.5s) confirms the work actually parallelised
  rather than merely overlapping I/O; the sub-linear wall speedup is
  bounded by the handful of SIMD / bulk-memory fixtures that each run
  10–15s and dominate the critical path.
- Deferred: resident engine bytes (still open from Stage 6B.9 — now
  especially impactful since slots are long-lived and parallel);
  dynamic timeout budgets per group (still open from Stage 6B.6 — the
  SIMD/bulk-memory ceiling is now the dominant critical-path bound);
  native parallel runner (the native harness is still sequential;
  mirroring the same `--jobs=N` + `slotLoop` pattern would shave the
  ~90s native baseline too).
- Evidence: `build/engine/refactor-stage6b-worker-parallel/summary.txt`,
  `build/engine/refactor-stage6b-worker-parallel/run-baseline.log`,
  and `build/engine/refactor-stage6b-worker-parallel/results.json`.

Stage 6B.11 native-corpus multi-process parallelism (2026-10-03):
- Lands the Stage 6B.10 deferred native-parallel-runner bullet. The
  native harness `tests/c-engine-native-runtime.cjs` now accepts
  `--jobs=N` (default 1) and dispatches tests across N parallel
  `waste-wast` child-process invocations while keeping console output
  and `results.json` in deterministic submission order.
- The harness mirrors the Stage 6B.10 browser slotLoop pattern. Unlike
  the browser case there is no long-lived worker to reuse — each test
  already spawns a fresh `build/cli-rt/waste-wast` via `runNative()` —
  so each pool slot is a plain async `slotLoop()` racing on a shared
  `nextIndex` cursor:
    1. `nextIndex` is the shared cursor into `tests`; each loop
       atomically claims an index with `nextIndex++`.
    2. Each slot runs its test via `runNative(sourceAbs)` under the
       existing `--timeout-ms=` deadline and resolves with
       `{ok, timedOut, stdout, stderr, code}`.
    3. Status lines and FAIL/TIMEOUT diagnostics are stashed into an
       `outputs[]` array keyed by submission index; a `flush()` helper
       drains contiguous completed indices from `nextToFlush` upward.
       N>1 therefore never reorders console output.
    4. Per-test records are stored in `slotResults[index]` and copied
       into `records[]` in order after `Promise.all(...slotLoop())`
       resolves, so `--results=path` output is also deterministic.
    5. The pool is capped at `min(jobs, tests.length)` so filtered
       runs don't spawn idle slots.
- Classification (PASS/FAIL/XFAIL/XPASS against
  `tests/native-corpus-expected-failures.txt`, failure diagnostics
  with up to 8 failed assertions, parseError / stderrPreview /
  spawnError capture) is lifted verbatim from the old for-loop into
  the slot body; the record contract and exit semantics are
  unchanged.
- Verification (smoke): libc-test group with `--jobs=4` reported
  12 PASS / 0 FAIL / 2 XFAIL / 0 XPASS in exact submission order.
- Verification (full corpus): `time node
  tests/c-engine-native-runtime.cjs --jobs=4 --json
  --results=build/engine/refactor-stage6b-native-parallel/results.json`
  reports **273 PASS / 0 FAIL / 2 XFAIL / 0 XPASS** — identical to the
  Stage 6B.8 sequential baseline, 275 fixtures reported in submission
  order. Wall time **30.7s** (user 73.7s, sys 30.4s) vs the ~90s
  Stage 6B.8 sequential native wall on the same host: a **~59s /
  65.9% reduction**, a **~2.93× speedup** with 4 slots. The
  (user+sys) ≫ wall relationship confirms the work actually
  parallelised. Sub-linear versus the ideal 4× is bounded by
  per-process startup (each test re-execs `waste-wast`, re-decodes
  the WAST, re-instantiates the engine) and critical-path outliers.
- Deferred: native per-process startup overhead (a persistent native
  worker — either `fork`+IPC or a long-lived child reading WAST paths
  off a pipe — would amortise re-decoding the engine per test,
  mirroring the browser's Stage 6B.9 pool-reuse win); resident engine
  bytes (still open from Stage 6B.9, browser-side); dynamic timeout
  budgets per group (still open from Stage 6B.6).
- Evidence: `build/engine/refactor-stage6b-native-parallel/summary.txt`,
  `build/engine/refactor-stage6b-native-parallel/run-baseline.log`,
  and `build/engine/refactor-stage6b-native-parallel/results.json`.

Stage 6B.12 browser-corpus resident engine bytes (2026-10-03):
- Lands the Stage 6B.9/6B.10 deferred resident-engine-bytes bullet.
  The browser harness previously structured-cloned the ~1-3MB engine
  `Uint8Array` into every per-test `postMessage` and the worker
  re-ran `WebAssembly.instantiate(bytes, imports)` on every message —
  a full compile + instantiate per fixture.  Both costs are now
  amortised across each worker's lifetime.
- Three changes:
    1. `src/html-rt/src/tests-worker.js` caches the compiled
       `WebAssembly.Module` in a module-level `cachedEngineModule`
       on first `runWastScript` and reuses it via
       `WebAssembly.instantiate(cachedEngineModule, imports)` for
       every subsequent test.  Module compile is stateless; each
       test still gets a fresh `instance`, so isolation is
       unchanged.  Dual-source lookup — per-message `wasmBytes` OR a
       `__WASTE_ENGINE_BYTES` global — keeps the file compatible
       with the real browser dashboard (which still postMessages
       `wasmBytes`) while allowing the Node Worker path to omit it.
    2. `tests/c-engine-worker-host.cjs` reads
       `workerData.engineBytes` on spawn and plants it on the vm
       context as `globalThis.__WASTE_ENGINE_BYTES` BEFORE
       evaluating the browser worker source.
    3. `tests/c-engine-browser-runtime.cjs` passes `engineBytes` via
       `workerData` on every `new Worker(...)` (including the
       timeout respawn) and drops `wasmBytes` from the per-test
       `postMessage({testSpec})`.  The main thread never
       structure-clones the engine payload on the hot path again.
- Verification (smoke): libc-test group with `--jobs=4` reported
  12 PASS / 0 FAIL / 2 XFAIL / 0 XPASS in exact submission order.
- Verification (full corpus): `time node tests/c-engine-browser-runtime.cjs
  build/html-rt/tests/payload.json --jobs=4 --json
  --results=build/engine/refactor-stage6b-resident-engine/results.json`
  reports **278 PASS / 0 FAIL / 2 XFAIL / 0 XPASS** — identical to
  the Stage 6B.10 baseline, 280 fixtures in submission order. Wall
  time **119.2s** (user 449.3s, sys 14.4s) vs Stage 6B.10's 118.6s /
  450.5s / 14.6s: a dead heat on wall and ~1s less user time.  The
  architectural win is real but the amortised compile + clone cost
  was already in the single-digit-second budget relative to the
  ~100s guest-execution critical path; SIMD / bulk-memory fixtures
  that take 10–15s each still dominate.  The structural improvement
  (one compile per worker, zero per-test engine clone) is still
  worth banking — the harness's cost structure is now proportional
  to actual guest work and future engine growth won't re-inflate
  per-test overhead.
- Deferred: dynamic timeout budgets per group (still open from
  Stage 6B.6 — now the dominant wall-time ceiling); native
  per-process startup overhead (still open from Stage 6B.11);
  oracle differential comparison across the shared record contract.
- Evidence: `build/engine/refactor-stage6b-resident-engine/summary.txt`,
  `build/engine/refactor-stage6b-resident-engine/run-baseline.log`,
  and `build/engine/refactor-stage6b-resident-engine/results.json`.

Stage 6B.13 browser-corpus per-group timeout budgets (2026-10-03):
- Lands the Stage 6B.6 deferred "dynamic timeout budgets per group"
  bullet.  The harness previously applied a single `--timeout-ms=`
  ceiling (default 60s) to every test, so a hang in any sub-second
  group burned ~55s of wall before detection.  Per-group budgets
  drop that to 5s for Band B groups while Band A groups keep enough
  headroom to run cleanly.
- Stage 6B.12 timing data splits groups into two bands with no
  overlap: Band A (slow, 15-28s per fixture at the max) is
  `core/bulk-memory`, `core/simd`, `core/memory64`, `core`; Band B
  (fast, all under ~1.5s) is `core/gc`, `core/multi-memory`,
  `core/exceptions`, `core/relaxed-simd`, `libc-test`,
  `diy-posix-test`, and the three `custom/*` groups.  Budgets add
  ~2x headroom over the observed max: 60s for bulk-memory, 50s for
  simd and memory64, 35s for core, 5s for every Band B group;
  unknown groups fall through to `--timeout-ms=` (default 60s).
- Three changes to `tests/c-engine-browser-runtime.cjs`:
    1. Added a `GROUP_TIMEOUT_MS` constant with the per-group table
       plus the comment above explaining why those numbers.
    2. Added `--timeout-group=NAME:MS` CLI parsing that writes
       directly into `GROUP_TIMEOUT_MS` before any test runs — CI
       can tune individual groups without touching the file.
    3. `runOnWorker` now takes `budgetMs` as a parameter; the slot
       loop resolves it per test via `timeoutFor(test.group)` and
       threads it through.  The TIMEOUT diagnostic reports the
       actual `budgetMs` used, not the global default.
- Verification (smoke): `--group=libc-test --jobs=4` under the 5s
  default budget reported 12 PASS / 0 FAIL / 2 XFAIL / 0 XPASS.
- Verification (override path): `--group=libc-test
  --timeout-group=libc-test:50 --jobs=4` reported 0 PASS / 14 FAIL
  with every fixture surfacing a `{type: "timeout", timeoutMs: 50}`
  diagnostic and non-zero exit — confirming the override is
  actually threaded into the slot loop and the TIMEOUT path still
  trips cleanly.
- Verification (full corpus): `time node tests/c-engine-browser-runtime.cjs
  build/html-rt/tests/payload.json --jobs=4 --json
  --results=build/engine/refactor-stage6b-group-timeouts/results.json`
  reports **278 PASS / 0 FAIL / 2 XFAIL / 0 XPASS** — identical to
  Stage 6B.12, 280 fixtures in submission order. Wall time
  **118.5s** (user 447.7s, sys 14.8s) vs Stage 6B.12's 119.2s /
  449.3s / 14.4s: a dead heat on wall. Expected — these budgets
  don't change happy-path timing, only unhappy-path (hang)
  detection latency.
- Hang-detection latency delta: a hang in a Band B test now
  surfaces as FAIL after **5s instead of 60s — a 12× tightening**
  of the feedback loop for the common development case.  Band A
  groups retain the original 50-60s ceilings so no legitimate slow
  fixture regresses.
- Deferred: native per-process startup overhead (still open from
  Stage 6B.11); native per-group timeouts (the same
  `GROUP_TIMEOUT_MS` table trivially applies to
  `tests/c-engine-native-runtime.cjs`; mirror once the native
  runner has a parallel baseline worth protecting); oracle
  differential comparison across the shared record contract.
- Evidence: `build/engine/refactor-stage6b-group-timeouts/summary.txt`,
  `build/engine/refactor-stage6b-group-timeouts/run-baseline.log`,
  and `build/engine/refactor-stage6b-group-timeouts/results.json`.

Stage 6B.14 native-corpus per-group timeout budgets (2026-10-03):
- Mirrors Stage 6B.13 into the native harness.  Previously
  `tests/c-engine-native-runtime.cjs` applied a single
  `--timeout-ms=` ceiling (default 30s) to every test, so a hang in
  a sub-second group would still burn ~30s of wall before detection.
  The native harness now assigns budgets per group that reflect
  native's measured per-test speed.
- Native is substantially faster than the browser worker.  Stage
  6B.11 timing data gives max 7.7s for `core`, max 4.2s for
  `core/simd`, max 2.2s for `core/bulk-memory`, max 1.9s for
  `core/memory64`, and max 1.1s or below for every other group.
  Budgets at ~2x observed max: `core` 15s; `core/simd`,
  `core/bulk-memory`, `core/memory64` 10s; all Band B groups 5s;
  unknown groups fall through to `--timeout-ms=` (default 30s).
- Three changes to `tests/c-engine-native-runtime.cjs`, structurally
  identical to the Stage 6B.13 browser slice:
    1. Added `GROUP_TIMEOUT_MS` constant plus `--timeout-group=NAME:MS`
       CLI parsing.
    2. `runNative(sourceAbs, budgetMs)` now takes the budget as a
       parameter and uses it for its `setTimeout` deadline.
    3. The slot loop resolves `budgetMs = timeoutFor(test.group)`
       per test and passes it through.  Classification, diagnostics,
       and exit semantics are unchanged.
- Verification (smoke): `--group=libc-test --jobs=4` under the 5s
  default budget reported 12 PASS / 0 FAIL / 2 XFAIL / 0 XPASS.
- Verification (override path): `--group=libc-test
  --timeout-group=libc-test:50 --jobs=4` reported 0 PASS / 14 FAIL,
  confirming the override is threaded through and the TIMEOUT path
  still trips.
- Verification (full corpus): `time node tests/c-engine-native-runtime.cjs
  --jobs=4 --json --results=build/engine/refactor-stage6b-native-group-timeouts/results.json`
  reports **273 PASS / 0 FAIL / 2 XFAIL / 0 XPASS** — identical to
  Stage 6B.11, 275 fixtures in submission order.  Wall time
  **30.6s** (user 74.7s, sys 28.5s) vs Stage 6B.11's 30.7s / 73.7s /
  30.4s: dead heat on wall.  Expected — these budgets don't change
  happy-path timing, only unhappy-path (hang) detection latency.
- Hang-detection latency delta: a hang in a native Band B test now
  surfaces as FAIL after **5s instead of 30s — a 6× tightening** of
  the feedback loop.  Band A groups retain the original 10-15s
  ceilings.
- Deferred: native per-process startup overhead (still open from
  Stage 6B.11 — a persistent child reading WAST paths off a pipe
  would amortise per-test re-decode of the engine); oracle
  differential comparison across the shared record contract.
- Evidence: `build/engine/refactor-stage6b-native-group-timeouts/summary.txt`,
  `build/engine/refactor-stage6b-native-group-timeouts/run-baseline.log`,
  and `build/engine/refactor-stage6b-native-group-timeouts/results.json`.

Stage 6B.15 native-corpus server-mode persistent child (2026-10-03):
- Lands the Stage 6B.11/6B.14 deferred "native per-process startup
  overhead" bullet.  Harness previously `spawn`-ed a fresh
  `build/cli-rt/waste-wast` child per test; each one paid exec,
  dynamic linker, static init, stdio buffer setup, and per-process
  mmap overhead before touching a single assertion.  With 275 tests
  on 4 slots that fixed cost dominated sys time.  The harness now
  runs ONE long-lived child per slot in a new `--server` mode that
  reads WAST paths from stdin and replies with JSON + sentinel per
  request.
- `src/cli-rt/main.c` grows a `run_server()` entry point: a
  `getc(stdin)` byte loop into a 4KB line buffer dispatches each
  line to the existing `run_normal(line, NULL)` and emits
  `###END###\n` on both stdout and stderr afterwards.
  Freestanding stdio lacks `fgets`/`fflush`, so the loop is
  primitive but equivalent.  `run_normal` is unchanged — it still
  allocates its own `native_store`, encodes modules, instantiates
  engines, runs assertions, and frees everything before returning,
  so test-to-test isolation inside the long-lived process is
  byte-for-byte identical to the one-shot path.  The only
  persisting state is already-loaded `.text`, `.rodata`, and the
  allocator free-list — exactly the fixed cost worth amortising.
- `tests/c-engine-native-runtime.cjs` was reworked to pool one
  server per slot:
    1. `spawnServer()` opens `spawn(runnerPath, ["--server"], ...)`
       and streams stdout and stderr into per-child buffers.  The
       `tryDeliver(state, stream)` helper scans each buffer for
       `"###END###\n"` and splits the per-test chunk off before the
       sentinel.
    2. `runNative(server, sourceAbs, budgetMs)` registers a pending
       request with a budget timer, writes `sourceAbs + "\n"` to
       stdin, and resolves once BOTH the stdout and stderr
       sentinels have arrived — so stderr accumulated between
       sentinels is attributed to the right test for FAIL
       diagnostics.
    3. On timeout, the slot `SIGKILL`s its child, resolves with
       `timedOut: true`, and the slot loop calls `spawnServer()`
       again before the next iteration — same preemption semantics
       as the browser slotLoop (Stage 6B.10).
    4. The slot loop calls `child.stdin.end(); child.kill("SIGTERM")`
       on clean exit so no leftover processes outlive the harness.
- Verification (server-mode spot check): `printf 'core/i32.wast\n' |
  build/cli-rt/waste-wast --server` emits valid JSON
  `{"file":"i32.wast","assertions":[...],"passed":459,"total":459}`
  followed by `###END###\n` on both stdout and stderr.
- Verification (smoke): `--group=libc-test --jobs=4` reported
  12 PASS / 0 FAIL / 2 XFAIL / 0 XPASS.
- Verification (full corpus): `time node tests/c-engine-native-runtime.cjs
  --jobs=4 --json --results=build/engine/refactor-stage6b-native-server/results.json`
  reports **273 PASS / 0 FAIL / 2 XFAIL / 0 XPASS** — identical to
  every baseline since Stage 6B.8, 275 fixtures in submission order.
  Wall time **25.3s** (user 68.7s, sys 11.5s) vs Stage 6B.14's 30.6s
  / 74.7s / 28.5s: wall **5.3s / 17.3%** reduction, user 6.0s /
  8.0% reduction, sys **17.0s / 59.6%** reduction.  The sys-time
  collapse is the clearest signal that fixed startup cost was
  eliminated — fewer execves, no per-test ELF loader runs, no
  re-fault of the engine's `.text` and `.rodata`, no stdio
  re-init.  Wall-time improvement is bounded by the Band A outliers
  that still dominate the critical path.
- Deferred: dynamic test re-ordering (the critical path is now the
  Band A outliers — scheduling them to start first across the slot
  pool would let Band B tests fill in behind them); oracle
  differential comparison across the shared record contract.
- Evidence: `build/engine/refactor-stage6b-native-server/summary.txt`,
  `build/engine/refactor-stage6b-native-server/run-baseline.log`,
  and `build/engine/refactor-stage6b-native-server/results.json`.

Stage 6B.16 duration-guided corpus dispatch (2026-10-03):
- Adds shared `tests/corpus-schedule.cjs` selection of dispatch indices to
  both corpus harnesses. `--schedule=longest-first --timings=PATH` consumes
  a previous `--results` file and starts tests in descending `elapsedMs`
  order. Default dispatch remains manifest order. Timings should come from
  the same runtime and comparable worker count; they are estimates, not
  execution budgets or evidence of a speed improvement.
- Dispatch changes only after existing file/group/exclusion filtering.
  Console output, `--list` and result records retain manifest order. Ties
  retain that order, missing timings have an estimate of zero, and history
  for removed or unselected tests does not schedule extra tests. Invalid
  durations, duplicate identities and incompatible options fail before
  starting execution.
- `tests/corpus-schedule-check.cjs` passed focused checks of historical
  ordering, ties, missing/stale identities, validation, actual native and
  browser harness reporting, negative selection, and timeout replacement.
  Synthetic native dispatch uses two slots; browser checks use a resident
  worker and verify its dispatch sequence. Both replace a hung backend
  and report the following tests successfully in manifest order.
- Full native/browser corpus verification is **pending**. The native
  full-corpus command was interrupted at its sandbox approval request;
  no full-run result or performance claim is recorded for this slice.
  Do not treat Stage 6B acceptance as passed.

Stage 6B.17 Node-independent native manifest runner (2026-10-03):
- Adds CLI-owned `waste-test` and `make -C src/cli-rt corpus-native`.
  The build uses the shared engine and native parser objects with system
  libc for child-process supervision. The target audits/encodes installed
  `src/vfs` snapshots and runs mounted `/tests/manifest.json`; it does not
  build the browser, generate dashboard payloads, refresh snapshots or run
  Node. An existing WVFS image needs only the native executable.
- `src/engine/test_suite.{c,h}` decodes the existing format-1 manifest using
  bounded JSON views, a nesting bound, explicit test/asset/string limits,
  UTF-8/escape validation and owned records. It rejects duplicate identities,
  conflicting required fields, invalid path/group contracts and unsupported
  manifest versions. Host provenance and embedded WAST/base64 data are
  validated as metadata, never used to locate or execute test sources.
  Native sources and companion bytes come exclusively from mounted paths.
- `src/cli-rt/native_wast.{c,h}` shares the existing native import adapters
  and assertion executor between `waste-cli` and `waste-test`. Assertions
  now enter the existing shared process driver, which binds the active
  process memory needed by the mmap fixture. Single-file/browser-spec/count/
  parse-only/server entry points remain available. No engine instruction,
  parser grammar, POSIX capability or OCaml submodule change is introduced.
- The batch adapter supports `--list`, repeatable group/file/exclusion
  selection, `--jobs=1..64`, global and per-group deadlines, SIGINT/SIGTERM
  cancellation, an explicit tracked expected-failures input, stdout summary
  JSON and `--results=PATH`. Defaults are 15s for core, 10s for SIMD,
  bulk-memory and memory64, and 5s elsewhere. Unlike the older Node harness,
  `--timeout-ms=N` overrides all defaults; per-group overrides take precedence.
- Each native child mounts a fresh store/kernel from the same immutable
  image bytes and stages declared `vfs-file` companions inside that guest
  namespace. Imported-memory aliases within one script remain shared. A
  deadline kills/reaps the test child and the suite continues; cancellation
  reaps active children and writes an interrupted report. Output/records stay
  in manifest order across jobs. Captured assertion/diagnostic files are
  bounded; partial reports after interruption or a crash are not embedded
  as valid JSON. Infrastructure failures cannot become expected failures.
- Records preserve the previous identity/status/expectation/group/file/mode/
  elapsed/count fields, add `nativeReport` containing every assertion, and
  include errors/skip reasons. Aggregate results add `skip` while preserving
  failures and unexpected-pass identities. FAIL/TIMEOUT/CANCELLED and XPASS
  fail the run; configured XFAIL and explicit SKIP do not. Reporting/config
  errors exit 2; cancellation returns the signal exit status.
- First full execution exposed the libc missing-path fixture's assumption
  that `/bin/ls` did not exist. Its authored client now checks
  `/tmp/waste-missing-path`, which is absent with the installed command
  namespace. Guest libc was rebuilt and `vfs-tests-install` /
  `vfs-tests-check` refreshed/audited the distribution explicitly. Only the
  `path-runtime.wast` libc snapshot changed bytes; corpus identities, source
  selection and runtime modes remain unchanged.
- Verification: native full corpus and ASan/UBSan full corpus both report
  **274 PASS / 0 FAIL / 2 XFAIL / 0 XPASS / 8 SKIP**. The 275 old native
  identities preserve their classifications and assertion counts; mmap adds
  **23 passing assertions**. Four legacy and four browser-compatibility
  fixtures formerly omitted by the native harness are now explicit skips.
  The normal four-job run took 24.3s on this host; this is a different mount/
  execution workload from earlier baselines, not a claimed speedup.
- Portable guest regressions are authored as `tests/test-suite-*.wast`;
  existing spectest isolation WAST is reused. The remaining Python check
  exercises the native host boundary: selection, report fidelity, XFAIL/
  XPASS/SKIP, private kernel state, intentional within-script memory aliases,
  cross-script memory isolation, companion bytes, timeout recovery,
  cancellation, missing/invalid inputs, UTF-8 identities and malformed/bounded
  manifest rejection. Normal and sanitizer checks pass; use
  `make -C src/cli-rt test-suite-check` for the sanitizer gate.
- Focused compiled-browser verification passes all seven portable fixtures,
  including the refreshed libc path probe and companion bytes. The portable
  manifest decoder also compiles for wasm32 with warnings as errors. It is
  not connected to the production browser batch API at this slice; Stage
  6B.18 below supplies that integration. This native stage did not authorize
  dashboard retirement.
- Oracle comparison: the existing native OCaml executable passes **258/261**
  installed official/custom fixtures. `core/custom.wast`,
  `core/return_call.wast` and `core/return_call_ref.wast` exceeded the 20s
  per-file comparison budget; an earlier attempt also timed out on
  `core/custom.wast` at 90s. These are incomplete comparisons, not C failures
  or new XFAIL entries. All three pass in C. Full oracle acceptance remains
  open, as do command-level streaming/recovery parity and scripted-session
  batch integration.
- Commands and result semantics are documented in `docs/test-corpus.md`.
  Evidence: `build/engine/refactor-stage6b-native-suite/{run.log,results.json,
  make-corpus.log,baseline-comparison.txt,focused.log,sanitize-focused.log,
  sanitize-run.log,sanitize-results.json,browser-fixtures.log,
  browser-fixture-results.json,oracle-results.json,corpus-install.log}` and
  `build/cli-rt/corpus-results.json`. Shell/Python syntax and whitespace
  checks pass.

Stage 6B.18 production-browser mounted batch execution (2026-10-03):

- The offline shell page now exposes **Diagnostics → Installed tests**:
  list the installed corpus, select groups/files, run parallel isolated tests,
  cancel a batch and download assertion-aware JSON. Ordinary browser runs
  require no Node process. The terminal retains its own production worker
  throughout enumeration, execution and cancellation.
- `browser_api.c` links `src/engine/test_suite.c`, mounts the boot image in a
  temporary catalogue kernel and decodes `/tests/manifest.json`. Bounded field
  exports expose the decoded identities, modes, skips and expected-failure
  policy. Execution preparation reads the chosen WAST and `vfs-file` companions
  through mounted paths and stages companion bytes for the fresh execution
  kernel. Source provenance, inline WAST and base64 metadata are not fallbacks.
- `worker.js` adds list/run requests to its production protocol. Each runnable
  test receives a fresh worker, C instance, store/kernel and identical immutable
  WVFS bytes. `test-suite.js` owns browser scheduling and worker lifetimes;
  it neither interprets WAST nor resolves guest paths. Positive group/file
  selectors combine, exclusions apply afterwards, and downloaded records retain
  manifest order across jobs. Unknown selections and unknown/duplicate tracked
  expected failures reject before test dispatch.
- PASS/FAIL/XFAIL/XPASS/TIMEOUT/CANCELLED/SKIP use the native identity, count,
  timing and `summary`/`tests` contract. `browserReport` keeps completed
  per-assertion results and diagnostics. Missing source/companions, mounting
  failures, worker crashes and interrupted/partial reports cannot become XFAIL.
  The report's aggregate `exitCode` is 0, 1 or 130 for accepted outcomes,
  regressions/XPASS or cancellation. Compatibility backends remain explicit skips.
- Parent watchdogs bound worker creation, Wasm instantiation, manifest/source
  preparation, parsing and execution, even before a guest dispatch safepoint.
  Active workers terminate on cancellation/deadline; undispatched runnable
  tests become CANCELLED while unsupported entries remain SKIP. Engine execution
  retains ordinary-return pump yields and explicit cancellation/deadline state.
  API group defaults match native 15s/10s/5s budgets; the page chooses a 60s
  global budget and two jobs, with a bounded 1–8 job control.
- Full production-worker coverage exposed the shell worker's former thin
  `Number` float conversion. It now uses the dashboard's established exact
  hexadecimal f32/f64 and decimal f32 rounding routines. The legacy dashboard
  retains its helper copy until consolidation/retirement; it remains a distinct
  compatibility harness, not the production batch execution path.
- The browser API now owns a copy of each pending WAST assertion and its
  originating instance across evaluator yields. Resume drives the selected
  process continuation, then checks the original expected return/trap/exhaustion
  through the shared assertion evaluator. It no longer records yielded
  assertions as generic `main` completions. The worker also polls SELECT waits
  so kernel deadlines can expire without an outside terminal event; the browser
  kernel's clock callback is bound for normal as well as deterministic runs.
- The authored `environment-boundaries` libc client now uses explicit zero
  timeval/timespec values for readiness polls. Its old null timeout asked for
  an indefinite wait while expecting immediate zero; actual production resume
  correctly exposed that mismatch. Rebuilt libc fixtures were explicitly
  installed/audited with `vfs-tests-install`/`vfs-tests-check`. The two tracked
  XFAIL identities remain because other unsupported boundary assertions fail.
- Portable guest checks reuse `tests/test-suite-*.wast` and existing spectest
  isolation clients. New `test-suite-yield.wast` checks long return/trap
  assertions and an intentionally wrong expected value across pump yields;
  `test-suite-select-timeout.wast` checks finite SELECT/PSELECT waits. The
  focused browser boundary gate covers reporting, companion bytes, kernel/memory
  isolation, selection, XFAIL/XPASS/SKIP, input rejection, UTF-8 identities,
  watchdog preemption, cancellation and a subsequent successful batch.
- Packaging explicitly embeds the tracked browser expected-failure list as a
  host asset, keeps it outside the guest inventory/image, and inlines the new
  controller into the single offline page. Packaging still only reads installed
  guest snapshots; it does not compile tests or discover frontend guest binaries.
- Verification: full production-browser mounted execution and the refreshed
  native corpus both report **274 PASS / 0 FAIL / 2 XFAIL / 0 XPASS / 8 SKIP**.
  The actual `file://` gate also passes listing, selected runs, cancellation,
  a later Bash command, assertion reports and no external runtime requests.
  Focused browser boundary checks, native manifest/driver ASan/UBSan checks,
  the refreshed libc boundary fixture under ASan/UBSan, installed-corpus
  audits, flattened/offline packaging guards and repeated shell WAT/WAST
  direct/shebang execution all pass. Browser screenshots were inspected.
  Shell/Python/JavaScript syntax and whitespace checks pass.
- All **284 identities/classifications** agree between native and browser.
  At this slice, three pre-existing native full-parser reporting gaps remained: browser counts
  133 versus native 123 in `core/linking.wast`, 4 versus 2 in
  `core/multi-memory/linking0.wast`, and 10 versus 4 in
  `core/multi-memory/linking3.wast`. Native grouping skips successful actions
  following module-link assertions; the browser stream executes/reports those
  actions. These are not new XFAIL entries and must be closed in the native
  command-stream parity slice rather than suppressing browser records. Stage
  6B.19 below closes these gaps.
  The retained legacy browser harness still reports **278 PASS / 2 XFAIL**:
  all **276 C-engine classifications** match the new runner; its four
  browser-native compatibility cases still pass there and remain explicit
  new-runner skips. Its four omitted unsupported identities are now visible
  as skips. This comparison preserves that distinct coverage until migration.
  Evidence: `build/engine/browser-suite-{build,focused,install,package,
  dashboard-package,packaging-check,native-check,native-corpus,libc-sanitize,
  shell-check,offline,legacy}.log`, `build/engine/browser-suite-libc-sanitize.json`,
  `build/engine/browser-suite-{comparison,legacy-comparison}.txt`,
  `build/engine/browser-suite-legacy-results.json`, `build/cli-rt/corpus-results.json`
  and `build/html-rt/offline-browser-check/{mounted-suite-results.json,
  bash.png,tests.png}`.
  Stage 6B acceptance remains open: the guest `/bin/waste-test` launcher,
  compatibility fixtures, scripted-session contract, coverage accounting and
  complete oracle comparison still precede dashboard retirement.

Stage 6B.19 landing slice — native command-stream assertion parity (2026-10-03):
- Replaced the native CLI/batch driver's full-file group traversal with the
  engine's balanced WAST command scanner, also used by the browser. Actions
  execute and report immediately against the current or explicitly named
  instance. Module assertions leave the current instance intact; registration
  updates the selected provider immediately. Definitions are retained as
  templates and each instance is encoded/instantiated afresh.
- Native assertion records include the command's source line and retain
  ordered indices, names, outcomes and errors. Balanced parse failures produce
  failed `(parse)` records and recover at the next command; an unterminated
  tail preserves earlier results and fails the file. The root parse-error
  field remains available to existing report consumers.
- Parsed metadata for definitions and live instances stays owned while the
  store borrows it; temporary assertion parses are released after execution.
  Superseded anonymous instances release metadata, while registered instances,
  active process engines and engines whose funcrefs may remain in imported
  tables preserve their necessary lifetimes. Native retained parses are bounded
  at 4096; allocation/retention failures are explicit failed results.
- The shared parser initially allocates one zeroed group, rather than reserving
  eight and separately clearing the first. This keeps command-only parses from
  touching large unused module arrays on native hosts with demand-zero calloc,
  preserving the same zero initialization and subsequent growth semantics.
  Parser counters/owned state are also separated from bounded scratch arrays:
  each context clears its state, while name/fixup/branch-label entries are
  initialized before consumption under their live counts. This avoids clearing
  approximately 5 MB of unused workspace for each short command, without
  introducing shared mutable parser state or changing deadline defaults.
- Portable `tests/test-suite-command-stream{,-fail,-recovery}.wast` fixtures
  cover actions after invalid/malformed/unlinkable modules and trapping starts,
  named/current selection, registration, anonymous replacement, definitions,
  a deliberately wrong post-module expectation and balanced parse recovery.
  Native Python and browser worker gates check serialization and host status;
  the guest assertions themselves remain WAST. These focused harness fixtures
  are authored sources, separate from the installed distribution snapshots.
- Per-assertion comparison also exposed eight browser name-reporting differences
  in `core/names.wast`: long names were truncated and a leading UTF-8 BOM was
  dropped during decoding. The browser API now provides bounded full-name
  pointer/length exports, and both production and retained dashboard workers
  preserve those names and BOM characters. The old fixed result layout remains
  intact for older host probes. `test-suite-result-names.wast` covers this
  reporting boundary in both runtimes. The retained dashboard automation now
  saves every completed assertion when a result file is requested, including
  successful actions, enabling comparison beyond file classifications.
- Ordinary module setup errors retain the existing native/browser assertion
  policy: they contribute no standalone assertion result, and native stderr
  retains their diagnostics. Full execution surfaced existing setup limitations
  involving segment-count bounds, active element segments and the `table64`
  spectest provider. Passing assertion totals do not establish acceptance of
  every ordinary module; a separate setup-coverage audit remains required.
- Verification: native execution with the unchanged default deadlines and full
  production-browser execution both report **274 PASS / 0 FAIL / 2 XFAIL /
  0 XPASS / 8 SKIP**. All **284 mounted identities**, manifest order and
  classifications agree. All **63,054 ordered assertion names/outcomes** agree
  across the **276 C-engine scripts** in native, production-browser, retained
  dashboard and ASan/UBSan execution. The three linking gaps are closed at
  **133/133**, **4/4** and **10/10** in both runtimes. The retained dashboard
  still reports **278 PASS / 2 XFAIL**, preserving its four browser-native
  compatibility passes separately from new-runner skips.
- Native warnings-as-errors builds and both focused boundary gates pass. The
  full ASan/UBSan corpus used nonzero allocation poisoning through the complete
  parser workspace; one `memory_copy64.wast` run exceeded its 60s budget under
  concurrent load and passed alone with a 180s budget in 58.2s. All resulting
  assertion records match the normal run, with no sanitizer errors. Normal
  native deadlines were not increased. Actual offline Chromium execution
  passes both pages, full mounted batches, cancellation, subsequent Bash use
  and no external runtime requests. Packaging, shell/Python/JavaScript syntax
  and whitespace checks pass. Installed corpus identities/snapshots and
  expected-failure lists are unchanged by this slice.
- Repeated official/custom oracle comparison gives **258/261 accepted scripts**;
  `core/custom.wast`, `core/return_call.wast` and `core/return_call_ref.wast`
  retain their existing 20s oracle timeouts. All three linking files pass the
  oracle. The oracle also accepts both positive portable fixtures and rejects
  the intentionally wrong post-module expectation. Complete oracle acceptance,
  ordinary setup-module coverage, guest launcher/session parity and fixture
  accounting remain open before dashboard retirement.
- Evidence: `build/engine/refactor-stage6b-native-stream/` contains native,
  browser and sanitizer build/gate logs, `native-results.json`,
  `browser-results-final.json`, `legacy-results.json`, `sanitize-results.json`,
  `sanitize-retry.json`, `oracle-{results,fixtures}.json`,
  `cli-linking-counts.json` and `comparison.txt`. Actual offline results and
  screenshots remain in `build/html-rt/offline-browser-check/`.

Stage 6B.20 landing slice — installed guest batch launcher (2026-10-03):
- `src/html-rt/commands/waste-test.c` builds a freestanding guest Wasm command.
  Explicit `guest-test-install` publishes matching executable snapshots at
  `/usr/bin/waste-test` and `/bin/waste-test`; the audited inventory now has
  491 nodes. Packaging reads the installed snapshots and does not compile them.
- The guest accepts list/group/file selection, exclusions, 1–8 jobs, global
  and group deadlines, `--json`, and `--results=GUEST_PATH`. Its versioned
  `waste_kernel.test_suite_v1` request uses the existing host-I/O yield/resume
  path: a 4-byte little-endian version followed by NUL-terminated arguments,
  bounded to 4 KiB/64 arguments. Replies have a 16-byte little-endian
  version/status/output-length/JSON-length header and at most 16 MiB total.
  The engine checks guest reply ranges and permissions before dispatch,
  copies owned request/reply bytes, and releases them after resumption.
- The native session runs/reaps its sibling batch executable in a dedicated
  process group; the production browser shell delegates to the installed
  controller and fresh workers. The runtime selects the original boot image
  and tracked XFAIL policy. The native session snapshots image bytes in an
  anonymous file, so replacing the host image during a shell session cannot
  change a batch. Guest flags cannot select host executables, images,
  manifests or policy/report paths. Fresh batch stores leave the capability
  disabled, preventing recursive privileged batches.
- The guest writes human output/JSON to its own descriptors and saves reports
  through `open_v1`; shell redirection and guest file permissions apply.
  Invalid-option diagnostics use stderr. Exit codes are 0 for accepted
  results, 1 for failures/XPASS/timeouts, 2 for invalid/unavailable requests,
  and 130 for cancellation. `/bin/download /tmp/results.json` uses the existing
  download capability to export saved guest reports.
- Ctrl-C cancels the batch while preserving Bash. Native cancellation kills
  and reaps the supervisor/test process group; browser cancellation terminates
  active worker requests, including pending enumeration. Both can return an
  explicit `cancelledBeforeEnumeration` report. Browser requests carry sequence
  IDs; input/resize events cannot prematurely resume a pending batch, and
  stale replies/cancels cannot apply to a later request. Session-wide native
  deadlines/external cancellation also reap the batch.
- Shared `guest-session-test-suite.json` exercises listing, JSON stdout/result
  equality through guest files, two executed tests with two jobs, visible
  compatibility skips,
  timeout and option errors, expected failures, Ctrl-C and subsequent parent
  shell output. `guest-test-launcher-check.py` compares native/production-worker
  summaries, identities, statuses and assertion counts; it does not treat
  Bash's exit code alone as assertion coverage. The portable capability WAST
  probes cover malformed requests, host-path flags, job limits, invalid reply
  ranges and disabled nested batches. The native boundary also replaces the
  host image after the first prompt and verifies execution from the boot
  snapshot. Closing native terminal input during a batch is also propagated
  to the guest, letting Bash observe EOF after the reply. Host adapter checks
  remain focused.
- The sanitized guest run exposed signed negation of `INT64_MIN` in the
  shared lexer. Negating the parsed unsigned magnitude before converting its
  bits to `i64_val` preserves the valid decimal/hex minimum literals without
  C undefined behavior. `test-suite-i64-min-literal.wast` covers both forms;
  the OCaml oracle passes that probe and official `int_literals`, `const`
  and `i64` scripts.
- Verification passed: native CLI/session/browser builds; normal and
  ASan/UBSan native batch boundary gates; normal and sanitized guest launcher
  parity (including the immutable-image and EOF probes); the capability probe
  (five rejected requests and explicit exit); disabled nested-batch and signed-minimum WAST probes; existing
  Bash/Coreutils/transfer/provider-isolation session regressions; frontend/VFS
  package guards; actual offline `file://` shell/dashboard startup, guest
  reports and cancellation with no external requests; shell/Python/JS syntax
  checks and `git diff --check`. LeakSanitizer stayed enabled for the final
  guest checks, run outside the sandbox because its process inspection is
  blocked inside it. Final native builds used Clang with warnings-as-errors.
- Full native and production-browser corpus runs each report **274 PASS,
  2 XFAIL, 8 SKIP**, with all **284 identities** and **63,054 ordered assertion
  names/outcomes** matching. Two native deadlines hit while compilation was
  active; the final run after builds finished passed at the normal budgets.
  Evidence, including `full-parity.json`, guest transcripts/reports, oracle
  literal checks and gate logs, is under
  `build/engine/refactor-stage6b-guest-suite/`.
- Stage 6B acceptance remains open. Next slice: audit authored C/CJS checks by
  assertion, retain host/private invariants, and move guest-observable coverage
  to installed WAST/session contracts. The four compatibility modes, ordinary
  setup-module diagnostics and oracle completion still need their own gates;
  Stage 6C consolidation and Stage 7 dashboard retirement remain pending.

Stage 6B.21 landing slice — retire WVFS in favor of tree/tar inputs (2026-10-03):
- User direction: retire the custom `.wvfs` serialization. `waste-cli`,
  `waste-session` and native `waste-test` accept `--vfs-root DIRECTORY` and
  read declared files directly from the installed host tree. `corpus-native`
  and guest-launcher checks no longer create `installed-vfs.wvfs`.
- `src/cli-rt/native_vfs.c` owns Linux directory I/O, including the
  freestanding CLI backend. Component-by-component `openat`/`O_NOFOLLOW`
  keeps reads beneath the open root; missing inventory-only empty directories
  remain valid. Nonregular files, missing/changed contents and symlink
  redirection fail before mounting. Native buffers transfer ownership to the
  shared catalogue without making another file-content copy.
- `src/engine/vfs.c` now consumes the existing JSON inventory and separate
  file buffers. It preserves exact modes, UID/GID, inode identities and signed
  seconds/nanoseconds, checks SHA-256 hashes, and retains the 960-entry/64-MiB
  bounds plus a 2-MiB inventory bound. Complete inputs are required before a
  fresh kernel is mounted. The suite and inventory decoders reuse the same
  bounded JSON reader in `src/engine/lib/json.h`; no tar parser enters C.
- The browser loader reads guest bytes from its existing tarballjs map;
  workers receive inventory plus individual buffers through structured
  messages and stage them through the C API. `vfs-image.bin`, the WVFS
  exports/encoder and archive-to-WVFS conversion are removed. The standard
  tar package contains one guest tree plus inventory. A local tarballjs
  adapter handles ustar prefixes/full-width names and checks checksums,
  member bounds, duplicate/unsafe paths and supported types. The upstream
  submodule remains unchanged; synchronous tar errors propagate through boot.
- Guest mutations stay in private engine files. Native test children inherit
  a validated in-memory catalogue; browser tests use fresh workers. The
  native shell passes an open root directory to its sibling batch companion,
  preserving directory identity across host rename/replacement without an
  anonymous serialized image. External content edits are revalidated at each
  batch launch; the root descriptor does not freeze file bytes. Guest arguments
  cannot select host roots, executables or policy/report paths, and host reports
  cannot replace installed inputs.
- Replaced the WVFS capacity/byte tests with installed-inventory/directory
  probes. Updated packaging, native/browser sessions, batch fixtures, guest
  launcher checks and current architecture/SDK/session/corpus documentation.
  `make -C src/cli-rt vfs-check` is the native mount gate. Historical landing
  evidence is retained, while obsolete generated boot bundles are removed.
- Full native and actual production-browser corpus runs each report
  **274 PASS, 2 XFAIL, 8 SKIP**. All **284 identities** and **63,054 ordered
  assertion names/outcomes** match. Evidence is under
  `build/engine/refactor-stage6b-vfs-retirement/`.
- Validation covers Clang warnings-as-errors, ASan/UBSan inventory capacity,
  all installed bytes/metadata, host preservation and kernel isolation;
  direct-directory malformed/hash/symlink/FIFO boundaries; native/browser
  batch selection, reports, companions and cancellation; guest launcher
  redirection, pinned root identity and EOF; compiled browser SDK/stat probes
  and all 284 mounted paths; tarballjs prefix/checksum/path bounds;
  frontend/package guards and actual offline `file://` pages. LeakSanitizer
  session checks run outside the tracing sandbox. Final native/browser builds,
  shell/Python/JS syntax checks and `git diff --check` pass. The regenerated
  Bash page embeds a 3,683,420-byte gzip tar containing the single guest tree.
- The remaining Stage 6B priorities are unchanged: C/CJS coverage accounting,
  portable guest/session fixtures and completion of the differential oracle
  comparison. This loading cutover does not retire the dashboard or claim
  completion of 6B, 6C or Stage 7.

Stage 6B.22 landing slice — executor assertion audit and portable WAST fixtures (2026-10-03):
- Audited the guest-observable smoke sequence in
  `tests/c-engine-i32-smoke.c`. Its 32 main-sequence assertions now live in
  authored `tests/engine-regressions/i32-smoke.wast`, preserving the module body,
  arguments, order, result types/counts and trap expectations. Removed those
  language-only checks and their pair/scalar helper functions from the C
  driver. `docs/test-coverage.md` maps each removed assertion to its WAST
  ordinal; it also records the retained callback, API and ownership gates.
- Added `extern-aliases.wast` and `instance-isolation.wast`, each with 16
  assertions. The former preserves six guest-visible alias observations and
  adds re-exported memory/global mutation, shared table growth and indirect
  calls before/after clearing a slot. The latter preserves nine fresh-instance
  observations and reverses writer/reader roles for seven additional checks.
  WAST does not claim decode-once/instantiate-twice or pointer identity coverage.
- Retained C `test_public_api`, real `host_add` callback/manual import bindings,
  global/memory/table pointer identity and teardown, decoded-source and clone
  lifetimes, lazy page allocation/copy-on-write/explicit sharing, protection,
  faults and virtual-mapping contracts. No host-only CJS harness is removed.
  The 28 C and 28 CJS source/helper files still need further audit tranches;
  this is not a completed repository-wide assertion mapping.
- Added the `engine-regressions` root to shared C/OCaml selection and the
  actual OCaml dashboard generator. Explicitly installed the three additive
  snapshots with `build-test-corpus.py --install --review-selection` and
  audited them with `vfs-tests-check`. The corpus now has **287 identities,
  283 nonlegacy inputs and 15 groups**; all previous **284 test records** are
  unchanged. The inventory has 495 nodes. Both regenerated offline pages
  package the new selection without a custom filesystem bundle.
- `make -C src/cli-rt i32-smoke` runs retained warnings-as-errors ASan/UBSan
  C probes, authored WAST through the native CLI, and the installed group
  through the sanitizer batch runner. It compares authored/installed bytes
  before running the group, so stale snapshots cannot retain sanitizer
  coverage on older fixtures. Native group execution needs no Node/browser
  build. `waste-test --vfs-root=src/vfs --group=engine-regressions` and guest
  `/bin/waste-test --group=engine-regressions` select the same distribution.
- Added focused installed-group selection/results output to
  `tests/browser-test-suite-runtime.cjs`. The actual Chromium `file://` gate
  selects the new group through Diagnostics and checks its exact identities
  and 16/32/16 assertion counts; it retains shell-after-batch, cancellation,
  guest report redirection and no-external-request checks. Packaging/corpus
  guards use the new explicit counts; no prior compatibility or legacy
  classification changes.
- Verification: native CLI, three-job native batch, one-job ASan/UBSan batch,
  two-job production browser workers and the actual offline page each pass
  **all 64 assertions**. All native/browser ordered names/outcomes match.
  The official native OCaml interpreter accepts all three scripts. The smoke
  module body matches its retained C fixture. GCC warnings-as-errors C checks,
  LeakSanitizer batch cleanup outside the tracing sandbox, distribution/source
  audits, atomic snapshot guards, frontend/package guards, Python/JS/shell
  syntax and `git diff --check` pass. Evidence, source-selection baseline and
  per-fixture native/browser elapsed times are under
  `build/engine/refactor-stage6b-executor-wast/`; no speedup is inferred from
  these differently scheduled focused runs.
- Next slice: audit caller-instance/continuation assertions, retaining host
  callback identity, replay/counter and ownership gates, then migrate remaining
  guest-observable kernel/path/signal/wait expectations through real guest
  imports or the shared session contract. Full official oracle comparison,
  compatibility-mode migration, Stage 6C and Stage 7 remain open.

Stage 6B.23 landing slice — caller/continuation audit and finite native waits (2026-10-03):
- Audited every successful-path CHECK in `c-engine-caller-instance.c` and
  `c-engine-continuation.c`, recording the 32 and 43 checks in
  `docs/test-coverage.md`. Retained real C callback/trampoline identity,
  sparse-page pointers, decode-once/instantiate-twice lifetimes, exact yield
  reasons, private snapshot capture/restore and host replay counters. No C/CJS
  assertion is removed in this tranche: production guest calls cannot replace
  the user-supplied C callback or private replay boundary.
- Fixed the stale caller gate's use of removed `exec_memory.data`. The callback
  and four observer reads now use bounded `exec_memory_read`; backing identity
  checks compare initialized sparse pages. Four explicit read-status CHECKs
  retain error diagnostics. Warnings-as-errors ASan/UBSan builds again execute
  the caller gate rather than relying on an older binary.
- Added authored `tests/engine-regressions/caller-memory.wast` with twelve
  observations corresponding to the C value checks. Real guest
  `env.pipe`/`write`/`read`/`close` round trips exercise each module's memory,
  B→A→host calls, unchanged B bytes and two identical independent modules.
  Private pointer/binding/lifetime checks stay in C.
- Added `continuation-waits.wast` with 23 assertions. Positive 30-ms SELECT
  waits replace the synthetic `host.pause` in this guest counterpart. Direct,
  indirect, jump and cross-module results preserve 42/43/7/45. Repeated calls
  and entry/completion counters verify one execution of guest side effects;
  consumers with distinct or absent memory exercise provider memory selection.
  An invalid-timeout canary remains unchanged. Jump-value probes cover zero
  normalization and a negative value. Repeated guest calls complement, rather
  than replace, the C snapshot replay checks.
- The timed fixture exposed native CLI/batch rejection of finite SELECT
  yields. `src/cli-rt/native_wast.c` now binds a native monotonic clock, waits
  in at most 5-ms host sleep intervals for the engine-owned deadline, checks
  interruption between sleeps and resumes through the shared process driver.
  Pending assertion arguments/results remain live while evaluator/process
  state stays in the engine. Linux clock/sleep calls stay in cli-rt. No engine
  syscall, Asyncify transform or JavaScript-only implementation is added.
  External input, indefinite/other waits and yielding module starts retain
  their full-session requirement.
- `caller-instance` and `continuation` Make gates run retained C sanitizers,
  compare authored/installed snapshots, run authored WAST in the CLI and run
  installed fixtures in the sanitizer batch companion. `i32-smoke` covers the
  expanded group. Native host checks now verify SELECT/PSELECT completion,
  a 30-second finite wait bounded by a 200-ms test deadline, cancellation of
  runnable/waiting guests and continued reporting of a subsequent test.
- Explicitly installed the two additive snapshots with the reviewed-selection
  option, audited all sources/atomic guards, and regenerated both offline pages.
  The distribution now has **289 identities, 285 nonlegacy inputs, 15 groups
  and 497 inventory nodes**. All previous **287 test records** are unchanged.
  The five-fixture regression group has **99 assertions**. The actual offline
  Chromium gate checks its exact 12/23/16/32/16 results and retains shell,
  cancellation, report redirection and no-external-request checks.
- Verification: CLI, three-job native batch, one-job ASan/UBSan/LeakSanitizer
  batch, production browser workers and actual `file://` pages pass all 99
  ordered assertions. The full native corpus reports **279 PASS, 2 XFAIL,
  8 SKIP** with all original 284 statuses/assertion names/outcomes preserved;
  current reports contain **63,153 assertions**. The full native session driver
  also passes the timed fixture's 23 assertions and records SELECT waits.
  Retained caller/continuation gates pass 32/43 checks. Distribution/provenance,
  atomic snapshot and frontend/package guards, shell/Python/JS syntax and
  `git diff --check` pass. Evidence is under
  `build/engine/refactor-stage6b-caller-continuation/`.
- Oracle scope: the official native OCaml interpreter accepts the pipe-based
  caller fixture and the preceding three language fixtures. It rejects the
  timed continuation fixture at `env.select` import resolution. This remains
  an explicit oracle capability gap, not an oracle pass or a new installed
  expected failure. The official full-corpus oracle comparison stays open.
- Next slice: audit kernel/path/signal/wait assertions and migrate their
  guest-observable expectations through real imports or the shared session
  contract while preserving internal ownership, errno-layout, teardown and
  cancellation gates. Compatibility-mode migration, broader coverage mapping,
  Stage 6C and Stage 7 remain open.

Stage 6B.24 landing slice — path VFS audit and portable guest assertions (2026-10-03):
- Audited all 72 original successful-path CHECKs in `tests/posix-path-vfs.c`
  and all 11 codec checks in `tests/posix-path-abi.c`. The complete mapping in
  `docs/test-coverage.md` identifies every retained group and each removed
  assertion by its original C ordinal and replacement WAST ordinals.
- Added authored `tests/engine-regressions/path-vfs.wast` with **71 assertions**
  through real guest open/read/write/close, i64 lseek, stat, access, chmod,
  mkdir/rename/unlink/rmdir, chdir/getcwd and versioned readlink imports. It
  creates private `/tmp/path-vfs` files, independent of installed executables
  and host writes. Checks observe complete file/cwd bytes, permissions, public
  stat mode/size, file mutation, path normalization, rejected traversal and
  guest errno. Each tested failure clears the errno slot first to reject stale
  errors. Descriptor allocation order is not part of the expectations.
  Versioned readlink retains its negative-errno ABI and the guest wrapper
  applies libc's return translation.
- Removed **15 guest-observable C assertions**: nine regular-file traversal
  cases, four relative/root/separator normalization cases, rejected chdir to a
  file and invalid access mode. Their WAST counterparts also check the guest
  errno slot, which direct kernel calls bypassed. Remaining **57 path checks**
  preserve installation APIs, seeded symlink setup/follow/readlink, directory
  enumeration, exact nanosecond mtime, owned executable snapshots and cleanup,
  explicit byte-span bounds and separately allocated kernels. All **11 codec
  checks** remain; public stat observations complement their private round
  trips rather than replacing layout/NULL-pointer invariants. No CJS harness
  or shared engine/runtime behavior is changed.
- `make -C src/cli-rt posix-path-vfs` now combines the retained warnings-as-errors
  ASan/UBSan C gate, authored/installed byte comparison, ordinary CLI execution
  and an installed sanitizer batch. The existing executor gate covers the
  expanded six-fixture group. Updated distribution, frontend and actual-browser
  assertions to require the additive fixture and exact assertion counts.
- Explicitly installed the reviewed additive snapshot, audited provenance and
  atomic refresh guards, and regenerated both offline pages. The distribution
  now has **290 identities, 286 nonlegacy inputs, 15 groups and 498 inventory
  nodes**. All previous **289 manifest records** remain identical. The shared
  regression group now contains **six fixtures and 170 assertions**.
- Verification: ordinary CLI, three-job native batch, one-job
  ASan/UBSan/LeakSanitizer batch, production browser workers and actual offline
  Chromium pages pass. All **170 ordered assertion names/outcomes** agree
  across native, sanitizer, worker and actual-browser reports. Retained path
  and codec gates pass **57/11 checks**; the expanded executor gate passes.
  Full native corpus: **280 PASS, 2 XFAIL, 8 SKIP**, **63,224 assertions**, with
  all previous **289 test statuses/counts/assertion names/outcomes preserved**.
  Compiled browser SDK/VFS probes read all 290 mounted test paths through EOF.
  Distribution/package/offline guards, shell/Python/JS syntax and
  `git diff --check` pass. LeakSanitizer and actual Chromium run outside the
  ptrace sandbox; the normal C Make gates retain its documented leak-check
  exception. Evidence is under `build/engine/refactor-stage6b-path-audit/`,
  including `coverage-parity.json` and the previous manifest/native reports.
- Oracle scope: the official native OCaml interpreter rejects this fixture
  at its unsupported `env.chmod` import. This remains an explicit capability
  gap, not an oracle pass or a new installed expected failure. The preceding
  timed fixture's `env.select` gap and full official oracle comparison remain
  open.
- Next slice: audit the remaining kernel/signal/wait assertions and move
  guest-observable expectations through real imports or shared session events.
  Path symlink setup, mtime and directory enumeration still need equivalent
  supported guest coverage before further C consolidation. Broader C/CJS
  coverage mapping, compatibility-mode migration, Stage 6C and Stage 7 remain
  open; Stage 6B acceptance is not complete.

Stage 6B.25 landing slice — kernel pipe/descriptor audit and zero-count imports (2026-10-03):
- Inventoried all **208 CHECK sites across 23 original functions** in
  `tests/posix-kernel.c`; loop iterations account for the original **348 runtime
  checks**. `docs/test-coverage.md` records each function's retained boundary
  and maps every removed assertion by its original local ordinal to WAST.
- Added authored `tests/engine-regressions/pipe-descriptors.wast` with **99
  assertions** through real pipe/read/write/close/dup/dup2/fcntl/select imports.
  Zero-timeout SELECT observes readiness and returned fd-set bits without host
  input or indefinite waits. Cases cover byte round trips, buffered bytes/EOF
  after writer close, EPIPE, full-pipe EAGAIN and recovery, retained duplicate
  writers, same-fd dup2, replacing an endpoint while another reader remains,
  close-on-exec flags and closed/invalid descriptor errors. Allocated endpoints
  use returned descriptors; the explicit dup2 target is fd 10, matching C.
- Removed **ten guest-observable C checks**: all four `test_dup2` assertions
  and six edge cases for closed/wrong-end read/write and zero counts. The
  replacement actually transfers bytes through overwritten/duplicated endpoints
  and verifies flag preservation/reset. The remaining **198 sites / 338
  runtime checks** preserve raw HUP/ERR flags, exact capacity/partial fill,
  terminal host events, descriptor exhaustion, retained backing objects,
  namespace/clone ownership, actual close-on-exec, deterministic clocks and C
  NULL-pointer contracts. Direct dup2 remains in the close-on-exec/clone gate.
  No CJS harness is removed.
- The zero-count probes exposed a shared import-adapter bug: read/write
  allocated no host buffer for count zero and passed NULL to the kernel, which
  returned EINVAL. `src/engine/guest_posix.c` now supplies a non-NULL stack byte
  for zero-count calls, preserving guest range validation and kernel descriptor
  bounds without allocating. Cleanup frees only the owned heap buffer. The
  kernel's existing zero-count ordering and C NULL-pointer semantics remain
  intact; no platform syscall or Asyncify transform enters shared code.
  WAST verifies successful zero counts, unchanged guest bytes and continued
  EINVAL for descriptors -1 and 64. Nonzero import behavior remains covered by
  the full corpus and retained sanitizer gates.
- Recorded the actual legacy import contracts: read/write return raw kernel
  errors, close/dup translate errors to -1 and write guest errno, and fcntl
  returns direct statuses. Error-observing close/dup wrappers clear the errno
  slot before calling the import. This migration does not silently standardize
  those conventions. The signal-mask audit also identified `env.sigprocmask`
  as a zero-returning stub; it cannot establish real blocked-signal or mask
  restoration coverage. Signal/wait assertions remain intact.
- `make -C src/cli-rt posix-kernel` now combines retained warnings-as-errors
  ASan/UBSan checks, authored/installed byte comparison, authored CLI execution
  and an installed sanitizer batch. The existing executor gate covers the
  expanded group. Rebuilt both native and browser engines, explicitly installed
  the reviewed additive snapshot, audited provenance/atomic refresh guards,
  and regenerated both offline pages. The corpus now has **291 identities,
  287 nonlegacy inputs, 15 groups and 499 inventory nodes**. All previous
  **290 manifest records** remain identical. Seven regression fixtures contain
  **269 assertions**.
- Verification: CLI, three-job native batch, one-job
  ASan/UBSan/LeakSanitizer batch, production browser worker and actual Chromium
  regression reports agree on all **269 ordered assertion names/outcomes**.
  Full native corpus: **281 PASS, 2 XFAIL, 8 SKIP**, **63,323 assertions**; all
  previous **290 statuses/counts/assertion names/outcomes are preserved**.
  Kernel/wait/signal/path/codec sanitizer gates pass **338/21/47/57/11 checks**,
  and the expanded executor gate passes. Compiled browser SDK/VFS probes read
  all 291 mounted test paths through EOF. Distribution/package and syntax
  guards, offline shell/report/cancellation checks, no-external-request checks
  and `git diff --check` pass. Full actual-browser corpus also reports
  **281 PASS, 2 XFAIL, 8 SKIP** and **63,323 assertions**; all **291 native/browser
  test statuses/counts/ordered assertion names/outcomes agree**. Evidence is under
  `build/engine/refactor-stage6b-pipe-audit/`, including the original kernel
  source, assertion inventory, previous manifest/native report and
  `coverage-parity.json`.
- Oracle scope: the official native OCaml interpreter rejects the new fixture
  at its unsupported `env.select` import, even though SELECT uses zero
  timeouts. This remains an explicit capability gap, not an oracle pass or a
  new installed expected failure. The preceding select/chmod gaps and full
  official oracle comparison remain open.
- Next slice: provide faithful signal-mask/query capabilities before migrating
  blocked-signal, handler-mask and pselect restoration expectations; use shared
  session events for signal/readiness wakeups and retain deterministic
  cancellation/generation checks. Guest setup for terminal, clone namespace,
  path symlink/mtime and directory enumeration still needs equivalent coverage.
  Broader C/CJS migration, compatibility modes, full oracle acceptance,
  Stage 6C and Stage 7 remain open; Stage 6B is not complete.

Stage 6B.26 landing slice — real signal masks and blocked-signal migration (2026-10-03):

- Replaced the shared `env.sigemptyset/sigaddset/sigdelset/sigismember/sigprocmask`
  zero-returning stubs with bounded, little-endian 128-bit guest operations.
  Added `env.sigfillset` and `env.sigpending`. Block/unblock/setmask use the
  engine-owned kernel mask; queries ignore `how` when `set` is NULL.
  SIGKILL/SIGSTOP bits are removed from process-mask updates. Pending queries
  observe blocked queued signals without consuming them. Invalid operations
  return `-1` and set guest errno; input is copied before old-mask output, so
  aliased buffers work and rejected reads/writes leave kernel state unchanged.
- Added authored `tests/engine-regressions/signal-masks.wast` with **134
  assertions** through real guest imports: all four words and boundary bits,
  membership/fill/delete, mask modes and aliasing, EINVAL/EFAULT and output
  canaries, blocked pending retention, pselect interruption, actual handler
  argument/call count/self and action masks, consumed pending state, ignored
  dispositions and original-mask restoration after interruption/readiness.
  All polls are bounded; no Node process is needed for ordinary execution.
- Audited all **47 original signal checks**. Removed `test_blocked_signal`'s
  five guest-observable checks only after mapping them to WAST **126, 127,
  129, 131 and 134**, including the original unblock/SELECT/EINTR sequence.
  The remaining **42 checks** retain direct kernel APIs, host event wakeup,
  active wait/cancel/error restoration, simultaneous input/signal ordering,
  disposition state and private handler enter/leave boundaries. Handler and
  ready-mask WAST assertions complement those retained checks. The
  `posix-signal` Make gate now runs C sanitizers, compares authored/installed
  bytes, and runs authored CLI plus installed sanitized WAST.
- Updated the authored public `signal.h`, reviewed provider/signature policy
  and signal capability ledger. Explicit `guest-sdk-install` and strict
  `guest-sdk-check` pass: **742 available functions, 154 unavailable functions
  and 31 globals**, with 56 headers/six include orders and ABI/provider negative
  checks. Extended the mounted-only compiled C probe with `sdk_signal_check`:
  real set/mask/pending imports, public 16-byte layout and an adjacent canary.
  Its generated WAST executes all **four SDK assertions** natively and through
  the compiled browser engine, preserving C header/compilation coverage.
- Explicitly installed the new selection, passed `vfs-tests-check`, and rebuilt
  both offline pages. The distribution now has **292 identities, 288 nonlegacy
  inputs, 15 groups and 500 inventory nodes**. All previous **291 test records**
  remain unchanged. The regression group contains eight fixtures and **403
  assertions**; native, full ASan/UBSan/LeakSanitizer, worker and actual offline
  Chromium reports agree on ordered identities, assertion names and outcomes.
- Full native and actual offline production-browser corpora both report
  **282 PASS, 2 XFAIL, 8 SKIP** and **63,457 assertions**. All **292 ordered test
  records** and assertion names/outcomes agree; all previous 291 native results
  are preserved. The existing expected-failure policy is unchanged. Retained
  signal/wait/kernel/path sanitizer gates pass **42/21/338/57 checks**, the
  SELECT ABI gate passes **1,097 checks**, and the retained executor gate passes.
  Compiled-browser VFS checks read all 292 mounted paths through EOF; SDK,
  corpus, packaging and syntax checks pass.
- Rebuilt the native session runner and its sanitizer artifact, then preserved
  native/browser parity for all five PID/process-group signal scenarios,
  terminal-control, Bash and pipeline sessions. The production worker also
  passes terminal-control/Bash/pipeline; host-only signal scenarios use the
  browser exports as before. Linked-provider fork isolation and native SELECT
  deadline/input-timeout/result boundaries pass. Evidence is in
  `session-parity.log`; native session checks enable LeakSanitizer.
- Oracle/scope boundary: the native OCaml oracle rejects the new fixture at
  missing `env.sigfillset`; this is a capability gap, not a differential pass
  or new installed XFAIL. Existing sigaction/raise/pselect raw-negative errno
  conventions remain explicit. The handler slot still names an **engine
  function index**, and caught handlers run at the existing **pselect** boundary;
  compiled C function-pointer/flags conformance, general delivery on unblock,
  default-action signal queuing and SELECT handler dispatch remain open. The
  compiled C probe deliberately checks set/mask ABI, not handler pointers.
- Evidence: `build/engine/refactor-stage6b-signal-audit/` contains baseline
  manifest/inventory/results/C audit snapshots, native/worker/actual-browser
  regression and complete-corpus reports, sanitizer, SDK and retained-gate
  logs, compiled SDK results, oracle rejection and coverage/parity accounting.
- Next slice: migrate deterministic wait wakeups through shared native/browser
  session events and audit remaining kernel/host callback assertions. Keep
  cancellation/generation/ownership and compiled-C ABI checks where WAST cannot
  observe them. Full supported-official oracle comparison and the broader CJS
  coverage audit still precede consolidation and dashboard retirement.

Stage 6B.27 landing slice — shared wait events and deterministic timeout boundaries (2026-10-03):

- Added `tests/guest-session-waits.wast` and its shared JSON event contract,
  with **24 session checks** through real SELECT/pselect, read, terminal and
  signal imports. It observes ready-before-wait count/output bits, delayed
  terminal input/readiness, cleared sets on timeout, a second timeout with a
  fresh deadline, post-yield handler argument/call count/self/action masks,
  original-mask restoration and another real input wakeup. The contract fixes
  the clock near the u32 boundary, sends input only after an explicit SELECT
  yield, advances to **499,999 ns** (still waiting) and **500,000 ns** (timeout),
  then advances to the repeated wait's independent deadline. No guest stub or
  preloaded input simulates a wakeup.
- Added native WSC1 **operation 4**, an absolute u64 monotonic guest-clock
  update encoded as low/high u32 words. It requires a preconfigured nonzero
  frozen clock and rejects backward/reset updates. Partial records keep the
  existing bounded reader. The browser's separate
  `waste_wast_advance_clock_monotonic_ns` export requires the same capability
  at a READ/SELECT safe point; the production worker accepts a validated
  `advance-clock` message and wakes I/O. Initial clock configuration remains
  separate. Host cancellation/deadline clocks keep real time.
- Audited all **21 original checks** in `tests/posix-wait.c`. Moved five
  guest-observable expectations: ready-before-wait count → WAST **2**;
  resumed count/output bit → **5–6**; timeout count/cleared bit → **8–9**.
  The remaining **16 checks** preserve all four C functions and their direct
  EAGAIN, active wait, generation, private BLOCKED/READY/TIMEOUT polling,
  host enqueue status, cancellation and teardown boundaries. Existing
  signal-after-yield C checks remain complementary private/API coverage;
  shared session checks now exercise the actual guest handler after a host
  signal, rather than equating kernel EINTR with handler delivery.
- `make -C src/cli-rt posix-wait` now combines the retained C sanitizer probe
  with `tests/guest-session-wait-check.py` and the shared WAST/JSON contract,
  **without Node**. The native gate also rejects four clock capability/reset/
  backward cases without publishing an event and proves a frozen guest clock
  cannot defeat the real host deadline. The focused Make gate disables only
  LeakSanitizer for the ptrace environment; a separate run with full
  ASan/UBSan/LeakSanitizer passes. Browser probes reject clock updates before
  a wait, after completion, without a frozen override, backward/reset values
  and malformed u32 worker messages.
- Native, native sanitizers, browser exports, packaged production worker and
  actual offline Chromium all pass **24/24** with the identical transcript.
  Native evidence records **six SELECT yields, two input bytes, one signal
  event and three clock events**. The real `file://` check now consumes the
  same authored scenario in an independent production worker, preserves the
  shell, and passes the eight-fixture/**403-assertion** installed regression
  group plus DOM/worker/archive boot and no-external-request checks.
- The mounted corpus stays **292 identities, 288 nonlegacy inputs, 15 groups
  and 500 nodes**; no selection or installed snapshot is changed. The full
  native corpus remains **282 PASS, 2 XFAIL, 8 SKIP**, **63,457 assertions**,
  preserving every previous ordered test/assertion outcome. The session
  fixture stays outside ordinary batches because its declared external events
  require a session driver; it is not published as an unexplained batch skip.
  Strict SDK/provider audit, corpus provenance, packaging and syntax gates
  pass. Retained SELECT/codec/wait/signal/kernel/path gates pass
  **86/1,097/16/42/338/57 checks**. Shared wait, clock, terminal-control, PID
  signal, Bash and linked-provider fork sessions preserve native/browser
  behavior. Worker terminal validation rejects **57 malformed/overflow
  controls**; its check observes unchanged guest output/wait state because
  ordinary SELECT timer polls can also publish `io-ready`.
- Oracle/scope: the OCaml oracle rejects missing `env.select`; no differential
  pass or new XFAIL is claimed. Existing raw-negative SELECT/pselect and
  function-index/pselect handler semantics remain unchanged. This slice adds
  host event plumbing and coverage, not a new engine scheduler or signal ABI.
- Evidence: `build/engine/refactor-stage6b-wait-audit/` contains original C
  wait/signal and manifest/result snapshots, retained-gate and native build
  logs, native/export/worker/actual-Chromium session results, malformed-event
  and deadline checks, SDK/corpus/package logs, oracle rejection and parity
  accounting. The native session, WAST and JSON contract remain authored
  sources; ordinary native verification does not require browser packaging.
- Next slice: audit remaining SELECT/terminal/kernel guest expectations and
  migrate those with supported setup, retaining private generations, callback
  identity, ownership and C ABI checks. Broader CJS coverage accounting,
  compatibility-mode migration and supported-official oracle comparison remain
  acceptance work before Stage 6C consolidation and Stage 7 retirement.

Stage 6B.28 landing slice — SELECT validation and pipe polling audit (2026-10-04):

- Audited all **86 original CHECK sites/runtime checks** across the **22
  functions** in `tests/posix-select.c`. Moved **38 guest-observable checks**
  to authored `tests/engine-regressions/select-polling.wast`; the remaining
  **48 checks in 15 functions** preserve NULL-kernel API errors, private
  EAGAIN/yield boundaries, terminal input/EOF/read/write readiness, same-fd
  interest/count behavior, terminal nfds limits, pselect readiness and
  independently allocated kernel isolation. Seven all-guest pipe/error test
  functions were removed. Every original ordinal has a retained/migrated
  mapping in `docs/test-coverage.md`; terminal checks are not silently treated
  as equivalent to pipe checks.
- Added **247 ordinary WAST assertions** through real `env.select/pselect`,
  pipe/read/write/close/dup2 imports. They cover nfds/timeval/timespec EINVAL,
  closed/out-of-table descriptors in all three interest sets, empty/null-set
  bounded polls, real pipe read/write/hangup/exceptional readiness, full/partial
  drain transitions, output-set filtering and duplicate read descriptors.
  Extra probes cover real duplicate writers at **31/32/63**, signed bit 31,
  nfds filtering across words, ignored descriptors above nfds, high nfds with
  only low valid bits and complete clearing through the last 128-byte set word.
  Zero-timeout calls never need external events or a Node process.
- Rejected fd-set/timeout/signal-mask spans preserve valid output words and
  three adjacent canaries; invalid kernel arguments retain their input bits.
  The fixture explicitly tests the existing raw-negative import ABI, including
  **EINVAL for invalid input spans**, rather than assuming libc `-1`/errno
  translation or changing engine semantics. It checks the isolated noninteractive
  store's lowest pipe pair before using fd 0/1 and closes all real duplicates
  before the last-writer hangup test. No fixture-only state machine provides
  readiness. The existing 1,097 codec checks remain intact.
- Expanded `posix-select` to run retained C sanitizers, authored/installed byte
  comparison, authored CLI execution and installed sanitized WAST. The ordinary
  executor gate verifies all nine installed snapshots and runs the complete
  regression group. These native gates require no Node. Explicit selection
  installation and `vfs-tests-check` pass; both offline pages were rebuilt.
  The distribution now has **293 identities, 289 nonlegacy inputs, 15 groups
  and 501 VFS nodes**; all previous **292 test records** remain unchanged.
  The regression group now has **nine fixtures and 650 assertions**.
- Native, full ASan/UBSan/LeakSanitizer, browser worker and actual offline
  Chromium regression reports agree on **650 ordered assertion names/outcomes**.
  The full native and actual production-browser corpora both report
  **283 PASS, 2 XFAIL, 8 SKIP**, **63,704 assertions**; all **293 ordered test
  records** and assertion names/outcomes agree and all previous 292 native
  outcomes are preserved. Expected-failure policy is unchanged. The independent
  actual offline wait session still passes **24/24** and both file:// pages
  preserve archive/worker/DOM boot, shell recovery and no external requests.
- Retained SELECT/codec/wait/signal/kernel/path gates pass
  **48/1,097/16/42/338/57 checks**; the executor gate passes. Compiled-browser
  VFS checks read all 293 installed paths through EOF and the mounted-only
  compiled C SDK probe passes all four assertions. Strict SDK/provider,
  provenance, packaging and syntax checks pass. Guest headers, runtime adapters
  and shared engine semantics are unchanged by this coverage migration.
- Oracle/scope: the native OCaml oracle rejects the new fixture at missing
  `env.select`; no differential pass or new installed XFAIL is claimed.
  Private EAGAIN still becomes a guest yield, so it cannot become an ordinary
  WAST expected return. Terminal-specific and cross-kernel checks remain direct
  C coverage until equivalent guest session setup/mapping is established.
- Evidence: `build/engine/refactor-stage6b-select-audit/` holds original C and
  manifest/inventory/native-result snapshots, all 38 ordinal mappings,
  native/sanitizer/worker/actual-Chromium regression and full-corpus reports,
  retained-gate logs, mounted C SDK/VFS results, distribution/package checks,
  oracle rejection and coverage/parity accounting.
- Next slice: migrate terminal-specific SELECT/readiness and canonical/raw
  terminal expectations through shared native/browser session events, retaining
  raw readiness masks, terminal host APIs, generation/ownership and compiled C
  ABI coverage. Broader CJS audit, compatibility modes and supported-official
  oracle comparison still precede Stage 6C consolidation and Stage 7 retirement.

Stage 6B.29 landing slice — shared terminal readiness and canonical/raw audit (2026-10-04):

- Audited the **48 checks in 15 functions** remaining in `tests/posix-select.c`
  after Stage 6B.28. Moved **38 terminal checks** to authored
  `tests/guest-session-terminal-readiness.wast` and its shared JSON event
  contract. Removed ten complete guest test functions; **10 C checks in five
  functions** retain NULL-kernel errors, internal EAGAIN with empty/null sets
  or terminal waits, and independently allocated interactive/noninteractive
  kernels. The coverage ledger records every original ordinal; two duplicated
  zero-timeout expectations share the same guest assertion pair.
- Added **76 session checks** through real SELECT/pselect, read/write,
  tcgetattr/tcsetattr/tcflow and exit imports. They preserve mixed live/closed
  descriptors, initial write-only readiness, three writable standard fds,
  same-terminal read/write counts, all-three-set filtering, nfds limits,
  high nfds, zero timeouts, empty pselect masks, input/drain and terminal EOF.
  Extra checks prove all three standard descriptors share queued input and
  verify actual read bytes. No pipe substitute or fixture readiness model
  provides the terminal results.
- The same session completes a canonical line only after two host events,
  checks `abc\n`, erase editing to `xZ\n`, configured DEL/backspace, line kill
  and ICRNL, then changes to raw **VMIN=3/VTIME=0**. Two bytes keep READ pending;
  the third completes it with `xyz`. Canonical VEOF releases `partial` without
  adding a byte, then produces zero-length reads and persistent SELECT/pselect
  readability. VEOF is genuine terminal input available to both adapters;
  it does not replace the retained host EOF API or raw POLL_HUP checks.
- Moved **10 of 21 original `test_terminal_modes` checks** out of
  `tests/posix-kernel.c`: attribute success, canonical and raw read contents/counts,
  and valid/invalid tcflow returns. Its **11 retained checks** preserve host
  enqueue results, exact readiness masks, private signal-pending/discard state,
  raw EAGAIN and host winsize/signal behavior. Setup and drains stay in C for
  these boundaries. The kernel gate now passes **328 checks**.
- Added native-only `terminal-readiness`, also required by `posix-select` and
  `posix-kernel`. Its Python driver supplies real post-yield events without Node
  or HTML and checks **six SELECT yields, two READ yields and 32 input bytes**.
  Two negative controls complete the line/VMIN input prematurely and prove the
  partial-input guard rejects progress. Native ASan/UBSan/LeakSanitizer pass;
  Make gates retain the documented ptrace LeakSanitizer exception.
- Shared native/browser-export/packaged-worker checks and an independent actual
  offline Chromium worker pass **76/76** with the exact transcript. Browser
  exports, packaged worker and actual Chromium agree on all **76 ordered
  assertion names/outcomes**. The actual file:// gate also retains the prior
  **24-check** frozen-clock/signal wait session, shell recovery, DOM/worker
  boot, both pages and no external requests.
- Mounted distribution is unchanged: **293 identities, 289 nonlegacy inputs,
  15 groups, 501 VFS nodes** and **nine batch fixtures/650 assertions**. The new
  session is an authored event-driven fixture, not an installed ordinary batch.
  Manifest/inventory bytes and all **293 prior native outcomes** are preserved;
  full native/actual-browser ordered results still match
  **283 PASS, 2 XFAIL, 8 SKIP** and **63,704 assertions**. No installation or
  expected-failure policy change is needed. Retained SELECT/codec/wait/signal/
  kernel/path gates pass **10/1,097/16/42/328/57 checks**; executor regressions,
  distribution/SDK/provider audits, packaging and syntax checks pass.
- Scope/oracle: no engine, runtime adapter, frontend or guest header semantics
  changed. The native OCaml oracle rejects the session at missing `env.select`;
  no differential pass or installed skip is claimed. Raw readiness flags,
  terminal host API returns, C structure/ownership checks and wait generation/
  cancellation remain direct sanitizer coverage.
- Evidence: `build/engine/refactor-stage6b-terminal-audit/` contains original C
  and manifest/inventory/corpus snapshots, all **48 ordinal mappings**, native
  boundary/sanitizer and shared-session reports, actual Chromium session and
  full-corpus reports, retained-gate/audit logs and coverage/parity accounting.
- Next slice: audit remaining terminal EOF/output and VTIME expectations through
  shared session input/clock events, retaining raw readiness, buffer bounds,
  wait ownership/generation and host APIs. Remaining kernel/libc/CJS migration,
  compatibility modes and supported-official oracle comparison still precede
  Stage 6C consolidation and Stage 7 dashboard retirement.

Stage 6B.30 landing slice — terminal EOF/output and exact VTIME audit (2026-10-04):

- Audited all **10 original checks** in `test_terminal_eof_and_output` and
  **10 original checks** in `test_terminal_vtime` in `tests/posix-kernel.c`.
  Moved **14 checks** to authored `tests/guest-session-terminal-timing.wast`
  and its shared JSON contract: **12** map to guest WAST assertions and **two**
  output-byte expectations map to the exact shared host transcript. Attribute
  setup/drain calls remain where needed for private checks; the kernel gate
  now passes **314 checks**. The coverage ledger accounts for all 20 ordinals.
- Retained the output helper's **two transformed-buffer length checks** in
  renamed `test_terminal_output_lengths`: ONLCR returns four output bytes for
  three input bytes; raw mode returns three. Guest write reports source bytes
  consumed, so its return cannot replace the private helper's length contract.
  Four VTIME C checks retain EAGAIN, active registration and exact fake-clock
  TIMEOUT polling. Ownership/generation/cancellation and raw readiness gates
  remain intact.
- Added **46 session checks** through real read/write, tcgetattr/tcsetattr,
  SELECT and exit imports. Exact transcript bytes prove `a\nb` becomes
  `a\r\nb` only with OPOST+ONLCR; raw, ONLCR-only and OPOST-only
  cases preserve `a\nb`. Successful writes still return three source bytes.
  A **257-byte** write crosses the adapter's **256-byte** chunk boundary and
  emits **259 bytes**, preserving both newline expansions and the final byte.
- A nonzero frozen clock starts at **4,294,967,290 ns**. VMIN=0/VTIME=2 READ
  stays pending at **199,999,999 ns** elapsed and returns zero at
  **200,000,000 ns**. VMIN=3 retains a real `x` across post-yield input and
  subdeadline clock resumes, then returns one byte at its original deadline.
  A fresh read returns `Q` before its deadline; a subsequent empty timed read
  has an independent deadline. Sentinel words, sampled untouched bytes and
  adjacent canaries check read-buffer preservation.
- Canonical VEOF input releases all seven `partial` bytes without adding the
  control byte, then returns zero and preserves the previous buffer/canaries.
  All input/clock events follow actual READ/SELECT yields. Native records show
  **eight READ yields, one SELECT yield, six clock events and 10 input bytes**.
  Three negative controls send the exact deadline at a one-nanosecond-early
  boundary and require the no-progress guard to reject premature completion.
- Added native-only `terminal-timing`, also required by `posix-kernel`.
  Its Python event driver needs no Node or HTML; Make uses ASan/UBSan with the
  documented ptrace leak-check exception. Standalone full
  ASan/UBSan/LeakSanitizer passes. Shared native/browser exports/packaged worker
  and actual offline Chromium pass **46/46** with the exact transcript;
  browser exports, worker and actual Chromium agree on **46 ordered assertion
  names/outcomes**. Prior **24-check wait** and **76-check terminal-readiness**
  sessions also pass unchanged.
- Distribution remains **293 identities, 289 nonlegacy inputs, 15 groups,
  501 VFS nodes** and **nine batch fixtures/650 assertions**. The new session
  needs outside-guest events and stays separate from the installed batch.
  Manifest/inventory bytes and all **293 prior native outcomes** are preserved;
  full native/actual-browser ordered results still agree on
  **283 PASS, 2 XFAIL, 8 SKIP** and **63,704 assertions**. Expected-failure
  policy is unchanged. SELECT/codec/wait/signal/kernel/path sanitizer gates pass
  **10/1,097/16/42/314/57 checks**; executor regressions, distribution/SDK/provider
  audits, packaging, both file:// pages and syntax checks pass.
- Scope/oracle: engine, runtime adapter, frontend and guest header semantics
  are unchanged. The OCaml oracle rejects the fixture at missing `env.select`;
  no differential pass or installed skip is claimed. READ timing here uses
  explicit clock events. Autonomous READ timer wakeups and broader VMIN/VTIME
  timing semantics remain acceptance work; the fixture audits the existing
  deadline established by the first read wait rather than adding a new timer
  model. Direct bounded output API lengths are not guest write counts.
- Evidence: `build/engine/refactor-stage6b-terminal-timing-audit/` contains the
  original C and manifest/inventory/corpus snapshots, all **14 mappings**,
  native/sanitizer/shared-session and actual Chromium reports, output segment
  accounting, full-corpus parity, retained-gate/audit logs and oracle rejection.
- Next slice: audit remaining descriptor flags, exhaustion and duplicate-fd
  guest expectations through the real imports, retaining clone/ownership,
  private OFD state and compiled C ABI checks. Broader kernel/libc/CJS migration,
  timer capability gaps, compatibility modes and supported-official oracle
  comparison still precede Stage 6C consolidation and Stage 7 retirement.

Stage 6B.31 landing slice — descriptor flags, exhaustion and pipe reuse (2026-10-04):

- Audited all **30 original checks** across `test_fd_exhaustion` (4),
  `test_close_on_exec` (9), `test_dup` (9), `test_edge_cases` (5) and
  `test_pipe_dup_readiness` (3) in `tests/posix-kernel.c`. Moved **14 checks**
  into authored `tests/engine-regressions/descriptor-flags.wast`, with
  **93 assertions** through real pipe, dup, dup2, fcntl, close, read and write
  imports. Removed the exhaustion function; the other four retain **16 checks**.
  The complete kernel sanitizer gate now passes **300 runtime checks**.
- The ordinal ledger retains terminal duplicate readiness/draining (dup 1–7),
  raw pipe HUP before/after the last writer closes (pipe-dup 2–3), C NULL
  arguments (edge 1–2/4–5), direct close-on-exec execution and clone ownership
  (close-on-exec 7–9). Their real setup calls remain. Guest descriptor flags
  do not replace execution of the private close-on-exec or clone APIs.
- Added independent flag checks for dup clearing CLOEXEC, dup2 replacement
  clearing it, self-dup2 preserving it, and F_DUPFD/F_DUPFD_CLOEXEC minimum/hole
  allocation. Writes through three writer aliases and reads through shared
  reader aliases verify actual pipe bytes. Invalid descriptor/flag/command
  cases preserve the existing adapter error conventions: legacy operations
  return -1 plus errno; fcntl returns raw negative errno. These are current
  implementation expectations, not a claim of complete POSIX conformance.
- Bounded allocation creates **32 pipes/64 descriptors** and verifies the
  complete ordered descriptor vector. Full-table pipe/dup/fcntl failures,
  self-dup2 without allocation, one-slot pipe failure without consuming the
  free slot, lowest-free dup reuse and two-slot pipe reuse all run as WAST.
  Closing the complete table and creating a fresh pipe verifies cleanup.
- The fixture exposed stale **FD_CLOEXEC** on recycled pipe slots. Fixed
  `src/engine/lib/kernel.c` to clear flags on both newly published pipe
  endpoints. Regression assertions cover recycled reader and writer slots.
  A separate **12-assertion C/OCaml differential probe** uses returned
  descriptor numbers and confirms both endpoints have clear flags after
  close/recreate; the OCaml implementation already initializes them to zero.
- Added the authored/installed descriptor fixture and sanitized mounted run
  to native-only `posix-kernel`. Explicitly reviewed and installed the new
  selection; all **ten** authored regression snapshots match installed bytes.
  Distribution is now **294 identities, 290 nonlegacy inputs, 15 groups and
  502 VFS nodes**; the regression group has **743 assertions**.
- Native, full ASan/UBSan/LeakSanitizer, production worker and actual offline
  Chromium agree on **743 ordered regression assertion names/outcomes**.
  Full native/actual-browser records agree on **284 PASS, 2 XFAIL, 8 SKIP**
  and **63,797 assertions**. All **293 prior records/63,704 assertions** and
  all previous manifest test/file records are preserved; expected-failure
  policy is unchanged. The **24/76/46-check shared sessions** pass unchanged.
- SELECT/codec/wait/signal/kernel/path sanitizer gates pass
  **10/1,097/16/42/300/57 checks**. Distribution/SDK/provider audits, mounted
  browser EOF checks across all 294 files, compiled C SDK probes, packaging,
  both actual file:// pages and syntax/whitespace checks pass. The Make gates
  keep their documented ptrace leak-check exception; standalone regression
  LeakSanitizer passes outside that environment.
- Scope/oracle: only shared pipe flag initialization changes engine behavior;
  no frontend, import adapter or guest headers change. The full new fixture
  stops in the oracle at initial fd allocation **3 versus noninteractive C 0**;
  its **1,024-slot** descriptor table also differs from C's **64 slots**.
  Record those runtime-profile gaps separately from the passing focused
  pipe-flag differential probe. No installed skip/XFAIL is added.
- Evidence: `build/engine/refactor-stage6b-descriptor-audit/` contains original
  C/manifest/inventory/corpus snapshots, **14 ordinal mappings**, native,
  sanitizer, worker and actual Chromium reports, the focused oracle probe,
  retained-gate/audit logs and ordered coverage/parity accounting.
- Next slice: audit guest-observable directory iteration and creation-mask
  expectations, retaining injected-clock timestamp, metadata structure and
  clone ownership checks. Remaining kernel/libc/CJS migration, timer and
  runtime-profile gaps, compatibility modes and supported-official oracle
  comparison still precede Stage 6C consolidation and Stage 7 retirement.

Stage 6B.32 landing slice — directory iteration, file masks and exclusive creation (2026-10-04):

- Audited all **15 original checks** in `test_directory_dot_entries` (10)
  and `test_creation_mask` (5) in `tests/posix-kernel.c`. Moved **six complete
  checks** and the guest-visible components of **six compound checks** into
  authored `tests/engine-regressions/directory-umask.wast`, with **219 assertions**
  through real imports. Nine C checks remain; the complete kernel gate now
  passes **294 runtime checks**. The ledger distinguishes full removal from
  partial migration rather than counting retained compound checks as removed.
- Renamed the retained directory helper `test_directory_metadata`. It keeps
  private fixture installation, exact host-seeded child/parent/file inode
  values **41/40/42**, and both fixed root inode checks **1**. Real open/readdir/
  close setup remains. Creation checks retain both exact injected realtime
  timestamps and the independent mask inherited by a clone. Guest names and
  stat/readdir identity comparisons do not replace private metadata contracts.
- WAST creates its own directory tree and verifies complete NUL-terminated
  `.`, `..` and `file` names, entry kinds, modes and identities against guest
  stat. Capacities **1/2/4** reject too-short names without consuming entries;
  capacities **2/3** accept dot/parent exactly. An invalid metadata range is
  rejected before iteration. Failure and repeated EOF preserve all **32 name
  bytes/48 metadata bytes**; surrounding canaries and bytes beyond short
  requested ranges remain unchanged. Root dot and parent resolve to root.
- A real dup shares the directory cursor; a separate open starts independently.
  Closing the original leaves the independent descriptor usable. Wrong-kind,
  closed and negative descriptors report the current raw readdir errors/errno.
  No outside-guest events or Node process are needed for ordinary execution.
- File creation verifies default **0022**, private **0077**, group **0002**,
  all-masked **0777** and zero masks. Umask returns the prior value, ignores
  bits outside **0777**, and affects new files without changing an existing
  file's mode when reopened with O_CREAT and a different mode argument.
- The new fixture exposed ordinary open accepting an existing path with
  **O_CREAT|O_EXCL**. Fixed the shared kernel to reject it with EEXIST before
  following a link or applying O_TRUNC. Regression checks cover new-file
  success, existing-file/directory rejection and preservation of actual
  `keep` bytes after an exclusive truncation attempt.
- The retained C gate caught the backing-path interaction with shm_open.
  Its existing namespace-level exclusivity check now consumes O_EXCL before
  opening the already materialized backing path. **Eight added guest checks**
  verify first creation, collision/errno, close/unlink and exclusive recreate.
  All existing C shared-memory checks remain; this is not their coverage audit.
- Added authored/installed comparison, direct CLI execution and sanitized
  mounted execution to native-only `posix-kernel`. Explicitly reviewed the new
  selection, refreshed final snapshots and rebuilt both pages. Distribution
  is **295 identities, 291 nonlegacy inputs, 15 groups, 503 VFS nodes**;
  **eleven batch fixtures/962 assertions** match authored bytes.
- Native, full ASan/UBSan/LeakSanitizer, production worker and actual offline
  Chromium agree on **962 ordered regression assertion names/outcomes**.
  Full native/actual-browser records agree on **285 PASS, 2 XFAIL, 8 SKIP**
  and **64,016 assertions**. All **294 prior records/63,797 assertions** and
  previous manifest test/file records are preserved; expected-failure policy
  is unchanged. The **24/76/46-check shared sessions** pass unchanged. The
  parallel native run hit the unchanged 10-second memory_copy deadline under
  contention; the full serial retry passes with no deadline override.
- SELECT/codec/wait/signal/kernel/path gates pass
  **10/1,097/16/42/294/57 checks**, plus **11 path ABI checks**. Distribution,
  SDK/provider, all-295-file browser EOF and compiled C SDK audits, packaging,
  both actual file:// pages and syntax/whitespace checks pass. Make retains
  its documented ptrace leak-check exception; standalone regression
  LeakSanitizer passes outside that environment.
- Oracle/scope: the full fixture stops at missing **env.readdir_v1**; no new
  skip/XFAIL or full differential pass is claimed. A focused **25-check**
  mask/exclusive-create comparison passes in C and OCaml with explicit stat
  profiles: mode offset **16 in C**, **8 in OCaml**. The oracle stores errno
  internally rather than in this guest errno slot, so that focused comparison
  excludes errno queries; installed C WAST tests them. Frontend, import adapter,
  headers and clock semantics are unchanged. Directory creation-mask semantics
  and broader capability/profile gaps are outside this file-mask audit.
- Evidence: `build/engine/refactor-stage6b-directory-audit/` contains original
  C/kernel/manifest/inventory/corpus snapshots, **12 full/partial ordinal
  mappings**, native/sanitizer/worker/actual Chromium reports, both focused
  oracle profiles, retained-gate/audit logs and ordered coverage/parity proof.
- Next slice: audit guest-visible named shared-memory creation, errors and
  lifetime expectations, retaining namespace attachment, credentials, retained
  object APIs and clone ownership in C. Remaining kernel/libc/CJS migration,
  timer/profile gaps and supported-official oracle comparison still precede
  Stage 6C consolidation and Stage 7 retirement.

Stage 6B.33 landing slice — named shared-memory coverage audit (2026-10-04):

- Audited all **15 original checks** in `test_shared_memory_names` in
  `tests/posix-kernel.c`. Moved **four complete checks** and the guest-visible
  unlink/name-lookup components of **one compound check** into authored
  `tests/engine-regressions/shared-memory.wast`, with **231 assertions**.
  Eleven C checks remain; the complete kernel gate passes **290 runtime
  checks**. The partial migration retains clone allocation and is not counted
  as a removed check. The assertion ledger records every original boundary.
- Retained namespace attachment across independent kernels, credential-based
  failed-creation rollback and access denial, root credential restoration,
  direct offset-independent object read/write APIs, and original/clone object
  reads after unlink. Real create/size/unlink setup remains in C. Guest
  seek/read behavior complements these private ownership and API contracts.
- WAST exercises real shm_open/shm_unlink/ftruncate/fstat/read/write/lseek/dup/
  close imports. It checks missing and malformed names, exclusive creation
  and collisions, initial size/mode, zero-filled growth, prefix-preserving
  shrink, and size preservation after rejected truncation. Separate opens
  share bytes with independent cursors; dup shares the original cursor.
  Reads check complete returned bytes, untouched buffer tails and adjacent
  canaries; public stat outputs retain their surrounding canaries.
- Unlink removes the name while existing descriptors remain usable. Recreating
  that name while old handles are live creates independent fresh bytes;
  writes to either generation leave the other intact. Both unlinked generations
  survive closing other handles, and a final generation starts empty/zero-filled.
  The fixture closes all its handles and unlinks all its names.
- Error assertions preserve the current import ABI: kernel shm_open/ftruncate
  errors use raw negative errno, while adapter/legacy failures use -1 plus
  guest errno. Empty guest strings currently produce EFAULT at the adapter;
  slash-only, relative and nested names produce EINVAL. This is regression
  coverage, not a claim of complete POSIX conformance. Inter-kernel namespaces,
  credentials, mmap and inode uniqueness across live unlinked generations
  remain outside this guest audit.
- Added authored/installed comparison, direct CLI execution and sanitized
  mounted execution to native-only `posix-kernel`. Explicitly reviewed and
  installed the new selection, then rebuilt both offline pages. Distribution
  is **296 identities, 292 nonlegacy inputs, 15 groups, 504 VFS nodes**;
  **twelve batch fixtures/1,193 assertions** match authored bytes.
- Native, full ASan/UBSan/LeakSanitizer, production worker and actual offline
  Chromium agree on **1,193 ordered regression assertion names/outcomes**.
  Full native/actual-browser records agree on **286 PASS, 2 XFAIL, 8 SKIP**
  and **64,247 assertions**. All **295 prior records/64,016 assertions** and
  previous manifest test/file records are preserved; expected-failure policy
  is unchanged. The **24/76/46-check shared sessions** pass unchanged.
  Native full-corpus verification uses one job and the unchanged deadlines.
- SELECT/codec/wait/signal/kernel/path gates pass
  **10/1,097/16/42/290/57 checks**, plus **11 path ABI checks**. Distribution,
  SDK/provider, all-296-file browser EOF and compiled C SDK audits, packaging,
  both actual file:// pages and syntax/whitespace checks pass. Make retains
  its documented ptrace leak-check exception; standalone regression
  LeakSanitizer passes outside that environment.
- Oracle/scope: the recorded OCaml attempt stops at missing **env.shm_open**.
  This POSIX fixture is outside the language oracle scope; it creates no
  OCaml provider backlog or kernel acceptance blocker. No new skip/XFAIL or
  differential pass is claimed. No engine, import adapter,
  runtime, guest-header or frontend behavior changed in this slice.
- Evidence: `build/engine/refactor-stage6b-shared-memory-audit/` contains original
  C/manifest/inventory/corpus snapshots, **five full/partial ordinal mappings**,
  native/sanitizer/worker/actual Chromium reports, retained-gate/audit logs,
  the oracle capability result and ordered coverage/parity proof.
- Next slice: audit guest-visible process-group and foreground-terminal
  expectations through real imports and shared input events, retaining host
  signal routing, raw readiness and private kernel state in C. Remaining
  kernel/libc/CJS migration, timer/profile gaps and supported-official oracle
  comparison still precede Stage 6C consolidation and Stage 7 retirement.

Stage 6B.34 landing slice — process groups and foreground-terminal routing (2026-10-04):

- Audited all **ten original checks** in `test_foreground_process_group_routing`
  in `tests/posix-kernel.c`. Moved **six complete checks** into authored
  `tests/guest-session-process-groups.wast` and its shared JSON event contract,
  with **71 checks** (70 assertions plus successful exit). Four C checks remain;
  the complete kernel gate passes **284 runtime checks**. The coverage ledger
  maps every original ordinal, including retained checks with guest complements.
- Retained direct host terminal enqueue, absence of a private SIGINT pending
  bit while background, raw kernel pselect EINTR, and private consumption of
  that pending bit. Real process-group/termios/foreground setup remains in C.
  Guest handler counts and sigpending queries complement these checks; blocked
  guest pending signals do not expose all internal kernel/terminal queues.
- The shared session uses real getpgrp/setpgid/tcgetpgrp/tcsetpgrp, termios,
  pselect, read/write and signal-handler imports. Initial process and terminal
  groups are independent. Invalid PID/group/descriptor changes preserve their
  identities. The three standard terminal descriptors share foreground state;
  changing the process group leaves that state unchanged.
- Two post-yield background VINTR events leave SELECT pending. Subsequent real
  `x`/`Y` input releases each wait without a handler call. Switching foreground
  to the process group routes the queued interrupt exactly once, returning
  EINTR and invoking the SIGINT handler. Repeated zero-time polls verify no
  replay. A foreground VINTR interrupts a genuine wait immediately; disabling
  ISIG turns the same ETX byte into ordinary data. Read canaries and complete
  untouched buffer tails remain intact.
- Native event accounting records **six SELECT yields, six input bytes and
  zero injected host-signal events**. Signals come from terminal input, not
  a host signal shortcut. Both background events require `stillWaiting`;
  two negative controls supply VINTR plus a data byte and prove the native
  driver rejects premature progress even while preserving the queued signal.
- Added native-only `make -C src/cli-rt process-groups`, required by
  `posix-kernel`, with ASan/UBSan and saved report/transcript. It requires no
  Node or HTML. The standalone checker passes full LeakSanitizer outside the
  ptrace environment; Make retains its documented leak-check exception.
  Shared exports/production-worker parity accepts `--scenario process-groups`;
  the actual offline Chromium gate runs the same source and event contract.
- Native, sanitizer, browser exports, packaged worker and actual offline
  Chromium pass **71/71 checks** with the exact `BG/MOVE/FG/RAW` transcript.
  Browser assertion names/outcomes agree across exports, worker and Chromium.
  The previous **24/76/46-check sessions** remain unchanged; four sessions now
  total **217 checks** outside the unchanged installed batch.
- Distribution remains **296 identities, 292 nonlegacy inputs, 15 groups,
  504 VFS nodes**, with **twelve batch fixtures/1,193 assertions**. The corpus
  audit caught stale source-input provenance from the recent OCaml-scope edits
  to the DIY/libc READMEs. Explicit refresh updates only those two source-input
  records; all prior manifest test/file records and installed fixture bytes
  remain unchanged. Both offline pages were rebuilt and checked.
- Full native/actual-browser results agree on **286 PASS, 2 XFAIL, 8 SKIP**
  and **64,247 assertions**. All **296 previous ordered records** remain
  unchanged, with no policy or deadline override. SELECT/codec/wait/signal/
  kernel/path gates pass **10/1,097/16/42/284/57 checks**, plus **11 path ABI
  checks**. SDK/provider, compiled C ABI, browser all-file EOF, corpus/package,
  syntax and whitespace gates pass.
- Scope: no engine, import adapter, runtime API, guest header, frontend behavior
  or signal-routing semantics changed. env.setpgid still returns the new
  positive group rather than POSIX's zero; the fixture records the existing
  ABI without claiming full job-control conformance. Forked group membership,
  sessions/setsid and background-I/O enforcement remain outside this slice.
  These are C POSIX contracts, outside the OCaml language oracle scope; no
  OCaml providers, comparisons or kernel work are required.
- Evidence: `build/engine/refactor-stage6b-process-group-audit/` contains original
  C/Makefile/manifest/inventory/corpus snapshots, **six ordinal mappings**, native
  sanitizer/leak and browser reports, negative-control counts, full-corpus
  comparisons, unchanged engine/runtime hashes and coverage/parity proof.
- Next slice: audit remaining guest-visible terminal descriptor duplication and
  close expectations through shared input events, retaining private OFD,
  reference-count, raw readiness and independently allocated kernel checks.
  Remaining libc/CJS migration, C runtime gaps, supported language-oracle
  comparison and coverage accounting still precede consolidation/retirement.

Stage 6B.35 landing slice — terminal descriptor aliases and close lifetimes (2026-10-04):

- Audited all **14 remaining checks** in `test_dup` (seven) and `test_close`
  (seven) in `tests/posix-kernel.c`. Moved **six complete checks** into
  `tests/guest-session-terminal-descriptors.wast` and its shared JSON event
  contract. The new session has **120 checks**: 119 assertions plus successful
  exit. Retained all **eight raw readiness/lifetime checks** unchanged, with
  their real dup/read/close setup. The complete kernel gate now passes
  **278 runtime checks**. Stage 6B.31 already moved the two historical invalid
  dup checks; this slice does not count them again.
- Real dup/dup2, close, read/write, SELECT and termios imports verify descriptor
  reuse, no-op dup2, shared terminal settings and input, closing either alias,
  restoring stdin, and preserving the target after failed duplication. A
  VMIN=2 read through a duplicate remains pending after the first input byte.
  SELECT reports every open alias; reading through one alias drains the shared
  queue. Closing stdin preserves queued data and the other aliases. After
  closing stdin and its duplicates, stdout still reads the final queued byte;
  stderr retains terminal state until its own close.
- Invalid dup/dup2/close operations preserve working aliases and distinguish
  EINVAL from EBADF through the current guest ABI. Closed reads return raw
  negative EBADF without changing errno; dup/dup2/close errors return -1 and
  set errno. Buffer canaries and complete untouched tails are checked after
  successful and failed reads. These assertions record existing behavior;
  no runtime error-convention change or broader POSIX conformance is claimed.
- Seven post-yield input events deliver **eight bytes**, with **four READ and
  three SELECT yields**. One native negative control supplies both minimum
  bytes at the first boundary and proves the driver rejects premature progress.
  Host input still targets fd 0: events precede its close, and real dup2 restores
  it before later input. No arbitrary-descriptor host injection API was added.
- Added native-only `make -C src/cli-rt terminal-descriptors`, required by
  `posix-kernel`, with ASan/UBSan and saved report/transcript. It needs no Node
  or HTML. The standalone checker passes LeakSanitizer outside sandbox ptrace;
  Make retains its documented leak-check exception. The shared parity driver
  accepts `--scenario terminal-descriptors`, and actual offline Chromium runs
  the same authored source and event contract.
- Native, sanitizer, browser exports, packaged worker and actual offline
  Chromium pass **120/120 checks** with the exact
  `DUP/ORIGINAL/SHARED/LAST/REOPEN/FINAL` transcript. Browser assertion names and
  outcomes agree across exports, worker and Chromium. The prior
  **24/76/46/71-check sessions** remain unchanged; five sessions total
  **337 checks** outside the unchanged installed batch.
- Distribution remains **296 identities, 292 nonlegacy inputs, 15 groups,
  504 VFS nodes**, including **twelve batch fixtures/1,193 assertions**.
  Manifest, inventory and installed fixture bytes are unchanged; no refresh or
  repackaging is needed. Full native/actual-browser results agree on
  **286 PASS, 2 XFAIL, 8 SKIP** and **64,247 assertions**, preserving all
  **296 previous ordered records** without policy or deadline overrides.
  SELECT/codec/wait/signal/kernel/path gates pass
  **10/1,097/16/42/278/57 checks**, plus **11 path ABI checks**. SDK/provider,
  compiled browser SDK ABI, all-file EOF, corpus/package, syntax and whitespace
  gates pass.
- Scope: engine, adapter, runtime API, guest headers and frontend behavior are
  unchanged. Private OFD/reference lifetime, raw readiness and independent
  kernel isolation remain C sanitizer checks. These are C POSIX contracts;
  they require no OCaml comparison, provider additions or kernel development.
- Evidence: `build/engine/refactor-stage6b-terminal-descriptor-audit/` contains
  original C/Makefile/manifest/inventory/corpus snapshots, **six ordinal
  mappings**, native sanitizer/leak and browser reports, the negative control,
  full-corpus comparisons, unchanged engine/runtime hashes and parity proof.
- Next slice: audit the guest-visible creation, byte-I/O and close/EOF/error
  expectations still retained in `test_pipe_readiness` and
  `test_pipe_close_transitions`. Reuse and extend the installed pipe fixture
  where coverage is equivalent; preserve raw POLL flags, direct kernel EAGAIN,
  reference ownership and capacity checks. Remaining libc/CJS migration,
  supported language-oracle comparison and coverage accounting still precede
  consolidation and dashboard retirement.

Stage 6B.36 landing slice — pipe creation, byte I/O and close/EOF results (2026-10-04):

- Audited all **23 checks** in `test_pipe_readiness` (14) and
  `test_pipe_close_transitions` (nine) in `tests/posix-kernel.c`. Removed
  **twelve complete guest-visible checks** already covered by the installed
  `pipe-descriptors.wast` fixture: creation, valid/distinct endpoints, write/read
  counts, exact bytes, buffered drain after writer close, EOF and broken-pipe
  EPIPE. The assertion ledger maps every original ordinal. Retained all
  **eleven raw readiness/direct-kernel checks** unchanged, with real setup.
  The complete kernel gate now passes **266 runtime checks**.
- Preserved **all 99 original pipe assertion commands and ordered outcomes**.
  Added **37 assertions**, bringing the fixture to **136** and the twelve-file
  regression group to **1,230 assertions**. Separate guarded-read exports
  exercise real imports without changing the original wrappers. Successful
  reads check exact test/abc bytes, both canaries and every untouched byte of a
  64-byte buffer. Empty-pipe EAGAIN, wrong-end/closed EBADF and repeated EOF
  check that the entire buffer remains unchanged. A broken-pipe write preserves
  its source bytes; queued data remains available after writer close.
- Raw POLL_IN/OUT/HUP/ERR masks and the direct kernel EAGAIN result remain C
  sanitizer checks. Guest SELECT readiness complements them without exposing
  those raw flags. Ordinary batch reads expose the current raw negative errors;
  this fixture does not establish interactive read suspension or change the
  import error convention. Pipe capacity, partial fills, duplicate-writer
  reference ownership and independently allocated kernels remain unchanged.
- The existing native-only `posix-kernel` gate already requires authored/
  installed byte equality, direct authored WAST execution and installed
  ASan/UBSan execution. No new driver or Node requirement was added. The full
  native regression group also passes standalone LeakSanitizer outside sandbox
  ptrace; Make retains its documented leak-check exception.
- Explicit `vfs-tests-install` and distribution audits refresh only the pipe
  fixture's test/file metadata and installed bytes. All **295 other test
  records**, **317 other file records**, source-input provenance, modes, mtimes
  and inodes remain unchanged. Inventory changes are limited to pipe content/
  size and the manifest hash. Distribution remains **296 identities,
  292 nonlegacy inputs, 15 groups and 504 VFS nodes**. Both offline pages were
  rebuilt, and the focused Chromium gate now expects 136 pipe assertions.
- Native, ASan/UBSan, LeakSanitizer, production worker and actual offline
  Chromium agree on **twelve fixtures/1,230 ordered assertions**. Full native/
  browser results agree on **286 PASS, 2 XFAIL, 8 SKIP** and
  **64,284 assertions**. All 295 other ordered corpus records and the pipe
  fixture's original 99-result prefix are preserved, without policy or deadline
  overrides. The five **24/76/46/71/120-check sessions** remain unchanged,
  totaling **337 checks** outside the installed batch.
- SELECT/codec/wait/signal/kernel/path gates pass
  **10/1,097/16/42/266/57 checks**, plus **11 path ABI checks**. SDK/provider,
  compiled browser SDK ABI, all-file EOF, corpus/package, syntax and whitespace
  gates pass. Engine, adapter, runtime API, guest headers and frontend behavior
  are unchanged. These C POSIX contracts require no OCaml comparisons, provider
  additions or kernel development.
- Evidence: `build/engine/refactor-stage6b-pipe-io-audit/` contains original
  C/Makefile/fixture/manifest/inventory/corpus snapshots, **twelve full ordinal
  mappings**, sanitizer/leak and browser reports, installed metadata comparison,
  unchanged private checks/source hashes and ordered parity proof.
- Next slice: audit remaining guest-visible terminal read/write and EOF
  expectations in `test_terminal_readiness`, reusing the shared terminal
  contract where equivalent and retaining direct host enqueue/EOF signaling,
  raw readiness and private terminal state. Remaining libc/CJS migration,
  supported language-oracle comparison and coverage accounting still precede
  consolidation and dashboard retirement.

Stage 6B.37 landing slice — terminal byte I/O and repeated EOF (2026-10-04):

- Audited all **17 checks** in `test_terminal_readiness` in
  `tests/posix-kernel.c`. Moved **four complete guest-visible checks** into the
  existing shared terminal-readiness contract: read count, exact hello bytes,
  read at EOF and five-byte write count. Retained all **thirteen host/raw
  readiness checks** unchanged, with their real read/EOF/write setup. Direct
  enqueue acceptance/rejection, host EOF signaling and raw POLL_IN/OUT/HUP
  masks remain C gates. The complete kernel gate passes **262 runtime checks**.
- Extended `guest-session-terminal-readiness.wast` from **76 to 90 checks**:
  **89 assertions plus successful exit**. All 76 original commands and their
  relative order/outcomes are preserved. Fourteen added assertions check both
  canaries and every untouched byte of a 64-byte read buffer after raw,
  canonical, edited, minimum-byte and EOF reads. Repeated EOF leaves the whole
  buffer unchanged; pselect reports all three standard descriptors readable
  and writable after EOF. A real five-byte stdout write returns five, preserves
  its hello source and appends exactly hello to the existing transcript.
- The **eight input events, 32 input bytes, six SELECT yields and two READ
  yields** are unchanged. The existing partial-line and partial-VMIN guards and
  **two premature-input negative controls** still pass. EOF comes from real
  canonical VEOF input, while the host EOF API and its raw hangup behavior stay
  directly tested in C. Guest readiness does not expose the raw POLL masks.
- The existing native-only `terminal-readiness` Make gate, required by
  `posix-kernel` and `posix-select`, now saves its sanitizer report/transcript
  to `build/cli-rt/terminal-readiness-results.json`. Its standalone checker
  accepts `--results PATH` and passes ASan/UBSan/LeakSanitizer outside sandbox
  ptrace; Make retains its documented leak-check exception. Native execution
  requires no Node or HTML, and no new session driver or runtime API was added.
- Native, sanitizer, browser exports, packaged worker and actual offline
  Chromium pass **90/90 checks** with the exact extended transcript. Browser
  paths agree on every ordered assertion name/outcome and preserve the prior
  76-result subsequence. The other **24/46/71/120-check sessions** are unchanged;
  five sessions now total **351 checks** outside the installed batch.
- Manifest, inventory and all installed fixture bytes remain unchanged:
  **296 identities, 292 nonlegacy inputs, 15 groups and 504 VFS nodes**,
  including **twelve batch fixtures/1,230 assertions**. No installation or page
  rebuild is needed. Full native/actual-browser results preserve all
  **296 ordered records and 64,284 assertions**: **286 PASS, 2 XFAIL, 8 SKIP**,
  without policy or deadline overrides. SELECT/codec/wait/signal/kernel/path
  gates pass **10/1,097/16/42/262/57 checks**, plus **11 path ABI checks**.
  SDK/provider, corpus/package, syntax and whitespace gates pass.
- Scope: engine, adapter, runtime API, guest headers and frontend behavior are
  unchanged. These C POSIX contracts require no OCaml comparison, additional
  providers or kernel development. No CJS harness is retired.
- Evidence: `build/engine/refactor-stage6b-terminal-io-audit/` contains original
  C/Makefile/session/manifest/inventory/corpus snapshots, **four full ordinal
  mappings**, the preserved-command index, native sanitizer/leak and browser
  reports, negative-control counts, unchanged private checks/source hashes and
  ordered parity proof.
- Next slice: finish the retained-kernel inventory across remaining ownership,
  isolation, capacity and host-metadata helpers, mapping any guest-visible
  expectations to existing fixtures where equivalent. Then begin the libc/CJS
  assertion audit. Supported language-oracle comparison and full coverage
  accounting still precede consolidation and dashboard retirement.

Stage 6B.38 landing slice — complete current retained-kernel inventory (2026-10-04):

- Reviewed every remaining CHECK in `tests/posix-kernel.c`: **21 helpers and
  122 original sites**. Moved the final standalone guest pipe-creation check in
  `test_pipe_full` to the existing `pipe-descriptors.wast` **assertion 36**.
  No new fixture or driver is needed. Real creation stays as setup; all four
  partial-fill/capacity/raw-readiness expressions remain identical. All other
  kernel helper bodies are unchanged.
- Completed [the retained-kernel inventory](posix-kernel-retained-coverage.md)
  for all **121 retained sites**, classifying each current ordinal exactly once
  with its C boundary and guest complement. The C source points to that review.
  Supporting guards remain inside host-seeded mapping, cloning and publication
  probes; guest open/read/write behavior alone cannot establish those API
  boundaries. No partially covered compound expression is counted as a full
  migration.
- The retained gate passes **261 runtime checks**. Five lifecycle sites execute
  **130 checks** through allocation guards and descriptor loops of **64/3/61**.
  Four pipe-capacity sites execute **19 checks**, including **sixteen positive
  partial writes**. The other **112 sites** execute once each. The inventory
  distinguishes source-site coverage from loop execution counts and accounts
  for the complete runtime total.
- Explicitly retain raw readiness/NULL-pointer errors; offset-independent
  mapping reads/writes and descriptor cursor independence; retained object
  handles and strict mapping ranges; clone/merge publication; exact seeded
  inodes and injected-clock mtimes; clone-private umask; independent kernels;
  host terminal enqueue/EOF/resize; private signal queues and clock waits;
  transformed output-helper lengths; direct exec teardown; namespace attachment
  and injected shared-memory credentials. Guest fixtures complement these
  checks without exposing their private C APIs.
- Native warnings-as-errors, ASan/UBSan and standalone **LeakSanitizer** pass
  all **261 checks**, including real retained-object/clone teardown. Make
  retains its documented sandbox-ptrace leak-check exception. The existing
  `posix-kernel` target still requires authored/installed fixture equality,
  direct authored WAST execution, installed sanitizer execution and shared
  native event contracts without Node or HTML. No harness is retired.
- All installed bytes and metadata remain unchanged: **296 identities,
  292 nonlegacy inputs, 15 groups and 504 VFS nodes**, including
  **twelve batch fixtures/1,230 assertions**. No installation or page rebuild
  is needed. Full native/actual-browser parity preserves all **296 ordered
  corpus records and 64,284 assertions**: **286 PASS, 2 XFAIL, 8 SKIP**,
  without policy or deadline overrides. All five shared sessions preserve
  their ordered results and transcripts: **24/90/46/71/120 checks**, totaling
  **351**. SELECT/codec/wait/signal/kernel/path gates pass
  **10/1,097/16/42/261/57 checks**, plus **11 path ABI checks**.
  SDK/provider, corpus/package, syntax and whitespace gates pass.
- Scope: classification is complete for this current C file, not all C/CJS
  harnesses or full kernel conformance. Engine, adapter, runtime API, guest
  headers and frontend behavior are unchanged. OCaml remains a Wasm/WAT/WAST
  language oracle; no kernel comparison, provider additions or development
  are required for these C contracts.
- Evidence: `build/engine/refactor-stage6b-retained-kernel-audit/` contains
  original C/Makefile/manifest/inventory/corpus snapshots, **one full migration
  mapping**, the **121-site inventory** with exact expressions/hashes and
  expected loop multiplicities, native sanitizer/leak and browser reports,
  preserved-check/source hashes and ordered parity proof.
- Next slice: audit `tests/libc-test/libc-runtime.cjs` selection/completion
  expectations against the installed libc WAST clients and native/browser C
  runners. Account for legacy stub-provider assumptions separately from real
  C providers; do not extend those OCaml stubs or kernel. Keep compiled C
  header/layout clients and focused artifact/allocator checks where they
  expose distinct boundaries. Other C audits, supported language-oracle
  comparison and coverage accounting still precede consolidation/retirement.

Stage 6B.39 landing slice — libc harness selection/completion audit (2026-10-04):

- Audited `tests/libc-test/libc-runtime.cjs` and all **fourteen authored libc
  clients / 56 installed assertions**. Guest value expectations already live
  in WAST; no new guest assertion or C/CJS removal is counted. The
  [complete libc harness inventory](libc-harness-coverage.md) accounts for
  file discovery, exact-file selection, the optional assertion-deletion filter,
  scheduler/artifact choice, loader completion, exit propagation and reporting.
- Added **`libc-native` and `libc-sanitize`** targets under cli-rt. These consume
  the installed tree without Node, HTML generation or OCaml builds. The focused
  checker requires the nonempty authored/installed selection, matching client
  bytes, assertion names derived from the existing C parser, every ordered
  outcome/count and native setup diagnostics free of load/encode failures.
  Existing reports from either C runtime can be checked with `--report PATH`.
- Preserved **12 PASS / 2 XFAIL**, comprising **51 passing assertions and five
  existing mismatches**: `boundary-execve`, `boundary-readlink`,
  `boundary-opendir`, `boundary-ioctl` and compound `terminal`. The focused gate
  rejects an additional failure inside either XFAIL file, changed failure phase,
  missing/reordered assertions, skips and unexpected passes. **Sixteen negative
  report controls** pass; native/browser selection, isolation, deadlines,
  cancellation, malformed-tail recovery and exit-status host gates also pass.
  No provider or expected-failure policy is changed to clear legacy assumptions.
- Classified all **sixteen legacy injected provider functions** separately
  from real C providers. OCaml remains only a Wasm/WAT/WAST language oracle;
  no stub, kernel or application-scheduler development/comparison is required.
  The optional regex export filter remains legacy debugging convenience;
  current runners execute complete fixtures and expose named results.
- Retained compiled C header/layout clients, SDK/provider and codec/canary
  checks. The separate allocator Node gate instantiates the actual guest-libc
  Wasm artifact. Fixed its stale import-free instantiation: the merged module
  has **24 function imports**, now bound to throwing guards. Its unchanged
  **5,000-allocation** alignment/live-range/byte/memory-growth workload and
  final frees pass with **zero kernel calls**; a real `isatty` wrapper call
  verifies guard rejection. It supplies no POSIX behavior and is not an
  OCaml harness. The five allocator WAST assertions complement that artifact
  boundary.
- Native ASan/UBSan and standalone **LeakSanitizer** preserve all **56 libc
  outcomes**. Production browser-worker and actual Chromium reports preserve
  the same ordered names/results. General ordinary setup-module acceptance
  remains separate work; passing reported assertions alone do not establish
  every module's acceptance, especially with bounded browser diagnostics.
- Updated the libc README and allocator setup, explicitly refreshed **only
  their source provenance** in the corpus manifest and rebuilt both offline
  pages. All test/file records,
  selection, installed fixture bytes and other source provenance remain
  unchanged. The inventory changes only the manifest content metadata/hash
  for `/tests/manifest.json`. Counts remain **296 identities / 292 nonlegacy inputs /
  15 groups / 504 nodes**. Full native/actual-browser parity preserves all
  **296 ordered records / 64,284 assertions**, **286 PASS / 2 XFAIL / 8 SKIP**,
  without policy/deadline overrides. Twelve regression fixtures still total
  **1,230 assertions**; five shared sessions retain **24/90/46/71/120 checks**,
  totaling **351**. SDK, distribution, frontend package, syntax and whitespace
  gates pass. Engine/runtime semantics and the **121-site / 261-runtime-check**
  retained kernel inventory remain unchanged.
- Evidence: `build/engine/refactor-stage6b-libc-harness-audit/` contains original
  harness/Makefile/manifest/inventory/corpus snapshots, the complete parsed libc
  assertion inventory, native/sanitizer/leak/worker/Chromium reports, rejection
  controls, artifact/package audits, source hashes and ordered parity proof.
- Next slice: audit `tests/diy-posix-test/posix-{kernel,control}-runtime.cjs`:
  map guest expectations, external pause/signal inputs and completion controls
  to C batch/shared-session coverage or explicit profile gaps. Retain host
  control boundaries; do not extend OCaml providers/kernel or retire drivers
  before their useful coverage is accounted for. Other C audits, setup-module
  acceptance, supported language-oracle comparison and consolidation remain.

Stage 6B.40 landing slice — legacy DIY POSIX harness audit (2026-10-04):

- Audited `tests/diy-posix-test/posix-{kernel,control}-runtime.cjs` completely:
  **seven authored kernel assertions, one embedded signal assertion** and their
  loader/scheduler/control/completion boundaries. The
  [per-expectation DIY inventory](diy-posix-harness-coverage.md) distinguishes
  supported C behavior, ABI adaptations and explicit profile gaps. Neither
  driver is retired; no removed C site is counted and the cumulative **188**
  moved C assertions remain unchanged.
- Original kernel assertions **1–5** pass through native sessions and browser
  C exports with unchanged module/assertion bytes: pid/ppid result **100**,
  foreground-group equality **1**, VFS and duplicate-pipe word **1819043176**,
  and fork/private-parent-memory/wait result **1803**. The diagnostic prefix
  records one fork and child exit. Existing installed path/pipe fixtures,
  shared group sessions and private process-lifecycle checks complement those
  expectations; the generated audit probes add no installed test identity.
- Recorded both kernel gaps without developing new scheduler behavior.
  Running the complete original fixture passes five then fails assertion 6:
  stop/continue result **1536**, expected **5759**. The full run stops there;
  an independent invocation accounts for assertion 7: foreground-job result
  **0**, expected **15**. Child-first C execution and the zero-returning sleep
  profile do not reproduce those concurrent job scenarios. The installed
  `browser-native` compatibility entry remains an explicit **SKIP** in both
  current C batch controllers, not seven passing checks or a new XFAIL policy.
- Added the shared **`guest-session-diy-control.wast`/JSON** counterpart.
  Its real `env.sigaction` handler is installed during module start; initial
  counters prove one installation and no early delivery. The host waits for
  `STARTED` and a pid-1 SELECT yield before sending SIGINT. `run` returns **2**,
  and handler/installation counters remain **1/1** after resume. Six assertions
  plus successful exit give **seven checks**. The C function-index ABI is
  explicit; the OCaml table-slot/control-page protocol is not reimplemented.
- Added **`make -C src/cli-rt diy-posix-control`**, a native ASan/UBSan gate
  requiring no Node or HTML generation. Two failure controls pass: no signal
  after the confirmed wait must reach host deadline/exit **124** with no guest
  exit; demanding return **12** after actual SIGINT must propagate the
  **actual 2 / expected 12** mismatch and exit **1**. Both retain **3/4**
  completion counts. Reports publish only after all controls pass. Standalone
  **LeakSanitizer** verifies positive and negative cleanup.
- Native, browser exports, packaged production worker and actual offline
  Chromium preserve all seven ordered checks and exact transcript. Added the
  scenario to the shared adapter selector and actual-browser gate. Existing
  five sessions preserve all **24/90/46/71/120** results and transcripts;
  shared coverage now totals **six sessions / 358 checks**. Runnable busy-loop
  pause/resume remains an explicit gap: supported pselect delivery and
  abort-style deadlines/cancellation do not prove that legacy capability.
- Updated the DIY README, explicitly refreshed **only its source provenance**
  in the installed manifest and rebuilt both offline pages. Test/file records,
  selection, installed fixture bytes and all other source provenance remain
  unchanged. Only manifest content hash changes in the inventory. Counts stay
  **296 identities / 292 nonlegacy inputs / 15 groups / 504 VFS nodes**;
  full native/actual-browser parity preserves **296 ordered records / 64,284
  assertions**, **286 PASS / 2 XFAIL / 8 SKIP**, without deadline/policy
  overrides. Twelve regression fixtures remain **1,230 assertions**. SDK,
  distribution/package, syntax and whitespace gates pass. Engine, runtime,
  provider and frontend behavior and the **121-site / 261-check** retained
  kernel inventory are unchanged.
- Scope: these C POSIX contracts require no OCaml comparison, provider additions
  or kernel development. OCaml remains the Wasm/WAT/WAST language oracle;
  existing kernel/application integration removal stays deferred. Private
  process/wait/signal APIs and ownership, compiled C ABI and host control gates
  remain retained. General setup-module acceptance and wider C/CJS accounting
  remain open.
- Evidence: `build/engine/refactor-stage6b-diy-posix-audit/` contains original
  driver/WAST/Makefile/manifest/inventory snapshots, exact legacy diagnostic
  selections and outcomes, the eight-expectation ledger, native sanitizer/leak
  and browser reports, control failure evidence, source hashes and ordered
  corpus/session parity proof.
- Next slice: audit `tests/diy-posix-test/bulk-operations.wast` and
  `spectest-isolation-{a,b}.wast`, the three language/host-memory compatibility
  fixtures, for shared native/browser command-stream execution. Preserve
  intentional imported-memory aliasing and fresh stores across scheduled tests.
  Keep the legacy kernel scheduling/control gaps explicit; do not extend
  OCaml kernel providers or retire compatibility evidence prematurely.

Stage 6B.41 landing slice — DIY language and host-memory cutover (2026-10-04):

- Audited `tests/diy-posix-test/bulk-operations.wast` and
  `spectest-isolation-{a,b}.wast` completely and promoted their installed
  execution mode from browser-native compatibility to shared **`wast-stream`**.
  Original authored bytes remain unchanged. Bulk operations retains **20**
  ordered checks: six ordinary invokes plus fourteen return assertions;
  memory aliasing and fresh-store fixtures retain **4/1** assertions. The
  25-command ledger preserves every original module/action, argument and
  expected value. No new identity, expected failure or provider is added.
- The C parser/executor now consumes those original WAST commands in cli-rt
  and html-rt; these scripts no longer need Binaryen-assembled module assets.
  Corrected the generator's DIY count handling so `--count` retains native
  parsed counts for streaming payloads; uncounted payloads record unknown zero.
  Distribution counts for unchanged source bytes remain preserved and are
  verified against the parser and execution reports. Mmap's execution spec and
  two data assets remain unchanged; its native-parity metadata now records
  the already shared engine backend.
- Strengthened existing native and browser host-boundary gates to run the
  original spectest pair in **both manifest orders / jobs 1 and 3**. `$writer`
  and `$observer` share their imported memory within one script and observe
  byte **170** after the store; the independently scheduled reader starts at
  **zero**. All **4/1** checks pass in every configuration. These complement
  retained private store/ownership checks rather than replacing them.
- All three unchanged fixtures pass both native C and the **OCaml language
  oracle**, each in a separate process. This uses only language semantics and
  standard spectest scaffolding; no OCaml POSIX/kernel/scheduler provider or
  development is introduced. Existing OCaml kernel removal remains deferred.
- Explicit `build-test-corpus.py --install --review-selection` refreshes the
  three reviewed modes and DIY README provenance. Four assembled Wasm support
  files and their three directories are removed. The inventory retains all
  other entries/metadata except the updated manifest size/hash; counts are
  **296 identities / 292 nonlegacy inputs / 15 groups / 497 VFS nodes**, with
  **314 manifest files**. All authored/installed WAST bytes and policies match.
  Both self-contained offline pages are rebuilt explicitly.
- Native **ASan/UBSan/LeakSanitizer** and the production browser worker agree
  on the installed DIY group: **4 PASS / 1 SKIP / 48 checks**, including mmap's
  23. The dashboard worker separately preserves the three promoted scripts'
  **25 ordered results**. Full native/actual offline Chromium parity is
  **289 PASS / 2 XFAIL / 5 SKIP / 64,309 ordered assertions**. Only the three
  prior compatibility SKIPs become PASS; all other **293** corpus records
  remain unchanged, with no deadline/policy overrides. Twelve regression
  fixtures remain **1,230 assertions**, and all six shared sessions preserve
  their **358 checks** and transcripts. Distribution/package, syntax and
  whitespace gates pass.
- `posix-kernel.wast` remains the sole DIY compatibility entry and explicit
  batch SKIP. Its two scheduling gaps and the runnable-loop pause gap remain
  documented; neither legacy driver is retired. No C assertions are removed:
  cumulative moved coverage remains **188**, and the retained kernel inventory
  stays **21 helpers / 121 sites / 261 checks**. Shared engine, runtime,
  provider and frontend behavior remain unchanged.
- Evidence: `build/engine/refactor-stage6b-diy-language-cutover/` contains prior
  generator/distribution/manifest/inventory snapshots, unchanged source hashes,
  the 25-command compatibility ledger, parser/oracle evidence, native sanitizer
  and leak reports, focused worker/dashboard and full native/actual-browser
  results, and ordered corpus/session parity proof. The
  [DIY coverage ledger](diy-posix-harness-coverage.md),
  [test coverage](test-coverage.md) and [corpus guide](test-corpus.md) record
  the current execution modes and retained boundaries.
- Next slice: audit ordinary setup-module diagnostics and completion, including
  scripts with no assertions and rejection before later assertions. Account
  for command order, errors and completion independently of assertion totals;
  compare supported language behavior with OCaml without extending its kernel.
  Wider C/CJS coverage and Stage 6B acceptance remain open.

Stage 6B.42 landing slice — ordinary setup and completion audit (2026-10-04):

- Audited ordinary module diagnostics separately from assertions. Baseline
  probes showed native C silently accepting invalid, unlinkable and trapping
  setup with **zero assertions**, while OCaml rejected each. The prior browser
  module path discarded the same errors. Later passing assertions could also
  hide earlier setup rejection. These were reporting/acceptance gaps, not
  successful language checks or missing OCaml kernel capabilities.
- Added a shared, caller-owned setup report with attempted/successful
  instantiations and ordered **line/status/phase/error** diagnostics. Native
  JSON and both browser workers expose `setup` independently of assertion/action
  counts; `completed` records command-stream EOF. Definition syntax and
  registration do not count as instantiations; explicit module assertions keep
  their existing results. Diagnostic capacity/allocation failure is visible,
  and copied errors survive store teardown until report reset.
- Ordinary setup errors now fail file acceptance and native exit status even
  when every assertion passes or there are none. Command-boundary recovery
  preserves subsequent results without clearing an earlier failure. Browser
  batch policy checks report consistency; the retained dashboard displays setup
  diagnostics and includes them in downloaded JSON. Existing XFAIL/XPASS policy
  now includes completed setup failures, with missing files, crashes,
  interruptions and incomplete reports retaining their host-failure policy.
- Added **eight** authored `tests/test-suite-setup-*.wast` host-boundary probes:
  empty stream, valid provider/definition/instance/import setup, invalid body,
  unresolved import, trapping start, unknown definition, quote encoding failure
  and recovery with two ordered setup failures plus three passing assertions.
  They are staged under owned scratch trees, not added to the installed corpus.
  Native warnings-as-errors **ASan/UBSan**, production worker and standalone
  **LeakSanitizer** verify acceptance, zero-assertion XFAIL/XPASS and teardown.
  Unterminated input retains **18 prior results / one parse failure** and
  reports incomplete EOF. C and OCaml agree on focused acceptance/rejection;
  OCaml stops at its first error rather than reproducing C recovery policy.
- Fixed browser parse ownership found during this audit: a failing module
  callback could free the script while dispatch still read its groups. Failed
  parses are now released after iteration. Failed-start engines stay store-owned
  when imported tables may retain their funcrefs. Private ownership/session
  and host-boundary gates remain retained; no kernel/scheduler feature is added.
- Full installed execution reveals **2,313 setup attempts / 2,303 successes /
  ten failures** across seven official files. OCaml accepts all seven unchanged
  files. Six diagnostics cover segment index **64 / 65 segments** in bulk-memory
  and memory64 fixtures; three cover active element segments in `core/elem.wast`;
  one is the unresolved standard `spectest.table64` import. The
  [setup ledger](wast-setup-coverage.md) records every identity, source line and
  rejection. Both tracked expected-failure files explicitly add these language
  gaps; the two prior libc entries remain. This exposes previously hidden
  acceptance failures without rewriting their expectations.
- Native/actual offline Chromium parity preserves **296 ordered identities /
  64,309 assertion/action results** and now records **282 PASS / 9 XFAIL /
  5 SKIP**. Only the seven former language PASS outcomes become XFAIL; all
  other **289** records and every prior ordered assertion result remain
  unchanged. Setup counts/diagnostics agree across runtimes. Twelve executor
  fixtures retain **1,230** results; all six sessions preserve **358** checks and
  transcripts. Installed manifest/inventory, all WAST bytes and corpus selection
  are unchanged: **292 nonlegacy inputs / 15 groups / 497 VFS nodes / 314
  manifest files**. Both offline pages rebuild explicitly. Distribution/package,
  syntax and whitespace gates pass; no deadline overrides are introduced.
- No C assertions are removed: cumulative moved coverage remains **188**, and
  the retained kernel inventory stays **21 helpers / 121 sites / 261 checks**.
  Legacy DIY kernel scheduling/control gaps remain explicit. OCaml remains
  language-only; its kernel/application cleanup stays deferred. Stage 6B
  acceptance, wider C/CJS accounting and dashboard retirement remain open.
- Evidence: `build/engine/refactor-stage6b-setup-audit/` contains prior adapter,
  worker, manifest/inventory and expected-failure snapshots, silent-pass baseline
  probes, every official setup diagnostic, focused/affected OCaml comparisons,
  native sanitizer/leak and browser reports, and ordered corpus/session parity
  proof. The corpus, coverage, native-session and techniques documents record
  the corrected setup/completion and ownership contract.
- Next slice: resolve the **six 65-segment/index diagnostics in five official
  files**, with native/browser and OCaml agreement before removing their XFAIL
  entries. Keep active-element and standard spectest table64 gaps visible and
  distinct from OCaml kernel work.

Stage 6B.43 landing slice — segment retention and capacity (2026-10-04):

- Resolved all **six setup diagnostics in five official files**. The unsigned
  LEB operands were already correct: the WAT parser silently retained only
  **32** data/element segments, then validation rejected references to index
  **64**. Raised both bounded capacities to **128**, consistently used by
  parser metadata, name resolution, binary loading and runtime segment state.
  No fixture-specific index exceptions or platform behavior are added.
- All thirteen segment append forms now reject capacity overflow explicitly,
  including active/passive/declarative segments and memory/table shorthand.
  Unretained data payloads are freed. A resource error stays a parse failure
  inside `assert_invalid`; it cannot masquerade as semantic invalidity or
  acceptance of a truncated module. Capacity remains bounded, rather than
  claiming arbitrary segment counts.
- Added authored `tests/test-suite-segment-indices.wast`, staged by the retained
  native/browser host-boundary gates rather than changing installed selection.
  Its **64 portable assertions / two setups** check indices **31, 32, 63, 64,
  127**, distinct data and function values, numeric and deferred named operands,
  memory32/table32 and memory64/table64 initialization, drops,
  post-drop traps, zero-length initialization and four unknown-index rejection
  cases. Two capacity fixtures exercise the **129th** declaration, both ordinary
  and inside `assert_invalid`. OCaml agrees on all portable language checks;
  implementation resource limits are separately tested C host boundaries.
- The five unchanged official files and portable probe pass native C,
  production browser worker and the OCaml language oracle. Native
  warnings-as-errors **ASan/UBSan** host-boundary and **LeakSanitizer** runs pass,
  including overflow payload cleanup. Removed only those five identities from
  both expected-failure lists. Four entries remain: two libc gaps,
  `core/elem.wast` and `core/memory64/table64.wast`.
- Full native/actual offline Chromium parity now records **287 PASS / 4 XFAIL /
  5 SKIP**, **2,313 setup attempts / 2,309 successes / four failures**. All
  **296 ordered identities / 64,309 assertion/action results** are preserved;
  only the five repaired outcomes change from XFAIL to PASS. The twelve
  executor fixtures retain **1,230** results, and six sessions retain **358**
  checks/transcripts. Installed corpus, manifest/inventory and all WAST bytes
  remain unchanged: **292 nonlegacy inputs / 15 groups / 497 VFS nodes / 314
  manifest files**. Both offline pages rebuild explicitly; distribution,
  packaging, syntax and whitespace gates pass without deadline overrides.
- The audit also found a separate flat-instruction syntax gap: `table.init`
  rejects valid plain syntax, while plain `data.drop`/`elem.drop` are ignored
  by the permissive WAST parser. Focused C/OCaml probes preserve this evidence;
  the portable regression uses the supported folded forms. This remains a
  supported-language follow-up, independent of the six fixed setup failures.
- No C assertions are removed; cumulative moved coverage remains **188**, and
  the kernel inventory stays **21 helpers / 121 sites / 261 checks**. OCaml is
  language-only, with no new kernel/provider development. Wider C/CJS audits,
  Stage 6B acceptance, consolidation and dashboard retirement remain open.
- Evidence: `build/engine/refactor-stage6b-segment-indices/` contains previous
  parser/capacity and baseline snapshots, unchanged corpus/source hashes,
  focused C/browser/OCaml reports, sanitizer/leak checks, full native/actual
  browser results, and ordered corpus/session parity proof. The
  [setup ledger](wast-setup-coverage.md), [coverage](test-coverage.md) and
  [corpus guide](test-corpus.md) record the current limits and remaining gaps.
- Next slice: resolve the **three active-element setup diagnostics in
  `core/elem.wast`**, verify native/browser/OCaml agreement, then remove its
  XFAIL entry. Keep the standard `spectest.table64` import and flat bulk
  instruction syntax gaps explicit; neither requires OCaml kernel work.

Stage 6B.44 landing slice — element segment reference types (2026-10-04):

- Resolved all **three ordinary setup diagnostics in `core/elem.wast`**, at
  lines **87, 448 and 482**. Bare function-index lists and `elemkind func`
  declare **non-null `(ref func)`**. The WAT parser had widened them to nullable
  `funcref`, so their encoded expression vectors could not initialize a
  non-null table. Explicit nullable segment types remain nullable even when
  every item is `ref.func`; value inspection must not narrow their declaration.
- Preserve non-null types for bare/`func` active segments, including empty
  vectors, and passive/declarative `func` segments. Synthesized table-shorthand
  segments inherit the table's declared type for both 32/64-bit tables. The
  existing expression-vector encoder carries these types without special
  fixture handling or source rewrites.
- Correct the binary loader's declared types for legacy **modes 0–3**, including
  passive/declarative segments used by `table.init` and GC validation. Mode
  **4** retains implicit nullable `funcref`; modes **5–7** retain their explicit
  reference type. Remove the active-only effective-type workaround and use
  the declared segment type consistently. Runtime ownership and platform
  adapters remain unchanged.
- Added authored `tests/test-suite-element-types.wast` outside installed corpus
  selection. Its **82 portable assertions / eleven ordinary setups** check
  bare/explicit function lists, empty active/passive/declarative vectors,
  nullable shorthand with
  null entries, table64 addressing, passive initialization, active/declarative
  drop state, repeated drops and post-drop traps. It covers **all eight binary
  element modes**, distinct initial/segment function values and rejection of
  nullable segment declarations in non-null tables, including vectors containing
  only non-null function values.
- The portable fixture and unchanged official file pass native C, production
  browser and the OCaml **language-only** oracle. OCaml also verifies all twenty
  C-encoded setup/invalid modules. Native warnings-as-errors
  **ASan/UBSan** host-boundary and **LeakSanitizer** checks pass for successful
  setup, expected invalid modules and segment/store teardown. Removed only
  `core/elem.wast` from both expected-failure lists after verified agreement.
- Full native/actual offline Chromium parity now records **288 PASS / 3 XFAIL /
  5 SKIP**, **2,313 setup attempts / 2,312 successes / one failure**. Preserve
  all **296 ordered identities / 64,309 assertion/action results**; only the
  repaired file changes XFAIL to PASS, and its setup successes rise **73 → 76**.
  Twelve executor fixtures retain **1,230** results; six sessions preserve
  **358** checks and transcripts. Installed selection, manifest/inventory and
  all WAST bytes remain unchanged: **292 nonlegacy inputs / 15 groups / 497
  VFS nodes / 314 manifest files**. Both offline pages rebuild explicitly;
  distribution/package, syntax and whitespace gates pass without deadline
  overrides.
- The remaining official setup XFAIL is the unresolved standard
  `spectest.table64` import; both existing libc XFAILs remain. Flat bulk syntax
  gaps recorded in 6B.43 remain separate. No C assertions are removed:
  cumulative moved coverage stays **188**, and the retained kernel inventory
  remains **21 helpers / 121 sites / 261 checks**. Stage 6B acceptance, wider
  C/CJS audits, consolidation and dashboard retirement remain open.
- Evidence: `build/engine/refactor-stage6b-active-elements/` contains previous
  parser/loader/baseline snapshots, minimal rejection and encoding evidence,
  native/browser/OCaml and sanitizer/leak reports, unchanged source/corpus
  hashes, and ordered corpus/session parity proof. A concurrent full native
  run reached the existing 10-second `memory_copy.wast` deadline; its host-timeout
  report is retained separately. The native rerun uses the same deadline and
  jobs=1 after browser/sanitizer work finishes, with no policy override. The
  [setup ledger](wast-setup-coverage.md), [coverage](test-coverage.md) and
  [corpus guide](test-corpus.md) record the corrected nullability contract.
- Next slice: implement the standard **`spectest.table64` language-test binding**
  in C with correct address width, type and limits; verify native/browser/OCaml
  agreement before removing the final official setup XFAIL. Keep flat bulk
  syntax and broader language acceptance explicit. This is C language-test
  scaffolding, with no new OCaml kernel/provider development.

Stage 6B.45 landing slice — standard spectest table64 binding (2026-10-04):

- Added the standard `spectest.table64` binding in the shared C store: a
  separate nullable `funcref` table with **64-bit indices / initial size 10 /
  maximum 20**, matching the OCaml language-test host. Import compatibility
  still uses the existing loader checks for width, element type and limits;
  no special acceptance policy or import-time growth is added.
- The store owns and frees the table's entries. Imports within one script
  share its identity and cross-module function references; separate test
  sandboxes receive fresh tables. Checkpoints capture it even before a module
  imports it. Two added private ASan/UBSan checks verify nested growth/function
  ownership and restoration of the original null contents and limits.
- Added authored `tests/test-suite-spectest-table64.wast` (**36 assertions /
  three ordinary setups**) and `tests/test-suite-spectest-table64-isolation.wast`
  (**three assertions / one setup**) outside installed selection. They cover
  incompatible widths/types/limits, initial null entries, 64-bit bounds,
  same-module aliases, cross-module calls, growth to the maximum, failed/zero
  growth, separation from `spectest.table` and fresh-store isolation. Native
  and production-browser host gates run both manifest orders at jobs **1/3**.
- Native C, production browser and the OCaml **language-only** oracle accept
  the unchanged official file and both portable fixtures. OCaml also checks
  all **nine C-encoded modules / 36 assertions** in source order, preserving
  shared table state between actions and setups. Native warnings-as-errors
  ASan/UBSan and LeakSanitizer cover successful/rejected imports, table growth,
  cross-module references and teardown. Only then removed the final official
  setup XFAIL, `core/memory64/table64.wast`, from both baselines.
- Full native/actual offline Chromium parity is **289 PASS / 2 XFAIL / 5 SKIP**,
  **2,313 setup attempts / 2,313 successes / zero failures**. Preserve all
  **296 ordered identities / 64,309 assertion/action results**; only the
  table64 file changes XFAIL to PASS, with setups **10 → 11** successful.
  Twelve executor fixtures retain **1,230** results; six sessions preserve
  **358** checks and transcripts. Installed manifest/inventory and WAST bytes
  remain unchanged: **292 nonlegacy inputs / 15 groups / 497 VFS nodes / 314
  manifest files**. Both offline pages rebuild explicitly; distribution,
  packaging, syntax and whitespace gates pass without deadline overrides.
- Actual dashboard execution verifies the official table64 row now passes.
  An authored setup-recovery fixture is staged only by the host test into a
  separate row, preserving visible failure diagnostics at lines **4/5** despite
  all three assertions passing. Its JSON retains both setup failures; this
  keeps the setup reporting gate without manufacturing an installed failure.
- Evidence: `build/engine/refactor-stage6b-spectest-table64/` contains prior
  store/checkpoint/baseline snapshots, native/browser/OCaml and encoding
  reports, sanitizer/leak checks, unchanged source/corpus hashes and ordered
  corpus/session parity proof. Full corpus/session gates pass; a focused
  actual-dashboard rerun passes after supplying the synthetic fixture's required
  source-byte count. `validation-runs.json` distinguishes the initial staging
  metadata failure from completed corpus results. The
  [setup ledger](wast-setup-coverage.md), [coverage](test-coverage.md),
  [corpus guide](test-corpus.md) and
  [techniques](techniques.md) record the binding and remaining acceptance work.
- Next slice: repair valid **flat `table.init`, `data.drop` and `elem.drop` WAT
  syntax**, including the silent drop-instruction omission recorded in 6B.43.
  Compare folded/plain behavior, C-encoded modules and rejection diagnostics
  with the OCaml language oracle, preserving corpus/setup/session parity.
  Broader supported-language acceptance and C/CJS audits remain open. No C
  assertions are removed: cumulative moved coverage stays **188**; retained
  kernel coverage stays **21 helpers / 121 sites / 261 checks**. No OCaml kernel
  development is added; existing kernel removal remains deferred.

Stage 6B.46 landing slice — plain bulk instructions and folded index parity (2026-10-04):

- Implemented plain `table.init` with abbreviated and explicit table indices,
  and dedicated plain `data.drop`/`elem.drop` tokens and productions. Drops
  previously fell through permissive instruction parsing without emitting an
  opcode; invalid segment references could therefore appear valid. Plain
  initialization now uses the same deferred index-space resolution as folded
  input, with text **table/element** order encoded as **element/table**.
- Repaired folded `table.init` with numeric/numeric and numeric/named indices,
  which generic branches omitted. Folded named abbreviated forms with no
  stack operands now emit the instruction so binary validation rejects the
  missing operands. No capacity, executor, kernel or platform policy changes
  are needed. Bison's expected conflict count remains **24**.
- Added authored `tests/test-suite-flat-bulk.wast`, outside installed selection:
  **190 portable assertions / three ordinary setups**. Both 32/64-bit table and
  memory modules compare plain/folded abbreviated, numeric and forward-named
  indices. Distinct functions **7/11/29** verify the destination table, segment
  and untouched peer. Real data/element drops are repeated, followed by traps
  and successful zero-length initialization. The highest supported segment
  index **127** retains distinct data/function values. Fourteen invalid module
  assertions check unknown indices, missing stack operands and wrong address
  widths; six quoted malformed assertions check missing immediates and
  unresolved symbolic references.
- Native C, production browser and the OCaml **language-only** oracle agree on
  all checks. OCaml also verifies all **17 C-encoded setup/invalid modules**
  in the complete ordered script, with malformed text retained as quoted text.
  Nine ordinary setup/parse probes agree on acceptance/rejection, including the
  original three 6B.43 reproductions: valid plain initialization now loads,
  while unknown plain drop indices fail validation. Native warnings-as-errors
  ASan/UBSan host gates and LeakSanitizer check success, expected rejection,
  owned data payload cleanup and teardown.
- Full native/actual offline Chromium parity remains **289 PASS / 2 XFAIL /
  5 SKIP**, **2,313 successful setups / zero setup failures**, with all
  **296 ordered identities / 64,309 assertion/action results** and every prior
  setup record unchanged. Twelve executor fixtures retain **1,230** results;
  six sessions retain **358** checks and transcripts. Installed manifest,
  inventory and WAST bytes are unchanged: **292 nonlegacy inputs / 15 groups /
  497 VFS nodes / 314 manifest files**. Both offline pages rebuild; the actual
  dashboard still verifies the passing official table64 row and synthetic
  setup-failure diagnostics. Distribution/package, syntax and whitespace gates
  pass without deadline overrides.
- Evidence: `build/engine/refactor-stage6b-flat-bulk/` contains previous
  grammar/lexer snapshots, native/browser/OCaml and C-encoding reports,
  acceptance/rejection probes, sanitizer/leak checks, unchanged source/corpus
  hashes and ordered corpus/session parity proof. The [setup ledger](wast-setup-coverage.md),
  [coverage](test-coverage.md), [corpus guide](test-corpus.md) and
  [techniques](techniques.md) record the corrected syntax contract.
- Follow-up probes confirm **plain `table.copy` and `table.fill`** still lack
  grammar productions despite being recognized tokens: C rejects valid text
  accepted by OCaml. `flat-table-followup.json` keeps these two remaining gaps
  separate from the nine repaired acceptance/rejection probes.
- Next slice: implement **plain `table.copy`/`table.fill`**, compare their
  default/explicit indices and folded forms, and retain reference, overlap,
  address-width and bounds checks. Then enumerate supported installed official
  Wasm/WAT/WAST inputs into a finite identity-level OCaml acceptance/comparison
  ledger with explicit exclusions, preserving malformed/invalid/link/trap
  distinctions and bounded implementation limits. POSIX fixtures and OCaml
  kernel compatibility remain outside oracle scope. Remaining C/CJS coverage
  audits, C runtime profile gaps, Stage 6C and Stage 7 stay open. No C assertions are removed: cumulative moved coverage remains
  **188** and retained kernel coverage remains **21 helpers / 121 sites / 261
  checks**. No OCaml kernel development is added; removal remains deferred.

Stage 6B.47 landing slice — plain table copy/fill and folded index parity (2026-10-04):

- Added plain `table.copy` with default table zero or two explicit table
  indices, and `table.fill` with default zero or one explicit table index.
  Both copy immediates retain **destination/source** order in text and binary.
  Deferred named references preserve table index space and source locations,
  including mixed numeric/named and forward table declarations.
- Repaired omitted folded numeric/numeric and numeric/named copy forms, and
  numeric fill forms with or without stack operands. Missing operands now reach
  binary validation. No executor, capacity, kernel or platform policy changes
  are needed; Bison's expected conflict count stays **24**.
- Added authored `tests/test-suite-table-copy-fill.wast`, outside installed
  selection: **611 portable assertions / seven ordinary setups**. Plain and
  folded forms compare default, numeric, forward-named and mixed indices.
  Distinct source/target/peer functions verify destination/source order, both
  overlap directions and untouched peer tables. Bounds traps preserve all
  entries; zero-length operations at/beyond the boundary, null fills/copies,
  externref identity, non-null tables and cross-module function ownership are
  checked. Both table widths and both mixed-width copy directions retain their
  address/count types. Twenty-six invalid modules and three quoted malformed
  assertions cover missing operands, unknown indices/names, reference and
  address mismatches, and one-index copy text.
- Native C, production browser and the OCaml **language-only** oracle agree on
  the complete fixture. OCaml also verifies all **33 C-encoded setup/invalid
  modules** in source order. Seven ordinary acceptance/rejection probes agree,
  including both original 6B.46 plain copy/fill reproductions. Native
  warnings-as-errors ASan/UBSan host gates and LeakSanitizer verify success,
  rejection, owned parser payload cleanup and teardown. The fixture uses
  explicit table/export declarations; inline exported-table shorthand remains
  outside this slice's syntax contract and is recorded separately in
  `deferred-language-gaps.json`.
- Full native/actual offline Chromium parity remains **289 PASS / 2 XFAIL /
  5 SKIP**, with **2,313 successful setups / zero setup failures** and all
  **296 ordered identities / 64,309 assertion/action results** unchanged.
  Twelve executor fixtures retain **1,230** results; six sessions retain **358**
  checks and transcripts. Installed manifest, inventory and test bytes are
  unchanged: **292 nonlegacy inputs / 15 groups / 497 VFS nodes / 314 manifest
  files**. Both offline pages rebuild; actual dashboard success and synthetic
  setup diagnostics, distribution/package, syntax and whitespace gates pass
  without deadline overrides.
- Evidence: `build/engine/refactor-stage6b-table-copy-fill/` contains the prior
  parser/corpus snapshots, native/browser/OCaml and C-encoding reports,
  acceptance/rejection probes, sanitizer/leak checks, unchanged source/corpus
  hashes and ordered corpus/session parity proof. The [setup ledger](wast-setup-coverage.md),
  [coverage](test-coverage.md), [corpus guide](test-corpus.md) and
  [techniques](techniques.md) record the copy/fill syntax and verification.
- Next slice: enumerate supported installed official **Wasm/WAT/WAST inputs**
  into a finite identity-level OCaml acceptance/comparison ledger, with explicit
  exclusions and malformed/invalid/link/trap distinctions. Record bounded
  implementation limits and unsupported text shorthand instead of inferring
  acceptance from assertion totals. POSIX fixtures remain outside oracle scope.
  Remaining C/CJS audits, C runtime profile gaps, Stage 6C and Stage 7 stay open.
  No C assertions are removed: cumulative moved coverage stays **188** and
  retained kernel coverage stays **21 helpers / 121 sites / 261 checks**. No
  additional OCaml kernel development is planned; removal remains deferred.

Stage 6B.48 landing slice — finite official language comparison ledger (2026-10-04):

- Added `tests/language-oracle-check.py` and the durable
  [official language ledger](wasm-language-coverage.md). Every installed identity
  is classified: **265 official identities / 261 compared inputs / four legacy
  syntax exclusions**, plus **31 repository fixtures outside official scope**.
  Every compared input is accepted independently by C and OCaml; **259** agree
  on ordered language-check identity/kind and ordinary setup counts. **Two
  differences remain explicit**; the comparison inventory is complete, while
  supported-language parity remains open.
- Audited installed inventory, upstream/authored hashes and pinned spec revision
  before comparison. OCaml runs unchanged installed scripts in fresh native
  processes with standard spec scaffolding. Enable `-ca` only for `custom/*`:
  enabling annotation handlers for core opaque binary sections recursively
  decodes arbitrary payloads. Preliminary evidence retains that corrected
  profile error; no OCaml code or kernel capability is added.
- Compare C parser metadata, actual command-stream assertion/setup/EOF reports
  and independent OCaml assertion traces, without another WAST command scanner.
  Preserve malformed/invalid/custom/link/runtime-trap/instantiation-trap/
  exception/exhaustion/return/bare-action distinctions and ordered names. Name
  display normalization decodes byte/Unicode escapes without Unicode folding
  or prefix matching. The official inputs retain **62,975 assertion/action
  checks / 2,261 C instantiations**; OCaml records **2,260 instantiations**.
  Traces exercise **810 binary definitions / 1,263 quoted text definitions**.
  This is installed-script comparison, not a claim that every C-encoded module
  has been cross-run or arbitrary language syntax is supported.
- The ledger exposes `core/names.wast` check **47**: a **257-byte Unicode name**
  is truncated to **255 bytes** in both C declaration and invocation. All
  **482** checks pass independently, but full action identity differs. Focused
  probes confirm C rejects a valid pair of long names sharing a prefix and
  fails equivalent `"\41"`/`"A"` export/action spellings accepted by OCaml.
  `core/inline-module.wast` accepts zero checks in both runtimes, but OCaml
  validates a definition with zero instances while C instantiates once.
- Added source-hash/identity/issue-kind-pinned
  `tests/language-oracle-policy.json`: new gaps, changed gap categories or
  repaired baselines fail; `--strict` also fails for recorded gaps. Known-gap
  success does not establish parity. Focused negative controls in
  `tests/language-oracle-ledger-check.py` protect omission/order/category
  mismatches, escaped names and distinct Unicode, failed/incomplete setup/EOF,
  duplicate/missing/stale report identities and expanded-gap rejection.
- A fresh full native corpus run remains **289 PASS / 2 XFAIL / 5 SKIP**,
  **2,313 successful setups / zero setup failures**. Reconciliation with the
  Stage 6B.47 actual offline-browser reports preserves all **296 identities /
  64,309 assertion/action records** and setup records. Runtime sources/binaries,
  installed manifest/inventory/test bytes and expected-failure lists are
  unchanged; no engine or platform behavior changes in this audit. The ledger
  records report hashes; existing reports lack source digests, so callers must
  supply current reports. Distribution, Python/shell syntax and whitespace
  gates pass without production deadline overrides.
- Evidence: `build/engine/refactor-stage6b-language-ledger/` retains preliminary
  and final traces/metadata, name-boundary probes, pinned ledger/policy, fresh
  native results, previous actual-browser reports, unchanged runtime/corpus
  hashes and complete ordered parity proof. [Setup](wast-setup-coverage.md),
  [coverage](test-coverage.md), [corpus](test-corpus.md) and
  [techniques](techniques.md) describe the reproducible comparison contract.
- Follow-up: full **export/action names**, byte/Unicode escape equivalence,
  bounded overflow handling and collision probes landed in Stages 6B.49–6B.50;
  the names ledger gap is removed. The inline definition/instantiation profile
  difference and exported-table shorthand gap remain visible for subsequent
  resolution. Remaining C/CJS audits, C runtime profile gaps, Stage 6C and Stage
  7 remain open. No C assertions are removed:
  cumulative moved coverage stays **188**; retained kernel coverage stays
  **21 helpers / 121 sites / 261 checks**. OCaml kernel removal remains deferred;
  no additional OCaml kernel development is planned.

Stage 6B.49 implementation slice — decoded bounded names (2026-10-05):

- Traced the `core/names.wast` gaps through WAT token retention, parser metadata,
  text encoding, binary decoding, runtime export lookup and the browser API.
  Quoted strings are retained as raw source interiors, so byte and Unicode
  escapes are not decoded when they become import/export/action names. The
  encoder then writes those source characters as name bytes. Parser metadata,
  binary import metadata and browser bridge buffers also use fixed-size C
  strings; some parser and browser paths truncate, while the binary decoder
  rejects names at its current bound. These are shared representation issues,
  not only a WAST action-lookup defect.
- Added a shared WAT name-copy path that decodes byte and Unicode escapes for
  function/global/table/memory/tag imports and exports and invoke/get actions.
  This keeps ordinary quoted data and custom-section string decoding on their
  existing path. The fixed internal name bound is now **511 bytes plus NUL**;
  text parsing reports an error on overflow instead of truncating. Binary import
  decoding, native export loading and browser binary-name parsing use the same
  bound and reject embedded NUL rather than allowing C-string lookup collisions.
  Browser action APIs now return an error for over-bound names rather than
  shortening them.
- This slice is not yet verified. Embedded NUL names remain unsupported because
  engine export and import lookup still uses C strings, and some non-export
  metadata names retain the older copy path. Therefore `core/names.wast` remains
  open in the Stage 6B.48 ledger and policy; no claim of full byte-name parity
  is made. Inline definition/instantiation profile behavior and exported-table
  shorthand remain separate open gaps.
- Next slice: run the language oracle on authored and C-encoded WAST, exercise
  collision and overflow probes, and compare native C with the production
  browser runtime. Fix any failures across all name-bearing paths before
  changing the names ledger. Then scope a length-bearing representation for
  embedded NUL names and remove remaining silent name copies. Keep the two
  unrelated language gaps visible. Remaining C/CJS audits, C runtime profile
  gaps, Stage 6C and Stage 7 remain open. No C assertions are removed;
  cumulative moved coverage stays **188** and retained kernel coverage stays
  **21 helpers / 121 sites / 261 checks**. OCaml kernel removal remains deferred;
  no additional OCaml kernel development is planned.

Stage 6B.50 landing slice — byte-preserving names and parity (2026-10-05):

- Added a reversible internal C-string encoding for raw name bytes: NUL maps to
  marker-plus-`0`, and marker byte `0x01` maps to marker-plus-`1`. All other
  bytes remain unchanged. WAT names first decode byte/Unicode escapes, then use
  this mapping; the Wasm encoder reverses it. Binary import/export loading and
  the browser binary-name reader apply the same mapping, so zero and marker
  bytes remain distinct through linking, export lookup, assertions and browser
  actions. The 512-byte storage bound gives ordinary names up to 511 bytes and
  reports overflow rather than truncating.
- The language-ledger adapter now distinguishes C's internal representation
  from OCaml's displayed WAT escapes. Removed `core/names.wast` from the
  expected-gap policy after the fresh comparison matched all **482 ordered
  checks** and **four setup records**. The official ledger is now **260 agreed
  inputs / one open gap** (`core/inline-module.wast`, setup-profile difference).
  The native corpus remains **289 PASS / 2 XFAIL / 5 SKIP**. Native and actual
  offline-browser runs of `core/names.wast` each pass **482/482** and match on
  ordered action names, outcomes and setup counts.
- Focused authored and C-encoded WAST probes cover equivalent `\\41`/`A`
  spellings, distinct `\\00` and `\\01` names, and two distinct **257-byte**
  names with a shared 256-byte prefix. The native runner passes every assertion;
  the OCaml oracle accepts and traces each C-encoded module/action. The
  **511-byte** boundary passes; **512** is
  rejected with `WAT name exceeds supported representation`. The probes and
  fresh ledger artifacts are under
  `build/engine/refactor-stage6b-name-parity/`.
- The full offline-browser harness still reports a failure in
  `engine-regressions/continuation-waits.wast` (three setups pass, but execution
  is incomplete with zero assertion results). The focused names dashboard run
  passes; the full-browser failure is retained for separate investigation and
  is not recorded as name parity evidence. No installed VFS tests or manifest
  bytes were changed.
- The inline definition/instantiation profile difference is resolved in Stage
  6B.51; exported-table shorthand is implemented in Stage 6B.52. The name gap
  stays removed unless a fresh pinned comparison regresses. Remaining
  C/CJS audits, C runtime profile gaps, Stage 6C and Stage 7 remain open. No C
  assertions are removed; cumulative moved coverage stays **188** and retained
  kernel coverage stays **21 helpers / 121 sites / 261 checks**. OCaml kernel
  removal remains deferred; no additional OCaml kernel development is planned.

Stage 6B.51 landing slice — inline WAST modules are definitions (2026-10-05):

- The final language-ledger difference was a profile mismatch: the official
  `core/inline-module.wast` contains bare inline module fields. OCaml validates
  those fields as a module definition and performs no instantiation, while the
  C parser previously treated them as an ordinary module command and the WAST
  runner instantiated them.
- The bare-inline-module grammar now marks its module as a definition, matching
  explicit `(module definition ...)` behavior. The C WAST runner therefore
  retains/validates the fields without instantiating them. The focused native
  corpus run reports **1 PASS**, with zero assertions and zero setups, matching
  the OCaml trace. The full native corpus reports **289 PASS / 2 XFAIL / 5
  SKIP**. The browser Wasm build passes, and the focused offline-browser run of
  `core/inline-module.wast` passes. The strict full official-language ledger
  rerun is recorded under
  `build/engine/refactor-stage6b-inline-module/comparison/`.
- Removed the pinned `core/inline-module.wast` expected-gap entry. The fresh
  comparison confirms **261/261 agreed** with zero expected gaps. The
  exported-table shorthand syntax gap
  remains separate and visible, as do remaining C/CJS audits, C runtime profile
  gaps, Stage 6C and Stage 7. No C assertions are removed; cumulative moved
  coverage stays **188** and retained kernel coverage stays **21 helpers / 121
  sites / 261 checks**. No additional OCaml kernel development is planned;
  removal remains deferred.

Stage 6B.52 landing slice — exported table32/table64 shorthand (2026-10-05):

- Implemented inline exports on table shorthand forms whose element list
  supplies the inferred initial size: `(table (export "name") funcref
  (elem ...))` and the table64 form with `i64`. The shorthand retains the
  table's reference type, synthesizes the active offset-zero element segment,
  and associates its table index correctly when other tables precede it.
- Added `tests/test-suite-exported-table-shorthand.wast` with four assertions:
  table32 and table64 indirect calls through the inline exports, plus importing
  the table32 export into a second module and calling through the shared alias.
  The prior C rejection at `funcref` is resolved. Native C and the OCaml
  language-only oracle pass all four checks; the actual offline browser worker
  passes the same fixture. The forced browser Wasm rebuild and native
  warnings-as-errors build pass.
- Exported-table shorthand is no longer an open syntax gap. The 261-input
  official ledger remains **261/261 agreed**; the new probe is repository-owned
  and does not change installed VFS bytes or official identity counts. Remaining
  C/CJS audits, C runtime profile gaps, Stage 6C and Stage 7 stay open. No C
  assertions are removed; cumulative moved coverage stays **188** and retained
  kernel coverage stays **21 helpers / 121 sites / 261 checks**. No additional
  OCaml kernel development is planned; removal remains deferred. A fresh full
  native corpus run remains **289 PASS / 2 XFAIL / 5 SKIP**, and the strict
  language comparison reports **261/261 agreed**, with no unexpected or
  repaired gaps. Probe/build/comparison evidence is under
  `build/engine/refactor-stage6b-export-table/`.

Stage 6B.53 landing slice — process-lifecycle C assertion audit (2026-10-05):

- Audited every runtime assertion in `tests/c-engine-process-lifecycle.c`:
  **117 CHECKs** across capsule/address-space ownership, handler cursors and
  wait transitions, exec/wake/reap lifecycle, process-group signal status, and
  mmap/file-backed shared-page ownership. The ordinal ranges, existing guest
  complements, and disposition of every check are recorded in
  [the coverage audit](test-coverage.md#process-lifecycle-c-gate-audit-stage-6b53).
- Guest/session tests already cover observable fork results, handler READ and
  SELECT resume, exit status, wait/reap, process-group signals, mmap behavior,
  and private-memory isolation. They cannot inspect borrowed-memory ownership,
  capsule transition labels, exact stream cursor state, cached-page identity,
  mapping-record splits, or fork-root engine ownership; these checks remain in
  the native C gate. No assertions were removed or duplicated in WAST.
- The audit found that dynamically loaded library names were stored using the
  256-byte executable-path bound despite accepting names up to the 512-byte
  WAST bound. Changed `native_loaded_library.name` to use
  `WAST_MAX_EXPORT_NAME`; path storage remains separately bounded. The
  warnings-as-errors ASan/UBSan `process-lifecycle` target now passes **117/117**.
- This is one tranche of the broader authored C/CJS audit, not a cutover
  declaration. Continue the assertion mapping across remaining standalone C
  probes and host/browser harnesses before consolidation. Full native corpus,
  official language-ledger and Stage 6C/7 gates remain as recorded above; no
  additional OCaml kernel development is planned.

Remaining Stage 6B priorities: coverage audit and portable fixtures
(user direction, 2026-10-03):
- Native enumeration, selection, reporting, expected failures and bounded
  execution are provided by Stage 6B.17; Stage 6B.18 connects the installed
  manifest and assertion contract to the production browser worker. Complete
  the coverage audit of the older harness before replacing it; Stage 6B.20
  supplies the installed guest batch launcher. Browser JavaScript remains
  responsible for browser events.
- Keep per-assertion native/browser comparisons and the retained legacy
  classifications as cutover gates. Audit ordinary setup-module diagnostics
  separately; do not infer full module acceptance from passing assertions.
  Complete supported Wasm/WAT/WAST language-oracle comparisons before
  claiming runner acceptance; OCaml POSIX support is not an acceptance gate.
- Audit authored `tests/*.c` and `tests/**/*.cjs` by assertion, recording
  each retained or migrated boundary. Convert guest-observable language,
  kernel/POSIX and libc checks to WAST clients using the real guest imports.
  Keep authored C guest clients when testing C headers, layout or compilation;
  compile them to Wasm and wrap their exported checks with WAST assertions
  rather than translating away the C interface being tested.
- Move interaction expectations from duplicated Node/native drivers into
  the shared session contract where possible. WAST alone cannot supply
  outside-the-guest terminal events, host cancellation or upload dialogs;
  the runtime must drive those events and expose their results consistently.
- Retain focused native C checks for bounded-reader cursor rollback,
  engine ownership, teardown and checkpoints that the guest ABI does not
  expose, with warnings-as-errors and ASan/UBSan. A WAST behavior regression
  may complement these checks; it does not establish their internal
  invariants. Keep private engine headers out of the guest SDK.
- Retain focused browser checks for the actual Wasm artifact, worker protocol,
  terminal JavaScript, DOM/rendering, packaging and offline `file://` boot.
  These cannot all become guest WAST assertions with equivalent coverage.
  Their use of Node is an automation choice, not a requirement for ordinary
  engine corpus execution.
- Prioritize this runner/fixture migration over further Node orchestration
  optimization. WebAssembly-language oracle comparison, C scripted-session
  parity and coverage accounting remain required before cutover.

Counted completion inventory and estimate (updated through Stage 6B.99, 2026-10-05):

- The current authored-source denominator is **56 files under `tests/`: 28 C
  and 28 CJS**. This is a file inventory, not 56 independent suites: it
  includes guest compile clients, fixture helpers, shared modules and focused
  host/browser drivers. Existing per-gate audits cover substantial portions
  (including the complete 261-check retained-kernel inventory, 188 checks
  moved to portable fixtures, multiple process/store C gates, VFS/package
  gates, and the browser/session harnesses). The coverage map now names **56/56
  files** (28 C, 28 CJS); no source files remain unnamed. “Named” is only a
  citation measure, not proof that the file's assertions have a completed
  disposition. All 56 are now cited; this remains an upper-bound audit
  denominator because some files are helpers or compile-only clients rather
  than assertion-bearing tests.
- The work left has **8 explicit checklist groups** across the remaining
  phases: Stage 6B is complete; Stage 6C has **3** (apply mappings and
  consolidate; split build/native/focused-browser/full-browser actions; measure
  and document filtered/release workflows); Stage 7 has **5** (remove the old
  dashboard and payload path; remove/replace dispatch options; inventory
  focused compatibility flags; eliminate obsolete paths/references and update
  docs; enumerate and remove only obsolete generated artifacts). These are
  grouped acceptance items, not one-slice-per-bullet estimates.
- The current working estimate is **4–6 additional landing slices**:
  **2–3** for Stage 6C and **2–3** for Stage 7. The production-browser acceptance
  group closed in Stage 6B.96; Stage 6B.97 fixed and verified the external
  executable startup-cwd contract and added worker-host negative-path checks.
  Stage 6B.98 restored exact citations for the four omitted sources and closed
  the uncited-source queue; Stage 6B.99 verified that all 56 references resolve
  to assertion tables or dedicated disposition sections. Stage 6B.93
  refreshed the reviewed browser-provider source hashes and installed SDK
  metadata; the strict SDK gate now passes. Stage 6B.97 resolved the unexpected `/bin/waste-probe`
  startup cwd recorded in Stage 6B.85. This host's local
  `file://` Chromium/Firefox launches remain blocked by startup crashes, while
  the user's full production-browser Bash-page batch passed in Stage 6B.96.

- Support single-file execution through the existing `/bin/wast` path and a
  documented `/bin/waste-test` batch command (landed in Stage 6B.20 as a
  freestanding guest C launcher with an opt-in host batch capability).
- Provide list/group/file selection, per-test isolation, assertion-aware
  pass/fail/skip results, bounded cancellation/timeouts, aggregate exit status,
  and machine-readable results usable by CI.
- Do not reduce results to a shell command's exit code when the dashboard
  previously exposed individual assertions or expected failures.
- Use a shared test manifest and scenario/result contract across the expanded
  native driver and production browser worker. Keep host stores, kernels,
  and process address spaces isolated between scheduled tests while preserving
  intentional imported-memory aliases within one test.
- Adapt `tests/c-engine-browser-runtime.cjs` to the unified offline page and
  mounted manifest; retain focused and full-suite browser automation.
- Retain the official OCaml interpreter solely as the Wasm/WAT/WAST language
  oracle. Compare supported official language tests against it. Do not add
  POSIX/kernel providers or require OCaml kernel parity. Account for useful
  legacy direct/threaded POSIX assertions in C before retiring their drivers
  under the deferred OCaml cleanup plan.
- Compare the old and new harness over the complete baseline corpus, including
  malformed/invalid modules, linking failures, traps, and interrupted tests.
- Preserve a usable shell and downloadable diagnostics/results after a batch
  finishes or fails. Avoid adding an external server or network dependency.

Gate: native and browser runners preserve previous coverage and result
semantics, with full C native/browser cutover parity and supported
Wasm/WAT/WAST language-oracle comparisons. A missing or
unsupported suite remains visible rather than disappearing during migration.

### Stage 6C: Consolidation and build/test separation

- Apply the assertion-level old-to-new coverage mapping. Consolidate shared
  setup and overlapping shell scenarios, retaining distinct boundary coverage.
- Implement separate page-build, primary-native-test, focused-browser-check,
  and full-browser-check actions from the proposed command contract.
- Adapt existing Node worker tests to consume the same manifests/fixtures,
  while retaining frontend model and actual browser checks where required.
- Measure suite duration/startup changes, verify filtered diagnostic runs,
  and document normal development versus full release verification.

Gate: building `bash.html` no longer implicitly runs the expensive suite;
every removed test has a retained coverage mapping and native/browser CI still
reports failures through a common machine-readable result contract.

Stage 6C.1 (2026-10-05) separates the Bash page build from its focused checks.
`./start.sh --html-bash` now stops after compile, VFS installation, staging and
HTML amalgamation; it no longer requires Node or runs the worker/model suite.
`./start.sh --html-check` runs the focused worker and terminal-model gates, and
`./start.sh --html-browser-full` explicitly starts full offline Chromium
acceptance. The existing assertion-level disposition ledger cites all 56/56
authored C/CJS harnesses; this slice records it as the retained source mapping,
while semantic consolidation and remaining assertion-boundary review stay
open. `docs/test-corpus.md` now distinguishes ordinary development commands
from release verification and points filtered investigations to the native
batch runner.

Measured on this host: page build **92.1 s**; focused worker/model checks
**38.3 s**. Both passed. The build log contained only build/package/install/
amalgamation steps. The focused check completed its terminal model, Bash
prompt/continuation/readline, heredoc/pipe, aggregate utility matrix,
shared-library/Rogue, clock, and packaged-mtime gates without launching
Chromium. The compiled-engine VFS harness passed the full 296-file content
and inventory checks. The additional shell redirection sweep failed on its
first file, `/tests/core/address.wast`; the same saved-descriptor duplication
failure reproduces on `/usr/share/waste/launch.wast`. `test-corpus-bash.py
--limit N` now bounds that diagnostic run. A forced rebuild corrected the
diagnosis: legacy `env.fcntl` failures must return `-1` and set guest `errno`,
and the descriptor fixture now checks that POSIX contract. The remaining
interactive-shell failure reports `EMFILE` (errno 24), identifying descriptor
availability at Bash's `F_DUPFD` save/restore boundary as the open issue. Keep
that boundary separate from the passing VFS check. Full-browser
acceptance was not rerun in this slice; a clean local Chromium pass remains
required before the Stage 6C gate closes. Stage 6C now
has an explicit split, with **1–2 landing slices** estimated for remaining
descriptor-pressure diagnosis, semantic consolidation and full-browser/result-
contract acceptance. The native installed-corpus run passed **289 PASS / 2
XFAIL / 5 SKIP**, zero unexpected results, in **58.5 s** wall time with four
jobs; the JSON report is `build/cli-rt/corpus-results.json`. The report stores
per-test elapsed times (228.6 s summed across concurrent jobs), so that sum is
not the suite wall time. Stage 7 remains separately estimated at **2–3 slices**.

## Stage 7: Retire `test.html` and close the layout migration

Status: complete

- Removed the standalone dashboard frontend, HTML-generation path, and
  `build.sh tests` target. Preserved the shared corpus/oracle collector and JSON
  payload generator used by focused worker conformance checks.
- Retired `--html-test`, `--generate-html`, and `--c-engine-html` from
  menu/help; direct invocations explain their replacements. Kept focused browser
  worker gates and the `bash.html` full-browser gate.
- Documented native testing through the shared VFS and browser testing through
  `bash.html` diagnostics and its installed `/tests` corpus. Kept `--html-bash`,
  native `--cli-test`, and the OCaml language-oracle build/run path. Legacy
  OCaml application-runtime removal remains deferred.
- Removed obsolete page sources/references from active tooling and instructions;
  dated plan entries retain historical command names as records, not runnable
  workflows. Updated `AGENTS.md`, architecture, techniques, build, and corpus
  instructions.
- Removed the exact generated `build/html-rt/test.html` artifact after the
  package and runtime gates passed.

Gate: one browser page serves shell and test execution; no supported workflow
requires the retired dashboard or an old staging path.

## Verification and completion

Run focused checks after each stage and the full relevant matrix at cutover:

- Native CLI build, official Wasm/WAT/WAST C/oracle comparisons, and
  warnings-as-errors ASan/UBSan executor/kernel/process gates when headers
  or execution paths move.
- Native guest-runtime shell/command/pipeline/library/TTY suites using the
  same VFS and guest Wasm modules as the browser; compare shared scenario results.
- Focused browser artifact, worker, input/resume, and package integration gates;
  terminal model/GLF fixtures and actual browser upload/download/render checks.
- Full browser core/proposal/annotation corpus plus relevant DIY POSIX and
  libc probes through the C runner. OCaml remains the Wasm/WAT/WAST language
  oracle only; its legacy POSIX/kernel probes are outside runtime acceptance.
- Offline `file://` startup, font/rendering, mounted file paths, shell/test
  interaction, and absence of external runtime requests.
- Deterministic package inventory, permissions, mtimes, import closure,
  corresponding-source/license mapping, and clean/incremental build behavior.
- `bash -n start.sh`, Python bytecode checks for changed tools, Node syntax
  checks for changed JS, and `git diff --check`.

Completion criteria are satisfied: `src/vfs` is shared by both runtimes, the
selected source/public-header layout is the maintained layout, native testing
runs guest applications and the shared-engine corpus, and focused plus explicit
full browser checks preserve platform coverage. Removed tests have coverage
mappings, page packaging is separate from expensive verification, obsolete
dashboard build options are retired, and the replacement workflow is documented.
This file is retained as the completed migration record.


Stage 6B.54 landing slice — exec-transition matrix C assertion audit (2026-10-05):

- Audited all **58 runtime CHECKs** in `tests/c-engine-exec-matrix.c`. The
  ordinal ranges, guest/session complements and retention rationale are in
  [the coverage audit](test-coverage.md#exec-transition-matrix-c-gate-audit-stage-6b54).
- The session matrix already exercises installed executable commands, output,
  command statuses, return to the shell prompt and process exit. It cannot
  inspect kernel metadata bindings, candidate-image ownership before commit,
  startup-block placement, continuation references, handler-payload transfer
  and rejected context lifetimes, or process-engine table independence. These
  remain assertions in the native ASan/UBSan gate; no checks were moved or
  removed.
- `make -C src/cli-rt exec-matrix` passes with warnings-as-errors and
  ASan/UBSan: **58 checks, 0 failures**. No implementation change was needed.
- Continue assertion-level audits across remaining native C probes and CJS
  host/browser harnesses; Stage 6C consolidation and Stage 7 retirement remain
  open.


Stage 6B.55 landing slice — exec lifecycle continuation C gate (2026-10-05):

- Audited all **22 runtime CHECKs** in `tests/c-engine-exec-lifecycle.c` over
  two controlled child-exec outcomes. The checks cover fixture loading/export,
  fork and exec yields, continuation capture, wrong-engine and duplicate-resume
  rejection, failed-exec exit 127, parent resume, wait/write activity, and
  successful-exec parent completion.
- The real guest process/session fixtures exercise end-to-end exec and shell
  outcomes. This driver instead isolates scheduler continuation behavior with
  host-controlled yields, which portable WAST cannot trigger or inspect by
  itself; keep it as a native API gate.
- Fixed a stale direct `exec_memory.data` access in the fixture host write import.
  Guest memory is sparse page-backed now, so the callback copies bytes via
  `exec_memory_read`, preserving memory bounds and structured errors. The
  warnings-as-errors ASan/UBSan `exec-lifecycle` target passes **22/22**.
- Continue the C/CJS audit; Stage 6C consolidation and Stage 7 dashboard
  retirement remain open.


Stage 6B.56 landing slice — process-contract C gate audit (2026-10-05):

- Audited all **12 runtime CHECKs** in `tests/c-engine-process-continuation.c`:
  fixture load; child exit and fork/wait counts; child memory marker; explicit
  child-to-parent memory reset; parent invocation and wait status; and the
  parent marker.
- `guest-session-linked-fork.wast`, `guest-session-waits.wast` and related
  session fixtures cover guest-visible fork/wait outcomes. This legacy driver
  selects child and parent phases explicitly and checks the process import ABI
  and their distinct memory effects. The later exec-lifecycle and linked-fork
  gates exercise scheduler continuations end to end, so retain this small
  contract gate as focused compatibility coverage without migrating it to WAST.
- Replaced stale direct flat-memory accesses in the host wait callback and
  assertions with `exec_memory_read`/`exec_memory_write`, matching sparse
  page-backed memory. Added a CHECK for the deliberate reset between simulated
  child and parent phases. The warnings-as-errors ASan/UBSan
  `process-continuation` target passes **12/12**.
- Continue the remaining C/CJS audit; Stage 6C consolidation and Stage 7
  dashboard retirement remain open.


Stage 6B.57 landing slice — repeated wait continuation C gate audit (2026-10-05):

- Audited all **11 runtime CHECKs** in `tests/c-engine-two-wait.c`: fixture
  open/read/load/export, first READ yield and continuation capture/resume, a
  second READ yield and replacement capture/resume, then final result and four
  host calls.
- Shared session wait fixtures exercise real input/readiness and guest-visible
  completion. This compact C probe holds one engine image while it is captured,
  resumed, suspended again, and captured a second time; retain it as a focused
  evaluator continuation test rather than duplicating it in WAST.
- The warnings-as-errors ASan/UBSan `two-wait` target passes **11/11**. No code
  change was required. Continue remaining C/CJS audits; Stage 6C consolidation
  and Stage 7 dashboard retirement remain open.


Stage 6B.58 landing slice — store checkpoint C gate audit (2026-10-05):

- Audited all **32 checks** in `tests/c-engine-store-checkpoint.c`: sparse and
  shared-memory fixture setup; rejection of live handler snapshots; process
  file-mapping topology; nested checkpoint capture/restore after memory/table
  growth; restoration of imported memory/table/global identity, table64 host
  state, globals and evaluator frames; and clone binding with explicitly shared
  memory. The per-range disposition is in [the coverage audit](test-coverage.md#store-checkpoint-c-gate-audit-stage-6b58).
- WAST/session tests cover guest-visible state isolation and memory operations,
  but cannot inspect store snapshots, alias identity, mapping file-object
  topology, evaluator yield frames or clone-provider resolution. Keep these as
  native private-state checks; no WAST duplication or assertion removal.
- The warnings-as-errors ASan/UBSan `store-checkpoint` target passes
  **32 checks, 0 failures**. No implementation change was needed.
- Continue the remaining authored C/CJS audit; Stage 6C consolidation and Stage 7
  dashboard retirement remain open.


Stage 6B.59 landing slice — installed VFS inventory gate audit (2026-10-05):

- Audited all **27 runtime `assert` sites** in
  `tests/c-engine-vfs-inventory.c`. With the installed snapshot currently at
  497 entries (448 regular files, 49 directories), its loops execute **4,380**
  assertions; the exact capacity and inventory expansion is recorded in
  [the coverage audit](test-coverage.md#installed-vfs-inventory-c-gate-audit-stage-6b59).
- `make -C src/cli-rt vfs-check` passes the ASan/UBSan inventory gate, the
  eight mounted-path assertions, twelve guest SDK mounted-path assertions, and
  the Python directory audit. The capacity fixture confirms 960 installed
  entries leave 64 path nodes for runtime creation, then rejects overflow.
- Existing guest checks cover mounted file access/modes. Metadata and byte-for-byte
  inventory parity, failed-replacement safety, isolation between mounted kernels,
  host snapshot preservation and path-node capacity remain host/native contracts.
  No assertion was migrated or removed.
- Continue remaining authored C/CJS audits; Stage 6C consolidation and Stage 7
  dashboard retirement remain open.


Stage 6B.60 landing slice — browser VFS staging harness audit (2026-10-05):

- Audited `tests/c-engine-vfs-browser.cjs` and its shared
  `tests/vfs-package.cjs` helper. Together they have 12 assertion sites and
  **6,294 runtime assertion executions** for the current package (446 staged
  files and six full staging cycles). The assertion groups and dynamic
  expansion are recorded in [the coverage audit](test-coverage.md#browser-vfs-staging-cjs-gate-audit-stage-6b60).
- The harness passes malformed-inventory and wrong-file rejection checks; stages
  inventory plus files; repeats mounted-path WAST in fresh stores; runs compiled
  SDK ABI/stat/signal probes; and compiles a probe that opens, reads to EOF, and
  verifies length and endpoint bytes for all **296** packaged corpus paths.
- `node tests/c-engine-vfs-browser.cjs build/html-rt/bash.html` passes all browser
  VFS checks. This is a focused host/browser packaging gate; ordinary guest WAST
  remains Node-independent. No assertion was removed or migrated.
- Continue the remaining C/CJS audit; Stage 6C consolidation and Stage 7
  dashboard retirement remain open.


Stage 6B.61 landing slice — tarballjs loader CJS audit (2026-10-05):

- Audited all five assertion sites / **10 executions** in
  `tests/vfs-tar-loader.cjs`. Valid members cover a full-width ustar name, a
  combined prefix/name path, byte fidelity and a `__proto__` entry without
  prototype pollution. Six rejected archives cover a corrupt header checksum,
  traversal and absolute paths, duplicate normalized names, an unsupported type,
  and truncated input.
- `node tests/vfs-tar-loader.cjs` passes against the production tarballjs
  extractor and browser loader. This focused Node harness validates the
  browser archive boundary; runtime guest tests remain directly runnable by the
  CLI and browser engine.
- No implementation change was needed. Continue the C/CJS audit; Stage 6C
  consolidation and Stage 7 dashboard retirement remain open.


Stage 6B.62 landing slice — VFS packaging CJS gate audit (2026-10-05):

- Audited all **19 assertion sites / 488 executions** in
  `tests/c-engine-vfs-packaging.cjs` for the current package. The ledger maps
  per-file offline/source byte parity, tar creation and archive audit, native
  mounted-path results, copied-tree audit, five malformed inventory mutations,
  five invalid component-install batches, successful atomic refresh, and
  protection against a modified installed file.
- `node tests/c-engine-vfs-packaging.cjs build/html-rt/bash.html` passes. The
  test distinguishes installed-directory and tar archive bytes/metadata, then
  verifies unsafe/conflicting/omitted/partial inputs fail before publication
  without changing the inventory or existing upload binary.
- These checks complement Stage 6B.59 inventory parsing and Stage 6B.60 browser
  transfer/staging tests; they remain host packaging/tooling assertions rather
  than guest WAST semantics. No code or assertions changed.
- Continue remaining authored C/CJS audits; Stage 6C consolidation and Stage 7
  dashboard retirement remain open.


Stage 6B.63 landing slice — browser upload/download transfer gate audit (2026-10-05):

- Audited the six explicit assertions and seven protocol waits in
  `tests/c-engine-vfs-transfer.cjs`. The scenario exercises binary upload and
  download roundtrip, verifies the suggested filename and exact payload bytes,
  confirms cancelled upload does not create a downloadable file, then verifies
  a subsequent shell command and clean worker exit.
- `tests/guest-session-transfer.json` and `.wast` already cover the four guest
  upload/download outcomes, including cancellation. This browser-worker harness
  adds actual terminal command/prompt sequencing, delayed asynchronous host
  replies, emitted download object details, post-cancellation filesystem state
  and continued worker usability. Retain these host integration checks.
- `node tests/c-engine-vfs-transfer.cjs build/html-rt/bash.html` passes. Removed
  unused `treeVfs`/`stageVfs` imports from the harness; no test behavior changed.
  `git diff --check` passes.
- Continue remaining authored C/CJS audits; Stage 6C consolidation and Stage 7
  dashboard retirement remain open.


Stage 6B.64 diagnostic slice — frontend packaging crash trace (2026-10-05):

- `tests/c-engine-frontend-packaging.cjs` now writes synchronous progress records
  to `build/html-rt/frontend-packaging.log` by default. Override the destination
  with `WASTE_FRONTEND_PACKAGING_LOG`; a relative override is resolved from the
  repository root. The log identifies the PID, start/exit, current page, corpus
  source, VFS path, generator guard or package guard, and uncaught exception
  stack. Each line is appended synchronously; later invocations append a new
  run rather than erasing earlier crash history. The first instrumented run
  stopped after entering the Bash page generic assertions; it recorded neither
  an uncaught exception nor normal exit. No host coredump or kernel OOM/kill
  event was found for that run window, so the terminating cause is unconfirmed.
  Generic assertions now each have their own pre-checkpoint for the next run.
- `node --check tests/c-engine-frontend-packaging.cjs` and `git diff --check`
  passed at this diagnostic point. The later Stage 6B.65 run rebuilt both
  package pages and reran the full static packaging harness successfully; that
  does not replace the separate real-browser boot check recorded there.


Stage 6B.65 landing slice — compressed Bash app startup (2026-10-05):

- Installed the Bash UI/runtime files under `src/vfs/waste/app` with a named
  `app` component and `vfs-install-app` target. The inventory now requires all
  seven app files. The Bash archive no longer carries a duplicate root worker;
  it starts the worker from the extracted app path.
- The generated page retains the initial loading screen and bootstrap code,
  compiles the embedded zlib-wasm module before fetching the archive, then uses
  that module for gzip decompression. Only after tar extraction does it load
  the app stylesheet and classic scripts in dependency order and call
  `startShell`. The zlib allocator now starts at the module's exported heap
  base and uses its exported memory, which the old fallback did not do.
- `build/html-rt/bash.html` fell from 12,070,656 bytes to 6,217,765 bytes in
  this build. `vfs.py audit`, `vfs-tar-loader.cjs`, frontend packaging, VFS
  packaging, and the Bash worker runtime checks pass. The frontend harness now
  passes against refreshed Bash and test pages, including source/archive byte
  parity.
- A focused real-page check (`node tests/c-engine-offline-browser.cjs
  --page=bash`) ended when Chromium dumped core with SIGTRAP before returning
  page results. `coredumpctl` records Chromium PID 10193, thread 9, at
  2026-10-05 09:42:46 MDT; available memory was 5.5 GiB and the kernel journal
  shows the matching `trap int3`, not an OOM kill. Its command line included
  Omarchy-injected Wayland flags/extensions as well as the harness's headless
  flags. This confirms a Chromium trap, but the stripped stack and failed pipe
  do not identify a page-level cause. Real browser boot therefore remains
  unverified in this slice; the static package and worker checks pass.
- `git diff --check` passes. Stage 6C consolidation, remaining C/CJS audits,
  and Stage 7 dashboard retirement remain open.


Stage 6B.66 landing slice — terminal JavaScript fixture audit (2026-10-05):

- Audited and ran the pure-JavaScript terminal gates. The model harness has 21
  direct checks for VT cell/color/cursor behavior, alternate screens, Rogue
  REP/character-set sequences, application key modes, cursor visibility, ECH,
  parser chunking and resize dimensions. The GLF gate performs 11 selected
  cmap range comparisons plus initialized-asset and geometry-shape checks
  (135,687 vertices and 63,123 triangles).
- `node tests/c-engine-terminal-model.cjs` and
  `node tests/c-engine-terminal-glf.cjs` both pass without starting Chromium.
  Keep these small Node VM tests: they exercise production JavaScript directly
  and do not map to guest Wasm/WAST assertions. Actual DOM/WebGL rendering
  remains a separate browser-only boundary.
- No assertions moved or removed. This is a coverage-accounting slice, not a
  Chromium workaround. Stage 6C command separation, remaining C/CJS audits,
  and Stage 7 dashboard retirement remain open.


Stage 6B.67 landing slice — production-worker pump cancellation audit (2026-10-05):

- Audited `tests/guest-session-pump-cancel.cjs`: four outcome assertions and
  two protocol wait gates around starting a spinning guest, observing its `P`
  marker, sending host cancellation during a 10 ms pump quantum, and receiving
  worker completion. It verifies not-ok, cancelled, under-2-second response,
  and not-cleanly-exited outcomes.
- The paired WAST fixture supplies the guest workload; it cannot send the
  external worker message or verify the browser-worker completion contract.
  Keep this narrow Node VM boundary gate until a native asynchronous host
  driver can exercise the same event/pump behavior. It does not need Chromium.
- The harness passes in 12 ms. Removed unused `packageVfs`/`stageVfs` imports
  and a start-message counter that had no assertions. No behavior checks were
  removed. See the assertion map in
  [test coverage](test-coverage.md#production-worker-pump-cancellation-cjs-gate-stage-6b67).
- `git diff --check` passes. Continue the C/CJS coverage audit; Stage 6C
  command separation and Stage 7 dashboard retirement remain open.


Stage 6B.68 landing slice — installed browser-worker batch parity (2026-10-05):

- Ran `tests/browser-test-suite-runtime.cjs --installed-group=engine-regressions`
  through production `worker.js` in Node worker threads: **12 tests / 1,230
  assertions, all passing**. This group branch has two harness-level
  assertions: successful aggregate exit and nonempty group-consistent results.
- Ran the same installed group through `build/cli-rt/waste-test` and saved both
  machine-readable reports under `build/`. A focused comparison confirms equal
  summaries, ordered test identities/statuses/counts, and all **1,230 ordered
  assertion function/outcome pairs**. Native reports 12 PASS / 0 failures;
  worker reports the same. This gate does not launch Chromium.
- The broader synthetic controller branch in
  `browser-test-suite-runtime.cjs` remains unaudited by this slice. No runtime
  assertions were removed. See [the coverage map](test-coverage.md#installed-browser-worker-batch-cjs-gate-stage-6b68).
- `git diff --check` passes. Continue the C/CJS audit; Stage 6C separation and
  Stage 7 dashboard retirement remain open.


Stage 6B.69 landing slice — synthetic browser-suite controller audit (2026-10-05):

- Completed the previously open synthetic branch audit in
  `tests/browser-test-suite-runtime.cjs`: **98 static assertion sites** cover
  catalogue/policy validation, selection and concurrent ordering, memory
  isolation, guest-command ABI, cancellation/deadlines/recovery, setup
  diagnostics, malformed streams, segment/table operations and limits, Unicode
  identities, and worker teardown.
- `node tests/browser-test-suite-runtime.cjs` passes. The fixture set exercises
  expected negative outcomes (FAIL, TIMEOUT, CANCELLED, XFAIL/XPASS) as well as
  passing runs; these are intentional controller assertions. The test uses
  Node worker threads and does not launch Chromium. WAST-only coverage cannot
  supply its host scheduling, cancellation messages or stuck-worker adapter.
- Removed unused `packageVfs`/`stageVfs` imports. No assertions or runtime
  behavior changed. `node --check` and `git diff --check` pass. The installed
  group parity was separately verified in Stage 6B.68; continue remaining
  C/CJS audits and Stage 6C/7 work.


Stage 6B.70 landing slice — execution-control worker gate audit (2026-10-05):

- Audited `tests/guest-session-control-browser.cjs`: its 17 assertion sites
  exercise Wasm deadline/cancellation stops across six runnable fixtures,
  expected-trap/invalid setup, same-instance recovery, and production worker
  timeout/cancel behavior for both runnable-loop and blocked-I/O sessions.
- The packaged worker branch initially failed because the I/O fixture was
  given only 100 ms to reach its blocking read after three runnable assertions.
  The gate now gives that fixture a 1 s timeout/cancel budget; the loop probe
  keeps its 100 ms budget. This changes only test timing, not runtime behavior.
- `node --check` and
  `node tests/guest-session-control-browser.cjs build/html-rt/waste-wast.wasm
  src/vfs build/html-rt/bash.html` pass, including all 12 native-Wasm interrupts
  and the four timeout/cancel cases using `worker.js` and Wasm extracted from
  the embedded offline archive. No Chromium was launched.
- `python3 tests/guest-session-control-check.py --page build/html-rt/bash.html`
  did not complete: the native sanitizer executable returned 1 during its
  first deadline case, after reporting the expected timeout, because
  LeakSanitizer reported that it cannot operate under this runner's ptrace
  environment. Native ASan/UBSan control-gate status therefore remains
  unverified here; this is separate from the passing Node VM checks.
- See the assertion map in
  [test coverage](test-coverage.md#execution-control-worker-cjs-gate-stage-6b70).
  Continue remaining C/CJS audits; Stage 6C separation and Stage 7 dashboard
  retirement remain open.


Stage 6B.71 landing slice — packaged guest-session worker contract (2026-10-05):

- Audited `tests/guest-session-worker.cjs` (19 static assertion sites) and
  `src/html-rt/src/worker.js`. A fresh Wasm and HTML rebuild reproduced a
  post-session `/tmp` access failure (`-EINVAL`): the worker called kernel
  access APIs after `waste_wast_run_script` had already destroyed its session
  store. Removed those invalid post-completion probes. VFS staging validates
  the installed inventory/files, and C reports installation failures as WAST
  results; the worker still posts its VFS path list for boundary checks.
- Corrected the worker harness to compare `done.exited` with the shared
  scenario's expected value, including the linked-fork fixture's intentional
  `exited: false` case.
- Reinstalled the authored app snapshot and rebuilt a test page from the current
  Wasm and VFS. `guest-session-worker.cjs` passes linked-fork (3/3), terminal
  control (9/9), and handlers (22/23, with the contract's expected handler
  mismatch). `guest-session-check.py --scenario handlers --page ...` confirms
  native, direct Wasm, and packaged-worker agreement. The Bash worker smoke test
  passes 7/7. No Chromium was launched.
- `node --check`, VFS package audit, and whitespace checks pass. See the
  [assertion map](test-coverage.md#packaged-guest-session-worker-cjs-gate-stage-6b71).
  Continue remaining C/CJS audits, Stage 6C consolidation, and Stage 7 dashboard
  retirement.


Stage 6B.72 landing slice — terminal control adapter CJS audit (2026-10-05):

- Audited and ran `tests/guest-session-terminal-browser.cjs` against the rebuilt
  offline page. The direct browser API path rejects nine invalid/unavailable
  control values, processes the shared terminal-control session's resize,
  signal, and input events, then reuses the Wasm instance for a fresh-store
  recovery session.
- The packaged production-worker path queues controls before startup, checks
  invalid signal/resize/clock message rejection and bounded-queue overflow,
  rejects invalid events again while blocked in SELECT, and then wakes/resumes
  through the remaining session events. It passes with 57 control-error
  rejections and all nine guest assertions.
- `node --check` and the CJS gate pass without Chromium. Removed its unused
  `treeVfs` import. The native `guest-session-terminal-check.py` sanitizer and
  malformed control-fd matrix remain separate C gates; they were not rerun in
  this slice.
- See the [assertion map](test-coverage.md#terminal-control-browser-cjs-gate-stage-6b72).
  Continue remaining C/CJS audits, Stage 6C consolidation, and Stage 7 dashboard
  retirement.


Stage 6B.73 landing slice — legacy corpus runner boundary audit (2026-10-05):

- `tests/c-engine-native-runtime.cjs` defaulted to `build/cli-rt/waste-wast`,
  an obsolete executable no longer produced by `src/cli-rt/Makefile`; the
  current `wast-native` target produces `waste-cli`. Updated the default and
  server-mode comment to match the current native runner.
- Ran the `engine-regressions` group through the old `tests-worker.js` browser
  corpus path and native `waste-cli`, excluding only
  `continuation-waits.wast`: **11 tests / 1,207 results**, all passing. Ordered
  identities, statuses, and result counts match; every browser setup and stream
  completion gate passes. The initial native mismatches came from the obsolete
  `waste-wast` binary, not current C/browser semantic differences.
- The excluded `continuation-waits.wast` reaches setup completion (3/3) in the
  legacy worker but yields at guest `select`; that worker does not resume host
  waits and reports zero results with `completed: false`. Keep this visible as
  a legacy dashboard limitation. Stage 6B.68 separately passes the complete
  12-test group through the production suite worker, which handles scheduled
  waits. No Chromium was launched.
- JavaScript syntax and report-parity checks pass. See the
  [coverage map](test-coverage.md#legacy-corpus-runner-cjs-audit-stage-6b73).
  Continue C/CJS audits, Stage 6C consolidation, and Stage 7 dashboard
  retirement.


Stage 6B.74 landing slice — legacy dashboard SELECT continuation (2026-10-05):

- The Stage 6B.73 wait-fixture gap was caused by `tests-worker.js` discarding
  the status from `waste_wast_run_script`. It now recognizes a yielded guest
  SELECT, waits briefly for the kernel deadline, and resumes through
  `waste_wast_resume`. Other host wait kinds fail explicitly instead of
  producing an incomplete, empty result set.
- The legacy browser runtime now passes all **12 engine regressions / 1,230
  assertion results**, including the continuation-waits fixture. The current
  `waste-cli` native runner passes the same group. A report comparison confirms
  matching summaries, ordered test identities/statuses/counts, successful
  browser setup/completion, and all 1,230 results passing.
- Rebuilt the self-contained dashboard package from the updated worker source,
  current Wasm and generated payload. Running the 12-test group from that
  embedded archive passes with the same report checks; this Node worker-thread
  verification did not launch Chromium. `node --check` and whitespace checks
  pass.
- See the [coverage map](test-coverage.md#legacy-dashboard-select-continuation-stage-6b74).
  Continue remaining C/CJS audits, Stage 6C consolidation, and Stage 7 dashboard
  retirement.


Stage 6B.75 landing slice — packaged Bash Readline/VFS completion (2026-10-05):

- Audited `tests/c-engine-bash-browser-runtime.cjs`'s
  `--readline-completion --full-package` path. It reads the VFS archive and
  manifest embedded in `build/html-rt/bash-next.html`, mounts the package, and
  verifies the worker announces the mounted paths.
- The harness types `/bin/pw` character by character, sends Tab, accepts the
  `/bin/pwd` completion, submits it, observes `/root`, and exits. Its ordered
  gates require prompt/startup, VFS setup, each completion/input transition,
  the command result, explicit exit, and successful worker completion; the
  aggregate reports **7/7**.
- The packaged-page worker VM run passes. `node --check` and the installed VFS
  audit pass; no Chromium was launched. This slice covers archive-backed VFS
  startup and interactive completion; full directory-listing coverage remains
  in the dedicated `--coreutils-ls --full-package` path.
- See the [coverage map](test-coverage.md#packaged-bash-readline-vfs-completion-stage-6b75).
  Continue remaining C/CJS audits, Stage 6C consolidation, and Stage 7 dashboard
  retirement.


Stage 6B.76 landing slice — full-package Bash directory listing (2026-10-05):

- Running `tests/c-engine-bash-browser-runtime.cjs --coreutils-ls
  --full-package` exposed stale `/` and `/bin` expectations and a listing
  parser that mixed echoed commands with guest output. The `/bin` check now
  derives the full-package names from the embedded manifest; the staged mode
  has its current reduced tree contract. Both listing parsers select output
  after the marker, independent of the echoed command and terminal control
  sequences.
- The full-package path checks exact root and command names, then empty and
  hidden directories, `.`/`..`, symlink details, TTY columns versus redirected
  lines, multiple directories, missing-path errno/status, and shell recovery.
  Full-package and staged-package variants both pass **7/7** through the Node
  VM harness with no Chromium.
- `node --check`, installed VFS audit, and whitespace checks pass. See the
  [coverage map](test-coverage.md#full-package-bash-directory-listing-stage-6b76).
  Continue remaining C/CJS audits, Stage 6C consolidation, and Stage 7 dashboard
  retirement.


Stage 6B.77 landing slice — Bash coreutils matrix staging parity (2026-10-05):

- Audited the 13-command `--coreutils-matrix` contract in both staged and
  embedded-full-package modes. The full-package mode passed first; the staged
  mode revealed it was not installing the matrix's coreutils snapshots, so
  commands returned 126. The harness now stages true, false, pwd, echo,
  printf, basename, dirname, cat, wc, ls, and date, plus the WAT/WAST inputs
  and text fixtures.
- Both modes now pass **7/7** and verify each command's expected exit status,
  representative output (including UTC year and wc counts), the VFS path
  announcement, continued command processing, and explicit worker exit.
- `node --check`, installed VFS audit, and whitespace checks pass. The checks
  use the Node VM harness; no Chromium was launched. See the
  [coverage map](test-coverage.md#bash-coreutils-matrix-staging-parity-stage-6b77).
  Continue remaining C/CJS audits, Stage 6C consolidation, and Stage 7 dashboard
  retirement.


Stage 6B.78 landing slice — Bash pipeline and redirection parity (2026-10-05):

- Audited the staged and full-package `--pipeline-probe` paths. Both pass **7/7**.
  The probe pipes `/bin/ls /bin` into `wc -l`, redirects the same listing to a
  file, reads it back with `cat`, then verifies another command runs before
  explicit exit.
- Tightened the harness to compare the pipeline's line count with the expected
  `/bin` entry count (six staged commands or the embedded manifest's full
  count) and to compare redirected file contents with the exact sorted command
  list. Previously it accepted any number after the pipe marker and only
  checked for the presence of the cat delimiters.
- JavaScript syntax and whitespace checks pass. The archive-backed Node VM
  runs do not launch Chromium. See the [coverage map](test-coverage.md#bash-pipeline-and-redirection-parity-stage-6b78).
  Continue remaining C/CJS audits, Stage 6C consolidation, and Stage 7 dashboard
  retirement.


Stage 6B.79 landing slice — Bash here-document VFS coverage (2026-10-05):

- Audited the three here-document paths. The `read` builtin variant passes
  **7/7** against both the staged tree and embedded full package. It verifies
  the here-document value reaches `read`, is printed intact, and Bash exits.
- The file-writing `/bin/cat` and stdout `/bin/cat` variants each pass **7/7**
  against the embedded full package. The file case checks content readback,
  mode `0644`, and absence of the epoch timestamp; the stdout case checks the
  here-document body reaches stdout without a temp-file error.
- At this stage, staged mini-VFS parity remained open for both cat-backed
  variants. Stage 6B.81 closed stdout parity; Stage 6B.82 closed file-write,
  readback, and mode parity by including both `cat` and `ls` in that staged
  scenario. No Chromium was launched.
- See the [coverage map](test-coverage.md#bash-here-document-vfs-coverage-stage-6b79).
  Continue remaining C/CJS audits, Stage 6C consolidation, and Stage 7 dashboard
  retirement.


Stage 6B.80 landing slice — repeated WAT/WAST command execution (2026-10-05):

- Audited the staged and full-package `--wast-repeat-probe` paths. Both pass
  **7/7**. The session invokes the `wat` wrapper three times, then invokes
  `wast` by command name, by direct executable path, and through a shebang
  script; each result is followed by another command and a final exit.
- The previous gate required only the three WAST statuses. It now requires all
  six zero-status markers from both wrappers and verifies `/bin/wat`,
  `/bin/wast`, and all three WAST fixture paths were announced by VFS setup.
  This makes the observed repeat/recovery sequence and package inputs part of
  the pass condition.
- JavaScript syntax and whitespace checks pass. Both packaged-page runs use
  the Node VM and do not launch Chromium. See the
  [coverage map](test-coverage.md#repeated-watwast-command-execution-stage-6b80).
  Continue remaining C/CJS audits, Stage 6C consolidation, and Stage 7 dashboard
  retirement.


Stage 6B.81 landing slice — staged Bash stdout here-document (2026-10-05):

- The staged mini-VFS includes the installed `cat` executable and verifies its
  VFS announcement for `--heredoc-stdout`.
- Staged and full-package stdout here-doc runs pass **7/7**. The full-package
  file-writing check also passes **7/7**; builtin `read` here-doc checks pass
  in both VFS modes. Stage 6B.82 subsequently closed the staged
  file-redirection case.
- `node --check`, installed VFS audit, and whitespace checks pass. No Chromium
  was launched. See the [coverage map](test-coverage.md#staged-bash-stdout-here-document-stage-6b81).
  Continue remaining C/CJS audits, Stage 6C consolidation, and Stage 7 dashboard
  retirement.


Stage 6B.82 landing slice — staged Bash file here-document dependencies (2026-10-05):

- Isolated the staged file-writing here-doc sequence: writing `hello.txt` and
  reading it back with `cat` both worked. The final `ls -l hello.txt` mode
  assertion was the missing dependency; the mini-VFS had no `ls`, and the
  combined scenario stalled at that boundary.
- The staged scenario now includes `cat` and `ls`, checks their announced VFS
  paths, and exercises file write, content readback, and mode `0644`. All six
  here-doc combinations—file, stdout, and builtin `read`, each staged and
  full-package—pass **7/7**.
- `node --check`, installed VFS audit, and whitespace checks pass. No Chromium
  was launched. See the [coverage map](test-coverage.md#staged-bash-file-here-document-dependencies-stage-6b82).
  Continue remaining C/CJS audits, Stage 6C consolidation, and Stage 7 dashboard
  retirement.


Stage 6B.83 landing slice — browser Bash external executable probe (2026-10-05):

- The diagnostic run localized the failure: the new image entered `_start`,
  standard descriptors were valid, but `env.fcntl` returned success for
  `F_SETFD(FD_CLOEXEC)` while a subsequent get still read zero. The browser
  host resolver had allowed a linked module provider to win over the stateful
  kernel adapter for this import. `env.fcntl` now prefers that engine-owned
  adapter, just as Bash's `env.lseek` and `env.__fpurge` already prefer their
  ABI-specific host implementations.
- The probe reports entry and descriptor state, and the controller prints an
  immediate status marker, verifies missing-command status 127, performs a
  second launch, and exits after collecting its status. This preserves useful
  output on future failures rather than hanging for the success marker.
- Staged and freshly generated embedded-full-package browser probe runs pass **7/7**.
  They verify descriptor flags `0,0,0,0,1,0,0`, startup data, three initial
  argv entries, environment, PID/cwd, missing-command recovery, and a second
  successful launch. Explicit exit-status-7 runs also pass **7/7** in both
  modes. Installed the rebuilt probe into `src/vfs`, audited all 507 VFS nodes,
  and generated a fresh `build/html-rt/bash.html`. `node --check` and
  `git diff --check` pass; no Chromium was launched.
- See the [coverage map](test-coverage.md#bash-executable-probe-stage-6b83).
  Continue with the remaining C/CJS audits,
  Stage 6C consolidation, and Stage 7 dashboard retirement.


Stage 6B.84 landing slice — native fcntl parity for external images (2026-10-05):

- The initial native failure came from running a stale `waste-session` binary:
  it did not contain the current fcntl host-preference change. The resolver
  itself returned `prefer_over_module=1`; after rebuilding the runner from the
  current engine sources, the imported callback persisted CLOEXEC in the active
  process kernel and reported `0,0,0,0,1,0,0`.
- The rebuilt native session passed the full probe sequence: startup/argv,
  descriptor flags, missing-command status 127, a second executable launch,
  and clean exit (**7/7**). Browser staged and refreshed full-package runs also
  pass **7/7**, and their explicit status-7 scenarios pass **7/7**. Thus all
  three VFS/runtime paths agree for the executable probe.
- Validation: rebuilt `build/cli-rt/waste-session`, ran the native interactive
  session check and both browser VM modes, plus `node --check`, the installed
  VFS audit, and `git diff --check`. No Chromium was launched. Continue the
  remaining C/CJS audits, Stage 6C consolidation, and Stage 7 dashboard
  retirement.


Stage 6B.85 landing slice — assert external-image startup fields (2026-10-05):

- Tightened the Bash browser harness so executable-probe success now requires
  the startup ABI values, not just the probe's self-reported success marker:
  the first invocation must receive argc 3 and argv[0] `/bin/waste-probe`, and
  the repeated invocation argc 2 with the same argv[0]. Both must provide a
  nonempty environment, positive PID, and absolute cwd.
- Staged and refreshed embedded-full-package browser VM runs pass **7/7** with
  the stronger assertions. The two invocations report environment count 8,
  positive PIDs, and absolute cwd values; descriptor flags and missing-command
  recovery remain covered. They currently report `/bin/waste-probe` as cwd,
  so this slice checks only its startup-block shape; inherited-cwd semantics
  remain an explicit follow-up. `node --check` passes. No Chromium was launched.
- See the [coverage map](test-coverage.md#bash-executable-probe-stage-6b83).
  Continue assertion-level C/CJS audit work, Stage 6C consolidation, and
  Stage 7 dashboard retirement.


Stage 6B.86 landing slice — external probe and session/worker host audit (2026-10-05):

- Audited three files from the source inventory: `c-engine-waste-probe.c`,
  `guest-session-browser.cjs`, and `c-engine-worker-host.cjs`. The first is a
  compiled guest ABI probe, the second is the 36-site direct-Wasm session
  adapter, and the third is worker-thread test infrastructure with no direct
  assertion sites. Their coverage complements, and does not replace, the
  retained private exec/process C gates.
- The full-package Bash executable probe passes **7/7**; direct-Wasm `io`
  session parity passes **7/7**, with linked-provider fork isolation **3/3**;
  the worker-host smoke passes `engine-regressions/i32-smoke.wast` **1/1**.
  No assertions were removed. No Chromium was launched.
- The audit identifies one infrastructure-only gap: no isolated negative test
  deliberately triggers the worker host's missing-handler and thrown-handler
  error forwarding. Timeout/replacement behavior remains owned by its parent
  runtime harness. It also preserves the startup-cwd follow-up: the probe
  reports `/bin/waste-probe`; only absolute-path shape is currently asserted.
- Updated the source-file inventory from **35/56** named to **38/56** named;
  **18 files** remain unnamed in the coverage map pending explicit dispositions.
  Continue the remaining audit, Stage 6B acceptance, Stage 6C, and Stage 7.


Stage 6B.87 landing slice — binary reader, import decoder and validator C gates (2026-10-05):

- Audited three native C probes: `wasm-binary-primitives.c` (**80 explicit
  CHECK sites**), `wasm-import-decode.c` (**49**), and `wasm-validation.c`
  (**11**). The first covers LEB encoding/decoding, canonical/malformed values,
  reader rollback/bounds and writer ownership. The second covers all five
  import descriptor kinds, malformed sections and copied-source ownership.
  The third checks validation status/location and resolved branch/call/local
  metadata, including one-time validation.
- These white-box contracts complement official WAST/oracle behavior and
  remain native gates because WAST cannot inspect reader cursors, decoded
  ownership records or validator metadata. Warnings-as-errors ASan/UBSan gates
  pass for all three; no assertions moved or removed.
- The coverage map now names **41/56** source files; **15 remain unnamed**
  (7 C, 8 CJS). With the current three-files-per-slice pace, estimate **10–14
  landing slices remain**: about five audit tranches, one to three acceptance
  slices, two to three consolidation slices, and two to three retirement
  slices. This is a planning estimate; dispositions may close helper-only files
  faster, while newly discovered defects may add work.


Stage 6B.88 landing slice — corpus scheduling and offline package helper audit (2026-10-05):

- Audited `corpus-schedule.cjs`, `corpus-schedule-check.cjs`, and
  `offline-html-package.cjs`. The shared scheduler has no local assertions;
  its 27-site self-check covers malformed timing inputs, stable longest-first
  ordering, filtering, result order, timeout reporting, worker replacement and
  both runner integrations. The offline package reader also has no local
  assertion sites; it is retained host tooling for inspecting embedded gzip/tar
  bytes, separate from browser-side inflate/DOM tests.
- The scheduler integration check passes. VFS packaging parity and malformed
  inventory/install guards pass, and the packaged Bash executable probe passes
  **7/7** through the offline package reader. Syntax and whitespace checks pass;
  no Chromium was launched. The scheduler test required execution approval to
  spawn nested Node processes after the sandbox returned `EPERM`.
- Updated the source inventory to **44/56** files named (21 C, 23 CJS), with
  **12 still unnamed** (7 C, 5 CJS). The current estimate is **9–13 remaining
  slices**: four tranches for the uncited-file queue, one to three Stage 6B
  acceptance slices, two to three Stage 6C slices, and two to three Stage 7
  slices. This assumes the 44 existing coverage entries substantiate their
  citations; any audit-boundary gaps or newly exposed engine defects add work.
- Dedicated malformed embedded-archive fixtures are still absent, and should
  be considered if the offline package reader is expanded beyond test tooling.


Stage 6B.89 landing slice — shared-library and coreutils CRT fixture audit (2026-10-05):

- Audited three remaining C sources: `c-engine-shared-lib-dylink.c`,
  `c-engine-shared-lib-trivial.c`, and `coreutils-sysroot-hello.c`. The legacy
  dylink probe contains a copied parser rather than calling production code;
  it passes 6/6 on its checked-in module, while a source-built PIC module
  produces metadata mismatch and passes only 4/6. The trivial library has no
  maintained target or consumer beyond that probe.
- The coreutils hello CRT fixture initially failed to link because the CRT
  expects waste-libc stdio accessors. Added fixture-local no-op standard-stream
  providers; the program uses `env.write`, and the production utility link still
  supplies real providers. `coreutils-crt-fixture` now passes sysroot
  compilation, linker audit and WAT generation. The target is build/audit-only,
  so executing its startup and exit path remains open.
- Updated coverage accounting to **47/56** files named (24 C, 23 CJS); **9
  remain unnamed** (4 C, 5 CJS). Estimate **9–13 slices remain**: three
  three-file audit tranches, two to four Stage 6B acceptance slices, two to
  three Stage 6C slices, and two to three Stage 7 slices. This includes one
  acceptance tranche for replacing the stale dylink probe if shared-library
  coverage remains in scope; consistency gaps in earlier audits can increase
  the estimate.

Stage 6B.90 landing slice — guest SDK, SELECT ABI and source-loader audit (2026-10-05):

- Audited three files: the guest SDK ABI client, native SELECT ABI helper
  probe, and source-loader parser probe. The SDK check must remain a compiled
  Wasm C test because it asserts wasm32 public type/layout and varargs ABI;
  SELECT helper byte layout and parser-view internals also need direct native
  assertions and are not replaced by WAST semantics.
- The ASan/UBSan SELECT gate passed 1,097 checks; the source-loader gate passed
  10 checks. `guest-sdk-check` failed before its expected negative-case result:
  `inspect_sdk` returned `browser SDK binding review is stale`. Record that as
  an open gate defect, not a pass.
- Coverage map advances to **50/56 files named** (27 C, 23 CJS), with **6
  unnamed** (1 C, 5 CJS). The estimate is **8–12 remaining slices**: two
  three-file audit tranches, two to four Stage 6B acceptance slices, two to
  three Stage 6C slices, and two to three Stage 7 slices. This assumes no
  additional regressions emerge from the stale SDK review or existing-file
  consistency pass.

Stage 6B.91 landing slice — parser reentrancy and legacy Bash driver audit (2026-10-05):

- Audited three files: one native parser/stream/process concurrency gate and
  two OCaml-runtime Bash smoke drivers. The parser sanitizer gate passed; it
  runs fourteen focused probes in each of four threads plus eighty additional
  valid/invalid parse pairs total. The harness also directly checks handler
  cursor ownership, yield/resume/completion, VFS snapshots and child wait
  results, which do not reduce to WAST assertions.
- The Bash CJS drivers each have three outer smoke conditions but run the
  legacy OCaml Bash application through Node. They remain legacy coverage and
  do not establish browser C-runtime parity; retire them only after matching
  C-runtime interactive and command behavior is accepted. They were audited,
  not executed, because the legacy runtime build was not part of this slice.
- Coverage map advances to **53/56 files named** (28 C, 25 CJS), with **3
  unnamed** (0 C, 3 CJS). The estimate is **7–11 remaining slices**: one
  three-file audit tranche, two to four Stage 6B acceptance slices, two to
  three Stage 6C slices, and two to three Stage 7 slices. This retains the
  prior assumptions about Stage 6B gaps and the 53 existing audit boundaries.

Stage 6B.92 landing slice — legacy DIY drivers and native allocator audit (2026-10-05):

- Audited the final three unnamed sources: the legacy DIY control and kernel
  CJS launchers, and the native Wasm allocator stress harness. The launchers
  add only process-completion checks around embedded WAST/control behavior;
  their guest expectations are already mapped in the DIY ledger. Native C
  coverage supersedes the supported kernel subset, while stop/continue,
  concurrent sleeping jobs, and pause/resume of a busy loop remain legacy
  profile gaps. No OCaml kernel work is planned.
- `diy-posix-control` passed seven checks, including timeout and mismatch
  rejection; `posix-kernel` passed 261 checks and the shared-memory regression
  passed. The allocator's Node WebAssembly stress passed 5,000 allocation
  cycles, import isolation and guard rejection (16 pages, 12 one-page grows).
- The inventory now names **56/56 source files** (28 C and 28 CJS); the
  uncited-file queue is closed. Estimate **6–10 remaining slices**, covering
  two to four Stage 6B acceptance slices, two to three Stage 6C slices, and
  two to three Stage 7 slices. This assumes the 56 existing audit boundaries
  withstand the consistency pass and acceptance reveals no new engine-wide
  defect.

Stage 6B.93 acceptance slice — corpus/oracle, SDK and browser gate reconciliation (2026-10-05):

- Re-ran the installed native corpus after SDK refresh: **289 PASS / 2 XFAIL /
  5 SKIP**, zero unexpected failures. The strict language-oracle comparison
  passed **261/261 supported official Wasm/WAT/WAST inputs**, with four
  explicitly excluded inputs and 31 repository fixtures outside OCaml scope.
- The browser POSIX kernel fixture is backend-specific: the native runner
  correctly skips `diy-posix-test/posix-kernel.wast` because it requires the
  browser compatibility backend; the C-engine worker run executes it and
  passes. The worker-based full corpus passed **290 runnable inputs** (2
  expected XFAIL, zero failures). Updated the offline full-suite assertion to
  expect **290 PASS / 2 XFAIL / 4 SKIP** for the browser backend rather than
  copying the native backend's one-extra-SKIP summary. `node --check` passes.
- Refreshed the reviewed hashes for the shared POSIX adapter extraction in
  `sdk-api-policy.json`, installed the guest SDK snapshot, and reran
  `guest-sdk-check`: 56 headers, six include orders, Wasm ABI/provider negative
  cases, and strict provider signatures all pass. Generated the self-contained
  test and Bash pages; the Bash build passed terminal, continuation, Readline,
  here-document, coreutils, shared-library and package timestamp checks. The
  browser batch controller contract test also passes.
- The full `file://` Chromium run remains unverified. Both the normal launcher
  and direct `/usr/lib/chromium/chromium` exit before the DevTools handshake
  with `ECONNRESET`; systemd records SIGTRAP (`SI_KERNEL`) in Chromium thread 9
  at the same unsymbolized binary offset, including without Omarchy's wrapper
  flags/extensions. Available memory was 5 GiB, so this was not an OOM kill.
  The existing core is retained by systemd; no copy was extracted. The exact
  Chromium trigger remains unknown, but the crash precedes page navigation and
  does not implicate the WASTE worker or page.
- Tried Firefox 155.0.1 as the alternate production browser. The local
  harness cannot drive Firefox directly because it uses Chromium's DevTools
  pipe protocol and no Firefox automation bridge is installed. More
  importantly, Firefox itself also fails before page navigation here: both
  `firefox --help` and a headless screenshot of `build/html-rt/bash.html`
  terminated with SIGSEGV (the latter exit 139); systemd-coredump recorded the
  crash in `libxul.so`. These local command-line launches do not establish a
  page or WASTE runtime fault.
- A user-run `/bin/waste-test` from `bash.html` has since exercised the
  production Bash page and mounted browser workers successfully. Its complete
  report contains 296 entries: **285 PASS / 4 TIMEOUT / 2 XFAIL / 5 SKIP**,
  exit 1. The four failures are bulk-memory `memory_copy.wast`, memory64
  `memory_copy64.wast`, and the f32x4/f64x2 `pmin_pmax` SIMD tests. Each hit
  its configured 10-second group deadline with no assertion report, so this
  run indicates a timeout/performance limit rather than an observed assertion
  mismatch. The user also confirmed `echo $?` returned 1, matching the report.
  The downloaded stdout and results files are byte-identical because `--json`
  sends the report to stdout while `--results` saves the same JSON in guest VFS.
- A user reran exactly those four files with one worker and a 60-second
  deadline. The captured output reports **4 PASS / 0 FAIL / 0 XFAIL / 0 XPASS
  / 0 SKIP**, confirming the previous four results were deadline timeouts and
  not reproducible assertion failures under the longer budget. The output
  artifact is present; the expected JSON report was not in the copied files,
  so assertion totals for this focused rerun are unavailable.
- The user then ran the complete installed suite from `bash.html` at a
  60-second timeout. The saved report in
  `build/html-rt/stage6b-browser-full-60s.json` contains **296 entries: 289
  PASS / 0 FAIL / 2 XFAIL / 0 XPASS / 5 SKIP**, with `exitCode: 0`. This
  matches the native corpus summary. Four skips are unsupported legacy
  exception syntax; `diy-posix-test/posix-kernel.wast` is explicitly skipped
  by the guest launcher because that fixture requires the browser
  compatibility backend. The Bash page and its mounted test workers therefore
  pass the full guest-launcher corpus in a real browser; the longer deadline
  accommodates all four previously timed-out files.
- The downloaded `waste-browser-test-results.json` from Bash-page Diagnostics
  again reports **289 PASS / 0 FAIL / 2 XFAIL / 0 XPASS / 5 SKIP**, exit 0;
  `diy-posix-test/posix-kernel.wast` remains skipped with the explicit reason
  that it requires the browser compatibility backend. This confirms that
  Diagnostics → Installed tests invokes the guest launcher but lacked the
  compatibility path. Stage 6B.96 ports that feature into the Bash page, so
  `test.html` is no longer needed for the POSIX-kernel compatibility checks.
  Legacy OCaml-kernel parity is not a C acceptance gate under the repository's
  current architecture. Updated estimate: **5–9 remaining landing slices**—
  one to three Stage 6B follow-ups, two to three Stage 6C slices, and two to
  three Stage 7 slices.

Stage 6B.96 landing slice — run browser-native compatibility tests from Bash
Installed tests (2026-10-05):

- Ported the manifest-driven browser-native compatibility path into the Bash
  page's Installed tests controller and worker. It reads each compatible
  execution spec from the mounted `/tests/manifest.json`, clears the runner's
  compatibility skip only when a valid spec is packaged, then executes the
  spec in an isolated worker with the normal timeout/cancellation lifecycle.
  This moves the required POSIX-kernel feature to `bash.html`; `test.html` is
  not needed for this acceptance flow.
- The mounted `diy-posix-test` group passes through the Bash controller: **5
  tests, 55 assertions**. The compatibility fixture itself reports **7/7
  checks and 1/1 module setup**, with no skip. The Bash page build, browser VM
  smoke, frontend packaging guard, and JavaScript syntax checks pass. The full
  mounted-corpus VM run reports **290 PASS / 0 FAIL / 2 XFAIL / 0 XPASS / 4
  SKIP** (all runnable assertions pass).
- This host still cannot validate page boot in Chromium or Firefox because
  both local browser launches fail before navigation. Acceptance in the user's
  actual browser was therefore used for the final production-page check. The
  downloaded report `build/html-rt/stage6b-browser-bash-compat.json` contains
  296 records: **290 PASS / 0 FAIL / 2 XFAIL / 0 XPASS / 4 SKIP**, with
  `exitCode: 0`. The `diy-posix-test/posix-kernel.wast` record ran in
  `browser-native` mode and passed all seven checks, including stop/continue
  and foreground-job checks; module setup passed 1/1. This closes the Bash-page
  browser acceptance check for Stage 6B.96. The four remaining skips are the
  documented unsupported legacy exception cases.
- The inventory remains 56/56 reviewed test sources. With this acceptance gate
  closed, the estimate is **4–7 remaining landing slices**: zero to one Stage
  6B consistency follow-up, two to three Stage 6C slices, and two to three
  Stage 7 slices. This assumes no new engine-wide issue appears in the
  remaining audit or dashboard-retirement work.

Stage 6B.97 landing slice — correct executable startup cwd and close worker-host error paths (2026-10-05):

- Fixed the startup block's cwd pointer. Offset 20 was captured before argv and
  env strings were written, so it pointed to the first argument string. The
  pointer is now stored after both vectors' strings; cwd content remains the
  process's inherited kernel cwd. The external Wasm probe now asserts `/root`
  in both initial and repeated Bash execs, while the native exec-matrix reads
  offset 20 and verifies that it references `/` rather than `argv[0]`.
- Added isolated worker-host negative checks for a missing `onmessage` handler
  and a handler that throws, verifying both become explicit `type: error`
  messages. This closes the worker-host forwarding gap noted in Stage 6B.86.
- Validation passed: native ASan/UBSan exec matrix **60/60**, exec lifecycle
  **22/22**, process continuation **12/12**; Bash page rebuild and staged plus
  full-package exec probes **7/7** each; focused browser worker suite across
  engine-regressions passed all 12 files. JavaScript syntax checks pass. The
  probe reports `WASTE_PROBE_CWD=/root` for both invocations.
- The old 6B.85 and 6B.86 notes describe the state before this fix. The known
  cwd and worker-host negative-path follow-ups are closed; only the 56-file
  audit-boundary consistency pass remains in the Stage 6B checklist. The
  overall estimate is **4–7 landing slices**, including Stage 6C and Stage 7.

Stage 6B.98 landing slice — close the uncited-source queue (2026-10-05):

- Cross-checked the 56 authored `tests/**/*.c` and `tests/**/*.cjs` files
  against exact `tests/...` paths in `docs/test-coverage.md`. Four omissions
  were found: the caller-instance and evaluator-continuation C gates, plus the
  frontend-packaging and offline-browser CJS harnesses. Added explicit rows
  recording their assertion counts, guest complements, and retained host or
  private-engine boundaries. The inventory now has **56/56 exact citations**.
- Rebuilt both generated HTML packages after the shared-engine update so their
  embedded Wasm bytes match. The frontend packaging guard passes. The
  caller-instance ASan/UBSan gate passes **32/32**; its installed
  `caller-memory.wast` passes **12/12**. The continuation ASan/UBSan gate passes
  **43/43**; installed `continuation-waits.wast` passes **23/23**. The focused
  browser worker regression also passed the 12-file engine-regression group.
- The `c-engine-offline-browser.cjs` gate remains unrun on this host because
  Chromium and Firefox exit before navigation. Its WebGL, offline-request, and
  real DOM/session assertions remain explicitly retained; the user's Stage
  6B.96 browser batch is complementary evidence, not a substitute for them.
- Stage 6B's uncited-source queue is closed. The final remaining Stage 6B task
  is checking the other 52 file citations against their referenced assertion
  ledgers and gates. Overall estimate remains **4–7 landing slices**: zero to
  one Stage 6B consistency slice, two to three Stage 6C slices, and two to
  three Stage 7 slices.

Stage 6B.99 closure — audit-ledger consistency and acceptance (2026-10-05):

- Rechecked all 56 exact source citations against the coverage document. The 52
  previously cited files resolve to assertion tables or dedicated subsections
  containing their check counts, guest complements, and retained host/private
  boundaries. The four sources added in Stage 6B.98 now have the same explicit
  treatment. No unsupported citation or undisposed test source was found.
- Stage 6B's native/browser corpus and reporting gates, supported Wasm/WAT/WAST
  oracle comparisons, retained C sanitizer gates, production Bash-page browser
  batch, and complete 56-file coverage inventory are now recorded and checked.
  Mark Stage 6B complete; no OCaml kernel parity or additional OCaml kernel
  development is part of its acceptance.
- The remaining plan is Stage 6C (2–3 landing slices) and Stage 7 (2–3), for an
  estimate of **4–6 remaining landing slices**. The focused real-browser
  WebGL/network/session gate remains available for a host where Chromium or
  Firefox can launch; the user's Stage 6B.96 Bash-page full-corpus run supplies
  production-page execution evidence but does not replace those platform
  assertions.

Stage 6C.2 landing slice — correct fcntl errors and refresh the Bash test page (2026-10-05; superseded redirection diagnosis below):

- Corrected the descriptor-import contract: failed `env.fcntl` calls return
  POSIX `-1` and set the guest `errno`. `descriptor-flags.wast` now checks
  invalid commands/descriptors and descriptor-table exhaustion using that
  contract. A forced native rebuild passes all **102/102** assertions; the
  installed snapshot was refreshed and `make -C src/html-rt vfs-tests-check`
  passes for all **296** records and **507** VFS nodes.
- Rebuilt both generated pages so their embedded engine bytes match. The Bash
  page build and `./start.sh --html-check` pass; the frontend packaging guard
  passes for Bash and the retained dashboard. `tests/c-engine-vfs-browser.cjs`
  confirms all **296** mounted test files open/read through EOF with exact
  lengths and endpoint bytes, with native-identical inventory and malformed-
  inventory checks. The full native corpus reports **289 PASS / 0 FAIL / 2
  XFAIL / 0 XPASS / 5 SKIP**.
- The first redirection probe had been run against a stale native binary. With
  rebuilt artifacts, Bash now receives the correct `EMFILE` (errno 24) from
  `F_DUPFD`, but cannot save a descriptor at or above `SHELL_FD_BASE` (10) in
  the interactive process. This remains an open shell/kernel integration
  issue; do not mark shell redirection acceptance complete based on the VFS
  byte checks. `tests/test-corpus-bash.py --limit 1` remains the bounded
  reproduction.
- Focused Node worker/model checks pass in **about 39 s**. The refreshed Bash
  page build took **about 86 s**; refreshing `test.html` took **about 63 s**.
  Local Chromium and Firefox still fail before page navigation, so production
  `file://` acceptance and browser corpus execution need a manual browser run.
  Open `build/html-rt/bash.html` directly in Firefox, confirm the Bash prompt,
  and run `echo STAGE6C_BROWSER_OK`, which should print that marker. Then use
  **Diagnostics → Installed tests** to run the mounted corpus and download its
  JSON report to `build/html-rt/stage6c-browser-results.json`. Expected batch result: **290 PASS / 0 FAIL / 2
  XFAIL / 0 XPASS / 4 SKIP**, exit 0. At the Bash prompt, run
  `: < /usr/share/waste/launch.wast; echo "__REDIR_STATUS__:$?"`; currently
  this is expected to reproduce `cannot duplicate fd`, errno 24 and status 1,
  so that failure still needs diagnosis before Stage 6C closes.
- Stage 6C remains open for descriptor-pressure diagnosis, semantic
  consolidation, and actual offline-browser/result-contract acceptance. Its
  current estimate is **1–2 more landing slices**; Stage 7 remains **2–3**.
  The overall estimate is **3–5 landing slices**, pending browser evidence and
  closure of the redirection issue.

Stage 6C.3 landing slice — fix Bash fcntl varargs and close mounted-file redirection (2026-10-05):

- The apparent descriptor exhaustion was an ABI mismatch, not a full table.
  Bash imports `fcntl` as a variadic function and passes a guest stack-area
  pointer for its third argument. The fixed-width adapter interpreted that
  pointer (for example, `232896`) as the `F_DUPFD` minimum, producing `EMFILE`.
- Added the versioned `waste_kernel.fcntl_varargs_v1` guest ABI. Its adapter
  reads the integer command argument from caller memory, then delegates to the
  existing fixed-width kernel operation. Fixed-arity `env.fcntl` callers remain
  supported; descriptor WAST probes now name `waste_kernel.fcntl_v1` explicitly.
- Added `: < /usr/share/waste/launch.wast; echo __REDIR_STATUS__:$?` to
  `./start.sh --html-check`. The focused reproduction now prints status 0 and
  passes 7/7. `python3 tests/test-corpus-bash.py build/html-rt/bash.html`
  opens all **296/296** installed WAST paths through guest redirection.
  `make -C src/html-rt vfs-tests-check` passes for all **296** test records
  and **507** VFS nodes. External executable descriptor flags still pass
  with the fixed-width API.
- Rebuilt the retained `test.html` after its embedded Wasm became stale;
  `node tests/c-engine-frontend-packaging.cjs` passes both page and package
  guards. `./start.sh --html-check` passes with the new redirection regression.
  `node tests/c-engine-vfs-browser.cjs build/html-rt/bash.html` passes its
  mounted-only SDK checks, reads all 296 files through EOF with exact lengths
  and endpoint bytes, and confirms native-identical inventories. The refreshed
  native corpus passes **289 / 0 / 2 / 0 / 5** (PASS/FAIL/XFAIL/XPASS/SKIP).
- The local host still cannot launch Chromium or Firefox through the actual
  `file://` navigation gate. Manual Firefox acceptance remains needed for page
  boot and browser-side batch/result-contract behavior; the redirection issue
  itself is now closed by worker and full mounted-path checks. In Firefox,
  open `file:///home/a/Work/waste/build/html-rt/bash.html`, run
  `echo STAGE6C_BROWSER_OK`, then enter
  `: < /usr/share/waste/launch.wast; echo "__REDIR_STATUS__:$?"` and confirm
  `__REDIR_STATUS__:0`. Under **Diagnostics → Installed tests**, run the
  mounted corpus and save its report as
  `build/html-rt/stage6c-browser-results.json`; expected result is **290
  PASS / 0 FAIL / 2 XFAIL / 0 XPASS / 4 SKIP**, exit 0.
- Stage 6C is complete after the manual Firefox acceptance below. Stage 7
  dashboard retirement remains, estimated at **2–3 slices**.


Stage 6C.4 acceptance — manual Firefox run and browser result contract (2026-10-05):

- The user confirmed Firefox page boot and the Bash prompt/redirection checks
  succeeded. `build/html-rt/stage6c-browser-results.json` contains 296 test
  records and the expected summary: **290 PASS / 0 FAIL / 2 XFAIL / 0 XPASS /
  4 SKIP**, with empty failure and unexpected-pass lists.
- Compared with `build/cli-rt/corpus-results.json`, browser and native runners
  expose the same top-level `summary`, `tests`, and `exitCode` fields and the
  same normalized summary counters/failure lists. Runner-specific diagnostics
  remain nested in their per-test reports. No legacy test coverage was
  removed; all 56 authored harnesses remain dispositioned in the coverage
  ledger.
- Together with the focused check, mounted-browser VFS test, complete 296-file
  shell-redirection sweep, native corpus, and packaging gates above, the Stage
  6C gate is complete. Stage 7 dashboard retirement is the final layout-migration stage.


Stage 7.1 landing slice — retire the standalone test dashboard workflow (2026-10-05):

- Removed `--html-test`, `--c-engine-html`, and `--generate-html` from the
  wizard and help. Direct invocation prints the replacement command. The
  C-engine core and relaxed-SIMD/POSIX worker suites now generate payloads and
  run directly through `tests/c-engine-browser-runtime.cjs`; they no longer
  build an intermediate HTML dashboard.
- Restricted `src/html-rt/tools/build.sh` and `amalgamate.py` to the unified
  Bash/test page. Removed the OCaml dashboard generator and the unused
  standalone dashboard UI sources. The C-engine payload tool now emits worker
  metadata only, with a named `--mounted-corpus` mode preserving the full 296
  mounted test selection. Corpus collection, oracle policy, and installed VFS
  snapshots remain reusable.
- Reworked the Chromium integration gate to target only `bash.html`; its full
  batch, selection, cancellation, shell-recovery, session, and external-request
  checks remain attached to the production page. Updated package, corpus, build,
  and architecture guidance. `build/html-rt/tests/payload.json` remains an
  input for focused worker tests, not a second page payload.
- Verification: `./start.sh --help` omits the retired switches and
  `./start.sh --html-test` exits 2 with the migration hint. Shell/Python/Node
  syntax checks, `git diff --check`, the 296-source/507-node corpus audit,
  worker scheduling/filtering/timeout regression, one official
  `core/address.wast` worker test, Bash packaging guards, a fresh `bash.html`
  build, and the full focused `./start.sh --html-check` pass. The native corpus
  and user-supplied Firefox full-batch report also pass as recorded in Stage 6C.
- Removed the exact generated `build/html-rt/test.html` artifact. Active build,
  test, and architecture instructions now describe the single-page flow.
  Remaining occurrences in dated plan entries are historical evidence, not
  supported commands or dependencies. Stage 7's gate is complete; no manual
  browser rerun is needed for this cleanup slice because shell/test UI behavior
  is unchanged and Firefox acceptance was already supplied.
