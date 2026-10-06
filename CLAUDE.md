# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

WASTE is a browser-hosted WebAssembly Threading Environment.  It passes the ${VERSION} webassembly spec tests.  It uses a flex/bison generative parser for wasm / wat files.  It converts these to the binary wasm op-codes and executes them in a custom frame environment.  The core reason for doing this is to provide for process / threading support in an attempt to provide a POSIX like environment within the browser.

This project approaches multi-threading with frames and program counters, which currently allows for sigset / longjmp and execution pausing.  

OCaml is retained only as a reference for Wasm/WAT/WAST language semantics and
standard spec-test scaffolding. The application-engine experiment was not
practical. Do not develop additional OCaml kernel, POSIX, process, scheduler,
VFS, signal, terminal, libc-host or broker capabilities. The existing OCaml
kernel and application-runtime integration are planned for removal in deferred
cleanup; see `docs/active-ocaml-language-oracle-plan.md`. C owns production
application execution. Missing OCaml POSIX imports do not block C acceptance.

There are 3 categories of POSIX functionality:

1. directly implementable, such as signal, string, memory, math operations.
2. emulatable, such as file-input-output on a virtual file system
3. un-implementable such as raw sockets.

For functionality in category 3 the design is to define a websocket communication that can carry these commands over to a websocket server which can perform appropriate action and return meaningful data.

## Build System & Commands

### Main Build Entry Point

All builds start from the repository root:

```sh
./start.sh
```

This opens an interactive wizard for dependency checks, compilation, and test execution. Non-interactive commands:

```sh
./start.sh --check              # Inspect dependencies and submodule state
./start.sh --install-deps       # Install missing system/OPAM packages
./start.sh --compile            # Build OCaml-to-Wasm interpreter (direct + CPS)
./start.sh --build-libc         # Build waste-libc.wasm and its tests
./start.sh --generate-bash-html # Generate self-contained WASTE Bash page
./start.sh --patch-status       # Show Wasm32 compatibility patch status
./start.sh --apply-i31          # Apply Wasm32 i31-int32 patch
./start.sh --revert-i31         # Revert Wasm32 patch
./start.sh --update             # Safe git pull, submodule update, restore patch
```

### Test Suites

```sh
# Legacy DIY POSIX probes (OCaml; retained pending deferred retirement)
node tests/diy-posix-test/posix-kernel-runtime.cjs
node tests/diy-posix-test/posix-kernel-runtime.cjs threaded

# Legacy OCaml guest libc probes; native allocator check remains separate
node tests/libc-test/libc-runtime.cjs
node tests/libc-test/libc-runtime.cjs threaded
node tests/libc-test/allocator-native.cjs
```

### Build Output Locations

```
build/ocaml/dist/                               # Sequential OCaml-Wasm interpreter + assets
build/ocaml/dist-threaded/                      # CPS OCaml-Wasm interpreter + assets
build/html-rt/bash-ocaml.html                   # Self-contained OCaml Bash interpreter
build/html-rt/waste-libc/waste-libc.wasm        # Guest libc binary
```

### Build Logs

All logs are written to `build/engine/logs/`:

```
build/engine/logs/build.log            # Latest OCaml-to-Wasm compilation
build/engine/logs/bash-html.log        # WASTE Bash page generation transcript
build/engine/logs/libc-build.log       # libc build log
build/engine/logs/test.log             # Test suite results
build/engine/logs/update.log           # Safe pull/submodule update transcript
build/engine/logs/c-engine-build.log   # C engine build transcript
build/engine/logs/c-engine-bash.log    # C engine Bash page generation
```

### Dependencies

**System packages** (Arch/Omarchy):
```sh
sudo pacman -S --needed git opam bubblewrap base-devel binaryen libnewt
```

**OPAM packages** (managed by start.sh, installed in isolated switch `waste-wasm`):
- `dune`, `menhir`
- `wasm_of_ocaml-compiler`, `js_of_ocaml`, `js_of_ocaml-ppx`

**Key versions:**
- OCaml: 5.3.0
- Binaryen wasm-opt: 119 or newer

## High-Level Architecture

### Execution Model: Four-Level Hierarchy

The C engine distinguishes four explicit ownership levels:

1. **Engine:** Immutable decoded modules, opcode metadata, optional code caches—shareable across all work
2. **Sandbox/Store:** One independently scheduled test or application with its own host-import environment, module registry, mutable instances, and kernel namespace
3. **Process:** Member of a sandbox kernel with private address space (via fork + eventual copy-on-write), descriptor table, lifecycle, signals. Descriptors reference shared open-file descriptions
4. **Thread:** Schedulable context inside a process with its own PC, value/control/call stacks, locals, signal mask, and pending signals; shares process address space and descriptor table

This hierarchy prevents accidental cross-test memory corruption: each spec `.wast` file gets a fresh sandbox, and modules within a sandbox may only share memory through explicit WebAssembly imports.

### Legacy OCaml Scheduler: Cooperative with Quantum Boundaries

This records the earlier scheduled OCaml experiment for deferred retirement.
It does not authorize additional OCaml scheduler/kernel development. Current
C evaluator/session ownership is described in `docs/architecture.md`.

The legacy interpreter yields to the browser event loop after each configured instruction quantum (default: 10,000 guest opcodes):

- Evaluator-only transitions (call, label, exception, signal frames) do not consume fuel
- Pause/resume are worker messages that gate the next slice
- Signals enter a versioned command ring at quantum boundaries
- Each test task owns an isolated continuation and runner state

Dashboard concurrency controls limit concurrently runnable test *sandboxes*, not guest threads (guest threading is future work).

### Engine Architecture

The engine (`src/engine/`) provides:
- **Public API:** Single `waste.h` header with values, errors, and opaque decoded-module and instance handles (`include/`)
- **Parser:** Reentrant Flex/Bison WAT grammar, context, builder, and literals (`wat/`)
- **Binary pipeline:** Bounded readers/writers, encoding, decoding, opcode metadata, and loading (`wasm/`)
- **Execution:** Frame-based dispatch, opcode-family handlers, and validation (`op/`)
- **Runtime:** Store, instance lifecycle, and instantiation (engine root)
- **WAST runner:** Command streaming, assertions, and WAT/WAST policy (`wast/`)
- **POSIX stubs:** Browser-side POSIX host function dispatch via `browser_host_resolver` callback (`posix_stubs.c/h`)
- **Browser API:** Exported WAST API functions, legacy per-module linking, browser streaming, yield/resume (`browser_api.c`)
- **Freestanding library:** Portable string, math, allocation, formatting, and errno support shared by native and Wasm builds (`src/engine/lib/`)
- **Platform backends:** Native Linux x86_64 support in `src/cli-rt/lib/` and Wasm/browser support in `src/html-rt/lib/`

### Legacy OCaml Browser Dashboard Architecture

The description below records the earlier experiment. It is not a runtime
to develop further; production execution uses the C engine and worker. Useful
legacy coverage must be accounted for before deferred kernel removal.

**Generate time:**
- Collects `.wast` test files from `submodules/wasm-spec/test`
- Embeds the CPS interpreter Wasm module as base64
- Groups tests by directory, marks legacy exception tests as unsupported
- Generates one self-contained `file://` HTML file

**Runtime:**
- Worker wraps the interpreter with pause/resume, signal ring, and quantum control
- No server, no cross-origin isolation, no `SharedArrayBuffer` required
- Signals validated via SHA-256 identities (source, CPS loader, interpreter Wasm)
- Test results can be downloaded as JSON (timing, exit code, output, errors)

### POSIX Runtime Model

Three capability tiers:

1. **Browser-backed:** Clocks, cryptographic entropy, terminal rendering, optional persistence
2. **Interpreter-emulated:** Processes, signals, timers, pipes, descriptor state, terminal job control, virtual filesystem (all in engine-owned kernel)
3. **Broker-backed (optional):** Raw sockets and operations browser security prevents, via versioned WebSocket protocol with request IDs, errno translation, and readiness notifications

The dashboard remains self-contained; WebSocket broker is optional for delegated capabilities. Without a broker, unsupported operations return `ENOSYS`.

See `docs/architecture.md` for runtime ownership and browser/emulation/broker
policy, and `docs/techniques.md` for continuation and non-local-jump techniques.

### Guest libc: waste-libc

Split across `src/html-rt/lib/` in focused modules, this owns guest linear memory and provides:

- Boundary-tag allocator exporting `malloc`, `calloc`, `realloc`, `free`, `sbrk`, `__errno_location` (`src/html-rt/lib/stdlib.wat`)
- `memory.grow`-backed MORECORE for heap expansion
- Memory-backed `FILE` streams and wasm32 variadic formatting (`lib/stdio.c`)
- UTF-8 multibyte/wide-char conversion (`lib/wchar.c`)
- C.UTF-8 locale (`lib/locale.c`), identity/passwd/group/service records (`lib/identity.c`)
- String and conversion helpers (`lib/string.c`, `lib/stdlib.c`)
- Regex and pattern matching (`lib/pattern.c`), resource-limit, time-formatting, terminal, and diagnostic helpers (`lib/misc.c`)
- Shared declarations across libc modules (`lib/include/helper.h`)

Guest libc delegates kernel operations to the C engine through its shared
guest ABI and native/browser adapters. Dynamic loading, process/VFS behavior
and optional broker capabilities belong to C. The earlier OCaml env overlay
is legacy integration to remove, not a provider implementation target.

Test fixture:
```sh
./start.sh --build-libc
# Outputs: build/html-rt/waste-libc/waste-libc.wasm, build/ocaml/bash-runtime.wast
```

### Shared-Library Libc Roadmap

The goal is a shared-library model where multiple executables (bash, coreutils, etc.) share a single libc instance in the browser, rather than embedding a copy in each binary. Progress and next steps:

1. **Executables compile with `--import-memory --import-table`** — DONE. Bash and future executables import memory and table from a `waste-runtime` module rather than declaring their own.

2. **Binary-level import resolution in the engine** — DONE. `native_load_module` in `store.c` resolves decoded imports from binary `.wasm` modules against registered modules in the `native_store`. The `native_host_resolver` callback decouples POSIX stub resolution from the shared store.

3. **VFS binary file support** — TODO. The engine needs a virtual filesystem layer so that binary `.wasm` files (executables) can be stored and loaded by path. This enables `execve` to locate and load programs. Likely requires:
   - A VFS data structure in the engine (in-memory file table mapping paths to byte buffers)
   - API for the browser host to populate the VFS with pre-loaded binaries
   - Integration with `native_load_module` to load from VFS paths

4. **Implement `execve` in the POSIX dispatch** — TODO. Add an `execve` stub in `posix_stubs.c` that looks up the target path in the VFS, loads the binary module via `native_load_module`, and transfers control. Requires the VFS from step 3.

5. **Add more executables** — TODO. Compile coreutils and other programs with `--import-memory --import-table` targeting the `waste-runtime` module, package them into the VFS.

## Key Files & Their Roles

### Documentation
- `README.md` — High-level project overview, build directions, dashboard usage
- `docs/architecture.md` — Runtime roles, ownership, phase boundaries, POSIX model, and browser deployment
- `docs/techniques.md` — Parser, validator, linker, execution, continuation, and testing practices
- `docs/active-c-engine-select-pselect-plan.md` — Full engine-owned descriptor readiness implementation plan
- `AGENTS.md` — Repository guidelines, coding style, testing conventions, commit practices

### Source: Engine (`src/engine/`)
- `include/waste.h` — Single public header: values, errors, and opaque module/instance API
- `wat/` — WAT lexer, parser, context, builder, literal conversion, and text AST
- `wast/` — WAST command classification/streaming, runner, and assertions
- `wasm/` — Binary reader/writer, LEB, encoder, decoder, loader, opcode metadata, and module
- `op/` — Frame-based dispatch, opcode-family execution units, and validation
- Engine root — Store, internal engine interface, instance lifetime, and instantiation
- `lib/*.c` — Portable freestanding string, math, allocation, formatting, and errno support shared by native and Wasm
- `lib/include/` — Freestanding headers (stdio.h, stdlib.h, string.h, math.h, etc.) used via `-Ilib/include`
- `Makefile` — Shared Flex/Bison generation rules

### Source: Browser/Wasm Runtime (`src/html-rt/`)
- `browser_api.c` — Exported WAST API, legacy per-module linking, browser streaming, yield/resume
- `posix_stubs.c/h` — POSIX host function dispatch tables and `browser_host_resolver`
- `lib/stdlib.c`, `lib/stdio.c`, `lib/unistd.c` — Wasm platform backend and guest libc (guarded by `WASTE_ENGINE`)
- `src/html-rt/lib/stdlib.wat` — WebAssembly guest libc core (memory, allocator, exports)
- `lib/wchar.c` — UTF-8 multibyte/wide-char conversion
- `lib/locale.c` — C.UTF-8 locale support
- `lib/identity.c` — passwd/group/service records
- `lib/string.c` — String and conversion helpers
- `lib/pattern.c` — Regex and pattern matching
- `lib/misc.c` — Resource-limit, time-formatting, terminal, and diagnostic helpers
- `lib/include/helper.h` — Shared declarations across guest libc modules
- `tools/generate-bash-html.py` — Generates self-contained Bash interpreter page with CPS loader and libc
- `tools/build-bash-runtime.py` — Relinks Bash and libc binaries to shared `waste-runtime` module
- `tools/build-waste-libc.py` — Builds libc Wasm binary and test fixtures

### Examples
- `examples/bash.wat`, `bash-i.wat` — Compiled Bash binaries (for browser testing)

### Tests
- `tests/diy-posix-test/*.wast` — POSIX regression probes (process control, signals, VFS, clock)
- `tests/diy-posix-test/*-runtime.cjs` — Legacy OCaml probe harnesses pending coverage accounting and deferred kernel retirement
- `tests/libc-test/*.wast.inc` — libc test clients
- `tests/libc-test/*-runtime.cjs` — Node.js harnesses for allocator and libc tests
- `tests/tail-call-smoke.wast` — Minimal CPS Bash smoke test

### Build & Configuration
- `start.sh` — Main interactive/non-interactive build wizard
- `.gitmodules` — Submodule reference to `submodules/wasm-spec`
- `submodules/wasm-spec-i31-int32.patch` — Wasm32 compatibility patch (applied/reverted via start.sh)

### Spec Submodule
- `submodules/wasm-spec/interpreter/` — Official OCaml WebAssembly reference interpreter
- `submodules/wasm-spec/test/` — Official Wasm core test suite and OCaml language-oracle inputs

## Key Design Decisions & Constraints

### Migration & Testing Strategy

1. **Language oracle:** Compare supported official Wasm/WAT/WAST language
   tests with the OCaml reference interpreter in `submodules/wasm-spec`.
   POSIX imports in WAST clients are outside that oracle scope.

2. **Verification:** C language changes use fixtures and OCaml language
   comparison. Kernel/libc/application changes use native/browser C parity,
   private sanitizer gates, compiled guest ABI checks and POSIX contract
   fixtures. No OCaml kernel parity or new providers are required. See
   `docs/architecture.md` and `docs/techniques.md`.

### Ownership & Memory Safety

1. **Immutable engine data:** Module metadata, decoded instructions, opcode tables must never be mutated. Sharing across sandboxes is safe.

2. **Sandbox isolation:** Each `.wast` file or application owns an independent store with fresh host imports, module registry, and kernel. Tests never share POSIX state across sandboxes.

3. **Explicit shared memory:** Modules may only share memory through explicit WebAssembly imports within one sandbox. No accidental aliasing between sandboxes.

4. **Handle-based JS boundary:** Integer handles cross between C engine and JavaScript; C pointers never become guest-visible state.

### Browser Constraints

1. **Self-contained deployment:** `bash.html` serves shell and installed-test diagnostics as one `file://` HTML page. No server or external assets are required.

2. **C control APIs:** Use C engine/session control contracts and versioned
   integer handles. The legacy OCaml signal ring is not a required C ABI.

3. **Legacy validation caching:** The OCaml dashboard used source/CPS-loader/
   interpreter hashes. Keep this as historical context for deferred removal,
   not an acceptance rule for the current C worker.

### POSIX Model

1. **Unavailable capabilities:** Return unsupported errors when the C
   engine/native/browser adapters cannot provide an operation. Route delegated
   capabilities through the optional WebSocket broker. Do not route C runtime
   behavior through OCaml or implement new OCaml kernel providers.

2. **Deterministic entropy:** The built-in entropy generator is for repeatable tests; production must seed from browser cryptography.

3. **No VirtualFile semantics in libc:** Libc cannot implement its own file semantics; it delegates to the engine-owned kernel, which owns the VFS.

## Workflow & Conventions

### Before Committing

- Run `bash -n start.sh` to check shell syntax
- Run Python bytecode checks for changed `src/html-rt/tools/*.py` files
- Run `git diff --check` to catch trailing whitespace
- Verify shell and Python conform to surrounding indentation style

### Testing Checklist

- **Kernel/runtime:** Test native and browser C paths for scheduler, signal,
  POSIX, process and libc changes. Legacy direct/threaded OCaml POSIX checks
  are coverage evidence for deferred retirement, not C acceptance gates.
- **Spec tests:** Compare supported Wasm/WAT/WAST language behavior with the
  OCaml oracle on official language tests.
- **Browser page:** Keep shell and installed-test diagnostics in one offline HTML; do not introduce server requirements or external assets
- **POSIX regression:** Local DIY/libc fixtures are C runtime regression
  probes. Preserve useful legacy assertions through explicit coverage mapping
  before removing OCaml kernel-dependent drivers.

### Code Style

- Snake_case for C functions and shell constants; kebab-case for dashboard/log file names
- Quote all shell expansions
- Do not edit generated Flex/Bison files, build artifacts, or upstream submodule history directly
- New C code must use bounded readers, structured errors, explicit ownership, and integer handles across the JavaScript boundary
- Place optional performance counters behind `WASTE_PROFILE` macro; keep hot paths allocation-free

### Naming Conventions

- Environment variable overrides: `WASTE_*` (e.g., `WASTE_OCAML_SWITCH`, `WASTE_INSTRUCTION_QUANTUM`)
- Patch status: reported as `available`, `applied`, or `conflict`
- Submodule patch: always represent as `submodules/wasm-spec-i31-int32.patch`, never edit submodule history directly

## Advanced Topics

### Instruction Quantum & Scheduling

The CPS interpreter executes `WASTE_INSTRUCTION_QUANTUM` guest opcodes per time slice (default: 10,000). Evaluator transitions are free; only decoded guest opcodes consume fuel. Use `WASTE_BASH_INSTRUCTION_QUANTUM` for the Bash page generator (default: 1,000,000 for faster startup).

### Profile Startup Phases

Enable in the Bash HTML generator to print cumulative millisecond timestamps for parsing, decoding, validation, import resolution, evaluator initialization, registration, invocation, opcode-category sampling, and browser markers (Date.now vs performance.now deltas).

### WebSocket Broker Protocol

If implementing broker-backed capabilities, design the protocol to be versioned, asynchronous, capability-scoped, with explicit `errno` translation, cancellation, and readiness notifications. The C engine must retain descriptor identity and blocking behavior; the broker is a capability transport only.

### Wasm32 Compatibility

The optional `wasm-spec-i31-int32.patch` allows compilation on systems where OCaml `int` is 32 bits. Apply/revert via `./start.sh --apply-i31` or `--revert-i31`. The patch preserves unsigned i31 and decoded u32, prevents alignment-shift overflow, and rejects unrepresentable local counts. Patch changes do not affect existing build artifacts until recompilation.

## Repository Layout Quick Reference

```
/
├── README.md                          # Project overview
├── AGENTS.md                          # Repository guidelines
├── CLAUDE.md                          # This file
├── start.sh                           # Main build wizard
├── LICENSE, .gitignore, .gitmodules
│
├── src/
│   ├── engine/                        # Wasm engine: parser, executor, linker
│   │   ├── include/waste.h           # Public API: values, errors, module/instance
│   │   ├── wat/                      # WAT text format: lexer, parser, builder, literals
│   │   ├── wast/                     # WAST script: runner, assertions, streaming, commands
│   │   ├── wasm/                     # Binary pipeline: reader/writer, encoder, decoder
│   │   ├── op/                       # Execution: dispatch, opcode families, validation
│   │   ├── store.c/h                 # Module registry and store
│   │   ├── instantiate.c/h          # Module instantiation
│   │   ├── instance.c               # Instance lifecycle
│   │   ├── api.c                     # Public API implementation
│   │   ├── engine_internal.h         # Internal engine interface
│   │   ├── runtime_internal.h        # Internal runtime interface
│   │   ├── Makefile                  # Flex/Bison generation rules
│   │   └── lib/                      # Freestanding support library
│   │       ├── freestanding_lib.c    # Portable freestanding library
│   │       ├── freestanding_native.c # Native Linux x86_64 syscall backend
│   │       └── include/              # Freestanding C headers
│   │
│   └── html-rt/                       # Browser packaging layer
│       ├── lib/                       # Platform backend + guest libc
│       │   ├── stdlib.c, stdio.c, unistd.c  # Wasm platform backend
│       │   ├── stdlib.wat             # Wasm core (memory, allocator)
│       │   ├── wchar.c               # UTF-8 multibyte/wide-char
│       │   ├── locale.c              # C.UTF-8 locale
│       │   ├── identity.c            # passwd/group/service records
│       │   ├── string.c              # String/conversion helpers
│       │   ├── pattern.c             # Regex and pattern matching
│       │   ├── misc.c                # Resource, time, terminal, diag
│       │   └── include/
│       │       └── helper.h          # Shared guest libc declarations
│       └── tools/                     # HTML generators & builders
│           ├── generate-bash-html.py
│           ├── build-bash-runtime.py
│           └── build-waste-libc.py
│
├── examples/                          # Example Wasm binaries
│   ├── bash.wat                       # Compiled Bash (interactive)
│   └── bash-i.wat                     # Compiled Bash (non-interactive)
│
├── tests/                             # Test suites
│   ├── c-engine-*.wast                # Engine regression fixtures
│   ├── c-engine-*.wat                 # Engine test modules
│   ├── c-engine-i32-smoke.c           # Sanitizer smoke test source
│   ├── diy-posix-test/                # POSIX regression probes
│   ├── libc-test/                     # libc regression probes
│   └── tail-call-smoke.wast           # Bash CPS smoke test
│
├── docs/                              # Architecture & planning
│
├── submodules/                        # External dependencies
│   ├── wasm-spec/                     # Official OCaml interpreter & tests
│   └── wasm-spec-i31-int32.patch      # Wasm32 compatibility patch
│
├── build/                             # Generated output (git-ignored)
│   ├── ocaml-wasm/                    # OCaml interpreter artifacts
│   ├── engine/                        # Engine build artifacts
│   ├── logs/                          # All build/test log files
│   ├── waste-libc/                    # libc artifacts
│   └── toolchain/                     # Wasm toolchain (if built locally)
│
└── .agents/, .codex/                 # Internal directories
```

---

For questions about specific areas, refer to the detailed docs in `docs/` and comments in `AGENTS.md`.
