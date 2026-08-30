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
compatibility patch changes the reference interpreter's i31 payload from
`int` to `int32`, preventing the maximum unsigned i31 value from being truncated
to `-1` by wasm_of_ocaml. The menu reports the patch as `available`, `applied`,
or `conflict` and provides confirmed apply and revert operations.
Apply and revert affect source used by the next compilation; they do not replace
already-generated files in `build/ocaml-wasm/dist` until you compile again.

Useful non-interactive commands are:

```sh
./start.sh --check
./start.sh --install-deps
./start.sh --compile
./start.sh --patch-status
./start.sh --apply-i31
./start.sh --revert-i31
```

The generated loader and Wasm assets are the compiled command-line reference
interpreter. A browser API for supplying WAT, presenting output, and controlling
execution still needs to be added; compilation alone does not provide pause and
resume semantics.
