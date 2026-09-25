#!/usr/bin/env python3
"""Relink one Coreutils Wasm image with the shared WASTE libc memory."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


RUNTIME = "coreutils-runtime"


def run(command: list[str], environment: dict[str, str]) -> None:
    subprocess.run(command, check=True, env=environment)


def rewrite_libc(source: str) -> str:
    memory = re.search(r'^ \(memory (\$[^ ]+) 4\)$\n', source, re.MULTILINE)
    if not memory:
        raise RuntimeError("guest libc memory declaration changed")
    memory_name = memory.group(1)
    source = source[:memory.start()] + source[memory.end():]
    table = re.search(r'^ \(table (\$[^ ]+) 1 1 funcref\)$\n', source, re.MULTILINE)
    if not table:
        raise RuntimeError("guest libc table declaration changed")
    table_name = table.group(1)
    source = source[:table.start()] + source[table.end():]
    first_global = re.search(r'^ \(global ', source, re.MULTILINE)
    if not first_global:
        raise RuntimeError("guest libc globals changed")
    imports = (f' (import "{RUNTIME}" "memory" (memory {memory_name} 4))\n'
               f' (import "{RUNTIME}" "table" (table {table_name} 4 4 funcref))\n')
    source = source[:first_global.start()] + imports + source[first_global.start():]
    source = re.sub(r'^ \(export "memory" \(memory [^)]+\)\)$\n', "",
                    source, count=1, flags=re.MULTILINE)
    source = re.sub(r'^ \(export "__indirect_function_table"[^\n]*\)$\n', "",
                    source, count=1, flags=re.MULTILINE)
    source = re.sub(r'^ \(module\b', '(module $waste_libc', source, count=1)
    return source


def rewrite_utility(source: str, utility: str) -> str:
    memory = re.search(r'^ \(memory (\$[^ ]+) 2\)$\n', source, re.MULTILINE)
    table = re.search(r'^ \(table (\$[^ ]+) 4 4 funcref\)$\n', source, re.MULTILINE)
    if not memory or not table:
        raise RuntimeError(f"Coreutils {utility} memory declaration changed")
    memory_name = memory.group(1)
    source = source[:memory.start()] + source[memory.end():]
    table = re.search(r'^ \(table (\$[^ ]+) 4 4 funcref\)$\n', source, re.MULTILINE)
    if not table:
        raise RuntimeError(f"Coreutils {utility} table declaration changed")
    table_name = table.group(1)
    source = source[:table.start()] + source[table.end():]
    first_global = re.search(r'^ \(global ', source, re.MULTILINE)
    if not first_global:
        raise RuntimeError(f"Coreutils {utility} globals changed")
    imports = (f' (import "{RUNTIME}" "memory" (memory {memory_name} 2))\n'
               f' (import "{RUNTIME}" "table" (table {table_name} 4 4 funcref))\n')
    source = source[:first_global.start()] + imports + source[first_global.start():]
    source = re.sub(r'^ \(export "memory" \(memory [^)]+\)\)$\n', "",
                    source, count=1, flags=re.MULTILINE)
    source = re.sub(r'^ \(export "__indirect_function_table"[^\n]*\)$\n', "",
                    source, count=1, flags=re.MULTILINE)
    source = re.sub(r'^ \(module\b', f'(module $coreutils_{utility}', source, count=1)
    return source


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--utility", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--libc", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.repo_root.resolve()
    environment = os.environ.copy()
    local_bin = root / "build/engine/toolchain/usr/bin"
    local_lib = root / "build/engine/toolchain/usr/lib"
    if not shutil.which("wasm-as") and (local_bin / "wasm-as").is_file():
        environment["PATH"] = str(local_bin) + os.pathsep + environment.get("PATH", "")
        environment["LD_LIBRARY_PATH"] = str(local_lib) + os.pathsep + environment.get("LD_LIBRARY_PATH", "")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.output.parent) as temporary_name:
        temporary = Path(temporary_name)
        utility_wat = temporary / f"{args.name}.wat"
        libc_wat = temporary / "libc.wat"
        runtime_wat = temporary / "runtime.wat"
        runtime_wasm = temporary / "runtime.wasm"
        libc_wasm = temporary / "libc.wasm"
        utility_wasm = temporary / f"{args.name}-rewritten.wasm"
        runtime_wat.write_text(
            f'''(module $coreutils_runtime
  (memory (export "memory") 4 4096)
  (table (export "table") 4 4 funcref))
''', encoding="utf-8")
        run(["wasm-dis", str(args.libc), "-o", str(temporary / "libc-source.wat")], environment)
        run(["wasm-dis", str(args.utility), "-o", str(temporary / f"{args.name}-source.wat")], environment)
        libc_wat.write_text(rewrite_libc((temporary / "libc-source.wat").read_text()), encoding="utf-8")
        utility_wat.write_text(rewrite_utility((temporary / f"{args.name}-source.wat").read_text(), args.name), encoding="utf-8")
        assemble_flags = ["--enable-bulk-memory", "--enable-nontrapping-float-to-int"]
        run(["wasm-as", str(runtime_wat), "-o", str(runtime_wasm), *assemble_flags], environment)
        run(["wasm-as", str(libc_wat), "-o", str(libc_wasm), *assemble_flags], environment)
        run(["wasm-as", str(utility_wat), "-o", str(utility_wasm), *assemble_flags], environment)
        run(["wasm-merge", str(runtime_wasm), RUNTIME, str(libc_wasm), "env",
             str(utility_wasm), args.name, "-o", str(args.output), "--enable-bulk-memory",
             "--enable-nontrapping-float-to-int"], environment)
    print(f"Built {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
