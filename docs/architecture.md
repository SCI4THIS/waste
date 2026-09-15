# WASTE Architecture

## Purpose and Sources of Truth

WASTE runs WebAssembly applications and specification scripts in a native C
diagnostic runtime and in a self-contained browser runtime.  The repository-owned
C engine is the production direction.  The official OCaml interpreter in
`submodules/wasm-spec` remains the differential oracle during migration; it is
not part of the C engine's instruction-execution path.

This document records durable system boundaries and ownership rules.  Concrete
unfinished work belongs in the active plans:

- [active-c-engine-select-pselect-plan.md](active-c-engine-select-pselect-plan.md)

[techniques.md](techniques.md) records reusable implementation and testing
practices.  Dated pass counts, resolved failure lists, and build-history notes
belong in Git history or test result artifacts rather than this document.

## Repository and Artifact Layout

The current source boundaries are:

```text
src/engine/    platform-neutral parser, encoder, decoder, validator,
               instantiation, linker, runner, and executor
src/cli-rt/    native CLI, mmap harness, native platform library, and Makefile
src/html-rt/   browser API, browser POSIX adapter, guest libc, HTML tools,
               and Wasm Makefile
```

The engine itself has one-way ownership directories:

```text
src/engine/include/  single waste.h header: values, errors, and opaque handles
src/engine/wat/      reentrant Flex/Bison WAT front end and text AST builder
src/engine/wast/     WAST command framing, parsing policy, and assertions
src/engine/wasm/     binary reader/writer, encoder, decoder, and loading
src/engine/op/       frame-based dispatch, opcode-family execution, and validation
src/engine/          store, instances, and instantiation (engine root)
src/engine/lib/      shared freestanding C support
```

Only `Makefile`, `README.md`, and the support directories live at the engine
root. Generated Flex/Bison files remain under `build/engine/gen/`.

Generated files stay under `build/`:

```text
build/engine/   shared generated parser sources, toolchain, and logs
build/cli-rt/   native executables, including waste-cli
build/html-rt/  browser Wasm, test.html, bash.html, and libc fixtures
build/ocaml/    OCaml oracle builds and staging
```

`examples/bash.wat` is the compiled Bash input.  Guest-libc sources are under
`src/html-rt/lib/`, and browser packaging tools are under
`src/html-rt/tools/`.  Nothing under `build/` is a source of truth.

## Runtime Roles

The same platform-neutral C engine is compiled for both native and browser
targets.  Platform adapters differ, but WebAssembly decoding, validation,
instantiation, execution, WAT/WAST behavior, and module linking must not.

The native runtime provides fast diagnostics, mmap input, warnings-as-errors
builds, and sanitizer coverage.  The browser runtime exposes the engine through
a narrow exported C API and packages the engine, tests, and applications into
single offline HTML files.  A behavior is not complete until the relevant
native and browser paths agree.

The OCaml runtime supplies three things during migration:

- the official WebAssembly language oracle;
- a behavioral comparison for POSIX subsystems already implemented there; and
- a rollback runtime while the C implementation is incomplete.

An executing application never switches engines midway through a process.

## Input and Module Pipeline

The architecture keeps text, binary, validation, instantiation, and execution
as distinct phases:

```text
WAT -- Flex/Bison --> text module -- encoder --> Wasm bytes
                                                   |
Wasm bytes ---------------------> bounded decoder --+--> immutable wasm_module
                                                           |
                                                       validate
                                                           |
                                                       instantiate
                                                           |
                                                        execute

WAST --> Flex boundary mode --> parse one command --> execute/classify --> next
```

The binary decoder produces an owned module with a private source copy,
section directory, and decoded imports. The binary load layer decodes the
remaining executable declarations and validation metadata through bounded
section readers. Instantiation creates mutable memories, tables, globals,
segments, objects, tags, and evaluator state. Decoding the same module once
and instantiating it twice must create isolated mutable state.

Text modules pass through the encoder before the common binary module path.
Neither the linker nor the runtime may independently rescan Wasm sections
already owned by the decoder.

## WAT and WAST Boundaries

WAT and WAST use the same lexical and module grammar rules, but their drivers
have different failure policy:

- WAT compilation accepts exactly one module, is transactional, and fails
  without returning partial output.
- WAST is a command stream.  A dedicated mode of the same reentrant Flex
  scanner frames one balanced top-level command.  The driver parses that exact
  range in a fresh context, executes or classifies it, records its result, and
  continues.

Malformed text, invalid modules, failed instantiation, failed linking, traps,
exceptions, exhaustion, and successful values are different observable WAST
outcomes.  Parser recovery must not collapse them into a generic failure or
skip later commands.

Flex/Bison owns textual syntax and source-boundary recognition.  Binary
decoding, module validation, assertion policy, linking, instantiation, and
execution remain C phases outside grammar actions.  Assertion value matching
and action execution are isolated in `wast/assert.c`; command policy
lives in `wast/`, while registry and lifetime transitions live in
`store.c`.

## Language and Harness Coverage

The maintained C-engine regression surface includes the official top-level
core tests and the configured bulk-memory, exception handling, GC, memory64,
multi-memory, relaxed-SIMD, and SIMD proposal suites.  It also includes the
repository's custom annotation coverage for custom sections, names, and branch
hints.  These suites run through both the native C runner and the browser C
artifact; the generated browser dashboard follows the OCaml dashboard's group
layout.

This coverage statement describes the configured repository revision, not all
future WebAssembly proposals.  The generated test results are authoritative if
a submodule update changes the corpus.  DIY POSIX and libc groups are separate
runtime regression suites and must not be counted as official WebAssembly
language conformance.

## Ownership Hierarchy

Scheduler tasks, POSIX processes, guest threads, and test sandboxes are not
interchangeable.  Mutable state has four ownership levels:

1. **Engine:** immutable decoded modules, opcode metadata, and optional code
   caches.  Shared engine data must not expose mutable guest state.
2. **Sandbox/store:** one independently launched WAST file or application.  It
   owns its module registry, host-import environment, mutable module instances,
   retained definitions, and POSIX kernel namespace.
3. **Process:** a member of a sandbox kernel with an address space, descriptor
   table, lifecycle, process group, session, current directory, and
   process-directed signals.
4. **Thread:** an execution context with PC, operand/control/call stacks,
   locals, signal mask, and thread-directed pending signals.  Threads share
   their process's address space and descriptor table.

Each WAST file receives a fresh sandbox even when the dashboard schedules
several files concurrently.  Dashboard concurrency controls runnable
sandboxes; it does not create guest threads.

Explicit Wasm imports may alias a memory, table, global, tag, or function
between module instances in one sandbox.  That aliasing is observable and must
survive linking and process cloning.  It does not authorize accidental state
sharing between independent sandboxes.

## Module Linking and Lifetime

The store resolves decoded import declarations against registered Wasm module
exports, standard `spectest` values, or an explicit platform host resolver.
Imports are compared structurally, including reference types whose numeric
type indexes belong to different modules.

Linked calls retain both the provider engine and provider-local function
index.  Imported memories, tables, globals, and tags reference their provider
objects rather than copies.  Providers must outlive all consuming instances,
including instances retained after failed WAST assertions where the language
requires observable side effects.

Every independently instantiated module receives new mutable state.  Decoded
source ownership is separate from instance ownership so a source buffer may be
released or cached without invalidating an instance.

## Execution and Stop Model

Function bodies are decoded into engine instructions and executed by a bounded
frame-based interpreter.  Normal calls create frames.  Proper tail calls
replace the active target, arguments, locals, and PC while preserving the
original return continuation; they must not allocate per tail transfer.

The engine uses explicit statuses for normal completion, format or validation
failure, unsupported behavior, traps, exceptions, exhaustion, process exit,
non-local control transfer, and yield.  These outcomes must remain distinct
through imported and cross-instance calls so WAST assertions can classify
them correctly.

Guest `sigsetjmp` and `siglongjmp` do not use native C `setjmp` or `longjmp`.
The engine records guest-visible jump environments as evaluator snapshots and
performs non-local transfer through ordinary C returns.  Exception handling
likewise uses an explicit carrier containing runtime tag identity, payload,
owner, and exception reference.

Blocking work saves interpreter state and returns a yield status to the host.
Resume reenters the saved execution without Asyncify.  The active POSIX plan
generalizes the current input-oriented yield into scheduler-owned wait records.

## Browser Runtime

The C browser artifact contains the WAST command scanner, Flex/Bison parser,
encoder, binary pipeline, linker, runner, executor, platform adapter, and
browser API.  Generated pages embed original WAST sources and call the same
streaming WAST entry point used by the C runtime; native preprocessing is not a
substitute for browser parsing.

The browser worker owns presentation and event delivery.  It may supply clocks,
entropy, terminal bytes, persistence callbacks, or optional broker messages.
It must not own guest module state, descriptor identity, process state, errno,
or WebAssembly semantics.

The worker may return to its event loop when the C engine yields.  It later
delivers input or another event and calls the explicit resume export.  No
server, external asset, `SharedArrayBuffer`, cross-origin-isolation header, or
Asyncify transform is required for the self-contained `file://` pages.

Integer handles cross the JavaScript boundary.  C pointers must not be exposed
as durable browser identities, and no pointer into a growable memory may
survive a call capable of growing that memory.

### Browser control channels

The OCaml scheduled runtime retains its version 1 control-page ABI as a
behavioral and compatibility reference.  The page is an array of 32-bit words:

| Word | Purpose |
| ---: | --- |
| 0 | Published command sequence |
| 1 | Pause state (`0` running, `1` paused) |
| 2 | Reserved runtime status |
| 3 | ABI version (`1`) |
| 4–259 | 256-word command ring |

A command packs a 16-bit operation and 16-bit argument.  Operations are pause,
POSIX signal, and termination.  The producer writes a ring entry before
publishing its sequence.  A single-worker `file://` page can alternate producer
and consumer work without shared memory; external controllers may use the same
layout in a `SharedArrayBuffer` with atomic publication.

The C runtime does not depend on the OCaml control-page representation.  It
currently returns explicit exit or yield statuses and resumes saved evaluator
frames through exported functions.  New C control APIs must preserve the same
observable pause, signal, termination, and scheduling behavior while using
versioned integer-handle messages appropriate to the C scheduler.

## POSIX Capability Model

Browser limitations define capability boundaries, not permission to move OS
semantics into JavaScript.  Facilities belong to one of three tiers:

1. **Browser-backed:** narrow primitives that the browser can supply faithfully,
   such as clocks, cryptographic entropy, terminal presentation, and optional
   persistence storage.
2. **Engine-emulated:** process, signal, timer, VFS, pipe, descriptor, terminal,
   readiness, and scheduling semantics implemented in the active runtime's
   kernel.
3. **Broker-backed:** operations the browser security model prevents, such as
   raw sockets, delegated through an optional WebSocket POSIX broker.

The broker is a capability transport, not a kernel.  The engine retains
descriptor identity, permissions, blocking state, readiness, cancellation,
signal interruption, and errno translation.  A broker protocol must be
versioned, authenticated, capability-scoped, and explicit about request IDs,
typed arguments, results, errno, cancellation, and readiness notifications.
Without an enabled capability, the guest receives an appropriate unsupported
error.

### Kernel object model

Each sandbox kernel owns a process table, VFS root, controlling terminal, PID
and inode allocation, and monotonic clock.  A process owns its PID, parent,
process group, session, lifecycle, current directory, descriptor table, signal
state, and timers.  A thread owns evaluator and signal-mask state within that
process.

Descriptor entries refer to shared open-file descriptions.  Consequently,
`dup`, `dup2`, inherited descriptors, file offsets, status flags, and pipe
endpoint counts remain shared where POSIX requires.  Pipes have bounded buffers
and readiness waiters.  Empty reads, full writes, child waits, stopped
processes, and deadlines leave the runnable queue instead of spinning through
instruction quanta.  An unblocked signal wakes an affected waiter so the
interrupted operation can return the correct result.

The VFS is a rooted in-memory namespace.  Persistent storage, if enabled, is a
mount backend rather than a replacement namespace in JavaScript.  The terminal
retains controlling-session, foreground-process-group, termios, and job-control
state in the kernel; JavaScript renders output and delivers input events.

`fork` clones the address-space object graph while preserving identity.  If two
module instances alias one imported memory or table before the fork, their
clones must still alias one corresponding child object.  Open-file descriptions
remain shared between parent and child.  When guest threads are added, a forked
child begins with only the calling thread.  Page-granular copy-on-write may
replace eager copying behind one memory-clone boundary without changing these
semantics.

The OCaml oracle currently owns the mature process/VFS/signal kernel used by
its scheduled POSIX probes.  The C browser runtime currently has partial host
adapters and explicit yield/resume for terminal reads; a complete C kernel is
still being migrated.  Do not describe OCaml kernel behavior as already owned
by the C runtime.  The `select`/`pselect` migration is specified in the active
plan rather than duplicated here.

## Guest Libc

`waste-libc.wasm` is built from `src/html-rt/lib/stdlib.wat` and the focused C
sources beside it.  It owns and exports guest linear memory, allocator state,
stdio objects, errno storage, string and conversion helpers, locale and wide
character support, identity databases, patterns, time/resource helpers, and
terminal or environment boundary functions.

Applications and libc are relinked to a neutral `waste-runtime` memory and
table owner.  Registering libc under the expected namespace supplies pure
libc exports, while unresolved process, descriptor, VFS, signal, clock, and
broker operations pass through the active runtime's kernel boundary.

Guest errno lives in guest memory.  Host adapters return explicit results or
error numbers and must never depend on the build host's global errno.  Calls
that require unavailable capabilities fail explicitly rather than pretending
to succeed.

Generated libc fixtures instantiate client modules against libc's exported
memory and table.  This is an ABI test as well as a functional test: pointers,
callbacks, allocator metadata, and errno must be observed through the actual
cross-module aliases.

## Testing, Measurement, and Deployment Policy

Every shared engine change needs proportional native and browser verification.
Native engine tests use warnings as errors, AddressSanitizer, and
UndefinedBehaviorSanitizer.  LeakSanitizer may remain disabled only for the
documented ptrace environment.  Binary tests cover truncation, malformed LEBs,
overflow, invalid UTF-8, ordering, duplicate sections, bad indexes, and type
mismatches.

Official WebAssembly tests are compared with the OCaml oracle.  Local DIY
POSIX and libc tests are regression probes, not formal POSIX certification.
Independent scripts must be tested in different orders and at different
dashboard concurrency settings to expose unintended global state.

Formal POSIX claims require an applicable licensed Open Group suite.  The Linux
Test Project's `testcases/open_posix_testsuite` is an open development baseline,
not certification.  If it is added later, pin its upstream revision, keep its
licensing and Wasm adaptation patches separate, preserve upstream assertion
identities, and classify results as emulated, browser-backed, broker-backed,
unsupported, or failed.

Browser results refer to the actual browser C artifact.  Native OCaml timings
must not be presented as browser speedups.  Performance reports identify the
artifact, engine, browser, machine, workload, instruction or allocation count,
and elapsed time; build and execution time are separate measurements.

The browser dashboard and Bash page remain single offline HTML documents.
Optional broker use does not change the packaging requirement: opening the
page through `file://` must work without a server when broker-backed features
are not requested.
