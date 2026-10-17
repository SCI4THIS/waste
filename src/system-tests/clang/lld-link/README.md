# Actual guest LLD link and execution

For shared-libc SDK linking, first build
`make -C src/aux clang-lld-no-lto install-clang-sdk`, then run
`make -C src/system-tests test-lld-sdk`. `sdk.c` uses the actual public C
headers, the installed production guest CRT and the installed libc provider. Guest LLD writes
the executable directly; no host tool rewrites its imports. `sdk.wast` owns nine
assertions: link/chmod/run statuses, exact libc/kernel output, named libc and
raw kernel imports, and a signature mismatch with no output file. The host
compiler only supplies the input objects; actual guest Clang remains a separate
end-to-end acceptance requirement. Select `WASM_RUNNER` for sanitizers as below.
`sdk-browser.json` stages the same fixture through the production worker and
expects ten results including launch.
All nine assertions pass in native and ASan/UBSan Bash; the production worker
passes all ten results with exit 0. ASan leak detection is disabled. Logs and
artifact/source hashes are under `build/engine/logs/clang-lld-sdk-*`.

This test uses the existing `build/aux/clang/wasm-ld.wasm` artifact. It does not
rebuild LLVM or install a compiler/linker. Missing artifacts must be built
through `src/aux/clang`; dependency freshness remains a separate acceptance gate.

```sh
make -C src/system-tests test-lld-link
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-lld-link \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Host Clang compiles two freestanding C inputs to Wasm objects under
`build/system-tests/clang/lld-link`; host `llvm-ar` packages `answer.o` into an
archive. Installed Bash runs the staged guest `wasm-ld` to resolve the entry
object against that archive, writes `/tmp/linked.wasm`, marks it executable and
runs it. The result calls the existing shared libc exit provider with status 37.
`commands.sh` records observations; all ten assertions live in `link.wast`.
They check link/chmod statuses, execution status and the emitted Wasm header.
Every run keeps commands and assertion-aware results in a new `run-*` directory.
Override `LLD_LINK_TOOL`, `LLD_LINK_CC` or `LLD_LINK_AR` when needed. The default
180-second session deadline accommodates instrumented loading of the real LLD.

The baseline probe covers one positive object/archive link and execution;
SDK/CRT linking, successful LTO and full compiler acceptance remain separate.

The [non-LTO variant](../../../aux/clang/lld-no-lto.md) uses the same positive
test plus `no-lto.wast`: six assertions check exit status, the explanatory
diagnostic and absence of output for direct and archived LLVM bitcode. Run
`make -C src/system-tests test-lld-link-no-lto` after building the variant.
`LLD_LINK_COMMANDS` selects its orchestration without duplicating the staging
runner. Host fixture generation also creates the bitcode inputs under `build/`.
Both native and ASan/UBSan pass all 16 assertions; the original linker continues
to pass the ten positive assertions.

`no-lto-browser.json` stages the positive inputs and authored WAST file into the
production worker loaded from `build/html-rt/bash.html`. Invoke it through
`tests/guest-session-worker.cjs`; it expects 11 results including launch. This
uses the production browser engine without starting a graphical browser.
All 11 results pass with exit 0. The earlier combined positive/negative worker
run reached its execution deadline after passing the ten positive assertions;
native/sanitizer checks retain bitcode-rejection coverage.

The portable `tests/engine-regressions/mmap-heap.wast` guards the underlying
kernel fix: automatic mappings leave the linear heap size unchanged, later
heap growth preserves mapping bytes, collision growth fails without mutation,
and unmapping does not expose a false heap extent. It imports process memory
so the same test observes the correct capsule in standalone cli-rt and in Bash.
Refresh snapshots with `vfs-tests-install`, then run
`/bin/wast --verbose /root/test/engine-regressions/mmap-heap.wast` in Bash.
`tests/guest-session-mmap-heap.json` drives that command through the production
browser worker. It does not run LLD in the browser.
