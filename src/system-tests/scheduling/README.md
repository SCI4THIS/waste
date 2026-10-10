# Process scheduling probes

The authored WAST files test the shared native/browser process scheduler:

- `timers.wast` arms three overlapping sleeps, checks WNOHANG and each wait
  status, then configures and terminates a sleeping child's process group.
- `cpu.wast` spins on a zero-timeout pipe readiness query until its parent gets
  a CPU slice and writes a byte. Its second assertion transfers more than a
  pipe buffer can hold, requiring blocked writers and readers to yield.
- `handler-long.wast` and `handler-short.wast` deliberately reuse module IDs and
  registrations. The aux-bash scenario runs these concurrently from Bash and
  checks that each handler resumes and cleans up only its own definitions.

Run the scheduler probes through the complete session driver. The harness uses
controlled guest-clock advances for `timers.wast`, checks the same assertions
through browser exports, and exercises the packaged Bash worker when a page is
provided:

```sh
make -C src/cli-rt guest-session-sanitize
make -C src/html-rt bash-html
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  python3 tests/guest-session-check.py \
  --native build/cli-rt/private/guest-session-sanitize \
  --scenario scheduling-timers --scenario scheduling-cpu --scenario aux-bash \
  --page build/html-rt/bash.html \
  > build/engine/concurrent-session-results.txt \
  2> build/engine/concurrent-session-check.log
```

Leak detection is disabled for the documented ptrace environment; address and
undefined-behavior checks remain enabled. These are system probes: fork inside
an executed WAST command still needs the complete session driver rather than
`/bin/wast`'s command-handler profile. The two handler fixtures themselves can
run through `/bin/wast --verbose` from Bash.

For a manual Firefox check, reopen the rebuilt offline `bash.html` and run:

```bash
(sleep 3; echo 3) & (sleep 2; echo 2) & (sleep 1; echo 1); wait
```

Expect `1`, `2`, `3`, with elapsed time near the longest sleep plus interpreter
startup, followed by the prompt. Background jobs also progress while the shell
waits at its prompt:

```bash
(sleep 1; echo BACKGROUND_DONE) &
```

Leave the shell idle; `BACKGROUND_DONE` should appear without pressing Enter.
Concurrent scheduling shares one interpreter worker. Guest pthread creation/join
and simultaneous execution on multiple host cores are separate implementation
work; the host upload/download/test-request channel currently remains serialized.
