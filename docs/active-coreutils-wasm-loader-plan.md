# WASTE image loader and GNU coreutils plan

Status: active (design complete; implementation pending)

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

Status: pending

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

## Stage 8B: Make VFS bytes authoritative for `execve`

Status: pending

Replace the current path-keyed executable byte registry with a loader whose
input is the regular file reached through the engine VFS. The registry may
remain as an immutable decoded-template cache, but it must not contain a second
copy of executable contents that can disagree with the VFS.

Work:

- Add a kernel operation that resolves a path with symlink-loop protection,
  verifies a regular executable node, and returns a bounded immutable byte
  snapshot plus inode, mode, size, and content generation.
- Resolve relative paths against the calling process cwd. PATH lookup remains
  Bash's responsibility; the `execve` boundary receives one path.
- Key validated-module caching by VFS object identity and content generation
  or content hash. Writes, truncation, rename, and replacement must not reuse a
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

## Stage 8C: Add shebang, WAT, and WAST image handling

Status: pending

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

## Stage 8D: Build a WASTE target sysroot and CRT

Status: pending

Work:

- Generate a target sysroot under `build/html-rt/coreutils/sysroot` from the
  selected headers; no generated sysroot file is a repository source of truth.
- Define the target tuple and compiler wrapper for `wasm32-unknown-unknown`
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

## Stage 8E: Close the utility-facing libc and POSIX ABI

Status: pending

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

## Stage 8F: Bring up coreutils in waves

Status: pending

Build and accept utilities in dependency order. Do not enable the next wave by
silently disabling failures in the current one.

### Wave 1: process and startup

- `true`
- `false`
- `pwd`

Gate: direct absolute invocation and PATH lookup return exact statuses, `pwd`
tracks `cd`, and Bash reaches a second prompt after every command.

### Wave 2: output and path strings

- `echo`
- `printf`
- `basename`
- `dirname`

Gate: invoke external utilities by absolute path where Bash has a builtin;
cover empty operands, option terminators, escapes, and non-ASCII bytes within
the documented locale model.

### Wave 3: regular-file data

- `cat`
- `wc`

Gate: cover packaged files, writable `/tmp` files, standard input, standard
output redirection, seekable and non-seekable descriptors, errors, and binary
data containing NUL bytes.

### Wave 4: directory presentation

- `ls`

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

Status: pending

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

Status: pending

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
