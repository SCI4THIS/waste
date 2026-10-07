# diy-posix-test

This directory contains WASTE-specific POSIX regression probes. They exercise
the interpreter's `env` ABI and cooperative runtime; they are not an official
or complete POSIX conformance suite.

- `posix-kernel.wast` has seven legacy process/VFS/descriptor/signal assertions.
  Its installed compatibility-mode entry remains an explicit batch SKIP.
  Five expectations have C native/browser evidence; stopped-child scheduling
  and a concurrent sleeping foreground job remain profile gaps.
- `spectest-isolation-{a,b}.wast` verify that modules within one script share
  their imported host memory while independently scheduled scripts receive
  fresh host environments.
- `bulk-operations.wast` checks passive memory/table initialization, overlapping
  memory copies, fills and indirect calls. It and the two host-memory fixtures
  use shared native/browser WAST command streams with unchanged authored bytes:
  20, 4 and 1 ordered checks respectively.

Run the installed DIY group directly in cli-rt:

```sh
build/cli-rt/waste-test --vfs-root=src/vfs --group=diy-posix-test --jobs=1
```

The group has four passing scripts (48 checks, including mmap) and the explicit
kernel compatibility SKIP. The same selection runs in html-rt's mounted suite.

The native control gate runs without Node or HTML generation:

```sh
make -C src/cli-rt diy-posix-control
```

It checks a start-installed handler, external SIGINT, exactly-once counters,
completion, a missing-event deadline and a deliberate result mismatch. The
same seven-check session runs through browser exports, the production worker
and actual offline Chromium. To compare the shared adapters explicitly:

```sh
python3 tests/guest-session-check.py --scenario diy-control --page build/html-rt/bash.html
```
