# GNU Awk guest command

`make -C src/aux -j4 awk` builds `build/aux/awk/awk.wasm` from the pinned,
read-only `submodules/gawk` checkout. Initialize it with
`git submodule update --init submodules/gawk` first. Sources, patched files,
configure output, objects and logs stay under `build/aux/awk`. Override
`BUILD_DIR` using the normal aux Make convention, with a directory under
this repository's `build/` tree.

The build needs Clang/clang++, wasm-ld, make, ar, ranlib, patch, tar, Python 3
and flock. Shared-libc math also needs the initialized, pinned
`submodules/llvm-project` checkout. It uses upstream's checked-in configure
script and generated parsers;
Autotools, Bison and a host-built Gawk are not required. The mounted guest
SDK is the only source of public C headers. Function availability comes from
the built shared libc, supplemented by `config.site` for kernel imports
and unavailable declarations. Linking uses the shared guest CRT (two-argument
main), imported memory/table and shared libc. No Asyncify is used.

This profile uses the SDK's single-byte character model (`MB_CUR_MAX == 1`).
It retains Gawk's bundled regex engines, fields, associative arrays, functions,
formatted output, files, pipes, math builtins and ordinary process support. NLS, dynamic
extensions, Internet network files, MPFR, Readline and the persistent allocator
are disabled. It does not claim complete upstream or POSIX conformance. `waste.patch` renames a
DFA-private helper that conflicts with the SDK and implements its space
classification with the byte ctype API for this profile. It also routes child
shell execution through the installed Bash with the public `execv` ABI, and
emits the developer NODE dump in declaration order without a `sort` subprocess.

`make -C src/aux install-awk` checks actual shared-libc import signatures,
refreshes shared libc for its math providers, then publishes `/usr/bin/awk`
with mode 0755 through the serialized VFS installer. It also installs upstream COPYING under `/usr/share/licenses/awk`.
The wizard's aux menu delegates to this target. HTML packaging reads the
installed VFS and never builds or installs Awk implicitly.

For release distribution, `make -C src/aux awk-source-package` writes
`build/aux/awk/awk-corresponding-source.tar.gz` with pristine Gawk sources,
repository patches/build inputs, configuration records and the executable hash.
Gawk is GPLv3-or-later; preserve the bundled notices and corresponding source.

The current kernel clones regular-file offsets across fork. Parent and child
writes to the same inherited redirected output can therefore overwrite bytes.
Use separate output files for child commands; the package pipe regression does
so. Terminal output and input through pipes are covered independently. This is
a runtime limitation, not an Awk conformance claim.

## Tests

`make -C src/aux test-awk` runs `test/system/installed.wast` using
`/bin/wast --verbose` inside the installed Bash. `test/system` points to the
canonical authored tests in `src/vfs/root/test/aux/awk`. `setup.sh` only prepares
inputs and records installed command results; all expectations are in WAST.
The tests check executable access, arithmetic/math/printf, fields and variables,
functions, associative arrays, regex/classes, substitutions/string operations,
script files, getline/redirection/close, pipeline input, explicit exit status
syntax errors, system commands and read/write command pipes. `WASM_RUNNER` can
select a native sanitizer runner; `WASTE_TEST_TIMEOUT_MS` overrides the
five-minute package deadline.

Run the same tests through the packaged production browser worker:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html src/aux/awk/test/browser.json
```

In the offline page's Bash shell:

```sh
. /root/test/aux/awk/setup.sh
/bin/wast --verbose /root/test/aux/awk/installed.wast
```

Keep verification logs under `build/engine/logs/`. The package test runner
stores per-run JSON under `build/aux/awk/tests`.
