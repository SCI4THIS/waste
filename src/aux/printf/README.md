# printf

Build with `make -C src/aux printf`; output is `build/aux/printf/printf.wasm`.
Install `/usr/bin/printf` with `make -C src/aux install-printf`.

`sources.mk` selects upstream objects from the read-only Coreutils dependency.
Shared staging, configure, compatibility headers and library rules live in
[`../coreutils`](../coreutils/README.md); upstream C sources are not duplicated.
