#!/usr/bin/env python3
"""Build the WASTE guest libc as a PIC shared library (.so.wasm).

Produces libc.so.wasm with a dylink.0 section, suitable for loading via
native_store_load_library() in the engine.  This replaces the older
build-waste-libc.py pipeline (stdlib.wat + wasm-merge) with a single
wasm-ld -shared link of all C sources including allocator.c.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

from libc_sources import LIBC_C_SOURCES


def ensure_sysroot(repo_root: Path, output_base: Path) -> Path:
    """Refresh from the mounted SDK; never reuse an unchecked old sysroot."""
    build_sysroot = repo_root / "src" / "html-rt" / "tools" / "build-waste-sysroot.py"
    sysroot = output_base / "sysroot"
    subprocess.run(
        [sys.executable, str(build_sysroot),
         "--repo-root", str(repo_root),
         "--output", str(sysroot)],
        check=True,
    )
    return sysroot


def compile_objects(
    sources: list[Path],
    obj_dir: Path,
    sysroot: Path,
) -> list[Path]:
    """Compile C sources to PIC wasm32 object files."""
    cc = str(sysroot / "bin" / "waste-wasm-clang")
    obj_dir.mkdir(parents=True, exist_ok=True)

    pic_flags = ["-fPIC", "-fvisibility=default"]
    objects = []
    failed = []

    for source in sources:
        obj_name = source.stem + ".o"
        # Flatten sys/ subdirectory: sys/random.c -> sys_random.o
        if source.parent.name == "sys":
            obj_name = f"sys_{source.stem}.o"
        obj = obj_dir / obj_name

        cmd = [cc, "-c", "-O2"] + pic_flags + [
            "-DNDEBUG",
            str(source), "-o", str(obj),
        ]
        result = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            failed.append((source.name, result.stderr[:500]))
        else:
            objects.append(obj)

    if failed:
        print(f"compile: {len(objects)} ok, {len(failed)} failed")
        for name, err in failed[:20]:
            print(f"  FAIL {name}: {err.strip()[:400]}")
    else:
        print(f"compile: {len(objects)} objects ok")
    return objects


def link_shared_library(
    objects: list[Path],
    output: Path,
) -> None:
    """Link object files into a PIC shared library with dylink.0."""
    command = [
        "wasm-ld",
        "--shared",
        "--import-memory",
        "--import-table",
        "--export-all",
        "--allow-undefined",
        "--no-entry",
        "-o", str(output),
    ] + [str(o) for o in objects]
    subprocess.run(command, check=True)
    size = output.stat().st_size
    print(f"link: {output.name} ({size} bytes)")


def read_leb128(data: bytes, pos: int) -> tuple[int, int]:
    """Read an unsigned LEB128 value, return (value, new_pos)."""
    value = 0
    shift = 0
    while True:
        b = data[pos]
        pos += 1
        value |= (b & 0x7f) << shift
        shift += 7
        if not (b & 0x80):
            break
    return value, pos


def inspect_dylink(wasm_path: Path) -> None:
    """Print a summary of the dylink.0 section and export count."""
    data = wasm_path.read_bytes()
    pos = 8  # skip magic + version
    export_count = 0
    while pos < len(data):
        section_id = data[pos]
        pos += 1
        size, pos = read_leb128(data, pos)
        section_end = pos + size

        if section_id == 0:  # custom section
            name_len, name_start = read_leb128(data, pos)
            name = data[name_start:name_start + name_len].decode("utf-8", errors="replace")
            if name == "dylink.0":
                payload_pos = name_start + name_len
                # Parse subsections.
                while payload_pos < section_end:
                    sub_type = data[payload_pos]
                    payload_pos += 1
                    sub_size, payload_pos = read_leb128(data, payload_pos)
                    sub_end = payload_pos + sub_size
                    if sub_type == 1:  # WASM_DYLINK_MEM_INFO
                        memsize, payload_pos = read_leb128(data, payload_pos)
                        memalign, payload_pos = read_leb128(data, payload_pos)
                        tablesize, payload_pos = read_leb128(data, payload_pos)
                        tablealign, payload_pos = read_leb128(data, payload_pos)
                        print(f"dylink.0: memorysize={memsize} memalign={memalign} "
                              f"tablesize={tablesize} tablealign={tablealign}")
                    payload_pos = sub_end
        elif section_id == 7:  # export section
            count, epos = read_leb128(data, pos)
            export_count = count
        pos = section_end

    print(f"exports: {export_count}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path,
                        default=Path("build/libc-shared"))
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    lib_dir = repo_root / "src" / "html-rt" / "lib"

    # Gather all source files: allocator.c + libc C sources.
    sources: list[Path] = [lib_dir / "allocator.c"]
    for name in LIBC_C_SOURCES:
        sources.append(lib_dir / name)

    missing = [s for s in sources if not s.is_file()]
    if missing:
        for m in missing:
            print(f"error: source not found: {m}")
        return 1

    # Ensure sysroot.
    sysroot = ensure_sysroot(repo_root, output)
    print(f"sysroot: {sysroot}")

    # Compile.
    objects = compile_objects(sources, output / "objects", sysroot)
    if not objects:
        print("error: no objects compiled")
        return 1

    # Link shared library.
    lib_output = output / "libc.so.wasm"
    link_shared_library(objects, lib_output)

    # Inspect the result.
    inspect_dylink(lib_output)

    # Stage for VFS.
    vfs_stage = output / "vfs"
    vfs_stage.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(lib_output, vfs_stage / "libc.so.wasm")
    print(f"\nstaged: {vfs_stage / 'libc.so.wasm'}")
    print("VFS path: /usr/lib/libc.so.wasm")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
