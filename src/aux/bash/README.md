# Source-built guest Bash

```sh
make -C src/aux -j4 bash
make -C src/aux install-bash
```

The read-only `submodules/bash` checkout supplies Bash's sources. `stage.sh`
copies them into `build/aux/bash/source` and applies `bash-waste.patch` there.
Configure, host generators and guest objects run under `build/aux/bash/configure`.
The executable is `build/aux/bash/bash.wasm`; installation explicitly publishes
it as `/usr/bin/bash`, with executable permissions and the upstream license.
`BUILD_DIR` may select another aux output directory beneath `build/`.

Prerequisites are Make, Clang, `wasm-ld`, a host C compiler (`HOST_CC`, default
`cc`), `patch`, and standard shell tools. Python only inspects/rewrites the
shared-libc ABI and installs files; it does not compile Bash. `start.sh` offers
Bash in the aux menu and delegates to `install-bash`.

Native applications start it directly with `build/cli-rt/wasm src/vfs/usr/bin/bash`;
Bash chooses interactive or script execution from its arguments and stdin.
The browser and private compatibility harness use `launch.wast`, installed by
`install-bash` at `/usr/share/waste/launch.wast`. The small authored launcher
calls `execve` with `/usr/bin/bash --norc -i` and an explicit environment,
including `PS1='# '`. Shared libc and stdio are initialized by the guest CRT.
`install-bash-launch` refreshes the launcher independently; HTML packaging
reads the installed tree without compiling or reinstalling Bash.

The shared process scheduler supports nested forks within the kernel's
process limit. `bash FILE` can run external commands, pipelines, substitutions
and `/bin/wast` commands, then return its script exit status to the calling
shell. Background jobs run concurrently: blocked sleeps let sibling processes
and the parent run, while CPU-bound jobs rotate on interpreter time slices.
Foreground input and the earliest background deadline both wake the session.

`cc.sh` uses the mounted guest SDK and package-private headers, without host
header fallback. Configure function answers are derived from the actual shared
libc provider; `config.site` selects supported kernel imports and overrides
features whose public declarations or layouts are unavailable. The package
uses the shared guest CRT and libc, PIC/imported memory and table, and explicit
engine `sigsetjmp`/`siglongjmp` checkpoints. Asyncify is not used.

The guest SDK declares SIGWINCH so Bash and Readline compile their resize
handlers. Guest-header changes trigger reconfiguration and recompilation;
explicitly run `install-bash` before packaging the updated executable. Shared
engine callbacks resolve caught signal handlers through the process table.
`make -C src/cli-rt terminal-check terminal-check-sanitize` verifies native
resize updates to LINES/COLUMNS and terminal restoration. The shared C probe
and `src/system-tests/cli-runtime/terminal/resize.json` also verify resize in
browser execution. The adjacent `interruptions.json` verifies Ctrl-C at the
prompt and during a foreground child. The native PTY gate also interrupts a
CPU-bound child and checks canonical input/echo and terminal restoration.
Default stop/job-control scheduling and arbitrary asynchronous caught handlers
remain unsupported.

The initial profile disables multibyte locale handling, network redirections,
conditional regular expressions and NLS. Private headers supply missing kernel
errno/terminal constants and checkpoint declarations; `lseek` uses shared
libc's wasm32 `off_t` signature. A fatal internal `abort()` traps the guest.
The small upstream patch fixes single-byte configuration errors and the escape
decoder's character pointer type in Bash 5.3.

After installation, run the same session against native, browser Wasm and the
packaged production worker:

```sh
make -C src/html-rt bash-html
python3 tests/guest-session-check.py --native build/cli-rt/private/guest-session-sanitize \
  --scenario aux-bash --page build/html-rt/bash.html
```

The scenario explicitly invokes `/usr/bin/bash` and checks version, arguments,
arrays/functions/arithmetic, exit and failure statuses, file I/O, pipelines,
substitution, script arguments and exit status, nested WAST commands,
background subshell sleeps, background wait and delayed interactive input. It also checks
`ldd` reports the shared libc dependency.

Bash is GPL-licensed; retain upstream `COPYING`. Before distributing a binary,
run `make -C src/aux bash-source-package` and provide the generated
`build/aux/bash/bash-corresponding-source.tar.gz` alongside it. That bundle
includes the pristine upstream source, repository patches/build inputs, headers, configuration,
source revision and binary hash. Unpack it, place `bash-source` at
`waste/submodules/bash`, and build from `waste` with the commands above using
the listed host tools. The other upstream checkouts are needed only when
building their packages; building Bash uses the included guest headers and an
already built shared-libc provider, or rebuild libc with `make -C src/aux install-libc`.
