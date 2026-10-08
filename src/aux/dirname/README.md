# dirname

Build with `make -C src/aux dirname`; output is `build/aux/dirname/dirname.wasm`.
Install `/usr/bin/dirname` with `make -C src/aux install-dirname`.

`sources.mk` selects upstream objects from the read-only Coreutils dependency.
Shared staging, configure, compatibility headers and library rules live in
[`../coreutils`](../coreutils/README.md); upstream C sources are not duplicated.
