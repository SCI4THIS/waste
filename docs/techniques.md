# C Engine Implementation Techniques

## Purpose

This document collects reusable techniques learned while bringing the C
engine, WAT/WAST front end, linker, executor, guest libc, and browser runtime
to conformance.  It explains how to extend the implementation without
reintroducing earlier ambiguity, ownership, or portability failures.

System ownership and runtime boundaries are defined in
[architecture.md](architecture.md).  Unfinished staged work belongs in the
active plans rather than this document.

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
exception.  The OCaml implementation's boxed continuation and exception path
was useful as a behavioral oracle but demonstrated why the C engine needs an
explicit frame-reuse operation.  Deep official tail-call fixtures and a large
bounded native loop should show constant C stack and no allocation per
transfer.

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

Keep the pinned GNU Coreutils submodule pristine. Repository changes belong in
`submodules/coreutils-waste.patch`; stage the patched source beneath
`build/coreutils/` and generate `configure` there with
`submodules/bootstrap-coreutils.sh`. That helper is the idempotent dependency
and bootstrap entry point. Configure output, the target sysroot, object files,
linked images, reports, and corresponding-source artifacts are generated
outputs and must remain below `build/`.

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

## Browser and Differential Testing

After native gates pass, build the same engine for the browser and exercise the
generated offline document through its worker harness:

```sh
./start.sh --html-test
node tests/c-engine-browser-runtime.cjs build/html-rt/test.html
```

Interactive runtime changes also run:

```sh
./start.sh --html-bash
node tests/c-engine-bash-browser-runtime.cjs build/html-rt/bash.html
```

Compare supported official tests with the OCaml oracle.  Run sequential and
threaded OCaml libc or DIY POSIX probes when changing shared ABI, scheduler,
signal, or process behavior.  Keep repository-owned interpreter changes in
`submodules/wasm-spec-i31-int32.patch`; do not commit them into submodule
history.

Each scheduled test needs a fresh store and kernel.  Run isolation-sensitive
fixtures in different orders and concurrency settings.  Imported-memory tests
must still observe intentional aliases inside one sandbox.

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
They do not replace the project validator or OCaml oracle, especially for
proposal and WAST assertion semantics.

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
- Do not claim browser performance from native OCaml measurements.

After shell or Python changes, run `bash -n start.sh`, bytecode checks for
changed `src/html-rt/tools/*.py`, and `git diff --check`.
