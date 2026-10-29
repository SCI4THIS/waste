# Auxiliary command notices

Every executable built by `src/aux` supports these WASTE extensions:

```sh
/usr/bin/COMMAND --license
/usr/bin/COMMAND --authors
```

Pass exactly one option, with no operand or option value. Output is the complete
installed notice, unchanged, on stdout. The command does not read stdin, open
user inputs, enter an interactive application, or request host clipboard/file
access. Missing notices and read/write failures produce a diagnostic and failure
status. Successful output returns zero, except `false`, which retains its GNU
informational-option failure status. `echo` treats these as literal text when
`POSIXLY_CORRECT` is set, like its existing `--help` and `--version` options.
Use explicit executable paths to bypass Bash builtins. Upstream help/version
output remains upstream-owned; these common extensions are documented here.

M4 additionally retains its package-owned `--copying` alias and existing
`--license` option parsing, including recognition with other M4 arguments.

The application CRT opts into `options.h` through build-time definitions in
`notices.mk`. The selected package is fixed when building, independent of
`argv[0]`. Shared libraries and fixture CRTs do not opt in. Copying uses bounded
128-byte reads, handles partial writes and EINTR, and closes the input file on
failure. These paths use ordinary guest libc/kernel APIs in both runtimes.
The compiler SDK installs a separately compiled plain CRT, so user programs
continue to own their `--license` and `--authors` arguments.

## Installed documents

`install-<command>` publishes notices under the normal VFS installation lock.
`install.py` reads staged upstream sources and leaves unchanged files alone.
The installed `src/vfs` documents are included in the offline page; standalone
Awk, M4, Dot and Bash packages explicitly include AUTHORS as well.

| Command/package | License under `/usr/share/licenses` | AUTHORS source |
| --- | --- | --- |
| Coreutils commands | `coreutils/COPYING` | One `coreutils/COMMAND/AUTHORS` per command, extracted from its upstream AUTHORS entry. `coreutils/AUTHORS` also collects the shipped commands. |
| `awk` | `awk/COPYING` | Gawk AUTHORS, complete |
| `bash` | `bash/COPYING` | Bash AUTHORS, complete |
| `m4` | `m4/COPYING` | Pinned M4 AUTHORS, complete |
| `dot` | `dot/COPYING` | Graphviz AUTHORS, complete |
| `openssl` | `openssl/LICENSE.txt` | OpenSSL AUTHORS.md, complete, installed as AUTHORS |
| `vim` | `vim/LICENSE` | Credits section of runtime/doc/intro.txt |
| `rogue` | `rogue/LICENSE.TXT` | Original and component attribution sections of LICENSE.TXT |
| `clang`, `wasm-ld`, `lld-link` | `llvm/LICENSE.TXT` | LLVM llvm/CREDITS.TXT, Clang Maintainers.md and LLD Maintainers.md; labeled extracts preserve the partial-list qualifier |
| `clipboard`, `upload`, `download` | `COMMAND/LICENSE` | Package AUTHORS: Andrew Kistler with AI assistance from Claude and Codex |
| `ldd` | `ldd/LICENSE` | Package AUTHORS identifies the WASTE project and points to LICENSE for copyright attribution |

The license output is the designated upstream document, not a replacement for
other bundled dependency notices. Extraction validates expected section markers
and command entries so an upstream layout change fails installation visibly.

## Verification

After building and installing the commands, run:

```sh
make -C src/aux test-notices
make -C src/aux test-notices WASM_RUNNER=/absolute/path/to/build/cli-rt/wasm-sanitize
make -C src/html-rt vfs-tests-install bash-html
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html src/aux/notices/browser.json
```

Canonical assertions and observation setup live in
`src/vfs/root/test/system/notices/`. They compare both flags for all 27 commands
against their installed documents, including complete EOF and exit status,
stderr, pipelines, ignored stdin, literal operands, POSIX echo, and write errors.
They also compile and execute a guest program to check that SDK startup does not
intercept either option.
