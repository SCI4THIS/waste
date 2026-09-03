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
