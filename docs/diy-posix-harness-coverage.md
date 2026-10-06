# DIY POSIX harness coverage audit

Stage 6B.40, 2026-10-04: account for the legacy
`tests/diy-posix-test/posix-{kernel,control}-runtime.cjs` drivers before deferred
OCaml kernel retirement. Neither driver is a C acceptance gate. OCaml remains
only the Wasm/WAT/WAST OCaml reference implementation; no OCaml provider, kernel or scheduler
development is required by this audit.

## Kernel driver's seven guest expectations

The driver reads `posix-kernel.wast`, chooses a sequential/threaded OCaml
artifact, requests cooperative scheduling with quantum 50, awaits completion,
rejects a nonzero interpreter exit and propagates thrown errors. All seven
guest expectations already reside in authored WAST. The installed entry still
has `executionSpec.mode=browser-native`; both current batch controllers report
an explicit compatibility-mode SKIP. It is not seven passing C batch checks.

| Original ordinal / function | Original expectation | Current C evidence / disposition |
| --- | --- | --- |
| 1 / `identity` | pid * 100 + ppid = 100 | The unchanged module plus original assertions 1–5 passes natively and through browser C exports. The initial native process is pid 1, ppid 0; private process-lifecycle checks retain exact store identity. |
| 2 / `terminal-group` | process group equals stdin foreground group | Same original-prefix evidence; shared process-group session separately checks group identities, changes and terminal routing. |
| 3 / `vfs` | first four bytes of `hello` = 1819043176 | Same original-prefix evidence. Installed `path-vfs.wast` checks open/write/seek/read/close, the word and final byte separately, with additional errno/path cases. |
| 4 / `pipe-and-dup` | a duplicated reader receives the same word | Same original-prefix evidence. Installed `pipe-descriptors.wast` checks byte transfer through duplicate descriptors, EOF/close, flags and bounds. |
| 5 / `fork-wait-isolation` | parent word 11 + child exit(7) status 1792 = 1803 | Same original-prefix evidence includes one fork and one child exit, with the exact authored result. Linked-fork sessions and retained process-lifecycle sanitizer probes complement parent/provider memory ownership and wait/reaping. |
| 6 / `stop-continue-wait` | stopped status 4991 + exit(3) status 768 = 5759 | **Profile gap.** Running the full unchanged WAST natively passes the first five, then fails here: actual 1536. Child-first C execution does not reproduce the legacy stopped-child/parent scheduling sequence. Private wait/signal records cover their own APIs, not this whole guest scenario. |
| 7 / `foreground-job` | SIGTERM group kill produces wait status 15 | **Profile gap.** An isolated invocation of the unchanged module returns 0. C `sleep` currently returns zero; the child exits before the parent can set its group and kill it. Real group-routing sessions complement routing but do not establish this concurrent sleeping-child scenario. |

The generated prefix/isolated probes under the evidence directory retain the
original module bytes and assertion commands; they are diagnostic selections,
not new authored fixtures or installed test identities. The full original
run stops at assertion 6; it supplies no observation of assertion 7. The
isolated probe accounts for that seventh expectation separately. Neither
mismatch is converted to a passing assertion or new file-level XFAIL. The
compatibility SKIP remains visible, and neither legacy driver is removed.

## External-control driver and C counterpart

The control driver contains one guest assertion: an externally delivered
SIGINT reaches a handler installed by the module start function, and `run`
returns 2. It also creates a SharedArrayBuffer control page, publishes sequenced
pause/resume/signal operations from a second host worker, waits for the loader,
rejects a nonzero exit and terminates the controller in `finally`.

The C counterpart is `tests/guest-session-diy-control.wast` with the shared
`guest-session-diy-control.json` contract. A real `env.sigaction` installation
runs during module start. The guest reports exactly one installation, no
early handler call and no seen signal. It writes `STARTED`, then performs an
indefinite `pselect`. The host waits for that output and a SELECT yield from
pid 1 before sending SIGINT through the native control fd, browser export or
production worker. `run` must return 2; the handler count and installation count
must both remain one. Six guest assertions plus successful exit give **seven
shared checks**. No imported POSIX function is implemented by a JavaScript stub.

| Legacy boundary | Current disposition |
| --- | --- |
| Handler installed during module start | New guest start function and before/after counters verify installation is complete before external delivery and is not replayed on resume. |
| External SIGINT; handler observes signal 2 | New WAST `run` assertion through native, browser exports, packaged worker and actual offline Chromium. Existing wait/process-group sessions retain mask/routing checks. |
| C handler address representation | The C env ABI stores function index 4. The legacy source stores table slot 2. This is an explicit ABI adaptation; no claim of identical table/function-pointer representation is made. |
| Busy loop progresses only after host pause/resume | **Profile gap.** C handler delivery uses a supported pselect safe point. The new session does not prove arbitrary busy-loop interruption, an OCaml control-page ring, or pause/resume of runnable code. Deadline/cancellation controls abort execution; they do not replace a resumable pause. |
| Cross-worker sequence/version/ring bookkeeping | Legacy OCaml control protocol, retained for retirement accounting. Current C native WSC1 framing and browser messages have separate validation/control gates. Do not add a shared-memory control page or cross-origin-isolation requirement to the offline page. |
| Await completion, reject errors/nonzero exit, clean up host controller | Shared C session completion and existing cancellation/teardown gates. The new native gate adds a missing-event deadline and a deliberately wrong expected return as two explicit failure controls. Native control descriptors and worker termination remain host boundaries. |
| Sequential/threaded loader, fixed host sleeps | Legacy scheduling profile. C event delivery follows a confirmed guest marker/wait rather than depending on OCaml load duration. No new OCaml scheduling work is needed. |

`make -C src/cli-rt diy-posix-control` runs the native ASan/UBSan session and
both negative controls without Node or HTML generation. With no signal sent
after the confirmed wait, the host deadline must exit 124, retain three
completed assertions plus the interrupted fourth, and report no guest exit or
signal event. Demanding return 12 while actually sending SIGINT must exit 1
with the exact result-mismatch diagnostic: actual 2, expected 12;
no fabricated successful completion or guest exit. The gate writes its report
only after all controls pass. Standalone LeakSanitizer checks the same paths.

Run the shared native/browser/packaged-worker comparison with:

```sh
python3 tests/guest-session-check.py --scenario diy-control --page build/html-rt/bash.html
```

The offline browser automation now exercises this contract in real Chromium
as the sixth shared session. Existing five sessions preserve their ordered
results and transcripts; **351 + 7 = 358 checks**. Batch identities and
assertions remain unchanged. Only DIY README provenance is refreshed in the
installed manifest; pages are rebuilt explicitly.

## Language and host-memory cutover

Stage 6B.41, 2026-10-04, migrates the three remaining language/host-memory
compatibility fixtures without editing their authored WAST:

| Script | Preserved ordered checks | Shared stream coverage |
| --- | --- | --- |
| `bulk-operations.wast` | 20: six invokes and fourteen `assert_return` commands | Passive data/table initialization, overlapping memory copy, fill, table copy and indirect calls. All original arguments and expected values are preserved. |
| `spectest-isolation-a.wast` | 4 `assert_return` commands | Both modules initially read zero; storing 170 through `$writer` is observed through `$observer` because their imported memory aliases within this script. |
| `spectest-isolation-b.wast` | 1 `assert_return` command | Independently scheduled scripts receive fresh spectest memory and read zero, including when scheduled after the writer. |

The generator sends original source bytes to the C command stream rather than
assembling modules with Binaryen. Parsed counts from `--count` survive for DIY
streaming payloads; an uncounted payload records unknown zero. The distribution
preserves existing counts for unchanged source bytes, verified here against
the native parser and all 25 executed results. Explicit selection review
removes four assembled Wasm files and their three support directories.
Mmap retains its two data assets and execution specification; its native-parity
metadata now accurately records the existing shared engine backend.

The installed DIY group agrees across native ASan/UBSan, LeakSanitizer and the
production browser worker: **4 PASS / 1 SKIP / 48 checks**, including mmap's 23.
Native and browser host-boundary gates additionally run the original memory
pair in both manifest orders with one and three jobs, checking all four alias
results and the separate script's initial zero. All three fixtures pass the
native OCaml reference implementation in separate processes; this is language/standard
host-memory verification, without OCaml kernel imports or development.

The actual offline Chromium full batch agrees with native on **296 ordered
records / 64,309 assertions**, **289 PASS / 2 XFAIL / 5 SKIP**. Only the three
promoted entries change outcomes; all other 293 corpus records and all six
session transcripts/results remain unchanged. The kernel compatibility entry
and its scheduling/control gaps remain visible. Evidence and the complete
25-command legacy-to-stream ledger are under
`build/engine/refactor-stage6b-diy-language-cutover/`.

## Limits and retained work

This is complete accounting for these two legacy drivers' seven WAST
assertions, one embedded control assertion and host orchestration. Five kernel
expectations have direct C evidence; two kernel scenarios and resumable
busy-loop pause remain explicit profile gaps. The new control counterpart
adds supported coverage without clearing those gaps or authorizing scheduler
feature work. Keep private wait/signal/process memory and ownership checks,
compiled guest ABI probes, host controls and the legacy compatibility entry.
Kernel retirement, other C/CJS audits, ordinary setup-module acceptance,
supported OCaml reference implementation comparison and dashboard retirement remain open.

Evidence: `build/engine/refactor-stage6b-diy-posix-audit/` contains original
source/manifest/inventory snapshots, exact legacy selections and outcomes,
native sanitizer/leak and browser session reports, a per-expectation ledger,
engine/runtime source hashes and full ordered corpus/session parity proof.
