# Guest C/C++ checkpoints

The shared SDK exposes the engine's existing evaluator checkpoint ABI through
`setjmp.h`. Both buffer types preserve Bash's 64-byte layout; the first word
contains an opaque copyable token. `setjmp` expands directly to the
returns-twice `sigsetjmp` import, keeping the checkpoint in the caller's live
activation. `longjmp` is a real C-linkage import so libc++ can expose
`std::longjmp`; both jump spellings use the same engine transition. Jump callers
must export their mutable i32 `__stack_pointer`.

The 218 functional assertions in `checkpoint.wast` inspect compiled C and C++
observations: public ABI and exit codes, positive/negative/zero jump values,
nested buffer reuse, copied tokens, volatile side effects and actual Wasm stack
restoration. `stack.s` reads the real guest global. Nonzero positive/negative
save flags preserve full-width signal masks after sleep, signal-handler jumps,
pending-signal delivery and fork; zero leaves mask changes intact. An explicit
mask boundary drains the newly unblocked signal before starting a fresh sleep,
so delivery timing at ordinary opcode safepoints does not affect expectations.
Ordinary signal-mask/disposition calls verify C++ linkage through `signal.h`. The 12
assertions in the authored evaluator regression cover direct imports, buffer
bounds and rejection of inactive/unknown tokens. All functional expectations
remain in WAST; the C/C++ programs only record observations.

```sh
make -C src/aux test-checkpoint
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-checkpoint \
  WASM_RUNNER=../../build/cli-rt/wasm-sanitize
```

Finish any aux Clang build before running these targets: their prerequisite may
refresh the same libc++ header configuration. The fixture uses actual generated
libc++ headers, the production CRT and shared libc. It does not need libc++
archive symbols or disable static destructor registration, and does not verify
C++ static shutdown or LLVM crash recovery itself; the latter has its own
[archive-linked fixture](../crash-recovery/README.md). No native machine jump
buffer or Asyncify is used.

After installing refreshed snapshots and rebuilding the page, run the same
checks through the production Bash worker:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/checkpoint/browser.json \
  > build/engine/logs/clang-browser-checkpoint-results.json \
  2> build/engine/logs/clang-browser-checkpoint-errors.log
```

For optional manual evidence, upload both
`build/system-tests/clang/checkpoint/probe-c.wasm` and `probe-cxx.wasm` into
`/tmp/checkpoint-c.wasm` and `/tmp/checkpoint-cxx.wasm` in `bash.html`, then run:

```sh
chmod +x /tmp/checkpoint-c.wasm /tmp/checkpoint-cxx.wasm
/tmp/checkpoint-c.wasm > /tmp/checkpoint-c.bin
printf '%s\n' "$?" > /tmp/checkpoint-c-status.txt
/tmp/checkpoint-cxx.wasm > /tmp/checkpoint-cxx.bin
printf '%s\n' "$?" > /tmp/checkpoint-cxx-status.txt
wast --verbose /root/test/system/clang/checkpoint/checkpoint.wast > /tmp/checkpoint.txt
echo "$?"
wast --verbose /root/test/engine-regressions/setjmp-buffer-copy.wast >> /tmp/checkpoint.txt
echo "$?"
download /tmp/checkpoint.txt
```

Expect 218 and 12 passes, both with status zero. The worker adds the Bash
launch assertion, giving 231 results. Keep downloads under `build/html-rt`.
