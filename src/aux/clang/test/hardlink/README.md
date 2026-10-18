# Guest hard links

`link` and `linkat` call the shared engine through `waste_kernel.linkat_v1`.
Directory entries share one retained inode, including its bytes and attributes.
Open descriptors and mappings retain it after unlink, without increasing its
link count. `link` and `linkat` with flags zero link a symlink itself;
`AT_SYMLINK_FOLLOW` resolves its final target. Directory links are rejected.
Relative inputs use their respective directory descriptors; absolute inputs
ignore those descriptors. Both strings are copied and validated before mutation.

`hardlink.wast` owns all 269 reported checks. A compiled C++ observer uses the
ordinary shared CRT and current C++ archives to exercise public declarations,
wrappers and a real fork/wait. The WAST checks shared content, permissions,
truncation, inode identity, rename between aliases, surviving unlinked descriptors,
symlink targets, dirfds, invalid flags/pointers, errno and failed-operation
preservation. The original metadata imports still write 48 bytes; the new
`fstatat_v2` writes the established 128-byte stat layout, including link counts.

Fork retains inode objects and their shared monotonic allocator. Names remain
in each kernel's cloned table and are reconciled at reap. This fixture verifies
new child names and shared file data, but does not establish immediate namespace
visibility or deletion reconciliation between processes. It does not run full
LLVM Path methods or establish the pending C++ static-exit registry.

```sh
make -C src/aux test-hardlink
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-hardlink \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect 269 passes and exit zero. Artifacts/session JSON live under
`build/system-tests/clang/hardlink`. Run installation, snapshot refresh/audit,
HTML packaging and guest sessions sequentially. For the production worker:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/hardlink/browser.json \
  > build/engine/logs/clang-browser-hardlink-results.json
```

Expect 270 worker passes, including Bash launch. An optional manual check uses a
fresh `bash.html` page. Run `upload /tmp/hardlink.wasm`, select
`build/system-tests/clang/hardlink/probe.wasm`, then run:

```sh
chmod +x /tmp/hardlink.wasm
/tmp/hardlink.wasm > /tmp/hardlink.bin
printf '%s\n' "$?" > /tmp/hardlink-status.txt
wast --verbose /root/test/system/clang/hardlink/hardlink.wast > /tmp/hardlink-results.txt
printf '%s\n' "$?"
download /tmp/hardlink-results.txt
```

Expect 269 passes and a zero status; save the download under `build/html-rt`.
Use a fresh page for repeats because the test intentionally leaves names behind.
