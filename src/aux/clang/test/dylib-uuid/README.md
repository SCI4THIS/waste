# Mach-O reader UUID formatting

The real TextAPI `DylibReader.cpp` guest branch reuses LLVM's existing
`raw_ostream::write_uuid` instead of unavailable C++ locale streams. The
native and threaded branch keeps upstream stream manipulators. No replacement
UUID formatter or binary parser is introduced.

```sh
make -C src/aux dylib-uuid-fixture dylib-uuid-native-source
make -C src/aux test-dylib-uuid
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-dylib-uuid \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

The observer includes the complete production reader to call its private
`readMachOHeader` without adding test hooks. It constructs actual Mach-O bundle
images and parses them using the production `MachOObjectFile::create` API.
Zero, all-ones, mixed high-bit and missing UUIDs are each observed in little-
and big-endian 32- and 64-bit images. All 35 expectations live in WAST: exact
uppercase hex, leading zeros, separators, empty missing UUIDs and process exit.
Owned outputs survive destruction of parsed objects, slices and source bytes,
then a shared sleep yield. The fixture uses the installed CRT and shared libc.

The guest's standalone production object and the complete source's native,
native-threaded and guest-threaded syntax checks are separate gates. These
checks do not establish a threaded guest runtime or complete compiler
acceptance.

After installing test snapshots and rebuilding `bash.html`, run:

```sh
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/dylib-uuid/browser.json
```

The worker expects 36 results including launch. This exercises the production
browser C runtime without opening a GUI browser.
