# Coreutils commands

The command directories select upstream objects in `sources.mk`; they
share the staging, compiler profile and rules in this directory. Upstream C
sources stay in the read-only `submodules/coreutils` checkout rather than being
duplicated in each command directory.

```sh
make -C src/aux cat
make -C src/aux install-cat
make -C src/aux -j4 coreutils
make -C src/aux install-coreutils
make -C src/aux coreutils-source-package
```

Builds stage and patch source under `build/aux/coreutils/source`, bootstrap there,
and configure/compile under `build/aux/coreutils/configure`. Shared objects are
built once. A separate trimmed archive preserves the upstream archive while
removing implementations supplied by the guest ABI. Each final binary goes to
`build/aux/NAME/NAME.wasm`. Only explicit `install-NAME` targets publish binaries
to `src/vfs/usr/bin/NAME`; `start.sh` delegates to those targets.

`cc.sh` uses Clang and the mounted public SDK. The private headers in `include/`
and staged Gnulib templates are only for this package. Configure assumptions
are documented in `config.site`. The inherited limited C23/SELinux compatibility
surface remains unchanged by this build migration.

Bootstrap requires the Autotools/Gnulib prerequisites listed by
`bootstrap.sh --check`. Normal Make builds use `--no-install`. Binaryen's
`wasm-opt` strips debug information. The generic Python `shared_libc.py` helper
rewrites exact matching libc imports and checks provider signatures; the generic
VFS helper publishes explicit installs. Coreutils-specific Python builders and
approval reports are retired.

The retained `ls` regression coverage includes terminal columns, redirected
one-name-per-line output, `-1`, `-A`, `-l`, multiple directories, symlink
targets and missing-path status 2. Collation is bytewise. Color,
locale-specific quoting and account-name lookup are outside that verified
subset; guest timestamps come from mounted VFS metadata.

Release corresponding source is generated with standard tar/gzip under
`build/aux/coreutils`, including the actual staged source and repository build
inputs. Publish that archive with releases; see
[the source distribution instructions](../../../docs/coreutils-source-distribution.md).
