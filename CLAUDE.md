# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

WASTE is a browser-hosted WebAssembly Threading Environment.  It passes the ${VERSION} webassembly spec tests.  It uses a flex/bison generative parser for wasm / wat files.  It converts these to the binary wasm op-codes and executes them in a custom frame environment.  The core reason for doing this is to provide for process / threading support in an attempt to provide a POSIX like environment within the browser.

This project approaches multi-threading with frames and program counters, which currently allows for sigset / longjmp and execution pausing.  

There are remnants left for reference of an attempt to use the OCaml reference interpreter with a threading patch in it.  It took bash 6 seconds to load.  With the new c-engine based one it runs in under a second.

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

This opens an interactive wizard for dependency checks, compilation, dashboard generation, and test execution. Non-interactive commands:

```sh
./start.sh --check              # Inspect dependencies and submodule state
./start.sh --install-deps       # Install missing system/OPAM packages
./start.sh --compile            # Build OCaml-to-Wasm interpreter (direct + CPS)
./start.sh --build-libc         # Build waste-libc.wasm and its tests
./start.sh --generate-html      # Generate offline browser test dashboard
./start.sh --generate-bash-html # Generate self-contained WASTE Bash page
./start.sh --patch-status       # Show Wasm32 compatibility patch status
./start.sh --apply-i31          # Apply Wasm32 i31-int32 patch
./start.sh --revert-i31         # Revert Wasm32 patch
./start.sh --update             # Safe git pull, submodule update, restore patch
```

### Test Suites

```sh
# DIY POSIX regression probes (OCaml interpreter)
node tests/diy-posix-test/posix-kernel-runtime.cjs
node tests/diy-posix-test/posix-kernel-runtime.cjs threaded

# Guest libc allocator tests (both OCaml interpreters)
node tests/libc-test/libc-runtime.cjs
node tests/libc-test/libc-runtime.cjs threaded
node tests/libc-test/allocator-native.cjs
```

### Build Output Locations

```
build/ocaml-wasm/dist/                          # Sequential OCaml-Wasm interpreter + assets
build/ocaml-wasm/dist-threaded/                 # CPS OCaml-Wasm interpreter + assets
build/ocaml-wasm/browser-tests.html             # Offline test dashboard (embeds spec tests)
build/ocaml-wasm/bash.html                      # Self-contained Bash interpreter
build/waste-libc/waste-libc.wasm                # Guest libc binary
```

### Build Logs

All logs are written to `build/logs/`:

```
build/logs/build.log            # Latest OCaml-to-Wasm compilation
build/logs/html.log             # Dashboard generation transcript
build/logs/bash-html.log        # WASTE Bash page generation transcript
build/logs/libc-build.log       # libc build log
build/logs/test.log             # Test suite results
build/logs/update.log           # Safe pull/submodule update transcript
build/logs/c-engine-html.log    # C engine dashboard generation
build/logs/c-engine-bash.log    # C engine Bash page generation
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

### Scheduler: Cooperative with Quantum Boundaries

The interpreter yields to the browser event loop after each configured instruction quantum (default: 10,000 guest opcodes):

- Evaluator-only transitions (call, label, exception, signal frames) do not consume fuel
- Pause/resume are worker messages that gate the next slice
- Signals enter a versioned command ring at quantum boundaries
- Each test task owns an isolated continuation and runner state

Dashboard concurrency controls limit concurrently runnable test *sandboxes*, not guest threads (guest threading is future work).

### C Engine Architecture

The C engine (`src/c-engine/`) provides:
- **Parser:** Flex/Bison grammar (`wast.l` / `wast.y`) for the full Wasm text format including GC, exceptions, tail calls, relaxed SIMD, multi-memory, and WAST script commands
- **Encoder:** Converts parsed text format to binary Wasm opcodes (`wast_encode.c`)
- **Executor:** Custom frame-based interpreter with fuel metering (`waste_exec.c`)
- **Linker:** Shared module registry, cross-module import resolution, binary import scanning, pluggable host resolver (`wast_linker.c/h`)
- **WAST runner:** Spec test harness with JSON output (`wast_runner.c`)
- **POSIX stubs:** Browser-side POSIX host function dispatch via `browser_host_resolver` callback (`posix_stubs.c/h`)
- **Browser API:** Exported WAST API functions, legacy per-module linking, browser streaming, yield/resume (`browser_api.c`)
- **Freestanding library:** Portable C implementations of string, math, and formatting functions shared by both native and Wasm builds (`freestanding_lib.c`)
- **Platform backends:** Native Linux x86_64 via raw syscalls (`freestanding_native.c`) and Wasm browser platform (allocator, I/O stubs, strtod/strtof via JS host imports) (`browser_wast.c`)

### Browser Dashboard Architecture

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

See `docs/posix-runtime.md` for control-page ABI, signal delivery, non-local-jump verification, and browser/emulation/broker policy details.

### Guest libc: waste-libc

Located in `libc/waste-libc.wat` and `libc/waste-libc-helpers.c`, this owns guest linear memory and provides:

- Boundary-tag allocator exporting `malloc`, `calloc`, `realloc`, `free`, `sbrk`, `__errno_location`
- `memory.grow`-backed MORECORE for heap expansion
- Memory-backed `FILE` streams, wasm32 variadic formatting, UTF-8 multibyte/wide-char conversion
- C.UTF-8 locale, identity/passwd/group/service records
- String, conversion, regex, resource-limit, time-formatting, terminal, and diagnostic helpers

`tools/build-bash-runtime.py` relinks Bash and libc to a neutral `waste-runtime` owner, then registers libc as an overlay on the OCaml host's `env` namespace. Operations requiring evaluator state (directory traversal, execve, descriptor readiness, dynamic loading, raw socket creation) deliberately return `ENOSYS` and must cross the OCaml process/VFS layer or the optional WebSocket broker.

Test fixture:
```sh
./start.sh --build-libc
# Outputs: build/waste-libc/waste-libc.wasm, build/bash/bash-runtime.wast
```

### Shared-Library Libc Roadmap

The goal is a shared-library model where multiple executables (bash, coreutils, etc.) share a single libc instance in the browser, rather than embedding a copy in each binary. Progress and next steps:

1. **Executables compile with `--import-memory --import-table`** — DONE. Bash and future executables import memory and table from a `waste-runtime` module rather than declaring their own.

2. **Binary-level import resolution in the engine** — DONE. `native_load_module` in `wast_linker.c` resolves imports from binary `.wasm` modules against registered modules in the `native_store`. The `native_host_resolver` callback decouples POSIX stub resolution from the shared linker.

3. **VFS binary file support** — TODO. The engine needs a virtual filesystem layer so that binary `.wasm` files (executables) can be stored and loaded by path. This enables `execve` to locate and load programs. Likely requires:
   - A VFS data structure in the engine (in-memory file table mapping paths to byte buffers)
   - API for the browser host to populate the VFS with pre-loaded binaries
   - Integration with `native_load_module` to load from VFS paths

4. **Implement `execve` in the POSIX dispatch** — TODO. Add an `execve` stub in `posix_stubs.c` that looks up the target path in the VFS, loads the binary module via `native_load_module`, and transfers control. Requires the VFS from step 3.

5. **Add more executables** — TODO. Compile coreutils and other programs with `--import-memory --import-table` targeting the `waste-runtime` module, package them into the VFS.

## Key Files & Their Roles

### Documentation
- `README.md` — High-level project overview, build directions, dashboard usage
- `docs/c-engine-port-plan.md` — Detailed C port strategy, phased gates, execution architecture, build integration
- `docs/c-engine-handoff.md` — C engine conformance status, browser baseline, implementation history
- `docs/posix-runtime.md` — Browser POSIX runtime tiers, control-page ABI, signal delivery, broker protocol
- `AGENTS.md` — Repository guidelines, coding style, testing conventions, commit practices

### Source: C Engine
- `src/c-engine/wast.y` — Bison parser for Wasm text format (MVP + GC + exceptions + tail calls + SIMD + WAST script)
- `src/c-engine/wast.l` — Flex lexer with dedicated tokens for structural keywords and generic OP for dotted instructions
- `src/c-engine/wast_runner.c/h` — WAST script runner: module instantiation, assertion dispatch, JSON output
- `src/c-engine/wast_encode.c/h` — Text-to-binary encoder: converts parsed AST to Wasm binary opcodes
- `src/c-engine/wast_stream.c/h` — Byte stream utilities for binary encoding
- `src/c-engine/wast_simd.c/h` — SIMD instruction lookup tables (0xFD prefix)
- `src/c-engine/wast_types.h` — Shared type definitions for the parser/encoder pipeline
- `src/c-engine/waste_exec.c/h` — Frame-based Wasm interpreter with fuel metering
- `src/c-engine/freestanding_lib.c` — Portable freestanding library (string, math, snprintf, conversions) shared by native and Wasm
- `src/c-engine/freestanding_native.c` — Native Linux x86_64 platform backend (raw syscalls, mmap allocator, FILE I/O)
- `src/c-engine/freestanding/` — Freestanding headers (stdio.h, stdlib.h, string.h, math.h, etc.) used via `-Ifreestanding`
- `src/c-engine/wast_linker.c/h` — Shared module linker: native_store registry, cross-module call trampoline, binary import scanner, pluggable host resolver
- `src/c-engine/browser_wast.c` — Wasm browser platform backend (strtod/strtof via JS, heap allocator, FILE I/O no-ops, getenv/isatty/exit stubs)
- `src/c-engine/posix_stubs.c/h` — POSIX host function dispatch tables and `browser_host_resolver` for the browser build
- `src/c-engine/browser_api.c` — Exported WAST API, legacy per-module linking, browser streaming, flat value helpers, yield/resume
- `src/c-engine/main_wast.c` — Native command-line WAST spec test runner
- `src/c-engine/wast_mmap_test.c` — Native WAST parse-only benchmark (mmap-based)
- `src/c-engine/Makefile` — Build rules for native and Wasm targets

### Examples
- `examples/bash.wat`, `bash-i.wat` — Compiled Bash binaries (for browser testing)

### Source: Guest libc
- `libc/waste-libc.wat` — WebAssembly libc core (memory, allocator, exports)
- `libc/waste-libc-helpers.c` — C helpers (FILE, formatting, locale, accounts, time, regex)
- `libc/waste-libc-extra.c` — Additional utilities

### Tools & Generators
- `tools/generate-browser-tests.py` — Collects `.wast` files, embeds interpreter, produces offline dashboard HTML
- `tools/generate-bash-html.py` — Generates self-contained Bash interpreter page with CPS loader and libc
- `tools/build-bash-runtime.py` — Relinks Bash and libc binaries to shared `waste-runtime` module
- `tools/build-waste-libc.py` — Builds libc Wasm binary and test fixtures

### Tests
- `tests/diy-posix-test/*.wast` — POSIX regression probes (process control, signals, VFS, clock)
- `tests/diy-posix-test/*-runtime.cjs` — Node.js harnesses for running probes against both interpreters
- `tests/libc-test/*.wast.inc` — libc test clients
- `tests/libc-test/*-runtime.cjs` — Node.js harnesses for allocator and libc tests
- `tests/tail-call-smoke.wast` — Minimal CPS Bash smoke test

### Build & Configuration
- `start.sh` — Main interactive/non-interactive build wizard
- `.gitmodules` — Submodule reference to `submodules/wasm-spec`
- `submodules/wasm-spec-i31-int32.patch` — Wasm32 compatibility patch (applied/reverted via start.sh)

### Spec Submodule
- `submodules/wasm-spec/interpreter/` — Official OCaml WebAssembly reference interpreter
- `submodules/wasm-spec/test/` — Official Wasm core test suite (embedded in browser dashboard)

## Key Design Decisions & Constraints

### Migration & Testing Strategy

1. **Spec oracle:** The OCaml reference interpreter in `submodules/wasm-spec` is the behavioral standard. Both implementations must agree on all spec tests.

2. **Differential testing:** Each C feature expansion must include a fixture and compare results with OCaml before marking as complete. See `docs/c-engine-port-plan.md` for staged gates.

### Ownership & Memory Safety

1. **Immutable engine data:** Module metadata, decoded instructions, opcode tables must never be mutated. Sharing across sandboxes is safe.

2. **Sandbox isolation:** Each `.wast` file or application owns an independent store with fresh host imports, module registry, and kernel. Tests never share POSIX state across sandboxes.

3. **Explicit shared memory:** Modules may only share memory through explicit WebAssembly imports within one sandbox. No accidental aliasing between sandboxes.

4. **Handle-based JS boundary:** Integer handles cross between C engine and JavaScript; C pointers never become guest-visible state.

### Browser Constraints

1. **Self-contained deployment:** The generated dashboard is a single `file://` HTML. No server, no external assets, no cross-origin-isolation headers required.

2. **Control ring for signals:** Signals enter a versioned command ring at quantum boundaries, avoiding `SharedArrayBuffer` and browser flags.

3. **Validation caching:** Browser skips runtime validation only when SHA-256 identities (source, CPS loader, interpreter Wasm) all match; otherwise validate normally.

### POSIX Model

1. **Operations returning ENOSYS:** Directory traversal, execve, descriptor readiness checking, dynamic loading, raw socket creation deliberately fail (not silently approximate). These must cross the OCaml VFS layer or use the optional WebSocket broker.

2. **Deterministic entropy:** The built-in entropy generator is for repeatable tests; production must seed from browser cryptography.

3. **No VirtualFile semantics in libc:** Libc cannot implement its own file semantics; it delegates to the engine-owned kernel, which owns the VFS.

## Workflow & Conventions

### Before Committing

- Run `bash -n start.sh` to check shell syntax
- Run Python bytecode checks for changed `tools/*.py` files
- Run `git diff --check` to catch trailing whitespace
- Verify shell and Python conform to surrounding indentation style

### Testing Checklist

- **Direct and threaded:** When changing scheduler, evaluator, signal, or POSIX behavior, test both `libc-runtime.cjs` and `libc-runtime.cjs threaded`
- **Spec tests:** Compare all new C behavior with OCaml oracle on official core tests
- **Browser page:** Verify dashboard remains a single offline HTML; do not introduce server requirements or external assets
- **POSIX regression:** Local fixtures in `tests/diy-posix-test/` and `tests/libc-test/` are regression probes; keep them in sync with both interpreters

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
├── src/                               # C engine
│   └── c-engine/                      # Parser, encoder, executor, test runner
│   │   ├── wast.y, wast.l            # Bison/Flex parser for Wasm text format
│   │   ├── wast_runner.c/h           # WAST script runner
│   │   ├── wast_encode.c/h           # Text-to-binary encoder
│   │   ├── wast_stream.c/h           # Byte stream utilities
│   │   ├── wast_simd.c/h             # SIMD instruction lookup
│   │   ├── wast_types.h              # Shared type definitions
│   │   ├── waste_exec.c/h            # Frame-based interpreter
│   │   ├── wast_linker.c/h           # Shared module linker + native_store
│   │   ├── freestanding_lib.c        # Portable freestanding library
│   │   ├── freestanding_native.c     # Native Linux x86_64 syscall backend
│   │   ├── freestanding/             # Freestanding C headers
│   │   ├── browser_wast.c            # Wasm browser platform backend
│   │   ├── posix_stubs.c/h           # POSIX host function dispatch
│   │   ├── browser_api.c             # Exported WAST API + browser streaming
│   │   ├── main_wast.c              # Native WAST spec test runner
│   │   ├── wast_mmap_test.c          # Parse-only benchmark
│   │   └── Makefile                  # Build rules
│
├── examples/                          # Example Wasm binaries
│   ├── bash.wat                       # Compiled Bash (interactive)
│   └── bash-i.wat                     # Compiled Bash (non-interactive)
│
├── libc/                              # Guest libc for Bash & applications
│   ├── waste-libc.wat                 # Wasm core (memory, allocator)
│   ├── waste-libc-helpers.c           # C helpers (FILE, locale, accounts, time)
│   └── waste-libc-extra.c             # Additional utilities
│
├── tools/                             # Generators & builders
│   ├── generate-browser-tests.py      # Dashboard generation
│   ├── generate-bash-html.py          # Bash page generation
│   ├── build-bash-runtime.py          # Bash + libc relinking
│   └── build-waste-libc.py            # libc compilation
│
├── tests/                             # Test suites
│   ├── c-engine-*.wast                # C engine regression fixtures
│   ├── c-engine-*.wat                 # C engine test modules
│   ├── c-engine-i32-smoke.c           # Sanitizer smoke test source
│   ├── diy-posix-test/                # POSIX regression probes
│   │   ├── *.wast                     # Fixtures
│   │   └── *-runtime.cjs             # Node harnesses
│   ├── libc-test/                     # libc regression probes
│   │   ├── *.wast.inc                 # Test clients
│   │   ├── *-runtime.cjs             # Node harnesses
│   │   └── allocator-native.cjs       # Native allocator stress test
│   └── tail-call-smoke.wast           # Bash CPS smoke test
│
├── docs/                              # Architecture & planning
│   ├── c-engine-port-plan.md          # Detailed C port roadmap
│   ├── c-engine-handoff.md            # C engine conformance & implementation history
│   ├── posix-runtime.md               # POSIX tiers, control-page ABI
│   └── return-call-two-iteration-trace.md
│
├── submodules/                        # External dependencies
│   ├── wasm-spec/                     # Official OCaml interpreter & tests
│   │   ├── interpreter/               # Reference implementation
│   │   └── test/                      # Core test suite
│   └── wasm-spec-i31-int32.patch      # Wasm32 compatibility patch
│
├── build/                             # Generated output (git-ignored)
│   ├── ocaml-wasm/
│   │   ├── dist/                      # Sequential interpreter + assets
│   │   ├── dist-threaded/             # CPS interpreter + assets
│   │   ├── browser-tests.html         # Dashboard
│   │   ├── bash.html                  # Bash interpreter page
│   │   └── staging/                   # Build overlay
│   ├── logs/                          # All build/test log files
│   ├── waste-libc/                    # libc artifacts
│   ├── c-engine/                      # C engine build artifacts
│   └── toolchain/                     # Wasm toolchain (if built locally)
│
└── .agents/, .codex/                 # Internal directories
```

---

For questions about specific areas, refer to the detailed docs in `docs/` and comments in `AGENTS.md`.
