# diy-posix-test

This directory contains WASTE-specific POSIX regression probes. They exercise
the interpreter's `env` ABI and cooperative runtime; they are not an official
or complete POSIX conformance suite.

- `posix-kernel.wast` checks process identity, VFS and descriptor operations,
  pipes, fork/wait, process groups, signals, and terminal foreground groups. It
  is embedded in `browser-tests.html` under the `diy-posix-test` group.
- `posix-kernel-runtime.cjs` runs that WAST fixture against a compiled runtime.
- `posix-control-runtime.cjs` is a Node harness for externally timed pause,
  resume, and signal delivery. It remains a harness rather than a dashboard
  WAST test because it coordinates a second host worker.
- `spectest-isolation-{a,b}.wast` verify that modules within one script share
  their imported host memory while independently scheduled scripts receive
  fresh host environments.

Run both compiled variants with:

```sh
node tests/diy-posix-test/posix-kernel-runtime.cjs
node tests/diy-posix-test/posix-kernel-runtime.cjs threaded
node tests/diy-posix-test/posix-control-runtime.cjs
node tests/diy-posix-test/posix-control-runtime.cjs threaded
```
