# LLVM DWARF diagnostic ownership

The real guest `DWARFVerifier.cpp` object rule exposed two unguarded
`std::mutex` fields in the diagnostic aggregator and compile-unit access map.
The owned staging patch selects LLVM's existing `SmartMutex<true>` only for
`LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`; both guards infer their owner's
actual type. Native and threaded builds retain standard mutexes.

`probe.cpp` links the complete actual verifier source, Support, Demangle,
the current C++ archives, production CRT and shared libc. It records category
and subcategory counts, callback state, owned string keys, sorted enumeration
and a fresh owner's state. One enabled callback calls shared `usleep` while
its diagnostic guard is live. All functional assertions are in
`dwarf-verifier.wast`, which runs from the installed Bash.

This accepts the diagnostic aggregator and source compatibility. It does not
exercise the verifier's private compile-unit access map or claim complete
DWARF validation, thread exclusion or a runnable compiler/linker.

From the repository root, with one LLVM build owner:

```sh
make -C src/aux dwarf-verifier-native-source dwarf-verifier-guest-source
make -C src/aux test-dwarf-verifier
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-dwarf-verifier \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/dwarf-verifier/browser.json \
  > build/engine/logs/clang-dwarf-verifier-browser.json
```

Expect 35 passing WAST assertions and 36 worker results including launch.
Outputs and native result JSON live under
`build/system-tests/clang/dwarf-verifier`; keep diagnostic logs under
`build/engine/logs/clang-dwarf-verifier-*`.

For an optional manual check in `bash.html`, upload
`build/system-tests/clang/dwarf-verifier/probe.wasm` to the indicated path:

```sh
upload /tmp/dwarf-verifier.wasm
chmod +x /tmp/dwarf-verifier.wasm
/tmp/dwarf-verifier.wasm > /tmp/dwarf-verifier.bin
printf '%s\n' "$?" > /tmp/dwarf-verifier-status.txt
wast --verbose /root/test/system/clang/dwarf-verifier/dwarf-verifier.wast > /tmp/dwarf-verifier-results.txt
echo "$?"
download /tmp/dwarf-verifier-results.txt
```

The observer exits 31; the WAST test checks that status and exits zero when
all observations match. Save downloaded evidence under `build/html-rt`.
