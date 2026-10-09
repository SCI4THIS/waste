---
name: add-aux-binary
description: Add or migrate a guest Wasm command into src/aux, with build, install and test Make targets, package-owned WAST tests and start.sh integration.
---

# Add an aux binary

Work from the repository root. Read `AGENTS.md` and the relevant parts of
`docs/architecture.md` and `docs/techniques.md`. Use the current implementation
as the reference: `src/aux/Makefile`, `src/aux/libc-imports.h`,
`src/aux/coreutils/coreutils.mk`, and `build_single_aux` in `start.sh`.

## Command directory

Create `src/aux/NAME/` containing the repository-owned inputs for the command:

- For an authored command, put its C sources and private headers here. Extract
  embedded C from an existing generator when migrating one.
- For an upstream command, keep upstream sources in their read-only submodule.
  Store repository patches, private configuration and source-selection files
  here. Stage, patch, configure and compile copies under `build/aux/NAME/`.
- For another Coreutils command, follow the per-command `sources.mk` pattern;
  add its object selection to the shared `src/aux/coreutils` rules.
- Add a short package README when build prerequisites, upstream ownership or
  supported behavior need explanation. Preserve dependency license notices
  and extend an existing corresponding-source bundle when applicable.

Put shared package support beside the commands that use it, as Coreutils does.
Keep private compatibility headers in the package. Public guest declarations
belong in `src/vfs/usr/include`; guest libc implementations belong in `src/aux/libc`.
Shared kernel semantics belong in `src/engine` and platform adapters in their
runtime directories.

## Three required Make targets

Expose all three targets through `src/aux/Makefile`, directly or via an included
package Makefile:

| Target | Required behavior |
| --- | --- |
| `NAME` | Build `build/aux/NAME/NAME.wasm`, without installing it. |
| `install-NAME` | Depend on `NAME`, check its library imports, then explicitly install the executable at `src/vfs/usr/bin/NAME`. |
| `test-NAME` | Run the package’s authored `.wast` tests through `/bin/wast --verbose` inside Bash. Return nonzero when any assertion fails. |

Shared-library packages use `build/aux/NAME/NAME.so.wasm` instead. Install
libraries with mode 0644 at their explicit `/lib` and `/usr/lib` paths, rather
than creating a command under `/usr/bin`; follow `src/aux/libc/libc.mk` or
`src/aux/libncurses/libncurses.mk`. libc additionally owns its private
interpreter support, shared guest CRT and shared-import tools.

Declare the command targets phony and make the build target depend on a real
artifact. Track relevant sources, private/public headers, patches, shared
helpers and providers so unchanged builds do no work. Add `NAME` to `all`.
Keep temporary files, objects, dependency files and generated configuration
under `BUILD_DIR`; keep the existing build-directory override convention.

For ordinary C commands, reuse the Clang/wasm-ld flags, public SDK include paths
and shared CRT used by `upload`, `download` and `ldd`. Use the guest headers
without host-header fallback. The binary needs the `_start` entrypoint; choose
the CRT variant matching its `main` signature. Reuse shared guest libc instead
of embedding another libc implementation. Attach library namespaces through
`libc-imports.h` or package-local typed declarations, and check imports against
the actual provider with `src/aux/libc/tools/shared_libc.py --check-imports`.

Preserve an upstream package's established ABI/linking profile when migrating
it. In particular, Coreutils links raw imports consistently before the shared
ABI helper rewrites matching libc imports. Mixing `env` and `libc` declarations
for one symbol across objects can fail linking. Keep Asyncify disabled.

Use the existing install recipe's provider checks and
`flock "$(BUILD_DIR)/.vfs-install.lock"` around `vfs.py install`. Extend its
grouped target list when reusing that recipe. A package with extra libraries
must check those providers too. Install executables with executable permissions
and keep library/notice installation explicit. HTML packaging consumes the
current VFS tree and must not build or reinstall the command.

## Integration outside the command directory

- **Installer:** Register `NAME` in the appropriate command list in
  `src/html-rt/tools/vfs.py`, which supplies the install CLI's component choices.
  Ordinary commands use `COMMANDS`; Coreutils additions use `COREUTILS`. The
  default destination is `/usr/bin/NAME`; update `command_path` only for an
  intentionally different guest location.
- **Wizard:** Add `NAME` to `AUX_UTILITIES` in `start.sh`. The aux menu and
  build-all loop use this array. Keep `build_single_aux` delegating to
  `make -C src/aux install-NAME`. Update its prerequisite case: an ordinary
  Clang package belongs with `libc|ldd|upload|download`, rather than falling into the
  Coreutils-specific `patch`/`wasm-opt` branch. Add package-specific prerequisites
  when needed. Adjust `aux_staged_path` and the displayed output path if the
  destination differs from the ordinary command convention.
- **Migration callers:** Replace calls to a retired generator with Make
  delegation, including any `src/html-rt/Makefile` compatibility targets.
  Remove obsolete scripts and profiles only after finding and updating their
  callers and preserving any shared functionality still in use.
- **Documentation and tests:** Update package instructions and relevant runtime
  scenarios. Keep all package-specific test sources and instructions under
  `src/aux/NAME/`; refresh installed snapshots explicitly when changing them.

## Package tests

Keep functional test logic entirely in authored `.wast` files under
`src/aux/NAME/tests/`. Store package-specific test instructions and supporting
inputs beside those files. Load the installed binary or library; do not generate
an embedded copy of libc or substitute host implementations. For a command,
invoke `/usr/bin/NAME` explicitly so a Bash builtin cannot mask it.

Use `src/aux/libc/tests` and `test-libc` as the shared-library pattern. Tests
import Bash’s `waste-runtime` process memory/table and the loaded `libc` provider.
Make `test-NAME` depend on `install-NAME` and `aux-test-runner`; include
`src/aux/test.mk` for that runner prerequisite.
The common `src/aux/run-wast-tests.sh` stages authored files into `/tmp` and feeds
commands to the installed Bash through `build/cli-rt/wasm`, using explicit
`--stage-file GUEST MODE HOST` arguments; it carries no assertion logic. `WASM_RUNNER` can
select `build/cli-rt/wasm-sanitize`. Save generated results under `build/`.

`/bin/wast --verbose FILE` reports `.` for a passing result and `F` for a failure,
then prints totals and the first failure. Exit status is zero only on success.
Reports use guest stdout, so shell redirection, pipelines and `download` work.
Publish snapshots with `vfs-tests-install`, then run the same authored tests in
`bash.html` at `/root/test/aux/NAME/FILE.wast`.

Tests of interactions between packages, the kernel, runtimes, or the compiler
SDK belong under `src/system-tests/`, with system test instructions and inputs
there. Do not make a package test own a copied SDK/sysroot or a generated libc
fixture profile. Preserve necessary compiler/declaration boundary checks as
system checks; runtime assertions belong in WAST.

## Verify the result

Build `NAME`, repeat to check caching, run `install-NAME` and `test-NAME`.
Confirm the installed artifact matches the build, with mode 0755 for commands
or 0644 for shared libraries. Rebuild `bash.html` and run the same WAST tests
through its Bash shell/worker; a GUI launch is not required for every change.
Use existing native sanitizer and system checks when shared semantics change.
Keep verification logs under `build/engine/logs/`. Run `bash -n start.sh`,
bytecode checks for changed Python helpers, and `git diff --check`; verify that
submodule contents stayed unchanged. Report the completed targets and checks.
