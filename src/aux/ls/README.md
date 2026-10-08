# ls

Build with `make -C src/aux ls`; output is `build/aux/ls/ls.wasm`.
Install `/usr/bin/ls` with `make -C src/aux install-ls`.

`sources.mk` selects upstream objects from the read-only Coreutils dependency.
Shared staging, configure, compatibility headers and library rules live in
[`../coreutils`](../coreutils/README.md); upstream C sources are not duplicated.
