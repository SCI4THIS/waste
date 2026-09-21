# C-engine WebGL shell and external-program plan

Status: active

## Objective

Replace the current form-and-`<pre>` Bash presentation with a WebGL terminal,
then extend the C engine from the existing interactive Bash proof to a useful
shell environment that can execute wasm32 programs such as GNU coreutils.

The completed browser artifact must remain one self-contained `file://` HTML
document. It must not require a server, cross-origin isolation,
`SharedArrayBuffer`, or Asyncify.

## Target architecture

The terminal boundary is a byte stream, not a curses API:

```text
browser keyboard/paste
        |
        v
input byte encoder ---- resize/signal events
        |                       |
        v                       v
C-engine terminal and process kernel
        |
        +---- Bash or child wasm32 process
        |
        v
terminal output bytes
        |
        v
VT/ANSI parser -> character/attribute grid -> WebGL renderer
```

JavaScript owns browser events, VT presentation state, glyph rendering, and
canvas sizing. The C engine owns termios state, canonical input, echo, signals,
file descriptors, processes, the VFS, executable lookup, and scheduling.

An executable image consists of process-private runtime memory/table state, a
libc instance, and an application instance. Decoded module templates may be
immutable and shared, but mutable instances and address spaces may not be
shared between processes unless POSIX thread semantics explicitly require it.

## Design constraints

- Reuse the useful MIT-licensed font/grid/WebGL techniques from
  [SCI4THIS/rogue-wasm](https://github.com/SCI4THIS/rogue-wasm), with
  attribution, but do not copy its execution model.
- Do not expose `move`, `addch`, `refresh`, `wgetch`, or general libc calls as
  JavaScript implementations. Ordinary programs write bytes to descriptors;
  guest ncurses will eventually emit terminal control sequences through the
  same path.
- Do not use Binaryen Asyncify, unwind/rewind imports, JSPI, or re-entry into
  `main`. A blocked guest operation yields through the engine's explicit
  continuation/checkpoint machinery and resumes through a new engine call.
- Keep POSIX state in the engine-owned kernel. JavaScript may provide only
  genuine browser capabilities such as input delivery, display, clocks,
  entropy, persistence, and the optional broker.
- Preserve the current offline packaging and the normal/missing-command Bash
  acceptance tests throughout the work.
- Compile applications against a documented WASTE wasm32 ABI. Undefined
  imports must resolve to guest libc/runtime modules or the versioned
  `waste_kernel` boundary, not an expanding collection of ad hoc JavaScript
  functions.
- Preserve repository-owned third-party changes as explicit patches and record
  pinned upstream revisions and licenses.

## Current baseline

- `build/html-rt/bash.html` starts Bash through the C engine and accepts later
  input after a failed command.
- `src/html-rt/src/bash/app.js` renders output through the VT model and WebGL
  canvas, with a bounded transcript for accessibility; the legacy form remains
  only as a fallback path.
- `src/html-rt/src/bash/worker.js` already carries terminal bytes across the
  browser/engine boundary without Asyncify.
- The engine terminal currently acts primarily as an input queue. Guest
  termios, `isatty`, raw/canonical behavior, kernel echo, and window sizing are
  incomplete.
- `native_posix_execve` currently returns `ENOENT` for every path. Therefore no
  successful external program can replace the forked child image yet.
- The packaged tar loader is an asset store, not yet the engine VFS used by
  pathname and executable operations.
- The existing process continuation is deliberately bounded and child-first;
  it is enough to prove one external child but not pipelines or concurrent jobs.

## Implementation log

- Stage 1 added `src/html-rt/src/bash/terminal/renderer.js`, a WebGL2 glyph
  atlas renderer with a Canvas2D fallback, and changed the Bash page to use a
  canvas plus a bounded accessibility transcript. The amalgamator now inlines
  the terminal sources so the generated page remains self-contained.
- Stage 2 added `src/html-rt/src/bash/terminal/model.js`, with incremental
  UTF-8/VT parsing, cursor movement, erasure, SGR colors, scrolling, alternate
  screen handling, and bounded escape-sequence state. Its standalone fixture is
  `tests/c-engine-terminal-model.cjs`.
- Stage 3 now routes all engine output through the model and renderer and
  translates canvas keyboard and paste events directly into terminal bytes;
  the frontend no longer maintains an editing buffer or manually echoes input.
  The normal and command-not-found browser fixtures pass after this cutover.
- Stage 4 added stable termios and window-size records to the kernel, canonical
  line buffering, erase/kill editing, VINTR-to-SIGINT handling, raw-mode
  readiness, `isatty`, `tcgetattr`, `tcsetattr`, and TTY window-size ioctl
  boundaries. The ASan/UBSan POSIX-kernel fixture now covers these transitions.
  Kernel creation remains byte-oriented for compatibility until the Bash image
  explicitly selects canonical/echo mode; browser per-key delivery is therefore
  intentionally not enabled yet. The interactive browser terminal now selects
  canonical/echo mode in the engine, and the browser API exposes bounded
  `waste_wast_resize_terminal`; the worker forwards model dimensions on startup
  and browser resize events into the engine winsize record.
- Stage 5 now has a first wasm32 application ABI probe in
  `tests/c-engine-waste-probe.c`, built by the `waste-probe` target in
  `src/cli-rt/Makefile`. It imports only `env.write`, exports `_start`, keeps
  memory in the Wasm image, and emits a deterministic marker. Argument,
  environment, executable manifests, and process-image replacement remain in
  the next Stage 5/6 slice. The initial contract is recorded in
  `docs/wasm32-abi.md`; it makes process-private mutable state, `_start`,
  pointer-span validation, errno, and pre-replacement validation explicit.
  `make -C src/cli-rt waste-probe BUILD_DIR=../../build/cli-rt` produces a
  valid wasm32 MVP image containing only the probe marker, `_start`, and
  `write`; native image instantiation and process replacement are still open.
- The executable registry, image loader, browser staging, continuation
  handoff, and parent capsule restoration are now covered by the native
  lifecycle/matrix fixtures and all three Bash browser modes. Stage 6 may
  continue with its remaining VFS, executable-manifest, and packaged utility
  work; the resolved parent-frame defect must not be reopened.

## Stage 1: Isolate and port the WebGL grid renderer

Status: complete (renderer and offline packaging gate)

Create a renderer under `src/html-rt/src/bash/terminal/` that is independent of
the C engine and terminal parser.

Work:

- Record rogue-wasm's pinned source revision and preserve its MIT notice for
  copied or derived files.
- Port the glyph/font loading, shader, buffer, and grid drawing pieces needed
  for a fixed character terminal.
- Define a renderer interface that accepts rows, columns, cell code points,
  foreground/background colors, attributes, and cursor state.
- Use a `<canvas>` in the Bash page while retaining the old `<pre>` behind a
  temporary debug flag.
- Avoid one browser-to-Wasm or one shader-program setup operation per cell;
  keep rendering data in typed arrays and batch redraws where practical.
- Add a deterministic standalone renderer fixture for printable text, colors,
  cursor placement, and resize.

Gate:

- An 80x24 test screen renders from local assets in the staging page and the
  amalgamated `file://` page.
- Renderer tests do not instantiate Bash or the C engine.
- No Asyncify, `SharedArrayBuffer`, server, or external asset is introduced.

## Stage 2: Add a deterministic VT/ANSI terminal model

Status: complete (model fixtures and generated-page gate)

Place a tested terminal state machine between output bytes and the WebGL grid.

Initial supported behavior:

- Incremental UTF-8 decoding across output chunks.
- Printable cells, CR, LF, tab, backspace, wrapping, and scrolling.
- CSI cursor movement and absolute positioning.
- Erase in line/display and saved/restored cursor.
- SGR reset, common attributes, 16 colors, 256 colors, and RGB colors.
- Cursor visibility and primary/alternate screen buffers.
- Bounded parsing of malformed, truncated, and oversized escape sequences.

Work:

- Keep parser/model code independent of WebGL so Node tests can compare exact
  cell grids and cursor state.
- Coalesce dirty ranges and schedule rendering with `requestAnimationFrame`.
- Preserve a bounded diagnostic transcript without retaining unlimited shell
  output in the DOM.

Gate:

- Node fixtures validate split escape sequences, scrolling, colors, erasure,
  cursor movement, malformed input, and alternate-screen entry/exit.
- A recorded Bash transcript produces the expected final grid.

## Stage 3: Put existing Bash on the WebGL terminal

Status: complete (canvas byte-input cutover and browser gates)

Replace the line-submit UI with a focusable terminal surface and deliver
keyboard/paste bytes to the existing worker input message.

Work:

- Translate printable input, Enter, Tab, Backspace, Escape, arrows, Home, End,
  Delete, Page Up/Down, and function keys to terminal bytes.
- Support paste as UTF-8 bytes and add bracketed-paste framing when the terminal
  mode requests it.
- Stop manually echoing submitted text in `app.js`; display only bytes emitted
  by the guest terminal path.
- Retain explicit buttons for restart, signals, and stop while making the
  canvas itself the normal input target.
- Keep the worker protocol renderer-agnostic and byte-oriented.

Gate:

- The current Bash startup and command-not-found tests pass through the new
  frontend protocol.
- A generated offline page accepts multiple commands, editing keys, paste,
  Ctrl-C, and normal exit without completing prematurely.
- Text selection/accessibility has an explicit fallback or mirrored bounded
  transcript rather than depending solely on canvas pixels.

## Stage 4: Implement engine-owned TTY and termios semantics

Status: in progress (canonical/raw minimum complete; timeout and job-control surface pending)

Move interactive input policy into the C kernel so Bash and later full-screen
programs see a credible terminal rather than a browser line editor.

Work:

- Add terminal attributes and window dimensions to the engine terminal object.
- Implement `isatty`, `tcgetattr`, `tcsetattr`, `tcflow`, and the required
  terminal `ioctl` requests, including `TIOCGWINSZ`/`TIOCSWINSZ`.
- Implement canonical and noncanonical reads, `ECHO`, `ICANON`, `ISIG`, input
  editing controls, `VMIN`, and `VTIME` to the supported POSIX boundary.
- Convert configured control characters into engine signals for the foreground
  process group. Deliver resize as SIGWINCH.
- Preserve wait/readiness behavior across mode changes and input arriving
  before or after a yield.

Gate:

- Native warnings-as-errors, ASan, and UBSan terminal tests cover canonical,
  raw, echo, EOF, interrupt, resize, readiness, and repeated yield/resume.
- Direct and threaded POSIX harnesses remain aligned where applicable.
- Browser Bash no longer relies on frontend line echo and remains interactive.

Implementation update (2026-09-20):

- Added engine-owned `tcflow` validation and the versioned `tcflow_v1` host
  boundary. Raw reads now honor `VMIN` thresholds instead of returning partial
  input immediately, and terminal resize raises `SIGWINCH` in the kernel signal
  set.
- Extended the warnings-as-errors ASan/UBSan POSIX fixture with raw `VMIN`,
  `tcflow`, resize, and `SIGWINCH` checks: 259 tests pass. `VTIME` deadlines,
  foreground job control, and full signal disposition handling remain for the
  next TTY slice.

## Stage 5: Define the executable-image and wasm32 application ABI

Status: in progress (ABI baseline and process-image probe complete; startup-data coverage pending)

Specify the contract needed to build and instantiate external programs before
porting a large upstream package.

Work:

- Document module imports/exports, `_start` or `main` entry, constructor order,
  argv/environment layout, exit behavior, memory/table ownership, errno, and
  signal entry points.
- Make registered modules and mutable instances process-scoped. Retain immutable
  decoded-module sharing where safe.
- Define an executable manifest containing path, mode, module bytes/template,
  ABI version, and optional entry metadata.
- Add a clang/wasm-ld build helper or minimal sysroot that produces ABI-compliant
  applications without Asyncify.
- Build a small `waste-probe` executable that reports argv, environment, cwd,
  PID, descriptor behavior, and a requested exit status.
- Add a build-time import audit that rejects unknown modules/functions and all
  Asyncify symbols.

Gate:

- `waste-probe.wasm` validates and instantiates natively in the C engine.
- Its import inventory contains only the documented runtime/libc/kernel ABI.
- Two instances receive isolated mutable memory and globals.

Implementation update (2026-09-20):

- The ABI baseline, immutable executable manifest, import audit, `_start`
  contract, process-private image ownership, and isolated-instance checks are
  now exercised by the executable matrix and probe path. Full argv/envp startup
  block reporting and utility-facing libc coverage remain open.

## Stage 6: Implement successful `execve` and process-image replacement

Status: in progress (bounded replacement path complete; broader status fixtures pending)

Extend the bounded child-first continuation so a forked Bash child can replace
itself with `waste-probe`, exit, and be reaped by the restored parent.

Work:

- Validate and copy pathname, argv, and envp out of the old image before
  destroying or replacing any caller state.
- Resolve paths and executable permission through the engine VFS and executable
  registry.
- On success, preserve PID, parent, process group/session, cwd, environment,
  signal dispositions required by POSIX, and non-close-on-exec descriptors.
- Instantiate the new runtime/libc/application image and begin its entry point.
- Ensure successful `execve` never returns to the old image. Preserve the old
  image and set precise errno on failure.
- Route child exit status through the existing zombie/waitpid path and resume
  Bash at the next prompt.

Gate:

- From browser Bash, `/bin/waste-probe one two` observes the correct arguments,
  environment, cwd, PID, and descriptors, then returns to the prompt.
- Exit 0, nonzero exit, signal termination, missing executable, malformed Wasm,
  and permission failure have correct shell statuses.
- Repeated successful and failed execs do not leak or corrupt checkpoints.

Implementation update (2026-09-20):

- The browser process driver now carries the replacement image through input
  waits and parent restoration. The browser probe covers repeated successful
  replacement and failed command status `0/127/0`; native sanitizer fixtures
  cover malformed, permission, missing, directory, symlink, exit, and reap
  transitions.
- Remaining Stage 6 breadth is explicit nonzero/signal executable fixtures and
  descriptor/CLOEXEC inheritance checks; those depend on the expanded startup
  ABI and VFS work rather than the resolved parent-restoration mechanism.

## Stage 7: Connect packaged files to the engine VFS

Status: blocked by Stage 6's executable/VFS boundary

Make the self-contained page's packaged filesystem visible through the same
engine VFS used by `stat`, directory enumeration, file descriptors, and
`execve`.

Work:

- Define the boot-time VFS manifest and bounded browser-to-engine loading API.
- Materialize directories, regular files, executable metadata, and symlinks in
  the engine kernel. Keep the JavaScript tar map as packaging input, not POSIX
  state.
- Complete `open`, read/write/seek, cwd, `chdir`, `getcwd`, directory iteration,
  link handling, metadata, permissions, and writable `/tmp` behavior needed by
  the initial utilities.
- Preserve descriptor/open-file-description sharing across fork and isolation
  across independent test sandboxes.

Gate:

- A purpose-built external `ls` probe lists `/`, `/bin`, and an empty directory.
- `cat`-style reads, file creation in `/tmp`, stat/lstat, cwd changes, and
  pathname errors pass native and browser tests.
- Restarting the shell begins from a deterministic filesystem state unless an
  explicit persistence backend is enabled.

## Stage 8: Add a reproducible GNU coreutils wasm32 build

Status: blocked by the unimplemented executable image and target libc ABI

Port coreutils only after the executable ABI and VFS are demonstrated by small
fixtures.

Work:

- Pin an upstream release/revision and record GPLv3 distribution obligations.
- Prefer an upstream release tarball for the initial cross build unless a git
  checkout is required; keep WASTE-specific changes in a repository-owned
  patch rather than upstream history.
- Cross-configure against the WASTE wasm32 headers/runtime and explicitly record
  configure cache answers. Separate build-machine generators from target code.
- Retain unresolved calls only when they are members of the documented
  libc/kernel ABI. Generate a per-program missing-import report.
- Bring up utilities incrementally: `true`, `false`, `pwd`, external `printf`
  or `echo`, `cat`, `basename`, `dirname`, `wc`, and finally `ls`.
- Package installed executable modules under `/bin` in the self-contained page.
- Add focused conformance fixtures for every enabled utility and document
  intentional option/platform limitations.

Gate:

- Browser Bash runs `/bin/true`, `/bin/false`, `/bin/pwd`, `/bin/cat`, and
  `/bin/ls` and receives correct exit statuses after each command.
- `ls /bin` reports the packaged executables from the engine VFS and the prompt
  remains usable afterward.
- Builds are incremental, reproducible, and do not silently accept new imports.

## Stage 9: General scheduling, pipelines, and guest ncurses

Status: pending (starts after executable images and VFS are available)

Move beyond the bounded single-child proof to the process behavior required by
a practical interactive shell and full-screen terminal programs.

Work:

- Add explicit runnable/blocked/exited thread and process states with a bounded
  scheduler; do not infer scheduler state in JavaScript.
- Run pipe producers and consumers until each blocks, exits, or yields, while
  preserving descriptor closure and SIGPIPE behavior.
- Complete redirection, descriptor duplication, foreground process groups,
  stop/continue, terminal ownership, and job-control signals.
- Compile guest ncurses against the WASTE libc/TTY ABI so it writes VT sequences
  through ordinary descriptors. Do not create a separate JavaScript curses
  syscall surface.
- Add one small ncurses fixture before attempting a larger application such as
  Rogue.

Gate:

```sh
ls /bin | wc -l
ls /bin > /tmp/list
cat /tmp/list
```

works from browser Bash, and a guest ncurses fixture enters the alternate
screen, accepts raw key input, restores terminal state, and returns to a usable
prompt.

## Verification required after every stage

- Keep native C tests warnings-as-errors and run focused ASan/UBSan gates with
  the documented LeakSanitizer exception for the ptrace environment.
- Run the existing normal and command-not-found C-engine Bash browser tests.
- Regenerate and manually exercise `build/html-rt/bash.html` through `file://`.
- Audit the engine and application Wasm imports/exports for Asyncify symbols.
- Run `bash -n start.sh`, Python bytecode checks for changed tools,
  `node --check` for changed Node/worker sources, and `git diff --check`.
- Update this document's stage status and durable `architecture.md` or
  `techniques.md` guidance when a stage is completed.

## Completion criteria

This plan is complete when the generated offline C-engine Bash page:

- Uses the WebGL terminal for normal presentation and per-key input.
- Implements terminal modes, signals, resize, and repeated waits without
  Asyncify.
- Executes packaged wasm32 programs through engine-owned `execve` semantics.
- Provides a useful initial GNU coreutils set including `ls` and `cat`.
- Supports redirection and a multi-process pipeline.
- Runs a guest ncurses fixture through the same byte-stream TTY path.
- Preserves process/address-space isolation, VFS ownership, and the existing
  Wasm conformance gates.

After these criteria are met and durable information has been consolidated into
`architecture.md` and `techniques.md`, delete this active plan.
