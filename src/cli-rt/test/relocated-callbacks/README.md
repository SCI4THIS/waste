# Executable static callback relocation

The fixture stores imported `free` and `puts` addresses in volatile static
function-pointer objects, then calls them from a constructor and main. Volatile
prevents the compiler from replacing the loads with direct calls. The fixture
records observations; assertions belong to the canonical
`src/vfs/root/test/system/cli-runtime/relocated-callbacks.wast`.

`check.sh` compiles against the real mounted SDK and shared guest CRT, checks
provider signatures and stages the artifact into native Bash. It never copies
a sysroot or embeds libc. An optional runner argument selects `wasm-sanitize`.
Artifacts live under `build/cli-rt/relocated-callbacks`; `WASTE_CALLBACK_BUILD`,
`WASTE_GUEST_CC` and `WASTE_GUEST_LD` can override output/compiler/linker.

```sh
make -C src/cli-rt relocated-callbacks
bash src/cli-rt/test/relocated-callbacks/check.sh build/cli-rt/wasm-sanitize
make -C src/html-rt vfs-tests-install bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html src/cli-rt/test/relocated-callbacks/browser.json
```

The shared process startup must apply PIC data relocations after GOT resolution and before
constructors. Without that step, the first indirect call traps even though
the imported function resolves and direct calls work.
