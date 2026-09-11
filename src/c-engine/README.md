# C engine proof of concept

This directory contains the first executable slice of the C port. It loads a
real WebAssembly binary with bounded readers, resolves function exports, decodes
functions to fixed-width numeric instructions, and executes `return_call` and
`return_call_ref` by replacing the active function, argument, and PC. Execution
does not push a frame or allocate memory for a tail transfer.

The accepted subset is intentionally strict: `(i64) -> i64` functions, function
exports, `local.get 0`, `i64.const`, `i64.eqz`, `i64.sub`, `if`/`else`/`end`,
`ref.func`, and the two tail-call instructions. Other types, imports, sections,
locals, and opcodes return a structured unsupported error.

```sh
tests/c-tail-poc/run.sh 5000000
make -C src/c-engine sanitize
```

`browser.c` supplies a freestanding adapter and a page-growing bump allocator.
It is only for the short-lived proof module; the production engine will replace
it with explicit arenas and refcounted guest-memory backing objects.

## Fast WAST parser checks

The native parser harness memory-maps each input before passing it through the
same `wast_parse_bytes` entry point used by the runner. This keeps grammar
iteration out of Node and the browser while preserving parser diagnostics:

```sh
make -C src/c-engine BUILD_DIR=../../build/c-engine \
  WAST_BUILD_DIR=../../build/c-engine wast-mmap-test
build/c-engine/wast-mmap-test tests/example.wast \
  submodules/wasm-spec/test/core/forward.wast
```

It accepts multiple files, reports each parse result and elapsed time, and
returns non-zero if any input fails. The mapped input is read-only; the parser
continues to own its preprocessing and script allocations.

`tests/c-engine-global-constexpr.wast` is the global-initializer grammar gate.
Initializers use the ordinary instruction-list parser, while a separate C
validation pass enforces the constant-opcode set and declared result type. The
stored expression remains terminator-free; the module encoder appends its
single required `end` opcode. The validator mirrors the OCaml `check_const`
whitelist: scalar/vector constants, `i32`/`i64` add/sub/mul, immutable
`global.get`, `ref.null`, `ref.func`, `ref.i31`, struct and array constructors,
and the two extern conversions. GC constructor stack effects are checked
against retained struct/array field metadata; forward `ref.func` bounds are
checked after module fixups.

Validation failure is mode-sensitive. Transactional WAT compilation returns
the semantic error immediately. An inline module owned by `assert_invalid`
instead retains that error in its module-group record, allowing preprocessing
to continue and the native/browser assertion harness to observe the rejection
without depending on the binary loader to fail for an unrelated reason.

`tests/c-engine-function-fields.wast` is the function header/body boundary
gate. Function fields follow the reference OCaml parser's recursive phases:
type use, parameters, results, locals, and finally the instruction list.
Inline exports recurse at phase boundaries for compatibility with existing
C-engine fixtures. This lets an opening parenthesis be classified by the next
field or instruction keyword without a function-body lexer sentinel.

Folded control instructions apply the same boundary: a block type is
accumulated through type-use, parameter, and result phases before its body is
parsed; multi-value signatures are reused or synthesized in the module type
section. Dedicated folded-start tokens keep body instructions out of the
nullable block-field decision.
