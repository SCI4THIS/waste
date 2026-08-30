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
```

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
./start.sh --generate-html
./start.sh --patch-status
./start.sh --apply-i31
./start.sh --revert-i31
./start.sh --update
```

The **Generate embedded browser test dashboard** entry creates the self-contained
`build/ocaml-wasm/browser-tests.html`. It embeds the compiled OCaml Wasm runtime
and every `.wast` file under `submodules/wasm-spec/test`, groups tests by their
source directory, and provides Run, Test module, and Test all controls with live
pass/fail indicators. Each result shows its elapsed time, and the header shows
the cumulative time for completed tests. A Download results button exports a
JSON report with the summary, timings, exit codes, captured output, and errors.
Test all records and displays its local start and finish times; both timestamps
are included in the downloaded report. Test all runs each displayed directory
group in order. Legacy exception groups are highlighted as unsupported and are
excluded from Test all, while remaining available for explicit runs. All custom
annotation handlers are enabled for browser tests. Every test gets a fresh Web
Worker so failures remain isolated.

The **Safe pull/rebase and submodule update** menu entry temporarily removes the
managed Wasm32 patch, runs `git pull --rebase --autostash`, updates initialized
submodules, and reapplies the patch only when the updated spec does not already
contain it. It refuses to proceed over unmanaged submodule changes. Its latest
transcript is stored in `update.log`.

The generated loader and Wasm assets are the compiled command-line reference
interpreter. A browser API for supplying WAT, presenting output, and controlling
execution still needs to be added; compilation alone does not provide pause and
resume semantics.
