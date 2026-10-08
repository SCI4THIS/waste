# date

Build with `make -C src/aux date`; output is `build/aux/date/date.wasm`.
Install `/usr/bin/date` with `make -C src/aux install-date`.

`sources.mk` selects upstream objects from the read-only Coreutils dependency.
Shared staging, configure, compatibility headers and library rules live in
[`../coreutils`](../coreutils/README.md); upstream C sources are not duplicated.
