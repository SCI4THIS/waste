# LLVM Mustache numeric interpolation

The guest libc++ profile disables localization and omits `std::ostringstream`.
LLVM's actual Mustache numeric path now calls the private
`llvm::detail::wasteMustacheNumber` only under `LLVM_WASTE_GUEST`; the native
path retains its original stream. The helper uses the implemented shared
`swprintf` with six-digit general formatting, checks its bounded ASCII output,
and returns an owning `std::string`. The wide formatter derives exact decimal
digits from binary64 and rounds nearest/ties-to-even. The current narrow
`snprintf` does not implement floating conversions; substituting it or raw
LLVM double streaming would change the numeric output. No libc++ locale,
standard type or provider is fabricated, and no unavailable SDK annotation
is removed. The guest locale remains its existing fixed C.UTF-8 profile.

```sh
make -C src/aux test-mustache mustache-native-source
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-mustache \
  WASM_RUNNER=../../build/cli-rt/wasm-sanitize
```

Finish any `make -C src/aux clang` attempt first: the fixture's prerequisite
may refresh the same guest configuration. The compiled guest observer executes
the same private numeric helper used by actual Mustache.cpp, linking the
current guest libc++/libc++abi archives, production CRT and shared libc. Normal
static-registration flags remain enabled. A separate native observer uses real
classic-locale streams for the identical IEEE bit patterns; its generated
`reference.bin` is an input to WAST, not a host assertion runner. The actual
full Mustache implementation is compiled separately in both guest and native
profiles. Complete template rendering still needs the remaining Support
library and is not claimed by this numeric fixture.

All 96 functional assertions belong to `mustache.wast`: 37 explicit text/length
cases plus complete byte agreement with the native reference, profile/errno
preservation, a heap-backed accumulated string, independent copies, moves,
mutation isolation, clearing and substring lifetime. Cases include signed zero,
exact six-digit ties, rounding across exponent thresholds, extremes, smallest
normal/subnormal values, infinities and signed/payload NaNs. All inputs are
shared through `values.inc`; neither observer contains expected results.
Results go under `build/system-tests/clang/mustache/run-*/`.
These checks do not establish full C++ startup, static-exit callbacks or
runnable guest Clang/LLD. General narrow floating printf remains pending.

Refresh snapshots and package the page before the production worker check:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/mustache/browser.json \
  > build/engine/logs/clang-browser-mustache-results.json \
  2> build/engine/logs/clang-browser-mustache-errors.log
```

For an optional manual check, upload both
`build/system-tests/clang/mustache/probe.wasm` as `/tmp/mustache.wasm` and
`build/system-tests/clang/mustache/reference.bin` as `/tmp/mustache-reference.bin`
in `bash.html`, then run:

```sh
chmod +x /tmp/mustache.wasm
/tmp/mustache.wasm > /tmp/mustache.bin
printf '%s\n' "$?" > /tmp/mustache-status.txt
wast --verbose /root/test/system/clang/mustache/mustache.wast > /tmp/mustache.txt
download /tmp/mustache.txt
```

Expect `WAST: 96 PASS, 0 FAIL, 96 total`. The worker adds the launch assertion.
