# Sourced by the guest Bash test driver. Assertions live in WAST.
/usr/bin/lld-link --version > /tmp/windows-link-version.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-version-status.txt
/usr/bin/clang --no-default-config --target=x86_64-pc-windows-msvc \
  -ffreestanding -fno-builtin -fno-stack-protector -nostdinc -O0 \
  -c /root/demo/windows-hello.c -o /tmp/windows-hello.obj \
  > /tmp/windows-compile-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-compile-status.txt
/usr/bin/lld-link /machine:x64 /subsystem:windows /entry:demo_start /nodefaultlib \
  /out:/tmp/windows-hello.exe /tmp/windows-hello.obj \
  /usr/lib/waste/windows-demo/kernel32.lib /usr/lib/waste/windows-demo/user32.lib > /tmp/windows-link-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-link-status.txt
/usr/bin/lld-link /machine:x64 /subsystem:windows /entry:missing_entry /nodefaultlib \
  /out:/tmp/windows-bad-entry.exe /tmp/windows-hello.obj \
  /usr/lib/waste/windows-demo/kernel32.lib /usr/lib/waste/windows-demo/user32.lib > /tmp/windows-entry-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-entry-status.txt
/usr/bin/lld-link /machine:x64 /subsystem:windows /entry:demo_start /nodefaultlib \
  /out:/tmp/windows-no-imports.exe /tmp/windows-hello.obj \
  > /tmp/windows-unresolved-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-unresolved-status.txt
/usr/bin/lld-link /machine:x64 /subsystem:windows /entry:demo_start /nodefaultlib \
  /out:/tmp/windows-missing-lib.exe /tmp/windows-hello.obj \
  /tmp/windows-missing.lib > /tmp/windows-library-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-library-status.txt
printf 'invalid COFF input\n' > /tmp/windows-malformed.obj
/usr/bin/lld-link /machine:x64 /subsystem:windows /entry:demo_start /nodefaultlib \
  /out:/tmp/windows-malformed.exe /tmp/windows-malformed.obj \
  > /tmp/windows-malformed-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-malformed-status.txt
/usr/bin/clang --no-default-config --target=x86_64-pc-windows-msvc \
  -ffreestanding -fno-builtin -fno-stack-protector -nostdinc -O0 \
  -emit-llvm -c /root/demo/windows-hello.c -o /tmp/windows-hello.bc \
  > /tmp/windows-bitcode-compile-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-bitcode-compile-status.txt
/usr/bin/lld-link /machine:x64 /subsystem:windows /entry:demo_start /nodefaultlib \
  /out:/tmp/windows-bitcode.exe /tmp/windows-hello.bc \
  > /tmp/windows-bitcode-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-bitcode-status.txt
/usr/bin/lld-link /debug /out:/tmp/windows-debug.exe /tmp/windows-hello.obj \
  > /tmp/windows-debug-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-debug-status.txt
printf '/debug\n' > /tmp/windows-options.rsp
/usr/bin/lld-link @/tmp/windows-options.rsp /out:/tmp/windows-response.exe \
  /tmp/windows-hello.obj > /tmp/windows-response-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-response-status.txt
/usr/bin/lld-link /lldltocache:/tmp/windows-lto-cache /out:/tmp/windows-lto-option.exe /tmp/windows-hello.obj \
  > /tmp/windows-lto-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-lto-status.txt
/usr/bin/lld-link '/opt:ref,lldlto=1' /out:/tmp/windows-opt-lto.exe /tmp/windows-hello.obj \
  > /tmp/windows-opt-lto-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-opt-lto-status.txt
/usr/bin/lld-link '/fat-lto-objects' /out:/tmp/windows-fat-lto.exe /tmp/windows-hello.obj \
  > /tmp/windows-fat-lto-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-fat-lto-status.txt
/usr/bin/lld-link '/lldsavetemps' /out:/tmp/windows-save-temps.exe /tmp/windows-hello.obj \
  > /tmp/windows-save-temps-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-save-temps-status.txt
/usr/bin/lld-link '/lldemit:llvm' /out:/tmp/windows-emit-llvm.exe /tmp/windows-hello.obj \
  > /tmp/windows-emit-llvm-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-emit-llvm-status.txt
/usr/bin/lld-link '/?lldsavetemps' /out:/tmp/windows-alt-prefix-lto.exe /tmp/windows-hello.obj \
  > /tmp/windows-alt-prefix-lto-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-alt-prefix-lto-status.txt
/usr/bin/lld-link '/lib' /out:/tmp/windows-archive.exe /tmp/windows-hello.obj \
  > /tmp/windows-archive-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-archive-status.txt
/usr/bin/lld-link --version > /tmp/windows-recovery-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-recovery-status.txt
# Compiler errors must also be nonzero, followed by a successful rebuild.
printf '#error WINDOWS_DEMO_EXPECTED_COMPILE_FAILURE\n' > /tmp/windows-invalid.c
/usr/bin/clang --no-default-config --target=x86_64-pc-windows-msvc \
  -ffreestanding -fno-builtin -fno-stack-protector -nostdinc -O0 \
  -c /tmp/windows-invalid.c -o /tmp/windows-invalid.obj \
  > /tmp/windows-invalid-compile-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-invalid-compile-status.txt
# A second real compile/link must work after the rejected requests. Bash's
# built-in file read and replacement avoid depending on an editor or sed.
WINDOWS_DEMO_TEXT="$(</root/demo/windows-hello.c)"
printf '%s\n' "${WINDOWS_DEMO_TEXT/Hello from WASTE!/Hello again from WASTE!}" \
  > /tmp/windows-changed.c
/usr/bin/clang --no-default-config --target=x86_64-pc-windows-msvc \
  -ffreestanding -fno-builtin -fno-stack-protector -nostdinc -O0 \
  -c /tmp/windows-changed.c -o /tmp/windows-changed.obj \
  > /tmp/windows-changed-compile-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-changed-compile-status.txt
/usr/bin/lld-link /machine:x64 /subsystem:windows /entry:demo_start /nodefaultlib \
  /out:/tmp/windows-changed.exe /tmp/windows-changed.obj \
  /usr/lib/waste/windows-demo/kernel32.lib /usr/lib/waste/windows-demo/user32.lib > /tmp/windows-changed-link-output.txt 2>&1
printf '%s\n' "$?" > /tmp/windows-changed-link-status.txt
/bin/wast --verbose "${WINDOWS_ARTIFACT_WAST:-/root/test/system/windows-demo/artifacts.wast}" || status=1
