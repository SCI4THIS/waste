# LLD filesystem and output ownership

This fixture compiles the complete staged `lld/Common/Filesystem.cpp`, actual
LLD diagnostics/context/memory and LLVM CodeGen/MC option initialization. It
links the production Support/Demangle and C++ archives, CRT and shared libc.
C/C++ only records observations; all 196 functional expectations live in
`filesystem.wast`.

The explicit `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS` profile selects the
upstream one-thread `unlinkAsync` fallback: leave the destination for the
ordinary output writer to replace. Requested strategy counts of zero or eight
cannot enable threads in this profile. Native and threaded branches keep the
detached-close optimization. No compiler scheduler or filesystem provider is
added.

WAST verifies that preflight and abandoned output retain existing bytes and
inode, mapped executable output commits after shared POSIX sleep, and an open
descriptor retains the old unlinked inode and bytes after replacement. It also
checks temporary-file cleanup, repeated discard with a live mapping, real
in-memory and zero-size writers, ordinary truncation/buffering/positional
writes, LTO read/seek/write, `/dev/null` fallback and exact failure diagnostics.

The public `inttypes.h` pointer-format macros use the compiler's format strings
for the matching `stdint.h` pointer aliases. Compiled C and C++ calls check all
six macros, high bits, signed bounds and a following 64-bit argument. LLVM's
actual `DataRefImpl` formatter checks the `PRIxPTR` dependency that prevented
the production Common DWARF source from compiling. No formatter implementation
is copied into the fixture.

Run serially from the repository root:

```sh
make -C src/aux lld-filesystem-fixture lld-filesystem-native-source lld-filesystem-guest-source
make -C src/aux test-lld-filesystem
make -C src/aux test-lld-filesystem WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
make -C src/html-rt vfs-tests-install vfs-tests-check bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html src/aux/clang/test/lld-filesystem/browser.json > build/engine/logs/clang-browser-lld-filesystem.json
```

Keep one LLVM build owner and one job; finish VFS installation before running
sessions. Guest source checks use the exact generated CMake/PCH rules. Native
and forced threaded-guest gates compile the complete filesystem helper with
warnings as errors. `lld-common-guest-source` enumerates all actual Common
object rules; `lld-wasm-guest-source` attempts those and the Wasm driver rules,
including the real generated options and IR PCH prerequisites. These broader
compile attempts can expose the next platform blocker and do not establish
final linker execution.

Direct CMake source rules need their generated `depend` target refreshed in a
separate Make invocation before compiling. Otherwise a changed SDK header can
leave the real PCH stale. Common also needs `llvm_vcsrevision_h` and its own
`VCSVersion.inc`. Both native-generator and guest configurations explicitly
use the LLVM submodule revision/repository for version metadata; the staged
source tree must not inherit the containing WASTE repository's identity.

Original native results and transcripts are retained in fresh directories under
`build/system-tests/clang/lld-filesystem`; `lld-filesystem-native.json` and
`lld-filesystem-sanitize.json` contain the latest results. The same artifact,
WAST and controlled-clock events run in the packaged production worker without
a GUI. Sanitizer sessions use ASan/UBSan with leak detection disabled under the
established sandbox profile. Successful sessions report 197/197 including the
launch assertion; the observer returns 27, which WAST checks before Bash exits
zero.

The installed `/root/test/system/clang/lld-filesystem/filesystem.wast` snapshot
requires the observer's generated files. Running it alone in a fresh Bash is
not the complete scenario; isolated batch execution skips it. This fixture
installs no compiler or linker executable. Full LLD/Clang compile, link and run
acceptance remains in the active plan.
