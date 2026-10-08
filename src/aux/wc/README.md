# wc

Build with `make -C src/aux wc`; output is `build/aux/wc/wc.wasm`.
Install `/usr/bin/wc` with `make -C src/aux install-wc`.

`sources.mk` selects upstream objects from the read-only Coreutils dependency.
Shared staging, configure, compatibility headers and library rules live in
[`../coreutils`](../coreutils/README.md); upstream C sources are not duplicated.
