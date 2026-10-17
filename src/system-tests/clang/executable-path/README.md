# Process image identity and LLVM executable discovery

`waste_get_executable_path` reads the pathname captured with the engine's owned
VFS image snapshot. It returns the byte length excluding NUL and copies the
terminator. Errors return -1 with errno; a short buffer remains untouched. This
is the resolved guest image pathname at load time, independent of argv, cwd,
later rename/unlink and shared-library callers. Fork inherits the identity and
successful exec replaces it. It does not expose the native host pathname or
provide a general realpath implementation.

The guest-only LLVM getMainExecutable branch uses that public shared-libc API.
Native branches retain their original implementations. Its private limits.h
derives PATH_MAX from src/config.h's existing VFS_PATH_MAX_BYTES, as does the
engine's image-path storage. This limits a resolved namespace pathname; the
separate POSIX input parser limit is unchanged. The guest toolchain searches
libc++ wrappers, private adapters, public SDK headers, then compiler headers.
The C++ bootstrap and native generator profiles keep their own header order.

The fixture extracts the actual getMainExecutable definition unchanged from
staged Unix/Path.inc, preserving the license and recording its full source hash.
Changed extraction boundaries fail explicitly. Required unrelated methods still
prevent full Path.cpp compilation; this test-only translation unit establishes
neither full Path support nor compiler acceptance. Current C++ archives and the
ordinary CRT retain normal static-registration flags. This fixture does not
exercise the pending static-exit registry.

The C++ observer captures API/LLVM calls and bytes. All 164 functional assertions
live in executable-path.wast. Relative dotted invocation must produce the
resolved pathname despite fabricated argv[0], a cwd change, rename and unlink.
Four fixed 640-byte slots capture the original process, its fork child, that
child's replacement image and the waiting parent. Explicit seeks keep evidence
independent of the pending cross-fork shared-file-offset semantics. WAST checks
strings, process identity,
errno, exact/short/zero capacities and canaries. Raw kernel calls additionally
exercise invalid pointers and a write crossing the end of memory without
partial modification. Unlinked open files remain readable/writable through
duplicate descriptors, including an inherited alias in the fork child; fstat
retains size and inode after unlink. The raw
getter is called from a WAST handler and must
still identify the active Bash process.

```sh
make -C src/system-tests test-executable-path
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-executable-path \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Artifacts and session results live under build/system-tests/clang/executable-path.
Do not run another LLVM CMake owner while this fixture configures its inputs.
Refresh snapshots, audit them and rebuild the page sequentially, then run:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/clang/executable-path/browser.json \
  > build/engine/logs/clang-browser-executable-path-results.json
```

Expect 165 worker passes, including Bash launch. For an optional manual check,
upload probe.wasm twice under the two names below, then run in bash.html:

```sh
upload /tmp/executable-path.wasm
upload /tmp/executable-path-copy.wasm
chmod +x /tmp/executable-path.wasm /tmp/executable-path-copy.wasm
cd /tmp
././executable-path.wasm > /tmp/executable-path.bin
printf '%s\n' "$?" > /tmp/executable-path-status.txt
wast --verbose /root/test/system/clang/executable-path/executable-path.wast > /tmp/executable-path.txt
echo "$?"
download /tmp/executable-path.txt
```

Expect 164 passes and status zero. The observer removes the first uploaded
executable while it runs; upload it again before repeating the test. Save any
downloaded evidence under build/html-rt.
