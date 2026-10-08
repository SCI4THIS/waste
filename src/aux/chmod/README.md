# chmod

Build with `make -C src/aux chmod`; output is `build/aux/chmod/chmod.wasm`.
Install `/usr/bin/chmod` with `make -C src/aux install-chmod`.

`sources.mk` selects the upstream GNU Coreutils implementation. Shared staging,
configuration, private headers and linking rules live in
[Coreutils support](../coreutils/README.md). The upstream checkout stays read-only.

The command uses shared guest libc and the engine-owned VFS; it changes guest
permission metadata rather than host permissions. Numeric and symbolic modes,
`umask`, `--reference`, verbose output and recursive traversal are exercised in
`tests/guest-session-chmod.json` across native and packaged browser workers.
The recursive walker uses directory-relative `openat`, `fstatat`, `fchmodat`
and guest directory streams. Symlink permission updates with
`AT_SYMLINK_NOFOLLOW` return `EOPNOTSUPP`. Full POSIX symlink traversal is
outside the verified command subset.
Permission changes last for the running guest session; they do not install
changes into the source VFS or rewrite `bash.html`.
