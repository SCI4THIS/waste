# LLVM guest host identity

The full `lld` graph first reached `TargetParser/Host.cpp`, where the generic
Unix include requires `sys/utsname.h`. A Wasm guest has no Unix host release to
detect. The owned LLVM patch selects a guest-only path that retains the
configured target triple and its optional environment override. Native LLVM
continues to use its upstream Unix host detection. Guest CPU name/features
remain LLVM's upstream generic/empty fallback for this architecture.

`probe.cpp` links the actual `LLVMTargetParser` and `LLVMSupport` archives,
records default/process triples and CPU identity, then returns 23. Functional
expectations are in `target-host.wast`. This boundary does not establish a
linked or runnable `lld` command.

From the repository root, with a single LLVM build owner:

```sh
make -C src/aux target-host-native-source target-host-fixture
make -C src/aux test-target-host
make -C src/aux test-target-host WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html src/aux/clang/test/target-host/browser.json
```

Build or refresh `bash.html` after installing the system WAST snapshot, then
run the packaged worker scenario. The CLI and worker both execute the actual
guest observer from Bash, then `/bin/wast --verbose` checks its output/status.
The initial source/graph probe log is
`build/engine/logs/clang-lld-full-graph-probe.log`.
