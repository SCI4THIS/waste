# C engine

This directory contains the WASTE C engine: a Flex/Bison parser, binary encoder,
frame-based interpreter, and WAST spec test runner. It compiles to both native
Linux x86_64 (via raw syscalls, no libc) and browser WebAssembly. The freestanding
library (`freestanding_lib.c`) is shared by both targets; platform-specific code
lives in `freestanding_native.c` (native) and `browser_wast.c` (Wasm).

```sh
make -C src/cli-rt wast-native wast-mmap-test
make -C src/html-rt wast-browser
```

## Fast WAST parser checks

The native parser harness memory-maps each input before passing it through the
same `wast_parse_bytes` entry point used by the runner. This keeps grammar
iteration out of Node and the browser while preserving parser diagnostics:

```sh
make -C src/cli-rt BUILD_DIR=../../build/cli-rt \
  ENGINE_BUILD_DIR=../../build/engine wast-mmap-test
build/cli-rt/wast-mmap-test tests/example.wast \
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
