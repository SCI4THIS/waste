# LLVM guest platform and file metadata boundary

The owned LLVM patch selects its POSIX implementations for the explicit
`LLVM_WASTE_GUEST` profile. CMake remains `Generic`; the compiler remains
`wasm32-unknown-unknown`, with no Linux/Emscripten/host Unix macros. Native
table generators and the libc++ bootstrap keep their separate profiles.
This is the initial platform port, not acceptance of all LLVM POSIX operations.

The public guest `stat` structure holds 64-bit size, device and inode fields,
while the existing `off_t`, `dev_t` and `ino_t` typedefs are 32-bit. LLVM's
private status representation derives its storage and constructor argument
types from the actual structure fields, preserving metadata without changing
installed command ABIs. Configure probes the separate `st_mtime_nsec` field;
the Unix status conversion uses it rather than discarding fractional seconds.

```sh
make -C src/aux clang-sdk-check
make -C src/aux test-filesystem
make -C src/aux test-filesystem-controlled
```

`filesystem-fixture` configures the staged LLVM prerequisites and builds a
compiled C++ observer with the host cross-compiler. It includes the actual
patched `FileSystem.h` and generated guest configuration. It does not require
a completed guest Clang. Functional assertions live entirely in the two WAST
files; reports/results go under `build/system-tests/clang/filesystem`.

The 63 assertions check platform/capability flags, unchanged public ABI widths,
LLVM's inline size storage beyond 32 bits, constructor timestamps, real C++
`stat`/`lstat`/`fstat`/directory-relative `fstatat` calls, errno, full metadata
agreement, modification timestamps within actual sampled clock bounds, and
output guards and preservation on ENOENT/EBADF. Synthetic large sizes exercise header storage;
they do not assert that the VFS supports files above its byte limit. The full
out-of-line LLVM path/status functions and device/inode accessors still need
the complete POSIX source to compile and link.

The controlled target uses the existing private kernel-clock harness, setting
realtime to 4,294,967,313 seconds plus 123,456,789 nanoseconds. It repeats the
63 assertions and adds five exact clock assertions from `clock.wast`; the
interval checks therefore verify the same exact timestamp in all four real
status calls. Timestamp mutation through `utimensat`/`futimens` remains
unsupported. No timestamp adapter or synthetic filesystem is substituted.
`clock.wast` requires this injected-clock scenario and must not run with an
ordinary live clock. The compiled report contains 16 metadata i32s, eight LLVM
values, eight operation results, four i64 clock bounds and six guarded stat
records (1,104 bytes).

After building `build/cli-rt/wasm-sanitize`, repeat the same compiled fixture:

```sh
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-filesystem \
  WASM_RUNNER=../../build/cli-rt/wasm-sanitize
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-filesystem-controlled \
  FILESYSTEM_SESSION_RUNNER=../../build/cli-rt/private/guest-session-sanitize
```

Refresh snapshots, audit them, then rebuild the page in sequence; installers
and the snapshot audit must finish before runtime tests start:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/filesystem/browser.json \
  > build/engine/logs/clang-browser-filesystem-results.json
```

Expect 69 passing worker results including Bash bootstrap and exit status zero.
A fixed realtime clock is injected by this scenario; `clock.wast` verifies it.
A GUI test is optional for this boundary. To run manually in the rebuilt page,
upload `build/system-tests/clang/filesystem/probe.wasm` as `/tmp/filesystem.wasm`:

```sh
chmod +x /tmp/filesystem.wasm
/tmp/filesystem.wasm > /tmp/filesystem.bin
printf '%s\n' "$?" > /tmp/filesystem-status.txt
wast --verbose /root/test/system/clang/filesystem/filesystem.wast > /tmp/filesystem-results.txt
echo "$?"
download /tmp/filesystem-results.txt
```

Expect 63 passes and status zero. Save downloaded evidence under `build/html-rt`.
