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
