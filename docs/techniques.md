# C Engine Implementation Techniques

## Purpose

This document collects reusable techniques learned while bringing the C
engine, WAT/WAST front end, linker, executor, guest libc, and browser runtime
to conformance.  It explains how to extend the implementation without
reintroducing earlier ambiguity, ownership, or portability failures.

System ownership and runtime boundaries are defined in
[architecture.md](architecture.md).  Unfinished staged work belongs in the
active plans rather than this document. The OCaml reference interpreter in
`submodules/wasm-spec/interpreter` may be built via
`./start.sh --ocaml-reference` to cross-check Wasm/WAT/WAST language semantics;
see [OCaml reference interpreter build](#ocaml-reference-interpreter-build).

## Auxiliary Guest Utilities

Authored `ldd` source lives in `src/aux/ldd/ldd.c`. Run `make -C src/aux ldd`
to compile and link `build/aux/ldd/ldd.wasm` directly with Clang and `wasm-ld`,
using the installed guest SDK and shared-libc import attributes. Run
`make -C src/aux install-ldd` to build and install the audited snapshot into
`src/vfs/usr/bin/ldd`; `start.sh` delegates to this target.
Python is used by the import signature check and VFS installer only.
`make -C src/aux` builds `ldd`, upload, download and Rogue together. Compilation
does not publish snapshots; use the corresponding `install-*` targets explicitly.

`ldd` reads Wasm import sections and follows resolved library dependencies
without executing the inspected modules. Check production dependency listings,
malformed input and missing libraries with the `shared-dependencies` scenario
in `tests/guest-session-check.py`.

Rogue's package configuration, ncurses Boolean ABI profile, library import
attributes and patches live in `src/aux/rogue`. Run `make -C src/aux rogue` to
copy upstream inputs into `build/aux/rogue/src`, patch that staging copy and
compile/link `build/aux/rogue/rogue.wasm` directly with Clang/`wasm-ld`.
The build uses current installed headers and attaches `libc`/`libncurses`
namespaces to C declarations; no Python build generator, temporary sysroot or
post-link import rewrite is needed. `make -C src/aux install-rogue` checks
library signatures and explicitly publishes `/usr/bin/rogue`; `start.sh`
delegates to it. Check `rogue-fresh`, `rogue` and `shared-dependencies` in the
production native/browser session harness. See
[the Rogue build notes](../src/aux/rogue/README.md) for patch responsibilities.

## Shared Guest Libc Build

`make -C src/libc` compiles the production guest library directly with Clang
and links `build/libc-shared/libc.so.wasm` with `wasm-ld --shared`. It uses
installed SDK headers, PIC objects and the existing POSIX I/O profile. Objects
and dependency files stay under `build/libc-shared/objects`; Make rebuilds
changed inputs and supports parallel compilation. The linker can use the
repository-staged toolchain when `wasm-ld` is absent from PATH.

`make -C src/libc install` explicitly publishes the audited library through
`vfs.py` to `/lib/libc.so.wasm` and its `/usr/lib` compatibility copy. The
browser Makefile delegates its `libc-shared` and `libc-shared-install` targets
to this Makefile. `start.sh --build-libc` builds the shared library there, then
builds the separate static WAST fixture profile through `waste-libc`.

`src/libc/sources.mk` is the ordered C helper list consumed by both Make and
the Python fixture/provider tools. The shared library additionally compiles
`allocator.c`; the static profile uses the allocator in `stdlib.wat`. Keep
these profiles distinct when verifying behavior. Run
`python3 tests/guest-session-check.py --scenario shared-libc --scenario shared-dependencies`
to check the installed production library in both native and browser C builds.

## Browser File Transfer

In the offline `bash.html` shell, `upload DEST` selects a host file and writes
its bytes to the guest VFS; `download FILE` sends an existing guest file to the
browser with its basename as the suggested filename. Both commands are installed
under `/usr/bin`. Upload cancellation or a file-read failure returns
nonzero. Download success means delivery to the browser download mechanism; the
page cannot confirm the eventual host disk write or detect a cancelled Save dialog.

Authored transfer sources live in `src/aux/upload/upload.c` and
`src/aux/download/download.c`. Run `make -C src/aux upload download` to build
`build/aux/upload/upload.wasm` and `build/aux/download/download.wasm` with the
installed guest SDK and shared libc. The Makefile invokes Clang and `wasm-ld`
directly, using the mounted headers and explicit C import attributes for the
`libc` namespace. Run `make -C src/aux install-upload install-download` to
build and publish the snapshots through `vfs.py`, preserving other files and
checking shared-libc import signatures. Installation targets serialize VFS
publication with `flock`; `start.sh` delegates to these targets. Python is used
for VFS installation, not compilation. Frontend changes
are explicitly installed with `vfs.py install --component app --source
src/html-rt/src` before packaging. The tarball contains `/root/waste/app`; do not
duplicate transfer code in the HTML generator or stage binaries in frontend sources.

The shared ABI in `src/engine/guest_posix.c` provides
`waste_kernel.host_upload_v1(path, length, flags)` and
`host_download_v1(name, name_length, data, data_length, flags)`. Browser replies
must release temporary buffers after the engine copies them, wake a waiting
continuation even on failure, and avoid delivering an old picker result to a
replacement shell worker. Empty files are valid uploads and downloads.

Focused verification, without launching a browser:

```sh
node tests/browser-upload-runtime.cjs
node tests/c-engine-vfs-transfer.cjs build/html-rt/bash.html
python3 tests/guest-session-check.py --scenario transfer
```

The first checks DOM/worker failure handling with simulated browser APIs; the
second runs real guest utilities through the packaged worker with scripted file
selection; the third checks native/browser engine transfer parity. Real picker
and download acceptance was confirmed on 2026-10-06: the user verified uploaded
contents and cancellation, and supplied a JSON report downloaded from Bash.
For future manual regressions, run `upload /tmp/test.txt`, select a known text
file and inspect it with `cat /tmp/test.txt`; run another upload, cancel the
picker and immediately check `echo $?` for a nonzero status. Run
`download /tmp/test.txt` and compare the saved contents.

## Virtual-Memory Ownership and Commit Boundaries

Keep virtual-memory metadata in the engine-owned process capsule, not in a
browser adapter or a host pointer. A mapping operation should validate its
address, length, overflow, protection, file range, and collision behavior
before publishing any region or acquiring a backing-page reference. On a
failure path, release references acquired during preparation and leave the
previous mapping topology unchanged.

Treat these states separately: a virtual page reserved but not mapped; a
mapped page protected by `PROT_NONE`; an unmapped address; and a mapped file
page whose backing file has since been truncated. The last case is a
structured engine memory fault annotated for the process signal boundary; it
is not a host signal and must not be converted into an ordinary out-of-range
error. `fork` and checkpoints retain backing-page and alias identity, while
`execve` builds a replacement graph transactionally.

Use the native lifecycle/checkpoint fixtures and the offline browser harness
after changing any of these ownership or commit rules.

## Deterministic WAST Command Framing

A WAST file is a sequence of independently observable commands.  Before
invoking the module grammar, `wast_stream` asks the reentrant Flex scanner's
boundary mode for one balanced top-level form.  That mode uses the same
location, string, escape, annotation, line-comment, and nested-block-comment
state as ordinary parsing.  The driver classifies the returned byte range and
parses it with a fresh `wat_context`.

This boundary provides deterministic recovery:

- a malformed command produces one recorded parse result;
- the next top-level command begins at a known byte boundary;
- registered modules and the current store survive successful commands; and
- partially built objects from a failed command are released once.

Do not add a second handwritten comment/string scanner, or use `setjmp`,
`longjmp`, GLR ambiguity, or grammar error recovery to find the next WAST
command.  Boundary recognition belongs to the shared scanner; recovery policy
belongs to the command driver.

Ordinary module commands are setup, not assertions. Record attempted and
successful instantiations plus ordered line/status/phase diagnostics separately
from assertion/action results. A later passing assertion cannot erase a failed
setup command, and zero assertions do not establish successful execution.
Distinguish EOF completion from scanner failure or interruption. Definitions
are retained syntax; count their instances when loaded rather than claiming a
definition was instantiated. Preserve explicit module assertions as assertions.

Release a failed command's retained parse after the dispatch loop finishes
reading it. A module-processing callback must not free the script whose groups
its caller is iterating. Keep engines from failed starts alive through store
teardown when imported tables may contain their funcrefs.

## Process-Continuation Diagnostics

When a fork/exec regression occurs, compare the parent capsule and resumed
frame before the first post-fork guest store: PID, function/program counter,
locals, memory object and backing buffer identity, page count, and stack-pointer
global.  Then inspect linked provider continuations (especially libc); a store
checkpoint that restores memory without restoring provider evaluator frames can
resume a child `execve` activation in the parent.

Keep the browser process driver as a selector, not a scheduler of its own.  It
records the selected capsule and wait reason, re-selects that PID on resume,
and resets deterministically on completion or error.  Exercise successful exec,
failed exec, command-not-found, repeated terminal waits, child exit, and parent
reaping under warnings-as-errors plus ASan/UBSan.  Disable LeakSanitizer only
for the documented ptrace environment; do not treat that exception as a reason
to weaken ownership checks.

## Terminal Rendering and Shell Verification

Keep VT parsing independent of rendering and engine execution. Feed the model
the exact engine-processed byte stream; canonical/raw handling and ONLCR belong
in the C kernel. Preserve incremental UTF-8/escape parsing, alternate-screen
state, CSI REP and character-set designators used by ncurses. Application-cursor
mode must affect the input encoder as well as output parsing.

Use font metrics to transform GLF geometry into cells. Preserve cmap format
4/6 lookup, missing-glyph behavior, indexed glyph ranges and analytic curve
coverage. Upload immutable geometry once, keep shader setup outside the cell
loop and coalesce redraws. Resize the backing canvas for device pixel ratio
without changing model contents. Any geometry optimization must preserve
glyph appearance; do not introduce a raster atlas into the WebGL path.

The fast checks run without a browser:

```sh
node tests/c-engine-terminal-glf.cjs
node tests/c-engine-terminal-model.cjs
```

For actual WebGL pixels, open the rebuilt `build/html-rt/bash.html` in
Firefox and run these commands at its Bash prompt:

```sh
wast /root/waste/tests/render/terminal.wast
download /tmp/terminal-renderer-results.json
```

The authored fixture is `tests/render/terminal.wast`; `vfs-tests-install`
installs its snapshot at `src/vfs/root/waste/tests/render/terminal.wast`.
The WAST calls the versioned `waste_kernel.render_test_v1` browser capability,
then writes the reply through ordinary guest descriptors. The DOM adapter uses
the production GLF renderer to test glyph pixels, blank cells, colors, cursor,
two widths and simulated display densities 1/2. It rejects the Canvas2D
fallback and glyph texture creation. Rendering failure still produces the JSON
file before the WAST assertion fails. Transport/filesystem failures instead
fail the file-writing assertion; do not interpret a stale earlier report as a
new result. The report includes its timestamp.

Save the downloaded JSON under `build/html-rt/`. Closure requires `ok: true`
and a manual check of the shape-sensitive text/colors printed by the fixture.
Also resize the browser and change zoom; simulated densities test sizing, not
monitor/zoom event delivery. The adapter draws into a temporary offscreen
canvas, releases its WebGL context afterward, and preserves the live Bash
terminal. There is no separate generated test page.

The `render` corpus group is marked interactive-only and skipped by unattended
batch runners. Native and isolated browser stores do not enable this capability;
`engine-regressions/render-capability.wast` checks the unsupported return.
The response capacity comes from `src/config.h` and is queried by the guest.
The wire reply is a little-endian u32 pass flag followed by UTF-8 JSON.
`tests/render-worker-session.json`, run through `tests/guest-session-worker.cjs`,
uses scripted replies to verify persistence, failure reporting, stale-reply
rejection and truncation across repeated shell runs. It does not test pixels.

Pixel checks complement the worker-backed shell checks. After building the
production page, run `./start.sh --html-check` for the focused worker suite.
For targeted changes, `tests/c-engine-bash-browser-runtime.cjs` accepts
`--missing-command`, `--pipeline-probe`, `--shared-library` and `--full-package`.
Run `--missing-command` without `--full-package`: its reduced fixture
deliberately omits `ls` to check command-not-found recovery.
The Rogue fixture must enter/leave the alternate screen, consume an arrow and
a game turn, and return to a usable Bash prompt twice in the same shell.
`python3 tests/guest-session-check.py --scenario shared-libc` checks explicit
guest `dlopen`/`dlsym`/`dlclose`, error consumption, reference counts and reopen
through the installed libc/ncurses libraries in both C builds. Its authored
fixture is `tests/engine-regressions/shared-libc.wast`; the installed copy can
also run from Bash with `/bin/waste-test engine-regressions/shared-libc.wast`.
Use the installed-test runner so this bootstrap test gets an isolated module
store instead of inheriting the shell's already-loaded libc.

Production packaging also needs a dependency check: a loader fixture alone
cannot establish that applications use the shared provider. Run
`python3 tests/shared-library-packaging-check.py` to inspect installed consumers,
then the `shared-dependencies`, `matrix` and `rogue` scenarios in
`tests/guest-session-check.py` to verify actual `ldd` output and execution
in both C runtimes. `shared_libc.py` rewrites only matching function imports,
checks the installed provider's exact signatures, and leaves memory, table,
relocation and kernel imports intact. Coreutils links imported process memory
without imposing a maximum absent from the exporter; ordinary Wasm limit
validation still applies. The install review flags cover those checked libc
imports explicitly. Preserve the separate static libc fixture profile.

For PIC libraries, keep relocation bases in storage owned by the loaded
module's call block. A pointer into the loader's temporary context silently
changes an existing DSO's base when another library loads. Patch GOT entries,
apply data relocations, then run constructors. Reserve executable table slots
before allocating dependency slots. Reuse the process's shared stack global,
falling back to the `waste-runtime` owner when the active application does not
export it. Test libc allocation after loading another DSO.

The synchronous loader defers cooperative pump yields across instantiation,
relocations and constructors, while retaining deadline/cancellation polling.
Restore the caller's pump quantum on every return. A yielded constructor
cannot resume a loader C activation that has already returned; do not report a
cooperative timeslice as `dlopen` failure. Verify through the installed browser
batch runner, which enables the pump, as well as direct engine calls.

Exec bootstrap must find the process-local libc through the loaded-library
catalogue before using legacy `env` fallback. Reset the allocator beyond the
immutable startup block, then set the environment and stdio. Registration
aliases alone do not identify the provider reliably; skipping initialization
can let inherited allocator state overlap a new executable's data. Verify `ls`
as well as simpler utilities. Native `waste-session --trace-process` writes
JSON process events to stderr, including exec rejection and child trap details;
redirect stderr under `build/engine/logs/` when diagnosing status 126/127.

Fork copies library metadata and rebinds providers to the child's graph.
Grow that copied metadata array before appending dependencies. On final close,
remove registrations and invalidate table references owned by the freed engine;
clear the handle's identity so reopen creates a live instance. Detach an
inherited clone from capsule ownership before freeing it. Memory/table range
and handle-slot reuse on ordinary close remain deferred; process exit releases the address
space. Repeated Rogue children test these lifetimes across real fork/exec/wait.

## WAT and WAST Parser Policy

WAT and WAST should not have competing lexers or module grammars.  The same
reentrant Flex scanner and pure LALR Bison parser recognize module syntax.
Drivers apply the mode-specific policy:

- WAT requires one complete module and fails transactionally.
- WAST parses one framed command, records the expected or unexpected outcome,
  and continues.

Keep syntax recognition in the lexer and grammar, but keep assertion checking,
name resolution, type checking, validation, instantiation, and execution in C
passes.  Grammar actions should capture structure and raw semantic values,
not decide whether an invalid module satisfies an assertion.

Parser and scanner state must be caller-owned.  `%define api.pure full` and a
reentrant scanner are insufficient if grammar actions still use file-static
accumulators.  Locations, temporary vectors, fixups, numeric scratch storage,
and current-module state all belong to an explicit parse context.

Command streams create many short-lived contexts. Keep zero-initialized parser
state and ownership fields before the bounded scratch arrays in `wat_context`.
The builder clears that state prefix; scratch entries must be initialized before
consumption, with zeroed live counts guarding every lookup. Add fields that need
an initial zero above this boundary. Start group storage with one zeroed group
and grow for whole-script parsing; reserving/clearing unused maximum workspace
on every command can dominate execution. Verify this boundary with nonzero
allocation poisoning and the complete corpus, including names, fixups and
branch-table cases.

## Folded Instruction Boundaries

Most words in a folded expression are instruction mnemonics, not special
grammar keywords.  Structural module fields such as `func`, `type`, `param`,
`result`, and `local` need dedicated grammar roles; ordinary dotted operators
can use metadata-driven tokens and shared operand parsing.

The ambiguity occurs at `(`, before the parser knows whether the form begins a
field, control instruction, or ordinary folded operator.  Resolve it with a
bounded lexical start token for the complete structural prefix, such as a
folded `if`, `block`, `loop`, `select`, or immediate-bearing instruction.  The
lexer should perform only enough lookahead to identify that boundary, then let
the grammar parse the operands and nested expressions.

After a block type, parse fields in the same order as the reference grammar:
type use, parameters, results, then body.  Flatten nullable phase handoffs when
they introduce shift/reduce conflicts.  Avoid adding semantic keyword tokens
for every opcode; opcode metadata should remain the common vocabulary.

## Parse General Forms, Validate in a Later Pass

When valid and intentionally invalid WAST modules share the same syntactic
shape, use a permissive structural production and validate afterward.  This is
essential for `assert_invalid`: the parser must retain the invalid expression
so semantic rejection can be observed.

Constant expressions are the model technique:

1. Parse the general instruction sequence into a terminator-free buffer.
2. Resolve deferred names and type references.
3. Run a `check_const`-style pass over the retained expression.
4. Verify the allowed operator set, operand/result types, immutable
   `global.get`, reference constructors, and declared destination type.
5. Store semantic rejection on the containing assertion group in WAST mode;
   fail the WAT transaction in WAT mode.
6. Let the binary encoder append the required `end` opcode exactly once.

The same pattern applies to element/data offsets and other contexts where the
reference grammar admits a general expression but validation restricts it.
Do not create overlapping grammar alternatives for every valid operator
combination.

## Deferred Resolution and Source Locations

Text syntax permits forward references in several index spaces.  Record a
bounded fixup containing:

- reference kind;
- source line, column, and byte offset;
- destination object and field;
- original name; and
- expected index space or type constraint.

Apply fixups only after the relevant module declarations are complete.  Keep
empty name-table entries so table positions match Wasm numeric index spaces.
Unknown names must produce a structured error at the original reference, not
an encoded sentinel that fails later in the binary decoder.

Function, type, table, memory, global, element, data, start, export, and
initializer references should use the same fixup mechanism.  Named operands
such as `table.init` may refer to more than one index space and must record each
role explicitly.

## Annotation Handling

Registered custom annotations need deterministic, bounded handling without
changing the ordinary module grammar's semantics.  The current implementation
retains annotation placement and payload information long enough for C code to
validate custom sections, names, and branch hints, including folded and plain
control instructions.  Unknown annotations remain ignorable according to the
text-format rules.

The scanner consumes annotation envelopes directly without rewriting the
source and preserves their original line, column, and byte offsets.  An
annotation error inside an assertion must be reported through that assertion's
malformed or invalid classification rather than terminating the WAST stream.
Keep bounded registered-annotation validation in C and do not add another
independent raw-text boundary scanner.

## Binary Readers, Writers, and LEB Values

All Wasm byte processing uses bounded readers and overflow-checked writers.
Readers expose transactions or sub-readers so a failed decode does not leave a
partially advanced cursor.  Section decoders receive a bounded section reader
and must consume it exactly.

Use the shared LEB implementation for unsigned, signed, 32-bit, 33-bit, and
64-bit fields.  Test shortest canonical encodings, accepted non-short
encodings, truncation, excessive length, unused-bit overflow, and cursor
rollback.  The linker and encoder must not carry private LEB implementations.

The decoder distinguishes malformed binary structure from semantically invalid
modules.  It rejects duplicate or out-of-order sections, unsupported standard
section IDs, invalid UTF-8, invalid indexes, and malformed limits with a
structured result rather than an assertion or out-of-bounds read.

## Decode, Validate, and Instantiate Separately

Decoded declarations are immutable and own their source-independent data.
Validation reads those declarations without creating mutable instance state.
Instantiation then allocates and initializes memories, tables, globals,
segments, tags, and runtime objects.

This separation enables three important tests:

- decode once and instantiate twice without shared mutable state;
- release or replace the source byte buffer without invalidating declarations;
  and
- classify decode, validation, linking, initialization-trap, and execution
  failures independently.

Constant-expression evaluation occurs during instantiation after validation.
Instantiation must preflight limits and references before exposing a partially
constructed instance, while preserving specification-required side effects on
failure paths.

## Cross-Module Linking

Resolve imports from the decoder's declarations rather than rescanning section
2.  A linked function binding records the provider instance and its local
function index.  Other extern bindings retain pointers to provider-owned
memory, table, global, or tag objects with explicit lifetime ordering.

Type equality cannot compare module-local type indexes directly.  Compare
function parameters/results and indexed reference types structurally across
their owning modules.  Preserve nullability, heap type, table address width,
limits, mutability, and tag signature.

Tests should cover provider/consumer modules, explicit registrations, imports
after expected instantiation failure, two consumers of one memory, and two
instances created from one decoded module.  Destroy consumers before providers
unless ownership has been promoted to a shared store object.

## Proper Tail Calls

Implement `return_call`, `return_call_indirect`, and `return_call_ref` as an
update of the active interpreter frame:

1. Validate and resolve the target.
2. Pop or copy its arguments before overwriting the caller's locals.
3. Replace function identity, arguments, locals, and body.
4. Reset the numeric PC and structured-control state.
5. Continue the dispatch loop with the original return continuation.

Do not represent each tail transfer with C recursion, heap allocation, or an
exception. The C engine needs an explicit frame-reuse operation. Deep official
tail-call fixtures and a large bounded native loop should show constant C
stack and no allocation per transfer.

## Guest Non-Local Control Transfer

Native C `setjmp`/`longjmp` cannot express a guest jump through a browser Wasm
interpreter safely.  Treat guest `sigsetjmp` as creation of an evaluator
snapshot identified by the guest jump-buffer address.  The snapshot records
the interpreter frame generation, PC, operand/control stacks, locals, and
guest stack-pointer state required by the ABI.

Treat guest `siglongjmp` as a typed engine result.  Ordinary C returns propagate
it until the matching live interpreted frame restores the snapshot.  Reject
stale environments and normalize a requested zero return value according to
the guest ABI.  Signal-mask restoration is part of `sigsetjmp` semantics, not
native process state.

This design avoids native stack-unwinding machinery and permits deterministic
cleanup on traps, exits, or cancellation.

## Explicit Yield and Resume

A host import that cannot complete synchronously returns `EXEC_YIELD`.  Before
returning, each active interpreter depth records the state needed to reenter
the same call.  The operand and control stacks live in engine-owned storage;
per-depth records retain PC, function identity, and control height.

On resume, reenter the original top-level invocation.  Saved frames recognize
the resume path, retry the pending import, and either complete or yield again.
Push original call arguments back before propagating `EXEC_YIELD` through
ordinary C returns so the imported instruction can be retried exactly.

This is not Asyncify and must never be implemented with an Asyncify transform,
runtime hooks, or unwind/rewind imports. The native C/Wasm call stack is not
saved. Only interpreter-owned data is retained, and the later resume export
starts a new host-to-Wasm call that reenters those saved evaluator frames. When
reviewing a build, any Asyncify flag or `asyncify_*` import/export is a failure,
not an optional optimization.

Do not reset a partially resumed engine, retain pointers into guest memory
across the yield, or infer the blocked operation in JavaScript.  The POSIX
runtime should attach an explicit wait reason, stable handles, deadline, and
cancellation generation as described by the active `select`/`pselect` plan.

## Bounded process continuation

The browser Bash command-not-found path uses a bounded child-first process
transition. At `fork`, capture the parent evaluator and mutable store state;
run the child until failed `execve` and `exit(127)`; record the zombie; restore
the parent; resume `fork` with the child PID; and let `waitpid` reap the saved
status. Child and parent transitions remain inside the C driver. Only terminal
or select waits cross into the browser worker.

This slice is intentionally not a general scheduler: process records do not
make multiple guest threads concurrently runnable. Add per-thread runnable and
blocked states before extending it to independent live parent/child work. The
regression must exercise more than one post-fork terminal read, because the
single-command smoke path does not validate repeated continuation re-entry.
The static Bash-page build therefore runs an assignment/expansion/process
sequence (`HOME_DIR=/home/a`, `echo ${HOME_DIR}`, `ls`), verifies exit status
127, accepts a later builtin and a second missing command, and exits normally.
Run this gate against the same staged worker and engine bytes embedded in the
self-contained page; an older generated page can otherwise hide a corrected
runtime behind stale assets.

## Proposal-Specific Representation

Feature implementations should preserve semantic distinctions through text,
binary, validation, and runtime layers:

- SIMD uses one mnemonic/opcode/immediate metadata table rather than separate
  parser and executor lists.  Lane, shuffle, memory, and relaxed operations
  retain their immediate shapes.
- Memory64 and multi-memory select address width and offset limits from the
  referenced memory.  Bulk operations may combine memories with different
  address widths and must validate each operand accordingly.
- GC types retain recursive groups, finality, supertypes, packed storage,
  mutability, defaultability, nullability, and indexed heap identity.  Runtime
  objects retain their dynamic reference type.
- Exceptions are not traps.  Their carrier preserves tag identity, typed
  payload, owner instance, and exception reference across direct, indirect,
  reference, and imported calls.

When a proposal adds syntax, extend shared metadata and structural boundaries
before adding isolated grammar alternatives.  When it adds runtime semantics,
preserve enough decoded information for validation to reject invalid modules
before execution.

## Browser Packaging

Generate browser pages by embedding the exact C-engine Wasm and original WAST
or application source.  The worker instantiates the engine, copies source into
its memory, runs the streaming entry point, and reads structured results from
exported accessors.

Interactive pages wait on ordinary worker events.  Terminal input is copied
into engine-owned state before resume; output is copied to the presentation
layer.  Keep the page self-contained and usable through `file://`.  Do not add
a server or cross-origin-isolation dependency merely to obtain scheduling.

Generated result counts must come from parsed commands, not a lexical count of
the word `assert`, because comments, quoted modules, and annotations can contain
assertion-like text.

## VFS-Backed Executable Loading

Treat an executable as an ordinary engine-VFS regular file. `execve` resolves
the pathname, checks execute permission and type, takes a bounded owned byte
snapshot, and validates or compiles that snapshot before constructing a
replacement process image. Production browser execution must not depend on a
parallel JavaScript blob map or executable-byte registry. A compatibility
registry may exist only for focused native lifecycle fixtures that do not
construct a VFS namespace.

Classify binary Wasm by its `\0asm` magic, not by a filename suffix. Textual
files use the explicit `/bin/wat` or `/bin/wast` handlers, a `.wat`/`.wast`
suffix, or a bounded shebang naming one of those handlers. Shebang splitting,
the optional single interpreter argument, recursion limits, and argv rewriting
belong to the loader boundary. Pass a shebang-stripped byte span to the shared
WAT/WAST parser; do not add general `#` comments to the grammar.

Keep loading transactional. Decode, validation, import checks, linking,
instantiation, startup-block creation, and entry lookup must succeed before
the old image is released. Preserve the old image and return the correct
failure status on any earlier error. Test binary files without `.wasm`, direct
and shebang WAT/WAST execution, malformed text, missing and non-executable
paths, repeated handler use, child exit status, and a later usable shell
prompt.

## Coreutils Cross-Build and Distribution

Keep every submodule read-only. Repository changes belong in
`submodules/coreutils-waste.patch`; copy source into `build/coreutils/`, apply
the patch to that staged copy, and generate `configure` there with
`submodules/bootstrap-coreutils.sh`. Configure output, the target sysroot,
object files, linked images, reports, and corresponding-source artifacts are
generated outputs and must remain below `build/`. The existing staging helper
still temporarily applies the patch to the submodule and must be refactored
before use under this policy; see [the submodule policy](submodule-policy.md).

Build utilities against the WASTE sysroot and guest libc, with the application
and libc sharing one process memory. Keep configure answers explicit and
machine-readable. Each accepted utility needs a report that records its source
objects, relink inputs, final image, import audit, and blockers. Reject unknown
imports and every Asyncify/unwind/rewind symbol; do not make a utility pass by
silently expanding a JavaScript import surface.

Use the stable build layers:

```sh
make -C src/html-rt BUILD_DIR=../../build/html-rt coreutils-wasm
make -C src/html-rt BUILD_DIR=../../build/html-rt coreutils-audit
make -C src/html-rt BUILD_DIR=../../build/html-rt coreutils-package-audit
./start.sh --html-bash
```

Install accepted utility images as extensionless `/bin/NAME` and
`/usr/bin/NAME` VFS files. Install `/bin/wat` and `/bin/wast` as engine-owned
handlers, and retain license, provenance, interpreter, and source-package
metadata under `/usr/share`. The offline tar uses normalized ordering,
ownership, and header timestamps for reproducibility; a separate manifest
carries source mtimes into guest `stat`, while engine-created namespace nodes
receive the engine image build timestamp.

GPL distribution is a build gate, not a release note added afterward. The
corresponding-source bundle must identify the pinned source commit, managed
patch, generated configure tree, sysroot/runtime sources, build instructions,
notices, and every shipped utility. Audit the package-to-source mapping and
publish its digest with the artifact. See
[coreutils-source-distribution.md](coreutils-source-distribution.md) for the
release procedure.

Retain both focused utility tests and one aggregate browser matrix. Focused
tests locate an ABI or utility regression; the aggregate test proves that all
accepted utilities plus `wat` and `wast` execute sequentially in one Bash
lifetime with correct statuses, representative output, a later prompt, and a
clean exit from the same self-contained `file://` package.

## Fast Native Testing

Use the mmap/native CLI path for parser, encoder, decoder, linker, validator,
and executor iteration.  It avoids Node and browser startup while running the
same platform-neutral engine code:

```sh
./start.sh --cli-compile
build/cli-rt/waste-cli --parse-only FILE.wast
build/cli-rt/waste-cli FILE.wast
```

Use focused fixtures for one grammar or runtime boundary, including a matching
negative fixture when failure classification matters.  Then run the broader
gate:

```sh
./start.sh --cli-test
```

Native unit tests compile with warnings as errors, AddressSanitizer, and
UndefinedBehaviorSanitizer.  LeakSanitizer is disabled only where the ptrace
environment prevents it from operating reliably; this is not permission to
ignore ownership leaks.

Move guest-observable C driver assertions into authored WAST with a recorded
argument/result/trap mapping. Keep direct C checks for embedding APIs, real host
callbacks, source/instance ownership, clone/page identity and teardown. The
executor smoke gate runs both retained C probes and the portable
`engine-regressions` group with ASan/UBSan; it compares installed fixture bytes
against authored inputs before using distribution snapshots. WAST instance
isolation complements the C decode-once/instantiate-twice lifetime check rather
than establishing that private ownership contract.

Use real guest imports for caller-memory and continuation counterparts: a
pipe round trip observes the executing module's bytes, and a positive timed
SELECT supplies a production wait boundary. Guest entry/completion counters
can detect repeated side effects after resume, but cannot establish the C
snapshot API's capture/restore/replay contract. Keep that sanitizer gate.
Private C memory probes must use bounded `exec_memory_read` and initialized
sparse-page observations rather than a removed contiguous `memory->data` field.
The native WAST adapter services finite SELECT deadlines with a monotonic clock
and bounded host sleeps; external input and indefinite waits use the session
driver. Verify POSIX fixtures through C native/browser parity and the retained
sanitizer/session contracts.

Path regressions can create private files with real guest imports instead of
depending on installed executable bytes or host filesystem writes. Assert the
guest return value and errno separately for failed operations. Preserve the
versioned import's negative-errno contract and perform libc's translation in
the guest wrapper where needed. Check public stat fields at the documented
Wasm32 offsets; keep private codec round trips, explicit byte-span bounds,
seeded symlinks, mtime and snapshot ownership under direct C sanitizers until
equivalent guest capabilities exist. Compare only the initialized pathname
and terminator when inspecting getcwd buffers.

Use zero-timeout SELECT to expose pipe readiness through the guest ABI, while
retaining raw HUP/ERR bits and reference ownership in C probes. Dup2 tests
should transfer bytes through the replaced descriptor and verify same-fd flag
preservation, rather than checking only that a descriptor appears open. For
zero-count I/O, validate the guest range and delegate descriptor bounds to the
kernel with a non-NULL stack byte. Keep that byte separate from the owned heap
buffer so ordinary cleanup never frees stack storage. Record existing import
error conventions explicitly; raw kernel statuses and libc-translated errno
must not be silently interchanged during fixture migration.

## Test Boundary Selection

Choose the layer that can observe the invariant. Moving guest behavior into
WAST does not remove the need to test private state or the browser boundary:

| Boundary | Retained verification |
| --- | --- |
| Language and guest-visible POSIX outcomes | Authored WAST under `tests/engine-regressions`, compiled libc clients and installed corpus execution |
| Post-yield input, clocks, signals, fork/exec and terminal transcripts | Shared `tests/guest-session-*.wast`/JSON contracts; see [native sessions](native-guest-session.md) |
| C API and ownership | Direct sanitizer gates for callback identity, decode-once instances, snapshot/replay, clone binding, page aliases, atomic exec transitions, handler cursors and teardown |
| Compiler ABI | Compiled guest C probes for public header widths, offsets, alignment, varargs and callback signatures; do not translate away the compiler being checked |
| Host adapters | Worker/DOM tests for message ordering, malformed controls, cancellation, FileReader, Blob downloads and allocation failures |
| Distribution | Inventory/hash/metadata and exact-byte checks, tar extraction, offline references and mounted-path shell probes |
| Real browser | Offline `file://` boot, DOM/worker behavior, rendering and downloadable reports; worker VM tests alone cannot establish these |

Keep private NULL/overflow guards, raw readiness bits, injected credentials or
clocks, object/reference identity and independent-kernel checks even where
guest calls cover corresponding visible effects. Direct mapping reads reject
ranges beyond EOF; ordinary POSIX reads may return partial bytes. Preserve
these distinct contracts. [Retained kernel checks](posix-kernel-retained-coverage.md)
and [libc harness coverage](libc-harness-coverage.md) retain detailed boundaries.

Tests must call the production implementation to establish its coverage. The
legacy `c-engine-shared-lib-dylink.c` probe duplicates a parser, and its checked-in
PIC fixture's metadata differs from a fresh source build; it is not evidence
for the production loader. Likewise, building/import-auditing a CRT fixture
does not establish that `_start` ran. Preserve these limits when evaluating
shared-library and executable startup acceptance.

For setup-report regressions use `tests/test-suite-setup-*.wast`: empty/valid
zero-assertion streams, invalid/unlinkable/start-trapping/unknown-definition/
encoding failures, and recovery followed by passing assertions. Also check
truncated tails, XFAIL/XPASS and diagnostic capacity failures. Diagnostics own
their strings and survive store teardown; reset them between runs. Do not let
allocation failure silently truncate the report or turn a failed setup into PASS.

## Installed Corpus Workflow

Treat `src/vfs/root/waste/tests` as installed snapshots. Edit official inputs
only through a pinned upstream update, authored regressions under `tests/`, or
libc client includes under `tests/libc-test`; generate libc fixtures under
`build/html-rt/waste-libc/tests`. Submodules remain read-only.

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/cli-rt corpus-native
build/cli-rt/waste-test --vfs-root=src/vfs --list
build/cli-rt/waste-test --vfs-root=src/vfs --group=engine-regressions --json
```

Installation verifies candidate test/companion consistency, then publishes test
files, manifest and license. An explicit refresh replaces managed corpus files,
including local edits; it needs no selection approval flag. Packaging discovers
the current VFS tree without rebuilding tests or comparing them to sources.
The explicit `vfs-tests-check` gate still checks corpus selection, test bytes and
policy against authored/upstream inputs; unrelated source hashes are not gates.
Native `corpus-native` writes
`build/cli-rt/corpus-results.json` and does not refresh the distribution.

Repeat `--group`, `--exclude` and `--exclude-group`, or select positional
identities, mounted paths or filenames. Positive selections combine before
exclusions. Use `--jobs`, `--timeout-ms` and `--timeout-group=NAME:N` to bound
runs; `--results` saves a host report for native `waste-test`. Runtime limits
come from `src/config.h` and policy files, not dated documentation counts.

In `bash.html`, **Diagnostics → Installed tests** lists/selects/runs/cancels
the same corpus and downloads JSON without Node. Interactive renderer fixtures
are skipped in batches; run them with `wast` as described in terminal verification.
The guest command also runs from native or browser Bash:

```sh
/bin/waste-test --list --group=libc-test
/bin/waste-test --jobs=2 --results=/tmp/results.json path-runtime.wast address.wast
/bin/download /tmp/results.json
```

Guest `--results` and stdout redirection write guest files. Parent directories
must already exist. Exit codes are 0 for accepted outcomes, 1 for failures,
XPASS or timeouts, 2 for invalid options/unavailable capability, and 130 for
Ctrl-C cancellation. Reports preserve assertion/setup details and manifest
order; Bash remains usable after a batch. Refresh the launcher explicitly with
`make -C src/cli-rt guest-test-install`, rebuild the page, and use
`make -C src/cli-rt guest-test-check` for native/production-worker parity.

Keep page generation, focused checks and browser acceptance separate:

```sh
./start.sh --html-bash
./start.sh --html-check
python3 tests/test-corpus-bash.py build/html-rt/bash.html
node tests/c-engine-vfs-browser.cjs build/html-rt/bash.html
make -C src/cli-rt vfs-check
```

The shell sweep checks guest redirection (`--limit N` bounds diagnosis); the
compiled-engine VFS probe checks exact mounted bytes and metadata. `--html-check`
uses Node without launching Chromium. `./start.sh --html-browser-full` explicitly
launches Chromium; manual offline-browser evidence is separate from worker VM
results and is useful on hosts where browser automation crashes.

The packaging harness extracts the embedded tar via `readOfflinePackage`
without opening a browser. Its synchronous checkpoints append to
`build/html-rt/frontend-packaging.log`, overridable with
`WASTE_FRONTEND_PACKAGING_LOG`. Compare large buffers with `Buffer.equals` and
concise path/length diagnostics to avoid constructing enormous failure diffs.
The last checkpoint narrows a crash location but does not establish its cause.

## Wasm32 Varargs and Callback Adapters

Decode C varargs through the guest ABI before calling a fixed-width host import.
Guest `open(path, flags, ...)` extracts mode only for `O_CREAT` and calls
`waste_kernel.open_v1(path, flags, mode)`; the kernel applies the creation mask
(initially `0022`). Bash's variadic `fcntl` passes a guest argument-area pointer.
`waste_kernel.fcntl_varargs_v1` reads the integer there, while `fcntl_v1` and
compatibility `env.fcntl` take a direct integer. Mistaking the pointer for an
`F_DUPFD` minimum produces descriptor exhaustion during shell redirection.
Verify guest redirection as well as lower-level VFS access.

Function-table calls must retain exact Wasm signatures. Bash's `pop_scope`
cleanup callback returns void but its unwind-protect dispatcher expects an
integer result; the launch builder supplies a typed adapter. Do not weaken
validation to make such calls succeed. Compiled guest ABI/layout probes and
shared native/browser read-to-prompt sessions cover different parts of this
boundary; use both when changing it.

## Browser and Differential Testing

After native gates pass, run focused WAST worker tests from their generated
payload and exercise the unified offline Bash/test page:

```sh
node tests/c-engine-browser-runtime.cjs
./start.sh --html-bash
node tests/c-engine-offline-browser.cjs --page=bash
```

Interactive runtime changes also run:

```sh
./start.sh --html-bash
node tests/c-engine-bash-browser-runtime.cjs build/html-rt/bash.html
```

Supported official Wasm/WAT/WAST language tests may be cross-checked against
the OCaml reference interpreter via `./start.sh --ocaml-reference`; see
[OCaml reference interpreter build](#ocaml-reference-interpreter-build). Use native/browser C
checks for kernel, shared ABI, scheduler, signal, process and libc behavior.

Each scheduled test needs a fresh store and kernel.  Run isolation-sensitive
fixtures in different orders and concurrency settings.  Imported-memory tests
must still observe intentional aliases inside one sandbox.

### OCaml reference interpreter build

The OCaml reference interpreter in `submodules/wasm-spec/interpreter` was used
as a language reference while implementing the WAT/WAST portions of the C
engine. Use it only to cross-check Wasm/WAT/WAST language behavior. WASTE
runtime and kernel development belongs in C; no additional kernel development
is planned in OCaml. The procedure below reproduces upstream reference behavior.

The upstream interpreter requires:

- `opam`
- `dune`
- `menhir`
- OCaml &ge; 4.12 (per the upstream README)

Install them through your system package manager and `opam` as usual.

From the repository root:

```sh
./start.sh --ocaml-reference
```

That subcommand invokes `make -C submodules ocaml-test`, which:

1. Copies `submodules/wasm-spec/interpreter` to `build/ocaml-interpreter/`
   (via the `ocaml` target).
2. Runs `make` in the staged copy.
3. Runs `make test`, which executes the upstream spec test suite.
4. Logs output to `build/engine/logs/ocaml-reference.log`.

The individual targets are also available directly:

```sh
make -C submodules ocaml         # stage and build without the test suite
make -C submodules ocaml-test    # stage, build, and run the spec tests
make -C submodules ocaml-clean   # remove the staged copy
```

The checked-out submodule remains read-only; every generated build output
belongs under `build/`. The staged copy under `build/ocaml-interpreter/` is
disposable.

## Diagnostics and Error Quality

Use structured errors with a category, source location when available, and a
short stable message.  Never replace a useful decoder, validation, linker, or
trap result with a generic “module load failed.”  Freestanding formatting must
retain diagnostics needed by browser results.

For a failure cluster, identify the earliest phase that diverges:

1. command framing;
2. text parsing and deferred resolution;
3. binary encoding;
4. binary decoding;
5. semantic validation;
6. import resolution;
7. instantiation and initializers;
8. invocation; or
9. result comparison.

Use independent tools such as Binaryen validation only as a triage signal.
They do not replace the project validator, especially for proposal and WAST
assertion semantics.

## Common Failure Patterns

- Do not special-case a known fixture's exact source or argument values.
- Do not silently accept unsupported syntax or opcodes.
- Do not discard invalid expressions that an `assert_invalid` command needs to
  observe.
- Do not let one malformed WAST command terminate the rest of the file.
- Do not maintain duplicate LEB, import, comment, annotation, or string
  scanners.
- Do not compare module-local type indexes across instances.
- Do not copy explicitly imported memories or tables during ordinary linking.
- Do not retain guest-memory pointers across `memory.grow`, yield, or a browser
  callback.
- Do not locate the active process or module through mutable global state.
- Do not put POSIX descriptor or process policy in JavaScript.
- Do not send `close` or another descriptor-lifecycle operation to a host shim
  when `open`, `pipe`, `read`, and `write` use the engine kernel; all operations
  on one descriptor must update the same open-file description.

After shell or Python changes, run `bash -n start.sh`, bytecode checks for
changed `src/html-rt/tools/*.py`, and `git diff --check`.

Guest batch launchers delegate scheduling to the runtime while keeping file I/O
in the guest. `waste_kernel.test_suite_v1` is an opt-in shell capability; its
versioned, bounded arguments never authorize guest-selected host paths. The
engine owns copied request/reply buffers and validates the future reply range
before returning `EXEC_YIELD_HOST_IO`. Runtime adapters run isolated batches
from the installed directory or extracted package, supply a reply, and resume through ordinary C returns.
Batch workers leave the capability disabled. Keep enumeration cancellation in
the browser controller's state as well as the active-worker state; an interrupt
can arrive before a catalogue exists. Sequence IDs guard delayed replies and
cancels, and unrelated input/resize wakeups must wait for a batch reply.

Parse signed integer literals as unsigned magnitudes and negate in unsigned
arithmetic before converting the bit pattern to the signed storage field.
Negating an already-cast `INT64_MIN` invokes C undefined behavior even though
`-9223372036854775808` is a valid WAT i64 literal. Keep decimal and hexadecimal
minimum-literal probes and run UBSan with `halt_on_error=1` so a recovered
sanitizer diagnostic cannot be mistaken for an assertion pass.


## Installed VFS Inputs

Use the current `src/vfs` tree as the filesystem definition. Native adapters
enumerate directories and open children with `openat`/`O_NOFOLLOW`; browser
packaging scans the same tree and the browser reuses the tarballjs file map.
Reject unsafe paths, symlinks, special files, oversized inputs and inconsistent
reads. Entry and byte bounds come from `src/config.h`. Derive inode IDs from
sorted paths, use guest root ownership, and preserve current modes and
nanosecond mtimes. Synthesize missing boot directories and WAT/WAST interpreter
nodes. Generate browser metadata and content hashes into the package for bounded
transport validation. Never require a checked-in inventory, source hashes or
SDK refresh before accepting file changes, and never wrap this tree in a second
custom archive. Build the page with `make -C src/html-rt bash-html` or
`./start.sh --html-bash`; both package the tree without reinstalling guest files.

Mount complete inputs into a fresh kernel. Guest writes modify private kernel
files and cannot alter the installed tree or another test's filesystem. Native
batch children inherit a validated in-memory catalogue. The native shell holds
an open root directory and passes that capability to its batch companion,
retaining directory identity across rename/replacement without serializing
file contents to a temporary image. The companion revalidates current files;
this directory capability does not freeze external file-content edits.

### Guest signal-mask operations

Use the fixed-width four-word guest signal-set codec, not a native host
`sigset_t` or an assumed compiled C handler pointer. Copy input before writing
an old-mask output so aliased buffers remain valid. Validate complete guest
ranges before writes and commit process-mask changes only after output succeeds;
rejected operations must preserve both output canaries and kernel state. Mask
updates exclude SIGKILL/SIGSTOP; pending queries observe blocked queued signals
without consuming them. Keep handler delivery at the existing pselect boundary
until a separate change establishes other safe points and C function-pointer
semantics. WAST tests real imported state; compiled C clients separately preserve
public-header/layout and canary coverage.

## Retain segment declarations or reject the resource bound

Correct unsigned segment operands cannot compensate for dropped declarations.
Take data/element capacities from `src/config.h`, with the same bound in parser
metadata, names, loader and evaluator state. Every append form checks capacity;
excess declarations fail parsing and unretained data payloads are freed. Resource
bounds remain hard parse errors inside `assert_invalid`, rather than satisfying
semantic-invalid expectations with truncated syntax.

Verify initialization and drop behavior as well as setup acceptance: the portable
segment probe checks distinct values at 31/32/63/64/127, numeric and deferred names,
32/64-bit memory/table operands, post-drop traps and zero-length operations.
Capacity overflow is a C implementation boundary. The existing fixed capacities
do not promise arbitrary segment counts. Generate overflow probes from the
current configured bound instead of fixing the fixture size to a historical
limit.

## Preserve Element Segment Nullability

Bare function-index vectors and `elemkind func` declare non-null `(ref func)`;
explicit `funcref` declares nullable `(ref null func)`. Keep the distinction in
active, passive and declarative WAT metadata. Synthesized table-shorthand
segments inherit the table's declared type, including its nullability.

Binary element modes 0–3 imply `(ref func)`, mode 4 implies nullable `funcref`,
and modes 5–7 carry an explicit reference type. Store that declared type for
both active initialization and instruction validation. Do not infer a narrower
segment type because every current item happens to be a non-null function.
The text encoder may use expression vectors with an explicit non-null type;
it need not produce byte-identical encodings.

Verify table contents and lifetime as well as setup: bare/empty vectors,
nullable null slots, passive `table.init`, dropped active/declarative segments,
post-drop traps, table64 and all eight binary modes. Reject nullable segments
when targeting non-null tables even if their vectors contain only `ref.func`.


## Standard language-test host tables

`spectest.table` and `spectest.table64` are distinct nullable funcref tables,
with initial size 10 and maximum 20. Preserve address width separately from
reference type: standard table64 imports use 64-bit indices. Resolve names to
store-owned objects and let the existing loader enforce width/type/limits;
do not grow host tables to satisfy incompatible imports. Zero-initialized
slots have no owner and represent null references.

Aliases within one script share size, contents and cross-module function
owners; separately scheduled scripts receive fresh host objects. Include both
host tables in checkpoint capture even before a module imports them. Store
teardown releases entries after engines, and snapshots restore object identity
and original bounds/contents. Keep private checkpoint ownership checks as well
as portable WAST bounds/growth/import and isolation probes. Run both manifest
orders sequentially and concurrently, and verify C-encoded modules in source
order so prior actions establish shared table state.


## Plain and folded bulk instructions

Give plain bulk operators grammar productions that emit their opcode and
immediates; permissive generic instruction handling must not silently omit
`data.drop` or `elem.drop`. `table.init` abbreviates the table index to zero,
or accepts explicit table/element indices in text order. Binary encoding places
the element index first. Keep table and element fixups in their own index spaces
and retain their original source locations, including forward names and mixed
numeric/named forms. Folded numeric/numeric and numeric/named productions must
emit the same operation as their plain counterparts. Preserve instructions with
missing stack operands so binary validation sees them; reject missing text
immediates or unresolved symbols through normal text parsing.

Compare effects using distinct source functions/data and an untouched peer
table; successful setup alone cannot prove the instruction was emitted. Check
both address widths, repeated drops, post-drop traps and zero-length operations.
Pair invalid-module assertions with quoted malformed-text assertions.
`tests/test-suite-flat-bulk.wast` supplies the portable checks; keep ordinary
rejection probes and resource-bound failures explicit too.


`table.copy` accepts no table indices (both zero) or two explicit indices;
`table.fill` accepts no index (zero) or one. Copy immediates stay in
**destination/source** order in both text and binary, unlike initialization's
reversed table/segment encoding. Preserve table references through deferred
index-space resolution, including forward names and all mixed numeric/named
forms. Give folded numeric forms explicit emission paths; even operand-free
fill/copy forms must reach binary operand validation.

Copy addresses use each table's width. The count is i64 only when both tables
are table64, otherwise i32; fill uses its table's width for address and count.
Check both overlap directions with distinct entries, no writes after a bounds
trap, zero-length boundary behavior, nulls, reference identity/nullability and
cross-module function owners. `tests/test-suite-table-copy-fill.wast` covers
portable valid/invalid/malformed cases; retain ordinary rejection probes
and sanitizer/leak gates.

## Exported table shorthand

Table element-list shorthand may carry an inline export and infer its minimum
size from the element list. Accept both table32 `(table (export "name")
funcref (elem ...))` and table64 `(table (export "name") i64 funcref (elem
...))` forms. Preserve the reference type, attach the synthesized active
element segment to the just-declared table, and emit the inline export through
the ordinary table export metadata. Test both address widths with indirect
calls, then import the exported table from a second module to verify aliasing
and shared contents. `tests/test-suite-exported-table-shorthand.wast` covers
these paths in native C and the production browser runtime.
