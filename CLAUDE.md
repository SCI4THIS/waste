# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

WASTE is a browser-hosted WebAssembly Threading Environment.  It passes the ${VERSION} webassembly spec tests.  It uses a flex/bison generative parser for wasm / wat files.  It converts these to the binary wasm op-codes and executes them in a custom frame environment.  The core reason for doing this is to provide for process / threading support in an attempt to provide a POSIX like environment within the browser.

This project approaches multi-threading with frames and program counters, which currently allows for sigset / longjmp and execution pausing.  

Treat all checked-out submodules as read-only source dependencies. Stage source
under `build/` before applying repository patches or running generators,
bootstrap/configure steps, or builds; all resulting files must stay under
`build/`. See `docs/submodule-policy.md`.

The OCaml reference interpreter in `submodules/wasm-spec/interpreter` was used
as a language reference while implementing the WAT/WAST portions of the C
engine. See
[OCaml reference interpreter build](docs/techniques.md#ocaml-reference-interpreter-build)
for the minimal build/test instructions.

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
./start.sh --install-deps       # Install missing system packages
./start.sh --build-libc         # Build production libc.so.wasm
./start.sh --generate-bash-html # Generate self-contained WASTE Bash page
./start.sh --ocaml-reference    # Stage and run the OCaml spec-test interpreter
```

### Build Output Locations

```
build/aux/libc/libc.so.wasm                # Production guest shared library
build/ocaml-interpreter/                        # Staged OCaml reference interpreter
```

### Build Logs

All logs are written to `build/engine/logs/`:

```
build/engine/logs/libc-build.log       # libc build log
build/engine/logs/test.log             # Test suite results
build/engine/logs/c-engine-build.log   # C engine build transcript
build/engine/logs/c-engine-bash.log    # C engine Bash page generation
build/engine/logs/ocaml-reference.log  # OCaml reference interpreter build + spec tests
```

### Dependencies

**System packages** (Arch/Omarchy):
```sh
sudo pacman -S --needed git bubblewrap base-devel binaryen libnewt
```

OPAM/dune/menhir/OCaml are only required for `--ocaml-reference`; see
[OCaml reference interpreter build](docs/techniques.md#ocaml-reference-interpreter-build).

**Key versions:**
- Binaryen wasm-opt: 119 or newer

## High-Level Architecture

### Execution Model: Four-Level Hierarchy

The C engine distinguishes four explicit ownership levels:

1. **Engine:** Immutable decoded modules, opcode metadata, optional code caches—shareable across all work
2. **Sandbox/Store:** One independently scheduled test or application with its own host-import environment, module registry, mutable instances, and kernel namespace
3. **Process:** Member of a sandbox kernel with private address space (via fork + eventual copy-on-write), descriptor table, lifecycle, signals. Descriptors reference shared open-file descriptions
4. **Thread:** Schedulable context inside a process with its own PC, value/control/call stacks, locals, signal mask, and pending signals; shares process address space and descriptor table

This hierarchy prevents accidental cross-test memory corruption: each spec `.wast` file gets a fresh sandbox, and modules within a sandbox may only share memory through explicit WebAssembly imports.

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
- **Freestanding library:** Portable string, math, allocation, formatting, and errno support shared by native and Wasm builds (`src/aux/libc/runtime/`)
- **Platform backends:** Native Linux x86_64 support in `src/cli-rt/lib/` and Wasm/browser support in `src/html-rt/lib/`

### POSIX Runtime Model

Three capability tiers:

1. **Browser-backed:** Clocks, cryptographic entropy, terminal rendering, optional persistence
2. **Interpreter-emulated:** Processes, signals, timers, pipes, descriptor state, terminal job control, virtual filesystem (all in engine-owned kernel)
3. **Broker-backed (optional):** Raw sockets and operations browser security prevents, via versioned WebSocket protocol with request IDs, errno translation, and readiness notifications

The dashboard remains self-contained; WebSocket broker is optional for delegated capabilities. Without a broker, unsupported operations return `ENOSYS`.

See `docs/architecture.md` for runtime ownership and browser/emulation/broker
policy, and `docs/techniques.md` for continuation and non-local-jump techniques.

### Guest libc: libc.so.wasm

Built with `make -C src/aux install-libc`, the PIC library in `/usr/lib/libc.so.wasm`
shares the application's process memory and table. Sources in `src/aux/libc/` provide:

- Boundary-tag allocator exporting `malloc`, `calloc`, `realloc`, `free`, `sbrk`, `__errno_location` (`allocator.c`)
- `memory.grow`-backed MORECORE for heap expansion
- Memory-backed `FILE` streams and wasm32 variadic formatting (`stdio.c`)
- UTF-8 multibyte/wide-char conversion (`wchar.c`)
- C.UTF-8 locale (`locale.c`), identity/passwd/group/service records (`identity.c`)
- String and conversion helpers (`string.c`, `stdlib.c`)
- Regex and pattern matching (`pattern.c`), resource-limit, time-formatting, terminal, and diagnostic helpers (`misc.c`)
- Shared declarations across libc modules (`include/helper.h`)

Guest libc delegates kernel operations to the C engine through its shared
guest ABI and native/browser adapters. Dynamic loading, process/VFS behavior
and optional broker capabilities belong to C.

Test fixture:
```sh
./start.sh --build-libc
# Output: build/aux/libc/libc.so.wasm
make -C src/aux install-libc
make -C src/aux test-libc
make -C src/system-tests test-libc
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
- `lib/*.c` — Kernel, descriptor readiness, and VFS path semantics; portable C support is in `src/aux/libc/runtime/`
- `lib/include/` — Freestanding headers (stdio.h, stdlib.h, string.h, math.h, etc.) used via `-Ilib/include`
- `Makefile` — Shared Flex/Bison generation rules

### Source: Browser/Wasm Runtime (`src/html-rt/`)
- `browser_api.c` — Exported WAST API, legacy per-module linking, browser streaming, yield/resume
- `posix_stubs.c/h` — POSIX host function dispatch tables and `browser_host_resolver`
- `lib/stdlib.c`, `lib/stdio.c`, `lib/unistd.c` — Wasm platform backend

### Source: Guest Libc (`src/aux/libc/`)
- `Makefile` — Builds and explicitly installs the production PIC library
- `allocator.c` — Production guest allocator
- `wchar.c` — UTF-8 multibyte/wide-char conversion
- `locale.c` — C.UTF-8 locale support
- `identity.c` — passwd/group/service records
- `string.c` — String and conversion helpers
- `pattern.c` — Regex and pattern matching
- `misc.c` — Resource-limit, time-formatting, terminal, and diagnostic helpers
- `include/helper.h` — Shared declarations across guest libc modules
- `runtime/` — Freestanding support compiled into the interpreter itself
- `src/aux/bash/launch.wast` — Launches installed `/usr/bin/bash` through `execve`
- `tools/shared_libc.py` — Shared import/signature helper
- `tests/*.wast` — Package tests executed with `/bin/wast --verbose` inside Bash

### Examples
- `src/aux/bash/bash.mk` — Builds Bash from the read-only upstream submodule

### Tests
- `tests/diy-posix-test/*.wast` — POSIX regression probes (process control, signals, VFS, clock)
- `src/aux/NAME/tests/*.wast` — Package runtime assertions (`make -C src/aux test-NAME`)
- `src/system-tests/` — System interaction WAST and compiler SDK probes
- `tests/tail-call-smoke.wast` — Minimal Bash smoke test

### Build & Configuration
- `start.sh` — Main interactive/non-interactive build wizard
- `.gitmodules` — Submodule reference to `submodules/wasm-spec`

### Spec Submodule
- `submodules/wasm-spec/interpreter/` — OCaml reference interpreter used only as a WAT/WAST language reference
- `submodules/wasm-spec/test/` — Official Wasm core test suite

## Key Design Decisions & Constraints

### Testing Strategy

**Verification:** C engine language changes use fixtures. The OCaml reference
interpreter in `submodules/wasm-spec/interpreter` may be built via
`./start.sh --ocaml-reference` to cross-check WAT/WAST semantics; see
[OCaml reference interpreter build](docs/techniques.md#ocaml-reference-interpreter-build).
Kernel/libc/application changes use
native/browser C parity, private sanitizer gates, compiled guest ABI checks
and POSIX contract fixtures. See `docs/architecture.md` and
`docs/techniques.md`.

### Ownership & Memory Safety

1. **Immutable engine data:** Module metadata, decoded instructions, opcode tables must never be mutated. Sharing across sandboxes is safe.

2. **Sandbox isolation:** Each `.wast` file or application owns an independent store with fresh host imports, module registry, and kernel. Tests never share POSIX state across sandboxes.

3. **Explicit shared memory:** Modules may only share memory through explicit WebAssembly imports within one sandbox. No accidental aliasing between sandboxes.

4. **Handle-based JS boundary:** Integer handles cross between C engine and JavaScript; C pointers never become guest-visible state.

### Browser Constraints

1. **Self-contained deployment:** `bash.html` serves shell and installed-test diagnostics as one `file://` HTML page. No server or external assets are required.

2. **C control APIs:** Use C engine/session control contracts and versioned
   integer handles.

### POSIX Model

1. **Unavailable capabilities:** Return unsupported errors when the C
   engine/native/browser adapters cannot provide an operation. Route delegated
   capabilities through the optional WebSocket broker.

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
  POSIX, process and libc changes.
- **Spec tests:** Optionally cross-check supported Wasm/WAT/WAST language
  behavior with the OCaml reference interpreter via
  `./start.sh --ocaml-reference`.
- **Browser page:** Keep shell and installed-test diagnostics in one offline HTML; do not introduce server requirements or external assets
- **POSIX regression:** Local DIY/libc fixtures are C runtime regression probes.

### Code Style

- Snake_case for C functions and shell constants; kebab-case for dashboard/log file names
- Quote all shell expansions
- Do not edit generated Flex/Bison files, build artifacts, or upstream submodule history directly
- New C code must use bounded readers, structured errors, explicit ownership, and integer handles across the JavaScript boundary
- Place optional performance counters behind `WASTE_PROFILE` macro; keep hot paths allocation-free

### Naming Conventions

- Environment variable overrides: `WASTE_*` (e.g., `WASTE_INSTRUCTION_QUANTUM`)
- Submodule patches: represent repository changes as patch files in the
  repository and apply them only to staged source under `build/`; never edit
  submodule history or working trees

## Advanced Topics

### Instruction Quantum & Scheduling

The engine supports a configurable quantum for cooperative yielding. See
`docs/architecture.md` for the current scheduling model.

### WebSocket Broker Protocol

If implementing broker-backed capabilities, design the protocol to be versioned, asynchronous, capability-scoped, with explicit `errno` translation, cancellation, and readiness notifications. The C engine must retain descriptor identity and blocking behavior; the broker is a capability transport only.

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
│   │   └── lib/                      # Kernel/path/readiness and private headers
│   ├── aux/                          # Guest commands and package libraries
│   │   └── libc/                     # Guest libc.so.wasm implementation
│   │       ├── allocator.c          # PIC guest allocator
│   │       ├── stdio.c, string.c, ... # Guest libc APIs and wrappers
│   │       ├── include/helper.h     # Private guest declarations
│   │       ├── runtime/             # Interpreter's freestanding C support
│   │       └── tools/               # Shared-import helper
│   ├── system-tests/                # System interaction WAST and SDK probes
│   └── html-rt/                     # Browser packaging layer
│       ├── lib/                     # Browser platform backend
│       │   └── stdlib.c, stdio.c, unistd.c
│       └── tools/                    # HTML packaging, SDK and corpus tools
│
├── tests/                             # Test suites
│   ├── c-engine-*.wast                # Engine regression fixtures
│   ├── c-engine-*.wat                 # Engine test modules
│   ├── c-engine-i32-smoke.c           # Sanitizer smoke test source
│   ├── diy-posix-test/                # POSIX regression probes
│   └── tail-call-smoke.wast           # Bash tail-call smoke test
│
├── docs/                              # Architecture & planning
│
├── submodules/                        # External dependencies
│   └── wasm-spec/                     # Reference interpreter & test corpus
│
├── build/                             # Generated output (git-ignored)
│   ├── engine/                        # Engine build artifacts
│   ├── html-rt/                       # Browser artifacts (libc, bash runtime)
│   ├── ocaml-interpreter/             # Staged OCaml reference interpreter
│   └── toolchain/                     # Wasm toolchain (if built locally)
│
└── .agents/, .codex/                 # Internal directories
```

---

For questions about specific areas, refer to the detailed docs in `docs/` and comments in `AGENTS.md`.
