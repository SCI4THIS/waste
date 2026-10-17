# Compiled wide-format boundary

The C and C++ observers call the installed `swprintf`/`vswprintf` providers
through `<wchar.h>` and libc++ `<cwchar>`. They only capture observations;
all functional expectations live in `wide-format.wast`. Nine records from
each executable capture return counts, errno, the original variadic argument
cursor and two guarded wide buffers. The 21 WAST assertions check compiler
alignment for mixed int/double/binary128/long-long arguments, full binary128
range and low bits, Unicode/UTF-8 element counts, starred fields, truncation,
exact fit, one/zero capacities, repeated `va_list` calls and direct `swprintf`.
The package's `wide-format.wast` separately checks 87 provider assertions.
These observers use generated headers, not a stale C++ archive; owning strings
and C++ constructors/shutdown need their separate startup acceptance.

```sh
make -C src/system-tests test-wide-format
WASTE_TEST_TIMEOUT_MS=180000 make -C src/system-tests test-wide-format \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Use `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1` for sanitizer runs. `wide-format-fixture` builds
both observers without installing anything. Outputs are under
`build/system-tests/clang/wide-format`; the unchanged build is cached.

Refresh snapshots, audit them and generate the offline page in sequence:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/wide-format/browser.json \
  > build/engine/logs/clang-browser-wide-format-results.json
```

Expect 109 passing results, including Bash bootstrap, and exit status zero.
The worker uses the generated page's production engine, package and scripts;
it does not launch a desktop browser or verify DOM rendering.

For optional manual evidence, upload the built `probe-c.wasm` and
`probe-cxx.wasm` as `/tmp/wide-format-c.wasm` and `/tmp/wide-format-cxx.wasm`.
Run in `bash.html`:

```sh
chmod +x /tmp/wide-format-c.wasm /tmp/wide-format-cxx.wasm
/tmp/wide-format-c.wasm > /tmp/wide-format-c.bin
printf '%s\n' "$?" > /tmp/wide-format-c-status.txt
/tmp/wide-format-cxx.wasm > /tmp/wide-format-cxx.bin
printf '%s\n' "$?" > /tmp/wide-format-cxx-status.txt
wast --verbose /root/test/system/clang/wide-format/wide-format.wast > /tmp/wide-format-results.txt
echo "$?"
wast --verbose /root/test/aux/libc/wide-format.wast >> /tmp/wide-format-results.txt
echo "$?"
download /tmp/wide-format-results.txt
```

Expect 21 and 87 passing assertions and both statuses zero. Save downloaded
evidence under `build/html-rt`. The package test can also run on its own without
uploading compiled fixtures.
