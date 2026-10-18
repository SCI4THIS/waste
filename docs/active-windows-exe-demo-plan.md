# Browser-to-Windows executable demo

## Goal and current status

Open the self-contained `bash.html`, compile the supplied C source to a native
Windows x64 executable, download it and run it on Windows. The current requested
demo is a small window displaying **Hello from WASTE!** in a label, with a
**Hello** button that closes the window and exits 0. The normal title-bar close
button also exits cleanly. Editing the greeting and recompiling changes the label.

The user reports that executable compilation is working. This GUI update extends
the previously accepted console toolchain; the compiler and linker binaries,
engine and runtime ABI are unchanged. GUI source and import-library changes are
implemented and pass 54 native assertions and 55 production-worker results.
**Complete:** saved browser/Windows evidence and the user’s confirmation verify
the original and changed GUI demos. Both compile/download chains and Windows
processes exit 0; executable hashes match the Windows report.

This is a market-interest demo, not a full Windows SDK or CRT implementation.

## Scope and architecture

- AMD64 COFF objects and PE32+ executables with the **Windows GUI subsystem**.
- Actual guest Clang generates objects; the existing COFF-only guest `lld-link`
  resolves them. No server, host compiler substitute or shipped example `.exe`.
- `/root/demo/windows-hello.c` contains the target-only Win32 declarations and
  explicit `demo_start` entrypoint. Static assertions check LLP64, WNDCLASSA and
  MSG sizes/offsets. Static zero-initialization avoids a target `memset` helper.
- KERNEL32 supplies `GetModuleHandleA` and `ExitProcess`. USER32 supplies
  `RegisterClassA`, `LoadCursorA`, `CreateWindowExA`, `DefWindowProcA`,
  `DestroyWindow`, `PostQuitMessage`, `GetMessageA`, `TranslateMessage` and
  `DispatchMessageA`. No Windows DLL is distributed.
- Built-in `STATIC` and `BUTTON` controls display the label and Hello button.
  BN_CLICKED reaches the window procedure through WM_COMMAND. DestroyWindow /
  WM_DESTROY / PostQuitMessage terminate the ordinary Windows message loop;
  failures exit 1. DefWindowProc handles the title-bar close control.
- No Windows SDK, target CRT, `windows.h`, WinMain startup shim, GDI drawing,
  resource compiler, manifest, PDB, LTO, other architecture or new UI framework.
- Clang retains Wasm defaults. The Windows compile bypasses `clang.cfg` with
  `--no-default-config`; `/nodefaultlib` keeps the Wasm CRT/libc out of the PE.
- The toolchain itself runs through existing shared libc/kernel processes,
  clocks and yielding waits. This adds no Windows syscalls or PE execution to
  WASTE. Windows executes the downloaded file. No Asyncify is used.
- The page remains a single offline `file://` document and uses the existing
  upload/download path. Source submodules remain read-only.

## Source ownership and build

- Source: `src/vfs/root/demo/windows-hello.c` (mounted at
  `/root/demo/windows-hello.c`). No authored Windows declarations enter the
  general guest SDK headers.
- Import definitions: `src/aux/lld-link/{kernel32.def,user32.def}`. Host
  `llvm-dlltool -m i386:x86-64` generates libraries under `build/aux/lld-link`.
- Installed metadata: `/usr/lib/waste/windows-demo/{kernel32.lib,user32.lib}`.
  Definition sources, README and LLVM license live under
  `/usr/share/licenses/lld-link`.
- Package build/install/test follow `skills/add-aux-binary/SKILL.md` through
  `src/aux/Makefile`. `start.sh` still delegates to the same package targets.
- `lld-link-imports` builds the two libraries without installing;
  `install-lld-link-imports` explicitly installs them and notices without
  rebuilding LLVM or replacing the accepted guest tools. Normal
  `install-lld-link` includes that step.
- Package setup/WAST/descriptor are authored at
  `src/vfs/root/test/aux/lld-link`, exposed through `src/aux/lld-link/test/system`.
  Interaction assertions are at
  `src/vfs/root/test/system/windows-demo/artifacts.wast`.
- Generated metadata/support stays under `build/` or the generated corpus
  namespace. Refresh through safe corpus publication; never copy a whole VFS or
  replace edited authored tests. HTML packaging does not compile/reinstall tools.

For an existing installed compiler/linker, update only demo link metadata:

```sh
make -C src/aux install-lld-link-imports
```

For a fresh toolchain build, use `install-clang` and `install-lld-link` through
`src/aux/Makefile`. Do not run independent LLVM builds against one configured
tree. Keep all mutable source staging, generated tools and libraries in `build/`.

## Automated acceptance

- [x] Implement window, label, Hello button, close handling and message loop.
- [x] Compile the target declarations with warnings as errors and ABI assertions.
- [x] Build/install both import libraries through aux Make targets.
- [x] Run host-produced diagnostic objects through the bounded WAST checker:
      16/16 pass. This is checker preflight, not guest compiler or GUI acceptance.
- [x] Run the actual guest compile/link/rejection/recovery suite natively:
      38 package + 16 artifact assertions = 54.
- [x] Refresh corpus metadata and audit provenance, preserving authored inputs.
- [x] Rebuild the single offline page with the updated source and libraries.
- [x] Run the same setup through the production worker: 55 results with launch.
- [x] Verify embedded bytes/offline packaging and binary transfer fidelity.
- [x] Record final metrics, syntax checks and `git diff --check`.

The artifact WAST checks bound headers/RVAs/sections, AMD64, PE32+, the GUI
subsystem, an executable entrypoint and exactly the two DLLs / eleven imports.
It rejects console subsystem, unexpected DLLs, ordinal/duplicate imports,
invalid RVAs, truncation and overflowing headers, then checks restored data.
It verifies both greeting strings and the null-terminated STATIC/BUTTON/Hello
control names and window title. It cannot prove actual Windows control behavior.
Package WAST retains failed compilation/linking, unsupported LTO/PDB requests
and successful recovery. Assertions remain in WAST; shell only captures inputs
and statuses. Do not silently accept a host-produced replacement executable.

The native installed-tool boundary driver is the same one used by Make:

```sh
bash src/aux/lld-link/test/system/run.sh "$PWD" "$PWD/src/vfs" lld-link \
  "$PWD/src/aux/lld-link/test/system" "$PWD/build/aux/lld-link/tests" \
  "$PWD/build/cli-rt/wasm" "$PWD/src/aux/lld-link/test/system/setup.sh"
```

The production-worker gate is:

```sh
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/lld-link/test/system/browser.json
```

GUI evidence goes under `build/engine/logs/windows-gui-demo/`: `native.log`,
`worker-results.json`, `package-check.json`, `transfer.log`, `corpus-audit.log`
and `results-summary.json`. All GUI automated checks passed. The page is
38,421,455 bytes, with a 28,706,914-byte compressed tarball. Corpus provenance
passes for 436 entries (293 supported); embedded source/library/test bytes,
offline references, binary transfer, cached import builds and shell syntax pass.
The final diff whitespace check passes. Actual Windows GUI interaction is
confirmed by the manual evidence below.

Prior console source-build/handoff evidence remains under
`build/engine/logs/windows-exe-demo/` and the isolated
`build/windows-demo-worktree/`; no new LLVM build is needed for this GUI update.
That baseline passed 51 native / 52 worker results, the seven existing Wasm
compiler tests and focused sanitizer identity/linker gates. The additional full
sanitizer attempt timed out at 20 minutes before reporting assertions and is
not a PASS. It is not repeated for target-only demo/import changes.

The migration's full sanitizer/topic matrix remains open; demo acceptance does
not claim that separate matrix is complete.

## Manual Firefox and Windows acceptance

- [x] Compile and download the original and changed source from the offline page:
      `original_chain_exit=0`, `changed_chain_exit=0`.
- [x] Confirm the window display and Hello buttons work correctly on Windows
      (user confirmation); both recorded process exits are 0.
- [x] Review saved executables and compare their SHA-256 hashes with the Windows
      report. Both are AMD64 PE32+ GUI executables, contain their respective
      greeting and built-in controls, and match their reported hashes.

Evidence supplied under `build/html-rt/`:

- `windows-demo-browser-status.txt`: both browser chains exit 0.
- `windows-demo-results.txt`: both Windows processes exit 0, on
  `Microsoft Windows NT 10.0.26100.0`.
- `hello.exe`: SHA-256
  `0cd15c23b43754c5ca957b9b4cf2ac1739edd16aa6378525bdfa6f22319a9f6b`.
- `hello-changed.exe`: SHA-256
  `2bee746e9f8cfc022a1f5ceb8782dc205e033204c7f0283964c3494febb78787`.

The generated `results-summary.json` also records this manual acceptance and
artifact checks. The commands below remain available for repeating the demo.

### Manual evidence commands

In a fresh Firefox page at
`file:///home/a/Work/waste/build/html-rt/bash.html`, run:

```sh
clang --no-default-config --target=x86_64-pc-windows-msvc \
  -ffreestanding -fno-builtin -fno-stack-protector -nostdinc -O0 \
  -c /root/demo/windows-hello.c -o /tmp/hello.obj &&
lld-link /machine:x64 /subsystem:windows /entry:demo_start /nodefaultlib \
  /out:/tmp/hello.exe /tmp/hello.obj \
  /usr/lib/waste/windows-demo/kernel32.lib \
  /usr/lib/waste/windows-demo/user32.lib &&
download /tmp/hello.exe
printf 'original_chain_exit=%s\n' "$?" > /tmp/windows-demo-browser-status.txt
```

Keep the `&&` chain so failed compilation/linking skips subsequent steps and
cannot download stale output. Change the label and repeat:

```sh
WINDOWS_SOURCE="$(</root/demo/windows-hello.c)"
printf '%s\n' "${WINDOWS_SOURCE/Hello from/Hello again from}" > /tmp/hello-changed.c
clang --no-default-config --target=x86_64-pc-windows-msvc \
  -ffreestanding -fno-builtin -fno-stack-protector -nostdinc -O0 \
  -c /tmp/hello-changed.c -o /tmp/hello-changed.obj &&
lld-link /machine:x64 /subsystem:windows /entry:demo_start /nodefaultlib \
  /out:/tmp/hello-changed.exe /tmp/hello-changed.obj \
  /usr/lib/waste/windows-demo/kernel32.lib \
  /usr/lib/waste/windows-demo/user32.lib &&
download /tmp/hello-changed.exe
printf 'changed_chain_exit=%s\n' "$?" >> /tmp/windows-demo-browser-status.txt
download /tmp/windows-demo-browser-status.txt
```

Both chain statuses should be 0. Put both executables in a directory on Windows
and run the following PowerShell there. For each window, observe its label and
press Hello. `Start-Process -Wait` waits for GUI completion; console-style
`$LASTEXITCODE` capture is not used.

```powershell
$evidence = foreach ($name in @('hello.exe', 'hello-changed.exe')) {
  $process = Start-Process -FilePath (Join-Path $PWD $name) -Wait -PassThru
  "$name exit=$($process.ExitCode)"
  "$name sha256=$((Get-FileHash ".\$name" -Algorithm SHA256).Hash)"
}
$evidence += "windows=$([Environment]::OSVersion.VersionString)"
$evidence | Set-Content -Encoding utf8 windows-demo-results.txt
```

Expect labels Hello from WASTE! and Hello again from WASTE!, each with exit 0
after the Hello button is pressed. Copy `hello.exe`, `hello-changed.exe`,
`windows-demo-browser-status.txt` and `windows-demo-results.txt` into
`build/html-rt/windows-exe-demo/`, and report that the labels/buttons behaved as
expected. Automated worker and format checks do not replace this observation.

## Completion gate

**Satisfied.** Offline browser compile/download and Windows GUI interaction
are confirmed for the original and changed greeting. This plan is complete and
ready to retire. No
Windows SDK, CRT, PDB, other architecture, editor, billing or backend service is
required to complete the demo. Keep future product work separate.

## API references

- [WNDCLASSA](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-wndclassa)
- [MSG](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-msg)
- [CreateWindowExA](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexa)
- [BN_CLICKED](https://learn.microsoft.com/en-us/windows/win32/controls/bn-clicked)
