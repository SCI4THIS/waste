# pwd

Build with `make -C src/aux pwd`; output is `build/aux/pwd/pwd.wasm`.
Install `/usr/bin/pwd` with `make -C src/aux install-pwd`.

`sources.mk` selects upstream objects from the read-only Coreutils dependency.
Shared staging, configure, compatibility headers and library rules live in
[`../coreutils`](../coreutils/README.md); upstream C sources are not duplicated.
