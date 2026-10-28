# Graphviz dot guest command

`make -C src/aux -j8 dot` builds `build/aux/dot/dot.wasm` without installing.
`make -C src/aux install-dot` installs shared libc and `/usr/bin/dot` (0755).
`make -C src/aux test-dot` runs the authored package WAST through the installed
Bash. The wizard and VFS installer expose the same command. HTML packaging
consumes the installed filesystem and never rebuilds this binary.

## Sources and profile

Inputs come from the read-only `submodules/graphviz` checkout. The current
checkout is Graphviz 16.1.1 development; the version banner includes its Git
revision. `stage.py` copies sources to `build/aux/dot/source`, applies the
repository patch and runs the upstream Bison/Flex and color/entity generators
there. All compilation, dependency files and link outputs stay under `build/`.
`BUILD_DIR` supports another output directory beneath the repository `build/`.
The selected C source list follows the upstream CMake library lists, without
requiring CMake or probing host rendering libraries.

Host requirements: Clang/clang++, wasm-ld, make, Python 3, Bison, Flex, patch,
Git and flock. Compilation uses C17, PIC, the mounted guest SDK and its shared
two-argument CRT. Matching function imports are rewritten to shared libc and
signature checked. Graphviz libraries and selected plugins are linked into
the command; libc stays shared. No Asyncify is used.

The `dot` hierarchical layout and built-in core renderers are registered in
`builtins.c`. Tested output includes SVG, plain, canonical DOT, positioned DOT,
JSON and xdot. Graphviz's built-in font metrics estimate label dimensions;
there is no font discovery or Pango/Cairo dependency. Other layout engines,
orthogonal routing, external plugin loading, raster/PDF output, compression,
and Expat HTML-like label parsing are outside this profile. Ordinary quoted
labels and SVG XML escaping work. `waste.patch` keeps the private string helper
distinct from the SDK helper, removes file-lock calls in this single-threaded
build, and reports monotonic elapsed time rather than unavailable CPU time.

Shared libc supplies the additional portable LLVM binary64 functions and
`strndup`. Shared process startup applies PIC static-data relocations before constructors, so
callbacks referring to imported functions work across multiple input graphs.
The SDK includes Clang's licensed `stdatomic.h`, C17 static assertions,
ASCII classification and the `RAND_MAX` matching the existing provider. These
are shared facilities rather than embedded copies of libc in Graphviz.

## Tests

Canonical observations/setup and WAST live in `src/vfs/root/test/aux/dot`;
`test/system` points there. Setup records `/usr/bin/dot` output and status;
all expectations are in `installed.wast`. Tests cover real geometry/rank
direction, edges, labels, colors, XML escaping, clusters, multiple graphs,
`-o`, version output and malformed input/unsupported formats/missing files.
Shared math and bounded-string tests live in the libc package.

```sh
make -C src/aux test-dot
make -C src/html-rt vfs-tests-install bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html src/aux/dot/test/browser.json
```

In the offline page's Bash:

```sh
. /root/test/aux/dot/setup.sh
/bin/wast --verbose /root/test/aux/dot/installed.wast
```

`WASM_RUNNER` selects the native sanitizer runner; `WASTE_TEST_TIMEOUT_MS`
overrides the five-minute package deadline. Complete installations before
running concurrent tests that mount the VFS. Logs belong under
`build/engine/logs`, with package result JSON under `build/aux/dot/tests`.

## License and corresponding source

Installation preserves upstream `COPYING`, `LICENSE` and `EPL-2.0.txt` under
`/usr/share/licenses/dot`. Upstream source is available from
https://gitlab.com/graphviz/graphviz at the revision recorded in the staged
`.source-revision` file. Repository-owned changes live in this directory.

`make -C src/aux dot-source-package` generates
`build/aux/dot/dot-corresponding-source.tar.gz`. Publish it with binary releases.
It includes the selected pristine Graphviz sources, private configuration and
patches, generated-header inputs, SDK and shared-libc sources/dependencies,
licenses and executable digest. The exported snapshot revision marker permits
building without Graphviz Git metadata. In an extracted bundle, first run
`make -C src/aux install-libc`, then `make -C src/aux dot`.
