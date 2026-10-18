# Layout-randomization seed-file input

CompilerInvocation's guest branch uses the private WasteSeedFile.h adapter to
read the first line through existing shared libc fopen/getline/close APIs.
The wrapper reuses WasteFileInput, also used by LayoutOverrideSource; each
reader owns its file and getline buffer.
Native/threaded branches keep std::ifstream/std::getline. Open-failure
diagnostics and the later explicit seed argument remain in the original
production option parser. No new filesystem or runtime adapter is introduced.

```sh
make -C src/aux seed-file-fixture seed-file-source
make -C src/aux test-seed-file
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-seed-file \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

All 20 functional assertions live in seed-file.wast. The observer calls the
same private reader as production against real VFS files. It checks missing
and empty files, LF first-line selection, spaces/tabs/CR, embedded NUL bytes,
missing final LF, an empty first line and a 4097-byte line. Owned results outlive
closed files, freed getline buffers and shared sleep. A complete observation
packet is compared with the original native ifstream/getline code, which reads
separate generated files under build/. Missing files preserve the previous
seed; successfully opened empty files clear it. Only LF is removed.
The fixture loads the installed production CRT and shared libc.

The source gate compiles the complete CompilerInvocation.cpp production object
and checks its full native/native-threaded/guest-threaded sources using host C++
headers and matching generated headers. These do not establish a threaded guest
runtime or complete compiler execution.

After refreshing installed snapshots and rebuilding bash.html:

```sh
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/seed-file/browser.json
```

The production worker expects 21 results including launch. This accepts the
reader boundary, not an invocation of the still-unbuilt guest compiler. Actual
option handling remains subject to compiler acceptance after installation.
The pending unsafe-buffer compiler scenario also supplies a real two-line
seed file, covering this option path during its actual compiler invocation.
