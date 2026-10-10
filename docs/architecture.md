# WASTE Architecture

## Purpose and References

WASTE runs WebAssembly applications and specification scripts in a native C
diagnostic runtime and in a self-contained browser runtime. The repository-owned
C engine is the production runtime. The OCaml reference interpreter in
`submodules/wasm-spec/interpreter` was used as a language reference while
implementing the WAT/WAST portions of the C engine. See
[OCaml reference interpreter build](techniques.md#ocaml-reference-interpreter-build) for the minimal
build/test instructions.

This document records durable system boundaries and ownership rules.
[techniques.md](techniques.md) records reusable implementation and testing
practices.  Dated pass counts, resolved failure lists, and build-history notes
belong in Git history or test result artifacts rather than this document.

Native `wat` and `wasm` launch applications directly; `wast` runs complete
specification scripts and standalone package tests. Applications inherit host
stdio through bounded native capabilities, with engine-owned foreground
signals and terminal processing. Private companions retain scripted session
controls and isolated batch scheduling; public builds produce `wat`, `wasm`
and `wast`.

## Repository and Artifact Layout

The current source boundaries are:

```text
src/aux/       guest auxiliary commands, package libraries and build targets
src/engine/    platform-neutral parser, encoder, decoder, validator,
               instantiation, linker, runner, executor and guest POSIX ABI
src/cli-rt/    native CLI/session drivers, mmap harness, platform library,
               and Makefile
src/aux/libc/  guest libc.so.wasm, guest CRT, private interpreter support,
               package WAST tests, shared-import tools and Make rules
src/system-tests/  system interaction WAST and compiler SDK probes
src/html-rt/   browser API, browser POSIX adapter, HTML tools,
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
build/engine/           shared generated parser sources, toolchain, and logs
build/aux/              auxiliary guest binaries, staged sources and configuration
build/cli-rt/           native wat/wasm/wast and private test companions
build/html-rt/          browser Wasm, bash.html and worker-test payloads
build/ocaml-interpreter/  staged OCaml reference interpreter (optional)
```

Checked-out submodules are read-only source dependencies. Build tools must copy
or stage their inputs under `build/` before applying repository patches or
running generators, bootstrap/configure steps, and compilers. All such outputs,
including test-runner output, belong under `build/`; see
[the submodule policy](submodule-policy.md) for existing helpers that still
need this refactor.

`src/aux/bash` builds the read-only `submodules/bash` sources into
`build/aux/bash/bash.wasm`, explicitly installed as `/usr/bin/bash`. The browser
and compatibility harness use the authored
`src/aux/bash/launch.wast`, installed at `/usr/share/waste/launch.wast`, to
replace the initial process with that binary through `execve`. Native `wasm`
loads it directly through the same process/library initialization path. Bash
uses the shared guest CRT/libc and engine kernel imports;
libc owns the default root account/hostname records and the CRT binds its stream
accessors. There is no embedded executable or WAT-rewriting build path. Guest-libc sources
are under `src/aux/libc/`, and browser packaging tools are under
`src/html-rt/tools/`. Nothing under `build/` is a source of truth.

Authored browser assets are flattened under `src/html-rt/src/`: the shell/test
page uses `index.html`, `app.js`, `worker.js`, and `style.css`, with shared
`test-suite.js` for installed-corpus execution. Focused Node worker conformance
uses a JSON payload and `tests-worker.js`; it has no HTML frontend. Both use
`loader.js`; canonical terminal assets and notices live in `terminal/`. Guest
distribution files live in `src/vfs`: commands in `usr/bin` and `bin`, and shared
libraries in `lib` and `usr/lib`. The current directory tree is authoritative;
adding, editing or deleting files needs no inventory refresh or source approval.
Mounting uses current host modes and mtimes, guest root ownership, and inode IDs
derived from sorted paths. Missing `/bin`, `/root` and `/tmp` are synthesized,
as are `/bin/wat` and `/bin/wast` interpreter nodes when absent.

Guest public headers are authored in `src/vfs/usr/include`, with selected,
licensed compiler-support snapshots in `usr/lib/waste/cc/include`. Sysroot and
guest libc builds consume this same mounted tree without host header fallback.
Private engine/native/libc headers stay beside their implementations; package
compatibility shims remain separate named build profiles. Coreutils owns its
private headers and shared Make rules in `src/aux/coreutils`; per-command
aux directories select upstream objects, and generated source and binaries
live under `build/aux`. SDK origin metadata
is informational; explicit checks inspect current declarations, provider
signatures and unavailable capabilities. See `guest-sdk.md`.

`src/html-rt/tools/vfs.py install` is an explicit atomic copy/publish utility;
build tools compile under `build/` and support a separate install step.
Packaging discovers the current tree, preserves it in standard tar and generates
`vfs-manifest.json` in the package. The browser's existing tarballjs extractor
supplies file bytes; the shared engine checks generated transport metadata and
hashes before mounting. `src/cli-rt/native_vfs.c` discovers and reads current files
directly beneath an open host directory, building the same bounded catalogue.
Both paths enforce canonical paths, regular files/directories, entry/byte limits
and read consistency. Hashes verify a generated package's integrity; they do not
compare edits against a stored approval ledger. Wasm validation occurs on load;
explicit installers and ABI tests also validate their candidate modules.
No custom filesystem bundle is generated.
The native option
establishes mount parity; the private `guest-session` companion adds shared
guest imports and concurrent fork/exec terminal sessions through the
shared process driver. Both runtimes use the shared WAST child handler, retaining
assertions across READ/SELECT/HOST_IO/PUMP resumes and mapping failed child assertions to an
exit status without stopping the parent shell. See `native-guest-session.md`.
Host engine assets and generated transport metadata are not guest VFS nodes.

`src/vfs/root/test` holds explicitly installed snapshots of the WebAssembly corpus
and authored `tests/engine-regressions/*.wast`, with a mounted policy/provenance
manifest and companion assets. Sources remain in the pinned spec submodule,
top-level regression directories, package `src/aux/NAME/tests` directories and
`src/system-tests`. The
corpus collector shares selection/grouping logic with the installer.
`wast --suite` delegates scheduling to the private `test-suite` companion, which
consumes `/root/test/manifest.json` and
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
The public native `wast` and batch companion use the reusable native runtime
for invocation, explicit yields, finite SELECT deadlines and monotonic clocks.
Language scripts begin with empty descriptors. Production-context scripts
receive terminal descriptors and can resume READ/SELECT through native stdin;
unsupported external waits in language context fail explicitly. Clock/sleep
syscalls and host polling stay in cli-rt. Nested host batch launching is disabled
in standalone scripts and batch children.

Public `wasm`/`wat` applications distinguish inherited TTYs, pipes and files,
and preserve their access directions. The kernel owns bounded input queues,
descriptor aliases, EOF and terminal state. OFDs carry opaque input/output
capability IDs; only `src/cli-rt/native_terminal.c` resolves these to private
duplicates of inherited host stdio. Closing or replacing a guest fd cannot
redirect a surviving alias to an unrelated host descriptor. Shared process
selection recognizes stream/terminal aliases beyond fd 0.

The native adapter captures/restores host terminal modes and signal handlers,
transports raw TTY bytes and queries real dimensions. Guest termios/Readline and
kernel output processing supply terminal behavior. Pipe/file stdio transfers
raw bytes; inherited regular files retain their type but are stream endpoints
without seeking or complete host stat metadata. This does not extend guest VFS
paths into host filesystem access. Private session and production WAST contexts
retain modeled terminals for deterministic tests. Host signals enqueue guest
foreground-group signals; they are distinct from timeout and harness cancellation.
Default termination is checked at evaluator checkpoints and wait resumption,
with POSIX signal wait status and descriptor cleanup before the parent resumes.
Native pump checkpoints poll terminal input so Ctrl-C can interrupt a CPU-bound
child. Canonical input, erase/kill and echo belong to the shared kernel; native
and browser adapters transport the resulting bytes.

Caught input-wait signals are engine-owned. A blocking read consumes one
unmasked pending signal; the guest adapter invokes its handler before returning
EINTR or re-registering a READ with SA_RESTART. SELECT/pselect invoke the same
handler path and return EINTR. Compiled C actions carry a shared-table slot,
action mask and flags; table resolution selects the process-owned function
instance and validates its signature. Legacy table-free WAT probes retain their
local function-index/action-prefix ABI. Callback mask/scope restoration also
covers traps, exits and guest longjmp. Handlers execute synchronously, suppress
browser pump yields and reject blocking calls rather than publishing a second
suspended callback. Unblocked self-signals run before kill/raise returns, which
allows Readline to forward a signal to the temporarily restored Bash handler.
Caught asynchronous handlers run at input waits; default termination also runs
at evaluator checkpoints. Exec resets caught actions while preserving ignored
actions, masks and pending signals. Default notification signals are ignored;
stop/continue job scheduling remains unsupported.

Standalone production context comes from the shared C process driver: it
creates the memory/table provider, loads mounted `/usr/lib/libc.so.wasm`, and
uses the production startup block and libc initialization without starting an
application. Imported minima determine resource sizes within shared limits.
Context setup clones the mounted kernel, attaches terminal descriptors in place,
and rolls back providers and kernel state on failure. Auto selection scans the
whole script before execution. Ordinary imports from `libc` or `waste-runtime`
request that context; script registrations of either provider or module
assertions using them preserve a language namespace for the entire file.
Explicit `--context runtime` rejects those conflicts; `language` suppresses
implicit providers. Every input file owns a fresh store/kernel. JSON assertion
reports retain the existing schema, and guest writes go to stderr in JSON mode
so stdout remains machine readable.
The offline shell's `test-suite.js` controller runs this same installed corpus
through fresh production `worker.js` instances. Browser API exports decode the
mounted manifest with the shared decoder, read WAST/companions through temporary
catalogue kernels, and remount the validated installed files for execution. JavaScript
owns worker scheduling, watchdogs and cancellation; assertion execution remains
in C. The live shell has its own instance. Browser expected-failure policy is a
tracked host package asset, separate from the guest inventory. Both batch paths
retain manifest order and distinguish infrastructure failures from XFAIL.
The installed guest batch launcher uses the runtime capability. See
[installed corpus workflow](techniques.md#installed-corpus-workflow) for refresh,
execution and mount checks, and [test boundary selection](techniques.md#test-boundary-selection)
for the contracts that require direct C or host tests.

Focused worker-test metadata lives in `build/html-rt/tests/payload.json` and
the Bash page consumes `build/html-rt/waste-wast.wasm` and the installed
launcher and executable from `src/vfs`. The one browser page,
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
`src/html-rt/posix_stubs.c` owns browser imports and wall-clock conversion.
Upload/download requests yield through `EXEC_YIELD_HOST_IO`, with pending state
in `native_store.host_io`. `browser_api.c` exposes the request to `worker.js`;
`app.js` owns the browser file picker, FileReader and Blob download. The engine
copies uploaded bytes into owned storage before the worker frees its temporary
buffer, then commits the file to the guest VFS on resume. Native polling,
clocks, execution and output live in `src/cli-rt/native_runtime.c`;
`guest_session.c` supplies the private compatibility harness CLI. Guest paths
are never forwarded to host syscalls.
Both runtimes resume explicitly saved evaluator state through ordinary C calls.

The native `wasm` and `wat` frontends share application option handling and
the runtime adapter. `native_input.c` discovers the default VFS from the actual
executable's location or accepts an explicit root. Canonical inputs beneath
that root map to guest paths; external inputs grant only a bounded file
snapshot staged in guest state. Their parent directories are not mounted and
guest changes do not write back to the source tree. Application deadlines,
traces and harness reports are opt-in. Common native engine/runtime objects
are cached separately for release and sanitizer linking profiles; guest libc
remains an installed Wasm shared library.

`src/engine/process_driver.{c,h}` owns reusable fork/exec/child selection and
parent/provider continuation restoration policy, with mutable state in a
per-session record. Native polling and browser event delivery remain in runtime
adapters. Borrowed trace/command-stream hooks do not grant host capabilities.
Capsules retain an explicit owner for fork root-engine clones across image or
handler replacement, releasing them during reaping or store teardown.

An executing application never switches engines midway through a process.

## Browser Terminal Contract

The terminal is a byte-stream boundary. The C kernel owns termios, canonical
editing, echo, output processing, signals, foreground process groups, window
size and descriptor readiness. Browser code forwards keyboard/paste bytes and
resize events; it presents output through the VT model without another shell
line editor or newline conversion. Guest ncurses uses ordinary descriptors and
VT sequences, including alternate-screen and application-cursor modes.

`src/html-rt/src/terminal/model.js` owns cells, colors, cursor and escape state.
`renderer.js` draws those cells from the vendored GLF curve geometry in
`glf.js`, using the rogue-wasm-derived analytic Bézier shader and Unicode cmap
lookup. The WebGL path uses static point/index buffers, never a glyph texture
atlas. Backgrounds, underlines and the cursor are separate geometry. Canvas2D
is only a fallback when WebGL is unavailable; the bounded text transcript is
an accessibility/diagnostic mirror. Neither changes terminal semantics.

The renderer/font provenance and MIT/OFL notices live in
`src/html-rt/src/terminal/GLF-NOTICES.md`. These assets and notices are installed
under `/root/app/terminal` and extracted from the offline VFS tarball
before the shell starts. No font fetch, server, cross-origin isolation,
SharedArrayBuffer or Asyncify is required.

Packaged applications execute through the engine's VFS and process-owned
address spaces. Coreutils pipelines and redirection use kernel descriptors;
process exit closes pipe endpoints so readers can observe EOF. Rogue and
ncurses use the shared-library loader and the same TTY path. The portable
`tests/engine-regressions/shared-libc.wast` regression exercises explicit guest
`dlopen`, `dlsym`, final `dlclose`, and reopening the installed libraries.

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

### Installed corpus and result contract

The mounted manifest records identities, source/companion hashes, pinned upstream
revision, grouping, runtime profiles and unsupported reasons. Provenance paths
are not execution fallbacks. Runners read WAST and `.support` assets from the
validated installed directory or extracted browser package; declared `vfs-file`
assets are copied to their guest paths before execution. The upstream test
license is installed at `/usr/share/licenses/wasm-spec-tests/LICENSE`.

Every scheduled test receives a fresh store/kernel; explicit imported-object
aliases persist within one script. Parallel results retain manifest order.
Native children and browser workers isolate tests from the interactive shell.
The guest `/bin/waste-test` command delegates to this runtime capability and
cannot override host root, executable, manifest or expected-failure policy paths.
Shell overlay edits do not alter the inputs used by isolated batch workers.
Browser-only compatibility and interactive renderer fixtures have explicit
runtime eligibility; a SKIP is not evidence of language or kernel acceptance.

Reports preserve per-assertion outcomes and a separate `setup` object:
`total` attempted instantiations, `passed` successes, `complete` diagnostic
collection, and ordered `failures` with `line`, `status`, `phase` and `error`.
Definitions retain syntax and count only when instantiated. Explicit module
assertions remain assertions. `completed` means the command scanner reached EOF,
independently of guest exit or assertion counts. An empty completed script may
pass; an ordinary invalid, unlinkable, trapping or unencodable module fails the
file even if later assertions pass. Recovery never clears that failed setup.

Tracked runtime policies classify completed semantic failures as XFAIL and
repaired expectations as XPASS. Missing inputs, mount failures, crashes,
timeouts, cancellation and incomplete diagnostics cannot become XFAIL.
Native/browser records share identity, status, count and timing fields and
retain detailed `nativeReport`/`browserReport` results. Interrupted browser
records discard partial assertion reports. The official language comparison
policy is separate and pins differences by identity, source hash and issue kind.

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

The C runtime returns explicit exit or yield statuses and resumes saved
evaluator frames through exported functions. Control APIs use versioned
integer-handle messages. Verify observable pause, signal, termination and
scheduling behavior with the shared C session contracts and native/browser
parity.

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
For directory mounts, synthesized boot directories and interpreter nodes use
epoch-zero mtimes; physical nodes retain current host metadata. The separate
unmounted engine fallback can initialize namespace nodes from a staged build
mtime. This fallback does not override metadata supplied by a mounted tree.
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

Both C runtimes use the shared engine process scheduler. Fork creates a
private evaluator/provider graph and immediately makes both parent and child
runnable. Round-robin selection rotates on opcode time slices and blocking
imports; READ/SELECT readiness, absolute timer deadlines, and child exit wake
only the affected processes. Blocking waitpid retries its original import
when the requested child exits. Runtimes service external input and the earliest
session deadline only when the engine has no runnable process. The current single host upload/download
and test-request channel remains serialized.

Cloned engine bindings have independent ownership and compose across fork
generations so linked calls and funcrefs retain process-local memory identity.
WAST command children own their command contexts and module namespaces; one
handler finishing cannot remove another handler's definitions. Concurrent
background jobs and CPU-bound processes share execution fairly on one runtime
worker. This scheduling layer is the foundation for shared-memory guest threads
and execution on multiple host cores; pthread creation/join and simultaneous
host-core execution still require implementation.
Acceptance uses C native/browser contracts.

The scheduling regressions in `src/system-tests/scheduling` cover overlapping
timers, CPU time slices, pipe backpressure, concurrent WAST command namespaces,
and process-group termination. The group-kill probe creates a child, places it
in its own process group, then sends SIGTERM and verifies wait status 15 in
both runtimes.

Stopped-then-continued wait remains an explicit profile gap: a parent that
`waitpid`s a child through SIGSTOP/SIGCONT to final exit (expecting composed
status 5759) does not yet reproduce the full legacy sequence. The individual
`waitpid`, `kill`, and `setpgid` APIs have separate coverage, but the stopped
process lifecycle still needs implementation.

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

Production Bash loads `/usr/lib/libc.so.wasm` from the installed VFS. Build
and install it with `make -C src/aux install-libc`; HTML packaging consumes that
snapshot just as the native runtime mounts it. `src/aux/libc/allocator.c` and the
focused C sources beside it compile with PIC and link as a `dylink.0` shared
object. The library provides allocator state, stdio objects, errno storage,
string and conversion helpers, locale and wide character support, identity
databases, patterns, and terminal/environment wrappers. `/lib/libc.so.wasm`
and `/usr/lib/libc.so.wasm` are compatibility copies, like the ncurses library.

The browser's authored Bash launcher supplies initial `waste-runtime` memory,
table and stack resources and calls `execve` on `/usr/bin/bash`. The shared
`native_process_driver_start` API can instead create these resources directly
in C for a fresh native or browser store and load an executable without a
launcher. A store-owned resource provider participates in the existing import
and fork graph. Initial startup keeps a candidate kernel until loading and
initialization succeed; failure releases the partial image/providers and
restores the original kernel and descriptors. Native `wasm` and `wat` use this
API; `private/guest-session --exec` retains a private compatibility route. Initial
start options can specify binary/text format, a checked alternative entry,
and readable interpreter input while subsequent guest exec retains execute
permission checks. Memoryless modules without a startup hook do not require
the C ABI startup block.
Initial startup and later exec share runtime initialization, which initializes
the process-local libc allocator, environment and stdio before entering the
CRT, which obtains streams through libc accessors. Initializers must have the
expected signatures and complete successfully; traps, failure results and
unsupported startup waits propagate to the caller. Named `libc` imports resolve
through the process's loaded-library catalogue; production startup needs no
`env` registration alias. Kernel imports use host adapters, and legacy fixtures
may register their own aliases. Libraries share the process memory/table while
keeping their own relocated data and globals. `printf`
formats in guest libc and calls `write`, which passes through engine-owned
descriptors to the native or browser I/O adapter. Process, exec, fork, wait,
thread, VFS, signal and clock semantics remain in the C engine's kernel.

On final `dlclose`, the loader frees the library engine and invalidates its
function-table references. Memory/table ranges and handle slots are retained
until process exit, so the per-process library bound also limits reopen cycles.
Range/slot reuse, blocking constructors and general POSIX loader conformance
remain deferred.

`src/aux/libc/runtime/` contains the small freestanding C support library used by
the interpreter itself: memory/string operations, allocation helpers, math,
formatting and host errno. Those sources compile into each runtime because the
interpreter needs them to load and execute guest Wasm. They operate on host
pointers, independently of the guest ABI. Platform backends remain in
`src/cli-rt/lib/` and `src/html-rt/lib/`.

Guest errno lives in guest memory.  Host adapters return explicit results or
error numbers and must never depend on the build host's global errno.  Calls
that require unavailable capabilities fail explicitly rather than pretending
to succeed.

Production Coreutils, Bash, Rogue, ncurses, ldd, upload and download import
matching function signatures from `libc`; utilities do not embed a copy of
libc. Memory and table imports remain process resources, and engine imports
retain their runtime namespaces. The freestanding waste-test command has no
libc dependency. `ldd` walks the unique import-module dependency closure
without executing code, reporting the installed library paths, built-in
adapters and unresolved libraries. Bash imports `lseek` and `__fpurge` directly
from shared libc using the public SDK signatures.

Package WAST tests under `src/aux/libc/tests` import the installed `libc` and
Bash's existing `waste-runtime` memory/table. System interactions are tested
under `src/system-tests/libc`. Both run through `/bin/wast` inside native or
browser Bash; they inherit the actual loaded production library rather than
constructing a separate test library. Cross-module pointers, callbacks,
allocator metadata and errno use the same aliases as applications. Compiler
header/layout checks remain separate system SDK probes, because WAST imports
cannot establish C declaration correctness.

## Wasm32 Application ABI

Application images are ordinary wasm32 modules with process-owned memory,
mutable globals and tables. Modules in one process may explicitly import its
shared `exec_memory`; independent processes share backing only through explicit
shared mappings. Guest pointers are bounded wasm32 offsets, never host addresses.
The virtual-memory ownership and fault rules above also apply at every import.

Registered executable images use ABI version 1 and an exported `_start` entry
of type `() -> ()`. The guest libc startup shim calls conventional
`main(argc, argv, envp)` from that entry. Constructors run before the entry, and returning from
the entry means exit status zero. Loader metadata records path, modes, ABI,
module and entry information. Invalid modules, unsupported ABI/imports or
missing entries are rejected before committing an `execve` replacement.

The versioned `waste_kernel` boundary carries integer handles and validated
guest byte spans. Compatibility `env` imports remain for prebuilt applications
and probes; their error conventions must not be inferred from a libc signature.
Kernel calls such as `open_v1` and `startup_v1` return negative errno values;
guest libc translates errors to its public return convention and image-local
errno. Some legacy imports return raw errors or `-1`. Provider signatures and
availability are maintained in [guest-sdk.md](guest-sdk.md).

### Startup block

The engine copies argument/environment strings and cwd into the replacement
image before committing it. `waste_kernel.startup_v1() -> i32` returns the
startup-block offset, or `-ENOENT` if unavailable. An optional
`__waste_startup(i32 block) -> ()` export receives the same offset;
`__waste_startup_call(entry)` in guest libc decodes it for a C main function.
The block's fields are fixed-width guest values:

| Byte offset | Field |
| --- | --- |
| 0 | argc |
| 4 | argv vector pointer |
| 8 | envc |
| 12 | envp vector pointer |
| 16 | PID |
| 20 | NUL-terminated cwd pointer |

Argument/environment vectors contain 32-bit pointers and end in zero. Storage
lives at the top of the image's linear memory; pointers must refer to copied
strings in that image, including the cwd independently of argv[0].

### Guest structure and timestamp layouts

Guest structures are wire layouts, not the host compiler's C structures.
`struct dirent` has 64-bit `d_ino` and `d_off`, then 16-bit `d_reclen`, 8-bit
`d_type`, and `d_name` at byte offset 19, following the public Wasm32 guest ABI. The compact
pathname metadata record is 48 bytes: kind/mode/uid/gid at offsets 0/4/8/12,
signed size at 16, inode at 24, signed mtime seconds at 32, nanoseconds at 40.
Guest `stat` expands it into the 128-byte public structure. Guest `time_t` is
signed 64-bit. Public headers and compiled guest ABI tests preserve these layouts.

`waste_kernel.realtime_v1` writes epoch seconds/nanoseconds into guest memory;
the browser derives realtime from `Date.now()`. Browser staging passes seconds
as explicit low/high 32-bit words plus nanoseconds. `waste_wast_stage_mtime`
applies source metadata to the last staged entry; `waste_wast_stage_build_mtime`
supplies timestamps for synthetic runtime/interpreter nodes.

## Testing, Measurement, and Deployment Policy

Every shared engine change needs proportional native and browser verification.
Native engine tests use warnings as errors, AddressSanitizer, and
UndefinedBehaviorSanitizer.  LeakSanitizer may remain disabled only for the
documented ptrace environment.  Binary tests cover truncation, malformed LEBs,
overflow, invalid UTF-8, ordering, duplicate sections, bad indexes, and type
mismatches.

Official Wasm/WAT/WAST language tests may be cross-checked against the OCaml
reference interpreter via `./start.sh --ocaml-reference`; see
[OCaml reference interpreter build](techniques.md#ocaml-reference-interpreter-build). Kernel, libc and
application behavior uses C native/browser parity, private sanitizer gates and
compiled guest ABI checks. Local DIY POSIX and libc tests are regression
probes, not formal POSIX certification. Independent scripts must be tested in
different orders and at different dashboard concurrency settings to expose
unintended global state.

Formal POSIX claims require an applicable licensed Open Group suite.  The Linux
Test Project's `testcases/open_posix_testsuite` is an open development baseline,
not certification.  If it is added later, pin its upstream revision, keep its
licensing and Wasm adaptation patches separate, preserve upstream assertion
identities, and classify results as emulated, browser-backed, broker-backed,
unsupported, or failed.

Browser results refer to the actual browser C artifact. Performance reports
identify the artifact, engine, browser, machine, workload, instruction or
allocation count, and elapsed time; build and execution time are separate
measurements.

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
