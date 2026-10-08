# false

Build with `make -C src/aux false`; output is `build/aux/false/false.wasm`.
Install `/usr/bin/false` with `make -C src/aux install-false`.

`sources.mk` selects upstream objects from the read-only Coreutils dependency.
Shared staging, configure, compatibility headers and library rules live in
[`../coreutils`](../coreutils/README.md); upstream C sources are not duplicated.
