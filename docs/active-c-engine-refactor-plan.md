# C Engine Refactor Plan

This is an active implementation plan.  Durable system boundaries are defined
in [architecture.md](architecture.md), and established implementation practices
are collected in [techniques.md](techniques.md).

## Purpose

The C engine has reached broad enough feature coverage that its original
proof-of-concept layout is now obscuring important phase boundaries.  The
largest translation unit, `src/engine/waste_exec.c`, currently contains binary
decoding, validation, instantiation, mutable runtime state, and execution.
Several smaller files independently scan text or Wasm bytes, and `op.c` is
included directly into `waste_exec.c` rather than compiled as an independent
module.

This plan reorganizes the engine around explicit representations and one-way
phase transitions.  It is a structural refactor, not an opportunity to reduce
conformance coverage or change observable WAT, WAST, Wasm, POSIX, or browser
behavior.

The goals are to:

- keep text syntax in one reentrant Flex/Bison front end;
- keep Wasm binary decoding in one bounded binary decoder;
- share one immutable decoded-module representation between text and binary
  inputs;
- separate validation, instantiation, and execution;
- remove duplicate LEB, import, string, annotation, comment, and command
  scanners;
- make runtime opcode modules real compilation units with narrow interfaces;
- preserve allocation-free hot paths and the existing pause/resume model; and
- make malformed, invalid, uninstantiable, unlinkable, and trapping outcomes
  remain distinguishable to WAST assertions.

Equal source-file sizes are not a goal.  A small file is appropriate when it
owns one coherent facility.  The problem to solve is mixed ownership and
dependency direction, not line-count variance.

## Current State

The current engine source is approximately organized as follows:

| File | Approximate size | Current responsibilities |
| --- | ---: | --- |
| `waste_exec.c` | 6,050 lines | Internal module and instance types, type relations, bounded binary reads, LEB decoding, section decoding, validation, function-body decoding, module loading, allocation, stacks, exceptions, GC objects, invocation, and dispatch |
| `op.c` | 1,787 lines | Stack helpers and numeric, conversion, SIMD, memory, and GC opcode helpers; textually included by `waste_exec.c` |
| `wast_runner.c` | 1,763 lines | Raw text scanning and normalization, annotation validation, quoted/binary-module retention, Flex/Bison invocation, result comparison, and assertion execution |
| `wast_linker.c` | 660 lines | Store and registry management, spectest imports, a second Wasm import scanner, linking, loading, and group encoding |
| `wast_encode.c` | 267 lines | Wasm binary writing, LEB encoding, and section encoding |
| `wast_simd.c` | 177 lines | SIMD mnemonic, opcode, and immediate metadata |
| `wast_stream.c` | 172 lines | WAST top-level command framing and recovery |
| `wast_types.h` | 404 lines | Values, text-module structures, script commands, assertions, and lexer state |

There are four important architectural issues.

### `waste_exec.c` combines distinct phases

The file contains at least these separable regions:

1. internal types and type relations;
2. Wasm byte and LEB readers;
3. module-section and instruction decoding;
4. validation;
5. loading and instantiation;
6. mutable instance and evaluator state; and
7. the execution loop.

This makes it difficult to test malformed binary handling independently of
runtime behavior, and makes decoded immutable data difficult to distinguish
from per-instance mutable state.

### `op.c` is not an independent module

`waste_exec.c` uses `#include "op.c"`.  The extracted code therefore remains
part of the same translation unit and depends on private types and functions
defined earlier in `waste_exec.c`.  It reduces the visible length of one file
without establishing an interface or ownership boundary.

### Text is scanned multiple times

Before Flex/Bison runs, `wast_runner.c` separately examines numeric literals,
UTF-8 strings, quoted and binary module payloads, annotations, comments, and
inline-module syntax.  `wast_stream.c` performs another structural scan for
comments, strings, parentheses, and command names.  The Flex lexer then scans
the transformed input again.

These paths can disagree about escaping, nesting, source locations, and command
boundaries.  They also make error recovery dependent on preprocessing behavior
outside the grammar.

### The parser interface is pure but its implementation state is not

`wast.y` declares a pure parser, but grammar actions use many file-static
`g_*` accumulators and fixup tables.  The lexer likewise has a static numeric
scratch buffer.  Concurrent parsers can therefore interfere even though the
generated Bison interface is reentrant.

## Phase and Representation Boundaries

The refactor should establish this data flow:

```text
WAT ---- Flex/Bison ----> module builder ----> immutable wasm_module
                                                  |
Wasm ---- binary decoder -------------------------+
                                                  |
                                             validate
                                                  |
                                             instantiate
                                                  |
                                               execute

immutable wasm_module ---- binary encoder ----> .wasm bytes

WAST ---- command driver ----> module/action/assertion commands
                                  |
                                  +---- parse, execute, and record each command
```

The important common point is `wasm_module`.  Both a decoded binary and a
parsed text module produce this representation.  Validation consumes it and
records resolved instruction and control-flow metadata.  Instantiation creates
mutable memories, tables, globals, segments, GC objects, exception objects,
and evaluator state from a validated module.

WAT compilation may encode the module to canonical Wasm bytes, but normal WAST
execution need not encode text to bytes only to decode those same bytes
immediately.  Binary and quoted-module assertions still use the binary decoder
or text parser at the phase required by the assertion.

## Proposed Source Layout

The exact public names can be adjusted while implementing the plan, but the
ownership boundaries should remain:

```text
src/engine/
  include/
    waste_engine.h
    waste_error.h
    waste_value.h

  wasm/
    wasm_module.h
    wasm_reader.c
    wasm_reader.h
    wasm_writer.c
    wasm_writer.h
    wasm_leb.c
    wasm_leb.h
    wasm_opcode.c
    wasm_opcode.h
    wasm_decode.c
    wasm_decode.h
    wasm_encode.c
    wasm_encode.h
    wasm_validate.c
    wasm_validate.h

  text/
    wat_lexer.l
    wat_parser.y
    wat_context.c
    wat_context.h
    wat_builder.c
    wat_builder.h
    wat_literal.c
    wat_literal.h

  script/
    wast_command.c
    wast_command.h
    wast_stream.c
    wast_stream.h
    wast_assert.c
    wast_assert.h
    wast_runner.c
    wast_runner.h

  runtime/
    runtime_internal.h
    store.c
    store.h
    instantiate.c
    instantiate.h
    execute.c
    execute_numeric.c
    execute_memory.c
    execute_table.c
    execute_simd.c
    execute_gc.c
    execute_exception.c
```

Generated Flex/Bison outputs remain under `build/engine/gen/`.

### Public API

The public headers expose values, structured errors, module handles, instance
handles, invocation, and lifecycle operations.  They must not expose parser
scratch state, decoder cursors, evaluator stack layouts, or mutable internal
module structures.

### Wasm binary layer

The `wasm/` layer owns byte-level format handling:

- bounded readers and sub-readers;
- canonical and malformed LEB handling;
- bounded names, vectors, limits, and value types;
- section order, size, and content decoding;
- instruction opcode and immediate decoding;
- canonical binary encoding; and
- immutable module and decoded-instruction storage.

`wast_linker.c` must stop scanning the import section independently.  Linking
should consume import declarations already exposed by the decoded module.

The reader and writer APIs should be deliberately small.  Every read returns a
status and advances only on success.  Sub-readers enforce section and function
body boundaries.  Errors carry a byte offset and a stable classification.

### Text layer

The `text/` layer owns WAT and the shared syntax used within WAST modules.  The
Flex scanner and Bison parser recognize syntax; a context-owned builder records
module declarations, instruction trees or arrays, names, source locations,
and unresolved references.

The grammar should call builder operations rather than directly mutating large
file-static structures.  Name resolution, type-use canonicalization,
constant-expression checking, and other semantic rules run after syntactic
recognition.

### Script layer

The `script/` layer owns WAST commands and their sequential semantics:

- module definition and registration;
- invocation and global access;
- assertion classification and comparison;
- WAST command boundaries and recovery; and
- WAT versus WAST failure policy.

WAT mode consumes one complete module and fails fast.  WAST mode recognizes
one top-level command, parses and executes or records it, then continues with
the next command.  A malformed command becomes an observable command result
where the enclosing assertion permits it; it must not corrupt the parser state
for the next command.

### Runtime layer

The runtime layer owns mutable state and execution.  An immutable validated
module is distinct from each module instance.  A store owns registered
instances and explicit imported-object aliases.  Execution state owns operand,
control, call, yield, signal, and jump snapshots.

Opcode helpers should receive an explicit execution context rather than rely
on private names made visible by including another C source file:

```c
typedef struct {
    waste_instance *instance;
    waste_thread *thread;
    waste_frame *frame;
    waste_value_stack *values;
    waste_error *error;
} waste_exec_context;
```

The central dispatch loop remains in `execute.c`.  Complex instruction
families can be compiled as separate modules.  Performance-sensitive scalar
operations may remain inline or be generated from shared opcode metadata, but
the decision must be based on native and browser measurements rather than on
source-file size alone.

## Flex/Bison Scope

### Text parsing that should move into the front end

The following handwritten preprocessing can be represented by the shared
Flex/Bison front end and context-owned helpers:

- nested block comments using a Flex start condition and context depth;
- line comments and whitespace;
- string escapes, UTF-8 checks, and length-aware byte strings;
- quoted identifiers;
- numeric token spelling and source ranges;
- annotations and annotation payloads;
- quoted and binary module payload capture;
- inline module-field syntax through a grammar entry point; and
- top-level command classification.

Binary strings must use a `{bytes, length}` value rather than a NUL-terminated
C string.  Numeric tokens should retain their raw spelling.  `wat_literal.c`
can perform exact integer, float, NaN, and underscore conversion after the
lexer identifies the token class.  This avoids placing complicated numeric
semantics inside grammar actions.

Annotations should be grammar values or ignorable lexer tokens as appropriate,
not source text deleted before parsing.  Inline modules should use a dedicated
start production instead of allocating and inserting a synthetic `(module ...)`
wrapper.

### Parsing that must not move into Flex/Bison

Wasm binary decoding is not text parsing and should not be implemented in the
WAT/WAST grammar.  It includes:

- LEB128 integers;
- section identifiers, sizes, and ordering;
- binary value, heap, field, table, memory, and tag types;
- binary opcode prefixes and immediates;
- malformed binary encodings; and
- binary constant expressions.

The browser and native engine must continue to accept raw `.wasm` independently
of the text front end.  WAST binary-module assertions also depend on precise
binary error classification.  Flex and Bison are technically capable of
receiving arbitrary bytes, but using the text grammar for the Wasm binary
format would conflate independent specifications and weaken bounded-reader
guarantees.

### Semantics that must remain outside grammar actions

The parser should accept syntactically representable invalid programs so WAST
assertions can observe the correct later failure phase.  The following remain
ordinary C passes:

- index and name resolution;
- type and subtype validation;
- constant-expression validation;
- import compatibility and linking;
- module instantiation and segment initialization;
- assertion phase and message matching; and
- instruction execution.

This keeps WAT and WAST on the same grammar while allowing WAT to fail fast and
WAST to continue command by command.

## WAST Command Streaming

The deterministic command boundary remains useful, but it should not remain a
second partially independent lexer.  The shared scanner should own string,
comment, annotation, parenthesis, line, and column state.  At top-level depth
zero it can expose a command boundary to the WAST driver.

There are two acceptable implementations:

1. use a pure Bison push parser and return a completed command whenever the
   shared scanner reaches a top-level boundary; or
2. use the scanner to produce a bounded source slice for one command, then run
   a fresh pure parser for that slice.

The second approach is initially simpler and naturally isolates recovery.  It
retains deterministic streaming without `setjmp`, `longjmp`, or GLR parsing.
The scanner state must be reset or advanced explicitly after every command,
and every parser-owned allocation must be released on both success and error.

## Opcode Metadata

Mnemonic lookup, binary opcode identity, prefix, immediate shape, feature, and
basic classification should have one source of truth.  This may be an X-macro
definition or a generated table consumed by:

- the text instruction builder;
- the binary instruction decoder;
- the binary encoder;
- the validator; and
- diagnostic and profiling output.

The metadata must not attempt to encode all semantic validation or execution in
one table.  SIMD metadata can remain a small cohesive module or become one
generated subset of the common opcode table.  Its current small size is not a
reason to merge it into the evaluator.

## Ownership Model

The refactor should make these ownership levels explicit:

1. `wasm_module` owns immutable decoded declarations, decoded instructions,
   names needed for diagnostics, and validation metadata.
2. `waste_store` owns registered module instances and host import definitions.
3. `waste_instance` owns mutable globals, memories, tables, segment state,
   runtime tags, and instance-local objects.
4. `waste_thread` owns schedulable execution state, including frames, stacks,
   fuel, signals, yields, and pending host operations.
5. `wast_script` owns commands and retained source payloads for one test
   sandbox.
6. `wat_context` owns every lexer, parser, builder, name, fixup, and temporary
   allocation for one parse.

No parser or engine-global mutable state should remain.  Immutable opcode
metadata may remain process-global.

## Migration Stages

Each stage must preserve the last known conformance baseline before proceeding.
Avoid combining source moves with semantic changes unless a regression test
first demonstrates the required behavior.

### Stage 1: Establish common binary primitives

**Status: complete (2026-09-12).**

- Add bounded `wasm_reader` and `wasm_writer` APIs.
- Move unsigned and signed LEB handling into one tested implementation.
- Preserve canonical/malformed distinctions and precise byte offsets.
- Migrate `wast_encode.c` to the writer.
- Migrate `waste_exec.c` binary reads to the reader.

Gate:

- native reader tests cover truncated, overlong, overflowed, signed, unsigned,
  32-bit, 33-bit, and 64-bit LEB cases;
- warnings-as-errors, ASan, and UBSan pass; and
- the existing native and browser WAST results do not change.

Completion record:

- `src/engine/wasm/wasm_leb.[ch]` now owns shortest-form unsigned and signed
  LEB encoding plus bounded u32, u64, i32, i64, and s33 decoding.  Decode
  results distinguish success, truncation, and invalid encodings, and a failed
  decode does not advance its input cursor.
- `src/engine/wasm/wasm_reader.[ch]` provides bounded bytes, scalar LEB,
  remaining-length, absolute-offset, and sub-reader operations.  Module
  sections and function bodies are constrained by sub-readers.
- `src/engine/wasm/wasm_writer.[ch]` provides overflow-checked dynamic output,
  byte and byte-range emission, canonical LEB emission, ownership transfer,
  and disposal.
- `waste_exec.c` uses the shared reader for module sections, section contents,
  function bodies, and instruction immediates.  Its private reader and six LEB
  implementations were removed.
- `wast_encode.c` uses the shared writer and LEB encoder.  Its private dynamic
  writer and three LEB implementations were removed.
- The CLI and browser Makefiles compile the same three shared binary modules.
  The browser Makefile also gives shared and browser-specific libc objects
  distinct names, fixing the pre-existing `lib_stdio.o` and `lib_stdlib.o`
  duplicate-symbol link failure exposed by a clean browser build.
- `tests/wasm-binary-primitives.c` covers canonical round trips, known byte
  sequences, accepted non-short encodings, truncated input, width overflow,
  excessive length, cursor transactions, bounded sub-readers, and writer
  ownership for unsigned, signed, 32-bit, 33-bit, and 64-bit values.

Verified gates:

- `make -C src/cli-rt wasm-binary-primitives` passes with AddressSanitizer and
  UndefinedBehaviorSanitizer; LeakSanitizer remains disabled for the ptrace
  environment.
- `make -C src/cli-rt i32-smoke` passes with AddressSanitizer and
  UndefinedBehaviorSanitizer.
- The new modules and migrated encoder compile with warnings treated as errors
  for native and Wasm targets.
- `./start.sh --cli-test` passes all 97 core WAST files, including
  `binary-leb128.wast`.
- The regenerated offline C-engine dashboard passes the core, proposal, custom
  annotation, and DIY POSIX groups.  It retains only the previously recorded
  `libc-test/environment-boundaries.wast` result mismatch and introduces no new
  browser failures.
- `./start.sh --html-bash` regenerates the offline C-engine Bash page and its
  browser smoke test passes 5/5.

Stage 1 intentionally left the linker's independent import scanner in place.
Stage 2, recorded below, subsequently replaced it with decoded declarations.

### Stage 2: Remove the linker import scanner

**Status: complete (2026-09-13).**

- Define decoded import declarations in `wasm_module`.
- Decode imports once in `wasm_decode.c`.
- Let the store/linker resolve the decoded declarations.
- Delete `bin_reader`, `read_leb`, and `scan_imports` from the linker.

Gate:

- cross-module function, global, memory, table, and tag imports pass;
- imported-memory aliasing remains explicit; and
- malformed imports are rejected by the decoder before linking.

Completion record:

- `src/engine/wasm/wasm_module.[ch]` defines owned decoded import
  declarations for functions, tables, memories, globals, and tags.  Each
  declaration retains its validated UTF-8 module/field names and complete
  binary descriptor, including type indices, value types, limits, mutability,
  and tag attributes.
- `src/engine/wasm/wasm_decode.[ch]` decodes section 2 with bounded readers,
  stable byte offsets, transactional LEB reads, explicit format/out-of-memory
  results, and exact section-boundary checks.  Binary value-type decoding also
  moved here so imports and the remaining executor decoder cannot disagree on
  reference-type encodings.
- `native_load_module` decodes imports before allocating bindings and resolves
  every input form from those declarations.  WAT-produced Wasm and literal
  `(module binary ...)` input no longer take different text/binary linking
  paths.
- `exec_load_decoded_with_imports` instantiates imports from the same decoded
  declarations after the type section is available.  The existing
  `exec_load_with_imports` API remains compatible by creating and disposing a
  decoded module internally.
- `bin_reader`, `import_request`, `read_leb`, `read_leb64`, `scan_imports`, and
  the text-metadata import-resolution loop were deleted from `wast_linker.c`.
  The old 512-import scanner ceiling and its incomplete tag descriptor skip
  were removed with them.
- Function trampoline storage is retained whenever a failed instantiation
  returns a live orphan instance, preventing escaped imported function
  references from retaining freed linker context.
- `tests/wasm-import-decode.c` covers all five import descriptors, names,
  memory64/table limits, tag attributes, modules without imports, invalid
  UTF-8, truncated sections, trailing bytes, ownership, and cleanup.

Verified gates:

- `make -C src/cli-rt wasm-import-decode` passes with AddressSanitizer and
  UndefinedBehaviorSanitizer; LeakSanitizer remains disabled for the ptrace
  environment.
- `make -C src/cli-rt i32-smoke` passes its direct function import and explicit
  cross-instance reference/import checks with the same sanitizers.
- Native and Wasm targets compile successfully, with engine C sources using
  warnings-as-errors in the native build.
- `./start.sh --cli-test` passes all 97 core WAST files, including import,
  linking, malformed UTF-8 import-name, and malformed UTF-8 import-module
  coverage.
- The regenerated offline browser dashboard passes function, global, memory,
  table, exception-tag, multi-memory aliasing, memory64, SIMD, GC, custom
  annotation, and DIY POSIX groups.  It retains only the pre-existing
  `libc-test/environment-boundaries.wast` mismatch and introduces no new
  browser failures.
- `./start.sh --html-bash` regenerates the offline C-engine Bash page and its
  browser smoke test passes 5/5.

### Stage 3: Separate decoding from instantiation

**Status: complete (2026-09-13).**

- Split immutable declarations from mutable instance objects.
- Make binary decoding produce an uninstantiated `wasm_module`.
- Move memory, table, global, segment, tag, and object allocation to
  `instantiate.c`.
- Evaluate constant expressions only during instantiation.  Stage 4 will split
  the current combined constant-expression check/evaluation into validated IR;
  requiring that IR before extracting validation would invert the dependency
  between these two stages.

Gate:

- malformed, invalid, unlinkable, and uninstantiable assertions retain their
  classifications;
- one decoded module can be instantiated more than once without shared mutable
  state; and
- sandbox isolation tests pass.

Completion record:

- `wasm_decode_module` now produces an owned, uninstantiated `wasm_module`.
  It copies the input bytes, decodes imports, and builds a bounded section
  directory, so callers may release their input immediately and section framing
  is not rescanned by each instance.
- `wasm_module` owns the immutable source, decoded import declarations, and
  section directory.  `wasm_module_dispose` releases all three independently of
  any instances already created from it.
- `runtime/instantiate.[ch]` defines the module-to-instance boundary through
  `wasm_instantiate_module`.  The linker and compatibility loading APIs now
  decode an owned module first and enter the executor exclusively through this
  instantiation operation.
- Concrete allocation and initialization for defined memories, tables,
  globals, tags, passive data segments, element vectors, GC value arrays, and
  exception/GC object storage is centralized in `instantiate.c`.  Imported
  memories and tables remain explicit aliases and are never marked as owned.
- Constant-expression processing is named and scoped as
  `instantiate_constexpr`; global and table initializers plus active data and
  element offsets are evaluated only while creating an instance.  Its semantic
  checker remains co-located until Stage 4 creates the promised validated
  expression IR, avoiding a temporary second validator.
- Start functions and active element/data application remain after allocation
  in the instantiation transaction.  A trapping start or out-of-bounds active
  segment retains the existing live-orphan behavior required for escaped
  references.
- The native sanitizer smoke test now decodes one module once, instantiates it
  twice, disposes the decoded module, and then verifies independent mutable
  globals, memory writes, memory growth, and identical data-segment
  initialization in both surviving instances.
- Decoder tests verify that an owned module is independent of its caller's
  source buffer and that its section directory and all owned allocations are
  released by disposal.

Verified gates:

- `make -C src/cli-rt wasm-import-decode` passes with AddressSanitizer and
  UndefinedBehaviorSanitizer, including owned-source and section-directory
  checks.
- `make -C src/cli-rt i32-smoke` passes the decode-once/double-instantiation
  isolation test with the same sanitizers; LeakSanitizer remains disabled for
  the ptrace environment.
- Native and Wasm targets compile successfully, with engine C sources using
  warnings-as-errors in the native build.
- `./start.sh --cli-test` passes all 97 core WAST files, preserving malformed,
  invalid, unlinkable, uninstantiable, trap, and success classifications.
- The regenerated offline browser dashboard passes the core, proposal,
  custom-annotation, and DIY POSIX groups, including the existing spectest
  isolation probes.  It retains only the pre-existing
  `libc-test/environment-boundaries.wast` mismatch and introduces no new
  browser failures.

### Stage 4: Extract validation

- Move type relations and instruction stack/control validation to
  `wasm_validate.c`.
- Replace the current inconclusive validator path with explicit feature
  coverage or a structured unsupported result.
- Record resolved branches, calls, types, and immediate metadata in the
  immutable instruction representation.

Gate:

- official `assert_invalid` cases match the OCaml oracle;
- valid functions are decoded and validated only once; and
- execution does not repeat static type checks in its hot loop.

### Stage 5: Split runtime execution

- Introduce `runtime_internal.h` and an explicit execution context.
- Compile opcode implementation files independently; remove `#include "op.c"`.
- Keep dispatch, fuel, yield, resume, and tail-call transitions in `execute.c`.
- Move numeric, memory, table, SIMD, GC, and exception helpers to their owned
  modules.
- Keep allocation outside hot instruction paths.

Gate:

- native and browser execution suites match the pre-refactor result JSON;
- the Bash prompt/input/command/exit smoke test passes;
- tail calls retain bounded frame and allocation behavior; and
- performance does not regress beyond an agreed measurement tolerance.

### Stage 6: Make parser state genuinely reentrant

- Introduce `wat_context` and move every `g_*` grammar accumulator into it.
- Pass the context through `%parse-param` and `%lex-param`.
- Remove static lexer scratch buffers.
- Give tokens owned or source-slice values with explicit lengths.
- Move grammar helper implementations into builder and literal modules.

Gate:

- two parser instances can run concurrently without shared mutable state;
- parser allocation and error paths are sanitizer-clean; and
- all existing WAT/WAST files retain their parse outcome and location quality.

### Stage 7: Remove raw textual preprocessing

- Handle nested comments and strings in the shared lexer.
- Represent annotations in the lexer/grammar path.
- Capture raw module payloads as length-aware semantic values.
- Add an inline-module-fields grammar entry point.
- Remove source rewriting and numeric normalization from `wast_runner.c`.

Gate:

- annotation, name annotation, branch hint, malformed UTF-8, numeric literal,
  quoted module, binary module, and folded-instruction suites match the OCaml
  oracle; and
- diagnostics still report original source locations.

### Stage 8: Consolidate WAST streaming and assertions

- Make the command driver consume boundaries from the shared scanner.
- Parse one command in an isolated context.
- Move result comparison and assertion execution into `wast_assert.c`.
- Keep orchestration and registry transitions in `wast_runner.c`.

Gate:

- a malformed command does not prevent later WAST commands from running;
- WAT mode still fails the entire input immediately;
- assertion counts and order match source order; and
- the native and static HTML dashboards report identical results.

## Testing and Delivery Rules

For every stage:

- build the native CLI and browser Wasm from the same engine sources;
- run warnings-as-errors, AddressSanitizer, and UndefinedBehaviorSanitizer;
- run all official core and proposal WAST groups currently covered;
- compare supported behavior with the OCaml reference interpreter;
- run sandbox isolation, DIY POSIX, libc, Bash, and browser smoke tests when the
  changed boundary can affect them;
- keep the dashboard a self-contained `file://` document;
- run `git diff --check`; and
- record any intentional result change before proceeding.

Useful phase-specific unit tests should be added as the interfaces appear:

- binary reader and writer round trips;
- malformed LEB and section boundaries;
- decoder-only module fixtures;
- validator-only instruction fixtures;
- multiple instantiations of one decoded module;
- parser concurrency and error cleanup;
- WAST recovery after a malformed command; and
- opcode metadata agreement across text lookup, binary decode, and encode.

## Expected Outcome

At completion, `execute.c` remains the central hot loop but no longer owns text
parsing, binary decoding, semantic validation, linking, or instantiation.
Flex/Bison owns WAT/WAST syntax without static parser state or external source
rewriting.  The binary reader owns all LEB and section decoding.  The linker
consumes decoded imports.  Both text and binary inputs converge on one
validated immutable module representation, and runtime instances contain only
the mutable state required to execute it.

This separation should make future proposal work smaller and safer: adding an
opcode requires coordinated metadata, decode, validation, encode, and execution
support, but no longer requires editing one multi-phase 6,000-line file or
duplicating parsing logic across the runner and linker.
