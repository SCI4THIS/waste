# Linked compiler imports

This observer instantiates LLVM's actual `FixedPointBuilder` with four runtime
scales and exercises fixed-to-floating and floating-to-fixed IR construction.
The guest uses shared libc's exact `ldexp` operation for powers of two. Native
and threaded profiles retain upstream `pow`. It also calls the public `ioctl`
header from C++ to verify C linkage and the existing kernel error path.

All expected values and exit-status checks live in `compiler-imports.wast`.
The separate shared-libc `lround.wast` covers ties, range limits and domain
errors for the import retained by Clang's sanitizer cutoff calculation.

Run `make -C src/aux test-compiler-imports`. Use
`WASM_RUNNER=/absolute/path/to/build/cli-rt/wasm-sanitize` with ASan/UBSan options
for the same test under sanitizers. `compiler-imports-source` checks the native,
native threaded and guest threaded compilation profiles. `browser.json` runs
the same observer and assertions through the packaged production worker.
