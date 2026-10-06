# Plan: WASTE Shared Library System (dlopen, ncurses, rogue)

Status: active — Rogue/ncurses, packaged `ldd`, and repeated-process browser
integration are complete. An explicit guest `dlopen`/`dlsym`/`dlclose`
fixture remains before plan closure.

## Current integration result (2026-09-30)

The self-contained `build/html-rt/bash.html` now installs and executes the
shared-library fixture with this VFS layout:

```
/bin/rogue                       executable Wasm image
/usr/bin/rogue                   executable Wasm image
/bin/ldd                         dependency-report executable
/usr/bin/ldd                     dependency-report executable alias
/lib/libncurses.so.wasm          PIC shared object
/usr/lib/libncurses.so.wasm      PIC shared object
```

There should not be a `/lib/rogue`: Rogue is the executable and ncurses is its
shared library. `/usr`, `/usr/bin`, `/usr/lib`, and `/lib` are directories.
Tar directory members are ignored by the browser package loader so a `usr/`
archive entry cannot replace `/usr` with a regular file.

The headless Bash acceptance test runs `ls -ld` over the relevant package
directories and files, verifies the ordinary Bash environment as
`HOME=/root`, `USER=root`, `LOGNAME=root`, `PWD=/root`,
`PATH=/bin:/usr/bin`, and `TERM=xterm`, and executes
`/bin/ldd /bin/rogue`. It requires `ldd` to report
`libncurses => /usr/lib/libncurses.so.wasm` and status 0 before launching
`/bin/rogue`. It then runs two independent Rogue children. For each child it
observes ncurses alternate-screen output and a rendered dungeon, exercises an
arrow and a guaranteed game turn, sends the quit interaction, returns to Bash,
and verifies Rogue status 0. This test is a required part of
`./start.sh --html-bash`.

The final loader defects found during this integration were:

- PIC executable GOT entries were allocated but not patched. GOT relocation
  now runs for both executables and shared objects.
- `GOT.func` references to an executable's ordinary imported function (Rogue's
  `exit` function pointer) were not representable. The loader now installs the
  executable's imported function index in the shared table.
- PIC data and function table ranges overlapped resident Bash/libc state.
  Rogue now starts data at page 5 and indirect functions at table slot 1024;
  DSOs use engine-owned high virtual-memory pages below the stack guard.
- Shared-object data relocations and constructors were not sequenced fully.
  `__wasm_apply_data_relocs` now runs after GOT patching and before
  `__wasm_call_ctors`.
- Registered providers could resolve to another process's module instance.
  dependency and symbol resolution now prefer the active process capsule's
  library engines and process-local cloned providers.
- Browser image startup attempted to call the resident libc's
  `waste_stdio_bind(0, 1, 2)` for dynamically linked images. Binding is now
  performed only when the executable itself exports slot addresses; Rogue's
  CRT obtains the resident streams through `waste_stdin/out/err`.
- Bash's initial `envp` vector used an eight-byte stride even though wasm32
  pointers are four bytes. The zero padding terminated the environment after
  `HOME`; packed four-byte pointers now expose `USER`, `LOGNAME`, `PATH`, and
  `TERM` to ordinary child processes.
- The guest allocator initially started at the module's `__heap_base`, below
  the startup block stored at the top of initial linear memory. A large `ldd`
  allocation overwrote argv/envp and later corrupted the resumed parent.
  Executed images now initialize their heap after the aligned startup-block
  end.
- Dependency table slots were allocated before reserving the executable's
  static table range. Rogue's element segment at slot 1024 could overwrite
  ncurses callback slots, trapping in `initscr` with an indirect-call type
  mismatch. The loader now grows the process table to the executable's
  imported minimum before assigning dependency slots.
- The canvas VT model omitted CSI `REP` and treated the final byte of
  `ESC ( B` as printable text. Rogue ran correctly, but compact ncurses room
  borders were visibly shortened and stray `B` cells could appear. The model
  now implements repeated-character drawing and consumes character-set
  designators, with a Rogue room-border regression in the normal HTML gate.
- Rogue's legacy daemon API stores unprototyped `void (*)()` callbacks but
  invoked every callback with an integer argument. Native C ABIs tolerate that
  undefined mismatch; WebAssembly `call_indirect` requires the target and call
  site signatures to match exactly. A movement turn therefore trapped in
  `do_daemons` when it called the argument-less `runners` daemon as
  `void (int)`. The repository-owned `submodules/rogue-waste.patch` now invokes
  daemon callbacks as `void (void)` and gives the sole argument-using callback,
  `turn_see(TRUE)`, an argument-less adapter. The required browser fixture sends
  ncurses application-mode ArrowUp (`ESC O A`), observes another input wait,
  sends `.` to guarantee daemon processing even if the arrow faced a wall,
  observes a further wait, then quits and verifies a usable Bash prompt and
  status 0.
- Process clones also shallow-share immutable decoded function/type/export
  tables. Those tables now carry a reference count so releasing the source
  executable engine cannot leave a surviving clone with dangling metadata; the
  native sanitizer fixture releases the source before invoking the clone.
- Reaping the first Rogue child left its process-loaded ncurses registration
  in the store and left the DSO's data pages mapped. A later fork incorporated
  that stale provider into its clone graph, and its ncurses preflight failed
  with `ENOEXEC` while reserving two pages. `waitpid` cleanup now runs DSO
  destructors, unmaps process-exit library pages, removes exact store
  registrations, and releases process-owned library engines. Sparse page-table
  capacity beyond a new DSO range is reused rather than treated as an invalid
  shrinking reservation. The required browser fixture locks this down by
  running and interacting with Rogue twice in one Bash lifetime.

The ncurses terminal-size path was also verified during diagnosis:
`isatty()` succeeds and `ioctl(TIOCGWINSZ)` returns 24x80. The apparent 64x64
screen was unrelated to `ioctl`; unpatched Rogue GOT entries were reading
resident data at address zero.

### Remaining closure work

- Add an acceptance fixture that calls guest `dlopen`, `dlsym`, and `dlclose`
  explicitly rather than relying only on executable dependency auto-loading.
- Page/table reclamation on final `dlclose` is deferred to a follow-up virtual
  range allocator task. POSIX does not require immediate address reuse;
  current unload runs destructors and drops the engine, while process exit
  releases the complete address space.

## Context

The [browser terminal contract](architecture.md#browser-terminal-contract)
requires a guest ncurses fixture. Rather than
statically linking ncurses into a single binary, the user wants a proper shared
library system: ncurses compiled as a `.wasm` shared object, placed in `/usr/lib`
in the VFS, dynamically loaded via `dlopen`, with `ldd` able to report
dependencies. This also sets the stage for moving libc itself into a proper
shared library and supporting additional programs like rogue.

The project already has a multi-module WebAssembly linking system where
`waste-runtime` owns shared memory/table and multiple modules (waste-libc, bash,
coreutils) resolve imports against registered modules. The new work extends this
from build-time static multi-module bundling to runtime dynamic loading.

## No hard blockers

The research identified no fundamental blockers:

- **PIC code generation**: `wasm-ld -shared` produces position-independent
  modules with `__memory_base`/`__table_base` global imports — the standard
  approach (same as emscripten's dynamic linking).
- **Shared memory/table**: Already works via `waste-runtime` module.
- **Import resolution**: `native_load_module()` already resolves imports against
  registered modules and the host resolver — just needs library modules added to
  the registry at load time.
- **VFS loading**: `posix_kernel_path_snapshot()` already loads binary file
  contents from VFS paths — same mechanism used for executable loading.
- **Cross-module calls**: Already supported via `native_linked_call` trampolines.

The main **technical challenges** (significant work, not blockers):

1. **GOT resolution**: `wasm-ld -shared` generates `GOT.mem.*` and `GOT.func.*`
   imports for cross-module data/function address references. The loader must
   resolve these by looking up symbols in exporting modules.

2. **Import module naming**: `wasm-ld` puts unresolved symbols under module
   `"env"` by default. The build must assign correct module names (e.g.,
   `"libncurses"`) so the loader knows which library provides each symbol. This
   needs either linker stub files or post-link import rewriting.

3. **Memory/table region allocation**: Each dynamically loaded library needs
   heap and table regions allocated at load time. The engine's existing region
   allocator (`native_process_region`) can be extended for this.

4. **ncurses terminal assumptions**: ncurses uses terminfo database lookups and
   POSIX termios. The existing guest `termcap.c` has hardcoded VT100/ANSI
   capabilities; this needs to satisfy ncurses's `setupterm`/`tparm`/`tputs`
   calls.

## Implementation phases

### Phase 1: Engine-side dynamic loader — IMPLEMENTED

**Status**: Core implementation complete.  Compiles clean (`clang --target=wasm32`
syntax check passes).  Dylink parser verified with ASan/UBSan test fixture
(`tests/c-engine-shared-lib-dylink.c`, 6/6 checks pass).

**Files modified**: `src/engine/store.h`, `src/engine/store.c`

**Data structures added** (`store.h`):

- `native_loaded_library` — per-library record with name, path, engine pointer,
  memory/table base and size, ref count, initialized flag.
- `native_dylink_info` — parsed `dylink.0` section fields: memory_size,
  memory_alignment, table_size, table_alignment.
- `native_library_load_context` — transient context set on the store during
  `native_load_module()` to resolve PIC globals (`__memory_base`,
  `__table_base`, `__stack_pointer`).
- `NATIVE_PROCESS_REGION_LIBRARY` region kind added to
  `native_process_region_kind`.
- `loaded_libraries`, `loaded_library_count`, `loaded_library_capacity` fields
  added to `native_process_capsule`.
- `library_load_ctx` field added to `native_store`.

**Functions implemented** (`store.c`):

- `native_parse_dylink(bytes, size, info)` — walks wasm sections looking for
  `dylink.0` custom section, extracts WASM_DYLINK_MEM_INFO subsection.
- `native_store_resolve_library(store, name, path_out, path_size)` — searches
  `/usr/lib/` and `/lib/` with `.wasm` and `.so.wasm` suffixes via
  `posix_kernel_path_stat`.
- `native_store_find_library(store, name)` — looks up an already-loaded library
  by name in the active process capsule.
- `native_store_load_library(store, path, error)` — full load pipeline: VFS
  snapshot → dylink parse → region allocation → library load context setup →
  `native_load_module()` → `__wasm_call_ctors` → store registration → capsule
  bookkeeping.
- `native_store_unload_library(store, index)` — ref-count decrement,
  destructor invocation, module deregistration, and engine release. Virtual
  memory/table range reclamation remains deferred until process exit.

**Import resolution extended** (in `native_load_module()`):

- When `library_load_ctx.active`, resolves `__memory_base`, `__table_base`,
  and `__stack_pointer` global imports from the context rather than from
  registered modules.
- `GOT.func` and `GOT.mem` module imports are represented as mutable i32
  globals and patched after instantiation. Data symbols receive their
  process-local virtual address; function symbols receive a shared-table
  index. PIC executables are patched by the same path as shared objects.

**Test fixture**:

- `tests/c-engine-shared-lib-trivial.c` — trivial PIC source (add, mul,
  increment with static counter).
- `tests/c-engine-shared-lib-trivial.so.wasm` — pre-built PIC binary with
  `dylink.0` section (memorysize=4, alignment=2, tablesize=0).
- `tests/c-engine-shared-lib-dylink.c` — native test that reads the binary
  and verifies dylink parser output.

**Completed during Rogue integration**:

- Executable imports may name non-runtime library providers.
- Executable dependencies are discovered and loaded before executable
  instantiation.
- Cross-module GOT data/function entries are patched, including addresses of
  ordinary functions imported by a PIC executable.
- Unload invokes destructors and frees the library engine. Range reclamation
  remains a documented follow-up.

**Fixed in Phase 2**: Table space allocation during library loading.  The
`__table_base` global is now set to the old table size after growing the shared
table by `dylink.table_size`, instead of hardcoding to 0.

### Phase 2: Guest dlopen/dlsym/dlclose — IMPLEMENTED

**Status**: Core implementation complete.  Compiles clean (`clang --target=wasm32`
syntax check passes on all modified files).  Existing dylink parser test still
passes (6/6 checks).

**Files modified**: `src/html-rt/posix_stubs.c`, `src/html-rt/lib/dlfcn.c`

**Files created**: `src/html-rt/lib/include/dlfcn.h`

**Also fixed** (`src/engine/store.c`): Table space allocation during library
loading — `native_store_load_library` now grows the shared table by
`dylink.table_size` and sets `__table_base` to the old table size instead of
hardcoding it to 0.  This was listed as Phase 1 deferred work.

**Host functions added** (`posix_stubs.c`):

- `native_posix_dlopen_v1(path_ptr, path_len, flags) -> handle` — reads a
  library path or name from guest memory, resolves it via
  `native_store_resolve_library` (searches `/usr/lib` and `/lib`), then loads
  via `native_store_load_library`.  Returns a 1-based handle into the capsule's
  loaded library list, or 0 on failure.  If the path is absolute it is used
  directly; otherwise the search path is consulted.  Already-loaded libraries
  get a ref-count bump and return the existing handle.

- `native_posix_dlsym_v1(handle, name_ptr, name_len) -> address` — looks up
  a symbol name in the library engine's export table.  For function exports:
  searches the shared indirect function table for an existing entry matching
  the library engine and function index; if not found, grows the table and
  installs a new entry.  Returns the table index (usable as a wasm function
  pointer via `call_indirect`).  For global exports: returns the global's i32
  value, which in PIC modules is already a relocated memory address
  (`__memory_base + offset`).  Returns 0 on failure.

- `native_posix_dlclose_v1(handle) -> status` — delegates to
  `native_store_unload_library`.  Returns 0 on success, negative errno on
  failure.

**Dispatch wiring** (`native_posix_function`, waste_kernel block):

Three new entries: `dlopen_v1`, `dlsym_v1`, `dlclose_v1`.

**Guest dlfcn.c** (`src/html-rt/lib/dlfcn.c`):

Replaced ENOSYS stubs with implementations that import waste_kernel host
functions via `__attribute__((import_module("waste_kernel"), import_name(...)))`.
- `dlopen` — reads path string, calls `__waste_dlopen`, converts handle to
  opaque `void *` (1-based i32).
- `dlsym` — reads symbol name, calls `__waste_dlsym`, returns result as
  `void *` (table index for functions, memory address for data).
- `dlclose` — calls `__waste_dlclose`, sets errno on failure.
- `dlerror` — returns a generic error string when the last operation failed;
  the engine does not currently propagate error text back to the guest.

**Guest dlfcn.h** (`src/html-rt/lib/include/dlfcn.h`):

Standard POSIX header with `RTLD_LAZY`, `RTLD_NOW`, `RTLD_GLOBAL`, `RTLD_LOCAL`
constants and function declarations for `dlopen`, `dlsym`, `dlclose`, `dlerror`.

**Not yet done** (deferred to integration with Phase 3+):

- `RTLD_DEFAULT` / `RTLD_NEXT` handle semantics (search all modules).
- Error text propagation from engine to guest dlerror buffer.
- `dladdr` — reverse symbol lookup.
- Thread safety for the loaded library list.

### Phase 3: Build ncurses as shared library — IMPLEMENTED

**Status**: Complete.  134 source files compile, link produces a 306 KB PIC
shared library with a valid `dylink.0` section (memorysize=57100,
tablesize=20) and 652 exports covering all standard ncurses API functions.
Curses headers installed in sysroot for downstream consumers (rogue).

**Files created**: `src/html-rt/tools/build-ncurses.py`

**Files modified**: `src/html-rt/lib/include/termios.h`,
`src/html-rt/lib/include/stdio.h`

**Files created (sysroot support)**: `src/html-rt/lib/include/search.h`

**Build script** (`build-ncurses.py`):

- `ensure_sysroot()` — locates or builds the WASTE application sysroot
  (reuses the coreutils sysroot at `build/coreutils/sysroot` when available).
- `configure_ncurses()` — runs autotools configure with
  `--host=wasm32-unknown-none`, `--without-shared`, `--without-cxx`,
  `--without-ada`, `--disable-database`, `--enable-termcap`,
  `--with-fallbacks=xterm,xterm-256color,vt100,dumb`.  A `config.site` file
  provides cached answers for cross-compilation checks (tcgetattr, signals,
  mmap, fork, etc.) using ncurses-specific `cf_cv_` prefixed variables.
- `generate_fallbacks()` — runs ncurses's `MKfallback.sh` when compatible
  host `tic` and `infocmp` tools are available and validates that all four
  requested terminal descriptions were generated. It falls back to an empty
  `_nc_fallback()` implementation only when those host tools cannot produce a
  valid source file. The current build contains xterm, xterm-256color, vt100,
  and dumb entries.
- `compile_ncurses()` — compiles 134 source files from three groups (base:
  71, tty: 6, tinfo: 47) plus 6 generated files, with `-fPIC
  -fvisibility=default -DHAVE_CONFIG_H`.
- `link_shared_library()` — `wasm-ld --shared --import-memory --import-table
  --export-all --allow-undefined --no-entry`.
- `install_headers()` — copies generated `curses.h`, `ncurses.h`, `term.h`,
  `ncurses_cfg.h`, `unctrl.h` into the sysroot include directory.

**Guest sysroot fixes required for ncurses compilation**:

- `termios.h`: Added `PARMRK`, `IXANY`, `IUTF8` input mode flags.  Changed
  from anonymous typedef to named `struct termios` (ncurses expects standard
  POSIX `struct termios`).  Added baud rate constants, `tcflag_t`/`cc_t`/
  `speed_t` typedefs, full control character indices, all `tcsetattr` actions.
- `termios.c`: Updated to use standard POSIX signatures (`int` and
  `struct termios *` instead of `i32` and `waste_termios *`).  Added
  `tcdrain`, `tcflush`, `cfgetospeed`, `cfgetispeed`, `cfsetospeed`,
  `cfsetispeed`.
- `stdio.h`: Added `sscanf`, `fscanf`, `vsscanf`, `vfscanf` declarations.
  Without proper variadic declarations, each call site inferred a different
  fixed function signature, causing wasm-ld signature mismatch errors.
- `search.h`: New stub header providing `tsearch`/`tfind`/`tdelete`/`twalk`
  declarations for `lib_tparm.c`.  The functions are left undefined
  (`--allow-undefined` at link time); ncurses falls back to internal linear
  search when they are unavailable.

**Build output**:

```
build/ncurses/libncurses.so.wasm    # 306 KB PIC shared library
build/ncurses/vfs/libncurses.so.wasm  # VFS staging copy
build/ncurses/build/                # Configure/make artifacts (cached)
```

**Key exports verified**: `initscr`, `endwin`, `newwin`, `wrefresh`,
`waddch`, `wgetch`, `wmove`, `cbreak`, `noecho`, `keypad`, `start_color`,
`init_pair`, `printw`, `mvprintw`, `clear`, `refresh`, `curs_set`, `raw`,
`setupterm`, `tputs`, `tparm`.

**Run**: `python3 src/html-rt/tools/build-ncurses.py --repo-root . --output build/ncurses`

### Phase 4: Build rogue as executable — IMPLEMENTED

**Status**: Complete. All 33 Rogue source files plus the PIC CRT compile; the
current link produces a 178 KiB wasm32 executable. Post-link import rewriting
moves ncurses function imports from module `"env"` to `"libncurses"`. The
browser package installs it at both `/bin/rogue` and `/usr/bin/rogue`.

**Files created**: `src/html-rt/tools/build-rogue.py`

**Build script** (`build-rogue.py`):

- `ensure_ncurses()` — builds ncurses shared library if not already built
  (delegates to `build-ncurses.py`).
- `prepare_sources()` — copies rogue source files into the build directory
  and applies source patches:
  - `main.c`: Replaced direct `curscr->_cury`/`_curx` struct access with
    `wmove(curscr, oy, ox)` in `tstp()`.  ncurses uses opaque WINDOW structs;
    direct field access does not compile.
  - `mdport.c`: Removed `getpwuid()` calls in `md_gethomedir()`,
    `md_getshell()`, and `md_getrealname()`.  The WASTE guest has no passwd
    database; these functions fall through to `getenv("HOME")`,
    `getenv("SHELL")`, and `sprintf(uidstr, "%d", uid)` respectively.
- Generates a custom `config.h` with feature flags appropriate for wasm32:
  enables `HAVE_TERMIOS_H`, `HAVE_TERM_H`, `HAVE_ERASECHAR`, `HAVE_KILLCHAR`,
  `HAVE_NCURSES_H`; disables `HAVE_WORKING_FORK`, `HAVE_PWD_H`,
  `HAVE_GETPWUID`, `HAVE_ALARM`, `SCOREFILE`, `MASTER`, `MAXLOAD`.
  Adds `PATH_MAX 1024` (not in guest libc limits.h).
- `compile_rogue()` — compiles 33 source files and the target CRT with `-fPIC`,
  `-DHAVE_CONFIG_H`, and ncurses include paths from the build directory.
- `link_executable()` — `wasm-ld --no-entry --import-memory --import-table
  --export=_start --export=main --experimental-pic
  --unresolved-symbols=import-dynamic --global-base=327680
  --table-base=1024`.
- `get_ncurses_exports()` — parses the export section of
  `libncurses.so.wasm` to build the set of ncurses function names (602
  functions).
- `rewrite_imports()` — scans the rogue executable's import section; for
  each import with module `"env"` whose name matches a ncurses export,
  rewrites the module name to `"libncurses"`.  Rebuilds the binary import
  section in-place.  This allows the engine loader to resolve ncurses
  symbols against the dynamically loaded library module rather than against
  the libc `"env"` namespace.

**Source patches applied** (at build time, not modifying submodule):

1. `main.c:241-242` — `curscr->_cury = oy; curscr->_curx = ox;` →
   `wmove(curscr, oy, ox);`
2. `mdport.c:md_gethomedir()` — removed `getpwuid(getuid())` path
3. `mdport.c:md_getshell()` — removed `getpwuid(getuid())` path
4. `mdport.c:md_getrealname()` — disabled `getpwuid(uid)` path

**Signal handling**: `extern.h` already contains `#undef SIGTSTP`, so the
`tstp()` suspend/resume handler is effectively dead code (never registered
as a signal handler).  Signal setup in `mdport.c` is guarded by `#ifdef
SIGHUP` etc. — since the guest libc defines these signals, the calls
compile but the signal stubs are no-ops in the browser.

**Import module split**:
- `env` (49 imports): libc functions — `malloc`, `printf`, `exit`,
  `getenv`, `strcmp`, `strlen`, etc.
- `libncurses` (38 imports): curses functions — `initscr`, `endwin`,
  `curs_set`, `wgetch`, `waddch`, `wmove`, `raw`, `noecho`, `keypad`, `wrefresh`,
  `mvprintw`, `newwin`, `baudrate`, etc.

**Build output**:

```
build/rogue/rogue.wasm           # 178 KiB executable (imports rewritten)
build/rogue/rogue-raw.wasm       # Pre-rewrite executable
build/rogue/vfs/rogue            # VFS staging copy
build/rogue/src/                 # Patched source copies
build/rogue/objects/             # Compiled object files
```

**Run**: `python3 src/html-rt/tools/build-rogue.py --repo-root . --output build/rogue`

### Phase 5: Shared-library libc migration — IMPLEMENTED

**Status**: Complete.  All 23 source files (allocator.c + 22 libc C modules)
compile with `-fPIC -fvisibility=default` and link into a 60 KB PIC shared
library with a valid `dylink.0` section (memorysize=1585, tablesize=0) and
367 exports covering the full libc API.

**Files created**: `src/html-rt/lib/allocator.c`,
`src/html-rt/tools/build-libc-shared.py`

**Allocator port** (`allocator.c`):

C port of the `stdlib.wat` boundary-tag free-list allocator.  Replaces the
hand-written WebAssembly text with equivalent C that compiles to PIC object
code via `clang --target=wasm32 -fPIC`.  Key design decisions:

- Uses `__builtin_wasm_memory_size(0)` and `__builtin_wasm_memory_grow(0, n)`
  for the `memory.size` and `memory.grow` WebAssembly intrinsics.
- Stores linear memory addresses as `i32` with `(u32)` casts for pointer
  conversions (safe on wasm32 where `sizeof(void *) == 4`).
- `__errno_location()` returns `i32*` (matching `helper.h` declaration) by
  casting the stored errno address through `(u32)`.
- Block layout identical to `stdlib.wat`: 16-byte header (size|flags at
  offset 0, prev at offset 4, next at offset 8, padding at offset 12),
  user data, 4-byte boundary tag at the end.  Minimum block size 32 bytes.
- All exported functions match `stdlib.wat`: `malloc`, `free`, `calloc`,
  `realloc`, `sbrk`, `__errno_location`, `waste_allocator_init`,
  `waste_malloc_usable_size`, `waste_heap_base`, `waste_heap_end`,
  `waste_memory_pages`, `waste_memory_grow_calls`.

**Build script** (`build-libc-shared.py`):

- `ensure_sysroot()` — locates or builds the WASTE application sysroot
  (reuses the coreutils sysroot when available).
- `compile_objects()` — compiles `allocator.c` plus all 22 libc C source
  files from `libc_sources.py` with `-fPIC -fvisibility=default`.  Flattens
  `sys/` subdirectory names to avoid object file path conflicts.
- `link_shared_library()` — `wasm-ld --shared --import-memory --import-table
  --export-all --allow-undefined --no-entry`.
- `inspect_dylink()` — parses the output binary's `dylink.0` section and
  prints a summary of memory/table sizes and export count.

**Key difference from old pipeline** (`build-waste-libc.py`):

The old pipeline: `wasm-as stdlib.wat` → core.wasm, `clang --target=wasm32`
all C sources → helpers.wasm, `wasm-merge core.wasm env helpers.wasm helpers`
→ waste-libc.wasm, then WAT text rewriting to change memory/table ownership
to `waste-runtime`.

The new pipeline: `allocator.c` + all C sources compiled with `-fPIC` to
object files → `wasm-ld --shared` → `libc.so.wasm` with native `dylink.0`
section.  No WAT text surgery, no `wasm-merge`, no `wasm-dis`/`wasm-as`
roundtrip.  The `dylink.0` section tells the engine loader exactly how much
memory and table space the library needs.

**Import module split** (31 imports total):

- `env` (8 imports): `memory`, `__indirect_function_table`, `__stack_pointer`,
  `__memory_base`, `__table_base` (standard PIC), plus `close`,
  `readdir_v1`.
- `waste_kernel` (21 imports): POSIX host functions — `realtime_v1`,
  `tcgetattr_v1`, `tcsetattr_v1`, `dlopen_v1`, `dlsym_v1`, `dlclose_v1`,
  `open_v1`, `chdir`, `getcwd`, `fcntl_v1`, `startup_v1`, etc.
- `GOT.mem` (2 imports): PIC global relocations for `error_text`,
  `random_state`.

**Build output**:

```
build/libc-shared/libc.so.wasm        # 60 KB PIC shared library
build/libc-shared/vfs/libc.so.wasm    # VFS staging copy
build/libc-shared/objects/            # 23 compiled object files
```

**Run**: `python3 src/html-rt/tools/build-libc-shared.py --repo-root . --output build/libc-shared`

### Phase 6: `ldd` utility — IMPLEMENTED

**Status**: Complete and browser-accepted. Compiles and links into a 4.6 KB
wasm32 PIC executable with the project CRT and `_start`. Source is embedded in
the build script. The Bash package installs it at `/bin/ldd` and
`/usr/bin/ldd`.

**Files created**: `src/html-rt/tools/build-ldd.py`

**Build script** (`build-ldd.py`):

Embeds the `ldd.c` source as a string constant. It compiles both `ldd.c` and
the project CRT with `-fPIC`, then links with `wasm-ld --no-entry
--import-memory --import-table --experimental-pic
--unresolved-symbols=import-dynamic --export=_start --export=main
--export=__heap_base --export=__data_end --global-base=327680
--table-base=1024`.

**Source** (`ldd.c`, embedded in build script):

A self-contained wasm binary import section parser that:

1. `stat()` the target to get file size, `malloc()` a buffer, `open()` +
   `read()` the entire binary, `close()`.
2. Verifies the wasm magic number (`\0asm`) and version.
3. Walks sections to find section id 2 (import section).
4. Parses each import entry: extracts module name and import name, skips
   the type descriptor (function/table/memory/global).
5. Collects unique module names with import counts (up to 64 dependencies,
   128-char names).
6. Classifies each dependency:
   - **Internal** (`GOT.mem`, `GOT.func`): PIC relocation modules, hidden
     from output.
   - **Built-in** (`env`, `waste_kernel`, `waste-runtime`,
     `wasi_snapshot_preview1`): provided by the runtime, shown with
     "(built-in)" tag.
   - **Library**: resolved against `/usr/lib` search path with suffix
     probing (`.so.wasm`, `.wasm`, `lib` prefix).
7. Prints in standard `ldd` format with import counts:
   ```
   	env (49 imports, built-in)
   	libncurses => /usr/lib/libncurses.so.wasm (38 imports)
   ```
8. Returns exit code 1 if any non-builtin dependency is unresolved.

**Library resolution search order**:

1. `/usr/lib/<name>.so.wasm`
2. `/usr/lib/<name>.wasm`
3. `/usr/lib/lib<name>.so.wasm`
4. `/lib/<name>.so.wasm`

**Import profile**: 22 imports: the memory and table plus 19 functions under
`env`, and `waste_kernel.startup_v1` for CRT startup. The libc imports include
`waste_stdin/out/err`, `fflush`, `exit`, `fprintf`, `stat`, `malloc`, `open`,
`read`, `close`, `free`, `printf`, `strcmp`, `strlen`, `memcmp`, `memcpy`,
`snprintf`, and `access`.

**Build output**:

```
build/ldd/ldd.wasm                # 4.6 KB executable
build/ldd/ldd.c                   # Extracted source
build/ldd/ldd.o                   # Object file
build/ldd/vfs/ldd                 # VFS staging copy
```

**Run**: `python3 src/html-rt/tools/build-ldd.py --repo-root . --output build/ldd`

### Phase 7: Process integration — IMPLEMENTED

**Status**: Complete.  Both modified files (`process.c`, `store.c`) compile
clean with `clang --target=wasm32 -fsyntax-only`.  Existing dylink parser
test still passes (6/6 checks).

**Files modified**: `src/engine/process.c`, `src/engine/store.c`

**Fork: library metadata cloning** (`native_process_capsule_clone`):

Added after handler cloning.  When the source capsule has loaded libraries,
the clone:
1. Allocates a new `loaded_libraries` array sized to the source count.
2. Copies all `native_loaded_library` entries via `memcpy` — engine
   pointers temporarily reference the parent's engines.
3. Sets `loaded_library_count` and `loaded_library_capacity`.

The engine pointers are stale at this point; they are remapped by
`clone_process_graph` after engine cloning completes.

**Fork: engine remapping** (`native_store_clone_process_graph`):

Added after `linked_engines` assignment, before `free(bindings)`.
Iterates the child's `loaded_libraries` and replaces each engine pointer
with its cloned counterpart using the existing `mapped_engine()` binding
lookup.  This works because library engines are already registered in
`store->modules` and are cloned as part of the registered-module loop.

**Capsule destruction** (`native_process_capsule_destroy`):

Added `free(capsule->loaded_libraries)` before region clearing.  The
engine pointers within the array are NOT freed here — they are owned by
their respective managers:
- **Parent process**: engines are store-level (`store->modules`) and
  outlive the capsule.
- **Forked child**: engines are in `linked_engines` (freed earlier in
  the same destroy function).

**Library unloading** (`native_store_unload_library`):

Replaced the 4-line TODO stub with a complete implementation:
1. **Ref-count check**: if `ref_count > 1`, decrements and returns.
2. **Destructor invocation**: looks up `__wasm_call_dtors` via
   `exec_find_export`; if found, calls it via `exec_invoke`.
3. **Store deregistration**: scans `store->modules` for the library
   engine and clears the entry (`engine = NULL`,
   `registered[0] = '\0'`).
4. **Engine free**: calls `exec_free(engine)`.
5. **Slot cleanup**: zeros `ref_count`, `engine`, and `name` but does
   NOT compact the array — dlopen handles are 1-based indices into
   `loaded_libraries`, so compaction would invalidate outstanding
   handles.

**Deferred work**:
- Memory/table region reclamation on unload — releasing pages from the
  middle of linear memory requires a region coalescing pass that is not
  yet implemented.  Regions remain allocated until process exit.
- Thread safety for the loaded library list (not needed until guest
  threading is implemented).

## Directory structure

```
src/html-rt/src/bash/             # Files embedded by the Bash-page generator
  rogue.wasm                      # Rogue executable package input
  ldd.wasm                        # Dependency-report executable package input
  libncurses.so.wasm              # ncurses shared-object package input

src/html-rt/tools/
  build-ncurses.py                # ncurses cross-compilation script
  build-rogue.py                  # rogue cross-compilation script
  build-libc-shared.py            # libc PIC shared library build script
  build-ldd.py                    # ldd dependency lister build script

submodules/
  ncurses/                        # ncurses source (already cloned)
  rogue/                          # rogue source (already cloned)
  ncurses-waste.patch             # WASTE-specific patches for ncurses
  rogue-waste.patch               # WASTE-specific patches for rogue
```

VFS layout at runtime:
```
/bin/rogue                        # packaged executable
/usr/bin/rogue                    # executable alias
/bin/ldd                          # packaged dependency reporter
/usr/bin/ldd                      # dependency-reporter alias
/lib/libncurses.so.wasm           # packaged shared library
/usr/lib/libncurses.so.wasm       # shared-library alias
```

The libc shared-object experiment is not yet part of the current
resident-libc browser package.

## Key existing code to reuse

- `native_load_module()` (store.c:947) — module decoding + import resolution
- `native_registered_module()` (store.c:882) — module lookup by name
- `native_store_add()` (store.c:912) — register new module
- `native_linked_call()` (store.c:690) — cross-module call trampoline
- `browser_host_resolver()` (posix_stubs.c:2315) — host function dispatch
- `posix_kernel_path_snapshot()` (kernel.c:465) — VFS file loading
- `native_process_region` (store.h:151) — memory region allocation
- `build-waste-sysroot.py` — sysroot/toolchain wrapper pattern
- `probe-coreutils-utility.py` — import audit pattern
- Existing `termcap.c` — hardcoded VT100/ANSI capabilities

## Verification

1. [x] `wasm-ld -shared` produces a valid PIC module with
   `__memory_base`/`__table_base` imports; the native dylink fixture passes
   6/6 checks under ASan/UBSan.
2. [x] The engine allocates and instantiates PIC module regions.
3. [ ] A standalone guest fixture explicitly calls
   `dlopen("/usr/lib/libncurses.so.wasm", RTLD_NOW)`.
4. [ ] That fixture calls `dlsym(handle, "initscr")` and `dlclose(handle)`.
5. [x] Packaged `ldd /usr/bin/rogue` reports
   `libncurses => /usr/lib/libncurses.so.wasm`.
6. [x] Rogue starts, enters the alternate screen, accepts raw key input,
   renders a dungeon, and returns to a usable Bash prompt with status 0.
7. [x] The full `--html-bash` regression gate, including the required
   shared-library/Rogue browser test, passes.
8. [x] Run two Rogue children in one Bash lifetime and retain this as the
   process-local provider reuse regression.
9. [x] Submit a fresh command one byte at a time after the second Rogue child
   returns to Bash. This covers ncurses `endwin()` termios restoration,
   parent/readline resumption, and post-alternate-screen input together. The
   page reclaims canvas focus at a returned Bash prompt and routes document
   keyboard events to the terminal if focus fell back to the page, while
   leaving diagnostics controls alone.
10. [x] The repository-owned Rogue patch calls `curs_set(0)` during game
    setup. The `@` glyph represents the player without an inverted hardware
    cursor moving independently over the dungeon, and ncurses restores the
    normal visible cursor as part of `endwin()` before Bash resumes.
