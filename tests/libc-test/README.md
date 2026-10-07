# libc-test

These are WASTE's guest-libc regression tests. The checked-in `*.wast.inc`
clients are combined with the actual merged guest-libc module under
`build/html-rt/waste-libc/tests/`. The browser therefore exercises the same binary ABI
as the standalone build without checking in copies of generated WAT.

Build the module and regenerate the test fixture with:

```sh
./start.sh --build-libc
```

Run the installed clients directly with the native C engine:

```sh
make -C src/cli-rt libc-native
make -C src/cli-rt libc-sanitize
build/cli-rt/waste-test --vfs-root=src/vfs --group=libc-test --list
build/cli-rt/waste-test --vfs-root=src/vfs libc-test/matching-sort.wast
```

The focused gates require every authored client in the installed manifest,
matching client bytes, and every parsed assertion in order. They preserve
the two tracked XFAIL fixtures while checking their five individual failing
expectations, so a further failure inside either file fails the gate. Reports
are saved under `build/cli-rt/`. Refresh edited clients with
`make -C src/html-rt vfs-tests-install vfs-tests-check`; ordinary runs consume
the installed tree without building HTML.

The same 14 fixtures are available through `/bin/waste-test --group=libc-test`
in the offline Bash page.

The separate allocator gate instantiates the actual guest-libc Wasm artifact
in the host WebAssembly implementation and remains a focused Node check:

```sh
node tests/libc-test/allocator-native.cjs
```

Its 5,000-allocation stress loop checks live ranges, endpoint bytes and memory
growth. It is distinct from the installed allocator WAST client.
