# GNU M4 guest command

`make -C src/aux -j4 m4` builds `build/aux/m4/m4.wasm` and
`make -C src/aux install-m4` installs `/usr/bin/m4` with mode 0755.
`test-m4` runs package assertions inside the installed Bash. The aux wizard
and VFS installer expose the same explicit build/install flow. HTML packaging
only reads the installed tree.

## Sources and build

The pinned `submodules/m4` master commit contains only a redirect README.
`source-revision` selects the local Git object for stable GNU M4 1.4.21;
`gnulib-revision` selects its matching Gnulib commit. `stage.sh` exports the
M4 objects from `submodules/m4` and Gnulib objects from the existing
`submodules/coreutils/gnulib` Git database. Neither checkout changes. Initialize
both repositories with full history before building. No branch checkout,
network fetch, downloaded release tarball or submodule writes are needed.

All staging, patching, Gnulib imports, Autotools generation, configuration and
compilation happen under `build/aux/m4`. `BUILD_DIR` can select another aux
output directory inside this repository's `build/`. The package pins both
revisions independently of the checked-out branch. Exported source snapshots
with matching `.waste-source-revision` markers also work, including the
corresponding-source bundle.

Host prerequisites are Clang, wasm-ld, make, ar, ranlib, git, tar, patch,
Python 3, autoconf, automake, autopoint, host m4, gperf and flock. Bootstrap
runs autopoint before Gnulib import so old gettext macros cannot overwrite
the pinned Gnulib macros. It omits translation downloads and the external
maintainer bootstrap module. Only the command and required support library
are compiled; host documentation generators and upstream test executables
are not part of this target.

The compiler uses the mounted SDK without host-header fallback, the existing
private Gnulib declaration bridges under `src/aux/coreutils/include`, PIC
objects, imported process memory/table and the two-argument shared guest CRT.
Matching function imports are assigned to the installed shared libc and
signature checked. The shared libc supplies opaque stream-state queries for
Gnulib, plus one-byte `ungetc` pushback used by M4's input lookahead; its
private FILE layout stays inside libc. No separate libc is embedded and no
Asyncify is used.

`waste.patch` connects `syscmd` and `esyscmd` to `process.c`, which uses the
existing guest fork/exec/pipe/wait APIs and `/usr/bin/bash`. It preserves M4's
exit/signal status encoding without requiring Gnulib's general posix_spawn
implementation. NLS, libsigsegv stack handling and optional crash handlers
requiring unsupported one-shot signal flags are disabled. The current
SDK uses single-byte character classification; this is not a claim of full
locale, POSIX or upstream test-suite conformance. The kernel's inherited
regular-file-offset limitation also applies when parent and child both write
to the same redirected file; command capture uses a pipe.

The temporary-name fallback uses Gnulib's existing `clock_gettime` seed and
exclusive creation, without importing the unavailable process CPU clock.

Installation preserves the upstream GPLv3-or-later notice at
`/usr/share/licenses/m4/COPYING`. Generate release corresponding source with
`make -C src/aux m4-source-package`. The archive under `build/aux/m4` contains
pristine pinned M4/Gnulib sources, patches, build inputs, configuration records,
shared-libc source dependencies and the binary digest. Publish it with releases.

## Tests

Authored tests live in `src/vfs/root/test/aux/m4`; `test/system` is a symlink
there. `setup.sh` prepares inputs and captures `/usr/bin/m4` output/status;
all expected results live in `installed.wast`. Coverage includes argument
expansion, quoting, definition stacks, conditionals, integer arithmetic,
diversions, string and regex operations, recursion, m4wrap, includes and `-D`,
frozen-state reload, shell command capture/status, explicit exit and errors.
`WASM_RUNNER` selects the native or sanitizer runtime, and
`WASTE_TEST_TIMEOUT_MS` overrides the five-minute package deadline.

```sh
make -C src/aux test-m4
make -C src/html-rt vfs-tests-install bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html src/aux/m4/test/browser.json
```

In the offline page's Bash shell:

```sh
. /root/test/aux/m4/setup.sh
/bin/wast --verbose /root/test/aux/m4/installed.wast
```

Verification logs belong under `build/engine/logs/`; native result JSON goes
under `build/aux/m4/tests`.
