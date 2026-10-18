# Guest canonical paths

`realpath.wast` owns all 213 expectations. It verifies canonical absolute paths,
relative cwd lookup, symlinks before `..`, final/intermediate/absolute/chained
links, trailing slashes, missing components and loops. Caller-owned output is
NUL-terminated and preserved on failure; allocated output uses ordinary libc
malloc/free/realloc, remains independent of other results, and preserves errno
on success. The versioned kernel operation returns a length or negative errno
without changing libc errno. It validates both guest pointers before publishing
any output, including copies crossing a protected page.

The public API follows the [POSIX realpath contract](https://pubs.opengroup.org/onlinepubs/9699919799/functions/realpath.html).

`probe.cpp` records public C++ calls through the ordinary CRT, installed libc
and current libc++ archives. It does not execute LLVM's complete `real_path`
method. The full LLVM Path source build remains the compile acceptance gate.
Private tests in `src/vfs/root/test/diy-posix-test/posix-path-vfs.c` inject kernel credentials for owner,
group and other directory search checks; guest identity changes are not yet
synchronized with those kernel credentials. No new identity subsystem is added.

The canonical namespace bound comes from `src/config.h`; Clang's private
`limits.h` supplies it as PATH_MAX. Public callers should prefer
`realpath(path, NULL)` for an owned result. This slice does not add public
pathconf limits, a GNU canonicalize_file_name provider, or general POSIX limits.
Input parsing retains the engine's separate bounded pathname-reader limit.

```sh
make -C src/aux test-realpath
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-realpath \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
make -C src/cli-rt posix-path-vfs
```

Expect 213 WAST passes and exit zero. Artifacts and session JSON live under
`build/system-tests/clang/realpath`. Run VFS installations and guest reads
sequentially. Refresh the installed snapshot and build the single offline page:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/realpath/browser.json \
  > build/engine/logs/clang-browser-realpath-results.json
```

Expect 214 worker passes including Bash launch. An optional manual browser
check can use the same fixture: open a fresh `bash.html`, run
`upload /tmp/realpath.wasm`, select
`build/system-tests/clang/realpath/probe.wasm`, then run:

```sh
chmod +x /tmp/realpath.wasm
/tmp/realpath.wasm > /tmp/realpath.bin
printf '%s\n' "$?" > /tmp/realpath-status.txt
wast --verbose /root/test/system/clang/realpath/realpath.wast > /tmp/realpath-results.txt
printf '%s\n' "$?"
download /tmp/realpath-results.txt
```

Expect 213 passes and zero status; save the download under `build/html-rt`.
Use a fresh page/session before repeating because the fixture creates names.
Descriptor locks, yielding usleep and C++ static-exit registration remain
separate providers. Complete LLVM Support and Clang/LLD acceptance is pending.
