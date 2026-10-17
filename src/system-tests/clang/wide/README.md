# Compiled wide-string SDK boundary

This observer uses the generated, wide-enabled libc++ headers and the actual
shared guest libc. It captures `std::wstring_view`, `char_traits<wchar_t>` and
public wide-memory/search, integer and floating-conversion calls; all functional
expectations are in `wide.wast`.
The SDK compile probe also checks `std::wstring` declarations. This test does
not construct an owning string or link the runtime archives. The wide-enabled
archives now rebuild; owning-string startup remains porting work. It never uses an older archive
to stand in for the current profile.
Its header operations need libc++'s diagnostic-abort helper. The fixture
compiles the original staged `verbose_abort.cpp` into a fresh object and
links its real shared-libc formatting/abort imports; it supplies no stub.

```sh
make -C src/system-tests test-wide
```

`wide-fixture` builds the observer only. The host cross-compiler uses guest
headers without a host fallback; outputs and run results go under
`build/system-tests/clang/wide`. Sixty-four WAST assertions check the 296-byte
report, exit status, signed four-byte wchar layout, non-ASCII view searches,
substring comparison, copy/move/fill return pointers, overlapping memory,
element values, canaries and the compiled public search APIs. Six conversion
records capture value, wide-element end offset and errno through the real
`<cwchar>`/`<cinttypes>` aliases, including 32/64-bit limits, saturation and
unsigned negation. Three floating records check compiler-generated binary32,
binary64 and binary128 returns, including rounding above a halfway value,
an exact double subnormal and the low binary128 fraction bit. Direct provider
edge cases belong to the package
`wide-memory.wast`, `integer-conversion.wast` and `floating-conversion.wast` tests.

After refreshing installed snapshots and rebuilding `bash.html`, run the same
compiled artifact through its production worker:

```sh
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/wide/browser.json \
  > build/engine/logs/clang-browser-wide-results.json
```

Expect 65 passing results including Bash bootstrap and exit status zero.
`memory-browser.json` exercises the 61 direct shared-provider assertions:

```sh
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/wide/memory-browser.json \
  > build/engine/logs/clang-browser-wide-memory-results.json
```

Expect 62 passing results including Bash bootstrap.
For optional manual evidence, upload `build/system-tests/clang/wide/probe.wasm`
as `/tmp/wide.wasm`, then run in browser Bash:

```sh
chmod +x /tmp/wide.wasm
/tmp/wide.wasm > /tmp/wide.bin
printf '%s\n' "$?" > /tmp/wide-status.txt
wast --verbose /root/test/system/clang/wide/wide.wast > /tmp/wide-results.txt
echo "$?"
download /tmp/wide-results.txt
```

Expect 64 passing assertions and status zero. Keep downloaded evidence under
`build/html-rt`. These checks establish the exercised SDK/provider boundary;
full libc++ startup, LLVM platform support and compiler acceptance remain pending.
