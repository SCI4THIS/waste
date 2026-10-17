# Compiled floating-point classification boundary

`make -C src/system-tests test-classification` builds real guest C and C++
clients against the public SDK and generated libc++ headers. Host Clang
cross-compiles these prerequisites; a guest compiler is not required.

The C input uses public classification macros; C++ calls `std::fpclassify`,
`std::isfinite`, `std::isnormal`, `std::isnan`, `std::isinf` and `std::signbit`.
Counted function calls load volatile values, preventing constant folding and
recording each argument's evaluation count. Thirteen cases per type cover
signed zeros, smallest/negative/largest subnormals, smallest/largest normals,
finite values, signed infinities and signed NaNs. The guest types are binary32,
binary64 and binary128 (16-byte long double, 113-bit significand).

`probe.h` captures observations only. The report starts with eleven i32 metadata
words, followed by 39 records of thirteen i32 words: six classification/trait
results, six call counts and errno. `classification.wast` owns all 83 assertions:
report lengths, process exits, classification codes/type metadata and every
case in both languages. It verifies single evaluation and preserved errno.
The shared-import helper assigns and verifies eight imports in each executable.
Both use the production CRT/shared libc, with C linkage for freestanding main.
They do not link libc++ runtime archives or establish C++ constructor behavior.

```sh
make -C src/system-tests test-classification
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-classification \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Artifacts and results live under `build/system-tests/clang/classification`.
Complete VFS installation before concurrent test readers. To repeat against a
stable tree, `run.sh` takes repository root, VFS root, fixture build directory
and the absolute runtime executable path.

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/classification/browser.json \
  > build/engine/logs/clang-browser-classification-results.json
```

Expect 108 passing worker results: 83 compiled classification assertions,
24 package binary128 comparison assertions, and Bash bootstrap. The package
assertions belong to `src/aux/libc/tests/quad-comparison.wast` and run with
`test-libc`. Exact comparison does not establish full binary128 arithmetic;
existing arithmetic/conversion helpers retain their double-based limitations.

For an optional manual check in the refreshed page, upload
`build/system-tests/clang/classification/probe-c.wasm` and `probe-cxx.wasm`
into the respective paths below:

```sh
upload /tmp/classification-c.wasm
chmod +x /tmp/classification-c.wasm
upload /tmp/classification-cxx.wasm
chmod +x /tmp/classification-cxx.wasm
/tmp/classification-c.wasm > /tmp/classification-c.bin
printf '%s\n' "$?" > /tmp/classification-c-status.txt
/tmp/classification-cxx.wasm > /tmp/classification-cxx.bin
printf '%s\n' "$?" > /tmp/classification-cxx-status.txt
wast --verbose /root/test/system/clang/classification/classification.wast > /tmp/classification-results.txt
echo "$?"
wast --verbose /root/test/aux/libc/quad-comparison.wast >> /tmp/classification-results.txt
echo "$?"
download /tmp/classification-results.txt
```

Expect 83 and 24 passes, with both statuses zero. Keep downloaded evidence in
`build/html-rt`. No GUI check is required for this compiler/SDK prerequisite.
