# C engine

This directory contains the WASTE C engine: a Flex/Bison parser, binary encoder,
frame-based interpreter, and WAST spec test runner. It compiles to both native
Linux x86_64 (via raw syscalls, no libc) and browser WebAssembly. Shared binary
reader, writer, LEB, module, decoder, and loader facilities live under
`wasm/`; frame-based dispatch and opcode-family execution live under `op/`;
instance allocation and store management live at the engine root.

The handwritten engine sources are grouped by ownership:

- `include/` exposes values, structured errors, and opaque decoded-module and
  instance handles via the single `waste.h` header. Embedders should include
  `waste.h` from this directory and never depend on internal layouts.
- `wat/` owns the reentrant WAT lexer/parser, parse context, builder, literals,
  and text AST.
- `wast/` owns WAST command classification, scanner boundaries, assertion
  execution, and WAT-versus-WAST parsing policy.
- `wasm/` owns bounded binary reading/writing, LEB values, encoding, decoding,
  opcode metadata, and binary loading.
- `op/` owns frame-based dispatch, opcode-family execution modules, and
  validation. `runtime_internal.h` is not a public API.
- The engine root owns the store, mutable instances, and instantiation.
  `engine_internal.h` is not a public API.
- `lib/` supplies the freestanding C subset shared by native and browser
  targets.

Generated scanner and parser sources stay under `build/engine/gen/`.

`op/validate.c` is the single type-relation and function
operand/control-stack validation pass. It returns distinct invalid and
unsupported outcomes and records resolved branch, call, type, and immediate
metadata before a function body becomes read-only to execution. Run its focused
warnings-as-errors and sanitizer gate with:

```sh
make -C src/cli-rt BUILD_DIR=../../build/cli-rt wasm-validation
```

Runtime execution is split into independently compiled units under `op/`.
`execute.c` owns dispatch, calls, tail-frame replacement, yield/resume, and
guest non-local-control snapshots. `execute_numeric.c`, `execute_memory.c`,
`execute_table.c`, `execute_simd.c`, `execute_gc.c`, and
`execute_exception.c` own their opcode-family helpers. `validate.c` owns the
type-relation and operand/control-stack validation pass. These modules share
the bounded private `waste_exec_context` declared by `runtime_internal.h`; no
implementation file is textually included by another C source file.

```sh
make -C src/cli-rt wast-native
make -C src/html-rt wast-browser
```

## Fast WAST parser checks

The `--parse-only` mode memory-maps each input before passing it through the
same `wast_parse_bytes` entry point used by the runner. This keeps grammar
iteration out of Node and the browser while preserving parser diagnostics:

```sh
make -C src/cli-rt BUILD_DIR=../../build/cli-rt \
  ENGINE_BUILD_DIR=../../build/engine wast-native
build/cli-rt/waste-cli --parse-only tests/example.wast \
  submodules/wasm-spec/test/core/forward.wast
```

It accepts multiple files, reports each parse result and elapsed time, and
returns non-zero if any input fails. The mapped input is read-only; the parser
consumes the original bytes without a rewriting pass and owns its script
allocations.

Each parse allocates one `wat_context`, passed explicitly to both the pure
Bison parser and reentrant Flex scanner. It owns lexer location/folding state,
module-building accumulators, name and fixup tables, retained token strings,
and bounded literal scratch storage. There is no mutable parser or lexer state
shared between invocations. The runner destroys the context on parser success,
syntax or semantic failure, and scanner setup failure.

The shared scanner also owns the text front-end boundaries. Exclusive lexer
states skip nested block comments and balanced annotation wrappers while
preserving original locations, including annotations between an opening
parenthesis and a folded operator. Quoted and binary module strings are decoded
by grammar actions into explicit byte/length payloads, so embedded NUL bytes do
not depend on C-string conventions. Bare module fields use a dedicated inline
module start production rather than a synthetic `(module ...)` wrapper.
Context-aware numeric tokens validate and convert integer and floating-point
spellings without rewriting the mapped input; strict WAT mode rejects the
WAST-only `nan:canonical` and `nan:arithmetic` expectation patterns.

WAST streaming uses a dedicated mode of that same scanner. It returns one
balanced command range while sharing comment, annotation, string, escape, and
location handling with ordinary parsing. `wast/stream.c` parses each
range in a new context, so a balanced malformed command is released and
recorded without preventing the next command from running. Strict WAT
compilation still parses the complete input transactionally. Command
classification lives in `wast/command.c`, assertion value matching and
action execution live in `wast/assert.c`, and store, registry, and
module-lifetime transitions live in `store.c`.

The focused concurrency and cleanup gate launches four parser threads and
exercises valid and malformed multi-line inputs under AddressSanitizer and
UndefinedBehaviorSanitizer:

```sh
make -C src/cli-rt BUILD_DIR=../../build/cli-rt parser-reentrant
```

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
instead retains that error in its module-group record, allowing script parsing
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

`tests/c-engine-stage7-front-end.wast` covers nested comments and annotations,
annotation-transparent folded operators and module fields, integer-spelled
floating constants, exact data-string bytes, and raw binary modules.
`tests/c-engine-stage7-inline-fields.wast` covers the dedicated bare-field
entry point after leading comments and annotations. The reentrant parser gate
also checks exact raw payload bytes and diagnostics for unterminated nested
comments. It additionally checks scanner-owned command recovery and ordering,
and verifies that strict WAT mode rejects a multi-module input as one failed
transaction.
