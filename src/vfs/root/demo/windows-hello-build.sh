#!/bin/bash
set -e

# Build in the current guest directory; keep Windows SDK/CRT dependencies out.
clang --no-default-config --target=x86_64-pc-windows-msvc \
  -ffreestanding -fno-builtin -fno-stack-protector -nostdinc -O0 \
  -c /root/demo/windows-hello.c -o windows-hello.obj

lld-link /machine:x64 /subsystem:windows /entry:demo_start /nodefaultlib \
  /out:windows-hello.exe windows-hello.obj \
  /usr/lib/waste/windows-demo/kernel32.lib \
  /usr/lib/waste/windows-demo/user32.lib

printf 'Built %s/windows-hello.exe\n' "$PWD"
