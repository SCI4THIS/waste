#!/usr/bin/env python3

import argparse
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


RUNTIME_MODULE = "waste-runtime"
RUNTIME_PAGES = 5
ARGV_ADDRESS = 240000
ENVP_ADDRESS = 240024
PACKAGE_ADDRESS = 240032
LOCALE_DIRECTORY_ADDRESS = 240037
HOSTTYPE_ADDRESS = 240056
OSTYPE_ADDRESS = 240063
EXE_SUFFIX_ADDRESS = 240084
STRING_ADDRESS = 240096
ALLOCATOR_BASE = 327680
COMMAND_MARKER = "__WASTE_BASH_COMMAND__"
ABI_CLEANUP_ADAPTERS = (
    # (table index, function index, pass dispatcher argument)
    (135, 1307, True),
    (138, 453, True),
    (455, 2260, False),
    (458, 1336, False),
)
SHARED_TABLE_SIZE = 488 + len(ABI_CLEANUP_ADAPTERS)


def run(command: list[str], environment: dict[str, str] | None = None) -> None:
    subprocess.run(command, check=True, env=environment)


def module_name(source: str, name: str) -> str:
    return re.sub(r"\(module\b", f"(module ${name}", source, count=1)


def tool_environment(root: Path) -> dict[str, str]:
    environment = os.environ.copy()
    local_bin = root / "build" / "toolchain" / "usr" / "bin"
    local_lib = root / "build" / "toolchain" / "usr" / "lib"
    if not shutil.which("wasm-ld") and (local_bin / "wasm-ld").is_file():
        environment["PATH"] = str(local_bin) + os.pathsep + environment.get("PATH", "")
        environment["LD_LIBRARY_PATH"] = str(local_lib) + os.pathsep + environment.get("LD_LIBRARY_PATH", "")
    return environment


def rewrite_libc(source: str) -> str:
    memory = re.search(r"^ \(memory (\$[^ ]+) 5\)$", source, re.MULTILINE)
    if not memory:
        raise RuntimeError("integrated libc does not define the expected five-page memory")
    memory_name = memory.group(1)
    source = source[:memory.start()] + source[memory.end() + 1:]
    source = re.sub(
        r'^ \(import "env" "__indirect_function_table" (\(table [^\n]+\))\)$',
        rf' (import "{RUNTIME_MODULE}" "table" \1)', source,
        count=1, flags=re.MULTILINE,
    )
    first_global = re.search(r"^ \(global ", source, re.MULTILINE)
    if not first_global:
        raise RuntimeError("could not locate libc globals")
    memory_import = f' (import "{RUNTIME_MODULE}" "memory" (memory {memory_name} 5))\n'
    source = source[:first_global.start()] + memory_import + source[first_global.start():]

    stack_export = re.search(r'^ \(export "__stack_pointer" \(global (\$[^)]+)\)\)$', source, re.MULTILINE)
    if not stack_export:
        raise RuntimeError("could not locate libc stack pointer export")
    stack_name = re.escape(stack_export.group(1))
    source, replacements = re.subn(
        rf"^ \(global {stack_name} \(mut i32\) \(i32.const [0-9]+\)\)$",
        f" (global {stack_export.group(1)} (mut i32) (i32.const 262144))",
        source, count=1, flags=re.MULTILINE,
    )
    if replacements != 1:
        raise RuntimeError("could not relocate libc stack pointer")
    return module_name(source, "waste_libc")


def rewrite_bash(source: str) -> str:
    imports = list(re.finditer(r"^  \(import [^\n]+\)$", source, re.MULTILINE))
    if not imports:
        raise RuntimeError("bash.wat has no imports")
    insertion = imports[-1].end()
    shared = (
        f'\n  (import "{RUNTIME_MODULE}" "table" (table {SHARED_TABLE_SIZE} {SHARED_TABLE_SIZE} funcref))'
        f'\n  (import "{RUNTIME_MODULE}" "memory" (memory 5))'
    )
    source = source[:insertion] + shared + source[insertion:]
    source, tables = re.subn(r"^  \(table \(;0;\) 488 488 funcref\)\n", "", source, count=1, flags=re.MULTILINE)
    source, memories = re.subn(r"^  \(memory \(;0;\) 4\)\n", "", source, count=1, flags=re.MULTILINE)
    if tables != 1 or memories != 1:
        raise RuntimeError("bash.wat table or memory layout changed")
    replacements = {
        "1634953250": PACKAGE_ADDRESS,          # malformed multi-character "bash" macro
        "1634493730": LOCALE_DIRECTORY_ADDRESS, # malformed LOCALEDIR macro
        "1597387810": HOSTTYPE_ADDRESS,         # malformed HOSTTYPE macro
        "1937339170": OSTYPE_ADDRESS,           # malformed OSTYPE/MACHTYPE macros
        "1702389026": EXE_SUFFIX_ADDRESS,       # malformed executable suffix macro
    }
    for encoded, address in replacements.items():
        source = source.replace(f"i32.const {encoded}", f"i32.const {address}")

    # Bash's unwind-protect list casts this void cleanup callback to the
    # dispatcher's int(void *) signature and discards the result. Native C
    # permits that historical pattern, but WebAssembly tables are strictly
    # typed. Keep the table type sound with a slot-specific ABI adapter.
    element = re.search(r"^  \(elem \(;0;\) \(i32\.const 1\) func ([^\n]+)\)$", source, re.MULTILINE)
    if not element:
        raise RuntimeError("bash function table layout changed")
    functions = element.group(1).split()
    adapters = []
    adapter_names = []
    remapping = []
    for adapter_offset, (table_index, function_index, pass_argument) in enumerate(ABI_CLEANUP_ADAPTERS):
        position = table_index - 1
        if position >= len(functions) or functions[position] != str(function_index):
            raise RuntimeError(f"bash cleanup callback is no longer at table slot {table_index}")
        adapter_name = f"$waste_cleanup_{function_index}_adapter"
        adapter_names.append(adapter_name)
        adapter_slot = 488 + adapter_offset
        argument = "    local.get 0\n" if pass_argument else ""
        adapters.append(
            f"  (func {adapter_name} (type 4) (param i32) (result i32)\n"
            f"{argument}"
            f"    call {function_index}\n"
            f"    i32.const 0)\n"
        )
        remapping.append(
            f"    local.get 0\n"
            f"    i32.const {table_index}\n"
            f"    i32.eq\n"
            f"    if\n"
            f"      i32.const {adapter_slot}\n"
            f"      local.set 0\n"
            f"    end\n"
        )
    adapter_element = (
        f'  (elem (i32.const 488) func {" ".join(adapter_names)})\n'
    )
    source = source[:element.start()] + "".join(adapters) + element.group(0) + "\n" + adapter_element + source[element.end():]

    register_cleanup = re.search(
        r"(  \(func \(;1108;\) \(type 8\) \(param i32 i32\)\n    \(local i32\)\n)", source,
    )
    if not register_cleanup:
        raise RuntimeError("bash cleanup registry function changed")
    source = (
        source[:register_cleanup.end()]
        + "".join(remapping)
        + source[register_cleanup.end():]
    )
    return module_name(source, "bash")


def wat_bytes(data: bytes) -> str:
    return "".join(f"\\{byte:02x}" for byte in data)


def runtime_module(interactive: bool) -> str:
    if interactive:
        pointers = (
            STRING_ADDRESS.to_bytes(4, "little")
            + (STRING_ADDRESS + 5).to_bytes(4, "little")
            + (STRING_ADDRESS + 12).to_bytes(4, "little")
            + (STRING_ADDRESS + 24).to_bytes(4, "little")
            + b"\0\0\0\0"
        )
        argument_data = "bash\\00--norc\\00--noediting\\00-i\\00"
    else:
        pointers = (
            STRING_ADDRESS.to_bytes(4, "little")
            + (STRING_ADDRESS + 5).to_bytes(4, "little")
            + (STRING_ADDRESS + 8).to_bytes(4, "little")
            + b"\0\0\0\0"
        )
        argument_data = f"bash\\00-c\\00{COMMAND_MARKER}\\00"
    return f'''(module $waste_runtime
  (table (export "table") {SHARED_TABLE_SIZE} {SHARED_TABLE_SIZE} funcref)
  (memory (export "memory") {RUNTIME_PAGES})
  (data (i32.const {ARGV_ADDRESS}) "{wat_bytes(pointers)}")
  (data (i32.const {ENVP_ADDRESS}) "\\00\\00\\00\\00")
  (data (i32.const {PACKAGE_ADDRESS}) "bash\\00/usr/share/locale\\00\\00wasm32\\00browser\\00wasm32-waste\\00\\00")
  (data (i32.const {STRING_ADDRESS}) "{argument_data}"))
(register "{RUNTIME_MODULE}" $waste_runtime)'''


def main() -> None:
    parser = argparse.ArgumentParser(description="Relink Bash, waste-libc, and the WASTE runtime namespace")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--interactive", action="store_true",
                        help="launch Bash on the virtual terminal instead of using bash -c")
    args = parser.parse_args()

    root = args.repo_root.resolve()
    output = args.output or root / "build" / "bash" / "bash-runtime.wast"
    environment = tool_environment(root)
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(dir=output.parent) as temporary_name:
        temporary = Path(temporary_name)
        core_wat = temporary / "core.wat"
        core_wasm = temporary / "core.wasm"
        helpers_wasm = temporary / "helpers.wasm"
        merged_wasm = temporary / "libc.wasm"
        merged_wat = temporary / "libc.wat"
        linked_libc_wat = temporary / "linked-libc.wat"
        linked_libc_wasm = temporary / "linked-libc.wasm"
        linked_bash_wat = temporary / "linked-bash.wat"
        linked_bash_wasm = temporary / "linked-bash.wasm"
        optimized_libc_wasm = temporary / "optimized-libc.wasm"
        optimized_bash_wasm = temporary / "optimized-bash.wasm"

        core_source = (root / "libc" / "waste-libc.wat").read_text(encoding="utf-8")
        core_source, changed = re.subn(
            r'\(memory \(export "memory"\) 4\)',
            '(memory (export "memory") 5)', core_source, count=1,
        )
        if changed != 1:
            raise RuntimeError("guest libc core memory declaration changed")
        core_wat.write_text(core_source, encoding="utf-8")
        run(["wasm-as", str(core_wat), "-o", str(core_wasm), "--enable-bulk-memory"])
        run([
            "clang", "--target=wasm32", "-O2", "-nostdlib", "-fno-builtin",
            "-DWASTE_POSIX_IO", str(root / "libc" / "waste-libc-helpers.c"),
            str(root / "libc" / "waste-libc-extra.c"), "-Wl,--no-entry",
            "-Wl,--import-memory", "-Wl,--import-table", "-Wl,--global-base=262144",
            "-Wl,-z,stack-size=16384", "-Wl,--initial-memory=327680",
            "-Wl,--allow-undefined", "-Wl,--export-all", "-Wl,--strip-all",
            "-o", str(helpers_wasm),
        ], environment)
        run(["wasm-merge", str(core_wasm), "env", str(helpers_wasm), "helpers",
             "-o", str(merged_wasm), "--enable-bulk-memory"])
        run(["wasm-dis", str(merged_wasm), "-o", str(merged_wat)])
        libc_source = rewrite_libc(merged_wat.read_text(encoding="utf-8"))
        linked_libc_wat.write_text(libc_source, encoding="utf-8")
        run(["wasm-as", str(linked_libc_wat), "-o", str(linked_libc_wasm),
             "--enable-bulk-memory"])
        run(["wasm-opt", str(linked_libc_wasm), "-O2", "--strip-debug",
             "--enable-bulk-memory", "-o", str(optimized_libc_wasm)])

        bash_source = rewrite_bash((root / "src" / "bash.wat").read_text(encoding="utf-8"))
        linked_bash_wat.write_text(bash_source, encoding="utf-8")
        run(["wasm-as", str(linked_bash_wat), "-o", str(linked_bash_wasm)])
        run(["wasm-opt", str(linked_bash_wasm), "-O2", "--strip-debug",
             "-o", str(optimized_bash_wasm)])
        libc_binary = wat_bytes(optimized_libc_wasm.read_bytes())
        bash_binary = wat_bytes(optimized_bash_wasm.read_bytes())

    launch = "\n".join([
        ";; Generated by tools/build-bash-runtime.py; do not edit.",
        runtime_module(args.interactive),
        f'(module $waste_libc binary "{libc_binary}")',
        '(register "env" $waste_libc)',
        f'(module $bash binary "{bash_binary}")',
        f'(assert_return (invoke $waste_libc "waste_allocator_init" (i32.const {ALLOCATOR_BASE})) (i32.const 1))',
        '(assert_return (invoke $waste_libc "waste_stdio_init" (i32.const 65536)) (i32.const 1))',
        '(invoke $bash "__wasm_call_ctors")',
        f'(invoke $bash "main" (i32.const {4 if args.interactive else 3}) (i32.const {ARGV_ADDRESS}) (i32.const {ENVP_ADDRESS}))',
        "",
    ])
    output.write_text(launch, encoding="utf-8")
    print(f"Built {output}")
    print("Mode: interactive" if args.interactive else f"Command marker: {COMMAND_MARKER}")


if __name__ == "__main__":
    main()
