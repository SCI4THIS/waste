# LLVM CFG node formatting

This observer calls the actual `DOTGraphTraits<DOTFuncInfo *>::getNodeAttributes`
from the staged LLVM header. It links the complete production `CFGPrinter`,
`HeatUtils`, `BlockFrequencyInfo` and `BlockFrequencyInfoImpl` objects, existing
Core/Support dependencies and current guest C++ libraries. No formatting or
analysis implementation is copied into the fixture.

The disabled-thread guest uses LLVM's `raw_string_ostream` for these attributes;
native and threaded profiles retain `std::stringstream`. The source gates
compile the complete CFG printer in both standard-stream profiles and check all
four actual guest object rules.

WAST owns all 70 observations, the 560-byte capture, exact 786-byte attribute
file and observer exit status 32. Eleven cases cover absent/null/empty node IDs,
copied identifiers after source mutation, embedded NULs, a 256-character ID,
comma placement and cold heat-color attributes. Formatting outputs survive
destruction of their callback/DOTFuncInfo owners. One ID callback yields through the
existing shared `usleep`; this fixture adds no timing or scheduling mechanism.

The real default `BlockFrequencyInfo` supplies zero frequencies for unprofiled
nodes. These checks do not establish profiled/hot colors, graph export, a viewer,
full LLVM Analysis compilation or a working compiler/linker.

The heat helper also needs shared libc's binary64 `log2` and `round` providers.
Their separate package WAST checks exact powers, selected high-precision
references within four ULPs, domain/pole errno, signed zeros, infinities/NaNs
and halfway rounding neighbors. The production worker runs both authored files.

From the repository root:

```sh
make -C src/system-tests cfg-format-fixture cfg-format-native-source cfg-format-guest-source
make -C src/system-tests test-cfg-format
make -C src/cli-rt BUILD_DIR=../../build/cli-rt ../../build/cli-rt/wasm-sanitize
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  bash src/system-tests/clang/cfg-format/run.sh "$PWD" "$PWD/src/vfs" \
  "$PWD/build/system-tests/clang/cfg-format" "$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install bash-html vfs-tests-check
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/cfg-format/browser.json
```

Native and sanitizer runs should report 73 PASS and zero failures. The production
worker also runs libc's 89-assertion `log2-round.wast` and should report 163/163
results including launch and exit zero. Leak checking
is disabled for the ptraced runner; AddressSanitizer and UndefinedBehaviorSanitizer
remain enabled. Repeat fixture/source targets to verify unchanged builds cache.
Save verification output under `build/engine/logs`.

For manual acceptance, open the rebuilt `build/html-rt/bash.html` in a fresh
browser tab. Run:

```sh
upload /tmp/cfg-format.wasm
```

Choose `build/system-tests/clang/cfg-format/probe.wasm`, then run:

```sh
chmod +x /tmp/cfg-format.wasm
/tmp/cfg-format.wasm > /tmp/cfg-format.bin
printf '%s\n' "$?" > /tmp/cfg-format-status.txt
wast --verbose /root/test/system/clang/cfg-format/cfg-format.wast > /tmp/cfg-format-results.txt
printf '%s\n' "$?" > /tmp/cfg-format-test-exit.txt
wast --verbose /root/test/aux/libc/log2-round.wast >> /tmp/cfg-format-results.txt
printf '%s\n' "$?" >> /tmp/cfg-format-test-exit.txt
download /tmp/cfg-format-results.txt
download /tmp/cfg-format-status.txt
download /tmp/cfg-format-test-exit.txt
```

Save all three downloads into `build/html-rt`. Expect 73 PASS and 89 PASS,
zero failures, observer status 32 and two WAST exit statuses of 0. Keep the
observer and report paths
consistent: WAST reads the files produced by those commands.
