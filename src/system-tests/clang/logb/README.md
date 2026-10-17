# LLVM integer exponent constants

The public guest `math.h` defines `FP_ILOGB0` as `INT_MIN` and `FP_ILOGBNAN`
as `INT_MAX`. Both are integer constant expressions of type `int`. Providing
these constants does not add a public `ilogb` function or libc provider.

This compiled boundary probe consumes the actual staged LLVM libc
`FPUtil/ManipulationFunctions.h`. It uses the same private namespace and
`LIBC_MATH_NO_ERRNO | LIBC_MATH_NO_EXCEPT` configuration as
`llvm/lib/Support/APFloat.cpp`. It records both `intlogb<int>` and
`intlogb<long>` for volatile binary32, binary64 and binary128 operands without
narrowing them. It also records public C/C++ constants and their types, LLVM's
specialized sentinels, and the preserved shared libc errno.

All 131 functional assertions are in `logb.wast`. They cover signed zeros,
smallest signed subnormals, largest subnormals, smallest normals, largest finite
values, positive/negative ordinary values, infinities and NaNs. The fixture
links the real guest CRT and shared libc, without a host-header fallback or
substitute floating-point helper. This does not establish general LLVM math
accuracy or full Clang runtime support.

```sh
make -C src/system-tests test-logb
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-logb \
  WASM_RUNNER=../../build/cli-rt/wasm-sanitize
```

The build input prerequisite refreshes the guest configuration when needed;
finish other aux Clang builds before invoking it. Generated fixtures and native
results live under `build/system-tests/clang/logb`. Refresh snapshots and rebuild
the production page in sequence:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/logb/browser.json \
  > build/engine/logs/clang-browser-logb-results.json
```

Expect 132 passing worker results including Bash bootstrap, and exit status
zero. No manual browser test is required for this header/helper boundary. For
an optional manual check, upload `build/system-tests/clang/logb/probe.wasm`
as `/tmp/logb.wasm` in the rebuilt page, then run:

```sh
chmod +x /tmp/logb.wasm
/tmp/logb.wasm > /tmp/logb.bin
printf '%s\n' "$?" > /tmp/logb-status.txt
wast --verbose /root/test/system/clang/logb/logb.wast > /tmp/logb-results.txt
echo "$?"
download /tmp/logb-results.txt
```

Expect 131 passes and status zero. Save downloaded evidence under
`build/html-rt`.
