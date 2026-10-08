# Rogue guest build

Run `make -C src/aux rogue` to build `build/aux/rogue/rogue.wasm` with Clang
and `wasm-ld`. Upstream sources come from the read-only `submodules/rogue`
checkout. The Makefile copies them into `build/aux/rogue/src`, applies these
repository-owned patches, and writes objects/dependency files under
`build/aux/rogue/objects`.

- `platform.patch`: replace direct curses cursor access with `wmove`, and use
  environment/default fallbacks instead of passwd lookups.
- `debug-dump.patch`: retain the `g` command that saves `/tmp/rogue.dump`.
- `rogue-waste.patch`: give delayed callbacks exact Wasm function types and
  hide the terminal cursor while the game draws its player glyph.
- `config.h`: the explicit wasm32 package feature profile.
- `bool-fix.h`: consistently use ncurses's four-byte `NCURSES_BOOL` in Rogue
  structures, regardless of upstream include order.
- `imports.h`: assign public function declarations to `libc` and `libncurses`
  at compilation; keep legacy `fgets`/`vsprintf` declarations private to Rogue.

Compilation uses the installed headers directly, without a generated sysroot
or Wasm import-rewrite step. Kernel adapters and data relocation globals retain
their runtime bindings. Make rebuilds affected objects after header changes,
and re-stages originals before applying changed patches.

Run `make -C src/aux install-rogue` to check actual imports against the installed
libc/ncurses providers and atomically publish `/usr/bin/rogue` in `src/vfs`.
`start.sh` delegates Rogue installation to this target. Installation uses the
existing Python signature checker and VFS copy utility; compilation does not
use Python or build/install ncurses implicitly.

After installation, rebuild the browser page and check the existing production
runtime scenarios without opening a browser:

```sh
make -C src/html-rt bash-html
python3 tests/guest-session-check.py --scenario rogue-fresh --scenario rogue \
  --scenario shared-dependencies --page build/html-rt/bash.html
```
