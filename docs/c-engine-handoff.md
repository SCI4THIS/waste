# C Engine Handoff

## Proven Result

The C direction is validated in Firefox using the self-contained
`build/c-tail-poc/tail-call-poc.html`. With five million guest tail transfers:

| Operation | Time | Transfers/second | Frames | Run allocations |
| --- | ---: | ---: | ---: | ---: |
| `return_call` | 1,122 ms | 4,456,328 | 1 | 0 |
| `return_call_ref` | 465 ms | 10,752,688 | 1 | 0 |

`return_call_ref` executed 40,000,006 decoded guest instructions and returned
zero. These are browser wall-clock measurements from `performance.now()`, not
native timings. Preserve this fixture as a performance regression gate.

## Current Implementation

`src/c-engine/waste_tail.c` has a bounded binary reader, strict subset decoder,
fixed-width instructions, numeric PCs, structured errors, fuel, export lookup,
and a single-frame execution loop. A tail transfer replaces the active function,
argument, value-stack cursor, and PC. It performs no allocation.

The accepted subset is `(i64) -> i64` functions, function exports,
`local.get 0`, `i64.const`, `i64.eqz`, `i64.sub`, `if`/`else`/`end`, `ref.func`,
`return_call`, and `return_call_ref`. The element section is accepted only to
support the declarative `ref.func` fixture; this is not full validation.

`src/c-engine/browser.c` is a freestanding adapter with a bump allocator that
grows engine memory in 64-KiB Wasm pages. It is suitable only for this
short-lived proof. Do not confuse engine memory with future guest linear-memory
backing objects.

## Reproduction

```sh
./start.sh --c-tail-poc
```

This runs the native five-million-transfer gate, rejects a truncated module,
builds the freestanding engine Wasm, and regenerates the static browser page.
The same action is available from the TUI. ASan/UBSan can be run with:

```sh
make -C src/c-engine sanitize
ASAN_OPTIONS=detect_leaks=0 \
  build/c-tail-poc/waste-tail-poc-sanitize \
  build/c-tail-poc/tail-call.wasm direct 100000
```

LeakSanitizer is disabled only because the managed execution environment uses
ptrace; AddressSanitizer and UndefinedBehaviorSanitizer remain active.

## Next Safe Expansion

1. Split the current file into `binary/`, `core/`, and `runtime/` without
   changing benchmark behavior.
2. Add generated opcode metadata shared by decoder, validator, executor, WAT,
   and disassembler.
3. Generalize function signatures, locals, and values; then add ordinary calls
   with compact reusable frames.
4. Add blocks, loops, branches, globals, tables, and memories incrementally,
   comparing each fixture with the OCaml oracle.
5. Introduce sandbox/store/process/thread handles before imports or POSIX state.
6. Model guest memories as refcounted backing objects so imports preserve
   identity and `fork` can later clone by identity and use copy-on-write.
7. Add the C engine as an optional dashboard backend. Do not switch engines in
   the middle of a test or process.

Keep unsupported features explicit. Do not broaden the parser by skipping
unknown standard sections or opcodes, and do not optimize away fuel/event
boundaries needed for pause, signals, and blocking operations.

## WAST Core-Module Encoder Status

### Current browser conformance baseline (2026-09-09)

The Wasm-compiled C WAST engine now passes all 97 top-level
`submodules/wasm-spec/test/core/*.wast` files through the self-contained
browser harness.  The separate multi-memory proposal suite also passes all 41
`submodules/wasm-spec/test/core/multi-memory/*.wast` files natively and through
the same browser worker.  The core SIMD suite passes all 59
`submodules/wasm-spec/test/core/simd/*.wast` files (24,335 assertions) both
natively and through the freestanding browser engine.  The memory64 proposal
suite passes all 25 `submodules/wasm-spec/test/core/memory64/*.wast` files in
both paths (8,409 native command results; 7,946 explicit assertion commands in
the generated browser dashboard).  The GC proposal suite passes all 17
`submodules/wasm-spec/test/core/gc/*.wast` files in the mmap, native execution,
and freestanding browser paths (591 explicit assertions).  The exception
handling suite passes all 4 `submodules/wasm-spec/test/core/exceptions/*.wast`
files in those same paths: 90 native command results and 70 explicit action
assertions in the browser dashboard.  Later paragraphs in this section preserve
the incremental implementation history; their smaller pass counts are no
longer the current baseline.

The multi-memory work adds deterministic folded load/store boundaries, indexed
load/store/size/grow and bulk-memory encoding, named data-segment resolution,
DataCount emission before code, and runtime semantics for `memory.init`,
`data.drop`, `memory.copy`, and `memory.fill`.  Reproduce its browser gate with:

```sh
make -C src/c-engine WAST_BUILD_DIR=../../build/c-engine wast-native wast-browser
python3 tools/generate-c-engine-tests.py \
  --runner build/c-engine/waste-wast \
  --wasm build/c-engine/waste-wast.wasm \
  --tests submodules/wasm-spec/test/core/multi-memory \
  --output build/c-engine/browser-tests-c-engine-multi-memory.html
node tests/c-engine-browser-runtime.cjs \
  build/c-engine/browser-tests-c-engine-multi-memory.html
```

The SIMD implementation shares one mnemonic/opcode/immediate table between
the text encoder and binary decoder.  Its deterministic grammar boundaries
separate lane, shuffle, and memory immediates while leaving operand and result
checking in the C validator.  It covers standard vector integer and floating
operations, conversions, lane-width operations, and SIMD memory accesses.
Reproduce the dedicated offline browser gate with:

```sh
make -C src/c-engine WAST_BUILD_DIR=../../build/c-engine wast-native wast-browser
python3 tools/generate-c-engine-tests.py \
  --runner build/c-engine/waste-wast \
  --wasm build/c-engine/waste-wast.wasm \
  --tests submodules/wasm-spec/test/core/simd \
  --count \
  --output build/c-engine/browser-tests-c-engine-simd.html
node tests/c-engine-browser-runtime.cjs \
  build/c-engine/browser-tests-c-engine-simd.html
```

Memory64 uses 64-bit limits and memarg offsets end to end, selects i32 or i64
address operands from each memory or table, and implements the mixed-address
rules for bulk memory and table instructions.  The encoder retains the
2^16-page limit for memory32 and uses the proposal's 2^48-page limit for
memory64; the validator likewise rejects offsets wider than u32 only when the
selected memory is memory32.

Reproduce the memory64 offline-browser gate with:

```sh
make -C src/c-engine WAST_BUILD_DIR=../../build/c-engine \
  wast-native wast-browser wast-mmap-test
build/c-engine/wast-mmap-test \
  submodules/wasm-spec/test/core/memory64/*.wast
python3 tools/generate-c-engine-tests.py \
  --runner build/c-engine/waste-wast \
  --wasm build/c-engine/waste-wast.wasm \
  --tests submodules/wasm-spec/test/core/memory64 \
  --count \
  --output build/c-engine/browser-tests-c-engine-memory64.html
node tests/c-engine-browser-runtime.cjs \
  build/c-engine/browser-tests-c-engine-memory64.html
```

GC types retain recursive-group, finality, supertype, field storage, and
mutability metadata through text and binary forms.  Validation covers declared
subtypes, structural recursive equivalence, function variance, constant
expressions, GC instruction operands, casts, and branch refinement.  Runtime
objects are engine-owned structs or arrays with dynamic reference-type
metadata; the executor implements struct/array construction and access, packed
fields, data/element initialization, bulk array operations, i31 references,
reference tests/casts/equality, and any/extern conversions.  Reproduce the GC
offline-browser gate with:

```sh
make -C src/c-engine WAST_BUILD_DIR=../../build/c-engine \
  wast-native wast-browser wast-mmap-test
build/c-engine/wast-mmap-test \
  submodules/wasm-spec/test/core/gc/*.wast
python3 tools/generate-c-engine-tests.py \
  --runner build/c-engine/waste-wast \
  --wasm build/c-engine/waste-wast.wasm \
  --tests submodules/wasm-spec/test/core/gc \
  --count \
  --output build/c-engine/browser-tests-c-engine-gc.html
node tests/c-engine-browser-runtime.cjs \
  build/c-engine/browser-tests-c-engine-gc.html
```

Exception handling is represented as a distinct executor result rather than a
trap.  The carrier preserves the thrown tag's runtime identity and typed
payload across direct, indirect, reference, and imported calls.  `try_table`
selects the innermost first matching `catch`, `catch_ref`, `catch_all`, or
`catch_all_ref`; reference catches materialize engine-owned exception objects
that `throw_ref` can rethrow.  Catch label immediates are resolved against the
labels outside the `try_table`, while branches in its body also see the
`try_table` label.  Validation checks tag function types, empty tag results,
throw operands, non-null exception references delivered by reference catches,
and every catch destination signature.  Reproduce the dedicated gate with:

```sh
make -C src/c-engine WAST_BUILD_DIR=../../build/c-engine \
  wast-native wast-browser wast-mmap-test
build/c-engine/wast-mmap-test \
  submodules/wasm-spec/test/core/exceptions/*.wast
python3 tools/generate-c-engine-tests.py \
  --runner build/c-engine/waste-wast \
  --wasm build/c-engine/waste-wast.wasm \
  --tests submodules/wasm-spec/test/core/exceptions \
  --count \
  --output build/c-engine/browser-tests-c-engine-exceptions.html
node tests/c-engine-browser-runtime.cjs \
  build/c-engine/browser-tests-c-engine-exceptions.html
```

The developing WAST path now uses dynamically grown per-module function
storage (bounded at 1024 functions) and emits canonical core sections in Wasm
order: type, import, function, table, memory, global, export, start, element,
code, and data. `tests/c-engine-many-functions.wast` is the capacity gate;
`tests/c-engine-sections.wast` covers declared types, a table, memory, global,
element segment, data segment, exports, and executable code. Both pass through
the self-contained browser C engine, and their generated modules pass an
independent Binaryen decode. `tests/c-engine-import-smoke.wat` independently
checks imported-function type and index encoding.

This is an encoder milestone, not core-suite completion. The earlier 30-file
measurement (10 preprocessing, 20 stopped in the grammar) is retained as
historical context; the broader current measurement is below. Some
preprocessing successes still use instructions unsupported by the executor.

The current top-level core corpus contains 97 `.wast` files. With the present
uncommitted grammar work, the strict mmap preprocessing gate accepts 85 and
reports 12 files with a grammar, translation, semantic-capture, or fixed-limit
error. The browser dashboard can be generated for the whole directory as a
diagnostic, but the normal `--c-engine-tests` command intentionally runs only
relaxed-SIMD and DIY POSIX directories ([`start.sh`](../start.sh)). The latest
full-core diagnostic passes 8 of 97 files and fails 89. Only `address.wast` and
`forward.wast` among those eight expose a nonzero emitted check count; the
other nominal passes reveal a command-accounting gap rather than meaningful
conformance. The dashboard currently embeds 18,108 checks, while a lexical
count finds roughly 20,030 assertion commands in the input corpus. Exact
accounting must come from the deterministic command scanner, because comments
and annotations can make a text-only count imprecise.

The dominant browser failures are module-load/validation gaps rather than
browser transport. Loading is currently eager and whole-module: one opcode
unsupported by `waste_exec.c` prevents every otherwise-supported export in the
same module from running. The freestanding `snprintf` stub also discards loader
diagnostics, reducing many concrete failures to the unhelpful `module load
failed`. A browser-host `WebAssembly.validate` audit of emitted groups found 91
nominally valid modules rejected and 37 `assert_invalid` modules accepted;
this is a useful encoder/validator triage signal, not a replacement for the
OCaml oracle or the C validator.

The current browser artifact is a WAST front end as well as an execution
artifact. Its link target contains the deterministic Flex/Bison parser,
encoder, command stream, runner, executor, and `browser_wast.c` adapter. The
HTML embeds the original `.wast` source and the worker calls
`waste_wast_run_script`; native preprocessing is used by `--count` only to
display expected assertion totals.

`token.wast` also exposed a parser-recovery ownership bug:
partially built modules could share data storage and be freed twice. The WAST
runner now frees duplicate segment/function allocations at most once, and this
fixture is an AddressSanitizer regression gate. The preprocessing input pass
also strips nested block comments and annotation forms while preserving
newlines; this brings `comments.wast` and `token.wast` through preprocessing.

The binary loader now performs stricter instantiation preflight: standard
sections must be unique and ordered, unsupported standard section IDs are
rejected, each supported section must be consumed exactly, and a start
function must have the required `() -> ()` type. These checks prevent malformed
modules from being accepted accidentally; they are validation hardening, not
the claim of complete core-suite semantics.

The deterministic WAST command boundary in `src/c-engine/wast_stream.[ch]`
scans balanced top-level forms while ignoring strings and nested comments,
classifies each command, and feeds one command at a time to the pure LALR
parser through `wast_parse_bytes`. Native and browser runners preserve the
store and retained module definitions across commands while isolating parser
failures, so one malformed command cannot truncate the rest of a WAST file.
Recovery policy remains in this C command boundary rather than lexer actions.

The C front end now also exposes `waste_wat_compile`, a transactional WAT
boundary: it requires exactly one parsed module, emits canonical Wasm only
after parsing succeeds, and returns no partial output on failure. WAST mode is
intended to use `wast_stream_run` plus `wast_parse_bytes` once cross-command
module/store state is factored out of parser globals, so assertion
classification and fail-fast-versus-streaming policy remain in C rather than
in lexer actions.

Global definitions now follow the OCaml parser's structural split:
`global_fields` recursively handles inline exports/imports, and the initializer
uses the same general instruction-list grammar as function bodies. A separate
C pass then enforces the approved constant opcodes and the declared global
result type. This lets WAST assertions retain invalid instruction sequences
without weakening fail-fast WAT compilation. The focused gate is
`tests/c-engine-global-constexpr.wast`. Parsed global initializer buffers are
terminator-free; only the binary module encoder appends the required `end`.
The post-parse pass now follows the OCaml `check_const` whitelist, including
scalar/vector constants, integer add/sub/mul, immutable `global.get`,
`ref.null`, `ref.func`, `ref.i31`, `struct.new[_default]`,
`array.new[_default|_fixed]`, and nullability-preserving extern conversions.
Struct/array type declarations retain field types, packed storage, mutability,
and defaultability so constructor operands are validated rather than merely
whitelisted. The module-complete pass runs after deferred references are
patched, which also rejects out-of-range `ref.func` targets. The encoder emits
the corresponding GC type definitions without confusing them with function
signatures. This initializer preprocessing/encoding work is now consumed by
the GC execution support described in the current baseline above.

Invalid initializer expressions are no longer discarded or promoted to a
whole-script parse failure when they occur inside `assert_invalid`. The parser
retains their raw instruction bytes, the module-complete validation pass stores
the first semantic rejection on the assertion's group, and browser-spec JSON
exports it as `module_assertion.validation_error`. The offline harness consumes
that rejection directly and does not instantiate the invalid module. Plain WAT
continues to fail fast with the same error. Unresolved initializer global/type/
function references use the same channel, while genuine malformed text remains
a syntax error handled at the deterministic WAST command boundary.

Function definitions now use the same recursive phase split as the OCaml text
parser: inline wrappers and type use lead into parameters, then results, then
locals, then the instruction list. The phase productions are flattened where
necessary so their nullable handoffs do not introduce LR conflicts. Inline
exports remain legal at phase boundaries for compatibility with existing
C-engine fixtures. `tests/c-engine-function-fields.wast` is the focused gate.

Folded `block`, `loop`, and `if` instructions now apply the same approach to
block type fields: type use, parameters, and results are parsed before the
instruction body. Composite lexer starts keep the nullable field/body handoff
deterministic, including `unreachable`, tail-call, `br_on_*`, and vector
constant forms. `tests/c-engine-block-fields.wast` is the focused native and
browser gate. The combined function/block work reduces the expected Bison
shift/reduce conflict count from 38 to 23 and moves strict top-level core
preprocessing from 63/97 to 85/97; all seven relaxed-SIMD files still pass.

The official core corpus now has a dedicated diagnostic dashboard command:
`./start.sh --c-engine-core-tests`. It writes
`build/c-engine/browser-tests-c-engine-core.html` and runs the same browser
harness over all top-level core WAST files. This deliberately does not change
the passing `--c-engine-tests` gate; core results currently expose parser and
engine coverage gaps (the latest run reports 89 failing files), while still
allowing every emitted command to be inspected in the offline HTML page.

Forward named function and function-type references now use bounded deferred
fixups. `tests/c-engine-forward-function.wast` and
`tests/c-engine-forward-type.wast` pass independent decoding and browser
execution; the matching unresolved-name fixtures must fail preprocessing with
`unknown function` or `unknown type` rather than encoding numeric sentinels.
Function, type, and global name tables retain empty entries so their positions
match their Wasm index spaces. Inline table/element shorthand is also expanded
into an actual table plus active element segment.

Deferred metadata now also covers table, memory, global, element-segment,
start, and standalone-export references. The combined
`tests/c-engine-forward-module-indices.wast` gate exercises forward exports,
an active element target, a global instruction, and a start function;
`tests/c-engine-forward-global-init.wast` covers a forward `ref.func` in a
global initializer. Named `table.init` operands are resolved in both the
element and table index spaces. Matching negative fixtures require explicit
`unknown ...` preprocessing errors.

Cross-module browser execution now preserves module IDs and `register` names.
The browser adapter retains provider engines, resolves a consumer's function,
table, memory, and global imports against registered exports, and keeps shared
extern owners alive until the test resets its registry. Actions with an
explicit module ID select that instance; unqualified actions return to the
latest module. `tests/c-engine-linking.wast` is the registered function-import
gate, while the existing extern provider/consumer fixtures cover linkage for
tables, memories, and mutable globals.

The reentrant lexer now tracks line and column positions, and every deferred
function, type, instruction-index, element, start, initializer, and standalone
export fixup retains its reference location. Unresolved-name diagnostics report
that source position; `tests/c-engine-deferred-location.wast` requires the
unknown function to be reported at `3:10`.

Declarative element shorthand now follows the OCaml text grammar: `(elem
declare func $f ...)` is normalized to a declarative `funcref` segment of
`ref.func` expressions. Nullable typed spellings `(ref null func)` and `(ref
null extern)` normalize losslessly to the core reference types already carried
by the C representation. The executor now decodes and validates expression-form
element sections (flags 4 through 7), installs active segments, and validates
passive and declarative expressions instead of silently skipping section 9.
`tests/c-engine-declarative-element.wast` and
`tests/c-engine-nullable-ref-types.wast` are the focused gates.

The C value-type representation now distinguishes nullable and non-null
function/extern references and reserves bounded values for nullable and
non-null indexed heap references. The encoder emits the `ref` type constructors
and signed heap types, while the executor decoder retains the same distinctions
for function signatures, imports, and globals. The expanded nullable-reference
fixture covers `(ref func)`, `(ref null $type)`, and `(ref $type)` in addition to
the legacy nullable shorthands, and Binaryen validates its output with GC and
reference types enabled.

Inline modules are now accepted as `assert_trap` subjects and represented as
module-instantiation expectations rather than function actions. The same group
metadata carries inline `assert_invalid`, `assert_malformed`, and
`assert_unlinkable` modules. Browser execution checks the loader status, with
instantiation traps distinguished from ordinary decode/link failures. Active
element and data bounds failures now return the trap category needed by these
assertions. `tests/c-engine-inline-module-assertions.wast` covers an
out-of-bounds element initializer and an unresolved inline-module import.

Table declarations and imports now use a dedicated reference-type production
that retains nullability and indexed heap identity. The encoder emits typed
table descriptors, the browser import scanner skips their variable-length heap
types correctly, and the executor decodes and compares the retained table type
instead of reducing it to `funcref` or `externref`. Imported indexed table
types are compared structurally against the provider's function signature,
not by their module-local numeric type indices. Element-section validation uses
the same representation. `tests/c-engine-indexed-table-types.wast` links
an exported `(ref null $type)` table into a second module and executes through
the consumer; Binaryen validates both generated modules with GC and reference
types enabled.

The official `linking.wast` now preprocesses all 133 checks without a grammar
error. Its browser run is not yet conformant: the remaining failures are in
module registration after expected instantiation failures, imported-table
state propagation, memory behavior, and detailed assertion/load semantics
rather than WAST parsing.

Script actions now retain whether they are an `invoke` or global `get`. Native
and browser runners resolve exported globals without invoking them and compare
their current values against assertion alternatives, including imported,
mutable, and explicitly module-qualified globals. The focused
`tests/c-engine-global-get-actions.wast` browser gate passes all four forms.
With global actions enabled, the current official `linking.wast` browser run
exposed a registration cascade rooted in the `$Mt` provider failing to decode
`call_indirect`. The decoder and executor now support typed indirect calls,
including table bounds, null entries, structural signature checks, argument
transfer, and result transfer. The browser adapter also supplies the standard
no-result `spectest` print functions used by linking fixtures. Consequently no
`unresolved registered module import` failures remain: the current run passes
101 of 136 emitted results. `tests/c-engine-call-indirect.wast` is the focused
execution gate.

Wasm-to-Wasm function bindings now retain the provider engine and declared type
index. Consumer loading structurally compares parameter and result types,
including indexed reference types across different module-local type spaces,
before accepting an import. Native host callbacks can explicitly remain
untyped, which is used for the standard `spectest` print helpers. The focused
`tests/c-engine-function-import-signatures.wast` gate accepts an exact signature
and rejects parameter and result mismatches. This raises the current official
`linking.wast` browser result to 103 of 136; one unexpectedly successful module
instantiation remains in a different extern-import category.

Invocation assertions now preserve `assert_return`, `assert_trap`, and
`assert_exhaustion` through preprocessing. Native and browser runners treat an
executor trap as success only for the latter two kinds; export lookup and other
engine errors still fail the assertion, and a successful invocation fails when
a trap was expected. Browser execution uses a 1 MiB native stack and a bounded
64-call limit so exhaustion is reported by the engine before the host Wasm
stack overflows. `tests/c-engine-assertion-kinds.wast` covers return,
unreachable-trap, and recursive exhaustion. This removes all 16 previously
misclassified invocation traps from `linking.wast`, raising it to 119 of 136
emitted results.
