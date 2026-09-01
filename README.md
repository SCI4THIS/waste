# WASTE

This repository is exploring a browser-hosted WebAssembly execution environment.
The official WebAssembly specification and its OCaml reference interpreter live
in `submodules/wasm-spec`.

## Compile the OCaml interpreter to WebAssembly

Run the dependency-first terminal wizard:

```sh
./start.sh
```

On Omarchy/Arch, the system prerequisites are:

```sh
sudo pacman -S --needed git opam bubblewrap base-devel binaryen libnewt
```

The wizard creates an isolated opam switch named `waste-wasm` using OCaml 5.3.0
and installs Dune, Menhir, `wasm_of_ocaml-compiler`, `js_of_ocaml`, and
`js_of_ocaml-ppx`. Binaryen must provide `wasm-opt` version 119 or newer.

The compile command builds from a temporary overlay, so it does not edit or
dirty the spec submodule. Its output is written to:

```text
build/ocaml-wasm/dist/wasm_cli.bc.wasm.js
build/ocaml-wasm/dist/wasm_cli.bc.wasm.assets/
build/ocaml-wasm/dist-threaded/wasm_cli.bc.wasm.js
build/ocaml-wasm/dist-threaded/wasm_cli.bc.wasm.assets/
```

The `dist` interpreter preserves the original direct build. The
`dist-threaded` interpreter uses the CPS effect backend required to suspend and
resume evaluator continuations. The browser dashboard embeds the CPS artifact
and controls how many logical test tasks may be runnable at once.

The most recent build transcript is written to `build.log` in the repository
root, including dependency failures and the Dune compiler output.

The main menu also manages `submodules/wasm-spec-i31-int32.patch`. This optional
Wasm32 compatibility patch removes assumptions that an OCaml `int` is wider
than 32 bits. It preserves unsigned i31 and decoded u32 values, prevents
alignment-shift overflow, and rejects unrepresentable local counts before they
trigger an enormous allocation. The menu reports the patch as `available`,
`applied`, or `conflict` and provides confirmed apply and revert operations.
The `--apply-i31` and `--revert-i31` names are retained for compatibility with
the original i31-only version of the patch.
Apply and revert affect source used by the next compilation; they do not replace
already-generated files in `build/ocaml-wasm/dist` until you compile again.

Useful non-interactive commands are:

```sh
./start.sh --check
./start.sh --install-deps
./start.sh --compile
./start.sh --build-libc
./start.sh --generate-html
./start.sh --generate-bash-html
./start.sh --patch-status
./start.sh --apply-i31
./start.sh --revert-i31
./start.sh --update
```

The **Generate embedded browser test dashboard** entry creates the self-contained
`build/ocaml-wasm/browser-tests.html`. It embeds the compiled OCaml Wasm runtime
and every source `.wast` file under `submodules/wasm-spec/test`, groups tests by their
source directory, and provides Run, Test module, and Test all controls with live
pass/fail indicators. Each result shows its elapsed time, and the header shows
the cumulative time for completed tests. A Download results button exports a
JSON report with the summary, timings, exit codes, captured output, and errors.
Test all records and displays its local start and finish times; both timestamps
are included in the downloaded report. Test all runs each displayed directory
group in order. Legacy exception groups are highlighted as unsupported and are
excluded from Test all, while remaining available for explicit runs. All custom
annotation handlers are enabled for browser tests. Generated compiler artifacts
under test `_output` directories are excluded from discovery.

The **Generate self-contained WASTE Bash page** entry creates
`build/ocaml-wasm/bash.html`. It embeds the CPS interpreter, the relinked Bash
and waste-libc binaries, and their shared runtime namespace in one file that can
be opened directly with `file://`. It starts one persistent
`bash --norc --noediting -i` process and feeds submitted lines into the OCaml
virtual terminal, so shell state such as the current directory survives between
commands. The page provides instruction-quantum control, terminal output,
pause/resume, signal, restart, and worker-stop controls. Set
`WASTE_BASH_INSTRUCTION_QUANTUM` to change the generator's default quantum.
Startup is intentionally visible and can be slow: the browser is decoding,
validating, and interpreting Bash inside the OCaml interpreter; restarting the
worker repeats that work. Enable **profile startup phases** before restarting to
print cumulative millisecond timestamps for parsing, decoding, validation,
import resolution, detailed evaluator initialization, registration, and
invocation directly in the terminal. Browser markers additionally report the
first-prompt duration from both `Date.now()` and `performance.now()`, plus their
difference and the browser's local timestamp.

The patched evaluator executes `memory.fill`, `memory.copy`, `memory.init`,
`table.fill`, `table.copy`, and `table.init` as bounds-checked runtime loops.
Each remains one scheduler-visible guest instruction; overlapping copies retain
memmove behavior without allocating recursive interpreter instruction lists.

## Guest libc

`libc/waste-libc.wat` and `libc/waste-libc-helpers.c` form the guest-side libc
used by Bash. The merged module owns and exports application linear memory. Its
`sbrk` implementation provides a dlmalloc-compatible MORECORE boundary and
expands through a series of `memory.grow 1` calls. The current boundary-tag
allocator exports `malloc`, `calloc`, `realloc`, `free`, `sbrk`, and
`__errno_location`.

The helper layer adds memory-backed `FILE` streams, wasm32 variadic formatting,
UTF-8 multibyte and wide-character conversion, C.UTF-8 locale behavior, and
configurable in-memory identity, passwd, group, and service records. It also
provides Bash's string, conversion, compiler-runtime, sorting, matching, basic
regex, resource-limit, UTC time-formatting, terminal, and diagnostic helpers.
Together with the OCaml host, its exports cover every named import currently in
`src/bash.wat`. `tools/build-bash-runtime.py` relinks Bash and libc to a neutral
`waste-runtime` owner for their shared memory and function table, then registers
libc as an overlay on the OCaml host's `env` namespace.

Operations that require evaluator state deliberately return `ENOSYS` here,
including directory traversal, `execve`, descriptor readiness, dynamic loading,
and direct socket creation. They must cross the existing OCaml process/VFS layer
or the optional WebSocket broker rather than create a second, inconsistent OS
model inside libc. The deterministic entropy generator is for repeatable tests;
a production adapter must seed it from browser cryptography.

Build `build/waste-libc/waste-libc.wasm` and regenerate its WAST fixture with:

```sh
./start.sh --build-libc
```

Libc tests appear in the dashboard's `libc-test` group and can also run
against both generated interpreters:

```sh
node tests/libc-test/libc-runtime.cjs
node tests/libc-test/libc-runtime.cjs threaded
node tests/libc-test/allocator-native.cjs
```

The dashboard offers thread-count choices of **1**, **#tests** (derived from
all supported embedded suites), or a custom positive integer. Test all passes
every supported script to one OCaml Wasm interpreter instance. The interpreter
starts at most the selected number of isolated logical tasks and backfills from
the ordered queue as tasks finish. Each task retains its own continuation and
runner state. The instruction-quantum input controls how many evaluator steps
a runnable task receives before it yields. The default is 10,000 and can also be set during
non-interactive generation with `WASTE_INSTRUCTION_QUANTUM`.

This is deterministic cooperative threading on one browser execution thread,
not multicore WebAssembly shared-memory threading. Logical tests share the
OCaml runtime heap, while their WebAssembly module instances, memories, script
registries, stacks, and program counters remain isolated. Keeping module memory
isolated is required for the specification tests to remain independent.
Because `core/custom.wast` intentionally triggers the accepted `-c custom`
handler failure, and CPS turns that recursive handler path into a non-terminating
loop rather than a promptly catchable host stack overflow, the cooperative runner
records it as the known failure after all runnable logical threads finish. Its
source is included in the batch, but the recursive handler is not entered.

The **Safe pull/rebase and submodule update** menu entry temporarily removes the
managed Wasm32 patch, runs `git pull --rebase --autostash`, updates initialized
submodules, and reapplies the patch only when the updated spec does not already
contain it. It refuses to proceed over unmanaged submodule changes. Its latest
transcript is stored in `update.log`.

The generated dashboard remains a single, self-contained HTML file that can be
opened directly with `file://`. **Pause**, **Resume**, and **Send signal** use
worker messages: the CPS interpreter returns to the worker event loop after
each instruction quantum, then continues with the same evaluator continuation.
Signals enter a versioned command ring which is checked once per guest
instruction. No server, cross-origin isolation, `SharedArrayBuffer`, or browser
flag is required. The signal and non-local-jump ABI, verification commands, and
remaining POSIX boundary are documented in
[docs/posix-runtime.md](docs/posix-runtime.md).

The POSIX kernel integration probe runs against both generated runtimes:

```sh
node tests/diy-posix-test/posix-kernel-runtime.cjs
node tests/diy-posix-test/posix-kernel-runtime.cjs threaded
```

The `diy-posix-test` suite validates WASTE's interpreter ABI and is embedded in
the browser dashboard alongside the WebAssembly spec tests. It is not an
official POSIX conformance suite. Test sourcing and the browser/emulation/
WebSocket-broker policy are documented in
[docs/posix-runtime.md](docs/posix-runtime.md).
