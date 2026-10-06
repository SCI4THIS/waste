# WASTE Architecture

## Purpose and Sources of Truth

WASTE runs WebAssembly applications and specification scripts in a native C
diagnostic runtime and in a self-contained browser runtime.  The repository-owned
C engine is the production direction.  The official OCaml interpreter in
`submodules/wasm-spec` is the differential oracle only for Wasm/WAT/WAST
language semantics: parsing, encoding/decoding, validation, linking,
instantiation, execution, traps and script assertions. Standard spec-test
imports and minimal language-test scaffolding remain in scope.

The OCaml application-engine experiment was not practical. No further OCaml
kernel, VFS, process, signal, scheduler, terminal, libc-host or broker
development is planned. The existing repository-added OCaml kernel and
application-runtime integration will be removed in deferred cleanup; see
[the OCaml scope and retirement plan](active-ocaml-language-oracle-plan.md).
C owns production application execution and kernel behavior. Missing OCaml
POSIX imports are outside oracle scope and do not block C kernel acceptance.

This document records durable system boundaries and ownership rules.  Concrete
unfinished work belongs in the active plans:

- [active-browser-vfs-layout-plan.md](active-browser-vfs-layout-plan.md)
- [active-ocaml-language-oracle-plan.md](active-ocaml-language-oracle-plan.md)

[techniques.md](techniques.md) records reusable implementation and testing
practices.  Dated pass counts, resolved failure lists, and build-history notes
belong in Git history or test result artifacts rather than this document.

## Repository and Artifact Layout

The current source boundaries are:

```text
src/engine/    platform-neutral parser, encoder, decoder, validator,
               instantiation, linker, runner, executor and guest POSIX ABI
src/cli-rt/    native CLI/session drivers, mmap harness, platform library,
               and Makefile
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
build/html-rt/  browser Wasm, bash.html, worker-test payloads, and libc fixtures
build/ocaml/    OCaml oracle builds and staging
```

`examples/bash.wat` is the compiled Bash input.  Guest-libc sources are under
`src/html-rt/lib/`, and browser packaging tools are under
`src/html-rt/tools/`.  Nothing under `build/` is a source of truth.

Authored browser assets are flattened under `src/html-rt/src/`: the shell/test
page uses `index.html`, `app.js`, `worker.js`, and `style.css`, with shared
`test-suite.js` for installed-corpus execution. Focused Node worker conformance
uses a JSON payload and `tests-worker.js`; it has no HTML frontend. Both use
`loader.js`; canonical terminal assets and notices live in `terminal/`. Guest
distribution snapshots live in `src/vfs`:
canonical commands in `usr/bin`, verified command copies in `bin`, and shared
libraries in `lib` with compatible `usr/lib` copies. The host-side
`.inventory.json` declares guest paths, ownership, modes, original mtimes,
hashes, aliases, source inputs and Wasm contracts. Inventory timestamps and empty
directories are authoritative even after a Git checkout changes host metadata.

Guest public headers are authored in `src/vfs/usr/include`, with selected,
licensed compiler-support snapshots in `usr/lib/waste/cc/include`. Sysroot and
guest libc builds consume this same mounted tree without host header fallback.
Private engine/native/libc headers stay beside their implementations; package
compatibility shims remain separate named build profiles. The SDK records its
audited function providers, signatures and unavailable capabilities; see
`guest-sdk.md` and Stage 4 of the active VFS plan.

`src/html-rt/tools/vfs.py install` explicitly publishes validated snapshots;
build tools compile under `build/` and support a separate `--install` step.
Packaging reads the installed inventory and preserves its directory tree in
standard tar. The browser's existing tarballjs extractor supplies file bytes;
`src/cli-rt/native_vfs.c` reads declared files directly beneath an open host
VFS directory. The shared engine validates inventory metadata and SHA-256
content hashes before mounting a complete catalogue into a fresh kernel.
Both the browser API and `waste-cli --vfs-root DIRECTORY FILE.wast` use this
inventory/file mounting contract. No custom filesystem bundle is generated.
The native option
establishes mount parity; the separate `waste-session` companion adds shared
guest imports and bounded child-first fork/exec terminal sessions through the
shared process driver. Both runtimes use the shared WAST child handler, retaining
assertions across READ/SELECT resumes and mapping failed child assertions to an
exit status without stopping the parent shell. See `native-guest-session.md`.
Host engine/worker assets and the host inventory are not guest VFS nodes.

`src/vfs/tests` holds explicitly installed snapshots of the WebAssembly corpus
and authored `tests/engine-regressions/*.wast`, with a mounted policy/provenance
manifest and companion assets. Sources remain in the pinned spec submodule,
top-level regression directories and generated libc fixture directory. The
corpus collector shares selection/grouping logic with the installer.
`waste-test`, the native batch companion, consumes `/tests/manifest.json` and
WAST/support bytes from an installed directory without Node or an HTML payload.
Its bounded manifest decoder
lives in `src/engine/test_suite.{c,h}`; native child isolation, signal handling,
deadlines and report files belong to `src/cli-rt/test_suite.c`. The CLI and batch
runner share native imports and assertion execution in `native_wast.c`, using
the engine's balanced command scanner and the shared process driver to attach
the active guest address space. Commands execute in source order; explicit
module assertions preserve the current instance, and register commands update
provider bindings immediately. Parsed definition/live-instance metadata is
retained while borrowed by the store; temporary assertion parses are released
after execution. Native
children each construct a fresh store/kernel and retain intentional aliases
within their own script. Unsupported execution modes remain explicit skips;
reports preserve assertion records and expected-failure distinctions.
The native WAST adapter resumes finite kernel SELECT deadlines against a host
monotonic clock through the shared process driver, preserving pending assertion
arguments/results. Clock/sleep syscalls stay in cli-rt; terminal-input,
indefinite and other externally driven waits require the full session adapter.
The offline shell's `test-suite.js` controller runs this same installed corpus
through fresh production `worker.js` instances. Browser API exports decode the
mounted manifest with the shared decoder, read WAST/companions through temporary
catalogue kernels, and remount the validated installed files for execution. JavaScript
owns worker scheduling, watchdogs and cancellation; assertion execution remains
in C. The live shell has its own instance. Browser expected-failure policy is a
tracked host package asset, separate from the guest inventory. Both batch paths
retain manifest order and distinguish infrastructure failures from XFAIL.
The installed guest batch launcher uses the runtime capability. Coverage
accounting, compatibility fixtures and supported WebAssembly-language oracle
comparisons remain pending. See `test-corpus.md` for execution, refresh and mount verification, and
`test-coverage.md` for assertion-level C/WAST mappings and retained private gates.

Focused worker-test metadata lives in `build/html-rt/tests/payload.json` and
the Bash page bootstrap inputs live in
`build/html-rt/{waste-wast.wasm,bash-runtime.wast}`. The one browser page,
`bash.html`, includes the installed corpus diagnostic runner. Packaging reads
authored frontend files plus these generated inputs; no standalone test
dashboard is generated.

## Runtime Roles

The same platform-neutral C engine is compiled for both native and browser
targets.  Platform adapters differ, but WebAssembly decoding, validation,
instantiation, execution, WAT/WAST behavior, and module linking must not.

The native runtime provides fast diagnostics, mmap input, warnings-as-errors
builds, and sanitizer coverage.  The browser runtime exposes the engine through
a narrow exported C API and packages the engine, tests, and applications into
single offline HTML files.  A behavior is not complete until the relevant
native and browser paths agree.

`src/engine/guest_posix.c` owns guest ABI decoding, legacy import selection,
errno conversion and operations on the store-owned kernel/process state.
Its private header is not a mounted application SDK header. A borrowed immutable
capability table and per-store context connect it to platform operations;
missing callbacks return unsupported errors, not implicit host access.
`src/html-rt/posix_stubs.c` owns browser imports, wall-clock conversion and
upload/download dialogs. Native polling, clocks and transcript output live in
`src/cli-rt/guest_session.c`; guest paths are never forwarded to host syscalls.
Both runtimes resume explicitly saved evaluator state through ordinary C calls.

`src/engine/process_driver.{c,h}` owns reusable fork/exec/child selection and
parent/provider continuation restoration policy, with mutable state in a
per-session record. Native polling and browser event delivery remain in runtime
adapters. Borrowed trace/command-stream hooks do not grant host capabilities.
Capsules retain an explicit owner for fork root-engine clones across image or
handler replacement, releasing them during reaping or store teardown.

The OCaml interpreter supplies the official WebAssembly language oracle. Its
existing POSIX/application artifacts are remnants of the earlier experiment,
not a production fallback or a kernel reference to develop further. Historical
comparisons do not create an ongoing OCaml kernel parity requirement.

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
hints. These suites run through both the native C runner and the browser C
artifact. The browser diagnostic runner reads the mounted manifest directly;
focused Node worker tests use the same corpus identities through JSON metadata.

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

Each WAST file receives a fresh sandbox even when the browser test runner schedules
several files concurrently. Browser concurrency controls runnable
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
That status propagates through ordinary C returns until control reaches the
host. Resume reenters the saved execution from engine-owned frames. The active
POSIX plan generalizes the current input-oriented yield into scheduler-owned
wait records.

### No Asyncify invariant

The C engine must run without Asyncify. Its Wasm binary is not passed through
an Asyncify transform and must not import or export Asyncify start/stop,
unwind, or rewind hooks. No Emscripten/Binaryen Asyncify flags belong in its
build. "Yield," "continuation," and "resume" in this repository refer only to
explicit engine data structures and status propagation; they do not refer to
capturing or rewriting the browser Wasm call stack.

At a blocking import, the interpreter records the guest PC, operand/control
stacks, locals, call frames, and typed wait reason in engine-owned memory. It
then returns `EXEC_YIELD` normally through each C caller and back to the worker.
After the browser event arrives, the worker calls the explicit resume export,
which reenters the interpreter and consumes the saved state. Process switching
uses the same explicit model with owned evaluator snapshots and store
checkpoints. Introducing Asyncify would duplicate that state machine and is an
architecture regression.

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
Asyncify transform is used or permitted for the self-contained `file://`
pages.

Integer handles cross the JavaScript boundary.  C pointers must not be exposed
as durable browser identities, and no pointer into a growable memory may
survive a call capable of growing that memory.

### Browser control channels

The legacy OCaml scheduled runtime has a version 1 control-page ABI. This
records the existing interface for deferred retirement; it is not a required
C control ABI or a target for further OCaml scheduler/signal development.
The page is an array of 32-bit words:

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
frames through exported functions. New C control APIs use versioned
integer-handle messages appropriate to the C scheduler. Verify observable
pause, signal, termination and scheduling behavior with the shared C session
contracts and native/browser parity, rather than OCaml control-page parity.

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

Every operation on an engine-owned descriptor, including `open`, `close`,
`read`, `write`, and duplication, resolves through the active process kernel.
Browser host shims are only a fallback for runtimes without that kernel. Mixing
the two paths would leave kernel reference and pipe-endpoint counts stale; in
particular, a pipe reader would never observe EOF after a host-only `close`.
The process kernel also owns the file-creation mask, initialized to `0022`,
inherited by `fork`, and replaced independently by `umask`. Creation applies
that mask to the caller-supplied mode for ordinary and shared-memory files.
Variadic argument decoding remains in guest libc: its `open` wrapper extracts
the optional mode from the calling image and passes a fixed scalar to the
versioned kernel boundary.

The VFS is a rooted in-memory namespace. Directory iteration synthesizes `.`
for the opened directory and `..` for its parent; the root's parent is root
itself. These are iterator records with the corresponding directory metadata,
not stored pathname nodes. Path metadata carries modification seconds and
nanoseconds. Packaged files receive their source artifact's mtime from a
separate manifest while tar headers remain normalized; runtime create,
truncate, and write operations use the kernel's injected UTC epoch clock.
Namespace nodes synthesized by the engine (`/`, `/bin`, `/usr`, `/usr/bin`,
`/bin/wat`, and `/bin/wast`) receive the browser engine image's build mtime,
which the launcher stages separately from guest package entries.
The browser backend supplies that clock from `Date.now()`, while the engine
retains no JavaScript dependency. Persistent storage, if enabled, is a mount
backend rather than a replacement namespace in JavaScript. The terminal retains
controlling-session, foreground-process-group, termios, and job-control state
in the kernel; JavaScript renders output and delivers input events.

The offline Bash page follows the non-persistent default: each worker start
creates a new sandbox kernel, imports a read-only copy of the embedded package
manifest, and owns a fresh writable overlay (including `/tmp`). Restarting the
shell therefore discards guest-created files and descriptor state while
leaving the page's packaged blobs unchanged. A future persistence backend must
be an explicit mount capability; it must not be implemented by retaining VFS
state in the page or worker protocol implicitly.

`fork` clones the address-space object graph while preserving identity.  If two
module instances alias one imported memory or table before the fork, their
clones must still alias one corresponding child object.  Open-file descriptions
remain shared between parent and child.  When guest threads are added, a forked
child begins with only the calling thread.  Page-granular copy-on-write may
replace eager copying behind one memory-clone boundary without changing these
semantics.

The legacy OCaml artifacts still contain a process/VFS/signal kernel used by
older scheduled POSIX probes. That kernel is outside the language oracle
scope and is planned for removal, not further development. The C browser
runtime owns the bounded child-first fork/failed-`execve`/exit/`waitpid` continuation used by the Bash
command-not-found path, including evaluator snapshots and store checkpoints.
It still does not provide general concurrent process or guest-thread
scheduling; remaining C capabilities belong to the active runtime plans.
Their acceptance uses C native/browser contracts, not an obligation to mirror
or extend the OCaml kernel. Legacy behavior alone does not prove a C capability.

### Engine-owned virtual memory

The C engine owns each process's virtual address-space graph. A process
capsule records its virtual regions, page mappings, protections, file-backed
ownership, and virtual-page limit; threads in that process share the capsule.
Independent processes have separate address-space graphs. A shared mapping or
named shared-memory object is the explicit exception and points at
reference-counted backing pages owned by the kernel/VFS layer.

Module instances that belong to one process may import the process's single
`exec_memory`, so application code, guest libc, allocator state, errno, and
stdio use one guest pointer space. `fork` preserves shared-page identity and
private-page copy-on-write state; `execve` validates and constructs a new
image graph before replacing the old one. Checkpoints preserve page,
protection, mapping, and alias topology rather than flattening memory into a
host buffer.

All engine and runtime accesses use bounded memory operations. A missing page
is not the same as an unmapped region, and a mapped `PROT_NONE` page is
reserved but inaccessible. Mapping, unmapping, protection changes, and
process-image replacement commit metadata only after validation succeeds. The
browser runtime follows these same rules and does not use host `mmap`,
`SharedArrayBuffer`, or a JavaScript-owned process address space.

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

Official Wasm/WAT/WAST language tests are compared with the OCaml oracle.
POSIX imports in a WAST client do not make its kernel behavior part of that
oracle. Kernel, libc and application behavior uses C native/browser parity,
private sanitizer gates and compiled guest ABI checks. Local DIY POSIX and
libc tests are regression probes, not formal POSIX certification.
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

The Bash and installed-test interface share one offline HTML document.
Optional broker use does not change the packaging requirement: opening the
page through `file://` must work without a server when broker-backed features
are not requested.

## Process Capsules and Browser Resumption

Each native process owns a capsule containing its current engine/image,
invocation descriptor, mutable continuation, linked-provider continuations,
pending fork result, and lifecycle state.  The store owns capsule selection,
fork cloning, executable-image replacement, child exit, wakeup, and zombie
reaping.  Browser code may cache only a bounded driver record identifying the
currently selected PID, entry invocation, and genuine browser wait reason.

Fork captures the parent descriptor and continuation before selecting the child.
Successful `execve` commits a new image to the child and eagerly restores the
parent capsule; child exit then selects and resumes that already-restored
parent.  Failed `execve` resumes the old child continuation once with its
one-shot errno.  JavaScript sees only terminal/select/input waits: internal
fork, exec, exit, wake, and process-selection transitions never become browser
callbacks.  This design does not use Asyncify, JSPI, native stack copying, or
`setjmp`/`longjmp`.
