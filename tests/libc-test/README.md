# libc-test

These are WASTE's guest-libc regression tests. `allocator.wast` is generated
from the actual `libc/waste-libc.wat` module and
`allocator-client.wast.inc`, preventing the browser tests from exercising a
copy of the allocator.

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

The generated `.wast` fixture is embedded in the `libc-test` group of the
offline browser dashboard.
