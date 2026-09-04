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

The developing WAST path now uses dynamically grown per-module function
storage (bounded at 1024 functions) and emits canonical core sections in Wasm
order: type, import, function, table, memory, global, export, start, element,
code, and data. `tests/c-engine-many-functions.wast` is the capacity gate;
`tests/c-engine-sections.wast` covers declared types, a table, memory, global,
element segment, data segment, exports, and executable code. Both pass through
the self-contained browser C engine, and their generated modules pass an
independent Binaryen decode. `tests/c-engine-import-smoke.wat` independently
checks imported-function type and index encoding.

This is an encoder milestone, not core-suite completion. Of the 30 previously
targeted core files, 10 currently preprocess and 20 still stop in the WAST
grammar. Some preprocessing successes still use instructions unsupported by
the executor.

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
