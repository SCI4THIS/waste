# Guest symbolic links

`symlink` and `symlinkat` are shared-libc wrappers over the versioned
`waste_kernel.symlinkat_v1` import. The engine validates both copied strings
before publishing an entry, stores the target verbatim and resolves relative
targets from the link's parent. Intermediate directory links work for lookup,
creation, rename, unlink, dirfd operations and executable loading. Resolve links
before `..`; do not collapse the input lexically before namespace traversal.

`symlink.wast` owns 187 functional assertions. A compiled C++ observer captures
public declarations, real wrapper calls, errno and configured pathname bounds;
it uses the ordinary CRT and current C++ archives. It does not exercise LLVM's
complete Path implementation or the pending C++ static-exit registry. WAST also
checks dangling links, loops, duplicate destinations, null/invalid/unterminated
pointers, readlink truncation/canaries, stat versus lstat, trailing slashes,
`O_EXCL`/`O_NOFOLLOW`, directory descriptors, chmod/chdir, rename replacement and
unlink lifetime. `symlink-loader.wast` checks two Bash command exit statuses after
executing a final link and a command beneath a directory link. The companion
hardlink fixture covers hard links; `realpath` has its own
[system fixture](../realpath/README.md). Host filesystem access and symlink
entries in the HTML transport format remain separate capabilities.

```sh
make -C src/aux test-symlink
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -C src/aux test-symlink \
  WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"
```

Expect 187 and 2 passes, with exit status zero. Artifacts and session JSON live
under `build/system-tests/clang/symlink`. Run LLVM configuration, installations,
snapshot refresh/audit and guest sessions sequentially. Then verify the packaged
production worker without launching a browser:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/html-rt bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/clang/test/symlink/browser.json \
  > build/engine/logs/clang-browser-symlink-results.json
```

Expect 190 worker passes, including Bash launch. For an optional manual check,
open the rebuilt `bash.html`, upload `build/system-tests/clang/symlink/probe.wasm`
under the name below and run in a fresh page:

```sh
upload /tmp/symlink.wasm
chmod +x /tmp/symlink.wasm
/tmp/symlink.wasm > /tmp/symlink.bin
printf '%s\n' "$?" > /tmp/symlink-status.txt
wast --verbose /root/test/system/clang/symlink/symlink.wast > /tmp/symlink.txt
result=$?
/tmp/symlink-command
printf '%s\n' "$?" > /tmp/symlink-loader.txt
/tmp/symlink-bin/true
printf '%s\n' "$?" >> /tmp/symlink-loader.txt
wast --verbose /root/test/system/clang/symlink/symlink-loader.wast >> /tmp/symlink.txt
last=$?
echo $((result | last))
download /tmp/symlink.txt
```

Save downloaded evidence under `build/html-rt`. A fresh page is needed for each
repeat because the test intentionally leaves entries for loader checks.
