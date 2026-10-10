# System tests

Stage tests here when they exercise interactions among packages, the engine
kernel, runtimes or compiler SDK. Package-only sources, WAST assertions and
test instructions belong under `src/aux/NAME/`.

`make -C src/system-tests test-libc` runs the authored `libc/*.wast` against
the installed shared library inside native Bash. These cover filesystem and
loader errors, readiness, wire layouts, terminal callbacks, resource limits
and temporary files. The shared transport only stages inputs and runs Bash;
all runtime assertions are in the WAST sources. `WASM_RUNNER` selects the
`wasm-sanitize` frontend. Results go under `build/system-tests/libc/run-*/`.

Install snapshots with `make -C src/html-rt vfs-tests-install`, rebuild the page,
then run from `bash.html`:

```sh
status=0
for test in /root/test/system/libc/*.wast; do
  /bin/wast --verbose "$test" || status=1
done
printf 'system test status: %s\n' "$status"
```

These tests also run with `build/cli-rt/wast --verbose FILE` or `wast --suite`
using an isolated production runtime context. They use the C kernel/library providers; the OCaml reference
implementation remains a WAT/WAST language reference only.

See [Bash WAST reporting](wast/README.md) for passing/failing progress controls,
quiet behavior, redirection and download steps.

`guest-sdk/` holds compiler-boundary probes, the SDK check and the sysroot
copier. `make -C src/system-tests check-sdk` checks header preprocessing,
include order, ABI/declaration signatures and negative compilation cases.
These checks necessarily compile C: WAST cannot test C header declarations.
`sysroot` stages installed headers and CRT into
`build/system-tests/guest-sdk/sysroot`; `crt-fixture` compiles/audits the startup
probe under `build/system-tests/guest-sdk`. `SYSROOT_DIR` and `BUILD_DIR` override these build destinations.
The HTML Makefile's old coreutils sysroot/startup targets delegate here; libc
neither builds nor owns these SDK fixtures.
