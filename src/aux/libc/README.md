# Guest libc

`make -C src/aux libc` builds `build/aux/libc/libc.so.wasm` with Clang and
`wasm-ld --shared`. The sources consume the installed public SDK, PIC objects,
imported process memory/table and versioned engine-kernel adapters.
`make -C src/aux install-libc` explicitly publishes identical mode-0644 copies
at `src/vfs/lib/libc.so.wasm` and `src/vfs/usr/lib/libc.so.wasm`, using the common
VFS installation lock. The aux wizard installs libc before commands.

`make -C src/aux test-libc` runs the authored `tests/*.wast` through
`/bin/wast --verbose` in native Bash. Assertions live entirely in those WAST
files, and call the installed shared library through Bash's process memory,
table and `libc` provider. There is no embedded/static libc test build. The
allocator stress case performs 5,000 allocations with a bounded live ring,
checking alignment, overlap, reuse and retained bytes. Kernel/POSIX interaction
probes live in `src/system-tests/libc`; run them with
`make -C src/system-tests test-libc`. The CLI's `libc-native` and `libc-sanitize`
compatibility targets run both sets, with the latter selecting ASan/UBSan.

Refresh browser snapshots with `make -C src/html-rt vfs-tests-install`, then
rebuild `bash.html`. From its Bash prompt, run:

```sh
status=0
for test in /root/test/aux/libc/*.wast; do
  /bin/wast --verbose "$test" || status=1
done
printf 'libc test status: %s\n' "$status"
```

`--verbose` prints dots for passing results, `F` for failures, totals and the
first failure. Exit status is zero only when the test succeeds. Reports can
be redirected into `/tmp` and downloaded. These fixtures also run directly with `build/cli-rt/wast --verbose FILE`
or `wast --suite`, which supply the installed production providers in an
isolated runtime context.
Native Make tests stage the authored inputs directly into `/tmp`, and save
commands/results under `build/aux/libc/tests/run-*/`. `WASM_RUNNER` selects a
public `wasm` or `wasm-sanitize` frontend; `BUILD_DIR` overrides the aux output root.

Production builds need Make, Clang and `wasm-ld`; installation also needs
Python and `flock`. Tests additionally build/use the public native `wasm` frontend.
`tools/shared_libc.py` preserves exact signatures while assigning imports,
and also serves Bash, Coreutils and ncurses. SDK/sysroot checks live under
`src/system-tests/guest-sdk`. The retired `libc-fixtures` and `libc-sysroot`
targets have no replacement inside this package.

`runtime/` remains private freestanding interpreter support, operating on host
pointers independently of guest APIs. `waste-crt.c` supplies guest startup;
`include/helper.h` remains private. `make -C src/aux/libc` is a convenience
entrypoint for the same package rules.
