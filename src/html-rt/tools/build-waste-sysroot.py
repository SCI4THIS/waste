#!/usr/bin/env python3
"""Copy the mounted guest SDK into a hermetic application build sysroot."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil

import guest_sdk


def write_wrapper(path, repo_root, profile=False):
    toolchain = repo_root / "build/engine/toolchain/usr"
    extra = ('  -DWASTE_LEGACY_DECLARATIONS -I "$sysroot/profiles/coreutils/include" '
             '-include "$sysroot/profiles/coreutils/include/waste-gnulib-compat.h" \\\n') if profile else ""
    path.write_text(
        '#!/bin/sh\nset -eu\n'
        'wrapper_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
        'sysroot=$(CDPATH= cd -- "$wrapper_dir/.." && pwd)\n'
        f'toolchain="{toolchain}"\n'
        'if [ -x "$toolchain/bin/wasm-ld" ]; then\n'
        '  export LD_LIBRARY_PATH="$toolchain/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
        '  set -- -B "$toolchain/bin" "$@"\nfi\n'
        'exec clang --target=wasm32 -std=c11 -ffreestanding -fno-builtin \\\n'
        '  -DWASTE_WASM -fno-stack-protector -fdata-sections -ffunction-sections \\\n'
        '  -nostdinc -nostdlib -isystem "$sysroot/include" \\\n'
        '  -isystem "$sysroot/lib/waste/cc/include" \\\n'
        + extra + '  -Wl,--no-entry "$@"\n', encoding="utf-8")
    path.chmod(0o755)


def build(repo_root, output):
    if not output.is_relative_to(repo_root / "build") or output == repo_root / "build":
        raise ValueError("generated sysroots must be a subdirectory of build/")
    root = repo_root / "src/vfs"
    sdk = guest_sdk.audit(root)
    output.mkdir(parents=True, exist_ok=True)
    # These exact generated subtrees belong to this builder. Remove stale
    # headers so obsolete public/host/private declarations cannot shadow SDK.
    for source, destination in (
        (root / "usr/include", output / "include"),
        (root / "usr/lib/waste/cc/include", output / "lib/waste/cc/include"),
        (repo_root / "src/html-rt/profiles/coreutils/include", output / "profiles/coreutils/include"),
    ):
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(source, destination)
    profile = output / "profiles/coreutils/include"
    for name in ("obstack", "unistr"):
        source = repo_root / f"build/coreutils/source/gnulib/lib/{name}.in.h"
        if source.is_file():
            shutil.copyfile(source, profile / f"{name}.h")
    crt = repo_root / "src/libc/waste-crt.c"
    shutil.copyfile(crt, output / "lib/waste-crt.c")
    (output / "bin").mkdir(exist_ok=True)
    write_wrapper(output / "bin/waste-wasm-clang", repo_root)
    write_wrapper(output / "bin/waste-coreutils-clang", repo_root, profile=True)
    (output / "manifest.json").write_text(json.dumps(dict(
        format=2, sdk=sdk, sdk_sha256=guest_sdk.sha(root / "usr/share/waste/sdk.json"),
        profiles=["default", "coreutils (gnulib/SELinux compatibility declarations; not guest libc)"],
        crt=dict(source="src/libc/waste-crt.c", sha256=guest_sdk.sha(crt)),
    ), indent=2, sort_keys=True) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build(args.repo_root.resolve(), args.output.resolve())


if __name__ == "__main__":
    main()
