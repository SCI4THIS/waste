# Repository Guidelines

## Project Structure & Module Organization

The original C interpreter and validator live in `src/`; notable inputs include
`src/bash.wat`, while generated parser/reference material is under `src/gen/`.
The official OCaml reference interpreter is the Git submodule at
`submodules/wasm-spec`. Repository-owned OCaml changes must be represented in
`submodules/wasm-spec-i31-int32.patch`, not committed inside the submodule.
Browser-dashboard generation is implemented by `tools/generate-browser-tests.py`,
and architecture notes belong in `docs/`. Integration probes and WAST fixtures
live in `tests/`. Generated OCaml/Wasm artifacts are written beneath
`build/ocaml-wasm/` and should not be treated as source.

## Build, Test, and Development Commands

- `./start.sh`: open the dependency, build, patch, update, and dashboard wizard.
- `./start.sh --check`: report system packages, the opam switch, and submodule state.
- `./start.sh --compile`: build direct and CPS OCaml interpreters as WebAssembly.
- `./start.sh --generate-html`: create the self-contained browser test dashboard.
- `./build.sh`: build the original C implementation.
- `node tests/posix-kernel-runtime.cjs [threaded]`: exercise process, VFS, pipe,
  terminal, timer, and fork behavior.
- `node tests/posix-control-runtime.cjs [threaded]`: verify pause/resume and signal
  delivery.

Before submitting shell or Python changes, run `bash -n start.sh`,
`python3 -m py_compile tools/generate-browser-tests.py`, and `git diff --check`.

## Coding Style & Naming Conventions

Match surrounding style: two-space indentation for shell, Python, JavaScript,
and OCaml; four spaces are acceptable where existing Python code uses them.
Use `snake_case` for shell/Python/OCaml functions, descriptive uppercase shell
constants, and kebab-case filenames for generated dashboards and logs. Quote
shell expansions and keep Bash functions focused. Avoid modifying generated
files or upstream submodule history directly.

## Testing Guidelines

Name OCaml-interpreter fixtures `tests/*.wast` and their Node harnesses
`tests/*-runtime.cjs`. Test both direct and `threaded` artifacts when changing
scheduler, evaluator, signal, or POSIX behavior. Browser changes must continue
to work as a single offline `file://` document; do not introduce a server,
external assets, or cross-origin-isolation requirements.

## Commit & Pull Request Guidelines

Recent commits use short, imperative subjects such as `Fix tests` and `Add
threading`. Keep each commit scoped and explain compatibility or performance
tradeoffs in its body. Pull requests should summarize behavior, list commands
run, identify submodule-patch changes, and include screenshots for dashboard UI
changes. Link relevant issues and attach result JSON only when it helps diagnose
a regression.
