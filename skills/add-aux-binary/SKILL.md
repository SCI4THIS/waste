---
name: add-aux-binary
description: Add or migrate a guest Wasm command into src/aux, with build and install Make targets, start.sh integration, and native/browser verification.
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
belong in `src/vfs/usr/include`; guest libc implementations belong in `src/libc`.
Shared kernel semantics belong in `src/engine` and platform adapters in their
runtime directories.

## Two required Make targets

Expose both targets through `src/aux/Makefile`, directly or via an included
package Makefile:

| Target | Required behavior |
| --- | --- |
| `NAME` | Build `build/aux/NAME/NAME.wasm`, without installing it. |
| `install-NAME` | Depend on `NAME`, check its library imports, then explicitly install the executable at `src/vfs/usr/bin/NAME`. |

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
the actual provider with `shared_libc.py --check-imports`.

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
  Clang command belongs with `ldd|upload|download`, rather than falling into the
  Coreutils-specific `patch`/`wasm-opt` branch. Add package-specific prerequisites
  when needed. Adjust `aux_staged_path` and the displayed output path if the
  destination differs from the ordinary command convention.
- **Migration callers:** Replace calls to a retired generator with Make
  delegation, including any `src/html-rt/Makefile` compatibility targets.
  Remove obsolete scripts and profiles only after finding and updating their
  callers and preserving any shared functionality still in use.
- **Documentation and tests:** Update package instructions and relevant runtime
  scenarios. Keep authored fixtures under `tests`; refresh installed test
  snapshots explicitly when changing a mounted fixture.

## Verify the result

Build `NAME`, build it again to check caching, and run `install-NAME`. Confirm
the installed file matches the built artifact and has executable permissions.
Run representative success and failure cases in native Bash and the packaged
browser worker/page, including arguments, output and exit status. Invoke
`/usr/bin/NAME` explicitly so a Bash builtin cannot mask the binary. Use
`ldd /usr/bin/NAME` when checking shared-library dependencies.

Reuse relevant guest-session scenarios and native sanitizer checks. Rebuild
`bash.html` for browser checks; use existing worker checks or manual offline
page testing, without requiring a GUI browser launch for every change. Save
verification logs under `build/engine/logs/`. Run `bash -n start.sh`, bytecode
checks for changed Python helpers, and `git diff --check`. Verify submodule
contents were not changed by staging/building. Report which targets, callers,
installation paths and runtime checks were completed.
