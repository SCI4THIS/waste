# Interactive Vim session

`session.json` drives the installed Vim from Bash, records its version, types
`VIM_PACKAGE_SAVED`, checks the displayed text and cursor, saves with `:wq`, and
invokes the package WAST assertions against the resulting file. The shared
harness checks the native runtime, browser exports and the packaged worker.
All file-content assertions live in `src/aux/vim/tests/installed.wast`.

`scroll-session.json` opens a numbered 60-line file and moves with `j` beyond
the bottom of the editor, then with `k` above the top. It checks the top and
bottom visible rows before each next key; a later full redraw cannot hide a
missed scroll. It also checks all text rows for stray underline attributes,
including trailing blank cells. Select it with `--scenario vim-scroll` in the
command below, or pass both `--scenario vim --scenario vim-scroll` to check
editing and scrolling.

```sh
make -C src/aux install-libc install-vim
make -C src/cli-rt guest-session-sanitize
make -C src/html-rt vfs-tests-install bash-html
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  python3 tests/guest-session-check.py \
  --native build/cli-rt/private/guest-session-sanitize \
  --scenario vim --page build/html-rt/bash.html \
  > build/engine/vim-session-results.txt \
  2> build/engine/vim-session-check.log
```

For manual Firefox acceptance, reopen the rebuilt `build/html-rt/bash.html`:

```sh
vim --version > /tmp/vim-package-version.txt
vim /tmp/vim-package.txt
```

Press `i`, type `VIM_PACKAGE_SAVED`, press Escape, type `:wq`, and press Enter.
Then run:

```sh
wast --verbose /root/test/aux/vim/installed.wast > /tmp/vim-results.txt
download /tmp/vim-results.txt
download /tmp/vim-package.txt
```

Expect four passing assertions and a saved file containing exactly the typed
text followed by a newline. The package uses a small authored defaults file;
upstream plugins, help/syntax runtime data and larger feature profiles remain
outside this minimal installation.

To check scrolling manually, open a long file, such as
`vim /root/test/aux/libc/accounts.wast`. Press `j` repeatedly past the bottom
of the text area, then `k` back past the top. The text rows should move together
while Vim's bottom status/command row stays in place. Exit with `:q!`.
