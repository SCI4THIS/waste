# true

Build with `make -C src/aux true`; output is `build/aux/true/true.wasm`.
Install `/usr/bin/true` with `make -C src/aux install-true`.

`sources.mk` selects upstream objects from the read-only Coreutils dependency.
Shared staging, configure, compatibility headers and library rules live in
[`../coreutils`](../coreutils/README.md); upstream C sources are not duplicated.
