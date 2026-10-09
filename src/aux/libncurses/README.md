# Shared guest ncurses library

Run `make -C src/aux libncurses` to build
`build/aux/libncurses/libncurses.so.wasm` with Clang and `wasm-ld`. This is the
shared library loaded by Rogue. It is not a standalone shell command.

The read-only `submodules/ncurses` checkout supplies the upstream sources and
license notices. `stage.sh` copies them into `build/aux/libncurses/source`.
Configure and all upstream generators run under `build/aux/libncurses/build`;
objects and dependency files live under `build/aux/libncurses/objects`.
`BUILD_DIR` can select another aux output directory under the repository's
`build/` tree.

- `sources.mk` selects the existing base, tty, terminfo and trace sources.
- `config.site` supplies the wasm32 feature profile, including the supported
  select timing path and ncurses's four-byte unsigned Boolean ABI.
- `cc.sh` uses installed guest public/compiler headers for configure probes.
  Host headers never supply the guest ABI.
- `fallbacks.sh` uses host `tic` and `infocmp` to embed `xterm`, `xterm-256color`,
  `vt100` and `dumb`. Missing or failed generation aborts the build.
- `shared-stdio.c` binds the library's stdio pointers to the current process's
  shared libc through its constructor.

Build prerequisites are Make, Clang, `wasm-ld`, a host C compiler (`HOST_CC`,
default `cc`), `tic`, `infocmp`, and standard shell tools. Python is used only
by the existing shared-libc ABI helper and SDK installer. The library remains
PIC with imported memory/table, without Asyncify or an embedded libc.

Run `make -C src/aux install-libncurses` to check the actual shared-libc
provider and atomically publish the library to `src/vfs/lib/libncurses.so.wasm`
and its `/usr/lib` compatibility copy, with matching public ncurses headers.
Publication uses `flock` and the existing SDK installer. `start.sh` delegates
to this target; HTML packaging consumes the installed snapshots.

After installation, verify the SDK and both production runtimes:

```sh
make -C src/html-rt guest-sdk-check bash-html
python3 tests/guest-session-check.py --scenario rogue-fresh --scenario rogue \
  --scenario shared-dependencies --page build/html-rt/bash.html
```
