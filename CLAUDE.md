# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

WASTE is a browser-hosted WebAssembly execution environment exploring a production C engine compiled to Wasm. The OCaml reference interpreter (in `submodules/wasm-spec`) serves as the behavioral oracle during migration. The project provides:

- A Wasm interpreter compiled to JavaScript/Wasm via OCaml with CPS (continuation-passing style) support for cooperative scheduling
- A guest libc (`waste-libc`) for running Bash and other POSIX applications
- An embedded browser test dashboard and standalone Bash interpreter page
- A C engine proof-of-concept focusing on tail-call performance
- DIY POSIX and libc regression test suites

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
./start.sh --c-tail-poc         # Build and benchmark C tail-call proof
./start.sh --patch-status       # Show Wasm32 compatibility patch status
./start.sh --apply-i31          # Apply Wasm32 i31-int32 patch
./start.sh --revert-i31         # Revert Wasm32 patch
./start.sh --update             # Safe git pull, submodule update, restore patch
```

### C Tail-Call Proof of Concept

The C engine proof is in `src/c-engine/` and demonstrates allocation-free tail transfers:

```sh
./start.sh --c-tail-poc                              # Full build + benchmark + HTML
tests/c-tail-poc/run.sh 5000000                      # Native gate (specify iteration count)
make -C src/c-engine sanitize                        # Build with AddressSanitizer/UBSanitizer
make -C src/c-engine BUILD_DIR=path all              # Build with custom output directory
```

The proof is intentionally restricted: `(i64) -> i64` functions only, supporting `local.get`, `i64.const`, `i64.eqz`, `i64.sub`, `if/else/end`, `ref.func`, `return_call`, and `return_call_ref`. See `docs/c-engine-handoff.md` for performance baselines and next expansion steps.

### Test Suites

```sh
# DIY POSIX regression probes (OCaml interpreter)
node tests/diy-posix-test/posix-kernel-runtime.cjs
node tests/diy-posix-test/posix-kernel-runtime.cjs threaded

# Guest libc allocator tests (both OCaml interpreters)
node tests/libc-test/libc-runtime.cjs
node tests/libc-test/libc-runtime.cjs threaded
node tests/libc-test/allocator-native.cjs

# C tail-call proof of concept
./start.sh --c-tail-poc
```

### Build Output Locations

```
build/ocaml-wasm/dist/                          # Sequential OCaml-Wasm interpreter + assets
build/ocaml-wasm/dist-threaded/                 # CPS OCaml-Wasm interpreter + assets
build/ocaml-wasm/browser-tests.html             # Offline test dashboard (embeds spec tests)
build/ocaml-wasm/bash.html                      # Self-contained Bash interpreter
build/waste-libc/waste-libc.wasm                # Guest libc binary
build/c-tail-poc/tail-call-poc.html             # C engine proof of concept page
```

### Build Logs

```
build.log                       # Latest OCaml-to-Wasm compilation
html.log                        # Dashboard generation transcript
bash-html.log                   # WASTE Bash page generation transcript
build/waste-libc/build.log      # libc build log
test.log                        # Test suite results
update.log                       # Safe pull/submodule update transcript
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

### Execution Model: Three-Tier Hierarchy

The C engine (under development) distinguishes four explicit ownership levels:

1. **Engine:** Immutable decoded modules, opcode metadata, optional code caches—shareable across all work
2. **Sandbox/Store:** One independently scheduled test or application with its own host-import environment, module registry, mutable instances, and kernel namespace
3. **Process:** Member of a sandbox kernel with private address space (via fork + eventual copy-on-write), descriptor table, lifecycle, signals. Descriptors reference shared open-file descriptions
4. **Thread:** Schedulable context inside a process with its own PC, value/control/call stacks, locals, signal mask, and pending signals; shares process address space and descriptor table

This hierarchy prevents accidental cross-test memory corruption: each spec `.wast` file gets a fresh sandbox, and modules within a sandbox may only share memory through explicit WebAssembly imports.

### Scheduler: Cooperative with Quantum Boundaries

The OCaml CPS interpreter (current implementation) yields to the browser event loop after each configured instruction quantum (default: 10,000 guest opcodes):

- Evaluator-only transitions (call, label, exception, signal frames) do not consume fuel
- Pause/resume are worker messages that gate the next slice
- Signals enter a versioned command ring at quantum boundaries
- Each test task owns an isolated continuation and runner state

The C engine will preserve this ABI. Dashboard concurrency controls limit concurrently runnable test *sandboxes*, not guest threads (guest threading is future work).

### Module Decode & Validation

The C proof (`src/c-engine/`) demonstrates:
- Bounded binary reader with structured error reporting
- One-pass decode to fixed-width numeric instructions
- Resolved direct calls and branch targets during validation
- Immutable decoded function bodies

The older `src/*.c` files (leb128, instr, validate, lookup, dis) provide reference groundwork for future phases but are not the active C engine.

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

## Key Files & Their Roles

### Documentation
- `README.md` — High-level project overview, build directions, OCaml-to-Wasm compile, guest libc, dashboard usage
- `docs/c-engine-port-plan.md` — Detailed C port strategy, phased gates, execution architecture, build integration
- `docs/c-engine-handoff.md` — C proof baseline performance, current implementation, reproduction, next safe expansion
- `docs/posix-runtime.md` — Browser POSIX runtime tiers, control-page ABI, signal delivery, broker protocol
- `AGENTS.md` — Repository guidelines, coding style, testing conventions, commit practices

### Source: C Engine Proof
- `src/c-engine/waste_tail.h` — Public API: module loading, export lookup, execution with fuel, error reporting
- `src/c-engine/waste_tail.c` — Bounded reader, decoder, validator, single-frame execution loop
- `src/c-engine/browser.c` — Freestanding Wasm adapter with bump allocator (temporary for proof only)
- `src/c-engine/main.c` — Native command-line runner
- `src/c-engine/Makefile` — Build rules for native and Wasm targets

### Source: Reference & Future Work
- `src/leb128.c/h` — LEB128 varint decoder (older foundation)
- `src/instr.c/h` — Opcode metadata and instruction enumeration (reference for future opcode tables)
- `src/validate.c/h`, `lookup.c/h`, `dis.c/h` — Validation, name resolution, disassembly (groundwork)
- `src/bash.wat`, `bash-i.wat` — Compiled Bash binaries (for browser testing)

### Source: Guest libc
- `libc/waste-libc.wat` — WebAssembly libc core (memory, allocator, exports)
- `libc/waste-libc-helpers.c` — C helpers (FILE, formatting, locale, accounts, time, regex)
- `libc/waste-libc-extra.c` — Additional utilities

### Tools & Generators
- `tools/generate-browser-tests.py` — Collects `.wast` files, embeds interpreter, produces offline dashboard HTML
- `tools/generate-bash-html.py` — Generates self-contained Bash interpreter page with CPS loader and libc
- `tools/build-bash-runtime.py` — Relinks Bash and libc binaries to shared `waste-runtime` module
- `tools/build-waste-libc.py` — Builds libc Wasm binary and test fixtures
- `tools/generate-c-tail-poc.py` — Generates C proof HTML with embedded Wasm engine and tail-call test module

### Tests
- `tests/c-tail-poc/tail-call.wat` — Tail-call proof fixture
- `tests/c-tail-poc/run.sh` — Native benchmark and truncated-module rejection test
- `tests/diy-posix-test/*.wast` — POSIX regression probes (process control, signals, VFS, clock)
- `tests/diy-posix-test/*-runtime.cjs` — Node.js harnesses for running probes against both interpreters
- `tests/libc-test/*.wast.inc` — libc test clients
- `tests/libc-test/*-runtime.cjs` — Node.js harnesses for allocator and libc tests
- `tests/tail-call-smoke.wast` — Minimal CPS Bash smoke test

### Build & Configuration
- `start.sh` — Main interactive/non-interactive build wizard (1,000+ lines)
- `build.sh` — Legacy grammar fragment extraction to `gen/`
- `src/build.sh`, `src/m.sh` — Older C prototype scripts (not used by current workflow)
- `.gitmodules` — Submodule reference to `submodules/wasm-spec`
- `submodules/wasm-spec-i31-int32.patch` — Wasm32 compatibility patch (applied/reverted via start.sh)
- `gen/` — Generated opcode production rules and grammar fragments (reference material)

### Spec Submodule
- `submodules/wasm-spec/interpreter/` — Official OCaml WebAssembly reference interpreter
- `submodules/wasm-spec/test/` — Official Wasm core test suite (embedded in browser dashboard)

## Key Design Decisions & Constraints

### Migration & Testing Strategy

1. **OCaml remains oracle:** Until C engine passes a declared compatibility gate, the OCaml reference interpreter is the behavioral standard. Both implementations must agree on all spec tests.

2. **Differential testing:** Each C feature expansion must include a fixture and compare results with OCaml before marking as complete. See `docs/c-engine-port-plan.md` for staged gates.

3. **No early optimization:** Do not claim browser speedup from native OCaml timings. Preserve C proof as a regression gate via `docs/c-engine-handoff.md` baseline.

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
- **C proof stability:** Do not widen the C engine subset without a new fixture and updated `docs/c-engine-handoff.md`
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

### Tail-Call Performance Gates

The C proof baseline (Firefox):
- `return_call`: 1,122 ms for 5M transfers = 4.5M transfers/sec, 1 frame, 0 allocations
- `return_call_ref`: 465 ms for 5M transfers = 10.7M transfers/sec, 1 frame, 0 allocations

Preserve this fixture and environment; any regression indicates a run-loop or dispatch issue. See `docs/c-engine-handoff.md`.

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
├── build.sh                           # Grammar extraction (legacy)
├── LICENSE, .gitignore, .gitmodules
│
├── src/                               # C engine proof of concept & reference work
│   ├── c-engine/                      # Bounded decoder, validator, tail-call executor
│   │   ├── waste_tail.h/c            # Public API & implementation
│   │   ├── browser.c, main.c         # Adapters
│   │   ├── Makefile                  # Build rules
│   │   └── README.md                 # Proof subset documentation
│   ├── leb128.c/h, instr.c/h          # Reference decoders & opcode metadata
│   ├── validate.c/h, lookup.c/h       # Reference groundwork
│   ├── dis.c/h                        # Disassembly
│   ├── bash.wat, bash-i.wat           # Compiled Bash binaries
│   ├── hello.wasm, tmp*               # Test/temp files
│   └── gen/                           # Generated code
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
│   ├── build-waste-libc.py            # libc compilation
│   └── generate-c-tail-poc.py         # C proof HTML generation
│
├── tests/                             # Test suites
│   ├── c-tail-poc/                    # C proof fixture & runner
│   │   ├── run.sh                     # Native benchmark & tests
│   │   └── tail-call.wat              # Proof fixture
│   ├── diy-posix-test/                # POSIX regression probes
│   │   ├── *.wast                     # Fixtures
│   │   └── *-runtime.cjs              # Node harnesses
│   ├── libc-test/                     # libc regression probes
│   │   ├── *.wast.inc                 # Test clients
│   │   ├── *-runtime.cjs              # Node harnesses
│   │   └── allocator-native.cjs       # Native allocator stress test
│   └── tail-call-smoke.wast           # Bash CPS smoke test
│
├── docs/                              # Architecture & planning
│   ├── c-engine-port-plan.md          # Detailed C port roadmap
│   ├── c-engine-handoff.md            # Proof baseline & next steps
│   ├── posix-runtime.md               # POSIX tiers, control-page ABI
│   └── return-call-two-iteration-trace.md
│
├── spec/                              # Official Wasm specification docs
│   └── README.md                      # Spec overview
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
│   ├── waste-libc/                    # libc artifacts
│   ├── c-tail-poc/                    # C proof artifacts
│   └── toolchain/                     # Wasm toolchain (if built locally)
│
├── gen/                               # Generated reference material
│   ├── productions.txt                # Opcode productions
│   └── xx*                            # Grammar fragments
│
├── build.log, test.log, *.log        # Build transcripts
└── .agents/, .codex/                 # Internal directories
```

---

For questions about specific areas, refer to the detailed docs in `docs/` and comments in `AGENTS.md`. When implementing new C features, see `docs/c-engine-handoff.md` for the next safe expansion sequence.
