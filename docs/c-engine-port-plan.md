# C Engine Porting Plan

## Purpose

WASTE will move its production browser execution path from the OCaml reference
interpreter to a repository-owned C engine compiled to WebAssembly. The OCaml
interpreter remains the executable specification, differential-testing oracle,
and fallback while the C implementation gains coverage. This is an incremental
migration, not a rewrite of the browser harness, POSIX model, or guest libc.

The port is intended to provide:

- predictable pause, resume, signal, and instruction-budget boundaries;
- compact numeric PCs, value stacks, control stacks, and reusable call frames;
- allocation-free hot instruction and proper-tail-call paths;
- separately measurable decode, validation, instantiation, and execution time;
- the same C core in native diagnostic builds and browser Wasm builds; and
- a self-contained `file://` dashboard with no required server.

Passing a single operation between OCaml and C is explicitly not the goal. The
cross-runtime representation cost would preserve most of the current overhead.
The C engine must own complete execution slices and return only for completion,
a trap, an import, a signal, blocking, or exhausted fuel.

## Starting Point

The existing `src/` tree is useful groundwork rather than a working engine.
`leb128.c`, `instr.c`, `validate.c`, `lookup.c`, and `dis.c` decode and inspect
an older core binary format. `execute.c` currently validates a module and finds
its start function; it does not execute it. Before reuse, the decoder needs
bounded reads, structured errors instead of assertions, current opcode support,
semantic type validation, ownership rules, and tests for malformed inputs.

Keep the existing implementation available during the transition, but organize
new code around explicit interfaces:

```text
src/
  core/       public engine API, values, traps, configuration
  binary/     bounded binary decoder, validator, module builder
  wat/        reentrant Flex scanner, Bison parser, two-pass encoder
  runtime/    module instances, stacks, scheduler, imports, signals
  posix/      process, descriptor, VFS, terminal, clock, broker model
  tools/      native runner and diagnostic disassembler
```

Generated Flex/Bison files belong under `build/`, not source control. Grammar,
scanner, opcode metadata, and generation scripts remain repository sources.

## Ownership Hierarchy

Do not use scheduler tasks, POSIX processes, and guest threads as synonyms. The
C implementation has four explicit ownership levels:

1. **Engine:** immutable decoded modules, opcode metadata, and optional code
   caches. Sharing here cannot expose mutable guest state.
2. **Sandbox/store:** one independently launched `.wast` script or application.
   It owns the host-import environment, module registry, mutable module
   instances, and a kernel namespace. Separate spec-test files get separate
   sandboxes even when one engine schedules them concurrently.
3. **Process:** a member of a sandbox kernel with its own address space,
   descriptor table, lifecycle, process group, session, and process-directed
   signals. `fork` clones memories, tables, globals, and evaluator state,
   eventually using copy-on-write, while inherited descriptors continue to
   reference shared open-file descriptions.
4. **Thread:** a schedulable context inside a process. It owns its PC, operand,
   control and call stacks, locals, signal mask, and thread-directed pending
   signals, while sharing the process address space and descriptor table.

Explicit WebAssembly imports may alias a memory or table between module
instances in the same address space. That is intentional object sharing, not
permission for unrelated sandboxes or processes to share all memory. Until
guest threads are implemented, dashboard concurrency controls select runnable
test sandboxes; they are not a pthread count.

## Build and TUI Integration

Add Flex and Bison to dependency reporting alongside the existing C compiler,
Clang, Wasm linker, and Binaryen checks. Replace the ad hoc C build scripts with
one reproducible build description that provides at least:

```text
build/c-engine/native/waste
build/c-engine/native/waste-sanitize
build/c-engine/wasm/waste-engine.wasm
```

Native and browser builds must compile the same engine sources; only host
adapters differ. Development builds retain names and source maps. Release builds
use `-O3`, hidden visibility, section garbage collection, and a final
`wasm-opt` stage only after semantic tests pass.

Extend `start.sh` incrementally with entries to build the C engine, run its unit
and differential suites, and generate the static dashboard with an engine
selector. Keep current OCaml build/test entries until final cutover. Every menu
action also needs a non-interactive option and a dedicated log so build,
conformance, and performance timing are not conflated.

## Execution Architecture

Decode each function once into an immutable fixed-width instruction array.
Resolve direct calls and branch targets during validation. Keep module metadata
immutable and put mutable instance/process/thread data in separate objects.

```c
typedef struct {
  uint16_t opcode;
  uint16_t flags;
  uint32_t a;
  uint32_t b;
  uint64_t immediate;
} waste_instr;

typedef struct {
  uint32_t function_index;
  uint32_t pc;
  uint32_t value_sp;
  uint32_t frame_sp;
  uint32_t control_sp;
  uint32_t pending_signals;
  uint32_t state;
} waste_thread;

waste_stop waste_run(waste_engine *, uint32_t thread_id, uint64_t fuel);
```

`waste_run` executes a full slice in a tight `switch`/`br_table` loop. It checks
fuel and a summarized pending-event word at the dispatch boundary. Profiling is
compiled behind `WASTE_PROFILE`; a disabled build must contain no counter branch.
The profile records opcode counts, calls, frame high-water marks, allocations,
imports, yields, traps, and elapsed host timestamps.

Normal calls push compact frames. `return_call` and `return_call_ref` validate
the target, replace arguments and locals in the active frame, set the numeric PC
to the callee entry, and continue dispatch without allocating or changing the
original return continuation. Imports return a typed stop record when the host
cannot complete synchronously. Blocking operations park the thread on a kernel
wait object instead of polling.

## WAT and WAST Front End

Use reentrant Flex and pure Bison interfaces so independent WASTE tasks can
parse concurrently:

```lex
%option reentrant bison-bridge noyywrap
```

```bison
%define api.pure full
%locations
%parse-param { struct wat_context *ctx }
%lex-param   { yyscan_t scanner }
```

Locations and semantic values store source offsets or interned identifiers,
never pointers into a buffer that may later be overwritten. Enforce limits on
nesting, tokens, names, functions, locals, section sizes, and total output.
Build long vectors such as `br_table` targets iteratively or with bounded,
tail-recursive helpers; official stress fixtures must not consume host call
stack in proportion to the number of immediates.

WAT translation uses two passes:

1. Parse without overwriting input. Intern names, resolve scopes, canonicalize
   type uses, count sections, record function/source ranges, and calculate exact
   encoded sizes. Retain compact metadata rather than a complete tree whenever
   possible.
2. Parse again and emit canonical Wasm sections using the resolved tables and
   known sizes. Initially write to a separate output arena. After differential
   tests are stable, allow guarded output to overlap and compact the original
   WAT allocation. A small fallback buffer remains mandatory when the writer
   could reach unread lexer input.

The first grammar milestone targets flat numeric WAT emitted by `wasm-dis`,
including `src/bash.wat`. Named references, inline import/export/type sugar,
folded instruction expressions, exact floating-point/NaN syntax, UTF-8 escapes,
and proposal syntax follow. Do not silently accept unsupported constructs.

`.wast` is a script language, not a single module. Its parser produces commands
such as module registration, invocation, and assertions. Reuse the dashboard's
existing result schema, but keep WAST orchestration outside the binary module
validator. Known embedded modules such as Bash should be assembled during HTML
generation; runtime WAT parsing remains available for uploaded or interactive
input.

## POSIX and Browser Integration

Port kernel ownership from OCaml to `src/posix/` without moving policy into
JavaScript. Browser-backed, engine-emulated, and optional WebSocket-broker
capability tiers remain unchanged. The C kernel owns PIDs, process groups,
sessions, lifecycle, signals, timers, VFS nodes, open-file descriptions,
descriptors, pipes, wait queues, terminal state, and `errno` translation.

Use integer handles across the JavaScript boundary. JavaScript supplies narrow
adapters for terminal presentation, clocks, entropy, optional persistence, and
broker transport. No C pointer may survive a call that can grow memory. The
control-page ABI should be preserved initially so the existing Pause, Resume,
Send Signal, and worker scheduling controls can drive either engine.

Retain `waste-libc.wasm` and the relinked `waste-runtime` module namespace. The
C engine first implements the same import contract currently exposed by OCaml;
moving a helper is complete only when its libc, DIY POSIX, and Bash behavior is
unchanged. `fork` initially copies linear memory and engine stacks; page-granular
copy-on-write replaces that operation behind one memory-clone interface later.

## Delivery Phases and Gates

1. **Foundation:** introduce directories, error/result types, bounded reader,
   arenas, native/wasm builds, sanitizers, and unit tests. Gate: decode malformed
   modules without crashes and match `wasm-tools validate` classifications for
   the selected core subset. Establish engine/sandbox/process/thread handle
   types and prohibit mutable objects at engine-global scope.
2. **Minimal executor:** constants, locals, numeric operations, structured
   control flow, direct/indirect calls, memory, globals, and imports. Gate: run
   `hello.wasm`, the smoke fixtures, and selected official core groups in both
   native and browser builds.
3. **Tail-call engine:** add reusable frames, `return_call`, and
   `return_call_ref`. Gate: official tail-call tests match the OCaml oracle and
   five million simple tail transfers complete without per-transfer allocation;
   publish instructions/second and elapsed browser time.
4. **WAT front end:** implement flat numeric grammar and separate-buffer
   two-pass encoder, then guarded in-place compaction. Gate: byte-equivalent or
   semantically equivalent output to `wasm-as` for generated fixtures and Bash.
5. **Harness adapter:** add an engine selector while retaining one
   `browser-tests.html`. Gate: identical JSON result schema, ordered groups,
   thread-count controls, timing, downloads, and static `file://` operation.
6. **POSIX migration:** move process, VFS, descriptors, pipes, signals, terminal,
   timers, and broker requests subsystem by subsystem. Gate: corresponding DIY
   POSIX and libc groups pass before changing the default owner.
7. **Bash cutover:** preassemble and instantiate Bash with the C engine. Gate:
   prompt, `echo`, `pwd`, `cd`, `ls`, signals, pipes, jobs, and exit pass, with
   startup and steady-state budgets recorded on named reference machines.
8. **Conformance and default switch:** expand WAT/WAST and proposal coverage,
   run the official core suite, fuzz decoders, and compare every supported test
   with OCaml. Make C the default only after the documented supported suite has
   no unexplained differential failures.

## Pre-Port Architecture Decisions

The C foundation should settle these points before opcode implementation:

- Freeze a versioned feature matrix for the MVP/core instructions and each
  proposal needed by Bash or the official tests. Generate opcode metadata used
  by the binary decoder, WAT lexer/parser, validator, disassembler, and executor
  from one checked-in definition so those paths cannot silently diverge.
- Put scheduler queues, pending tail transfers, active runner state, profiling
  counters, and host-import instances in explicit engine, sandbox, process, or
  thread objects. The cooperative OCaml implementation currently relies on
  several restorable module globals; copying that pattern would prevent
  reentrancy and later native parallelism. In particular, replace the existing
  C validator's static `module_tmp` scratch object with caller-owned context.
- Clone object graphs by identity. If two module instances import the same
  memory, table, global, or function, `fork` must create one corresponding clone
  and preserve both aliases. Cloning each module's arrays independently would
  accidentally split a shared address space.
- Represent guest linear memory as a refcounted backing object addressed by a
  handle and byte offset. It is data inside the engine's own Wasm memory, not a
  stable C pointer or necessarily a distinct JavaScript `WebAssembly.Memory`.
  Route `memory.grow`, imported-memory aliasing, host views, and later COW
  through this abstraction from the first executor milestone.
- Define process and thread signal state separately. Process-directed pending
  signals, thread-directed signals, per-thread masks, dispositions shared by
  the process, and delivery selection need distinct owners.
- Make host calls receive an explicit execution context. Imports must not find
  the active process through a mutable engine-global pointer, particularly once
  more than one native host thread can call the engine.
- Give stores and extern objects stable integer handles with generations or
  equivalent stale-handle detection. Document destruction order for sandboxes,
  processes, threads, modules, memories, wait objects, and broker requests.
- Preserve WebAssembly instantiation side effects and aliasing exactly within a
  sandbox, including failure paths. Test isolation occurs between scripts, not
  by resetting mutable state between commands in one script.
- Define `fork` in a multithreaded process before adding pthreads: the child
  receives only the calling thread, while process-owned address-space and
  descriptor semantics follow POSIX. `exec` later replaces that address space.
- Version the engine stop protocol now: completed, trapped, imported-call,
  blocked, signaled, paused, and out-of-fuel results need distinct payloads.
  Define deterministic resource limits and cleanup for every stop so malformed
  or cancelled browser work cannot leak partially instantiated stores.
- Keep scheduling deterministic for a fixed input, quantum, and event stream.
  Native parallel execution can be added later at sandbox boundaries without
  changing guest-visible process scheduling or result ordering.

## Test and Measurement Policy

Every engine change needs native tests, browser tests, or both. Native builds use
warnings-as-errors plus AddressSanitizer and UndefinedBehaviorSanitizer. Decoder
tests include truncated LEBs, oversized counts, invalid UTF-8, section-order
errors, bad indexes, type mismatches, and generated mutations. Browser timing
uses `performance.now()` and records wall-clock `Date` timestamps in downloaded
results.

Performance work must report the artifact, engine, browser, machine, test,
instruction count, allocation count, and elapsed time. Build time and test time
are reported separately. The native OCaml spec suite measures the oracle, not
the browser C engine. Optimize only profiles from the artifact being evaluated;
retain before/after result JSON for regressions.

Isolation tests must cover both sides of the ownership contract: modules in one
script that import the same host memory observe the same object, while scripts
scheduled concurrently receive fresh host memories, tables, globals, module
registries, and kernels. Run `data.wast` followed by `imports.wast` in one
engine invocation at concurrency one and greater than one; results must not
depend on order, quantum, or concurrency.

## Cutover and Rollback

During migration, generated pages can select `ocaml` or `c`, with OCaml as the
reference and C enabled only for its declared feature set. Unsupported C
features must fail explicitly or route the entire test to OCaml; never switch
engines in the middle of a guest process. Keep module namespace and control-page
versions explicit. A release can return to the OCaml artifact without changing
test definitions, libc fixtures, result downloads, or the static deployment
model.
