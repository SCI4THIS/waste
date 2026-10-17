# Guest Clang and Wasm linker

Clang and LLD build from the read-only `submodules/llvm-project` checkout.
`stage.sh` copies sources under `build/aux/clang/source` and applies the
repository-owned `clang-waste.patch` there. All configuration, generated
sources, archives and executables stay under `build/aux/clang`.

Build, install and test from the repository root:

```sh
make -C src/aux clang CLANG_JOBS=1
make -C src/aux install-clang CLANG_JOBS=1
make -C src/aux test-clang CLANG_JOBS=1
```

Prerequisites are matching Clang/clang++, wasm-ld, llvm-ar, llvm-ranlib,
llvm-objcopy, Make, CMake 3.20 or newer, Python for shared-ABI inspection and
installation, Git, flock and standard shell utilities. Make downloads nothing.
`CLANG_CXX` and `CLANG_HOST_CC` select host compilers; `CLANG_CMAKE` selects
CMake, defaulting to PATH or the previously unpacked package-local CMake.
`CLANG_JOBS` defaults to one to bound host build memory. Avoid concurrent LLVM
builds against the same tree. Public SDK/configuration header changes can
invalidate substantial portions of the LLVM dependency graph.

The build separates host table generators, guest libc++/libc++abi and guest
LLVM/Clang/LLD into `native`, `cxx` and `guest` directories. Useful partial
targets are `clang-source`, `clang-sdk-check`, `clang-native-tools`,
`clang-cxx`, `clang-support`, `clang-core`, `clang-dwarf` and `clang-gsym`.
An unchanged `clang` build does no work; installation is an explicit operation.

`install-clang` checks imports against the real shared providers and installs:

- `/usr/bin/clang` and the selected non-LTO `/usr/bin/wasm-ld`;
- portable/WebAssembly headers under `/usr/lib/clang/MAJOR/include`;
- `/usr/lib/waste/cc/crt1.o` and the sibling `/usr/bin/clang.cfg` defaults;
- the upstream license under `/usr/share/licenses/clang`.

`clang-sdk-files` stages headers using upstream CMake install components and
reads the resource version from generated `Version.inc`. `install-clang-sdk`
explicitly publishes those headers, CRT, configuration and license. The same
installed VFS files are discovered by cli-rt and packaged by html-rt. HTML
packaging neither builds nor reinstalls these commands.

Inside guest Bash, the ordinary compile/link/run path is:

```sh
printf 'int main(void) { return 37; }\n' > /tmp/example.c
clang /tmp/example.c -o /tmp/example.wasm
/bin/wasm /tmp/example.wasm
printf '%s\n' "$?"
```

The result is 37. `clang.cfg` selects the guest root, public and matching
resource headers, and Wasm32 target. Its `$`-prefixed tail arguments add the
shared CRT and `/lib/libc.so.wasm` only for linking; `-c` and `-fsyntax-only`
remain separate actions. Generated commands import process memory/table and
use the existing shared-library namespaces. `/bin/wasm` accepts a readable
binary and forwards its arguments through ordinary image startup.

The installed linker accepts Wasm objects, archives and shared providers. It
rejects LLVM bitcode, including archived bitcode, with an explicit diagnostic;
compile without `-flto`. Ordinary per-file optimization remains available.
The larger LTO-enabled `build/aux/clang/wasm-ld.wasm` is retained for comparison.
See [the linker profile and measurements](lld-no-lto.md).

The compiler's optional Wasm `name` section is removed before import checks and
installation. Its named raw executable remains under `guest/bin` for debugging.
This preserves code, data and link metadata. The engine validates every body
and retains its encoding, then decodes functions on first call. Clones share
static code ownership; decoded functions stay alive until the last owner is
released. This is generic engine behavior, with no compiler-specific cache.
See [execution techniques](../../../docs/techniques.md).

LLVM's guest build generates WebAssembly code and disables compiler-internal
threads, exceptions, optional host libraries and machine backtraces. The private
single-thread adapters retain LLVM ownership semantics and preserve upstream
native/threaded branches. They do not replace WASTE's concurrent process/thread
scheduler. LLVM waits, clocks, alarms, child processes and I/O use the existing
shared libc/kernel paths, including the yielding sleep implementation. Optional
unavailable host capabilities return explicit errors; arbitrary POSIX host
functionality and general C++ exception/localization support are not claimed.

All seven package assertions live in `tests/installed.wast`. `tests/setup.sh`
only prepares inputs and captures observations: actual installed compiler
version, invalid C diagnostics, valid SDK compilation/linking, emitted-command
output/exit status and parent-shell recovery. The valid input also checks
SOURCE_DATE_EPOCH timestamp formatting and the debug-sleep pragma. The package
runner defaults to ten minutes; `WASTE_TEST_TIMEOUT_MS` overrides that budget.
Sanitizer execution can need a longer deadline:

```sh
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  WASTE_TEST_TIMEOUT_MS=1800000 make -C src/aux test-clang \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Refresh installed snapshots and rebuild the offline page after installation:

```sh
make -C src/html-rt vfs-tests-install vfs-tests-check bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/aux/clang/tests/browser.json
```

The production-worker scenario uses the installed compiler/linker and WAST
snapshot from the embedded VFS; only its setup script is staged. It expects
seven WAST results plus the outer Bash launch. It checks browser-engine
execution, without requiring Chromium or a GUI.

Compiler/libc/kernel interactions and actual upstream-source regressions live
under `src/system-tests/clang`, with assertions in WAST. In particular,
`test-unsafe-buffer` exercises real compiler-generated C++ fix-its; the linker
fixtures check object/archive linking, shared imports and bitcode rejection.
Each fixture owns its source, native driver, browser scenario and instructions.
The source-profile gates preserve native and threaded LLVM branches.

See [the Clang plan](../../../docs/active-clang-plan.md) for acceptance evidence,
measurements and port history, and [architecture](../../../docs/architecture.md)
for the engine/runtime boundary.
