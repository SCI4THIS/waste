# C tail-call proof

This deliberately narrow fixture answers whether a repository-owned C dispatch
loop can make proper tail calls fast without growing a host or guest call stack.
It assembles a real Wasm module and exercises both `return_call` and
`return_call_ref`.

```sh
tests/c-tail-poc/run.sh 5000000
```

The command succeeds only when both exports return zero, perform exactly the
requested number of tail transfers, retain one frame, and allocate nothing in
the execution loop. The decoder rejects every section, type, and opcode outside
this proof subset; it is not yet a general WebAssembly engine.

The TUI's **Build native/browser C tail-call proof and benchmark** entry also
creates `build/c-tail-poc/tail-call-poc.html`. That self-contained `file://`
page embeds both the C engine Wasm and guest module and measures them with
`performance.now()`; no server or external asset is required.
