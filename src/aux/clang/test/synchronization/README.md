# LLVM compiler synchronization profile

The bootstrap compiler and libc++ have threads disabled. The owned LLVM patch
adapts six audited classes under `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`:
PerThreadAllocator, FileCollector, HashingOutputBackend, AtomicLineLogger,
SmartRWMutex and BalancedPartitioning's private pool. The first four use
LLVM's existing guest recursive mutex, with lock guards deriving their type
from the member. BalancedPartitioning omits the unused mutex/condition-variable
members; its task methods already reject disabled-thread use upstream.

SmartRWMutex retains counted reader/writer ownership and rejects conflicting
try-locks. Its scoped reader releases once and cannot be copied or moved.
Native and threaded branches retain their original synchronization backends.
These adaptations serve a single compiler execution thread; they provide no
inter-thread exclusion and do not implement WASTE kernel synchronization.

```sh
make -C src/aux test-synchronization
make -C src/aux synchronization-native-headers
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-synchronization \
  WASM_RUNNER=../../build/cli-rt/wasm-sanitize
```

The fixture compiles the real patched LLVM headers with guest-generated
configuration and links the current bootstrap libc++/libc++abi archives.
It also compiles LLVM's actual `Allocator.cpp` for its atomic process-unique
ID function; unused printer sections are discarded without substitutes.
The observer now retains normal static-destructor registration. The shared libc
provider executes its compiler-generated cache callbacks on normal exit; the
separate [shutdown fixture](../shutdown/README.md) checks static cleanup order,
actual cache backing-allocation release and process-image isolation explicitly.
An aligned observer allocator records generic PerThreadAllocator construction,
cache reuse, allocation/deallocation forwarding, move construction/assignment,
reset/red-zone forwarding, ID growth and destruction. Its storage is a bounded
member buffer. This verifies the real generic wrapper and archive allocation
paths, not concrete BumpPtrAllocator slab behavior. The actual bump template and
hash backend are instantiated in a separate compile gate. FileCollector and
AtomicLineLogger's complete file behavior remains separate acceptance work.

All 97 functional assertions are in `synchronization.wast`. They also exercise
both SmartRWMutex variants, nested/scoped readers, exclusive writers, counted
try-lock ownership and noncopyable types. Unowned-release diagnostics are not
counted as verified by these positive ownership tests.

The separate native declaration gate checks that native and guest/threaded
branches retain standard mutex backends using the host's real C++ library.
Its preprocessor branch selection is not guest pthread capability evidence.

The build input prerequisite refreshes guest configuration when needed; finish
other aux Clang builds before invoking it. Generated outputs and native logs
live under `build/system-tests/clang/synchronization`. Refresh snapshots and
rebuild the production page in sequence:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/synchronization/browser.json \
  > build/engine/logs/clang-browser-synchronization-results.json
```

Expect 98 passing worker results including Bash bootstrap, and exit status zero.
No manual browser test is required for this compiler-profile boundary. For an
optional manual check, upload
`build/system-tests/clang/synchronization/probe.wasm` as
`/tmp/synchronization.wasm` in the rebuilt page, then run:

```sh
chmod +x /tmp/synchronization.wasm
/tmp/synchronization.wasm > /tmp/synchronization.bin
printf '%s\n' "$?" > /tmp/synchronization-status.txt
wast --verbose /root/test/system/clang/synchronization/synchronization.wast > /tmp/synchronization-results.txt
echo "$?"
download /tmp/synchronization-results.txt
```

Expect 97 passes and status zero. Save downloaded evidence under `build/html-rt`.
