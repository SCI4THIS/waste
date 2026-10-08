# Mounted guest C headers

The SDK describes a deliberately partial wasm32 guest ABI. Public
declarations, provider signatures, compiler include closure and selected ABI
probes are checked. This is not full ISO C/POSIX support or an implemented
in-guest compiler; a provider can still be a documented compatibility stub.

## Ownership and installation

Authored guest headers live in `src/vfs/usr/include`. Private libc helpers remain
in `src/libc/include/helper.h`; engine and native headers remain in their
own include roots. `waste/abi/posix.h` contains fixed-width guest wire layouts,
not Linux host structures. Public `FILE` is opaque.

Selected Clang 22.1.8 support headers are installed snapshots under
`src/vfs/usr/lib/waste/cc/include`, with LLVM's license under
`usr/share/licenses/clang`. They supply C11 fundamental headers plus the
checked-arithmetic/count-of extensions used by the pinned Coreutils source.
Their dependencies are selected explicitly, not mounted from a host directory.

Configured ncurses headers are snapshots from `build/ncurses/build/include`.
The adapted upstream `curses.h` lives under `waste/ncurses`; the public wrapper
preserves C11 `bool` while parsing ncurses declarations with the DSO's four-byte
`unsigned` Boolean ABI. Rogue's named legacy build profile intentionally uses
`NCURSES_BOOL` in its own structures. Missing optional/debug/SP functions receive
unavailable attributes without changing the DSO ABI. The manifest records both
upstream and adapted hashes. `ncurses_cfg.h` stays private.

`usr/share/waste/sdk.json` records informational target assumptions, header
origins, compiler version and dependency configuration at installation time.
Recorded hashes describe those inputs; they are not freshness requirements.
The current files in `src/vfs` are authoritative. Edit or add a header/binary
there and native mounting uses it on the next run; rebuild `bash.html` to include
it in the browser package. No metadata refresh or import approval is required.

Explicit provider checks discover current headers and compare actual Wasm
signatures against current libraries and the adapter signatures in
`sdk-api-policy.json`. Their API report is generated under `build/`, not stored
in the mounted SDK as an approval ledger. Source changes do not require a new
source hash. The historical `browser-env` provider label and
`browser_adapter_review` policy key remain for compatibility; they do not
imply a separate native guest ABI or establish full semantic conformance.

Check current public headers and providers with:

```sh
make -C src/html-rt guest-sdk-check
```

Use `make -C src/html-rt guest-sdk-install` only when deliberately refreshing
selected compiler and configured ncurses headers. It uses the available Clang,
generated ncurses headers and compiled guest libc; the target builds libc first.
SDK preparation runs the provider/signature check before atomic publication.
A rejected candidate leaves existing files untouched; edited or additional
files are not rejected for differing from recorded hashes. To refresh a library
and its matching headers together:

```sh
python3 src/html-rt/tools/build-ncurses.py --repo-root . --install
```

The SDK installer preserves files outside its explicit copy list. Filesystem
metadata comes from current files; inode IDs are derived from sorted paths.
HTML packaging neither installs headers nor checks SDK provenance freshness.

## Build profiles and ABI

`build-waste-sysroot.py` copies only the mounted public/compiler tree for its
default wrapper. It uses `-ffreestanding -nostdinc -nostdlib`; no host C include
directory or implicit Clang resource path is permitted. Compiler predefined
types/limits, builtin `va_list`, attributes, and `push_macro`/`pop_macro` remain
explicit requirements for a future compatible parser/compiler.

`waste-coreutils-clang` adds the separate gnulib/SELinux compatibility profile
from `src/html-rt/profiles/coreutils/include`, including pinned generated
gnulib templates. Those package-only declarations are not mounted public APIs.
The normal `waste-wasm-clang` wrapper does not force-include them. Implementation
builds may still use their own generated/private configuration headers.
Gnulib locale-object layouts and UTF-32 helpers live only in that profile;
default `locale_t` is opaque. The named Coreutils profile retains inherited
declarations for its own implementations. Rogue opts into its historical abort
binding explicitly; neither exception makes those APIs default capabilities.

The current wasm32 profile has four-byte pointers/int/long, eight-byte
`long long` and `time_t`, and legacy four-byte `off_t`. `struct stat` is 128
bytes, `dirent` 280, termios 60, timeval/timespec 16. The engine adapter's
`mmap` offset is explicitly `int64_t`, not legacy `off_t`; `MAP_FIXED` is not
supported. These are guest layouts, not native Linux declarations.
Direct `lseek` is unavailable in the default SDK: the browser overrides that
symbol with Bash's i64 ABI. `__fpurge` is also unavailable because its browser
override returns i32 rather than the declared void. Use the supported libc
file-position/purge interfaces through the documented link profile instead.
`abort` is unavailable because its browser binding returns normally. Default
`assert` traps without a diagnostic or SIGABRT; `NDEBUG` omits evaluation.

Other unavailable APIs fail on calls or address-taking through Clang's
`unavailable` attribute. Available does not mean fully implemented: examples
include no-op `atexit` and sleep, environment mutation and
single-threaded mutex stubs. Network wrappers return unsupported errors; there
is no host socket or filesystem access. Consult the mounted capability notes.

Signal-set helpers now operate on the public 16-byte, four-word `sigset_t`.
`sigprocmask` implements block/unblock/setmask and old-mask queries;
`sigpending` reports blocked queued signals without consuming them. SIGKILL and
SIGSTOP cannot be blocked by process-mask updates. The compiled C probe checks
this layout and an adjacent canary. Handler delivery remains the engine's
existing pselect/function-index ABI; compiled C handler pointers, sigaction
flags, general delivery on unblock and SELECT handler invocation remain
unverified or unsupported. The capability ledger keeps these limits explicit.

Guest libc and existing CRT/runtime link builders still supply execution.
Mounting these headers does not install a compiler, general-purpose link driver,
or new POSIX capabilities.

## Verification

`tests/guest-sdk-check.py` checks standalone headers, include orders, hermetic
dependencies, ABI sizes/offsets, real Wasm variadic execution and negative
host/private-header cases. Native directory-mount sanitizer tests verify all
mounted bytes/metadata; `tests/guest-sdk-mounted.wast` checks public presence
and private exclusion in both runtimes. `tests/c-engine-vfs-browser.cjs` also
compiles with the installed-only sysroot and executes variadic, stat-buffer
canary, signal-set/mask canary and assertion-trap probes through the compiled C engine. The SDK check
rejects every unavailable declaration on address-taking and tests missing
providers, incompatible signatures and malformed signature inputs.

```sh
python3 src/html-rt/tools/audit-guest-providers.py
```

The command inspects declarations and compiles an address-reference probe to
compare actual Wasm signatures against installed libc.so.wasm/ncurses exports. Ncurses's
imports must also match their providers. Browser-only signatures and binding
precedence are explicit in `sdk-api-policy.json`. This is
signature/capability accounting, not semantic conformance proof.

There is no strict metadata freshness gate. `guest-sdk-check` checks current
header compilation, layouts and provider compatibility without comparing a
stored report or source hashes. Rebuild browser packages after file changes.
The three standard stream globals require CRT startup; other data addresses require normal link
relocations, not wasm-ld's unresolved-data address-zero fallback.
