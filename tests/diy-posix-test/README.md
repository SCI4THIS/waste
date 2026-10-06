# diy-posix-test

This directory contains WASTE-specific POSIX regression probes. They exercise
the interpreter's `env` ABI and cooperative runtime; they are not an official
or complete POSIX conformance suite.

- `posix-kernel.wast` has seven legacy process/VFS/descriptor/signal assertions.
  Its installed compatibility-mode entry remains an explicit batch SKIP.
  Five expectations have C native/browser evidence; stopped-child scheduling
  and a concurrent sleeping foreground job remain profile gaps.
- `posix-kernel-runtime.cjs` is its legacy OCaml runtime driver.
- `posix-control-runtime.cjs` coordinates legacy OCaml pause/resume and SIGINT
  through a second host worker. The C counterpart uses a confirmed pselect
  wait; it does not establish resumable pause of a runnable busy loop.
- `spectest-isolation-{a,b}.wast` verify that modules within one script share
  their imported host memory while independently scheduled scripts receive
  fresh host environments.
- `bulk-operations.wast` checks passive memory/table initialization, overlapping
  memory copies, fills and indirect calls. It and the two host-memory fixtures
  use shared native/browser WAST command streams with unchanged authored bytes:
  20, 4 and 1 ordered checks respectively. No assembled compatibility assets
  are needed for these three scripts.

Run the installed DIY group directly in cli-rt:

```sh
build/cli-rt/waste-test --vfs-root=src/vfs --group=diy-posix-test --jobs=1
```

The group has four passing scripts (48 checks, including mmap) and the explicit
kernel compatibility SKIP. The same selection runs in html-rt's mounted suite.

The commands below exercise legacy direct/threaded OCaml artifacts. They
remain for coverage accounting during deferred retirement, not as required
parity gates for C kernel development. OCaml is now used only for Wasm/WAT/WAST
language checks; no additional OCaml POSIX/kernel capabilities will be added,
and its existing kernel is planned for removal. See
[the retirement plan](../../docs/active-ocaml-language-oracle-plan.md) and
[the C coverage ledger](../../docs/test-coverage.md).

The current native control gate runs without Node or HTML generation:

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

See [the complete DIY harness audit](../../docs/diy-posix-harness-coverage.md)
for all original expectations, ABI adaptations and retained gaps. No additional
OCaml providers or kernel behavior are planned, and no legacy driver is retired
by this audit.

Legacy reproduction commands:

```sh
node tests/diy-posix-test/posix-kernel-runtime.cjs
node tests/diy-posix-test/posix-kernel-runtime.cjs threaded
node tests/diy-posix-test/posix-control-runtime.cjs
node tests/diy-posix-test/posix-control-runtime.cjs threaded
```
