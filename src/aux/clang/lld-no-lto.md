# LLD without link-time optimization

LTO means link-time optimization. Instead of ordinary compiled Wasm object
files, `-flto` makes a compiler emit LLVM bitcode. An LTO-enabled linker runs
LLVM's optimizer and code generator across those inputs before linking the
resulting object code. Normal per-file optimization (`-O1`, `-O2`, `-Os`) does
not require linker LTO support.

The `clang-lld-no-lto` target builds the object/archive linker selected by
`install-clang` at `build/aux/clang/wasm-ld-no-lto.wasm`. It retains ordinary
Wasm object, archive and shared-library parsing. It rejects LLVM bitcode with a diagnostic
asking for Wasm objects compiled without `-flto`. No deferred LTO module is
implemented. The original `wasm-ld.wasm` remains the LTO-enabled comparison.

```sh
make -C src/aux clang-lld-no-lto
make -C src/system-tests test-lld-link-no-lto
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-lld-link-no-lto \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

This variant requires the existing configured guest LLVM tree, its built archives
and original LLD driver objects. It does not refresh the whole LLVM dependency
graph. `lld-no-lto.mk` copies the staged Wasm linker sources into its private
build directory and applies `lld-no-lto.patch` and `lld-shared-imports.patch`
there. It compiles the remaining
LLD translation units with Clang using an explicit Wasm target and the existing
private SDK/LLVM flags, omits `LTO.cpp`, and links against the existing archives.
Static extraction and section collection select only referenced code. The
existing shared-libc helper rewrites and checks provider imports afterward.
Checked-out submodules, the original staged sources and baseline archives stay
unchanged. This build target does not install files; `install-clang` publishes
this variant as `/usr/bin/wasm-ld`.

The separate `LLD_WASTE_NAMED_IMPORTS` profile permits fixed-address commands
that import process memory to link against `.so.wasm` providers. Shared function
imports use the provider's basename without `.so.wasm`, matching the existing
engine loader registration. Upstream symbol resolution and signature checks
still apply, local definitions stay local, and unresolved kernel calls keep
their original namespaces. The original upstream profile remains unchanged.
`make -C src/system-tests test-lld-sdk` checks public-header calls, the shared
CRT, emitted import namespaces, output/exit status and signature rejection,
without a host-side rewrite of the guest link result.

The patch condition is private to this variant. It removes target-backend
initialization and LLVM-bitcode construction/compilation, while preserving the
upstream LTO-enabled source path for ordinary builds. It avoids changing class
layout used by the original driver/common objects; variant-local bitcode helper
state is not instantiated in the disabled path.

All runtime expectations live in system WAST under
`src/system-tests/clang/lld-link`: ten assertions cover real object/archive
linking and execution of the result; six more cover nonzero bitcode diagnostics
and absence of output for direct and archived bitcode. The shell files only
stage inputs, record command observations and combine WAST runner statuses.

The measured comparison against the preserved LTO-enabled artifact is:

| Measurement | LTO enabled | LTO disabled |
| --- | ---: | ---: |
| Wasm executable | 53.34 MiB | 6.65 MiB |
| Defined functions | 90,685 | 12,186 |
| Decoded instruction arrays | 655.39 MiB | 75.87 MiB |
| Native version-command peak RSS | about 920 MiB | 144.19 MiB |

Decoded instruction storage falls by 88.4%. The non-LTO version command exits
0 in 2.60 seconds under a 512 MiB native address-space cap. These are measured
startup figures, not a full compiler or browser memory budget. The small real
link plus direct/archived bitcode rejection passes all 16 WAST assertions in
native and ASan/UBSan Bash. The original linker still passes its ten positive
assertions. Leak detection is disabled for the sanitizer run.

The production-worker scenario stages the same artifact and inputs into the
packaged offline page, without installing them in the source VFS:

```sh
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/lld-link/no-lto-browser.json
```

Its host wait budget is 240 seconds and guest execution deadline is 180 seconds.
It checks the positive link and execution, with 11 expected results including
launch; all 11 pass with exit 0. The initial combined browser scenario passed
all ten positive WAST assertions, then reached its execution deadline during
bitcode rejection.
Rejection is accepted in native/sanitizer runs; the browser smoke test isolates
the positive link. The default short worker-test wait also expired before
results arrived. Evidence and measurements are saved under
`build/engine/logs/clang-lld-no-lto-*`.
