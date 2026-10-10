# Source-built guest Vim

```sh
make -C src/aux -j4 vim
make -C src/aux install-vim
```

The read-only `submodules/vim` checkout supplies Vim's sources. `stage.sh`
copies them into `build/aux/vim/source` and applies `vim-waste.patch` there.
Vim's in-tree configure/build runs in that staged copy at
`build/aux/vim/source/src`; the submodule remains read-only.
The executable is `build/aux/vim/vim.wasm`; installation explicitly publishes it
as `/usr/bin/vim`, with executable permissions and the upstream `LICENSE`.
`BUILD_DIR` may select another aux output directory beneath `build/`.

Prerequisites are Make, Clang, `wasm-ld`, a host C compiler (`HOST_CC`, default
`cc`), `patch`, and standard shell tools. Python only inspects/rewrites the
shared-libc ABI and installs files; it does not compile Vim. `start.sh` offers
Vim in the aux menu and delegates to `install-vim`.

Vim runs from inside Bash; it has no separate browser launcher. Invoke
`/usr/bin/vim` explicitly from a shell session (Bash is the shared session
bootstrap installed by `install-bash-launch`).

`cc.sh` uses the mounted guest SDK and package-private headers, without host
header fallback. Configure function answers are derived from the actual shared
libc provider; `config.site` selects supported kernel imports, cross-build
`vim_cv_*` answers (uname strings, timer availability, networking disabled)
and overrides features whose public declarations or layouts are unavailable.
The package uses the two-argument variant of the shared guest CRT and libc,
PIC/imported memory and table, and `--with-features=tiny` for the minimum
feature set. Asyncify is
not used; GUI, X11, scripting-language bindings, NetBeans, channels, XIM,
clipboard, mouse, GPM, sysmouse, SELinux/Smack/ACL, NLS and bidi are all
disabled.

Configure's `--with-tlib=ncurses` probe uses empty staging archives. The
executable resolves its actual terminal functions and `BC`, `UP`, `PC`, and
`ospeed` globals from shared libc's built-in ANSI termcap provider. Set
`vim_cv_terminfo=no` to match that provider's `tgoto` syntax; claiming terminfo
support makes Vim's `%p1`/`%p2` sequences produce incorrect cursor positions.
The installer checks named libc/libncurses function imports, while the launch
regression verifies complete runtime resolution.

The package installs minimal authored startup defaults at
`/usr/share/vim/defaults.vim`, and compiles that runtime directory into Vim.
Startup does not require the upstream plugin/runtime tree. File writes and
descriptor `fsync` commit to the engine's in-memory VFS. This does not add
host-disk persistence; download guest files to retain them after shell restart.
Nanosleep uses libc's resumable readiness wait. Unavailable `setsid` and
system-wide `sync` paths are disabled in the configure profile.

The initial profile targets a minimal `vim --version`/`vim --help`/basic
editor path. Larger feature levels (`normal`, `huge`), scripting languages,
runtime files, syntax highlighting, GUI and clipboard integration are future
work and will require additional patches, provider imports and installed
runtime data.

Vim is distributed under its own permissive license (see `submodules/vim/LICENSE`);
retain upstream `LICENSE`. Package tests live under `src/aux/vim/tests/*.wast`
and run through the installed Bash with `make -C src/aux test-vim`.

`tests/setup.sh` only supplies Vim inputs and produces the files inspected by
`installed.wast`; the assertions check executable access, actual version output,
and exact bytes saved by the Ex editor. Run the interactive native/browser/worker
regression with `python3 tests/guest-session-check.py --scenario vim --page
build/html-rt/bash.html`. It starts ordinary `vim FILE` from Bash, types text,
checks the visible line before Escape, saves with `:wq`, and runs the same
package WAST assertions. See `src/system-tests/vim` for manual browser steps.
