# cat

Build with `make -C src/aux cat`; output is `build/aux/cat/cat.wasm`.
Install `/usr/bin/cat` with `make -C src/aux install-cat`.

`sources.mk` selects upstream objects from the read-only Coreutils dependency.
Shared staging, configure, compatibility headers and library rules live in
[`../coreutils`](../coreutils/README.md); upstream C sources are not duplicated.
