# Minimal Windows COFF linker

This package builds a Wasm-hosted, COFF-only `lld-link` for the
[browser-to-Windows demo](../../../docs/active-windows-exe-demo-plan.md).
It targets Windows x64 GUI PE32+ executables using Windows APIs directly.
The downloaded executable runs on Windows; WASTE does not execute PE files.

`kernel32.def` and `user32.def` list the target imports used by
`src/vfs/root/demo/windows-hello.c`. KERNEL32 supplies module lookup and process
exit; USER32 supplies class registration, the arrow cursor, window/control
creation and the message loop. Host `llvm-dlltool` generates the two tiny import
libraries. No Windows SDK, CRT or DLL is bundled. The sample checks Windows
LLP64 and WNDCLASSA/MSG layouts and uses `demo_start` as its explicit entrypoint.
It opens a window with a greeting label and a **Hello** button. Clicking that
button destroys the window and exits 0; the normal close button also works.

Build with `make -C src/aux lld-link`, explicitly install with
`make -C src/aux install-lld-link`, and test with
`make -C src/aux test-lld-link`. Build outputs live under `build/aux/lld-link`.
The compiler's private LLVM build supplies libraries; all upstream sources stay
read-only. `coff-waste.patch` applies to a separate staged COFF source copy.
The guest file-loader uses the existing LLVM deferred-task support; the native
threaded source path retains standard futures. No new kernel scheduling or
Windows syscall implementation is introduced.

The profile excludes LTO, other LLD drivers and archive creation. Bitcode/LTO
inputs, LTO options and PDB-generation requests fail explicitly. Install audits
shared-libc imports and publishes:

- `/usr/bin/lld-link`
- `/usr/lib/waste/windows-demo/{kernel32.lib,user32.lib}`
- `/usr/share/licenses/lld-link/{LICENSE.TXT,kernel32.def,user32.def,README.md}`

The X86-enabled guest Clang preserves its default Wasm configuration. In guest
Bash, bypass that configuration explicitly for the Windows demo:

```sh
clang --no-default-config --target=x86_64-pc-windows-msvc \
  -ffreestanding -fno-builtin -fno-stack-protector -nostdinc -O0 \
  -c /root/demo/windows-hello.c -o /tmp/hello.obj &&
lld-link /machine:x64 /subsystem:windows /entry:demo_start /nodefaultlib \
  /out:/tmp/hello.exe /tmp/hello.obj \
  /usr/lib/waste/windows-demo/kernel32.lib \
  /usr/lib/waste/windows-demo/user32.lib &&
download /tmp/hello.exe
```

Keep the `&&` chain so errors skip later steps and cannot download stale output.
The source calls Windows APIs directly; WASTE libc runs the toolchain but
is not linked into the Windows executable.

`test/system` points into canonical authored inputs at
`src/vfs/root/test/aux/lld-link`. Its Bash setup captures real statuses and
observations. WAST owns all 38 package assertions, plus 16 interaction assertions
at `/root/test/system/windows-demo/artifacts.wast`: bounded COFF/PE structures,
the GUI subsystem, exactly the KERNEL32/USER32 imports, standard controls and
both greetings, rejected requests and recovery after compiler
and linker errors. The native driver allows ten minutes, overridable with
`WASTE_TEST_TIMEOUT_MS`; `WASM_RUNNER` selects an instrumented native runner.

The same setup runs through the production-worker harness:

```sh
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html \
  src/aux/lld-link/test/system/browser.json
```

It expects 55 results including launch and has a fifteen-minute host deadline.
Worker evidence does not establish actual Firefox downloads or Windows execution;
those final steps are tracked in the demo plan.

The COFF package uses private `COFF_LLD_*` Make variables. The existing Wasm
object/archive regression is `test-clang-lld-link`; its non-LTO and SDK wrappers
retain their names. `test-lld-link` runs this COFF package.

When only demo API imports change, run `make -C src/aux install-lld-link-imports`.
It regenerates and installs the two import libraries and their definition files
without rebuilding LLVM or replacing the guest compiler/linker. Normal
`install-lld-link` includes that step. `lld-link-imports` builds without installing.

Target ABI references: [WNDCLASSA](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-wndclassa),
[MSG](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-msg),
[CreateWindowExA](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexa),
[BN_CLICKED](https://learn.microsoft.com/en-us/windows/win32/controls/bn-clicked).
