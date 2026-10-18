# LLVM managed static registry

LLVM's actual `ManagedStatic.cpp` now selects its existing counted recursive
`SmartMutex<true>` only for `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`.
Native and threaded profiles retain `std::recursive_mutex`. The upstream
single-thread registration path and registry/destruction code remain unchanged.
This compiler-local adaptation does not change WASTE's kernel threading model.

```sh
make -C src/aux test-managed-static
make -C src/aux managed-static-native-source
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-managed-static \
  WASM_RUNNER=../../build/cli-rt/wasm-sanitize
```

Finish any `make -C src/aux clang` attempt first: the fixture's prerequisite
may refresh the same guest CMake configuration. The probe compiles actual
staged LLVM `ManagedStatic.cpp` and links the current guest libc++/libc++abi
archives, production CRT and shared libc. Normal static-destructor registration
flags remain enabled. ManagedStatic handles are trivially destructible; explicit
`llvm_shutdown` owns their registry cleanup. This is separate from the still
missing C++ static-exit callbacks (`__cxa_atexit`), and does not establish full
LLVM or C++ runtime startup/shutdown acceptance.

All 91 functional assertions live in `managed-static.wast`. The C++ observer
records lazy nested construction, const/mutable identity, explicit destruction
of the last registration, reverse shutdown with a live destructor dependency,
empty/repeated shutdown, reuse, scoped shutdown, custom allocation/deletion,
array element destruction and claimed-object lifetime. A claimed object is
manually deleted before shutdown, which must not delete it twice; its handle is
reused only after shutdown clears the original registration. Results are saved
under `build/system-tests/clang/managed-static/run-*/`.
The native source gate compiles the same actual implementation with host
headers and checks its mutex alias, including an explicitly threaded guest
preprocessor branch; it does not claim guest pthread support.

Refresh installed WAST snapshots and package the page before checking the
production browser worker:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/managed-static/browser.json \
  > build/engine/logs/clang-browser-managed-static-results.json \
  2> build/engine/logs/clang-browser-managed-static-errors.log
```

An optional manual browser check can use the same fixture. Upload
`build/system-tests/clang/managed-static/probe.wasm` to `/tmp/managed-static.wasm`
in `bash.html`, then run:

```sh
chmod +x /tmp/managed-static.wasm
/tmp/managed-static.wasm > /tmp/managed-static.bin
printf '%s\n' "$?" > /tmp/managed-static-status.txt
wast --verbose /root/test/system/clang/managed-static/managed-static.wast > /tmp/managed-static.txt
download /tmp/managed-static.txt
```

Expect `WAST: 91 PASS, 0 FAIL, 91 total`. The worker adds the launch assertion.
These checks establish the managed registry boundary, not runnable Clang/LLD.
