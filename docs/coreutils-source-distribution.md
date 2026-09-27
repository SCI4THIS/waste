# GNU Coreutils corresponding source

The WASTE browser package distributes selected GNU Coreutils programs under
GPLv3-or-later. Its corresponding-source artifact is generated as
`build/coreutils/coreutils-corresponding-source.tar.gz` by:

```sh
make -C src/html-rt BUILD_DIR=../../build/html-rt coreutils-source-package
```

The archive contains the exact patched and bootstrapped Coreutils source tree,
the repository-managed patch, configure answers and reports, WASTE engine and
guest-runtime source, interface headers, build scripts, and license notices.
`build/coreutils/coreutils-source-package.json` records the archive SHA-256,
source commit, covered utility names, and these instructions. Release tooling
must publish the archive beside every browser artifact that contains the
covered binaries and retain it for the period required by the chosen GPLv3
distribution method.

This bundle covers the Coreutils binaries and WASTE support code identified by
its manifest. Other components in the complete browser page retain their own
source and notice obligations and must be audited separately.
