# Submodule source and build policy

Checked-out submodules are read-only source dependencies. Do not edit their
tracked or untracked contents, apply repository patches inside them, or run
generators, bootstrap/configure steps, or builds that write into them. This
applies to temporary changes too: restoring a submodule after a build does not
make an in-place mutation compatible with the policy.

When a submodule is an input to a build, copy or stage the required source into
the corresponding directory under `build/` first. Apply repository-owned
patches to that staged copy and keep compiler products, generated sources,
logs, test outputs, and intermediate files under `build/`. Source-controlled
patch files and scripts belong in the repository, outside the submodule.

For upstream test runners that generate output, pass an output directory under
`build/`. For example:

```sh
python3 submodules/wasm-spec/test/core/run.py \
  --wasm build/cli-rt/waste-wast-ocaml \
  --out build/ocaml/spec-roundtrip
```

## Existing helpers that need migration

Some existing entry points predate this policy and still mutate a submodule
temporarily:

- `make -C submodules ...` applies `wasm-spec-i31-int32.patch` to
  `submodules/wasm-spec` before building, then reverses it.
- `./start.sh --apply-i31`, `./start.sh --compile`, and the OCaml build wizard
  apply the same patch to that checkout.
- `./start.sh --update` may update submodule checkouts and restore the managed
  patch in place; do not use it as an ordinary build or test command.
- `src/html-rt/tools/stage-coreutils.py` temporarily applies
  `submodules/coreutils-waste.patch` before copying source to `build/`.

These workflows are not compliant and should not be run under the read-only
policy until refactored to patch staged copies under `build/`. The Coreutils
bootstrap helper already emits generated configure/build files under
`build/coreutils/`; its source staging step must also avoid modifying the
submodule while applying the managed patch.
