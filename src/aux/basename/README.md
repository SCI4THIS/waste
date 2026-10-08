# basename

Build with `make -C src/aux basename`; output is `build/aux/basename/basename.wasm`.
Install `/usr/bin/basename` with `make -C src/aux install-basename`.

`sources.mk` selects upstream objects from the read-only Coreutils dependency.
Shared staging, configure, compatibility headers and library rules live in
[`../coreutils`](../coreutils/README.md); upstream C sources are not duplicated.
