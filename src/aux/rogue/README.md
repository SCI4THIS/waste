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
- `wizard-password.patch`: use shared libc input/termios for the wizard startup
  password, handle EOF, and bound the in-game password buffer.
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

## Wizard password

The guest profile enables upstream `MASTER` support with password **wizard**.
During a game, press **+**, enter `wizard`, then Enter. A successful check prints
that you are suddenly as smart as Ken Arnold; press **+** again to leave wizard
mode. Incorrect passwords are rejected. To authenticate at startup, run
`rogue ""` and enter the same password. Startup input disables terminal echo
and restores the previous terminal settings afterward. Wizard games do not
count toward scores, following upstream behavior.

`config.h` supplies a complete DES password hash for upstream `md_crypt`/
`xcrypt`; the upstream default hash is incomplete. Password handling uses the
existing shared libc and engine terminal/read suspension path. No new runtime
or host password API is introduced. The source submodule remains unchanged.

The interactive regression checks an incorrect and correct in-game password,
leaving wizard mode, incorrect and correct startup passwords, and return to
Bash with the expected exit statuses:

```sh
python3 src/engine/test/guest-session/guest-session-check.py --scenario rogue-wizard \
  --page build/html-rt/bash.html
```

Its input contract is `test/wizard-session.json`. Rebuild the page after
installation, and reopen it before testing wizard mode manually.

After installation, rebuild the browser page and check the existing production
runtime scenarios without opening a browser:

```sh
make -C src/html-rt bash-html
python3 src/engine/test/guest-session/guest-session-check.py --scenario rogue-fresh --scenario rogue \
  --scenario shared-dependencies --page build/html-rt/bash.html
```
