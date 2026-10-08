# GNU Coreutils corresponding source

The WASTE browser package distributes selected GNU Coreutils programs under
GPLv3-or-later. Its corresponding-source artifact is generated as
`build/aux/coreutils/coreutils-corresponding-source.tar.gz` by:

```sh
make -C src/aux coreutils-source-package
```

The archive contains the exact patched and bootstrapped Coreutils source tree,
the repository-managed patch, configure answers and logs, WASTE engine and
guest-runtime source, interface headers, build scripts, and license notices.
`build/aux/coreutils/coreutils-source-package.json` records the archive SHA-256,
source commit, covered utility names, and these instructions. `SOURCE-BUNDLE.json` inside
the archive records the SHA-256 of each built utility. This is an explicit
release artifact; ordinary builds and HTML packaging do not require a stored
source-package approval. Release tooling
must publish the archive beside every browser artifact that contains the
covered binaries and retain it for the period required by the chosen GPLv3
distribution method.

This bundle covers the Coreutils binaries and WASTE support code identified by
its manifest. Other components in the complete browser page retain their own
source and notice obligations and must be audited separately.
