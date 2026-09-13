# C Engine Implementation Techniques

## Purpose

This document collects reusable techniques learned while bringing the C
engine, WAT/WAST front end, linker, executor, guest libc, and browser runtime
to conformance.  It explains how to extend the implementation without
reintroducing earlier ambiguity, ownership, or portability failures.

System ownership and runtime boundaries are defined in
[architecture.md](architecture.md).  Unfinished staged work belongs in the
active plans rather than this document.

## Deterministic WAST Command Framing

A WAST file is a sequence of independently observable commands.  Before
invoking the module grammar, `wast_stream` scans one balanced top-level form
while tracking strings, escapes, line comments, nested block comments, and
parenthesis depth.  It classifies the command and passes exactly that byte
range to the parser.

This boundary provides deterministic recovery:

- a malformed command produces one recorded parse result;
- the next top-level command begins at a known byte boundary;
- registered modules and the current store survive successful commands; and
- partially built objects from a failed command are released once.

Do not use `setjmp`, `longjmp`, GLR ambiguity, or lexer error recovery to find
the next WAST command.  Recovery policy belongs to the command driver.

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

Preserve newlines and source offsets when removing an annotation envelope from
the module grammar's input.  An annotation error inside an assertion must be
reported through that assertion's malformed or invalid classification rather
than terminating the WAST stream.  The active parser refactor may move this
work into scanner tokens; it must preserve the same bounded validation and
locations rather than adding another independent raw-text scanner.

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

This design keeps the browser artifact independent of native unwinding and
permits deterministic cleanup on traps, exits, or cancellation.

## Explicit Yield and Resume

A host import that cannot complete synchronously returns `EXEC_YIELD`.  Before
returning, each active interpreter depth records the state needed to reenter
the same call.  The operand and control stacks live in engine-owned storage;
per-depth records retain PC, function identity, and control height.

On resume, reenter the original top-level invocation.  Saved frames recognize
the resume path, retry the pending import, and either complete or yield again.
Push original call arguments back before unwinding so the imported instruction
can be retried exactly.

Do not reset a partially resumed engine, retain pointers into guest memory
across the yield, or infer the blocked operation in JavaScript.  The POSIX
runtime should attach an explicit wait reason, stable handles, deadline, and
cancellation generation as described by the active `select`/`pselect` plan.

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
- Do not claim browser performance from native OCaml measurements.

After shell or Python changes, run `bash -n start.sh`, bytecode checks for
changed `src/html-rt/tools/*.py`, and `git diff --check`.
