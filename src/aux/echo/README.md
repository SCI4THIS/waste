# echo

Build with `make -C src/aux echo`; output is `build/aux/echo/echo.wasm`.
Install `/usr/bin/echo` with `make -C src/aux install-echo`.

`sources.mk` selects upstream objects from the read-only Coreutils dependency.
Shared staging, configure, compatibility headers and library rules live in
[`../coreutils`](../coreutils/README.md); upstream C sources are not duplicated.
