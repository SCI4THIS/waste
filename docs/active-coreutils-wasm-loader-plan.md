# WASTE image loader and GNU coreutils plan

Status: active (binary/text loader, ten-utility Coreutils bring-up, and
distribution packaging complete; final command-matrix integration remains)

Parent stage: Stage 8 of `docs/active-c-engine-webgl-shell-plan.md`

## Objective

Make an executable regular file in the engine VFS the source of truth for
`execve`, load Wasm binary images directly from those file bytes, and ship an
initial GNU coreutils set in the offline Bash filesystem. Add `/bin/wat` and
`/bin/wast` as VFS-visible interpreter utilities so executable text modules and
test scripts can use normal shebang dispatch.

The completed path must remain inside the C engine and its guest libc. It must
not require JavaScript system-call implementations, Asyncify, JSPI, a server,
or an ELF compatibility layer.

This plan assumes `submodules/coreutils` is already a pinned git submodule.
Adding or cloning that submodule is outside this plan.

## Fixed decisions

### Executable formats

WASTE implements an executable-image loader; it does not describe Wasm files
as ELF files. Bash continues to perform command lookup and call `execve`.
`execve` resolves the final VFS node, checks permissions, reads a bounded file
snapshot, classifies it, validates the prospective image, and only then commits
process replacement.

Classification has this order:

| Input | Recognition | Behavior |
| --- | --- | --- |
| Interpreter script | First two bytes are `#!` | Resolve and execute the named interpreter through the same VFS loader. |
| Wasm binary | Bytes begin with `\0asm` | Decode, validate, link, instantiate, and enter `_start`. The filename suffix is irrelevant. |
| WAT source | Explicit `/bin/wat` use, `#!/bin/wat`, or executable `.wat` suffix | Strip an accepted shebang, parse exactly one text module, encode it, and use the common Wasm image path. |
| WAST source | Explicit `/bin/wast` use, `#!/bin/wast`, or executable `.wast` suffix | Run the WAST command stream in the child process and convert its result to a process exit status. |
| Other text or malformed input | No accepted classification | Fail with `ENOEXEC` without changing the old process image. |

Binary Wasm is identified by content, so `/bin/true`, `./program`, and
`./program.wasm` behave the same when they contain the same executable bytes.
The `.wat` and `.wast` suffixes are retained only to disambiguate the two text
grammars when there is no shebang or explicit interpreter. A WAST stream may
start with `(module ...)`, so content probing alone cannot distinguish it from
a single WAT module.

### Shebang behavior

The WASTE image loader, not Bash, owns shebang handling. The loader recognizes
the first line, resolves the interpreter, and constructs the interpreter
invocation. The selected interpreter then owns removing that line before
calling the language parser. For an executable file `script.wat` beginning
with:

```text
#!/bin/wat
```

the loader resolves `/bin/wat` and constructs the conventional argument list:

```text
argv[0] = "/bin/wat"
argv[1] = resolved script path
argv[2...] = original argv[1...]
```

Support one optional interpreter argument, an optional carriage return before
the line feed, a bounded first line, and a small fixed recursion limit. Empty,
relative, overlong, recursive, missing, and non-executable interpreter paths
must fail with stable errno values. `/bin/wat` and `/bin/wast` remove the
accepted shebang line before parsing because `#!` is not WAT or WAST syntax.

The parser boundary must make this explicit. Either the loader passes a source
span beginning after the shebang, or the WAT/WAST front end gains a narrowly
scoped `allow_shebang` input mode that recognizes `#!` only at byte offset zero
and consumes exactly one complete first line. It must not turn `#` into a
general WAT/WAST comment token. The chosen implementation must preserve the
original source path and line offset so diagnostics still identify the correct
line in the VFS file.

Do not rely on Bash's `ENOEXEC` shell-script fallback. Unknown text remains an
execution error instead of being reinterpreted as shell input.

### Text interpreter utilities

`/bin/wat` and `/bin/wast` are immutable executable nodes in the packaged VFS.
They are engine-owned executable handlers, represented explicitly in the
executable manifest rather than as JavaScript callbacks or ad hoc path-name
special cases. Extend process images with a bounded image-kind discriminator:

- `WASTE_IMAGE_WASM` for a normal instantiated application;
- `WASTE_IMAGE_WAT` for the one-module text compile-and-replace handler;
- `WASTE_IMAGE_WAST` for the WAST stream runner.

Invoking `wat FILE [ARG...]` replaces the interpreter child with the module
compiled from `FILE`; the module observes `FILE` as `argv[0]`. Invoking
`wast FILE` runs the script in the child and exits zero only when parsing,
validation, execution, and all assertions succeed. Diagnostics go to file
descriptor 2 and normal script output goes through the inherited descriptors.
Both handlers use the engine's existing WAT/WAST parser, encoder, linker, and
runner and may yield only through the existing explicit continuation path.

### Coreutils source and patches

All WASTE changes to GNU coreutils live in
`submodules/coreutils-waste.patch`. Do not commit changes in the coreutils
submodule or edit its history.

The build applies the patch as a transaction like the Wasm spec interpreter:

1. Require the submodule to match either the clean pinned revision or the
   exactly applied managed patch.
2. Check and apply `submodules/coreutils-waste.patch`.
3. Copy the patched source to a generated staging tree under `build/`.
4. Reverse the patch from the submodule through an `EXIT` trap on success,
   failure, or interruption.
5. Run bootstrap/configure/build only in the staging tree so generated files
   never dirty the submodule.

Provide a patch-status target that reports `available`, `applied`, or
`conflict`. A conflict or unrelated dirty state is a hard failure.

### Toolchain and target ABI

Follow the useful part of the zlib-wasm model: compile C with Clang for wasm32,
link with `wasm-ld`, and leave only documented guest-libc/kernel calls as
imports. Do not use `emcc`, Emscripten JavaScript libraries, WASI, or Asyncify.

Emscripten sysroot headers may seed the first configure experiment, but they
are not the WASTE ABI. The reproducible build must construct an explicit WASTE
sysroot and record the origin and license of every imported header. Prefer
repository-owned WASTE headers as each required surface stabilizes. Do not
define `__EMSCRIPTEN__` or accept Emscripten runtime imports merely to satisfy
feature detection.

No tool path may be hard-coded. Clang, `wasm-ld`, Binaryen/WABT tools, and any
temporary header source are supplied through documented command-line options
or narrowly named environment variables. Record tool versions in the build
report.

## Stage 8A: Establish provenance and the patch transaction

Status: complete (provenance, clean staging, and patch transaction tooling)

Work:

- Record the pinned coreutils commit, upstream release relationship, nested
  gnulib revision, and source licenses without changing the gitlink.
- Add the initially empty or first required
  `submodules/coreutils-waste.patch` and a transactional staging rule.
- Keep native build tools separate from target objects. If the git checkout
  requires `bootstrap`, run it in the generated staging copy.
- Capture `COPYING`, package notices, per-file exceptions, and relevant gnulib
  notices in the generated distribution manifest.
- Add a source-offer checklist stating where the exact pinned source, patch,
  configure answers, and build scripts accompanying a distributed Wasm image
  can be obtained.

Gate:

- Patch status is deterministic and an interrupted build leaves the submodule
  at its original revision and cleanliness.
- The source staging tree can be recreated without network access after all
  declared submodules and tool dependencies are present.
- The provenance report names the exact source and every license shipped with
  the selected utility set.

Implementation update (2026-09-21, provenance and staging tooling):

- Added `src/html-rt/tools/stage-coreutils.py` with `status`, `provenance`, and
  `stage` actions. It records the pinned coreutils commit, upstream remote,
  managed patch hash, nested submodule state, and hashes of discovered license
  and dependency notices.
- Added the repository-owned empty patch placeholder
  `submodules/coreutils-waste.patch`. The stager accepts the clean empty patch
  state now and will apply/reverse a nonempty patch transactionally through an
  exit-safe path once the first WASTE source change is needed.
- Added `coreutils-patch-status`, `coreutils-provenance`, and
  `coreutils-stage` targets to `src/html-rt/Makefile`. Provenance is generated
  at `build/html-rt/coreutils/provenance.json`; the generated directory is not
  a source of truth.
- The pinned source is coreutils commit
  `cecd945aa93ab77e759fe766206cfe93e634d07b` (`v9.12-17-gcecd945aa`). Its
  declared `gnulib` commit is `106e9b2384d08a1696fcbd40cbab52237943f208`, but
  that nested submodule is now initialized recursively and included in the
  generated staging tree.
- `coreutils-patch-status`, provenance generation, Python compilation, and
  `git diff --check` pass. `coreutils-stage` copies the complete source tree
  to `build/html-rt/coreutils/source`, writes its provenance report, and leaves
  both the coreutils and nested gnulib worktrees clean.
- The managed patch is still intentionally empty because no WASTE source
  adaptation has been selected yet. The stager's nonempty-patch path is ready
  to apply and reverse `submodules/coreutils-waste.patch` transactionally when
  Stage 8D/8E identifies the first required source change.

## Stage 8B: Make VFS bytes authoritative for `execve`

Status: complete (binary Wasm VFS snapshots and atomic image handoff)

Replace the current path-keyed executable byte registry with a loader whose
input is the regular file reached through the engine VFS. The registry may
remain as an immutable decoded-template cache, but it must not contain a second
copy of executable contents that can disagree with the VFS.

Work:

- Add a kernel operation that resolves a path with symlink-loop protection,
  verifies a regular executable node, and returns a bounded immutable byte
  snapshot plus inode, mode, and size.
- Resolve relative paths against the calling process cwd. PATH lookup remains
  Bash's responsibility; the `execve` boundary receives one path.
- If a validated-module cache is added later, key it by VFS object identity and
  content generation or content hash. Until then, fresh snapshots on every
  execution ensure writes, truncation, rename, and replacement cannot reuse a
  stale decoded image.
- Copy pathname, argv, and envp before examining the target. Decode, import
  audit, link, instantiate, create the startup block, and locate `_start`
  before committing the new image.
- Preserve PID, parent/process-group/session identity, cwd, environment,
  signal rules, and non-`FD_CLOEXEC` descriptors through the existing commit
  path.
- Return precise errors for missing paths, directory targets, permission
  failures, symlink loops, oversized files, malformed modules, unknown
  imports, missing entry points, and allocation failure. Every failure leaves
  the old image runnable exactly once.
- Keep a compatibility registration helper only for focused fixtures while
  migrating them to staged VFS files; remove it when no production path uses
  separate bytes.

Gate:

- Bash executes the same probe from an ordinary VFS file without calling the
  old executable-byte staging export.
- A binary file without a suffix executes by Wasm magic, and a `.wasm` file
  with invalid bytes fails `ENOEXEC`.
- Modifying an executable file invalidates any cached template; a failed
  replacement preserves the caller and correct shell status.
- Native ASan/UBSan lifecycle and VFS fixtures cover success, all principal
  errno paths, repeated execution, and cache invalidation.

Implementation update (2026-09-21, VFS executable snapshot):

- Added `posix_kernel_path_snapshot`, which resolves relative paths and
  symlinks in the engine-owned namespace, enforces regular-file and execute
  permission requirements, bounds the file size, and returns an owned byte
  snapshot plus metadata. The snapshot API does not expose kernel pointers to
  the loader or guest.
- Changed `native_store_instantiate_executable` to load binary image bytes
  from that VFS snapshot before decoding, linking, startup-block creation, and
  process-image commit. Each execution obtains a fresh snapshot, so current
  writable-file changes cannot reuse stale decoded bytes; no decoded-module
  cache has been introduced yet.
- Kept the executable registry only as a compatibility path for focused
  fixtures that have no VFS file. Registered fixtures now populate VFS data as
  well, so they exercise the same snapshot path. The browser probe remains
  compatible while packaged VFS files are authoritative when present.
- Added snapshot coverage for executable bytes, permission failures,
  directory targets, missing files, and size limits. The sanitized native path
  fixture passes 56 checks; the executable transition matrix passes 37 checks.
- Rebuilt the browser C engine. Normal Bash, command-not-found, and executable
  probe gates pass, including the probe bytes staged as an ordinary VFS file.

The remaining text classification, shebang dispatch, WAT encoding, and WAST
runner work is Stage 8C.

## Stage 8C: Add shebang, WAT, and WAST image handling

Status: complete

Work:

- Add a bounded, allocation-aware shebang parser before Wasm magic detection.
- Route the interpreter through normal VFS lookup and recursively through the
  same image loader, with a fixed recursion counter stored in the owned exec
  request.
- Add WAT source loading whose interpreter strips an accepted shebang, then
  parses one module, encodes it to Wasm, and uses the exact Stage 8B validation
  and image commit path.
- Add a WAST process-image runner whose interpreter strips an accepted shebang,
  consumes one bounded stream, reuses the existing command parser and runner,
  emits deterministic diagnostics, and exits nonzero on the first failed
  parse, assertion, validation, or runtime command.
- Decide whether shebang removal is implemented as a loader-produced source
  span or as an explicit parser mode. If the parser changes, add lexer/parser
  fixtures proving that only byte-zero `#!` is accepted, that ordinary `#`
  input remains invalid, and that source locations retain the skipped-line
  offset.
- Package executable `/bin/wat` and `/bin/wast` handler entries in the VFS.
- Support both explicit commands and executable source files:

  ```sh
  wat program.wat one two
  wast checks.wast
  ./program.wat one two
  ./checks.wast
  ```

- Keep WAST execution isolated in the Bash child process. It must not replace
  the parent shell's module registry, cwd, descriptors, or kernel state.

Gate:

- An executable `#!/bin/wat` file receives its expected argv and exit status,
  including when the module yields for terminal or file input.
- `/bin/wat FILE` and direct `FILE.wat` produce the same executable behavior.
- `/bin/wast FILE` and direct `FILE.wast` produce deterministic pass/fail
  status and diagnostics.
- Missing interpreters, shebang recursion, malformed text, multi-module WAT,
  failing WAST assertions, and oversized text fail without corrupting Bash.
- WAT/WAST parser tests cover shebang-at-byte-zero, CRLF, missing final LF,
  indented `#!`, and `#` appearing after the first line. Only the first two
  accepted bytes can select shebang handling.
- `ls -l /bin/wat /bin/wast` and ordinary VFS access observe both handlers as
  executable packaged files.

### Stage 8C WAST process-driver sub-plan

This is a contained architectural extension, not a parser or loader stub. The
existing exec driver assumes that every successful request produces one
`native_process_image` with one engine and entry function. A WAST executable
instead owns a command stream that may instantiate multiple modules and yield
between commands. It therefore needs a process-owned handler state alongside
the ordinary Wasm image state. Keep this work inside Stage 8C; Stage 8D must
not begin until the completion gate below passes.

1. Extract the common child-exec transition from the Wasm-image branch.
   Preparing the child, restoring the suspended Bash parent, committing the
   replacement execution state, and reporting transition failures must have
   one ordering for both Wasm images and WAST handlers. Do not return a WAST
   result directly from `EXEC_YIELD_EXEC`: that bypasses the parent's saved
   continuation and prevents Bash from observing the child status through
   `$?`.
2. Add a process-handler state owned by the child process capsule. It must tag
   the handler kind and own the WAST source bytes, `wast_stream` cursor,
   command-runner context, current status, and cleanup state. Checkpoint,
   process destruction, failed exec, and normal exit must each release this
   state exactly once. A capsule may own either a committed Wasm image or a
   handler state, never an ambiguous mixture of both.
3. Connect the loader handoff to the process driver. After successful WAST
   preflight, take `NATIVE_EXEC_HANDLER_WAST` with
   `native_exec_request_take_handler`, initialize the child handler, destroy
   the consumed request, and continue through the common exec transition.
   Invalid or missing handler payloads must fail atomically without changing
   the old image.
4. Drive one WAST command at a time with the existing stream parser and
   command runner. Preserve source lines and offsets, stop on the first parse,
   assertion, validation, linking, or runtime failure, and map success to exit
   status zero and failure to a deterministic nonzero status. The child's
   linked-module registry, cwd, descriptors, kernel state, diagnostics, and
   temporary definitions must remain isolated from the Bash parent.
5. Make handler execution resumable through ordinary engine returns. When a
   command produces `EXEC_YIELD`, retain both the evaluator continuation and
   WAST stream cursor in the child capsule, publish the existing terminal or
   select wait, and resume the same command through the exported browser API.
   Do not add Asyncify or represent this as stack unwinding.
   The browser adapter must normalize the cursor contract explicitly:
   `wast_stream.offset` is relative to the shebang-stripped body, while stream
   callbacks report offsets in the original source. It must not copy one into
   the other without applying the stored source offset, or a resumed handler
   can skip or repeat a command after a shebang.
6. Complete child exit and parent wakeup through the existing process path.
   Dispose the handler and child-only modules, record the encoded wait status,
   wake the Bash parent, resume its continuation, and verify that `$?` is the
   WAST result while the shell remains usable.
7. Replace the current unsupported-boundary probes with success/failure
   execution gates for `/bin/wast FILE`, direct `.wast`, and `#!/bin/wast`.
   Add assertion failure, malformed input, terminal/select yield and resume,
   repeated invocation, child cleanup, and parent-isolation cases. Retain a
   focused ownership test proving that the handler payload cannot be taken or
   freed twice.

Sub-plan completion gate:

- All three WAST entry forms execute and return deterministic status to Bash.
- A yielding WAST command resumes at the same command and then completes.
- Failed and successful WAST children leave the parent shell's process image,
  module registry, cwd, descriptors, kernel state, and subsequent commands
  unchanged.
- Sanitizer tests cover payload ownership, handler destruction, failed
  transitions, repeated execution, and yield/resume cleanup.
- The ordinary Wasm/WAT exec, fork, wait, terminal, and browser Bash gates
  continue to pass without a second process-lifecycle implementation.

Implementation update (2026-09-21, parser-side shebang boundary):

- Added `src/engine/source.c` and `src/engine/source.h` with a borrowed source
  view that recognizes `#!` only at byte offset zero, accepts CRLF and a
  missing final LF, captures one optional interpreter argument, and rejects
  empty, relative, overlong, or extra-argument shebang lines. It does not add
  a general `#` comment rule.
- Wired the view into `wast_parse_bytes`/`waste_wat_compile` and
  `wast_stream_init`, so native and browser parser entry points remove the
  interpreter line in the engine loader boundary. Stream callbacks retain the
  original source offsets and line numbers for diagnostics.
- Added focused parser coverage for `/bin/wat` with CRLF and an optional
  argument, `/bin/wast`, and rejection of an indented shebang. The full
  process-image replacement path, interpreter VFS nodes, WAST executable
  classification, and direct `.wast` dispatch remain to be implemented in the
  next 8C increment.
- Extended `native_store_instantiate_executable` to classify a VFS snapshot
  before loading it. Executable `.wat` files and files with `#!/bin/wat` are
  compiled through `waste_wat_compile`, then use the same Wasm loader and
  atomic process-image handoff as binary modules. Non-Wasm, non-WAT content is
  rejected before the existing image is replaced; WAST execution and the
  `/bin/wat`/`/bin/wast` handler nodes remain subsequent work.
- For a `#!/bin/wat` source image, the staged process startup vector now uses
  the interpreter convention `argv[0] = /bin/wat`, `argv[1] = script path`,
  followed by the original script arguments. These temporary argument strings
  are owned and released before the image is committed. The loader still
  needs explicit VFS interpreter-node resolution and recursion accounting
  before this is a complete recursive shebang implementation.
- Added engine-owned executable VFS nodes for `/bin/wat` and `/bin/wast`, and
  rebind them whenever the terminal kernel replaces the initial kernel. The
  browser worker now checks both paths with `X_OK` and reports them in its VFS
  inventory. They are deliberately empty handler nodes at this increment:
  invoking them still fails until the process-aware WAT/WAST handler images
  are implemented, so their presence is not being confused with utility
  execution support.
- Implemented the first functional handler operation for `/bin/wat`: an
  explicit `wat FILE [ARG...]` request snapshots `FILE` from the VFS, compiles
  it as WAT, and uses the normal Wasm loader and image commit path. The new
  image receives `FILE` as `argv[0]`, followed by the remaining user
  arguments. Missing source paths, empty handler requests, malformed WAT, and
  failed module loading all fail before replacing the caller image.
- Completed optional shebang-argument propagation for WAT scripts. For
  `#!/bin/wat --mode`, the startup vector is now `/bin/wat`, `--mode`, the
  script path, and the original script arguments, with the fixed argument
  bound enforced before allocation. Explicit `wat FILE ...` remains separate
  and does not inherit a script's shebang argument.
- Added `interpreter_depth` to the owned exec request and enforce a fixed
  four-level limit before compiling a shebang-selected WAT image. The loader
  also verifies `/bin/wat` with `X_OK` in the engine VFS before accepting the
  source image. The counter is now carried into the staged request; fully
  recursive interpreter replacement remains pending until non-WAT handlers
  are implemented.
- Added WAST loader preflight for `.wast`, `#!/bin/wast`, and explicit
  `/bin/wast FILE` requests. The VFS snapshot is passed through the shared
  WAST parser; malformed input fails as a format error, while syntactically
  valid input returns a distinct unsupported-handler result before any image
  commit. This establishes the parser/loader boundary without pretending a
  command stream can run as a Wasm `_start` image; the process-aware WAST
  driver remains the next implementation step.
- The loader preflight now has a distinct `EXEC_ERROR_UNSUPPORTED` boundary
  for syntactically valid WAST, while the existing focused exec matrix keeps
  parser stubs and remains unchanged in scope. The full WAST parser/runtime
  path is exercised through the browser engine build; the native parser
  fixture remains the place for parser-specific regression coverage.
- Added an end-to-end browser Bash probe for the explicit WAT handler. The
  fixture stages an executable `/tmp/wat-probe.wat`, runs
  `wat /tmp/wat-probe.wat`, verifies `$? == 0`, and proves the shell remains
  usable afterward. This confirms VFS staging, PATH lookup, `/bin/wat`
  dispatch, WAT compilation, startup, and return to Bash in one gate.
- Extended the browser gate with a direct shebang case: an executable
  `/tmp/wat-shebang.wat` beginning `#!/bin/wat --probe` runs by pathname,
  returns status zero, and leaves Bash usable. This covers loader shebang
  classification and optional interpreter-argument propagation in addition
  to explicit `wat FILE` dispatch.
- Added a failed-image browser gate for malformed WAT. Running an executable
  invalid source through `/bin/wat` yields the expected shell status `126`
  (`ENOEXEC`) and the next Bash command still runs, confirming that parser
  failure does not corrupt or replace the caller image.
- Added direct suffix coverage for an executable `.wat` file without a
  shebang. Invoking `/tmp/wat-direct.wat` by pathname succeeds with status
  zero and returns to Bash, completing the three WAT entry forms: explicit
  `/bin/wat FILE`, direct `.wat`, and `#!/bin/wat`.
- Added a browser preflight gate for valid WAST: `/bin/wast /tmp/checks.wast`
  reaches the parser-backed unsupported boundary, Bash observes status `126`,
  and a follow-up command succeeds. This confirms the VFS snapshot and WAST
  classification path while keeping actual command-stream execution visibly
  pending.
- Added direct WAST shebang coverage with an executable
  `/tmp/wast-shebang.wast` beginning `#!/bin/wast`. The loader reports the
  expected bad-interpreter/`ENOEXEC` status at the current unsupported-handler
  boundary, and Bash remains usable afterward.
- Added malformed-WAST coverage for `/bin/wast /tmp/bad.wast`. Parser failure
  maps to shell status `126`, while the follow-up Bash command succeeds;
  valid and invalid WAST now both stay on the preflight side of the atomic
  image boundary.
- Added native parser-fixture coverage for shebang stream coordinates: WAST
  command callbacks must report original-file line numbers and byte offsets
  after the stripped interpreter line. The sanitizer parser target remains
  subject to the existing generated-Flex rebuild timeout in this environment;
  the fixture is retained for the next successful native parser run.
- Added a fast `make -C src/cli-rt BUILD_DIR=../../build/cli-rt source-loader`
  gate with 10 ASan/UBSan checks for the shared source view: CRLF, missing
  final LF, optional arguments, byte-zero-only recognition, ordinary `#`,
  invalid interpreters, extra arguments, and overlong lines. This isolates
  loader contract regressions from generated Flex/Bison rebuild time.
- Added a bounded WAST handler payload to the owned exec request. After valid
  WAST preflight, the loader transfers the VFS snapshot with an explicit
  `NATIVE_EXEC_HANDLER_WAST` tag; request destruction frees it, including the
  unsupported-handler retry path. This is the handoff seam for the upcoming
  process-aware stream driver and does not execute WAST yet.
- Added `native_exec_request_take_handler`, the single ownership-transfer API
  for the future WAST process driver. It validates the handler tag, transfers
  the byte buffer exactly once, and clears the request metadata so the normal
  request destructor cannot double-free it. The browser driver does not call it
  yet; valid WAST still ends at `EXEC_ERROR_UNSUPPORTED` until the isolated
  command-stream process runner is implemented.
- Added exec-matrix coverage for the handler contract: a WAST payload can be
  taken once, its tag and size are preserved, and a second take is rejected.
  The next process-driver increment must consume this payload inside the
  existing child transition and restore the suspended Bash parent before
  propagating the WAST exit status; returning directly from the exec yield
  would bypass Bash's continuation and lose `$?`.
- Added direct `.wast` pathname coverage with `/tmp/checks-direct.wast`.
  Valid source reaches the parser-backed unsupported result, returns shell
  status `126`, and leaves Bash usable. WAST now has explicit interpreter,
  direct suffix, and shebang entry probes, matching the WAT matrix while the
  actual process-stream runner remains pending.
- Began the WAST process-driver sub-plan by extracting the browser's forked
  child-to-parent restoration into one shared helper. Wasm-image exec now uses
  the same ordering that a future WAST handler must use: restore the suspended
  Bash continuation, resume the parent with the fork result, mark the parent
  restored, and only then commit or finish child execution. Existing WAT,
  fork, and executable Bash gates remain behaviorally unchanged.
- Added `native_process_handler` state to the engine-owned process capsule.
  It owns a handler kind, source bytes, stream offset/line cursor, execution
  status, exit code, and optional context destructor independently of
  `native_process_image`. Handler installation transfers source ownership;
  capsule destruction releases it exactly once, and fork cloning duplicates
  the source/cursor state when no opaque context is present. The WAST loader
  has not been connected to this slot yet; that remains the next handoff step.
- Added `native_store_commit_process_handler`, an atomic exec-transition
  handoff for tagged WAST payloads. It takes the request-owned bytes, installs
  them in the active capsule, closes close-on-exec descriptors, releases the
  old image, clears the pending transition, and leaves request destruction
  safe. The exec matrix now covers the image-to-handler replacement contract;
  the browser driver still needs to invoke this operation and drive the
  handler cursor.
- Added `native_process_capsule_attach_handler_context` so a runtime-specific
  stream object can be attached only after the engine owns the handler bytes.
  Capsule cleanup owns the attached context destructor as well as the source
  buffer. The browser driver is intentionally still on the preflight boundary
  until handler execution and parent-continuation resumption are connected as
  one transition; the WAST probes therefore remain expected status `126`.
- Extended the process lifecycle sanitizer to attach an opaque handler context
  to a forked child and verify that reaping the child invokes its destructor
  exactly once. This validates the cleanup half of the stream-driver contract
  without enabling the still-unresolved browser continuation path.
- A bounded browser-driver trial was not retained because valid WAST reached
  the handler but parent resumption returned `function index out of range`.
  The preflight boundary remains intact. The next continuation increment must
  preserve and select the parent capsule's engine/function pair explicitly
  across handler completion before enabling live WAST execution.
- Updated `browser_restore_fork_parent` to take the continuation's
  `owner_engine`, `root_func_idx`, and root arguments as the authoritative
  parent execution pair, then copy them back into the process capsule after
  resume. Existing WAT, executable, browser-build, and process-lifecycle
  gates pass; the live WAST handler remains disabled until this fix is
  exercised by a dedicated continuation regression.
- The existing direct WAT, shebang-WAT, and executable Bash probes now serve
  as the continuation regression: each performs a child exec, returns to the
  original Bash parent, and executes the follow-up command successfully after
  the explicit engine/function-pair synchronization. No WAST probe has been
  promoted past status `126` yet.
- A second bounded WAST-driver trial still reports `function index out of
  range` after the handler handoff, even with the synchronized parent pair.
  That proves the remaining defect is in the handler-to-parent transition
  itself, not merely stale cached root metadata. The trial was removed and the
  preflight behavior restored; the next increment must add an instrumented
  process-transition regression before another browser integration attempt.
- Hardened `native_store_commit_process_handler` as that transition
  regression's first invariant: it now retires the old child evaluator when
  it is not owned by a process image, clears the cached root arguments, and
  leaves the capsule with only handler state. The native exec matrix remains
  clean at 41 checks, the process lifecycle sanitizer at 30 checks, and the
  browser WAST preflight remains unchanged while the deeper parent-transition
  instrumentation is still pending.
- Added `native_process_capsule_select_entry` as the single engine-owned
  operation for selecting a resumed process's engine, function, and root
  arguments. Browser parent-restoration and child-wakeup paths now use this
  operation instead of writing capsule entry fields directly; the lifecycle
  sanitizer covers rejection of a missing engine. This centralizes the
  transition invariant, but does not yet enable live WAST execution.
- Hardened the handler handoff's atomic-failure boundary. A missing payload or
  duplicate handler now fails before `native_exec_request_take_handler` can
  consume ownership, leaving the pending transition, request bytes, and active
  handler unchanged. The exec-transition regression covers both cases at 44
  checks; the process lifecycle sanitizer remains clean at 30 checks. This
  closes a process-state ownership bug in the handoff, while the actual WAST
  command driver and parent wakeup are still pending.
- Added `native_store_complete_process_handler` as the next engine-owned
  transition seam. It records the handler status and exit code, releases the
  source/context exactly once, and only then enters the existing child-exit
  path; attempts to complete PID 1 or a process without a handler leave state
  untouched. Lifecycle coverage exercises trap completion, context cleanup,
  zombie formation, and later reaping. The command-stream driver still needs
  to call this seam after its final command and wake the suspended Bash parent.
- Added bounded handler-cursor APIs for the future command driver. The driver
  can borrow the owned source and current offset/line, then commit only a
  monotonic, in-bounds cursor advance; backward, out-of-range, and invalid-line
  updates are rejected without changing the capsule. Lifecycle sanitizer
  coverage is now 34 checks, while the exec-transition matrix remains 44
  checks. This establishes resumable source positioning without pretending
  that command parsing or yield/resume execution is complete.
- Extended the lifecycle regression to prove cursor isolation across fork:
  advancing the child handler's offset and line leaves the parent's copied
  source position unchanged. This is the state boundary a yielded WAST
  command will rely on when the child resumes; lifecycle coverage is now 35
  checks, with command parsing and actual yield/resume still pending.
- Added explicit handler suspend/resume APIs for ordinary host waits. A
  handler may enter browser- or wait-blocked state only while runnable, and
  resumption returns it to runnable state without changing its source cursor.
  Lifecycle coverage now exercises cursor-preserving yield/resume at 37
  checks. This defines the process boundary for a future command runner but
  does not yet connect a WAST command callback or terminal/select import.
- Hardened the yield boundary so a blocked handler cannot complete or become a
  zombie until it is explicitly resumed. The rejected completion preserves
  both handler ownership and cursor state; lifecycle coverage now reaches 38
  checks, and the command driver remains the next integration step.
- Extended the same regression across both supported blocked states: a handler
  can suspend and resume through browser-blocked and wait-blocked transitions
  while retaining its cursor. Lifecycle coverage is now 40 checks. These are
  the state-machine primitives for terminal/select yields; no WAST command has
  been connected to either wait source yet.
- Added `native_process_capsule_run_handler_step`, a callback seam for the
  parser/command runner. It passes the owned source and cursor to one step,
  preserves the cursor when the step yields, and commits only a validated
  monotonic advance on success; failed or malformed advances are not committed.
  Lifecycle coverage now reaches 42 checks. The callback is runner-neutral:
  WAST parsing, module instantiation, and wait-source wiring remain pending.
- Added the callback seam's failure-side regression: a runner that proposes a
  backward cursor after a completed step receives `EXEC_ERROR_FORMAT`, while
  the last committed offset and line remain intact. Lifecycle coverage is now
  43 checks, closing the cursor-commit contract before parser integration.
- Recorded each handler-step result in the process-owned handler: yields,
  successful command completion, and rejected cursor advances now leave an
  explicit `exec_status` for the eventual driver to consume. Lifecycle tests
  verify all three statuses; coverage remains sanitizer-clean at 43 checks.
- Added deterministic handler-result exit mapping through
  `native_store_complete_process_handler_default`: success returns 0,
  format/unsupported/not-found results return 126, other failures return 127,
  and explicit guest exits retain their recorded code. The lifecycle gate now
  completes a format failure through this mapping and verifies wait status
  126; a pure mapping regression covers success, shell failures, generic
  traps, and explicit exits. The actual WAST command runner still must supply
  the result.
- Added a validated handler exit-code setter for command runners that observe
  an explicit guest `(exit ...)`. The value is constrained to one shell exit
  byte and is process-owned: lifecycle coverage confirms a child can change
  its code without mutating the parent's copied handler state. Default shell
  failure mapping remains separate from explicit guest exit handling.
- Added the invalid-input regression for that setter: values above 255 are
  rejected and cannot overwrite the previously recorded guest exit code.
  Negative values are rejected by the same boundary; lifecycle coverage now
  stands at 47 checks, completing the exit-code input contract before
  command-runner integration.
- Added the callback seam's direct execution-failure regression: a runner
  returning `EXEC_ERROR_TRAP` records that status but cannot commit its
  proposed cursor. Lifecycle coverage is now 48 checks, completing the
  success, yield, malformed-advance, and direct-failure outcomes for one
  handler step.
- Added the blocked-dispatch regression: a suspended handler rejects a command
  step before invoking its callback, preserving both the cursor and callback
  count until explicit resumption. Lifecycle coverage now stands at 50 checks,
  preventing duplicate command execution across a host wake boundary.
- Completed the handler state-transition matrix for repeated operations:
  blocked handlers reject a second suspend, and runnable handlers reject a
  duplicate resume without changing state. Lifecycle coverage now reaches 52
  checks; the remaining integration work is connecting these guarded states to
  real WAST parser and POSIX wait events.
- Extended fork isolation coverage beyond the cursor: a child handler's
  yielded/trapped status and explicit exit code remain private to the child,
  while the parent's handler retains its initial result state. Lifecycle
  coverage now reaches 53 checks, preserving the parent-isolation requirement
  for eventual WAST completion and wakeup.
- Added the first explicit handler-to-parent transition regression: handler
  completion creates the child exit state, `native_store_wake_process` selects
  the parent and queues the child PID, and wake completion clears the pending
  transition before reaping. This isolates the ordering required by the live
  browser driver; the lifecycle gate now covers the complete store-side path.
- Covered the wakeup edge cases as well: zombie children reject wake requests,
  and parents reject duplicate wakes while a wake transition is pending. This
  leaves the store-side child-exit contract deterministic for the browser
  continuation integration.
- Made the transition regression mirror the browser state boundary: the parent
  is explicitly browser-blocked before wake, and wake must restore it to
  runnable while preserving the queued child PID. This confirms the final
  store-side state change needed before continuation resumption.
- Centralized wake-result consumption in `native_store_take_process_wake` and
  routed the fork POSIX import through it. The API atomically returns the child
  PID, clears the pending result, and completes the wake transition; lifecycle
  coverage now exercises this consume point instead of directly clearing
  capsule fields.
- Added the repeated-consumption regression: once the wake result is taken,
  another take returns `EINVAL` and leaves the parent transition complete. This
  closes the wake-result ownership contract before browser continuation code
  consumes it.
- Added null-destination validation for wake consumption; invalid callers are
  rejected before the pending child PID is consumed. The lifecycle gate now
  covers the complete wake-result argument and ownership contract.
- A regression attempt to require the browser fork import to see an explicit
  `NATIVE_PROCESS_TRANSITION_WAKE` exposed an existing ordering dependency:
  the browser can publish a pending fork result before labeling the capsule's
  transition as `WAKE`. The centralized consume API therefore preserves the
  prior pending-result contract and only clears the transition when it is
  explicitly `WAKE`; stricter validation remains deferred to continuation
  instrumentation.
- Rebuilt the browser artifact and reran the WAT, executable, and WAST Bash
  probes after that compatibility adjustment. WAT and binary exec returned to
  status 0 with a later prompt; WAST remains the expected status-126 preflight
  and Bash remains usable. The process lifecycle sanitizer remains clean at
  60 checks.
- Reran the complete text-entry regression matrix: shebang, direct, and
  malformed WAT all pass with the expected 0/126 statuses; shebang, direct,
  and malformed WAST all remain stable at the documented 126 preflight
  boundary with a later Bash prompt. Each browser probe reports 5/5 checks.
- Added a regression for the browser ordering dependency: wake-result
  consumption clears the pending result even when the transition label is
  still `EXEC`, but leaves that unrelated transition label untouched. This
  preserves ownership between the fork import and the continuation transition
  while keeping the consume operation atomic.
- Re-ran the native ownership and transition gates after the wake API changes:
  the exec-transition matrix passes 44/44, the source-loader gate passes 10/10,
  and the process lifecycle sanitizer passes 61/61. This confirms the loader
  and handler seams remain stable while browser continuation integration stays
  isolated.
- Ran the complete browser regression gate: baseline Bash, binary exec, all
  WAT entry/failure forms, and all WAST preflight forms pass 5/5 each; `bash
  -n start.sh`, Node syntax validation, and `git diff --check` also pass. WAT
  remains executable, while WAST remains intentionally preflight-only.
- Ran the native i32 executor smoke gate successfully. The generic browser
  dashboard harness was also attempted, but `build/html-rt/test.html` is not
  present in this workspace, so that harness could not extract its generated
  payload; no browser runtime assertion was made from that unavailable input.
- Attempted to generate the missing dashboard with `./start.sh --html-test`.
  Guest fixtures, the native engine, and the browser engine built, but HTML
  amalgamation stopped because the repository is missing
  `src/html-rt/src/tests/terminal/model.js`. The dashboard harness therefore
  remains blocked by a missing test asset, not by a loader or process-runtime
  assertion.
- Fixed the dashboard asset boundary in `src/html-rt/tools/amalgamate.py`:
  when the test staging directory has no terminal copy, amalgamation now uses
  the canonical `src/html-rt/src/bash/terminal` sources. `./start.sh
  --html-test` now completes and produces `build/html-rt/test.html`; the Bash
  page also regenerates and passes both its smoke and continuation checks.
  Running the full staged browser payload reaches the complete suite, with the
  existing eight unrelated engine/libc conformance failures (bulk-memory,
  element/table initialization, one memory64 table import, and libc boundary
  expectations) recorded by the harness. The loader-specific WAT/WAST and
  Bash continuation probes remain passing, so the dashboard staging blocker is
  resolved without reopening the 8C WAST continuation design.
- Hardened the pending WAST handoff seam in `native_store_commit_process_handler`:
  replacing a prior Wasm image now releases the image through its existing
  reference-counted owner exactly once. The former post-release engine check
  could inspect freed image storage and double-release the engine when live
  handler execution was eventually wired. The full native CLI build and the
  ASan/UBSan process-lifecycle gate still pass (61/61); this is an ownership
  correction at the handler transition boundary, not completion of the live
  browser WAST driver.
- Extended the ASan/UBSan exec-transition matrix to commit a WAST handler over
  an owned prior image, exercising the corrected release path rather than only
  testing an image-less handler. The matrix passes 44/44, confirming that the
  handler handoff is now sanitizer-covered while the browser continuation
  driver remains the next architectural increment.
- Corrected the parent continuation restore to resume through
  `continuation->owner_engine`, matching the engine used for the authoritative
  saved function index, before reselecting that entry in the parent capsule.
  The previous call used the capsule's possibly stale engine pointer and could
  produce the observed `function index out of range` failure after child
  handoff. Native exec lifecycle (22/22), process lifecycle (61/61), and the
  browser WAT entry matrix (four probes, 5/5 each) pass after the change. The
  WAST browser probe remains intentionally at the 126 preflight boundary; a
  live handler retry is still gated on explicit handler-driver integration.
- Retried a bounded synchronous WAST handler bridge after that correction. The
  child reached handler commit and the existing WAST command callback, but
  completion still returned `function index out of range` while resuming the
  Bash parent. The bridge was removed after the browser probe failed, and the
  established WAST preflight behavior was reverified at 5/5 with smoke and
  continuation checks passing. This isolates the next task to tracing the
  parent capsule's post-handler entry selection; it is not safe to promote
  WAST execution until that trace is covered by a dedicated regression.
- Added a bounded process-entry invariant before every browser invocation. It
  reports the selected function index and the active engine's import/function
  range before the executor can emit its generic out-of-range error. This is
  diagnostic hardening for the next parent-capsule trace, not a behavior change
  to successful execution. Rebuilding the Bash page and rerunning the WAST
  preflight probe still pass 5/5, with live handler execution deliberately
  disabled until the invariant is exercised by the dedicated retry.
- Tightened the same invariant at the engine-owned capsule boundary:
  `native_process_capsule_select_entry` now rejects a function index outside
  the selected engine's imported-plus-defined function range before mutating
  the capsule. The ASan/UBSan process-lifecycle gate remains 61/61 and the
  exec-transition matrix remains 44/44. This converts stale parent-entry
  selection into an atomic transition error and leaves the remaining live-WAST
  work focused on identifying why the handler completion path supplies that
  stale pair.
- Added lifecycle regression coverage for the capsule guard: an out-of-range
  function index is rejected without changing the capsule, while a valid index
  is accepted and recorded. The ASan/UBSan process-lifecycle gate now passes
  63/63, and the exec-transition matrix remains 44/44.
- Applied the same atomic entry validation to ordinary Wasm image commits:
  `native_store_commit_process_image` now rejects missing engines, invalid
  entry indices, or a capsule still occupied by a handler before closing
  descriptors or replacing the old image. The exec-transition matrix covers
  the invalid-image case and passes 45/45 under ASan/UBSan; process lifecycle
  remains 63/63.
- Added the inverse ownership regression: an ordinary image commit is rejected
  while a WAST handler is active, and the handler source remains intact. The
  exec-transition matrix now passes 46/46 under ASan/UBSan, with the process
  lifecycle gate still at 63/63.
- Added a resumability regression for a handler command that consumes input
  before yielding. The cursor advances to the command's returned offset/line,
  the handler remains marked `EXEC_YIELD`, and cleanup remains owned by the
  capsule. The ASan/UBSan process-lifecycle gate now passes 64/64; this defines
  the cursor contract needed when the live WAST driver eventually persists a
  command continuation across a browser wait.
- Extended handler completion coverage to verify the parent-visible result is
  published before cleanup: `pending_result` carries the mapped shell status,
  `pending_error` preserves the execution status, and the child stores the
  corresponding POSIX wait encoding. The process-lifecycle sanitizer remains
  clean at 64/64.
- Made explicit handler completion reject exit codes outside the shell byte
  range before publishing any pending result or clearing handler state. The
  new regression passes atomically, with the process-lifecycle ASan/UBSan gate
  at 65/65 and the exec-transition matrix at 46/46.
- Added a pending-result guard to handler completion. A handler now refuses to
  overwrite an already queued wake/result and leaves its source, status, and
  pending value unchanged. The process-lifecycle sanitizer passes 66/66 and
  the exec-transition matrix remains 46/46.
- Extended pending-result ownership to both exec commit paths. Wasm image and
  WAST handler commits now reject a capsule with a queued wake before touching
  descriptors, images, or handler bytes. The exec-transition matrix covers
  the handler case and passes 47/47 under ASan/UBSan; process lifecycle remains
  66/66.
- Required an explicit pending `EXEC` transition for ordinary image commits,
  matching the existing WAST handler commit contract. Direct image replacement
  outside the exec protocol is now rejected before any process state changes;
  the exec-transition matrix remains 47/47 and process lifecycle remains
  66/66 under ASan/UBSan.
- Added a direct valid-image regression for that protocol guard. A well-formed
  image presented without a pending `EXEC` is rejected atomically, leaving the
  active capsule untouched. The ASan/UBSan exec-transition matrix now passes
  48/48.
- Hardened `native_store_prepare_process_exec` against both queued results and
  in-flight transitions. It now refuses to overwrite either state before the
  request reaches an image or handler commit. The lifecycle gate passes 67/67
  and the exec-transition matrix remains 48/48 under ASan/UBSan.
- Added duplicate-preparation coverage: a second exec preparation returns
  `EBUSY` and leaves the original `EXEC` transition intact. The process
  lifecycle ASan/UBSan gate now passes 68/68.
- Widened imported-plus-defined function-count arithmetic in capsule and image
  entry validation to prevent 32-bit count addition from wrapping. An
  overflow-shaped engine fixture now passes the lifecycle guard; process
  lifecycle is 69/69 and the exec-transition matrix remains 48/48 under
  ASan/UBSan.
- Applied the executable-size bound to WAST handler installation and commit.
  Oversized handler requests are rejected before ownership transfer, preserving
  the request for cleanup or retry. The ASan/UBSan exec-transition matrix now
  passes 49/49; process lifecycle remains 69/69.
- Added direct capsule-installer coverage for the same bound. An oversized
  source is rejected without installing a handler or consuming caller-owned
  memory. The process-lifecycle ASan/UBSan gate now passes 70/70.
- Extended the size guard to `native_exec_request_take_handler`, so oversized
  payloads cannot be transferred out of the exec request before validation.
  Direct transfer regression coverage passes, and the ASan/UBSan
  exec-transition matrix now passes 50/50.
- Restricted capsule handler installation to the implemented
  `NATIVE_PROCESS_HANDLER_WAST` kind. Unknown nonzero enum values are now
  rejected before source ownership changes; the process-lifecycle ASan/UBSan
  gate passes 71/71.
- Added request-identity coverage: `native_store_prepare_process_exec` rejects
  a request naming another PID without creating a transition. The process
  lifecycle ASan/UBSan gate now passes 72/72.
- Rebuilt the self-contained browser Bash artifact after the process guards.
  The smoke test, continuation test, direct/shebang/direct-path/malformed WAT
  probes, and WAST preflight probe all pass 5/5. This confirms the guarded
  process transitions remain compatible with the browser runtime while live
  WAST handler execution is still intentionally deferred.
- Regenerated the complete offline C-engine dashboard with `./start.sh
  --html-test`; amalgamation now succeeds and produces `build/html-rt/test.html`.
  A focused browser payload gate covering DIY POSIX kernel/bulk operations and
  libc path, select, and stat fixtures passes 5/5. The full dashboard remains
  subject to the previously recorded unrelated conformance failures.
- Updated `tests/c-engine-browser-runtime.cjs` to recognize the current
  manifest-backed dashboard format. When legacy embedded payload markers are
  absent, the harness now uses the generated staging payload and engine bytes
  while preserving the same worker/test execution path. The HTML-path harness
  reaches the focused five-test gate successfully; Node syntax validation and
  `git diff --check` pass.
- Aligned the browser process-entry diagnostic with the engine-owned widened
  function-count validation, avoiding 32-bit count wrap in the browser guard.
  The self-contained Bash artifact rebuilds cleanly; WAT and WAST preflight
  probes pass 5/5, Node syntax validation passes, and `git diff --check` is
  clean.
- Ran the consolidated native loader checks after the browser rebuild: source
  loader remains 10/10 and the POSIX path VFS gate passes 56/56 under
  ASan/UBSan. The combined parser-reentrant make target did not produce a
  completion result after generated-parser compilation and was interrupted;
  no parser pass is claimed from that attempt.
- Retried `parser-reentrant` independently after the generated objects were
  cached. The sanitizer lexer compilation again produced no completion result
  and had to be interrupted; process inspection showed no surviving compiler
  process afterward. This remains a verification/build-resource issue, not a
  parser behavior result, so the parser-reentrant gate stays open and Stage 8C
  is not marked complete.
- Adjusted only the test-target compilation of the generated Flex scanner from
  `-O1` to `-O0`; sanitizer coverage is unchanged, while the previous
  optimizer-heavy translation exceeded two minutes before the parser test was
  reached. `make -C src/cli-rt BUILD_DIR=../../build/cli-rt parser-reentrant`
  now completes and reports concurrent parser isolation passed, closing this
  verification sub-gate. Stage 8C remains open only for the WAST process-driver
  execution path and its browser continuation gate.
- Rechecked the lightweight process-lifecycle sanitizer gate after isolating
  the parser dependency boundary: all 72 lifecycle checks pass. The next
  handler increment remains the browser-side stream adapter, including the
  explicit relative-body versus original-source offset normalization recorded
  in the process-driver sub-plan.
- Added `wast_stream_position`, an engine-owned cursor conversion that returns
  original-source offsets and current source lines while preserving the
  scanner's shebang-stripped internal representation. The ASan/UBSan
  `parser-reentrant` gate passes with a shebang cursor regression covering the
  new API; the browser handler still needs to consume this API during live
  WAST execution.
- Attempted the browser-side handler handoff using the new cursor API. The
  browser artifact linked, but the valid-WAST Bash probe did not return a
  prompt, so that incomplete handoff was removed rather than weakening the
  existing gate. The Bash smoke and process-continuation checks pass, and the
  WAST unsupported-boundary probe remains 5/5 with status 126. The next
  increment must instrument child completion/parent wake ordering before
  retaining live handler execution.
- Tightened the process lifecycle regression to assert that WAST handler
  completion publishes the child exit status and `EXIT` transition while the
  parent remains unwoken; the explicit wake transition is still required and
  remains separately validated. The ASan/UBSan lifecycle gate passes all 72
  checks, confirming the ordering needed by the future browser continuation
  adapter.
- Added `native_store_complete_process_handler_and_wake[_default]`, which
  validates the live parent relationship and rejects an occupied parent wake
  slot before completing the handler. It then publishes the child exit and
  queues the parent wake as one checked transition. The lifecycle sanitizer
  gate now passes 73 checks, including exactly-once wake consumption and the
  reapable-zombie state; browser integration still needs to adopt this helper.
- Added the negative ownership/order regression: a queued parent result makes
  the combined completion helper return `EBUSY`, while the child handler and
  live child remain intact. The ASan/UBSan lifecycle gate now passes 74 checks;
  this closes the helper's atomic rejection case before browser adoption.
- Rebuilt the self-contained browser Bash artifact after the completion/wake
  changes. The Bash smoke and process-continuation gates both pass, confirming
  that ordinary Wasm exec, fork, wait, terminal, and parent continuation paths
  remain unchanged. Valid WAST handler execution is still intentionally not
  enabled in the browser until the adapter consumes the combined helper.
- Extended the combined completion/wake regression to cover a zombie or
  otherwise disappeared parent. The helper returns `EBUSY` before changing
  the child handler or exit state; the ASan/UBSan lifecycle gate now passes 75
  checks. This closes the parent-liveness rejection case for browser handler
  adoption.
- Added explicit handler status mapping coverage: success maps to zero,
  format/unsupported failures map to 126, explicit exits preserve their
  byte-range code, and traps map to 127. The lifecycle sanitizer gate now
  passes 76 checks, defining the status contract the browser adapter must
  expose to Bash.
- Exercised the explicit-exit branch through the combined completion/wake
  helper itself: a handler exit of 9 is published, wakes the parent, and is
  later reaped as `(9 << 8)`. The 76-check ASan/UBSan lifecycle gate remains
  green, so nonzero WAST completion is now covered at the transition boundary.
- Added exactly-once completion coverage: after the handler wake is consumed,
  a second completion/wake attempt returns `EINVAL` and leaves the parent
  runnable with no queued result. The lifecycle sanitizer gate now passes 77
  checks, closing the repeated-cleanup case before browser integration.
- Hardened the combined completion/wake helper so it rejects every occupied
  parent transition, including `EXEC`, rather than only rejecting an existing
  wake. The child handler and parent transition remain unchanged on rejection;
  the lifecycle sanitizer gate now passes 78 checks.
- Re-ran the ASan/UBSan executable transition matrix after that invariant
  change. All 50 image-exec, pending-result, and failed-transition checks pass;
  the stricter WAST completion helper does not alter ordinary Wasm replacement
  behavior.
- Re-ran the consolidated loader baseline: source-loader passes 10/10 and the
  POSIX path VFS gate passes 56/56. Together with the 78-check lifecycle and
  50-check exec matrices, the native loader boundaries remain green while the
  browser WAST adapter is still the only open execution gate.
- Re-ran all eight browser text-entry probes. Direct, shebang, and failing WAT
  probes each pass 5/5 with the expected 0/126 statuses; direct, shebang, and
  malformed WAST probes also pass 5/5 at the intentional unsupported boundary
  with status 126. No browser regression is present while live WAST execution
  remains deferred.
- Strengthened the failed-transition ownership assertions: occupied,
  conflicting, and disappeared-parent rejection paths all preserve the
  attached handler context, with destruction still occurring exactly once at
  successful reap. The 78-check lifecycle sanitizer gate remains green.
- Completed a full native Stage 8C gate batch: parser reentrancy passes,
  source loading passes 10/10, POSIX path VFS passes 56/56, the exec matrix
  passes 50/50, and process lifecycle passes 78/78 under ASan/UBSan. This
  leaves the browser-side live WAST adapter as the next implementation stage,
  with no native loader regression to resolve first.
- Documented the completion helper's browser-facing contract in `store.h`:
  validate the parent and transition slot, publish child completion, queue one
  wake, and only then resume the parent continuation. This keeps the remaining
  browser work focused on stream/evaluator adaptation rather than reimplementing
  process lifecycle ordering.
- Extended the parser sanitizer regression to verify the handler's initial
  cursor before its first command (`offset=12`, `line=2` for a shebang) as well
  as the post-command position. The `parser-reentrant` gate passes, so both
  handler installation and resume now have tested original-source coordinates.
- Added a capsule-bridge regression that applies the stream's initial shebang
  position to an installed WAST handler and verifies the owned source pointer,
  offset, and line. It also confirms malformed cursor-output requests are
  rejected without mutation; the ASan/UBSan parser-reentrant gate passes.
- Extended the capsule bridge across LF, CRLF, and missing-final-LF shebang
  sources. The expected original offsets (12 for LF, 13 for CRLF) and line 2
  are installed into the handler without mutation; the parser-reentrant
  sanitizer gate remains green.
- Extended the same bridge through the first parsed command: the stream's
  post-command original offset and line are advanced into the capsule and read
  back unchanged for both LF and CRLF forms. This verifies the cursor handoff
  needed for a resumed WAST command; the parser-reentrant sanitizer gate passes.
- The capsule bridge now covers startup and post-command cursor installation
  for LF, CRLF, and missing-final-LF sources, including malformed-output
  rejection and source ownership. This closes the native cursor-contract
  prerequisite; the remaining Stage 8C work is browser evaluator/stream
  integration and parent continuation resumption.
- Clarified the browser adapter lifetime requirement: its `wast_stream`,
  browser command context, and any pending-resume marker must be owned by the
  process capsule (or an equally persistent store-owned driver), not by a
  `browser_invoke_process` stack frame. `waste_wast_resume()` re-enters through
  a later call, so stack-local handler state would lose the stream cursor and
  make a yielding command impossible to resume safely.
- Added the clone-isolation regression: a capsule with an attached handler
  context rejects cloning, while the original context remains owned by the
  child and is destroyed only during normal reap. The ASan/UBSan lifecycle
  gate now passes 79 checks, confirming the persistent browser context cannot
  be aliased across process capsules.
- Rebuilt the native CLI runner and reran the browser text-entry matrix after
  the transition hardening: direct WAT, shebang WAT, direct WAT failure, and
  WAST preflight all pass 5/5, while the native runner builds with warnings as
  errors. This confirms the new commit/preparation guards do not regress the
  established loader behavior; live WAST execution remains the outstanding
  architectural work.
- Added the no-pending regression: a parent cannot consume a wake result before
  the child wake is queued, and its transition remains unchanged. This closes
  the negative side of the fork-import guard used by the browser path.
- Attempted the first live browser handoff using the existing `wast_stream`
  command processor: the loader payload reached the handler path, but the
  Bash WAST probe timed out before its prompt returned. The integration was
  reverted after the probe failed; browser build and WAST preflight remain
  clean. This narrows the unresolved work to the handler-to-Bash continuation
  loop and child wakeup ordering, rather than the loader or handler ownership
  seams.
- Closed the checkpoint safety boundary for the persistent handler state.
  Store checkpoints do not yet serialize process capsules, WAST stream
  cursors, or opaque browser driver contexts, so capture now rejects a live
  process handler with `EXEC_ERROR_UNSUPPORTED` before publishing a snapshot.
  The ASan/UBSan store-checkpoint gate passes 16/16, including rejection
  atomicity and the ordinary evaluator snapshot path; this is a deliberate
  integration boundary for the remaining browser WAST driver, not a new
  architectural blocker.
- Added the attached-context handler-step API. A process driver can now invoke
  one parser/command step through the context owned by the capsule, so the
  callback cannot accidentally borrow a stack-local browser stream context
  across `waste_wast_resume()`. The lifecycle sanitizer gate passes 80/80,
  including context delivery, cursor preservation on yield, and the existing
  cleanup path; the browser adapter still needs to install its concrete
  context and use this API for live WAST execution.
- Added an atomic handler handoff variant that accepts the persistent driver
  context and its destructor while transferring the WAST source from the exec
  request. The context is now present at the same commit boundary as the
  handler, eliminating a runnable-without-driver window for browser adoption.
  The ASan/UBSan exec-transition matrix passes 50/50, including context
  identity and image replacement; the context-free wrapper remains available
  for existing callers.
- Extended the failed-handoff regression to pass a destructor-bearing driver
  context through a duplicate-handler commit. The rejected request retains
  its bytes, the active capsule retains its original context, and the retry
  context is destroyed exactly once by its caller rather than by the failed
  transition. The ASan/UBSan exec-transition matrix remains 50/50, closing
  the retry ownership contract before browser integration.
- Added the store-level handler-step dispatch API. A browser driver can now
  select the active process through the store and run one attached-context
  step without retaining or dereferencing a capsule pointer across a resume.
  The process lifecycle sanitizer gate passes 81/81, including active-process
  selection, capsule-owned context delivery, and yielded cursor preservation.
  This leaves the concrete browser WAST stream adapter as the next integration
  step.
- Added store-level handler cursor get/advance operations. The browser driver
  can now borrow the owned source and original-source cursor, then commit a
  monotonic offset/line update through the active process after a resumed
  command. Complete-output validation and post-advance visibility are covered;
  the process lifecycle sanitizer gate passes 83/83. The remaining work is to
  connect these seams to a persistent `wast_stream` adapter.
- Connected those store seams to a real reentrant `wast_stream` regression.
  A shebang-bearing source is installed with its original offset/line, one
  command is consumed per yielded handler step, and a final step reaches EOF
  with the correct unadjusted source coordinate. This exposed and fixed an EOF
  double-application of the stripped shebang offset in `wast_stream_position`.
  The ASan/UBSan parser-reentrant gate passes, including concurrent parser
  isolation and the persistent two-command handler stream; browser adapter
  integration remains the next step.
- Added active-store lookup for the capsule-owned handler context. A later
  resume can recover the persistent stream-driver object through the selected
  process, with complete-output validation and no direct capsule-pointer
  requirement. The process lifecycle sanitizer gate passes 84/84, closing the
  context lookup seam needed before browser WAST execution is enabled.
- Added active-store suspend/resume operations for handler waits. Browser and
  readiness callbacks can now transition the selected process through either
  blocked state while preserving its owned stream cursor. The ASan/UBSan
  process lifecycle gate passes 86/86, covering both wait classes; the browser
  adapter still needs to map its actual yield reasons onto these operations.
- Added the yield-reason mapping boundary: external `READ` and `SELECT` waits
  enter the browser-blocked handler state, while internal `FORK` and `EXEC`
  transitions are rejected before changing process state. The process
  lifecycle sanitizer gate passes 87/87, so browser resumption now has an
  engine-owned reason-to-state contract.
- Added active-store handler-result lookup. The browser driver can inspect the
  process-owned `exec_status` and explicit exit byte after a command step,
  with complete-output validation before any completion/wake transition. The
  process lifecycle sanitizer gate passes 88/88, closing the status handoff
  needed to map WAST completion to Bash-visible results.
- Completed the native end-to-end WAST handler transition. The parser
  regression now forks a parent/child pair, consumes a shebang WAST stream one
  command per handler step, completes the child, queues and consumes the parent
  wake, and reaps the child with status zero. The ASan/UBSan parser-reentrant
  gate passes with concurrent parser isolation, proving the process-driver
  ordering before browser promotion; the remaining 8C work is browser adapter
  wiring and its continuation gate.
- Attempted the first browser adapter promotion using the persistent stream
  driver and store-level handler APIs. The browser artifact compiled, but the
  valid-WAST Bash probe hung before returning to its prompt; the branch was
  removed and the established WAST status-126 preflight was reverified at
  5/5. This confirms the native handoff is ready while the unresolved issue is
  specifically browser handler completion/continuation scheduling, not source
  ownership or parser cursor state.
- Added a result-owned completion helper that derives the shell exit byte from
  the handler status recorded by its last step, then performs the checked
  child completion and parent wake. The real forked parser regression now uses
  this helper rather than passing a separately supplied status/code. The
  ASan/UBSan parser-reentrant gate passes, further narrowing browser work to
  continuation scheduling and not result propagation.
- Added the explicit VFS-to-stream regression requested by the loader design.
  A regular executable `/bin/stream.wast` is staged in the POSIX VFS, copied
  through `posix_kernel_path_snapshot`, and consumed by the reentrant WAST
  stream one command at a time. The parser-reentrant ASan/UBSan gate passes,
  confirming that the source handoff is an owned VFS byte buffer; mmap versus
  VFS storage is not the remaining browser blocker.
- Extended the real forked stream completion regression across an external
  wait: after the first parsed command, the child enters the browser-blocked
  state for `READ`, resumes through the store, and continues with the next
  command before completing and waking its parent. The parser-reentrant
  ASan/UBSan gate remains green, covering cursor persistence across the actual
  wait boundary.
- Added the malformed-stream completion path to the same forked regression.
  A parser error records `EXEC_ERROR_FORMAT`, the result-owned completion
  helper maps it to shell status 126, wakes the blocked parent, and the child
  remains reapable with the encoded status. The parser-reentrant ASan/UBSan
  gate passes, covering both successful and failed handler completion.
- Extended the successful forked stream regression across both actual external
  wait classes: `READ` after the first command and `SELECT` after the second.
  Each wait resumes through the active store before the final command completes
  and wakes the parent; the parser-reentrant ASan/UBSan gate remains green.
- Routed the lifecycle explicit-exit wake regression through the recorded-result
  completion helper. A process-owned `EXEC_ERROR_EXIT` with code 9 now drives
  child completion, parent wake, and POSIX wait encoding without separately
  supplied status arguments. The ASan/UBSan process lifecycle gate passes
  88/88.
- Made external wait reasons process-owned as well. Handler capsules now retain
  `READ` versus `SELECT` while blocked, expose it through the active store, and
  clear it only on resume; internal transition reasons remain rejected. The
  process lifecycle ASan/UBSan gate passes 88/88, reducing browser continuation
  dependence on global wait state.
- Normalized wait state during handler cloning. A forked child begins runnable
  with no inherited external wait reason, while the parent retains its blocked
  `READ`/`SELECT` marker. The process lifecycle ASan/UBSan gate passes 90/90,
  covering context-free cloneability and child wake-state isolation.
- Strengthened the clone regression to verify both sides of that boundary: the
  child is runnable with `EXEC_YIELD_NONE`, while the parent remains blocked
  with its original `SELECT` reason. The process lifecycle ASan/UBSan gate
  remains green at 90/90.
- Wired the process-owned wait reason into the browser exports. The public
  wait-kind and resume entry points now consult the selected handler capsule
  before falling back to legacy global state, and the browser WAST artifact
  builds successfully. This is preparatory ownership wiring only; live WAST
  command execution remains gated by the unresolved handler continuation loop.
- Rebuilt the self-contained Bash page after the wait-state export change.
  Smoke and process-continuation checks pass, and valid, shebang, and malformed
  WAST browser probes each remain 5/5 at the documented status-126 preflight
  boundary. No regression was introduced while live handler execution remains
  disabled.
- Re-ran the consolidated native loader baseline after the browser wait-state
  wiring: source loading passes 10/10, POSIX path VFS passes 56/56, the exec
  transition matrix passes 50/50, and process lifecycle passes 90/90 under
  ASan/UBSan. The native boundaries are green; the only open 8C execution gate
  remains the browser WAST continuation loop.
- Added a user-facing browser evidence mode to the self-contained C-engine Bash
  page. The page now stages small WAT and WAST files in its VFS, runs explicit,
  direct, and shebang loader probes from a button, records expected exit-status
  markers, and downloads a plain-text log containing browser metadata and the
  terminal transcript. The rebuilt artifact passes the existing C-engine Bash
  smoke gate (5/5); this provides reproducible browser evidence even while the
  live WAST continuation path remains intentionally reported at status 126.
- Reviewed the first downloaded Firefox evidence log,
  `waste-browser-loader-evidence-2026-09-22T23-01-29-868Z.log`. The explicit,
  direct, and shebang WAT probes all returned their expected status-zero
  markers. The first explicit WAST command emitted `errno 8` and then failed to
  return to Bash, so no WAST marker or completion timestamp was recorded. This
  reproduces the known browser continuation blocker in the user-facing page:
  WAST is not merely an untested status-126 boundary; its live handler path
  still hangs before child completion and parent wakeup.
- Attempted to promote the browser adapter to live WAST handler execution.
  The existing Bash smoke and process-continuation gates remained green, but
  the valid-WAST probe reached parent continuation restoration with a null or
  invalid executable function table. The promotion was reverted to preserve
  the stable status-126 preflight boundary. The live WAST continuation blocker
  is therefore still open and needs a focused parent-engine ownership fix.
- Started that ownership fix by pinning the parent process image when its
  continuation is captured for a fork. This keeps the parent image alive while
  the child may replace its own image with a WAST handler; the continuation
  consumes the pin during normal restoration. The native process-lifecycle
  ASan/UBSan gate passes 90/90, and the native i32 smoke gate passes. The
  broader native WAST build was interrupted before completion, so no new WAST
  integration pass is claimed; browser testing is deferred until live handler
  activation is re-enabled.
- Moved the image-pin contract probe into the executable-transition matrix,
  where the image ownership implementation is linked, rather than the smaller
  process-lifecycle binary. The new checks cover release-before-unpin and
  verify that a continuation pin keeps a zero-reference image alive. Rebuilt
  and ran the sanitizer matrix with the probe: 53 checks pass with zero
  failures. This closes the native image-ownership sub-step; the live WAST
  browser handler remains disabled pending the separate parent-engine
  continuation fix, so browser testing remains deferred.
- Re-enabled the live browser WAST handoff under the existing Bash harness to
  test that continuation fix. Both eager parent restoration and deferred
  restoration reached the same failure after the child handler completed: the
  restored parent engine had a zero function/import count, producing
  `process entry 871 is outside engine function range 0`. The experiment was
  reverted; `./start.sh --html-bash` remains green with the intentional WAST
  status-126 preflight boundary. The blocker is now narrowed to preserving the
  parent engine's decoded function tables across handler completion, not WAST
  stream parsing or VFS byte delivery.
- Added a native continuation invariant for that boundary: capture and resume
  must preserve the engine's decoded function and imported-function counts.
  The executable-transition ASan/UBSan matrix now passes 54/54, including the
  new invariant. To make this sanitizer gate reproducible within the build
  window, only the test target's optimization level was lowered to `-O0`; the
  production engine remains unchanged. This rules out evaluator restoration as
  the direct source of table loss and leaves browser process/store ownership as
  the next 8C investigation.
- Extended the same matrix through `fork` and
  `native_store_clone_process_graph`: the child receives an independent engine,
  graph binding leaves both parent and child function/import counts intact, and
  the gate passes 57/57 under ASan/UBSan. This rules out the ordinary native
  process-graph clone as the direct source of the browser table loss; the next
  investigation is the browser-specific handler completion path and its global
  active-engine bookkeeping.
- Rebuilt the offline browser Bash artifact after that native graph gate and
  reran the WAST loader probe. The stable preflight boundary remains green at
  5/5 with the expected `errno 8`/status-126 result, and the ordinary Bash
  smoke and continuation gates remain green. No manual browser test is needed
  for this increment; a browser run will be requested only after the live
  handler transition is changed again.
- Attempted the next browser-specific diagnostic by tracing engine metadata at
  fork, handler commit, child completion, and parent restoration. The offline
  worker harness does not expose native `stderr`, so the trace produced no
  additional observable transition data and was removed. The stable artifact
  was rebuilt afterward and the WAST probe again passes 5/5; the next useful
  increment must expose a bounded engine-transition evidence record through
  the existing browser log rather than relying on native stderr. No manual
  browser test is needed yet.
- Added that bounded evidence path. The C browser API now records a fixed-size
  transition sequence and exports its pointer/length; the worker emits it into
  the existing transcript/log after execution. The offline WAST probe reports
  `run-start,exec-yield,exec-preflight-rejected` and still passes 5/5, so the
  evidence is now reproducible without native stderr. No manual browser test
  is required for this increment; an optional browser run can confirm the
  downloaded log contains the new `WASTE_TRANSITION_EVIDENCE=` line.
- Used the evidence path with live WAST temporarily enabled. It identified
  `release-current-f2107-i231` immediately before the parent became `f0/i0`:
  anonymous-module cleanup was freeing the parent Bash engine. Cleanup now
  moves process-owned engines to the store orphan list instead of freeing
  them. With live WAST enabled, counts remain `f2107/i231` through handler
  completion and parent wake; the remaining live failure is parent Bash
  resumption, not engine-table loss. Live activation was disabled again while
  that wake/resume issue remains isolated. The stable artifact passes the
  offline WAST probe 5/5, and its evidence now records the preserved parent
  counts. No manual browser test is required yet.
- Extended the evidence through parent resumption. With the engine-freeing fix
  applied, live WAST reaches `parent-invoke` and then yields
  `parent-yield-r1` (`EXEC_YIELD_READ`); it no longer fails with an empty
  function table. The browser prompt/output handoff is still incomplete, so
  live activation remains disabled. The restored stable artifact passes the
  offline WAST probe 5/5, and the evidence path now captures parent-resume
  attempts. No manual browser test is required for this increment.
- Confirmed the terminal boundary with the parent-resume evidence: the
  restored Bash engine re-enters repeatedly and reaches `EXEC_YIELD_READ`, but
  the expected post-command prompt write is not observed before the worker
  waits for input. The stable artifact still passes the offline WAST probe
  5/5; the next fix must preserve or replay the parent terminal-output state
  across the wake, not alter WAST parsing or engine ownership. No manual
  browser test is required yet.
- Added a bounded `term-write` transition marker at the browser POSIX terminal
  write boundary and retested live WAST. After parent wake, the evidence shows
  terminal writes occurring before `parent-yield-r1`; Bash and the kernel are
  producing output, while browser-side publication or prompt recognition is
  still incomplete. Live activation remains disabled, the stable WAST probe
  passes 5/5, and the write evidence is retained for the next browser adapter
  fix. No manual browser test is required yet.
- Extended the write marker with translated byte counts and prompt detection.
  Live WAST now records `term-write-n10-bash` after parent wake, proving that
  the restored Bash engine emits a nonempty prompt through the terminal path;
  the offline harness still does not advance to the expected status marker.
  This moves the remaining issue to worker message scheduling/prompt
  recognition after publication. Live activation remains disabled, the stable
  WAST probe passes 5/5, and no manual browser test is required yet.
- Tested the suspected prompt-recognition cause by making the browser harness
  carry prompt fragments across worker messages. It did not advance the live
  WAST command, and the change was reverted to avoid stale-prompt matches.
  The stable artifact still passes the WAST probe 5/5; the remaining issue is
  in the live worker/input scheduling after the prompt write, not simple prompt
  fragmentation. No manual browser test is required yet.
- Added temporary sequencing diagnostics around the follow-up input request.
  The harness recognizes the post-wake `bash-5.2#` prompt and schedules the
  status command, but no subsequent engine output arrives; the diagnostic was
  removed after confirming this. The stable artifact still passes the offline
  WAST probe 5/5, and the remaining live blocker is input enqueue/resume after
  the parent terminal read. No manual browser test is required yet.
- Added enqueue/read outcome evidence. Live WAST records
  `input-enqueue-ok` after the parent’s `read-eagain`, but no following
  `read-data` event; the queued bytes are not consumed by the resumed live
  handler path. The stable preflight path does consume input and passes 5/5,
  so the next fix is specifically the live handler’s resumed evaluator state.
  The extra outcome markers are retained, live activation remains disabled, and
  no manual browser test is required yet.
- Corrected the browser resume entry point so a handler-driven Bash parent that
  yields again for terminal input re-arms `g_yield_active` before returning to
  JavaScript. With live WAST temporarily enabled, the transition evidence now
  reaches `parent-invoke`, `read-data`, and a second `yielded:1` after the
  post-command status request; the probe still times out before its final
  follow-up command, so the live handler remains disabled behind the stable
  preflight boundary. The temporary input/resume snapshots were removed; the
  bounded final transition evidence remains. The stable WAST probe is still
  5/5, and no manual browser test is required until the live probe reaches a
  clean second prompt and exit.
- Corrected the browser probe’s expected-status selection: it had accidentally
  classified every WAST probe as a failing WAT probe and therefore waited for
  status 126 after a successful live WAST command. Live WAST is now enabled;
  the normal, shebang, malformed, and direct-file probes all pass 5/5 through
  the offline browser harness, including the bounded transition evidence and
  a later prompt/exit. Browser artifact generation and Node syntax/diff checks
  pass. The next browser-facing check is now manual evidence capture from
  `build/html-rt/bash.html`, not another loader implementation blocker.
- Fixed the browser evidence action itself. Its WAST expectations now use
  status 0 for valid modules, and it sends each loader command only after the
  preceding marker appears instead of batching the entire script into one
  terminal read. This prevents the WAST shebang case from stalling at the
  child fork/exec handoff. The offline browser harness still passes the normal,
  shebang, malformed, and direct WAST probes 5/5 after rebuilding
  `build/html-rt/bash.html`; a fresh manual evidence log is now required to
  verify the UI download path.
- The next evidence log showed that marker-only sequencing still sent the
  following command before Bash emitted its prompt. Updated the UI sequencer
  to require both the completed marker and a subsequent `bash-...#`/`$` prompt
  before enqueueing the next command. The rebuilt artifact passes the complete
  WAST normal, shebang, malformed, and direct-file harness matrix 5/5; manual
  browser evidence should be rerun once more to verify the end marker and
  downloaded log.
- The following evidence log showed the same early-send behavior because the
  marker and prompt could arrive in one terminal message, with the prompt
  preceding the marker. The sequencer now records the marker’s position and
  accepts only a Bash prompt appearing after that position. The rebuilt
  artifact passes the complete WAST browser harness matrix 5/5; manual
  evidence capture remains the final UI confirmation.
- The 01:15 evidence log identified a compound-command boundary: direct WAST
  execution followed by `printf "$?"` could terminate the browser runner
  before the next shebang command was delivered. The evidence UI now runs each
  WAST image command separately, waits for its next Bash prompt, and only then
  issues the status-marker command. The rebuilt artifact passes the complete
  WAST browser harness matrix 5/5; a fresh manual evidence log is required to
  confirm the UI sequence now reaches the shebang marker and end marker.
- The 01:19 evidence log exposed a browser-worker timing race: Bash’s prompt
  output can arrive on the main thread before the worker’s synchronous Wasm
  resume has returned. Added an explicit `io-ready` worker handshake and made
  evidence sequencing require the marker, the later prompt, and the returned
  worker state before sending input. The rebuilt artifact passes the complete
  WAST harness matrix 5/5; manual evidence capture should now be repeated.
- The 01:22 log showed that the first evidence command had not consumed the
  worker-ready token, leaving every later command one resume handshake ahead.
  Fixed the initial token accounting and added a same-shell regression that
  runs explicit, direct, and shebang WAST images consecutively with separate
  status checks. That mixed-form regression passes 5/5, as do all eight
  individual WAT/WAST normal, direct, shebang, and malformed browser probes.
  The rebuilt `build/html-rt/bash.html` is ready for final manual evidence
  capture.
- The 01:38 log showed one remaining sequencer defect: marker-driven steps
  required the marker and worker-ready state but not the Bash prompt after the
  marker. Restored the marker-then-prompt requirement for those steps. The
  rebuilt artifact passes the mixed explicit/direct/shebang WAST regression and
  all eight WAT/WAST browser probes; the result-detail and rolling transition
  evidence remain available for any future failure.
- The 01:44 log exposed the actual runtime leak: after the third mixed-form
  WAST invocation, `main` failed with `parent continuation graph allocation
  failed`. Repeated fork/exec restored the parent graph but left its consumed
  continuation arrays attached to the process capsule, exhausting the Wasm
  heap. Added explicit graph disposal immediately after parent restoration and
  retained failure-result/rolling-transition diagnostics. The exact sequence
  of three WAT executions followed by explicit, direct, and shebang WAST now
  passes 5/5, along with all eight individual browser probes.
- The 01:52 browser log reached all six positive markers and correctly left
  Bash running, but its downloaded metadata still said
  `evidence_finished_at=not-finished`. Hardened the evidence recorder to test
  the accumulated transcript (including markers sharing a worker message) and
  added an explicit `evidence_complete=yes|no` field. Rebuilt browser Bash
  passes the repeated mixed-form regression 5/5 and all eight individual WAT/
  WAST probes; native ASan/UBSan parser, source-loader, exec-transition,
  process-lifecycle, and checkpoint gates also pass. One fresh browser
  download is required to confirm the new completion metadata.
- The 02:03 Firefox browser evidence log closes that final acceptance check:
  `evidence_complete=yes`, a populated `evidence_finished_at`, and all six
  WAT/WAST explicit, direct, and shebang markers are present. Bash remains at
  a usable prompt after the sequence. Stage 8C is closed; Stage 8D is the
  next active implementation stage.

## Stage 8D: Build a WASTE target sysroot and CRT

Status: complete (sysroot, CRT, cross-configure, deterministic utility link,
and import-report pipeline)

Work:

- Generate a target sysroot under `build/html-rt/coreutils/sysroot` from the
  selected headers; no generated sysroot file is a repository source of truth.
- Define the target tuple and compiler wrapper for wasm32 freestanding code
  with explicit include paths, freestanding flags, feature macros, and no host
  headers.
- Provide a small CRT object exporting `_start`, running constructors, calling
  `__waste_startup_call(main)`, and mapping the return value to process exit.
- Link one application module per utility against the existing WASTE runtime
  memory/table and guest-libc interface. Do not statically create a second
  kernel or libc state inside each utility.
- Cross-configure coreutils out of tree with a reviewed `config.site` or cache
  file. Every forced answer must include a reason and a test or known browser
  limitation; never claim an unimplemented function works merely to advance
  configure.
- Generate dependency, undefined-symbol, import, export, section, and size
  reports for each utility. Reject unknown imports and all Asyncify,
  unwind/rewind, WASI, and Emscripten runtime symbols.

Gate:

- A repository-owned hello/startup fixture built through the same wrapper
  receives argc/argv/envp, cwd, descriptors, and nonzero exit status correctly.
- Two clean builds with the same tools and source produce identical utility
  modules or a documented, normalized source of nondeterminism.
- The generated sysroot and configure report contain no accidental host paths.

Implementation update (2026-09-23, target sysroot and CRT seam):

- Added `src/html-rt/tools/build-waste-sysroot.py`, which copies the selected
  guest headers into `build/html-rt/coreutils/sysroot/include`, records
  repository-relative source paths and SHA-256 hashes in `manifest.json`, and
  emits a freestanding `waste-wasm-clang` wrapper for the
  wasm32 freestanding target. The generated wrapper uses the Clang
  resource headers explicitly and does not inherit host include directories.
- Added `src/html-rt/lib/waste-crt.c`. Its `_start` export runs weak Wasm
  constructors, obtains the engine-owned `startup_v1` block, calls the normal
  `main(argc, argv, envp)` entry, and maps the return value through the
  versioned `env.exit` boundary. It does not create a second kernel or libc.
- Added `tests/coreutils-sysroot-hello.c` and the
  `coreutils-sysroot`/`coreutils-crt-fixture` Make targets. The fixture builds
  through the generated wrapper, links with `wasm-ld`, emits import/disassembly
  reports, and passes the explicit WASTE import audit with no Asyncify symbols
  or unknown imports.
- The fixture module rebuild is byte-for-byte deterministic
  (`39c7d73e4e62b05e229515294f628ae8d8d93bff6cb5d322637047e74a4ac45a` in the
  current toolchain), and generated manifest/module reports contain no host
  paths. Python bytecode, shell syntax, and `git diff --check` gates pass.

The next 8D increment is the reviewed out-of-tree coreutils configure probe;
it must consume this sysroot and record every forced answer before any utility
is treated as buildable.

Implementation update (2026-09-23, configure probe boundary):

- Added `src/html-rt/tools/coreutils-waste.config.site` with only the two
  target facts that are safe to force (`ac_cv_prog_cc_cross=yes` and
  `ac_cv_c_cross=yes`); each is documented with its reason and verification
  method. No function, header, library, or semantic result is being guessed.
- Added `src/html-rt/tools/configure-coreutils.py` and the
  `coreutils-configure-probe` Make target. It stages coreutils out of tree,
  copies the reviewed site file, records the target/compiler/sysroot,
  prerequisites, forced-answer ledger, configure command, and blockers in
  `build/html-rt/coreutils/configure/configure-report.json`, and runs
  configure only when the generated script and all required bootstrap tools
  are present.
- The current pinned source initially lacked generated `configure`; its local
  bootstrap probe recorded the missing `gperf` and `wget` as an honest blocked
  result. It did not download dependencies, modify the coreutils submodule, or
  claim that configure answers succeeded.
- Fixed `stage-coreutils.py` to replace only the staged `source/` tree and
  provenance report, preserving sibling generated outputs such as `sysroot/`
  and `configure/` across repeated staging. Sysroot regeneration, configure
  reporting, Python/shell syntax, and `git diff --check` pass.

The subsequent bootstrap-helper increment provisions those tools and enables
the real cross-configure attempt described below.

Implementation update (2026-09-23, idempotent coreutils bootstrap helper):

- Added `submodules/bootstrap-coreutils.sh`. Its default action detects and
  installs only missing upstream bootstrap prerequisites using pacman,
  apt-get, dnf, or zypper; stages the pinned coreutils source under
  `build/html-rt/coreutils/source`; and runs `bootstrap --gen` against the
  checked-in Gnulib copy to generate `configure` without a network pull.
- The helper records the coreutils commit and managed-patch hash in the staged
  tree. Repeated runs skip both restaging and bootstrap when that identity and
  generated `configure` are current; `--force` deliberately regenerates them.
  `--check`, `--no-install`, and `--install-only` provide non-mutating and
  controlled installation modes. The pinned coreutils submodule is checked
  clean after generation.
- Expanded the machine-readable configure probe to check the complete command
  set declared by coreutils bootstrap rather than only Autoconf, Automake,
  Gperf, and Wget. On the current machine, the read-only helper check reports
  only `gperf` and `wget` missing. Shell syntax and `git diff --check` pass;
  package installation was not invoked automatically.

The helper has now been run with permission to install those system packages;
the first real cross-configure attempt is recorded below.

Implementation update (2026-09-23, first cross-configure pass):

- After the bootstrap helper installed the missing `gperf` and `wget`, the
  staged source generated `configure`, `aclocal.m4`, `lib/config.hin`, and
  the Gnulib-generated make fragments successfully. The pinned submodule
  remains clean; all generated files stay under `build/`.
- Fixed the generated compiler wrapper to locate the repository's bundled
  `wasm-ld`, export its `liblld*.so` directory, pass `-nostdlib`, and use
  `--no-entry` for Autoconf's link-only test programs. This prevents host
  libc, host compiler-rt, and an implicit `_start` requirement from entering
  the target probe.
- Changed the GNU configure host tuple to the accepted freestanding value
  `wasm32-unknown-none`; the wrapper still compiles for wasm32. The configure
  probe now passes with no unknown imports involved.
- The reviewed configuration ledger records five explicit constraints:
  cross compilation, the `wasm32-unknown-none` target, configured-out
  post-2038 timestamps pending Stage 8E, WebAssembly little-endian order, and
  the temporary 32-bit `socklen_t` socket ABI assumption pending Stage 8E.
  These are emitted in `configure-report.json` with reasons and validation
  requirements rather than hidden cache answers.
- `make -B -C src/html-rt BUILD_DIR=../../build/html-rt
  coreutils-configure-probe` completes with `status=passed`; the next
  increment is compiling a deliberately small coreutils utility and auditing
  its imports against the guest libc/POSIX boundary.

Implementation update (2026-09-23, first utility compile boundary):

- Added `src/html-rt/tools/probe-coreutils-utility.py` and the
  `coreutils-utility-probe` Make target. It compiles staged `src/true` with
  `make -j1 V=1`, captures the complete build log, and writes a relative-path
  JSON report containing missing headers, undefined symbols, unknown imports,
  and Asyncify symbols. Expected ABI gaps are reported as `blocked` so the
  diagnostic target remains usable while unexpected compiler/linker failures
  return failure.
- The first probe reaches upstream coreutils compilation and is blocked at the
  guest boundary because the current sysroot has no public `stdio.h`. The
  captured report is `build/html-rt/coreutils/utility-probe/true-report.json`
  and the log is `true.build.log`; this is a Stage 8E libc/header task, not a
  loader or configure failure. The probe also exposed and fixed its own output
  path handling, and its report now points to the sibling staged `source/` and
  `sysroot/` trees correctly.
- No browser test is needed for this increment: it changes only build-time
  diagnostics and does not alter the browser runtime or packaged VFS. The
  next implementation work is to add the truthful public headers and libc
  declarations needed by `true`, then rerun this same probe before attempting
  linking or import auditing.

Implementation update (2026-09-23, Stage 8E public-header seam):

- Added the first WASTE-owned application headers to the generated sysroot:
  `stdio.h`, `stdlib.h`, `string.h`, `stdint.h`, `inttypes.h`, `ctype.h`,
  `uchar.h`, `locale.h`, `time.h`, `sys/types.h`, `sys/time.h`, `sys/stat.h`,
  `dirent.h`, `fcntl.h`, `unistd.h`, `errno.h`, `error.h`, and `alloca.h`.
  Their ABI uses Wasm32 widths and the engine-owned structures from
  `helper.h`; no host headers are copied into the target sysroot.
- Added allocator-side `posix_memalign` with an explicit 16-byte alignment
  contract. Requests outside that contract return `EINVAL` instead of
  pretending that a stronger alignment guarantee exists.
- The probe now compiles `src/true.c` and its startup objects, then advances
  through coreutils/gnulib support compilation. The latest captured failure
  is a missing `getdelim` declaration in `argv-iter.c`; the public `stdio.h`
  declaration has now been added. This demonstrates that the blocker is no
  longer the loader or configure path, but the ordinary libc and POSIX ABI
  surface required by upstream support code.
- These headers are compile-seam declarations, not an acceptance claim:
  Stage 8E still must provide or deliberately reject every linked symbol, run
  the import audit, and verify the corresponding VFS/errno semantics. Browser
  testing is not required for this increment because no packaged Wasm module
  or browser runtime changed.

Implementation update (2026-09-23, stream and recursive-header increment):

- Implemented guest `fread`, `fgetc`, `getc`, `feof`, `getdelim`, and
  `getline` in `src/html-rt/lib/stdio.c`. The implementation preserves the
  delimiter contract, grows the caller-owned buffer, marks EOF/error state,
  and uses the existing descriptor-backed `read` path when POSIX I/O is
  enabled.
- Added the `arpa/inet.h` application header and fixed the sysroot Make
  dependency to discover headers recursively. This prevents nested headers
  such as `arpa/inet.h` from silently being omitted from a regenerated
  sysroot manifest.
- After regeneration, `true` compiles its main source and the preceding
  support objects; the probe advanced into `openat-proc.c`, where the next
  missing ABI constant was `O_CLOEXEC`. That constant has now been added to
  the WASTE `fcntl.h` surface. The next run must continue until compilation
  completes, then link and audit every referenced libc/POSIX symbol.

Implementation update (2026-09-23, allocator and character-I/O increment):

- Added overflow-checked `reallocarray` to both the freestanding compiler
  support and guest libc surfaces. The `true` probe now compiles through the
  allocator, path, directory, and binary-I/O support objects.
- Added the minimal WASTE `wchar.h` surface and guest `btowc` implementation,
  including `WEOF`; this is backed by the existing ASCII/UTF-8 character
  handling in `wchar.c`, not host locale state.
- The latest probe reaches coreutils' `btowc.c` and is currently stopped at
  the next source-level ABI declaration/constant boundary. The utility is
  still not linkable or import-audited, so Stage 8E remains in progress.
- The rebuilt guest libc Wasm module and generated fixtures pass, as do
  Python bytecode, shell syntax, and `git diff --check`. Browser testing is
  still not required until a linked utility module is packaged into the VFS.

Implementation update (2026-09-23, file-type and locale-header increment):

- Added WASTE-owned `byteswap.h` and `langinfo.h`, with byte-order helpers
  matching WebAssembly little-endian memory and the existing compact `C.UTF-8`
  locale implementation. The duplicate byte-swap helper names were avoided
  between `arpa/inet.h` and `byteswap.h` so both headers can coexist.
- Completed the file-type macro surface in `sys/stat.h`, including regular,
  directory, link, device, FIFO, and socket predicates plus explicit false
  results for unsupported host-only object types. These macros describe the
  WASTE VFS model; they do not claim unsupported object kinds exist.
- The latest probe advances through the coreutils file-type support and stops
  at the missing `langinfo.h` boundary, which has now been added. The next
  run continues the compile ledger; linking and import auditing remain
  pending.

Implementation update (2026-09-23, numeric and wide-character increment):

- Added the standard `long double strtold` declaration and guest implementation
  backed by the existing WASTE floating-point conversion path. The previous
  internal quad-word helper was not exposed as a mismatched C ABI.
- Added `wctype.h`, `c32_type_test_t`, `c32is*` ASCII predicates, and the
  missing wide-character declarations needed by Gnulib's type-test helpers.
  The predicates intentionally implement the documented compact ASCII/
  `C.UTF-8` model rather than host locale behavior.
- The probe now compiles through the long-double and wide-character conversion
  objects and currently reaches the remaining `c32` type-test support. It has
  not yet reached utility linking; import auditing and semantic POSIX tests
  remain subsequent work.

Implementation update (2026-09-23, generated Unicode support increment):

- Extended the utility probe to generate coreutils' staged headers
  (`configmake.h`, `version.h`, `unitypes.h`, and `unictype.h`) before compiling
  the utility. This keeps generated-source prerequisites in the out-of-tree
  build instead of copying them into the WASTE sysroot.
- Added the WASTE-facing `c32` declarations and wide-character graph,
  punctuation, and hexadecimal predicates required by Gnulib's generated
  Unicode classification objects.
- The probe now compiles through the initial `c32` classification objects and
  stops at the next missing wide-character predicate boundary. Linking and
  import auditing remain pending; the guest libc build must be rerun after
  this source increment.

Implementation update (2026-09-23, C32 conversion ABI increment):

- Removed inline `c32is*` and `c32isspace` definitions from the public
  `uchar.h` header where they conflicted with Gnulib's generated translation
  units; the header now exposes declarations and the staged sources provide
  the implementations.
- Corrected the `c16rtomb`, `c32rtomb`, `mbrtoc16`, and `mbrtoc32` prototypes
  to use the public `mbstate_t *` type. The probe had reached all C32
  classification objects and stopped on that exact signature mismatch.
- The next compile pass proceeds into the C32 conversion objects. The module
  is still pre-link, so browser testing and import evidence remain deferred.

Implementation update (2026-09-23, standard C32 return-width correction):

- Corrected the public `uchar.h` conversion prototypes to return `size_t`,
  matching the C standard and the staged Gnulib definitions for
  `c16rtomb`, `c32rtomb`, `mbrtoc16`, and `mbrtoc32`. The prior `int` return
  type was detected by the compiler before any link step.
- The next required work is to provide the corresponding guest-libc C32
  conversion implementations (or explicitly configure the unsupported
  variants out) before a utility can reach linking. Browser testing remains
  deferred because no coreutils Wasm binary exists yet.

Implementation update (2026-09-23, guest C32 conversion implementation):

- Implemented `c32rtomb`, `mbrtoc32`, `c16rtomb`, and `mbrtoc16` in the guest
  `wchar.c` using the existing checked UTF-8 encoder/decoder. Invalid
  surrogate and unsupported wide-codepoint cases return the WASTE encoding
  error instead of silently truncating.
- The guest libc Wasm module rebuild passes after these additions, along with
  Python bytecode, shell syntax, and `git diff --check`. The coreutils probe
  is ready to resume at the C32 conversion compilation boundary; it remains
  pre-link and therefore needs no browser test yet.

Implementation update (2026-09-23, generated Unicode and width increment):

- Added generated-header preflight for `unicase.h`, `uninorm.h`, and
  `uniwidth.h`, keeping these Gnulib products in the out-of-tree configure
  tree rather than treating them as WASTE-owned source headers.
- Added guest `wcwidth` and `wcswidth` with the compact character model:
  controls are non-printable, ordinary supported characters occupy one
  column, and no host terminal database is consulted.
- The probe now reaches C32 width support and stops at the missing `wcwidth`
  boundary, which has now been implemented. It remains pre-link; no browser
  test is required yet.

## Stage 8E: Close the utility-facing libc and POSIX ABI

Status: complete for the accepted ten-utility set; later utilities extend the
same per-utility ABI ledger rather than reopening this stage

Use real build failures and per-utility import reports to extend the ABI. Keep
shared POSIX semantics in `src/engine/`, browser capability adapters in
`src/html-rt/`, and ordinary C-library behavior in the guest libc.

Work:

- Maintain a machine-readable ledger for every missing declaration, symbol,
  import, option, and semantic failure, with one of: implemented, deliberately
  unsupported, configured out, or deferred.
- Add only versioned kernel calls requiring process/VFS/terminal state. Keep
  formatting, allocation, string, locale, and other ordinary library code in
  guest libc.
- Verify errno ownership, pointer-span checks, integer handles, blocking yield
  behavior, and process-private mutable state for every new boundary.
- Prefer a small truthful locale and identity model over incomplete host
  passthrough. Record unsupported platform facilities in utility help/testing
  notes.
- Update `docs/wasm32-abi.md` whenever a boundary graduates from experimental
  build support to the supported application ABI.

Gate:

- Each enabled utility has zero undeclared imports and no JavaScript-provided
  libc function.
- Native sanitizer tests cover each new engine operation before a utility is
  accepted in the browser package.
- The existing libc fixtures, Wasm conformance tests, Bash probe, and
  command-not-found behavior remain green.

Implementation update (2026-09-23, public-header and generated-source seam):

- Added the guest `netdb.h`, `endian.h`, `assert.h`, `fnmatch.h`, `math.h`,
  `stdio_ext.h`, and Gnulib compatibility headers required by the real
  out-of-tree `true` build. The network declarations now match the existing
  guest implementation rather than relying on pointer-compatible `void *`
  prototypes.
- Added the corresponding Wasm32 declarations and small guest implementations
  for `faccessat`, `fstatat`, `fchmodat`, `fchown`, `fchdir`, `lchown`,
  `utimensat`, `posix_fadvise`, `fseeko`, `_exit`, `rawmemchr`, `mempcpy`,
  `stpcpy`, `strnul`, and `mbscasecmp`; unsupported filesystem operations keep
  returning the engine's explicit unsupported result.
- Extended generated-header preflight to build and consume
  `lib/malloc/scratch_buffer.gl.h` and `lib/crc-sliceby8.h`. The sysroot
  manifest now includes nested headers and the compiler wrapper pre-includes
  only syntax-level Gnulib annotations.
- The probe advanced from missing `netdb.h` through the generated allocator,
  CRC, Unicode, stdio, file-operation, and fcntl boundaries. It currently
  remains pre-link at the next file-creation declaration boundary (`creat`)
  after the fcntl constants were added; 8E is not closed and no browser test
  is requested yet.

Implementation update (2026-09-23, timestamp and file-mode increment):

- Added the guest timestamp ABI declarations/stubs for `utimensat` and
  `futimens`, large-file `ftello`, directory type conversion macros, and the
  standard set-id/sticky mode bits used by Gnulib file-mode formatting.
- The real `true` probe now compiles past `fdutimensat`, `fflush`, and
  `file-has-acl`; its current pre-link boundary is the mode-bit declaration
  seam (`S_ISUID`, `S_ISGID`, and `S_ISVTX`). The utility remains neither
  linkable nor import-audited, so browser testing is still deferred.
- Rebuilt the guest libc successfully after completing the timestamp type
  definition boundary; Python bytecode checks, bootstrap shell syntax, and
  `git diff --check` also pass.

Implementation update (2026-09-23, Gnulib fpending and time-zone seam):

- Added the repository-owned `submodules/coreutils-waste.patch` change for
  Gnulib `fpending.c`. WASTE uses an opaque guest `FILE`, so the patch selects
  a truthful zero-pending-byte path instead of inspecting a host `FILE`
  layout. The sysroot compiler defines `WASTE_WASM` for this target only.
- Forced configure to recognize the guest `stdio_ext.h` and `__fpending`
  declarations, and added the opaque `timezone_t`, `off64_t`, and related
  time-zone declarations needed by Gnulib's formatting code.
- The patch applies cleanly and configure completes successfully. The next
  probe boundary is the remaining `mktime_z`/`localtime_rz` time-zone ABI;
  the utility is still pre-link and browser testing remains deferred.

Implementation update (2026-09-23, stdio extension continuation):

- Added the guest `__fpurge` declaration/stub and the standard `freopen`
  declaration as the compile trace moved beyond Gnulib's `fpending.c` and
  into the remaining stdio portability objects.
- The latest `true` probe compiles through `fpending.c` and currently stops at
  the `freopen` declaration boundary. It remains pre-link; no browser test is
  required.

Implementation update (2026-09-23, stream and directory ABI increment):

- Added the Wasm32 `_POSIX_VERSION` declarations so Gnulib can select its
  portable `fseeko` path, plus guest `strtof`, generated `fts_.h` preflight,
  `dirfd`, and the existing guest `qsort` declaration.
- The probe now compiles through `fseeko`, `ftoastr`, and the generated FTS
  header; its current boundary is the FTS directory-descriptor/qsort seam.
  The utility is still pre-link and browser testing remains deferred.

Implementation update (2026-09-23, socket and resource header boundary):

- Added the freestanding `sys/socket.h` and `sys/resource.h` seams with the
  current WASTE socket/resource constants and ABI-shaped declarations. These
  declarations intentionally preserve unsupported operations as explicit
  runtime failures rather than pretending to provide host networking.
- The probe advanced through FTS and now reports the next missing public
  header, `sys/resource.h`, which has been added. The utility remains
  pre-link and browser testing is still deferred.

Implementation update (2026-09-23, gnulib compatibility and identity headers):

- Added the freestanding `stdbit.h`, `locale.h`, `getopt.h`, `sys/random.h`,
  and `pwd.h` seams, including the GNU getopt ABI annotations, the guest
  locale-category representation, C23 bit helpers used by gnulib, and the
  existing WASTE identity-record layout. Added the corresponding time
  resolution and locale-null declarations needed by generated support code.
- The probe now compiles through getopt, locale-name, random, time, hard-locale,
  hash, and the `pwd.h` identity seam. Its current boundary is the next public
  identity header, `grp.h`. This is still a native pre-link probe, so no browser
  test is requested yet.

Implementation update (2026-09-23, group and locale support boundary):

- Added `grp.h` and completed the matching `getgrgid`/`getgrnam` wrappers over
  the existing guest group record. Added explicit `linkat`/`AT_SYMLINK_FOLLOW`
  declarations and an unsupported-operation result for the guest ABI, then
  filled the locale-conversion, collation, multibyte, and C23 compatibility
  declarations needed by the next gnulib objects.
- The probe passed the group, linkat, localeconv, localeinfo, and btoc32
  compilation boundaries. Its current boundary is the locale-name support's
  `uselocale` declaration, which is now represented in the guest header. The
  utility remains pre-link; browser testing is still deferred.

Implementation update (2026-09-23, stdio and multibyte continuation):

- Added the guest `__fseterr` stdio extension and connected it to the existing
  stream error state in both the engine and guest-libc builds. Added the
  existing `mbsinit` implementation to the public multibyte header so gnulib's
  `mbrtoc32` support can use it.
- The probe advanced through locale-name support, mbbuf, and the stdio error
  seam. Its current boundary is the `mbsinit` declaration in the multibyte
  conversion sources; the declaration is now in place for the next probe.
  The utility remains pre-link and browser testing is still deferred.

Implementation update (2026-09-23, multibyte width and group-list boundary):

- Added the gnulib string declarations for `str_endswith` and `mbslen`, the
  guest `c32width` declaration, and the identity ABI's `getgrouplist` plus
  public `getgroups` declaration. The group-list implementation reports the
  configured primary group and preserves the existing bounded failure model.
- The probe advanced through multibyte conversion, string-suffix/length,
  multibyte-width, and now reaches the `mgetgroups` group-list seam. The
  utility remains pre-link; browser testing is still deferred.

Implementation update (2026-09-23, group ABI and file-mode macros):

- Matched gnulib's configured `GETGROUPS_T` ABI (`int *`) for the guest
  `getgroups` declaration and identity implementation, while retaining the
  existing bounded group-list behavior. Added the standard `S_IRWX*` and
  `S_IXUGO` mode masks plus an explicit unsupported `lchmod` operation.
- The probe advanced through `getgroups` and now reaches the directory
  ownership/mode support object's file-mode macro boundary. The utility remains
  pre-link; browser testing is still deferred.

Implementation update (2026-09-23, mode-mask and FIFO ABI boundary):

- Added the aggregate `S_IRWXUGO` mode mask and explicit unsupported guest
  `mkfifo`/`mkfifoat` declarations and wrappers. This keeps FIFO creation
  visible as a deliberate capability boundary while allowing gnulib's normal
  replacement objects to compile.
- The probe advanced through the directory mode-mask object and now reaches the
  `mkfifoat` replacement. The utility remains pre-link; browser testing is
  still deferred.

Implementation update (2026-09-23, mount portability and select ABI):

- Added a WASTE-specific hunk to `submodules/coreutils-waste.patch` so gnulib's
  unported `mountlist.c` produces an empty mount list on the freestanding guest
  instead of aborting compilation. The patch is applied only while staging and
  the coreutils submodule remains clean.
- Added the engine-owned `sys/select.h` fd-set/timeval ABI and public select
  declarations. Configure was regenerated with the guest config site,
  including the explicit absence of the MSVC invalid-parameter runtime.
- The probe advanced past mountlist, mknodat, and the MSVC runtime seam and now
  reports the next missing public header, `sys/select.h`, which has been added.
  The utility remains pre-link; browser testing is still deferred.

Implementation update (2026-09-23, signal and mount-entry headers):

- Added the guest `signal.h` surface over the existing fixed-size signal-set
  representation, including action, mask, and common signal constants needed
  by coreutils. Added the minimal `mntent.h` structure and declarations so the
  mount-list portability code can compile even though guest mount enumeration
  is intentionally empty.
- The probe advanced past the select and signal header boundaries and now
  reports the next missing public header, `mntent.h`, which has been added. The
  utility remains pre-link; browser testing is still deferred.

Implementation update (2026-09-23, scheduler header boundary):

- Added the freestanding `sched.h` ABI with scheduler parameter and priority
  declarations. The current coreutils wave does not yet require a scheduler
  implementation; the header keeps this capability explicit for later process
  and timeout utilities.
- The probe advanced through select, signal, and mount-entry header discovery
  and now reports `sched.h` as the next missing public header. The header is
  now present; the utility remains pre-link and browser testing is still
  deferred.

Implementation update (2026-09-23, scheduler detection and generated obstack):

- Corrected cross-configure false positives for CPU-affinity and HP-UX `pstat`
  APIs by forcing those host-only probes off in the WASTE config site. Added
  the upstream gnulib `obstack.in.h` template to the deterministic sysroot
  when the staged coreutils source is available, preserving it as generated
  support rather than copying a host libc header.
- The probe advanced through `nproc` and obstack support and now reaches the
  `O_ACCMODE` open-flag boundary, which is being added to the guest fcntl ABI.
  The utility remains pre-link; browser testing is still deferred.

Implementation update (2026-09-23, integer-format and time declarations):

- Added the missing `intmax_t`/`INTMAX_*` definitions and `PRIdMAX`/`PRIuMAX`
  format macros required by generated date parsing. Added the ordinary
  `strftime`, `gmtime_r`, and `localtime_r` declarations alongside the existing
  timezone-aware guest interfaces.
- The probe advanced through open flags, obstack, and integer formatting and
  now reaches the date/time conversion declaration seam. The utility remains
  pre-link; browser testing is still deferred.

Implementation update (2026-09-23, process-spawn and pthread compatibility seam):

- Added the guest `spawn.h` and `pthread.h` declarations needed to compile
  gnulib's broad support set, including the fixed-size attribute/action ABI,
  mutex/condition/once types, and signal-mask entry points. These declarations
  are compile-time ABI surfaces only; process spawning remains an engine-owned
  capability and is not claimed to work yet.
- Added a WASTE-specific hunk to `submodules/coreutils-waste.patch` that
  replaces gnulib's host `spawni.c` fork/vfork implementation with an explicit
  `ENOSYS` stub. This keeps host process mechanics out of the Wasm guest while
  preserving a clean, reviewable patch boundary.
- Filled the next small freestanding seams exposed by the probe: pipe/open
  flags, `timespec` declaration ordering, environment/rename declarations,
  secure string clearing, stdio buffering constants, and Unicode regex type
  predicates. The probe has advanced through the `WEOF` regex boundary and
  currently needs the next `renameat`/path-support compilation pass.
- The staged source patch passes `git apply --check`, configure still passes,
  and the utility remains pre-link. Browser testing is still deferred until a
  real coreutils Wasm module is linkable and import-audited.

Implementation update (2026-09-23, path, process-status, and optional-host ABI seam):

- Added the guest `renameat`, GNU `qsort_r`, and `sys/wait.h` surfaces,
  including status macros needed by the process-oriented coreutils support
  files. Added the minimal `fork`/`vfork` declarations while keeping their
  implementation outside the browser ABI.
- Added explicit unavailable SELinux include shims for `selinux/selinux.h`,
  `selinux/context.h`, and `selinux/label.h`; the configure result continues
  to report SELinux as unavailable, so these headers do not import host
  labeling behavior. Added the `settimeofday` declaration for the guest time
  boundary.
- The probe advanced through `renameat`, `qsort_r`, wait status, and SELinux
  header discovery and currently reaches the `settimeofday` declaration seam.
  The utility remains pre-link; browser testing is still deferred.

Implementation update (2026-09-23, temporary-file and symlink ABI seam):

- Added `mkstemp`/`mkostemp`, `symlink`/`symlinkat`, and corrected the guest
  `strerror` declaration to match gnulib's const-qualified interface.
- The probe advanced through temporary-file helpers and now reaches the
  symlink declaration seam. The utility remains pre-link; browser testing is
  still deferred.

Implementation update (2026-09-23, generated Unicode and unlink-at ABI seam):

- Added the guest `clock` and `secure_getenv` declarations and staged the
  pinned gnulib `unistr.in.h` template as generated `unistr.h`, supplying the
  complete declaration surface for the Unicode objects compiled by gnulib.
- Added `AT_REMOVEDIR` and `unlinkat` to the guest filesystem ABI. The probe
  advanced through temporary-name generation, Unicode support, and now
  reaches the unlink-at declaration seam. The utility remains pre-link;
  browser testing is still deferred.

Implementation update (2026-09-23, identity and timestamp ABI seam):

- Added guest UID queries (`getuid`/`geteuid`) and the `utime.h` surface.
- Added `UTIME_NOW`/`UTIME_OMIT`, `futimesat`, `utimens`, and `lutimens`
  declarations to complete the timestamp compatibility boundary used by
  gnulib.
- The probe advanced through unlink-directory identity handling and now
  reaches the `lutimens` declaration seam. The utility remains pre-link;
  browser testing is still deferred.

Implementation update (2026-09-23, formatted I/O and host-name ABI seam):

- Added the formatted-allocation/output declarations `vaszprintf`,
  `aszprintf`, `vfzprintf`, and `vzprintf`, and made the shared `off64_t`
  definition available from `sys/types.h`.
- Added the guest `gethostname` declaration for gnulib’s bounded host-name
  wrapper. The probe advanced through timestamp and formatted-I/O support and
  now reaches the host-name declaration seam. The utility remains pre-link;
  browser testing is still deferred.

Implementation update (2026-09-23, sleep, input, and wide-character ABI seam):

- Added `pause` and `getchar`, expanded the guest wide-character interface
  with `wmemchr`, `wmempcpy`, `wcscat`, and `mbsrtowcs`, and completed the
  fnmatch flag aliases used by gnulib.
- Added the `tzset` declaration for the timezone-independent guest time
  implementation. The probe advanced through host-name, sleep, input, and
  wide-character support and now reaches the timezone declaration seam. The
  utility remains pre-link; browser testing is still deferred.

Implementation update (2026-09-23, first complete coreutils compile gate):

- Added the bounded formatted-output declarations `vsnzprintf` and
  `vszprintf`, plus the long-double `frexpl` math declaration required by
  gnulib's `vasnprintf` implementation.
- The `true` utility probe now passes compilation across all selected
  generated headers and gnulib support objects: no missing headers, compiler
  errors, or asyncify symbols remain. This closes the current compile-only
  substage of 8E.
- The next 8E substage is linking `src/true` with the guest CRT/libc and then
  auditing its imports. Browser testing remains deferred until that linked
  Wasm artifact exists.

Implementation update (2026-09-23, executable-link probe and ABI audit wiring):

- Extended `probe-coreutils-utility.py` so a utility probe compiles the WASTE
  CRT, relinks the exact generated target with `_start`, exported memory, and
  explicitly allowed unresolved imports, and records the Wasm artifact plus an
  import-audit report. The probe removes only the selected generated target;
  it does not force a rebuild of the configure tree.
- The first audit attempt showed that the earlier link success was a false
  milestone: without the CRT and `_start` root, `src/true` was a valid but
  empty Wasm module containing only memory. This is a loader/link integration
  issue, not a browser failure.
- Added the next guest pthread and SELinux declarations exposed by the real
  gnulib object set. The current probe now advances into the remaining
  pthread TLS ABI (`pthread_key_t`) seam and is still pre-link. Browser
  testing is not requested yet; it becomes useful only after the report has a
  non-empty linked artifact and a completed import audit.

Implementation update (2026-09-23, first non-empty linked utility and import gate):

- Completed the remaining compile-only pthread TLS declarations needed by the
  selected gnulib object set, including `pthread_key_t` and its accessor
  functions. The `true` target now compiles and links with the WASTE CRT,
  exports `_start` and `memory`, and contains the expected startup imports
  `waste_kernel:startup_v1` and `env:exit`.
- Added those two versioned startup imports to the explicit audit allowlist.
  The captured report now passes with no unknown imports and no Asyncify
  symbols: `build/html-rt/coreutils/utility-probe/true-report.json`.
- This closes the 8E compile/link/import-audit gate for `true`; it does not yet
  prove runtime behavior. Stage 8F must package the linked artifact as a VFS
  executable and exercise its `_start` path before browser evidence is useful.

## Stage 8F: Bring up coreutils in waves

Status: complete (Waves 1 through 4 accepted)

Dependency clarification (2026-09-24):

- The engine virtual-memory plan is a prerequisite handoff, not a plan that
  must close at the same time as Stage 8F.
- `true` has already passed the first handoff gate: compile/link/import audit,
  VFS packaging, Bash PATH execution, and status 0 in the offline browser.
- `false` has passed compile/link/import audit, VFS packaging, and browser
  behavior validation with status 1.
- The VM plan is closed. Implementation ownership is now here for the
  remaining utility waves; no VM-plan work should be duplicated in this plan.

Implementation update (2026-09-23, first coreutils VFS packaging gate):

- Extended the C-engine Bash packaging path to carry `true.wasm` into the
  offline manifest and stage it as executable `/bin/true`. The worker now
  advertises the packaged path through the existing VFS metadata/data plane;
  the startup script and browser page remain self-contained.
- Added a focused `--coreutils-true` browser-runtime probe. It reaches Bash,
  invokes `/bin/true`, and observes the following current result:
  `__C_ENGINE_COREUTILS_TRUE_STATUS_127__`. The transition trace shows the
  fork/exec handoff and child wake completing, so packaging and VFS lookup are
  working; the remaining failure is child startup/exit-status propagation.
- Rebuilt the C engine after adding the diagnostic transition detail and
  fixed two engine-build declaration seams (`realloc` and engine-local
  `off_t`). The ordinary C-engine Bash smoke test still passes. The focused
  coreutils test is intentionally recorded as failing until `/bin/true`
  returns status 0.

The next 8F increment is to identify whether the 127 is produced by the WASTE
CRT startup import, the coreutils `_start`/`exit` path, or parent wait-status
translation. Browser testing is already automated for this gate; no manual
browser evidence is requested until that status becomes 0.

Implementation update (2026-09-23, indirect-call table and guest-libc boundary):

- The child trace identified the 127 precisely: the linked `true` module had a
  `call_indirect` but its element segment was discarded by the link, producing
  `uninitialized element` at `_start`. The utility link probe now preserves
  the indirect-function table and its element segment with
  `--export-table`.
- With the table retained, the loader reaches the next intended ABI check
  instead of trapping. The browser probe now reports `errno 8`/status 126 and
  the transition evidence identifies the first unresolved import as
  `env.getenv`. The import audit lists the remaining guest-libc surface
  (`malloc`, string, stdio, locale, wide-character, errno, and compiler
  runtime functions); these must be linked into the utility or provided by a
  versioned WASTE guest-libc module. They must not be hidden by broadening the
  allowlist.
- This supersedes the earlier claim that 8E was fully closed for `true`: the
  compile gate is complete, but the executable-link gate remains open until
  the table and guest-libc import closure are both reproducible. The focused
  browser test is now exercising the correct preflight rejection path.

Implementation update (2026-09-23, relocatable guest allocator increment):

- The existing `stdlib.wat` allocator was confirmed to be a standalone merged
  guest-libc module, not a relocatable object suitable for a coreutils link.
  The utility probe therefore now compiles the reusable C allocator portion of
  `src/html-rt/lib/stdlib.c` as `coreutils-allocator.o` and links it with the
  WASTE CRT. `--gc-sections` keeps unused host-facing allocator helpers out of
  the image, while `--export-table` retains indirect-call element segments.
- Added the `WASTE_ALLOCATOR_ONLY` build guard around the allocator's optional
  `posix_memalign` and `reallocarray` wrappers. Coreutils/gnulib already
  provides those symbols, so linking them twice was a real duplicate-symbol
  failure rather than an import-resolution problem.
- The reproducible `true` link now succeeds with the allocator object. The
  remaining audit is narrower but still open: 46 unresolved `env:*` imports,
  chiefly stdio, string/wide-character, locale, errno, floating-point helper,
  and process-exit APIs. The probe report now correctly points to 8F while
  this ABI closure is in progress; the previous 8E label was a reporting bug.

This increment is not browser-ready for a success claim. It proves the
allocator/link boundary and prevents the browser loader from seeing the old
uninitialized-table failure, but `/bin/true` must not be packaged as accepted
until its remaining imports are either linked from the guest libc or mapped to
versioned WASTE kernel/host adapters with behavior tests.

The shared browser C engine rebuilt successfully, and the ordinary Bash browser
smoke remains green (5/5). No new manual browser run is needed yet: the
coreutils-focused browser check should be rerun only after the import audit is
closed, at which point it must verify `/bin/true` returns status 0 rather than
merely reaching the diagnostic status marker.

Implementation update (2026-09-23, reproducible coreutils build entry point):

- Added `make -C src/html-rt build-coreutils` as the normal Stage 8F build
  path. Its staged source, sysroot, configure tree, objects, reports, and
  linked utility now live under `build/coreutils/`; the browser engine remains
  under `build/html-rt/`.
- The public target now rejects a `blocked` import audit. The lower-level
  `coreutils-utility-probe` remains useful for collecting the next ABI report,
  but the normal build and Bash packaging path cannot ship an unresolved
  executable.
- A clean-build dependency gap was closed: configure probing now depends on
  `coreutils-stage`, so a fresh `build/coreutils/` creates the staged source
  before bootstrap/configure rather than failing with missing generated
  headers or an absent source tree.
- The bootstrap helper now accepts the Make-selected coreutils build root, so
  staging and bootstrap operate on the same `build/coreutils/source` tree.
  Its legacy `build/html-rt/coreutils` default remains available for direct
  callers that have not selected the new public target.
- The fresh sysroot probe found and fixed a relative toolchain lookup bug: the
  generated compiler wrapper now resolves `build/engine/toolchain` correctly
  from `build/coreutils/sysroot/bin`, and generated gnulib headers are read
  from the selected coreutils build root rather than a hard-coded HTML path.
- The wrapper now passes its direct `--version` smoke check (`clang 22.1.8`,
  target `wasm32`). The fresh build emitted `build/coreutils/source/configure`
  and reached the long configure probe using that wrapper. The configure run
  was interrupted during late feature checks, so its report is not an accepted
  result and the utility gate remains unverified; the next run can reuse the
  staged source and generated configure tree instead of rebuilding bootstrap.
- Coreutils staging is now guarded by an out-of-tree source stamp. Bootstrap
  can resume against the generated source tree without recopying it, avoiding
  the prior `Directory not empty` failure when a long bootstrap/configure run
  is interrupted.
- Removed the remaining phony classification from `coreutils-stage`; otherwise
  Make would correctly preserve the source tree but still rerun the expensive
  configure probe on every utility request. The stamp now provides the normal
  incremental dependency behavior.
- The configure report now depends directly on the stamp file rather than the
  convenience alias target. This prevents GNU Make from treating the alias as
  perpetually newer and rerunning configure during every import-audit request.
- The utility preflight leaves `lib/config.h` to the completed configure pass;
  trying to regenerate it through Make re-enters the staged source's
  `--recheck` path, which is not valid for the intentionally out-of-tree
  configure layout. A completed configure pass must therefore be treated as a
  prerequisite before linking the utility.
- The probe now skips the Make preflight when all required generated headers
  already exist, which is the normal incremental case. Make is only attempted
  to fill genuinely missing generated headers.
- The utility compile target also marks `config.status` as preserved; otherwise
  Make can re-enter configure while resolving an object dependency even after
  the header preflight was skipped.
- After regenerating the staged source tree, compilation advanced to gnulib's
  `btowc.c`. The guest implementation already supplied `mbtowc` and `mblen`,
  but the public `wchar.h` omitted their declarations; those declarations are
  now part of the sysroot ABI.
- With the completed configure output reused, the `true` probe now compiles
  and links reproducibly at `build/coreutils/configure/src/true` with no
  undefined linker symbols. Its import audit remains blocked on the guest
  libc/compiler-runtime closure: the current report records 55 unresolved
  `env:*` imports, including stdio, string, wide-character, locale, errno,
  floating-point helper, and process-exit functions. This is an import/link
  integration task, not another Coreutils source or loader architecture
  blocker; the existing standalone `waste-libc.wasm` cannot simply be merged
  yet because both modules currently define their own linear memory.
- The next 8F increment is therefore to produce a shared-memory guest-libc
  link/merge artifact (or an equivalent relocatable guest-libc object set),
  then rerun the import audit before packaging `/bin/true`. The current
  browser test should not be rerun for acceptance yet: the executable is not
  import-closed, so browser evidence would only reproduce the expected
  unresolved-import failure.
- A direct experiment confirmed why this cannot be solved by simply merging
  the existing `build/html-rt/waste-libc/waste-libc.wasm`: that module exports
  its own linear memory, while a Coreutils executable also owns or imports a
  memory. `wasm-merge` rejects the two-memory result unless multi-memory is
  enabled, which would give the libc and utility different pointer spaces.
  The required implementation is therefore a one-memory link boundary: make
  libc relocatable before the final link, or teach the merge step to rewrite
  the libc memory definition/import and relocate its data and globals. This
  is an implementation seam in the build/link layer, not a reason to broaden
  the loader's import allowlist.
- Added `./start.sh --build-coreutils` and made the Bash-page build pass the
  resulting `build/coreutils/configure/src/true` explicitly to the generator.
  This separates the coreutils product build from the browser runtime build
  while preserving the offline packaging flow.
- The intended runtime result is an executable VFS file `/usr/bin/true`, with
  `/bin/true` retained as a compatibility alias. Typing `true` in Bash should
  resolve `/usr/bin/true` through PATH and invoke `execve`.

Implementation update (2026-09-24, VM handoff and Wave 1 continuation):

- The engine virtual-memory plan is closed and is no longer a blocker for
  this loader plan. Its process-memory interfaces remain available to later
  Coreutils waves; no VM work is duplicated here.
- `true` and `false` remain accepted Wave 1 utilities: both compile, relink,
  pass the explicit import audit, are packaged as `/usr/bin` and `/bin`
  aliases, and return their expected statuses in the offline browser gate.
- `pwd` now also compiles, relinks, and passes the import audit. The existing
  `env:fstat` implementation was added to the probe's explicit runtime-import
  allowlist, and the linked image is packaged under both `/usr/bin/pwd` and
  `/bin/pwd`.
- The focused `pwd` browser gate is not accepted yet. The child reaches the
  image entry point but traps with `call stack exhausted`; Bash also reports a
  `getcwd` startup error. The attempted VFS home-directory and guest
  `getcwd(NULL, 0)` fixes did not remove that runtime failure. This is now a
  bounded runtime/guest-libc investigation, not a Coreutils compile, link, or
  loader-classification blocker. No manual browser test is requested until
  the headless gate reports the expected status 0.

Wave 1 user-directory prerequisite (2026-09-25):

- The virtual shell must not derive its identity, home directory, or cwd from
  the machine that built the browser artifact. The engine-owned startup
  profile for the current shell is UID 0/GID 0 with `root` as the user and
  `/root` as its home directory.
- Install the corresponding guest passwd record through the guest libc
  identity interface: `root:x:0:0:root:/root:/bin/bash`. It must be
  available before Bash initializes its environment so Bash's existing
  `getpwuid(0)` and `getpwnam("root")` paths resolve consistently.
- Stage `/root` as an engine-owned VFS directory and initialize the browser
  shell's process cwd to `/root`. Set `HOME=/root`, `USER=root`,
  `LOGNAME=root`, and `PWD=/root` in the virtual process environment. These
  values are process state, not packaging-time host values.
- Keep `/home/a` out of the production initialization path. Any occurrence
  in a browser fixture must be an explicitly named test input, never an
  inherited host cwd or home directory.
- The `pwd` gate must verify physical cwd output and environment consistency:
  startup prints `/root`, `HOME` prints `/root`, and after `cd /tmp`, `pwd`
  prints `/tmp` while `PWD` tracks the directory change. A child utility must
  inherit the same cwd and identity without changing the parent shell state.

Implementation update (2026-09-25, root startup profile):

- The generated interactive Bash runtime now carries the virtual root
  environment (`HOME=/root`, `USER=root`, `LOGNAME=root`, `PWD=/root`, and the
  virtual PATH) and installs the guest passwd record
  `root:x:0:0:root:/root:/bin/bash` before Bash starts.
- The browser launcher now stages `/root`, submits `/root` as the initial cwd
  through the engine API, and reports `/root` in its VFS inventory. The host
  build directory is no longer used to select the virtual shell home.
- The headless `pwd` gate still does not pass: the generated browser run exits
  before the VFS verification step can observe `/tmp` (the engine reports an
  invalid path-access state and produces no prompt). This is a browser launch
  lifecycle/runtime blocker, not evidence that `/home/a` is still being used.
  No browser test is requested from the user until the headless gate reaches
  the command prompt again.

Implementation update (2026-09-25, environment ABI isolation):

- The launch failure is now narrowed to Bash's environment-vector handoff. A
  one-entry `HOME=/root` vector reaches the interactive prompt, while adding
  the next environment pointer makes Bash trap with `out of bounds memory
  access` before startup. This explains the earlier empty-output/VFS error;
  it is not caused by selecting `/root` or by Coreutils `pwd`.
- The full root environment remains specified in the generator, but the
  browser gate is intentionally still open until the multi-entry `envp`
  representation is corrected and the gate verifies `HOME`, `PWD`, `cd`, and
  the child utility together. The next implementation increment is therefore
  the Bash/guest-libc environment-vector ABI, followed by rerunning the
  existing headless gate.
- The environment-vector generator now derives every pointer from the actual
  encoded entry lengths instead of maintaining hand-counted offsets. The
  generated WAST parses successfully. The Bash startup ABI uses eight-byte
  environment-pointer slots in this merged artifact, so the generator now
  emits four-byte Wasm pointers with four-byte padding per slot and an
  eight-byte terminator. The full root environment reaches the prompt again;
  the prior multi-entry startup trap is resolved.
- The generic headless Bash gate passes with the complete root profile and
  records the expected UID/root startup. The focused `pwd` gate remains open
  separately: the child utility still reaches the known call-stack exhaustion
  path and returns status 127. The next increment is therefore the utility
  child stack/entry-path fix, not environment setup.

Implementation update (2026-09-25, child depth diagnostic):

- The executor now reports the exact managed-call depth when it rejects a
  call-stack transition. The focused `pwd` evidence identifies the failure as
  `call stack exhausted at depth 256` while entering the child image; this is
  an executor/utility entry-depth boundary, not a VFS lookup or root-profile
  failure.
- A trial increase of the global depth limit was reverted because it exposed a
  deeper engine-memory trap rather than producing a valid child continuation.
  The bounded diagnostic is retained, and the next increment is to reduce the
  child entry call depth or introduce a continuation-safe image entry boundary
  before changing the global limit.

Implementation update (2026-09-25, pwd runtime fix — Wave 1 closed):

- Root cause of `call stack exhausted at depth 256`: gnulib's `rpl_fcntl`
  contained an infinite self-recursion.  The `REPLACE_FCNTL=1` config macro
  renamed `fcntl` to `rpl_fcntl` via `#define`, but gnulib's `fcntl.c` still
  compiled a `fcntl()` body that called itself after `#undef`.  Fixed by
  removing `libcoreutils_a-fcntl.o` from `libcoreutils.a` via `ar d` before
  relinking, so the guest libc's kernel-backed `fcntl` wins.
- Second failure: `unreachable` trap in function 343.  The guest libc's
  `exit()` was `__builtin_trap()` inside `coreutils-allocator.o` (compiled
  with `WASTE_ENGINE`).  gnulib's `close_stdout → exit` hit the trap.  Fixed
  by wrapping `exit`/`_exit`/`abort`/`atexit` in `#ifndef WASTE_ALLOCATOR_ONLY`
  so they become `env:exit`/`env:_exit`/`env:atexit` imports resolved by the
  engine.  Added `_exit` and `atexit` handlers in `posix_stubs.c`.
- Third failure: exit status 1 (pwd error).  The exec'd child process had no
  guest libc initialization.  Added `waste_allocator_init`,
  `waste_stdio_init`, and `waste_stdio_bind(0,1,2)` bootstrap calls in
  `browser_api.c`'s exec flow after `native_store_commit_process_image`.
- Fourth failure: exit status 0 but no output.  `waste-libc.wasm` was built
  without `-DWASTE_POSIX_IO`, so `fwrite` buffered to guest memory but never
  called `write()`.  Fixed by changing `build-coreutils-runtime.py` to build
  its own POSIX-IO-enabled libc (matching `build-bash-runtime.py`) rather
  than reusing the base `waste-libc.wasm`.  Also added `fflush(NULL)` before
  `waste_env_exit` in `waste-crt.c`.
- Added `env:read`, `env:write` to the probe's runtime-import allowlist and
  registered `waste_kernel:fchdir_v1`, `waste_kernel:fcntl_v1` in
  `posix_stubs.c`.  Implemented `posix_kernel_fchdir` in `kernel.c`.
- The focused `--coreutils-pwd` headless gate now passes: the child outputs
  `/root`, returns status 0, and Bash reaches a second prompt.  The
  `--coreutils-true` and `--coreutils-false` gates remain green.
- Wave 1 is closed: `true`, `false`, and `pwd` all compile, relink with a
  POSIX-IO-enabled guest libc, pass the import audit, and return expected
  statuses with correct output in the offline browser gate.

Implementation update (2026-09-26, Wave 2 — echo, basename, dirname, printf):

- `echo` and `basename` linked and passed the headless browser gate without
  incident.  `dirname` required extending the CRT with
  `WASTE_MAIN_TWO_ARGS` to match its `main(int, char**)` signature.
- `printf` was the longest debugging effort in Wave 2.  Its crash at address
  0xFFFFFFB4 (signed −76, OOB) persisted across five distinct fixes:
  1.  Extended `format_variadic` with `j/z/t` length modifiers and flag
      characters.
  2.  Provided waste-libc implementations of gnulib's `vfzprintf`, `vzprintf`,
      `vsnzprintf`, `vszprintf`, `vaszprintf`, `aszprintf`, `vasprintf`,
      and `vprintf`, bypassing gnulib's `vasnprintf` chain.
  3.  Stripped nine gnulib `.o` files from `libcoreutils.a` so the waste-libc
      versions win at link time.
  4.  Fixed the runtime module's `__stack_pointer` from 65536 to the
      utility's actual 131072.
  5.  Added `stdin`/`stdout`/`stderr` variable definitions to `waste-crt.c`
      so wasm-ld allocates them at valid addresses (136456–136464) instead of
      address 0.
  None of these fixed the crash.
- Root cause: **dual allocator corruption**.  The probe script compiled a
  standalone C allocator (`coreutils-allocator.o` from `stdlib.c` under
  `-DWASTE_ENGINE -DWASTE_ALLOCATOR_ONLY`) and linked it into the utility.
  After `wasm-merge`, the merged binary had two independent heap allocators —
  the C linked-list allocator and waste-libc's WAT boundary-tag allocator —
  both starting from `__heap_base` and managing separate metadata over the
  same linear memory.  gnulib's `xmalloc` used the C allocator while
  `vfprintf` used the WAT allocator; their interleaved allocations corrupted
  each other's metadata, producing the −76 fault address.
- Fix: removed `coreutils-allocator.o` from the link line.  With
  `--allow-undefined`, `malloc`/`free`/`calloc`/`realloc` become imports
  that `wasm-merge` resolves against waste-libc's single WAT allocator.
  Added `getenv` and `secure_getenv` to the guest libc (previously only in
  the `WASTE_ENGINE` build).
- Secondary fix: corrected the `off64_t` typedef in both the sysroot
  (`build/coreutils/sysroot/include/sys/types.h`) and waste-libc
  (`src/html-rt/lib/stdio.c`) from `signed long` (32-bit on wasm32) to
  `signed long long` (64-bit).  gnulib defines `off64_t` as `long long`;
  the mismatch caused silent return-type truncation for the `*zprintf`
  family.
- Build ordering fix: gnulib strip must happen AFTER `make` rebuilds
  `libcoreutils.a` (make recreates the archive from all `.o` files, undoing
  any earlier `ar d`).  The probe script now runs make first, strips the
  conflicting objects, then manually relinks.
- All seven coreutils headless gates pass: `true`, `false`, `pwd`, `echo`,
  `basename`, `dirname`, `printf`.  The baseline Bash smoke test remains
  green.

Implementation update (2026-09-26, Wave 2 build-entry integration):

- The normal `src/html-rt/Makefile` path now has explicit probe targets for
  `echo`, `printf`, `basename`, and `dirname`, in addition to the existing
  true/false/pwd targets.  `build-coreutils` and the documented
  `coreutils-wasm`/`coreutils-audit` entry points cover all seven accepted
  utilities instead of silently checking only Wave 1.
- The offline Bash package builder now stages all seven linked files from the
  generated page directory.  A missing utility is a packaging error, so a
  page cannot appear complete while its command is absent from the archive.
- This closes the Wave 2 build-graph gap.  Stage 8G is still open for the
  complete `/bin/wat` and `/bin/wast` package, license/source provenance
  files, deterministic archive audit, and the final distribution gate; no
  browser action is required for this Makefile-only increment.

Implementation update (2026-09-26, Wave 3 `cat` acceptance):

- `cat` first exposed missing public ABI declarations for `ENOTTY`, `ETXTBSY`,
  `EFBIG`, `getpagesize`, `copy_file_range`, and `splice`. The optional
  zero-copy and pipe calls now return `ENOSYS`, causing Coreutils to use its
  ordinary read/write fallback until those kernel accelerators are available.
- Corrected the merged libc signatures for `posix_fadvise`, `__fpurge`, and
  the `*zprintf` family. Added a versioned `waste_kernel:lseek` bridge so the
  utility-facing 32-bit `off_t` ABI does not bind directly to the engine's
  internal 64-bit seek callback.
- Removed gnulib's `open.o` replacement after each archive rebuild. Like the
  earlier `fcntl.o` case, its replacement called itself in this freestanding
  target instead of reaching the guest kernel.
- Coreutils images now request a 1 MiB initial memory, and the shared runtime
  adopts each utility's declared memory minimum. This avoids constraining
  data-heavy utilities to the earlier four-page bootstrap image.
- The browser hang was traced to configure selecting `posix_memalign` even
  though waste-libc advertises only 16-byte alignment. The WASTE config site
  now reports that function unavailable, allowing gnulib's portable
  over-allocation shim to satisfy `cat`'s 64 KiB page-aligned buffers without
  falsifying the WebAssembly page size.
- `cat` now compiles, relinks into the shared-memory image, passes its import
  audit, reads a packaged engine-VFS file with `-n`, writes the expected bytes,
  returns status 0, reaches a later Bash prompt, and exits cleanly in the
  headless browser gate. Its Make target, normal build list, generated page,
  deterministic package audit, and corresponding-source mapping are wired.
- The corresponding-source archive now covers eight accepted utilities and
  has SHA-256
  `4f8cfb7191af9cf54d490280acde10aff27ac2f2cf91206943ed29aea40c6265`.
  Wave 3 continues with `wc`; no manual browser test is required for this
  increment.

Implementation update (2026-09-26, Wave 3 `wc` acceptance):

- `wc` compiles from the pinned GNU source, relinks into the shared-memory
  runtime, and passes its import audit with no unknown imports or Asyncify
  symbols. It is now covered by the normal Make targets, `start.sh` build
  list, page generator, package audit, and corresponding-source mapping.
- Its first browser execution exposed a genuine stack underflow: GNU `wc`'s
  counting frame exceeded the old 128 KiB linker stack and wrapped its read
  buffer to an address near `UINT32_MAX`. Coreutils applications now reserve
  a 512 KiB stack while retaining the bounded 1 MiB initial memory.
- The one-memory relinker now reads the utility's declared memory minimum
  before building the libc participants. The allocator core, C libc helpers,
  rewritten utility, and engine-owned runtime therefore agree on the same
  minimum instead of hard-coding a four-page helper memory that cannot hold a
  larger utility stack and static data.
- The aggregate rebuild also corrected the manual link object manifests for
  GNU `true` and `false`: both use `true.o` plus their respective
  `true-true.o` or `true-false.o` selector. This preserves their exact 0 and 1
  statuses in clean builds rather than relying on an unresolved selector.
- The focused browser gate reads an engine-VFS fixture and verifies GNU
  `wc -l -w -c` output `2 3 14`, status 0, a later Bash prompt, and clean
  exit. All eight earlier focused Coreutils gates and the baseline Bash gate
  remain green after the larger-stack relink.
- The aggregate nine-utility build and import gate passes, and the
  deterministic browser package audit includes `wc.wasm`. The corresponding
  source archive now covers all nine accepted utilities and has SHA-256
  `526fc106e4bca3027bdf399d603c7fd9d282cce60f8225740da7cde045352c22`.
  Wave 3 is closed; Wave 4 begins with `ls`. No manual browser test is needed
  for this increment.

Implementation update (2026-09-27, Wave 4 `ls` acceptance):

- `ls` compiles from the pinned GNU source, relinks into the one-memory guest
  runtime, and passes its import audit with no unknown imports or Asyncify
  symbols. The managed Coreutils patch removes only the unavailable
  stack-restoring `setjmp` collation fallback for WASTE, selects the documented
  bytewise locale ordering, and resets the utility mode for repeat process
  images; the submodule remains clean.
- Completed the VFS metadata boundary needed by directory presentation:
  directory entries use the guest ABI layout, stat modes carry file-type bits,
  and symlink metadata plus `readlink` reach the utility through the versioned
  kernel interface. Explicit host-binding precedence is limited to the Bash
  `lseek` and `__fpurge` compatibility shims whose signatures differ from the
  registered guest-libc exports.
- Shell redirection now uses engine-backed `dup` and `dup2`, `open` returns the
  POSIX `-1`/guest-errno contract, and pathname/file changes made by a forked
  child are published to the waiting parent before the child is reaped. The
  latter is the current process-lifecycle synchronization point; it does not
  claim a general shared-mount-namespace implementation for concurrently
  running processes.
- The generated `ls-report.json` includes the accepted feature ledger and
  explicit deviations. Supported behavior covers `/`, `/bin`, empty and
  hidden directories, symlinks, long metadata, terminal columns, redirected
  one-name-per-line output, multiple operands, missing-path status 2, and a
  later shell command. Locale collation is bytewise; color and locale-specific
  quoting are outside this accepted subset; account names and host timestamps
  are not fabricated.
- The same fixture shapes were checked against a native build from pinned
  source commit `cecd945aa93ab77e759fe766206cfe93e634d07b`: ordering, terminal
  and non-terminal layouts, symlink presentation, multiple operands, and
  missing-path status match. Native account names and filesystem times differ
  only in the fields identified by the deviation ledger.
- Runtime crash: the linked `ls` binary hit `call stack exhausted at depth
  256` on its first invocation.  Root cause was gnulib's `rpl_localeconv`
  wrapper — the same infinite-recursion pattern as `rpl_fcntl` in Wave 1.
  gnulib's `localeconv.c` `#undef`s `localeconv` and calls the "real" system
  function, but in Wasm there is no separate system implementation so the call
  resolves back to the wrapper itself.  Fixed by adding
  `libcoreutils_a-localeconv.o` to the gnulib strip list in
  `probe-coreutils-utility.py`, so the waste-libc `localeconv` wins at link
  time.  The same strip-list pattern now covers `fcntl`, `open`, `stat`,
  `lstat`, `fstatat`, `localeconv`, and the seven `*zprintf` variants.
- The final headless browser matrix passes 12/12 (baseline Bash, missing
  command, and all ten coreutils utilities) and returns status 0 after the
  representative status-2 failure. Native warnings-as-errors sanitizer gates
  also pass: the executor smoke gate is green and the POSIX kernel reports
  329 tests. Wave 4 and Stage 8F are closed; no manual browser test is needed.

Build and accept utilities in dependency order. Do not enable the next wave by
silently disabling failures in the current one.

### Wave 1: process and startup

- `true` — accepted
- `false` — accepted
- `pwd` — accepted

Gate: direct absolute invocation and PATH lookup return exact statuses, the
engine-owned root profile is visible as `/root`, `pwd` tracks `cd`, and Bash
reaches a second prompt after every command. The gate must also prove that no
host `HOME`, `PWD`, uid, gid, or cwd value enters the virtual process.

### Wave 2: output and path strings

- `echo` — accepted
- `printf` — accepted
- `basename` — accepted
- `dirname` — accepted

Gate: invoke external utilities by absolute path where Bash has a builtin;
cover empty operands, option terminators, escapes, and non-ASCII bytes within
the documented locale model.

### Wave 3: regular-file data

- `cat` — accepted
- `wc` — accepted

Gate: cover packaged files, writable `/tmp` files, standard input, standard
output redirection, seekable and non-seekable descriptors, errors, and binary
data containing NUL bytes.

### Wave 4: directory presentation

- `ls` — accepted

Gate: cover `/`, `/bin`, empty directories, hidden names, symlinks, long
format metadata, terminal/non-terminal output differences, missing paths, and
multiple operands. Unsupported owner, locale, color, or timestamp details must
be explicit rather than fabricated.

For every accepted utility:

- compare the supported option subset and output with the pinned native GNU
  utility using deterministic fixtures;
- store its import audit and feature ledger under the generated build tree;
- test exit 0, representative nonzero status, and a second Bash command;
- record intentional deviations without describing them as POSIX conformance.

## Stage 8G: Package the utilities and satisfy distribution requirements

Status: complete (ten utilities, interpreters, notices, provenance, and
corresponding source are covered by the deterministic package audit)

Implementation update (2026-09-26, Stage 8G package metadata):

- `build-coreutils` now generates the machine-readable Coreutils provenance
  report as part of the required build, so a successful utility build always
  has source commit, managed-patch, submodule, and license-file metadata.
- The Bash archive builder now includes that report at
  `/usr/share/waste/coreutils-provenance.json` and the upstream `COPYING` file
  at `/usr/share/licenses/coreutils/COPYING`. Missing metadata is a hard
  packaging error rather than a silently incomplete release.
- The browser worker registers the corresponding `/usr/share/licenses` and
  `/usr/share/licenses/coreutils` directories before staging packaged files.
  The metadata therefore remains visible through the engine-owned VFS, while
  JavaScript continues to transport bytes only.
- The remaining Stage 8G work is to package the executable `/bin/wat` and
  `/bin/wast` handler nodes, include the complete source-access bundle, and
  run the deterministic archive/license audit. This increment does not require
  a manual browser test.

Implementation update (2026-09-26, Stage 8G interpreter package contract):

- The generated C-engine Bash page now carries the two engine-owned handler
  paths in its VFS metadata: `/bin/wat` and `/bin/wast`, both executable. It
  also registers `/usr`, `/usr/bin`, `/usr/share`, and the license directory
  hierarchy before loading package files.
- The monolithic page payload now includes the provenance report and
  Coreutils `COPYING` bytes at their final VFS paths. The page generator fails
  early if either required distribution file is absent, so a browser artifact
  cannot claim the Stage 8G package contract without its notices.
- A generated package check confirmed that the page contains both handler
  paths and both metadata paths. The remaining work is the complete source
  access bundle and deterministic archive/license audit; the handlers
  themselves are now represented in the generated package contract.

Implementation update (2026-09-26, Stage 8G deterministic package audit):

- Added `src/html-rt/tools/audit-coreutils-package.py`. It checks the offline
  archive for all seven utility images, provenance, `COPYING`, and an explicit
  interpreter manifest listing `/bin/wat` and `/bin/wast`. It also rejects
  unsorted entries, nonzero ownership metadata, names, or timestamps.
- `src/html-rt/tools/build.sh` now creates a normalized ustar archive with
  stable ordering, epoch timestamps, numeric root ownership, and a no-name
  gzip header before amalgamation. The audit runs before the manifest is
  embedded in the HTML page.
- The same Bash page was built twice and produced the identical SHA-256
  digest, `b83b7f72f21ce29bdeb832cfe775654ef7f77a1a0a7db0d6987f7bd94513a16a`.
- Stage 8G still needs the complete corresponding-source bundle and release
  license mapping; the browser package archive and deterministic audit are now
  closed.

Implementation update (2026-09-26, Stage 8G corresponding source):

- Added `src/html-rt/tools/package-coreutils-source.py` and the
  `coreutils-source-package` Make target. They create and audit
  `build/coreutils/coreutils-corresponding-source.tar.gz` from the exact
  patched/bootstrapped source tree used by the build.
- The source archive also contains the managed patch, bootstrap and configure
  inputs, generated configure/provenance reports, WASTE engine and browser
  runtime source, guest libc and interface headers, build tools, notices, and
  `docs/coreutils-source-distribution.md` instructions. Generated Wasm and
  Python bytecode are excluded.
- `coreutils-source-package.json` maps all seven accepted utility binaries to
  the archive, pinned source commit, instructions, and SHA-256. The browser
  package installs that mapping under `/usr/share/waste`, and both the page
  generator and package audit reject a missing or incomplete mapping.
- The normal C-engine Bash generation path now builds the corresponding-source
  artifact before producing the distributable page. Repository source inputs
  invalidate its Make stamp, preventing a stale archive after runtime or build
  script changes.
- Two independent builds from the same inputs produced SHA-256
  `595211955a5eec2007baa801bde57fe93796e96564e4219fb6e6454191929856`.
  Distribution requirements are complete for the currently accepted utility
  set. Stage 8G remains open only because its final `/bin` inventory includes
  `ls`, which belongs to the remaining Stage 8F wave.

Implementation update (2026-09-26, nine-utility package inventory):

- Added `wc.wasm` to the deterministic package contract and extended the
  source-package mapping from eight to nine accepted utilities.
- `make -C src/html-rt BUILD_DIR=../../build/html-rt
  coreutils-package-audit` now rebuilds and audits all nine reports, generates
  the corresponding-source archive, requires every utility in the offline
  tar manifest, and produces the self-contained Bash page successfully.
- The only utility still missing from the planned final inventory is `ls`.

Implementation update (2026-09-27, final ten-utility package inventory):

- Added `ls.wasm` to the normal build, page generation, VFS inventory,
  deterministic archive audit, and corresponding-source mapping. The package
  audit now requires all ten accepted utilities plus `/bin/wat`, `/bin/wast`,
  provenance, source mapping, and the Coreutils license notice.
- `make -C src/html-rt BUILD_DIR=../../build/html-rt
  coreutils-package-audit` passes. The final corresponding-source archive maps
  the pinned source, managed patch, WASTE runtime/build inputs, and all ten
  binaries with SHA-256
  `307962944ac6006511fd2a30d7345094040ca08a49d6df0126a48fa33575ac77`.
- The rebuilt self-contained `file://` Bash page passes its baseline and
  process-continuation browser gates, and its focused `/bin/ls` gate observes
  the packaged `/bin` inventory. Stage 8G is closed without modifying the
  Coreutils submodule.

Work:

- Install Wasm binaries as normal executable files named `/bin/true`,
  `/bin/false`, and so on. Do not require `.wasm` in command names.
- Install `/bin/wat` and `/bin/wast`, license notices under
  `/usr/share/licenses`, and a generated build/provenance manifest under
  `/usr/share/waste`.
- Feed all installed files through the existing offline tar manifest so the
  worker stages them into the engine-owned VFS. JavaScript remains packaging
  transport, not executable lookup or POSIX state.
- Preserve mode bits, deterministic path ordering, normalized timestamps, and
  stable ownership metadata in the generated archive.
- For every distributed browser artifact containing GPL-covered binaries,
  provide the exact corresponding source through the chosen GPLv3-compliant
  distribution method. The pinned gitlink alone is not the release checklist:
  include the managed patch, build scripts, configure answers, interface
  definitions, notices, and clear source-access instructions.
- Audit copied sysroot headers and linked support code separately; do not
  assume every coreutils or gnulib file has the package-level license.

Gate:

- `ls /bin` reports the selected coreutils plus `wat`, `wast`, and the WASTE
  probe from the engine VFS.
- The generated page makes no network request and can execute every installed
  utility through `file://`.
- Rebuilding the package does not modify either the coreutils submodule or
  repository-owned generated output outside `build/`.
- A release-license audit can map every shipped binary and header-derived
  component to source, patch, build instructions, and notices.

## Stage 8H: Integrate commands and regression gates

Status: in progress (entry points and focused fixtures exist; unified command
matrix and durable documentation consolidation remain)

Implementation update (2026-09-27, Stage 8H handoff):

- The stable `coreutils-wasm` and `coreutils-audit` Make entry points,
  `./start.sh --build-coreutils`, offline Bash rebuild, native loader tests,
  and focused browser utility switches are in place. Stages 8F and 8G no
  longer block integration.
- The next increment is one aggregate browser command matrix covering the
  accepted utilities and WAT/WAST handlers in a single shell lifetime, plus
  consolidation of the now-stable loader, package, and Wasm32 ABI rules into
  the durable architecture documents. It must retain the existing individual
  gates so failures still identify one boundary.

Add focused build entry points without changing the existing command meanings:

- `make -C src/html-rt BUILD_DIR=../../build/html-rt coreutils-wasm`
- `make -C src/html-rt BUILD_DIR=../../build/html-rt coreutils-audit`
- a sectioned `start.sh` action for building coreutils and rebuilding the
  offline Bash page;
- a native image-loader fixture and a browser coreutils/runtime fixture.

The browser fixture must run at least:

```sh
/bin/true
/bin/false
/bin/pwd
/bin/cat /usr/share/waste/launch.wast
/bin/ls /bin
printf '(module (func (export "_start")))\n' > /tmp/empty.wat
/bin/wat /tmp/empty.wat
```

Add separate checked-in WAT/WAST fixtures for shebang execution; do not depend
on shell `printf` preserving a large test program.

Gate:

- Native warnings-as-errors ASan/UBSan tests pass for VFS loading, shebangs,
  text formats, process replacement, and all new kernel boundaries.
- Browser Bash runs the required command matrix, checks stdout/stderr and
  statuses, reaches a later prompt, and exits cleanly.
- `audit-wasm-imports.py` passes for every application and the guest libc.
- Python bytecode checks, shell syntax checks, Node syntax checks, and
  `git diff --check` pass.
- Durable loader and packaging rules are consolidated into
  `docs/architecture.md`, `docs/techniques.md`, and `docs/wasm32-abi.md` as
  they become implemented.

## Completion criteria

Stage 8 is complete when:

- `execve` loads executable bytes from the engine VFS and no production path
  depends on a separate executable-byte registry;
- binary Wasm executes by magic with or without a `.wasm` suffix;
- executable `.wat` and `.wast` files work directly and through
  `#!/bin/wat` and `#!/bin/wast`;
- `/bin/wat`, `/bin/wast`, and the accepted GNU coreutils are visible and
  executable in the packaged VFS;
- the coreutils submodule remains pinned and clean, with every WASTE source
  change represented by `submodules/coreutils-waste.patch`;
- build, import, behavior, reproducibility, and licensing gates pass for the
  offline browser artifact; and
- the parent shell plan can proceed to general pipelines and scheduling
  without reopening executable loading or coreutils bootstrap design.
