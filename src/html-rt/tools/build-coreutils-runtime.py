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

from libc_sources import guest_include_flags, libc_source_paths


RUNTIME = "coreutils-runtime"

DATA_SEGMENT = re.compile(
    r'^\s*\(data\s+\$\w+\s+\(i32\.const\s+(\d+)\)\s+"((?:[^"\\]|\\.)*)"\)',
    re.MULTILINE,
)


def compute_data_end(wat_source: str) -> int:
    """Scan all data segments and return the first address past the last byte."""
    end = 0
    for match in DATA_SEGMENT.finditer(wat_source):
        offset = int(match.group(1))
        # Each escaped byte (\\xx) is 1 byte; each literal char is 1 byte.
        raw = match.group(2)
        length = 0
        i = 0
        while i < len(raw):
            if raw[i] == '\\':
                i += 1
                if i < len(raw) and raw[i] in ('n', 't', 'r', '\\', '"', "'"):
                    length += 1
                    i += 1
                else:
                    # hex escape: \xx
                    i += 2
                    length += 1
            else:
                length += 1
                i += 1
        segment_end = offset + length
        if segment_end > end:
            end = segment_end
    return end


def extract_global_value(wat_source: str, export_name: str) -> int | None:
    """Extract the initial value of an exported global from WAT source."""
    export_match = re.search(
        r'\(export "' + re.escape(export_name)
        + r'" \(global (\$[^ )]+)\)\)', wat_source)
    if not export_match:
        return None
    global_name = export_match.group(1)
    pattern = re.compile(
        r'\(global ' + re.escape(global_name)
        + r' (?:\(mut i32\)|i32) \(i32\.const (\d+)\)\)')
    match = pattern.search(wat_source)
    if not match:
        return None
    return int(match.group(1))


def extract_heap_base(wat_source: str) -> int | None:
    """Extract the __heap_base global value from WAT source.

    The linker sets __heap_base past all data segments AND BSS, so this
    value is the true end of the module's static footprint — unlike
    compute_data_end() which only counts explicit data segments.
    """
    return extract_global_value(wat_source, "__heap_base")


def fixup_heap_base(wat_source: str, data_end: int) -> str:
    """Update the __heap_base global so the allocator starts past all data."""
    aligned = (data_end + 15) & ~15
    # Find the export to identify which global is __heap_base.
    export_match = re.search(
        r'\(export "__heap_base" \(global (\$[^ )]+)\)\)', wat_source)
    if not export_match:
        return wat_source
    global_name = export_match.group(1)
    # Update that global's initial value.
    pattern = re.compile(
        r'(\(global ' + re.escape(global_name)
        + r' i32 \(i32\.const )(\d+)(\)\))')
    match = pattern.search(wat_source)
    if not match:
        return wat_source
    old_value = int(match.group(2))
    if aligned <= old_value:
        return wat_source
    return wat_source[:match.start(2)] + str(aligned) + wat_source[match.end(2):]


def run(command: list[str], environment: dict[str, str]) -> None:
    subprocess.run(command, check=True, env=environment)


def _replace_stack_pointer(source: str, module_label: str) -> str:
    """Replace a module's own __stack_pointer global with an import from the runtime.

    The compiled module declares a mutable i32 global for the stack pointer
    and exports it as ``__stack_pointer``.  After this rewrite the module
    imports the runtime's shared stack pointer instead so all merged modules
    share a single stack.
    """
    # Identify the stack-pointer global name from the export.
    sp_export = re.search(
        r'^ \(export "__stack_pointer" \(global (\$[^ )]+)\)\)$\n',
        source, re.MULTILINE)
    if not sp_export:
        return source
    sp_name = sp_export.group(1)
    # Remove the global declaration.
    sp_global = re.search(
        r'^ \(global ' + re.escape(sp_name) + r' \(mut i32\) \(i32\.const \d+\)\)$\n',
        source, re.MULTILINE)
    if not sp_global:
        return source
    source = source[:sp_global.start()] + source[sp_global.end():]
    # Remove the export line (re-search since positions shifted).
    source = re.sub(
        r'^ \(export "__stack_pointer" \(global ' + re.escape(sp_name) + r'\)\)$\n',
        "", source, count=1, flags=re.MULTILINE)
    # Insert an import of the runtime's shared stack pointer, keeping the
    # same local name so all existing global.get/set references still work.
    first_import = re.search(r'^ \(import ', source, re.MULTILINE)
    if first_import:
        insert_pos = first_import.start()
    else:
        first_global = re.search(r'^ \(global ', source, re.MULTILINE)
        insert_pos = first_global.start() if first_global else 0
    sp_import = (f' (import "{RUNTIME}" "__stack_pointer"'
                 f' (global {sp_name} (mut i32)))\n')
    source = source[:insert_pos] + sp_import + source[insert_pos:]
    return source


def rewrite_libc(source: str, table_size: int = 4,
                 memory_initial: int = 4) -> str:
    memory = re.search(
        r'^ \(memory (\$[^ ]+) ' + str(memory_initial) + r'\)$\n',
        source, re.MULTILINE)
    if not memory:
        raise RuntimeError("guest libc memory declaration changed")
    memory_name = memory.group(1)
    source = source[:memory.start()] + source[memory.end():]
    table = re.search(r'^ \(table (\$[^ ]+) 1 1 funcref\)$\n', source, re.MULTILINE)
    if not table:
        raise RuntimeError("guest libc table declaration changed")
    table_name = table.group(1)
    source = source[:table.start()] + source[table.end():]
    source = _replace_stack_pointer(source, "libc")
    first_global = re.search(r'^ \(global ', source, re.MULTILINE)
    first_import = re.search(r'^ \(import ', source, re.MULTILINE)
    insert_pos = first_import.start() if first_import else (first_global.start() if first_global else 0)
    imports = (f' (import "{RUNTIME}" "memory"'
               f' (memory {memory_name} {memory_initial}))\n'
               f' (import "{RUNTIME}" "table" (table {table_name} {table_size} {table_size} funcref))\n')
    source = source[:insert_pos] + imports + source[insert_pos:]
    source = re.sub(r'^ \(export "memory" \(memory [^)]+\)\)$\n', "",
                    source, count=1, flags=re.MULTILINE)
    source = re.sub(r'^ \(export "__indirect_function_table"[^\n]*\)$\n', "",
                    source, count=1, flags=re.MULTILINE)
    source = re.sub(r'^ \(module\b', '(module $waste_libc', source, count=1)
    return source


def rewrite_utility(source: str, utility: str) -> tuple[str, int, int]:
    """Rewrite utility WAT and return source, table size, and memory minimum."""
    memory = re.search(r'^ \(memory (\$[^ ]+) (\d+)(?: \d+)?\)$\n', source, re.MULTILINE)
    table = re.search(r'^ \(table (\$[^ ]+) (\d+) (\d+) funcref\)$\n', source, re.MULTILINE)
    if not memory or not table:
        raise RuntimeError(f"Coreutils {utility} memory/table declaration changed")
    memory_name = memory.group(1)
    memory_initial = int(memory.group(2))
    table_name = table.group(1)
    table_size = int(table.group(3))
    source = source[:memory.start()] + source[memory.end():]
    table = re.search(r'^ \(table (\$[^ ]+) \d+ \d+ funcref\)$\n', source, re.MULTILINE)
    if not table:
        raise RuntimeError(f"Coreutils {utility} table declaration changed")
    source = source[:table.start()] + source[table.end():]
    source = _replace_stack_pointer(source, utility)
    first_global = re.search(r'^ \(global ', source, re.MULTILINE)
    first_import = re.search(r'^ \(import ', source, re.MULTILINE)
    insert_pos = first_import.start() if first_import else (first_global.start() if first_global else 0)
    imports = (f' (import "{RUNTIME}" "memory" (memory {memory_name} {memory_initial}))\n'
               f' (import "{RUNTIME}" "table" (table {table_name} {table_size} {table_size} funcref))\n')
    source = source[:insert_pos] + imports + source[insert_pos:]
    source = re.sub(r'^ \(export "memory" \(memory [^)]+\)\)$\n', "",
                    source, count=1, flags=re.MULTILINE)
    source = re.sub(r'^ \(export "(?:__indirect_function_table|table)" \(table [^)]+\)\)$\n', "",
                    source, count=1, flags=re.MULTILINE)
    # Remove __heap_base export to avoid conflict with the runtime module.
    source = re.sub(r'^ \(export "__heap_base" \(global [^)]+\)\)$\n', "",
                    source, count=1, flags=re.MULTILINE)
    source = re.sub(r'^ \(module\b', f'(module $coreutils_{utility}', source, count=1)
    return source, table_size, memory_initial


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
    if (not shutil.which("wasm-ld") or not shutil.which("wasm-as")) and local_bin.is_dir():
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
        # Build a POSIX-IO-enabled libc so write() calls reach the kernel.
        core_wat_path = temporary / "core.wat"
        core_wasm = temporary / "core.wasm"
        helpers_wasm = temporary / "helpers.wasm"
        merged_libc_wasm = temporary / "merged-libc.wasm"
        # Read the utility first: its linked memory minimum includes the stack
        # reservation and is therefore the minimum required by every module
        # participating in the one-memory merge.
        run(["wasm-dis", str(args.utility), "-o",
             str(temporary / f"{args.name}-source.wat")], environment)
        utility_source_text = (temporary / f"{args.name}-source.wat").read_text()
        utility_source, table_size, memory_initial = rewrite_utility(
            utility_source_text, args.name)
        core_source = (root / "src" / "html-rt" / "lib" / "stdlib.wat").read_text(
            encoding="utf-8")
        core_memory = '  (memory (export "memory") 4)'
        if core_memory not in core_source:
            raise RuntimeError("guest libc core memory declaration changed")
        core_source = core_source.replace(
            core_memory,
            f'  (memory (export "memory") {memory_initial})',
            1,
        )
        core_wat_path.write_text(core_source, encoding="utf-8")
        assemble_flags = ["--enable-bulk-memory", "--enable-nontrapping-float-to-int"]
        run(["wasm-as", str(core_wat_path), "-o", str(core_wasm), *assemble_flags],
            environment)
        # Disassemble the utility first so we can place the libc
        # data/BSS above ALL of the utility's static footprint (data
        # segments AND BSS).  __heap_base accounts for both; the old
        # compute_data_end() only counted data segments and missed BSS,
        # causing destructive overlap with uninitialized globals like
        # stdout/stdin/stderr.
        utility_heap_base = extract_heap_base(utility_source_text)
        if utility_heap_base is not None:
            libc_global_base = (utility_heap_base + 15) & ~15
        else:
            # Fallback: scan data segments only (no BSS info available).
            utility_data_end = compute_data_end(utility_source_text)
            libc_global_base = (utility_data_end + 15) & ~15
        if libc_global_base < 65536:
            libc_global_base = 65536
        run([
            "clang", "--target=wasm32", "-O2", "-nostdlib", "-fno-builtin",
            *guest_include_flags(root),
            "-DWASTE_POSIX_IO",
            *(str(path) for path in libc_source_paths(root)),
            "-Wl,--no-entry", "-Wl,--import-memory",
            f"-Wl,--initial-memory={memory_initial * 65536}",
            "-Wl,--allow-undefined", "-Wl,--export-all", "-Wl,--strip-all",
            f"-Wl,--global-base={libc_global_base}",
            "-o", str(helpers_wasm),
        ], environment)
        run(["wasm-merge", str(core_wasm), "env", str(helpers_wasm), "helpers",
             "-o", str(merged_libc_wasm), *assemble_flags], environment)
        run(["wasm-dis", str(merged_libc_wasm), "-o", str(temporary / "libc-source.wat")],
            environment)
        libc_wat.write_text(rewrite_libc(
            (temporary / "libc-source.wat").read_text(), table_size,
            memory_initial), encoding="utf-8")
        utility_wat.write_text(utility_source, encoding="utf-8")
        # Use the utility's original __stack_pointer value so the runtime
        # provides the same amount of stack the utility was linked with.
        utility_sp = extract_global_value(utility_source_text, "__stack_pointer")
        if utility_sp is None:
            utility_sp = 65536
        runtime_wat.write_text(
            f'''(module $coreutils_runtime
  (memory (export "memory") {max(4, memory_initial)} 4096)
  (table (export "table") {table_size} {table_size} funcref)
  (global (export "__stack_pointer") (mut i32) (i32.const {utility_sp})))
''', encoding="utf-8")
        run(["wasm-as", str(runtime_wat), "-o", str(runtime_wasm), *assemble_flags], environment)
        run(["wasm-as", str(libc_wat), "-o", str(libc_wasm), *assemble_flags], environment)
        run(["wasm-as", str(utility_wat), "-o", str(utility_wasm), *assemble_flags], environment)
        run(["wasm-merge", str(runtime_wasm), RUNTIME, str(libc_wasm), "env",
             str(utility_wasm), args.name, "-o", str(args.output), *assemble_flags],
            environment)
        # Post-merge: fix __heap_base so the allocator starts past all data
        # segments from both the libc and utility modules.
        merged_wat = temporary / "merged-fixup.wat"
        run(["wasm-dis", str(args.output), "-o", str(merged_wat)], environment)
        merged_source = merged_wat.read_text(encoding="utf-8")
        data_end = compute_data_end(merged_source)
        if data_end > 0:
            fixed = fixup_heap_base(merged_source, data_end)
            if fixed is not merged_source:
                merged_wat.write_text(fixed, encoding="utf-8")
                run(["wasm-as", str(merged_wat), "-o", str(args.output),
                     *assemble_flags], environment)
    print(f"Built {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
