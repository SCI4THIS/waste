# libc-test

These are WASTE's guest-libc regression tests. The checked-in `*.wast.inc`
clients are combined with the actual merged guest-libc module under
`build/waste-libc/tests/`. The browser therefore exercises the same binary ABI
as the standalone build without checking in four copies of generated WAT.

Build the module and regenerate the test fixture with:

```sh
./start.sh --build-libc
```

Run the generated fixture against either interpreter build with:

```sh
node tests/libc-test/libc-runtime.cjs
node tests/libc-test/libc-runtime.cjs threaded
node tests/libc-test/allocator-native.cjs
```

The generated `.wast` fixtures are embedded in the `libc-test` group of the
offline browser dashboard.
