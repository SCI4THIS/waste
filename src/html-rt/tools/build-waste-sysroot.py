#!/usr/bin/env python3
"""Build the deterministic WASTE application sysroot and compiler wrapper."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path


HEADER_ROOT = Path("src/html-rt/lib/include")
CRT_SOURCE = Path("src/html-rt/lib/waste-crt.c")


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def write_wrapper(path: Path) -> None:
    path.write_text(
        """#!/bin/sh
set -eu
wrapper_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sysroot=$(CDPATH= cd -- "$wrapper_dir/.." && pwd)
toolchain_bin=$(CDPATH= cd -- "$wrapper_dir/../../../engine/toolchain/usr/bin" && pwd)
toolchain_lib=$(CDPATH= cd -- "$wrapper_dir/../../../engine/toolchain/usr/lib" && pwd)
if [ -x "$toolchain_bin/wasm-ld" ]; then
  export LD_LIBRARY_PATH="$toolchain_lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  set -- -B "$toolchain_bin" "$@"
fi
exec clang --target=wasm32 -std=c11 -ffreestanding -fno-builtin \\
  -DWASTE_WASM \\
  -fno-stack-protector -fdata-sections -ffunction-sections \\
  -nostdinc -nostdlib -isystem "$sysroot/include" \\
  -isystem "$(clang -print-resource-dir)/include" \\
  -include "$sysroot/include/waste-gnulib-compat.h" \\
  -Wl,--no-entry \\
  -I "$sysroot/include" "$@"
""",
        encoding="utf-8",
    )
    path.chmod(0o755)


def build(repo_root: Path, output: Path) -> None:
    headers = repo_root / HEADER_ROOT
    crt = repo_root / CRT_SOURCE
    if not headers.is_dir():
        raise RuntimeError(f"missing guest header directory: {headers}")
    if not crt.is_file():
        raise RuntimeError(f"missing CRT source: {crt}")

    include = output / "include"
    bin_dir = output / "bin"
    include.mkdir(parents=True, exist_ok=True)
    bin_dir.mkdir(parents=True, exist_ok=True)
    entries = []
    for source in sorted(headers.rglob("*.h")):
        relative = source.relative_to(headers)
        destination = include / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        entries.append({
            "path": f"include/{relative.as_posix()}",
            "source": (HEADER_ROOT / relative).as_posix(),
            "sha256": digest(source),
        })

    # obstack.h is a generated gnulib public header rather than a guest libc
    # header. Stage the pinned template when the coreutils source tree is
    # available; it is freestanding and does not depend on the host layout.
    coreutils_source = output.parent / "source"
    obstack_template = coreutils_source / "gnulib/lib/obstack.in.h"
    if obstack_template.is_file():
        obstack_destination = include / "obstack.h"
        shutil.copyfile(obstack_template, obstack_destination)
        entries.append({
            "path": "include/obstack.h",
            "source": str(obstack_template.relative_to(repo_root)),
            "sha256": digest(obstack_template),
        })

    # The Unicode string objects use gnulib's generated public declaration
    # template. Stage it when the pinned source tree is available so the
    # freestanding build sees the same complete API as the generated objects.
    unistr_template = coreutils_source / "gnulib/lib/unistr.in.h"
    if unistr_template.is_file():
        unistr_destination = include / "unistr.h"
        shutil.copyfile(unistr_template, unistr_destination)
        entries.append({
            "path": "include/unistr.h",
            "source": str(unistr_template.relative_to(repo_root)),
            "sha256": digest(unistr_template),
        })

    crt_destination = output / "lib" / "waste-crt.c"
    crt_destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(crt, crt_destination)
    write_wrapper(bin_dir / "waste-wasm-clang")

    manifest = {
        "format": 1,
        "target": "wasm32-unknown-unknown",
        "compiler": "clang --target=wasm32",
        "freestanding": True,
        "headers": entries,
        "crt": {"path": "lib/waste-crt.c", "source": CRT_SOURCE.as_posix(),
                "sha256": digest(crt)},
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build(args.repo_root.resolve(), args.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
