# Shared binary64 providers

`math.cpp` exposes a C ABI around `exp`, `log`, `pow`, `sin`, `cos`, `atan2`
and `fmod` from the pinned LLVM libc submodule. `llvm-math.mk` stages the
private headers under `build/aux/libc/llvm-math/source` before compilation.
Initialize `submodules/llvm-project` before rebuilding libc. No libc++ runtime,
host headers, host math imports, or C++ global constructors are required.

The guest profile uses LLVM's bounded fast pass with errno and fixed
nearest-even rounding. Floating-point exceptions and the accurate fallback
pass are disabled; transcendental results are not promised to be correctly
rounded. The accurate pass would require unavailable 128-bit compiler-rt
helpers. All remaining linked imports use ordinary shared-libc facilities.

The upstream Apache-2.0-with-LLVM-exception notices stay in the staged headers
and installed LICENSE.TXT. Release source bundles include these inputs.
`src/vfs/root/test/aux/libc/awk-math.wast` owns the functional assertions.
