# OCaml reference interpreter build

The OCaml reference interpreter in `submodules/wasm-spec/interpreter` was used
as a language reference while implementing the WAT/WAST portions of the C
engine. It is not wired into any WASTE runtime, test corpus, or build output.
This document records how to reproduce the upstream reference behavior.

## Prerequisites

The upstream interpreter requires:

- `opam`
- `dune`
- `menhir`
- OCaml &ge; 4.12 (per the upstream README)

Install them through your system package manager and `opam` as usual.

## Build and test

From the repository root:

```sh
./start.sh --ocaml-reference
```

That subcommand invokes `make -C submodules ocaml-test`, which:

1. Copies `submodules/wasm-spec/interpreter` to `build/ocaml-interpreter/`
   (via the `ocaml` target).
2. Runs `make` in the staged copy.
3. Runs `make test`, which executes the upstream spec test suite.
4. Logs output to `build/engine/logs/ocaml-reference.log`.

The individual targets are also available directly:

```sh
make -C submodules ocaml         # stage and build without the test suite
make -C submodules ocaml-test    # stage, build, and run the spec tests
make -C submodules ocaml-clean   # remove the staged copy
```

The checked-out submodule is never mutated; the staged copy under `build/` is
disposable.
