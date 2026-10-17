# Shared null device

`null.wast` owns 67 assertions for the kernel-created `/dev/null`. Run from
Bash with shared libc. It checks character metadata and inode identity,
zero-sized storage, EOF/canaries, discarded writes, access modes, immediate
read/write readiness, seek-to-zero, dup/dup2 and descriptor flags, openat,
directory entry type, symlink resolution, hard-link lifetime and path errors.
It leaves `/dev/null` in place and removes its temporary aliases.

The device follows the EOF/discard contract described in
[null(4)](https://man7.org/linux/man-pages/man4/null.4.html). Seeking with the
three ordinary seek origins returns zero, following the
[Linux null-device implementation](https://github.com/torvalds/linux/blob/master/drivers/char/mem.c).
Invalid seek origins return EINVAL. Raw guest read/write keep the existing
negative-errno contract; the test records that boundary explicitly.
Device creation/mapping, general device ioctls and file-status flag mutation
are separate capabilities. Truncation rejects this nonregular descriptor.

Build/install before starting read-only guest sessions:

```sh
make -C src/system-tests test-devices
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/system-tests test-devices \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Native results live under `build/system-tests/devices/run-*/results.json`.
Refresh installed snapshots and finish packaging before using the worker:

```sh
make -C src/html-rt vfs-tests-install vfs-tests-check bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/devices/browser.json \
  > build/engine/logs/clang-browser-null-results.json
```

Expect 68 worker passes including launch. For an optional Firefox check:

```sh
wast --verbose /root/test/system/devices/null.wast > /tmp/null-results.txt
printf '%s\n' "$?"
download /tmp/null-results.txt
```

Expect 67 passes and status zero; save evidence under `build/html-rt`.
The [actual LLVM execution fixture](../clang/program-execution/README.md)
checks empty stdin/stdout/stderr paths and detached children through this same
device. Private sanitizer checks in `tests/posix-kernel.c` retain the orphan
inode after both namespaces unlink it and the parent kernel is destroyed.
