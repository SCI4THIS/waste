# LLVM DWARF context cache ownership

The complete production `DWARFContext.cpp` object failed because its
`ThreadSafeState` unconditionally used `std::recursive_mutex`. The owned LLVM
staging patch selects the existing counted `SmartMutex<true>` only for
`LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`. Its guards infer the owner's type;
native and threaded builds retain the standard recursive mutex. The state
continues to parse DWO units eagerly, including when a caller requests lazy
parsing. The upstream `isThreadSafe` value identifies this selected state; the
disabled-thread guest backend does not establish cross-thread exclusion.

`probe.cpp` includes the complete actual context source so it can observe both
private state implementations without adding production test hooks. It links
the actual DWARF, Object, MC and other required guest archives, production CRT,
C++ archives and shared libc. A small `DWARFObject` supplies valid DWARF4 normal
and split compile-unit bytes through LLVM's ordinary client interface. All
parsing, cache access and state methods come from the production sources.
The callback reenters the context's abbreviation cache and calls the existing
shared `usleep` provider while the outer context guard remains live.

All functional assertions are in `dwarf-context.wast`: unit versions, tags,
names and addresses; cache identity and source-read counts; range boundaries;
eager/lazy DWO behavior; type maps; ownership destruction and fresh caches.
The MC dependency's pseudo-probe formatter selects LLVM raw streams in that
same guest profile. Its actual archive method supplies exact-output checks
for empty and nested contexts, maximum probe indices, frame order and retained
strings after the descriptor names change. The two MC parser macro-depth error
formatters use the same guest-only raw-stream selection. Source checks cover
all four complete files; macro-depth diagnostics still need execution coverage
through the actual assembler driver.
The fixture accepts this context/cache boundary, not full DWARF conformance,
thread exclusion, or a runnable compiler/linker.

From the repository root, with one LLVM build owner:

```sh
make -C src/aux dwarf-context-native-source dwarf-context-guest-source
make -C src/aux test-dwarf-context
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-dwarf-context \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/dwarf-context/browser.json \
  > build/engine/logs/clang-dwarf-context-browser.json
```

Expect 51 passing WAST assertions and 52 worker results including launch.
`make -C src/aux clang-dwarf` builds the production guest DWARF archive and its
dependencies, without installing a compiler. Outputs and native result JSON
live under `build/system-tests/clang/dwarf-context`; save logs under
`build/engine/logs/clang-dwarf-context-*`.

For an optional manual check in `bash.html`:

```sh
upload /tmp/dwarf-context.wasm
chmod +x /tmp/dwarf-context.wasm
/tmp/dwarf-context.wasm > /tmp/dwarf-context.bin
printf '%s\n' "$?" > /tmp/dwarf-context-status.txt
wast --verbose /root/test/system/clang/dwarf-context/dwarf-context.wast > /tmp/dwarf-context-results.txt
echo "$?"
download /tmp/dwarf-context-results.txt
```

Select `build/system-tests/clang/dwarf-context/probe.wasm` when uploading.
The observer exits 29; the WAST test checks that status and exits zero when
all expectations pass. Save downloaded evidence under `build/html-rt`.
