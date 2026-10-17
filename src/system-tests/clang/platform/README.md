# LLVM filesystem queries and C++ provider boundary

The guest's namespace exposes files and mappings, but does not expose volume
capacity or mount locality. Its installed-file packaging budget is not a disk
quota. Under `LLVM_WASTE_GUEST`, actual `disk_space` and both `is_local` overloads
return `function_not_supported` (ENOSYS), preserve errno and leave the locality
output unchanged. LLVM's Boolean wrappers conservatively return false. Native
builds retain their original statvfs/statfs implementation.

The fixture's actual libc++ system_error.cpp now uses normal static-destructor
registration through shared libc. The separate
[shutdown fixture](../shutdown/README.md) accepts C++ registration/finalization;
these query observations do not independently verify category cleanup.

LLVM's `CachePruning.cpp` propagates a capacity-query error when pruning by
bytes or available-space percentage. These pruning policies remain unavailable
in the guest. RealFileSystem and its forwarding wrappers propagate locality
errors. This does not disable required Path.cpp or claim compiler acceptance.

The fixture extracts the unchanged definitions of these three functions from
the staged Unix/Path.inc, keeping the license header and recording the complete
source hash. Its generator rejects changed function boundaries. Unrelated Path
methods still prevent a complete guest source compile, so this test-only unit
does not establish full Path support or replace any production implementation.
It uses actual generated LLVM configuration and the upstream inline IO sandbox.

The observer also compiles direct C++ calls against the public dlfcn.h and
sys/mman.h declarations, whose C linkage must bind to existing providers.
Anonymous mappings check zero initialization, protection changes and lifetime.
Shared file mappings commit through msync; private copies preserve file data.
A 4-GiB offset must fail without narrowing the mmap import's i64 argument.
Dynamic loading uses a freshly compiled PIC library, function-pointer calls,
shared state and reference lifetime. Missing symbols/libraries and consumed
dlerror state are observed. Process-wide `dlopen(NULL)` remains unsupported;
this is recorded explicitly rather than establishing LLVM process-wide lookup.
RTLD flag enforcement and full LLVM Memory/DynamicLibrary execution are separate
acceptance work. No new libc, kernel, scheduler or sleep implementation is added.

All 81 functional assertions live in platform.wast. The observer emits a
512-byte block with 78 observations and their count at slot 127, then WAST reads
the actual file and saved process status. It links current C++ archives,
the ordinary guest CRT and the actual shared-libc provider without LTO, with
normal static-destructor registration.

```sh
make -C src/system-tests test-platform
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-platform \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Artifacts and native session results are under build/system-tests/clang/platform.
The build first configures its shared aux inputs; do not run another LLVM CMake
owner concurrently. Unchanged builds retain the compiled artifacts.

Refresh snapshots, audit them and rebuild the page sequentially, then run:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/platform/browser.json \
  > build/engine/logs/clang-browser-platform-results.json
```

Expect 82 worker passes, including Bash launch. A manual GUI check is optional.
Upload the compiled observer and library in bash.html and run:

```sh
upload /tmp/platform.wasm
upload /tmp/platform-library.so.wasm
chmod +x /tmp/platform.wasm
/tmp/platform.wasm > /tmp/platform.bin
printf '%s\n' "$?" > /tmp/platform-status.txt
wast --verbose /root/test/system/clang/platform/platform.wast > /tmp/platform.txt
echo "$?"
download /tmp/platform.txt
```

Expect 81 passes and status zero. Save downloaded evidence under build/html-rt.
