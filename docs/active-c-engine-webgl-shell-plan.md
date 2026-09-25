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
VT/ANSI parser -> character/attribute grid -> GLF/Bézier WebGL renderer
```

JavaScript owns browser events, VT presentation state, glyph rendering, and
canvas sizing. The C engine owns termios state, canonical input, echo, output
post-processing, signals, file descriptors, processes, the VFS, executable
lookup, and scheduling. The canvas is the terminal surface; browser key events
are encoded as terminal input bytes and are not interpreted as a second shell
line editor.

An executable image consists of process-private runtime memory/table state, a
libc instance, and an application instance. Decoded module templates may be
immutable and shared, but mutable instances and address spaces may not be
shared between processes unless POSIX thread semantics explicitly require it.

## Design constraints

- Port the proven GLF/Bézier font renderer from
  [SCI4THIS/rogue-wasm](https://github.com/SCI4THIS/rogue-wasm) at pinned
  revision `28a574d9fe602165e77c52f2b629ffee4477a429`. Reuse its cmap lookup,
  curve geometry, shaders, glyph-range lookup, and cell transforms wherever
  they fit the terminal renderer, while preserving its MIT notice and the
  VT323 font's OFL notice.
- Do not replace that renderer with a Canvas2D-generated texture atlas, a
  bitmap font, signed-distance-field texture, or other raster glyph cache.
  Glyphs are drawn directly from the GLF point/index data, and the fragment
  shader evaluates the encoded quadratic Bézier regions.
- Reuse rogue-wasm's rendering implementation, not its execution model. Its
  JavaScript curses imports and Asyncify-based control flow do not cross into
  WASTE; the existing VT byte stream, terminal model, engine scheduler, and
  explicit continuations remain authoritative.
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
- Keep the transcript only as a bounded accessibility/diagnostic mirror while
  the migration is in progress. The eventual normal presentation is entirely
  the canvas and its VT model; no separate HTML text surface may become a
  second source of cursor or newline semantics.
- Compile applications against a documented WASTE wasm32 ABI. Undefined
  imports must resolve to guest libc/runtime modules or the versioned
  `waste_kernel` boundary, not an expanding collection of ad hoc JavaScript
  functions.
- Preserve repository-owned third-party changes as explicit patches and record
  pinned upstream revisions and licenses.

## Current baseline

- `build/html-rt/bash.html` starts Bash through the C engine and accepts later
  input after a failed command.
- `src/html-rt/src/bash/app.js` routes output through the VT model to a WebGL
  canvas, with a bounded transcript for accessibility. The WebGL path now
  uses the reopened Stage 1 GLF/Bézier renderer, and terminal output is
  normalized by the engine before both the model and transcript consume it.
- `src/html-rt/src/bash/worker.js` already carries terminal bytes across the
  browser/engine boundary without Asyncify.
- The engine terminal currently acts primarily as an input queue. Guest
  termios, `isatty`, raw/canonical behavior, kernel echo, and window sizing are
  incomplete.
- `native_posix_execve` now resolves registered executable manifests and carries
  successful replacements through the child capsule; packaged tar files are
  still a separate Stage 7 VFS concern.
- The packaged tar loader is an asset store, not yet the engine VFS used by
  pathname and executable operations.
- The existing process continuation is deliberately bounded and child-first;
  it is enough to prove one external child but not pipelines or concurrent jobs.

## Implementation log

- The original Stage 1 added `src/html-rt/src/bash/terminal/renderer.js`, a
  provisional WebGL2 texture-atlas renderer with a Canvas2D fallback, and
  changed the Bash page to use a canvas plus a bounded accessibility
  transcript. Browser inspection subsequently showed incorrect glyphs, and
  the atlas design did not match the intended rogue-wasm renderer. The stage
  was reopened and the atlas path has now been replaced by the direct GLF
  implementation described below.
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
- Stage 5's wasm32 application ABI probe is in `tests/c-engine-waste-probe.c`,
  built by the `waste-probe` target in `src/cli-rt/Makefile`. It exports
  `_start`, keeps memory in the Wasm image, validates startup metadata and
  standard descriptor flags, and emits deterministic markers. The completed
  contract is recorded in
  `docs/wasm32-abi.md`; it makes process-private mutable state, `_start`,
  pointer-span validation, errno, and pre-replacement validation explicit.
  `make -C src/cli-rt waste-probe BUILD_DIR=../../build/cli-rt` produces a
  valid wasm32 MVP image containing only the probe marker, `_start`, and
  `write`; native image instantiation and process replacement are still open.
- The executable registry, image loader, browser staging, continuation
  handoff, and parent capsule restoration are covered by the native
  lifecycle/matrix fixtures and all three Bash browser modes. The resolved
  parent-frame defect must not be reopened; packaged tar files remain Stage 7
  work.
- Stage 7 now has a bounded boot-time metadata handoff: the Bash worker submits
  `/tmp`, `/usr/bin`, and the staged probe path to the engine before starting
  Bash, and the engine owns those pathname records after terminal creation.
- Stage 7 now carries bounded regular-file bytes through the same boot handoff.
  Kernel path nodes own immutable packaged contents until a writable open grows
  them; regular descriptors have shared offsets, `open` honors create/truncate/
  append modes, and browser `open/read/write` calls use the engine VFS rather
  than the host stub. The focused native path fixture covers packaged reads,
  `/tmp` creation, writes, and updated sizes. Directory iteration, tar-entry
  enumeration, symlink resolution, and a general writable overlay remain open.

## Stage 1: Isolate and port the WebGL grid renderer

Status: in progress (2026-09-20; GLF renderer and offline integration
complete, screenshot gate pending a browser that can run in this environment)

Create a renderer under `src/html-rt/src/bash/terminal/` that is independent of
the C engine and terminal parser.

The source of truth for this port is rogue-wasm revision
`28a574d9fe602165e77c52f2b629ffee4477a429`, especially `src/curses.js`, the
`create_prag`/`create_prog` shader setup in `src/index.html`, and
`src/VT323-glf.js`. Its GLF object contains:

- `cmap` subtables used to translate Unicode code points to font glyph IDs;
- `lookup[glyph]` entries containing the first index and index count;
- `pts`, with four floats per vertex (`x`, `y`, and curve-local `s`, `t`);
- `idx`, the triangle element indices consumed as `UNSIGNED_INT` values.

The vertex shader transforms font-space coordinates into the selected terminal
cell and forwards `s,t`. The fragment shader uses the sign of `t` and the
quadratic test `1 - s * s < abs(t)` to select or discard the Bézier region.
This is the required rendering mechanism; it does not sample a font texture.

### Stage 1A: Import and provenance

- Add the pinned GLF data as a repository-owned, offline source asset. Do not
  fetch it at page load or build time.
- Add the rogue-wasm MIT license and revision/provenance record beside the
  derived renderer code. Add the VT323 OFL license and upstream font
  provenance beside the GLF data.
- Keep the original numeric point/index data intact for the first working
  port. Any later size optimization must have a glyph-equivalence gate and
  must not turn the data into a texture atlas.
- Extend the HTML amalgamator so the GLF data is inlined in the generated
  `file://` document rather than loaded as an external URL.

### Stage 1B: Faithful GLF rendering core

- Port rogue-wasm's cmap format 4 and format 6 lookup behavior, including the
  missing-glyph path, into a small renderer-owned helper with deterministic
  unit fixtures.
- Upload `pts` once as a static vertex buffer and `idx` once as a static
  element buffer. Preserve the four-float vertex layout and the GLF
  `lookup.start`/`lookup.len` draw ranges.
- Port the rogue-wasm vertex and fragment shader logic directly, adapting only
  WebGL version syntax, color uniforms, and the local program/buffer helpers.
- Port the font-space-to-cell transform using named metrics rather than
  unexplained literals. First match rogue-wasm's proven VT323 metrics and
  orientation exactly; tune terminal padding only after glyph identity and
  baselines are correct.
- Draw cell backgrounds and cursor rectangles separately from glyph curves.
  Apply the terminal model's foreground color to the curve shader without
  changing its coverage test.
- Do not retain `makeAtlas`, `uAtlas`, UV calculation, `texImage2D`, or any
  font-texture sampling in the WebGL path after the cutover.

### Stage 1C: Terminal integration

- Preserve the existing renderer interface: rows, columns, code points,
  foreground/background colors, attributes, cursor state, dirty redraw, and
  resize. The VT model remains unaware of GLF representation.
- Map Unicode code points through the GLF cmap. Use the font's missing glyph
  when a code point is unmapped; treat space as an intentional empty glyph,
  not as an error.
- Retain the bounded text transcript as the accessibility and diagnostics
  surface. A Canvas2D fallback may remain for browsers without WebGL, but it
  is explicitly a fallback and is not used to create WebGL textures.
- Keep shader/program creation outside the cell loop. The faithful initial
  port may issue one indexed glyph draw per visible non-space cell, as
  rogue-wasm does. Optimize batching only after visual equivalence, using GLF
  geometry rather than rasterization.

### Stage 1D: Deterministic verification

- Add pure fixtures for cmap format 4/6 lookup, space, ASCII punctuation,
  digits, upper/lowercase letters, an unmapped code point, and exact
  `lookup.start`/`lookup.len` selection.
- Add a standalone renderer page that draws a fixed 80x24 screen containing
  glyph-shape-sensitive strings such as `Il1|`, `O0`, `[]{}()`, `/\\`, and a
  full printable ASCII row, plus foreground/background colors and a cursor.
- Add a headless browser pixel/screenshot gate that verifies blank cells stay
  blank and samples several known glyph cells. Store a compact expected image
  or deterministic pixel assertions, not a copy of the full Bash artifact.
- Test resize and device-pixel-ratio changes without changing the logical
  terminal grid or glyph selection.
- Regenerate `build/html-rt/bash.html`, open it as `file://`, and verify the
  same fixed screen and live Bash prompt use the GLF path with no external
  requests.

Gate:

- The standalone and amalgamated `file://` pages render the same legible VT323
  glyphs from GLF curve data on an 80x24 test screen; blank cells are blank.
- Runtime inspection shows no glyph texture creation or sampling and no
  Canvas2D-generated atlas in the WebGL path.
- Cmap/range tests and the headless visual gate pass without instantiating Bash
  or the C engine.
- The normal, command-not-found, and executable-probe Bash browser gates still
  pass after the renderer replacement.
- No Asyncify, `SharedArrayBuffer`, server, cross-origin requirement, or
  external runtime asset is introduced.

Implementation update (2026-09-20):

- Vendored the pinned rogue-wasm GLF asset as
  `src/html-rt/src/bash/terminal/glf.js` and recorded the rogue-wasm MIT and
  VT323 OFL provenance in `GLF-NOTICES.md`.
- Replaced the Canvas2D-generated atlas with direct static GLF point/index
  buffers, format 4/6 cmap lookup, rogue-wasm-derived cell transforms, and
  the analytic quadratic curve fragment shader. Cell backgrounds and the
  cursor are rendered independently, and the Canvas2D path remains only as a
  no-WebGL fallback.
- Extended the amalgamator and staging page to inline the GLF data into the
  offline HTML. The generated page is about 7.9 MB and contains no external
  GLF request or glyph texture path.
- Added `tests/c-engine-terminal-glf.cjs`; its 11 cmap/range cases pass over
  135,687 vertices and 63,123 triangles. Normal, command-not-found, and
  executable-probe C-engine browser protocol gates all pass.
- Chromium and Firefox both terminate before producing a screenshot in the
  current ptrace-restricted environment, so the headless pixel portion of the
  gate remains open and must be run on a functioning browser host. This is an
  environment limitation, not a passing visual claim.

## Stage 2: Add a deterministic VT/ANSI terminal model

Status: complete

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
- LF/CRLF cursor parity is validated together with the engine's `OPOST`/
  `ONLCR` output-processing tests in Stage 4; the model must not silently
  rewrite raw-mode output.

Completion update (2026-09-21):

- Stage 4 now supplies the engine-owned `OPOST`/`ONLCR` byte stream used by
  both the VT model and transcript, closing the previously deferred LF/CRLF
  parity gate. Stage 2 is complete; remaining visual screenshot validation is
  isolated to Stage 1.

## Stage 3: Put existing Bash on the WebGL terminal

Status: complete (canvas byte-input cutover and browser gates)

Replace the line-submit UI with a focusable canvas terminal surface and deliver
keyboard/paste bytes to the existing worker input message. The HTML input form
may remain as a temporary accessibility/debug fallback, but it is not part of
the terminal's normal editing or display path.

Work:

- Translate printable input, Enter, Tab, Backspace, Escape, arrows, Home, End,
  Delete, Page Up/Down, and function keys to terminal bytes.
- Encode Ctrl-C as the terminal's configured `VINTR` byte (normally ETX, 0x03)
  and Ctrl-D as its configured `VEOF` byte (normally EOT, 0x04). JavaScript
  must not directly synthesize a process signal or consume EOF; the engine's
  termios and foreground-process-group logic owns those effects.
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
  Ctrl-C/SIGINT, Ctrl-D/EOF, and normal exit without completing prematurely.
- Text selection/accessibility has an explicit fallback or mirrored bounded
  transcript rather than depending solely on canvas pixels.
- The canvas remains the only normal display and input surface; the bounded
  transcript is observational and cannot alter terminal state.

## Stage 4: Implement engine-owned TTY and termios semantics

Status: complete for the current browser TTY boundary (canonical/raw, cooked
output, VTIME timing, signal dispositions, guest `sigaction`, caught-handler
delivery, temporary handler masks, and foreground process-group routing)

Move interactive input and output policy into the C kernel so Bash and later
full-screen programs see a credible terminal rather than a browser line
editor. The byte stream delivered to the VT model must be the same stream a
real terminal would present after the engine's configured output processing.

Work:

- Add terminal attributes and window dimensions to the engine terminal object.
- Implement `isatty`, `tcgetattr`, `tcsetattr`, `tcflow`, and the required
  terminal `ioctl` requests, including `TIOCGWINSZ`/`TIOCSWINSZ`.
- Implement canonical and noncanonical reads, `ECHO`, `ICANON`, `ISIG`, input
  editing controls, `VMIN`, and `VTIME` to the supported POSIX boundary.
- Implement terminal output processing for terminal descriptors, beginning with
  `OPOST` and `ONLCR` (`LF` becomes `CR LF` in the normal cooked terminal
  mode). Keep output processing disabled or faithful to the configured flags in
  raw/full-screen modes; do not make the JavaScript model guess whether a lone
  LF should reset the column.
- Convert configured control characters into engine signals for the foreground
  process group. Deliver resize as SIGWINCH.
- Treat `VINTR` (Ctrl-C/ETX) and `VEOF` (Ctrl-D/EOT) as configured terminal
  controls. `VINTR` raises SIGINT for the foreground process group; `VEOF`
  completes canonical input according to POSIX without being delivered as a
  literal byte. Preserve wait/readiness behavior across mode changes and input
  arriving before or after a yield.

Gate:

- Native warnings-as-errors, ASan, and UBSan terminal tests cover canonical,
  raw, echo, EOF, interrupt, output `OPOST`/`ONLCR`, resize, readiness, and
  repeated yield/resume.
- Direct and threaded POSIX harnesses remain aligned where applicable.
- Browser Bash no longer relies on frontend line echo, Ctrl-C reaches the
  engine as SIGINT, Ctrl-D reaches the engine as VEOF/EOF, and the canvas/model
  agrees with the transcript for LF, CRLF, and cursor position.

Implementation update (2026-09-20):

- Added engine-owned `tcflow` validation and the versioned `tcflow_v1` host
  boundary. Raw reads now honor `VMIN` thresholds instead of returning partial
  input immediately, and terminal resize raises `SIGWINCH` in the kernel signal
  set.
- Extended the warnings-as-errors ASan/UBSan POSIX fixture with raw `VMIN`,
  `tcflow`, resize, and `SIGWINCH` checks: 259 tests pass. `VTIME` deadlines,
  foreground job control, and guest handler delivery remain for the next TTY
  slice.

Implementation update (2026-09-20, terminal byte-stream slice):

- Added stable `OPOST`/`ONLCR` termios flags and an engine-owned bounded output
  transformer. Interactive terminal setup enables cooked output processing;
  raw output leaves LF bytes unchanged.
- Routed browser terminal writes through that transformer before the host
  output callback, so the transcript and VT/WebGL model now receive the same
  CR/LF stream. Pipe and non-terminal writes retain their existing path.
- Corrected canonical `VEOF`: Ctrl-D releases pending input without adding a
  byte and produces EOF on the following read when the line is empty or
  exhausted. Ctrl-C continues to raise SIGINT through the configured `VINTR`
  byte rather than through a JavaScript signal shortcut.
- The sanitized native POSIX-kernel fixture now passes 269 tests. HTML
  regeneration, the normal browser smoke gate, command-not-found continuation,
  and executable-probe browser gates pass after the change.
- Remaining Stage 4 work is foreground job control and invoking guest handler
  functions at the engine delivery boundary; newline parity is no longer
  blocked on the renderer or transcript layer.

Implementation update (2026-09-20, VTIME slice):

- Added bounded noncanonical `VMIN`/`VTIME` deadlines using the kernel's
  monotonic clock and pointer-free wait record. `VMIN=0` reads return zero at
  timeout; `VMIN>0` reads return bytes accumulated before the timer expires.
- Preserved the existing yield/resume path: the deadline is polled by the
  engine wait record and the resumed read completes or returns its partial
  data without JavaScript-side timers or Asyncify.
- Added fake-clock coverage for zero-minimum and partial-byte timeout cases;
  the sanitized POSIX-kernel fixture now passes 289 tests.
- Regenerated the offline Bash page and reran normal, command-not-found, and
  executable-probe browser gates successfully. Remaining Stage 4 work is
  foreground job control and invoking guest handlers at delivery time.

Implementation update (2026-09-21, signal-disposition slice):

- Added per-kernel default, ignore, and caught signal dispositions. Ignored
  signals are discarded at raise time; caught signals remain pending for the
  engine's delivery boundary; default signals retain existing pending/wait
  behavior. SIGKILL and SIGSTOP reject non-default dispositions.
- Disposition state is copied with the process kernel capsule, keeping signal
  policy isolated per sandbox/process while preserving the existing signal
  mask and pending-set semantics.
- Added sanitizer coverage for disposition transitions, pending clearing,
  invalid dispositions, and the uncatchable signals. The signal fixture passes
  47 tests and the POSIX-kernel fixture passes 289 tests.
- Regenerated the offline Bash page and reran command-not-found and
  executable-probe browser gates successfully. The remaining guest-facing
  handler invocation and foreground process-group routing are intentionally not
  represented as JavaScript shortcuts.

Implementation update (2026-09-21, guest sigaction boundary):

- Replaced the previous no-op `env.sigaction` binding with a fixed-width guest
  ABI boundary. The handler pointer prefix is read from the guest action record,
  the previous handler pointer is returned through `oldact`, and disposition
  state is committed to the engine-owned kernel.
- Recognizes default, ignore (`0xfffffffe`, plus the conventional `1`), and
  caught handler pointers. Pointer ranges are validated before reading or
  writing guest memory; masks and flags remain untouched for compatibility
  with larger guest records.
- Added handler-pointer round-trip coverage to the sanitized signal fixture;
  it now passes 47 tests. Browser HTML regeneration and command-not-found and
  executable-probe gates remain green.
- The engine still records caught handlers as pending delivery; invoking a
  guest handler now occurs at the interrupted `pselect` engine boundary. The
  remaining ABI extension is full `sigaction` flags support.

Implementation update (2026-09-21, caught-handler delivery slice):

- The kernel records which unmasked signal interrupted a wait. The browser
  `pselect` boundary consumes that record and invokes a caught Wasm handler
  directly through the caller engine with the POSIX signal number, preserving
  `EINTR` for the interrupted call and avoiding JavaScript callbacks.
- Default and ignored signals retain their existing wait semantics. Handler
  failures do not replace the interrupted-call result; the host boundary
  preserves `EINTR` and remains pointer-free.
- Native signal and POSIX-kernel fixtures remain green (47 and 295 tests).
  Browser HTML regeneration and command-not-found/executable-probe gates stay
  green. Temporary handler masks and foreground process-group routing are now
  covered by the completed Stage 4 TTY slice.

Implementation update (2026-09-21, temporary handler-mask slice):

- Extended the fixed-width guest `sigaction` prefix with a 128-bit signal mask;
  `oldact` now returns both the previous handler and mask.
- The kernel stores per-signal action masks and, during caught-handler
  delivery, saves the current mask, applies the action mask plus the delivered
  signal itself, then restores the exact prior mask after the guest call.
- This keeps nested signal delivery deterministic and entirely engine-owned;
  no JavaScript timer or callback participates. Native signal/POSIX fixtures
  and all Bash browser gates remain green.

Implementation update (2026-09-21, foreground process-group slice):

- Added process-group identity to each kernel and a foreground process-group
  identity to the shared terminal open-file description. Implemented engine
  `getpgrp`, `setpgid`, `tcgetpgrp`, and `tcsetpgrp` boundaries with validation.
- `VINTR` now targets the terminal's foreground group. Signals raised while a
  different group owns the terminal remain queued on the shared terminal and
  are routed when the owning process next polls or waits, instead of being
  delivered to the input-enqueuing process by accident.
- Added native foreground-group routing coverage; the POSIX-kernel fixture now
  passes 296 tests. Browser HTML regeneration, smoke, command-not-found, and
  executable-probe gates remain green.

## Stage 5: Define the executable-image and wasm32 application ABI

Status: complete (startup, argument/environment, exit-status, and descriptor
assertions covered)

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
  now exercised by the executable matrix and probe path. Native executable
  images now retain bounded copies of PID, cwd, argv, and envp for the new
  image; guest libc startup-block materialization and utility-facing coverage
  remain open.

Implementation update (2026-09-21, native startup metadata slice):

- Extended `native_process_image` with owned startup metadata: process ID,
  cwd, bounded argv/envp vectors, and lifecycle-safe cleanup on image release
  or allocation failure.
- `native_store_instantiate_executable` copies the validated `execve` vectors
  before the old image can be replaced. The executable matrix now checks PID,
  cwd, argument, environment, and startup-hook preservation (37 checks, zero
  failures).
- Regenerated the browser Bash page after the ABI changes; normal,
  command-not-found, and executable-probe browser gates remain green. The
  remaining work is expanding the guest libc startup shim beyond the optional
  `__waste_startup` hook.

Implementation update (2026-09-21, startup ABI accessor):

- Added `waste_kernel.startup_v1() -> i32`, returning the active image's
  startup-block offset or `ENOENT` when no block exists. Only a wasm32 integer
  offset crosses the boundary; startup state remains engine-owned.
- Documented the accessor and block layout in `docs/wasm32-abi.md`, allowing a
  guest libc shim to construct `main(argc, argv, envp)` without JavaScript
  callbacks or host pointers.
- Browser HTML regeneration and the executable-probe and command-not-found
  gates remain green. The startup accessor is now consumed by the probe and
  the shared guest libc bridge.

Implementation update (2026-09-21, guest libc startup decoder):

- Added `__waste_startup_view` to the guest `unistd` layer. It imports
  `waste_kernel.startup_v1`, decodes the fixed-width block, and returns
  image-local argc/argv/envc/envp pointers without retaining host addresses.
- The wrapper is a no-op under `WASTE_ENGINE` builds and returns an error when
  no startup block is present, preserving the existing minimal probe ABI.
- Browser regeneration and executable-probe/command-not-found gates remain
  green; the libc amalgamation compiles the decoder successfully.

Implementation update (2026-09-21, callable CRT bridge):

- Added guest `__waste_startup_call(entry)`, which decodes the startup block
  and invokes an application-supplied `main(argc, argv, envp)` function pointer
  entirely within Wasm.
- This provides the CRT handoff without host callbacks, JavaScript re-entry,
  or Asyncify. Application-specific `_start` shims can now delegate to the
  shared libc helper while retaining normal table/call-indirect behavior.
- The full WASTE libc amalgamation builds successfully, and executable-probe
  and command-not-found browser gates remain green.

Implementation update (2026-09-21, startup-aware waste-probe):

- Extended `tests/c-engine-waste-probe.c` to consume `waste_kernel.startup_v1`
  directly, validate the startup block, and report argc/argv, envc, PID, and
  cwd before its deterministic success marker.
- The probe keeps its existing `env.write`-only output path and fails with a
  nonzero return when startup metadata is absent or malformed, so malformed
  image handoffs are observable without host inspection.
- The rebuilt probe and executable browser gate pass. The probe supports an
  environment-triggered `exit(7)` mode, and the dedicated
  `tests/c-engine-bash-browser-runtime.cjs --exec-exit` gate runs it through
  Bash, asserts `"$?" == 7`, verifies a later command executes, and then exits
  cleanly.

Implementation update (2026-09-21, browser descriptor slice):

- Extended the probe with the documented `env.fcntl` ABI and explicit
  `F_GETFD`/`F_SETFD` checks for descriptors 0, 1, and 2. It verifies the
  standard descriptors enter the executable image without `FD_CLOEXEC`, then
  exercises setting, reading, and clearing the flag before reporting
  `WASTE_PROBE_FDS_OK`.
- Added `WASTE_PROBE_FDS_OK` to the executable browser assertion and allowed
  only `env.fcntl` in the external-image import audit. The rebuilt probe,
  executable-probe, nonzero-exit, and command-not-found browser gates pass.

## Stage 6: Implement successful `execve` and process-image replacement

Status: complete (replacement, status, signal, descriptor, and continuation
boundaries covered)

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

Implementation update (2026-09-21, explicit exit-status slice):

- Replaced the browser `env.exit` no-op with an engine termination result. The
  requested status is normalized to the POSIX low byte and returned through
  `EXEC_ERROR_EXIT`, so the existing child zombie/wait and parent restoration
  path observes the real nonzero status.
- Ordinary `abort`/jump placeholders retain their previous behavior; only the
  explicit process-exit boundary changes. The browser build and executable
  probe remain green.
- Remaining Stage 6 work is signal termination and descriptor/CLOEXEC
  inheritance coverage.

Implementation update (2026-09-21, signal-routing slice):

- Replaced the browser `raise`, `kill`, and `killpg` no-op bindings with
  engine-owned routing. Signals target the active process, an explicit PID, or
  every process in a process group, and therefore participate in the existing
  disposition, pending, and wait-delivery paths.
- Invalid targets and signal numbers return the stable POSIX error result;
  ignored signals remain discarded and caught signals remain pending until
  the normal engine delivery boundary.
- Browser HTML regeneration, executable-probe, and command-not-found gates
  remain green. Descriptor/CLOEXEC inheritance is the next Stage 6 slice.

Implementation update (2026-09-21, descriptor close-on-exec slice):

- Added per-descriptor `FD_CLOEXEC` state to the engine kernel. `dup` and
  `dup2` clear the flag as required, forked kernels preserve descriptor state,
  and successful image commit closes marked descriptors before the new image
  runs.
- Implemented the guest `fcntl(F_GETFD/F_SETFD)` boundary for this flag and
  added native close-on-exec coverage. The POSIX-kernel fixture now passes 295
  tests; the executable transition matrix passes 37 checks with no failures.
- Browser HTML regeneration and executable-probe browser validation remain
  green. Broader descriptor inheritance and signal-termination status cases
  remain for the next Stage 6 slice.

Implementation update (2026-09-21, default signal termination slice):

- Active-process `raise(signal)` now consults the engine disposition. Ignored
  signals remain successful no-ops, caught signals remain pending for handler
  delivery, and default dispositions terminate the active invocation with the
  conventional `128 + signal` status (except stop/continue control signals).
- Termination still travels through `EXEC_ERROR_EXIT`, so the existing child
  zombie/reap and parent-continuation restoration logic remains the single
  status path.
- Browser HTML regeneration, executable-probe, and command-not-found gates
  remain green. Targeted cross-process signal status fixtures are still open.

Implementation update (2026-09-21, cross-process signal-status slice):

- Added process-level signal delivery for explicit `kill(pid, signal)`. The
  target kernel receives the signal disposition-aware; a default-disposition
  target becomes a zombie with the conventional `128 + signal` wait status,
  while ignored and caught targets remain runnable/pending as appropriate.
- Parent `waitpid` continues to reap the target through the existing process
  table, preserving the same status encoding used by direct `raise` and
  `exit`.
- The executable transition matrix remains at 37 checks with no failures, and
  the executable-probe browser gate remains green. Group-wide signal status
  coverage is the remaining Stage 6 signal slice.

Implementation update (2026-09-21, process-group signal status):

- `killpg` now uses the same disposition-aware process termination path as
  `kill`, covering every live process in the selected process group. Default
  dispositions produce reapable `128 + signal` statuses; ignored and caught
  dispositions remain nonfatal.
- Added native lifecycle coverage for a signaled group member: 25 checks pass
  with zero failures. Browser HTML regeneration and executable-probe
  validation remain green.
- Stage 6 signal-status plumbing is complete; remaining Stage 6 work is the
  broader descriptor inheritance matrix and VFS utility coverage.

Implementation update (2026-09-21, descriptor inheritance matrix slice):

- Extended the native descriptor fixture to verify unmarked descriptors remain
  usable after kernel cloning while `FD_CLOEXEC` descriptors are removed at
  image replacement. This covers the fork/exec boundary without exposing host
  descriptor pointers.
- The sanitized POSIX-kernel fixture now passes 298 tests. Executable matrix
  and browser executable-probe gates remain green.
- At this point the remaining work was the broader descriptor combinations and
  packaged VFS boundary; the descriptor portion is now covered and the VFS
  portion is tracked directly in Stage 7.

Implementation update (2026-09-21, descriptor duplication combinations):

- Added `dup2` replacement coverage: replacing a close-on-exec descriptor
  clears the flag, while independently marked descriptors remain eligible for
  closure during image replacement.
- The sanitized POSIX-kernel fixture now passes 298 tests. The executable
  transition matrix and browser executable-probe gate remain green.
- Descriptor inheritance coverage is complete for the current kernel boundary;
  packaged VFS utility coverage is now the active Stage 7 work.

Completion update (2026-09-21):

- Corrected the native CLI Makefile to pass absolute fixture paths when a test
  is launched from `src/cli-rt`; `make -C src/cli-rt exec-matrix
  BUILD_DIR=../../build/cli-rt` now completes with 37 checks and zero failures.
- Successful replacement, low-byte exit status, default and cross-process
  signal termination, parent restoration, `FD_CLOEXEC`, `dup`/`dup2`, and
  startup descriptor checks are covered by the native lifecycle/matrix tests
  and the Bash browser executable/exit gates. Stage 6 is complete; packaged
  files and utility path operations are isolated in Stage 7.

## Stage 7: Connect packaged files to the engine VFS

Status: in progress (boot metadata and regular-file data/open/write slice
complete; directory and utility operations remain)

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

Implementation update (2026-09-21, boot manifest metadata slice):

- Added bounded `waste_wast_stage_path` and `waste_wast_path_access` browser
  exports. The browser submits pointer-free path metadata before script start;
  the engine binds it after optional terminal-kernel replacement, preventing
  metadata loss when the interactive kernel is created.
- The Bash worker stages `/tmp`, `/usr/bin`, and `/bin/waste-probe`, then
  checks their existence/execute permission through the engine-owned pathname
  API and publishes a `vfs` readiness event. The browser harness requires this
  event in the normal, command-not-found, and executable-probe modes.
- Regenerated `build/html-rt/bash.html`; smoke, process-continuation,
  executable-probe, nonzero-exit, and command-not-found browser gates pass.
  The next slice must carry packaged file bytes, directory entries, and
  writable-overlay operations rather than metadata alone.

Implementation update (2026-09-21, regular-file data slice):

- Added engine-owned regular-file contents to pathname nodes and a bounded
  `posix_kernel_open` path. Read-only packaged files can be opened and read;
  `O_CREAT`, `O_TRUNC`, and `O_APPEND` support the initial writable `/tmp`
  overlay behavior, with file sizes reflected in path metadata.
- Added `waste_wast_stage_file(path, bytes, mode)` and changed the Bash worker
  to stage the probe's actual wasm bytes. Browser `open`, `read`, and `write`
  now route regular descriptors through the kernel while terminal descriptors
  retain their existing output callback and yield behavior.
- The native ASan/UBSan path-VFS fixture passes 33 checks. The browser C-engine
  build, normal Bash smoke, process continuation, executable probe, and
  command-not-found gates pass. The next slice must add tar-backed directory
  entries and explicit `readdir`/`stat`/`chdir`/symlink coverage.

Implementation update (2026-09-21, directory and pathname slice):

- Added engine-owned directory descriptors and pointer-free directory cursors;
  `open` on a directory plus `readdir_v1` now enumerate immediate children
  from the same path-node namespace used by regular files.
- Added real kernel `getcwd`/`chdir` behavior and guest-libc `opendir`,
  `readdir`, and `closedir` wrappers. Added bounded symlink targets with
  follow/non-follow stat behavior and loop protection.
- The native ASan/UBSan path-VFS fixture now passes 40 checks, including
  regular-file reads, `/tmp` creation, directory enumeration, cwd changes,
  and symlink metadata/target resolution. The guest libc amalgamation and
  browser C-engine build succeed; Bash smoke, continuation, and executable
  probe gates remain green. Tar extraction, explicit `lstat` dispatch, and
  durable writable-overlay semantics remain for the next Stage 7 slice.

Implementation update (2026-09-21, stat and writable-overlay slice):

- Split native `stat` and `lstat` dispatch so symlink metadata can be observed
  without following the target. Added engine-owned `mkdir`, `unlink`, and
  same-sandbox `rename` operations, including non-empty-directory and type
  mismatch errors.
- Extended the native ASan/UBSan path-VFS fixture to 45 checks covering
  directory creation, rename, deletion, and post-delete pathname errors.
  The browser engine rebuild and regenerated offline Bash smoke/continuation
  gates pass. Tar extraction and packaging the complete directory tree remain
  the final Stage 7 data-loading slice before Stage 8 can begin.

Implementation update (2026-09-21, embedded tar VFS slice):

- Retained tar entry metadata in the shared offline loader and passed packaged
  file blobs from the page into the Bash worker. The worker now stages the
  complete embedded archive under engine-owned paths, mapping the executable
  probe to `/bin/waste-probe` and support files below `/usr/share/waste`.
- Added packaged-path checks to the browser VFS readiness event. The
  self-contained `file://` Bash page exercises the same kernel VFS for
  packaged bytes, directory parents, executable lookup, and pathname
  operations; development/staging mode keeps its existing probe fallback.
- Regenerated the offline page. Native path-VFS coverage remains 45 checks
  with zero failures, and browser smoke, continuation, executable-probe, and
  command-not-found gates pass. Remaining Stage 7 work is limited to broader
  tar contents, utility fixtures, and persistence-policy documentation.

Implementation update (2026-09-21, utility pathname slice):

- Added engine-owned `readlink`, regular-file `lseek`, and `rmdir` paths and
  wired them through the guest libc/native import boundary. `readlink` keeps
  the target un-followed, `lseek` preserves the shared open-file offset, and
  `rmdir` rejects non-empty directories through the kernel namespace.
- The native ASan/UBSan path-VFS fixture now passes 50 checks, including
  symlink target reads, seek offsets, and directory removal. The regenerated
  offline C-engine Bash page passes its smoke and continuation gates.
- Stage 7 now has the core pathname/data-plane behavior needed by utility
  bring-up. Remaining work is utility-specific fixtures and documenting the
  deterministic restart/persistence policy before Stage 8 coreutils work.

Implementation update (2026-09-21, packaged utility regression slice):

- Extended the C-engine Bash browser fixture to pass packaged `launch.wast`
  and `waste-probe.wasm` through the same worker VFS staging protocol used by
  the generated offline page. VFS readiness now requires `/usr/share/waste/
  launch.wast`, preventing a probe-only staging path from masking incomplete
  package loading.
- The executable-probe browser gate passes with the packaged support file and
  probe present in the engine namespace. Each worker start still creates a
  fresh kernel and consumes a fresh bounded manifest; the embedded tar map is
  read-only page state, so shell restart does not persist `/tmp` mutations.
- Node syntax and diff checks remain clean. Stage 7’s remaining documentation
  task is to consolidate that restart/persistence rule before beginning the
  Stage 8 utility matrix.

Implementation update (2026-09-21, restart policy consolidation):

- Recorded the default VFS lifecycle in `docs/architecture.md`: every Bash
  worker start receives a fresh kernel and writable overlay, while packaged
  tar blobs remain immutable page state. Restart therefore discards `/tmp`,
  descriptors, cwd, and process state unless an explicit future mount backend
  is enabled.
- The packaged-support browser regression remains green and now requires
  `/usr/share/waste/launch.wast` in the VFS readiness message. Shell syntax,
  Node/Python checks, and `git diff --check` pass. Stage 7’s durable restart
  policy is documented; Stage 8 can proceed through its dedicated image-loader
  and coreutils plan.

## Stage 8: Add a reproducible GNU coreutils wasm32 build

Status: active (detailed plan written; implementation pending)

Stage 8 is tracked in
`docs/active-coreutils-wasm-loader-plan.md`. That plan assumes the pinned
`submodules/coreutils` gitlink already exists and makes the engine VFS the
source of executable bytes before bringing up utilities.

The Coreutils utility waves are currently paused at their shared-memory link
boundary while `docs/active-engine-virtual-memory-plan.md` replaces contiguous
engine memory with process-owned virtual pages. That prerequisite supplies the
single application/libc pointer space and the copy-on-write and shared backing
needed for the long-term POSIX `fork` and `mmap` model.

The stage now includes three related deliverables:

- a WASTE image loader that recognizes binary Wasm by magic and performs
  atomic `execve` replacement from an ordinary executable VFS file;
- executable WAT/WAST handling, including engine-owned `/bin/wat` and
  `/bin/wast` VFS utilities and normal `#!` dispatch. Shebang recognition is
  owned by the loader/interpreter boundary; the WAT/WAST parser may receive a
  shebang-stripped span or a narrowly scoped first-line mode, but must not gain
  general `#` comments; and
- a transactional coreutils build using
  `submodules/coreutils-waste.patch`, a reviewed target sysroot/configuration,
  per-utility import audits, incremental utility waves, offline packaging, and
  GPL source/provenance gates.

Gate:

- Browser Bash runs `/bin/true`, `/bin/false`, `/bin/pwd`, `/bin/cat`, and
  `/bin/ls` and receives correct exit statuses after each command.
- `ls /bin` reports the packaged executables from the engine VFS and the prompt
  remains usable afterward.
- Executable Wasm loads from VFS bytes without a required suffix; `.wat` and
  `.wast` fixtures execute directly and through `#!/bin/wat` and
  `#!/bin/wast`.
- Builds are incremental, reproducible, and do not silently accept new imports.

Implementation update (2026-09-21, ABI audit unblocker):

- Added `src/html-rt/tools/audit-wasm-imports.py`, a reproducible `wasm-dis`
  based audit that emits JSON, rejects imports outside an explicit
  `module:name` allowlist, and rejects Asyncify/unwind/rewind symbols.
- Audited the rebuilt `waste-libc.wasm` against the documented kernel/libc
  boundary: 15 unique imports, zero unknown imports, and zero Asyncify symbols.
  The report is generated at `build/html-rt/waste-libc/import-audit.json`.
- This removes the previously unmeasured libc import surface, but Stage 8
  now proceeds through the dedicated plan. Its first implementation slice is
  the provenance/patch transaction followed by the VFS-backed binary Wasm
  loader; the first `true`/`false`/`pwd` cross-build follows that loader gate.

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
- For Stage 1 changes, run the GLF cmap/range fixtures and headless pixel gate;
  confirm the WebGL path creates no font texture and performs no network fetch.
- Audit the engine and application Wasm imports/exports for Asyncify symbols.
- Run `bash -n start.sh`, Python bytecode checks for changed tools,
  `node --check` for changed Node/worker sources, and `git diff --check`.
- Update this document's stage status and durable `architecture.md` or
  `techniques.md` guidance when a stage is completed.

## Completion criteria

This plan is complete when the generated offline C-engine Bash page:

- Uses the rogue-wasm-derived GLF/Bézier WebGL terminal for normal
  presentation and per-key input, with no glyph texture atlas.
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
