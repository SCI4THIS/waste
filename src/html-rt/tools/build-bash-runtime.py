#!/usr/bin/env python3

import argparse
import re
import subprocess
import tempfile
from pathlib import Path

from shared_libc import rewrite

RUNTIME_MODULE = "waste-runtime"
RUNTIME_PAGES = 5
ARGV_ADDRESS = 240000
ENVP_ADDRESS = 240024
PACKAGE_ADDRESS = 240080
LOCALE_DIRECTORY_ADDRESS = 240085
HOSTTYPE_ADDRESS = 240104
OSTYPE_ADDRESS = 240111
EXE_SUFFIX_ADDRESS = 240132
STRING_ADDRESS = 240136
ENV_STRING_ADDRESS = 240168
ROOT_NAME_ADDRESS = 240248
ROOT_PASSWORD_ADDRESS = 240253
ROOT_GECOS_ADDRESS = 240255
ROOT_HOME_ADDRESS = 240260
ROOT_SHELL_ADDRESS = 240266
HOSTNAME_ADDRESS = 240276
ALLOCATOR_BASE = 327680
BASH_STDIN_SLOT = 129176
BASH_STDOUT_SLOT = 112228
BASH_STDERR_SLOT = 112232
COMMAND_MARKER = "__WASTE_BASH_COMMAND__"
ABI_CLEANUP_ADAPTERS = (
    # (table index, function index, pass dispatcher argument)
    (135, 1307, True),
    (138, 453, True),
    # read_builtin registers pop_scope as an unwind cleanup too. Its native
    # void(char *) callback needs the dispatcher's int(void *) adapter.
    (145, 1268, True),
    (455, 2260, False),
    (458, 1336, False),
)
SHARED_TABLE_SIZE = 488 + len(ABI_CLEANUP_ADAPTERS)


def run(command: list[str], environment: dict[str, str] | None = None) -> None:
    subprocess.run(command, check=True, env=environment)


def module_name(source: str, name: str) -> str:
    return re.sub(r"\(module\b", f"(module ${name}", source, count=1)


def rewrite_bash(source: str) -> str:
    # Bash already imports memory and table from waste-runtime (compiled with
    # --import-memory --import-table or equivalent WAT edits).  Expand the
    # imported table to accommodate the ABI adapter entries added below.
    source, tables = re.subn(
        rf'^  \(import "{RUNTIME_MODULE}" "table" \(table \(;0;\) 488 488 funcref\)\)$',
        f'  (import "{RUNTIME_MODULE}" "table" (table (;0;) {SHARED_TABLE_SIZE} funcref))',
        source, count=1, flags=re.MULTILINE,
    )
    if tables != 1:
        raise RuntimeError("bash.wat waste-runtime table import not found")
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
    environment_entries = (
        b"HOME=/root",
        b"USER=root",
        b"LOGNAME=root",
        b"PWD=/root",
        b"PATH=/bin:/usr/bin",
        b"TERM=xterm",
    )
    environment = b"\0".join(environment_entries) + b"\0"
    # wasm32 char ** entries are packed i32 pointers. Padding each pointer to
    # eight bytes makes Bash see the first zero pad as the envp terminator, so
    # only HOME survives and Bash synthesizes defaults such as TERM=dumb.
    environment_pointers = b"".join(
        (ENV_STRING_ADDRESS + offset).to_bytes(4, "little")
        for offset in (0, *(sum(len(entry) + 1 for entry in environment_entries[:index])
                            for index in range(1, len(environment_entries))))
    ) + b"\0\0\0\0"
    root_profile = b"root\0x\0root\0/root\0/bin/bash\0waste\0"
    if interactive:
        pointers = (
            STRING_ADDRESS.to_bytes(4, "little")
            + (STRING_ADDRESS + 5).to_bytes(4, "little")
            + (STRING_ADDRESS + 12).to_bytes(4, "little")
            + b"\0\0\0\0"
        )
        argument_data = "bash\\00--norc\\00-i\\00"
    else:
        pointers = (
            STRING_ADDRESS.to_bytes(4, "little")
            + (STRING_ADDRESS + 5).to_bytes(4, "little")
            + (STRING_ADDRESS + 8).to_bytes(4, "little")
            + b"\0\0\0\0"
        )
        argument_data = f"bash\\00-c\\00{COMMAND_MARKER}\\00"
    return f'''(module $waste_runtime
  (import "waste_kernel" "dlopen_v1" (func $dlopen (param i32 i32 i32) (result i32)))
  (import "waste_kernel" "dlsym_v1" (func $dlsym (param i32 i32 i32) (result i32)))
  (type $initialize (func (param i32) (result i32)))
  (table (export "table") {SHARED_TABLE_SIZE} funcref)
  (global (export "__stack_pointer") (mut i32) (i32.const 262144))
  (data (i32.const 239900) "/usr/lib/libc.so.wasm\\00")
  (data (i32.const 239930) "waste_allocator_init\\00")
  (func (export "waste_allocator_init") (param $base i32) (result i32)
    (local $handle i32) (local $initialize i32)
    (local.set $handle (call $dlopen (i32.const 239900) (i32.const 21) (i32.const 2)))
    (if (i32.eqz (local.get $handle)) (then (return (i32.const 0))))
    (local.set $initialize (call $dlsym (local.get $handle) (i32.const 239930) (i32.const 20)))
    (if (i32.eqz (local.get $initialize)) (then (return (i32.const 0))))
    (call_indirect (type $initialize) (local.get $base) (local.get $initialize)))
  (memory (export "memory") {RUNTIME_PAGES})
  (data (i32.const {ARGV_ADDRESS}) "{wat_bytes(pointers)}")
  (data (i32.const {ENVP_ADDRESS}) "{wat_bytes(environment_pointers)}")
  (data (i32.const {PACKAGE_ADDRESS}) "bash\\00/usr/share/locale\\00\\00wasm32\\00browser\\00wasm32-waste\\00\\00")
  (data (i32.const {STRING_ADDRESS}) "{argument_data}")
  (data (i32.const {ENV_STRING_ADDRESS}) "{wat_bytes(environment)}")
  (data (i32.const {ROOT_NAME_ADDRESS}) "{wat_bytes(root_profile)}"))
(register "{RUNTIME_MODULE}" $waste_runtime)'''


def main() -> None:
    parser = argparse.ArgumentParser(description="Build the Bash bootstrap using installed libc.so.wasm")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--interactive", action="store_true",
                        help="launch Bash on the virtual terminal instead of using bash -c")
    args = parser.parse_args()

    root = args.repo_root.resolve()
    output = args.output or root / "build" / "html-rt" / "bash-runtime.wast"
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(dir=output.parent) as temporary_name:
        temporary = Path(temporary_name)
        linked_bash_wat = temporary / "linked-bash.wat"
        linked_bash_wasm = temporary / "linked-bash.wasm"
        optimized_bash_wasm = temporary / "optimized-bash.wasm"

        bash_source = rewrite_bash((root / "examples" / "bash.wat").read_text(encoding="utf-8"))
        linked_bash_wat.write_text(bash_source, encoding="utf-8")
        run(["wasm-as", str(linked_bash_wat), "-o", str(linked_bash_wasm)])
        run(["wasm-opt", str(linked_bash_wasm), "-O2", "--strip-debug",
             "-o", str(optimized_bash_wasm)])
        # Retain the legacy typed kernel adapters: Bash uses i64 off_t
        # and an i32-returning __fpurge instead of libc's void API.
        rewrite(optimized_bash_wasm, root / "src/vfs/lib/libc.so.wasm",
                kernel_functions=("lseek", "__fpurge"))
        bash_binary = wat_bytes(optimized_bash_wasm.read_bytes())

    launch = "\n".join([
        ";; Generated by src/html-rt/tools/build-bash-runtime.py; do not edit.",
        runtime_module(args.interactive),
        f'(assert_return (invoke "waste_allocator_init" (i32.const {ALLOCATOR_BASE})) (i32.const 1))',
        '(register "env")',
        '(assert_return (invoke "waste_stdio_init" (i32.const 65536)) (i32.const 1))',
        f'(assert_return (invoke "waste_stdio_bind" '
        f'(i32.const {BASH_STDIN_SLOT}) (i32.const {BASH_STDOUT_SLOT}) '
        f'(i32.const {BASH_STDERR_SLOT})) (i32.const 1))',
        f'(invoke "waste_identity_set" (i32.const 0) (i32.const 0) '
        f'(i32.const 0) (i32.const 0) (i32.const {HOSTNAME_ADDRESS}))',
        f'(invoke "waste_passwd_set" (i32.const {ROOT_NAME_ADDRESS}) '
        f'(i32.const {ROOT_PASSWORD_ADDRESS}) (i32.const 0) (i32.const 0) '
        f'(i32.const {ROOT_GECOS_ADDRESS}) (i32.const {ROOT_HOME_ADDRESS}) '
        f'(i32.const {ROOT_SHELL_ADDRESS}))',
        f'(module $bash binary "{bash_binary}")',
        '(invoke "__wasm_call_ctors")',
        f'(invoke "main" (i32.const 3) (i32.const {ARGV_ADDRESS}) (i32.const {ENVP_ADDRESS}))',
        "",
    ])
    output.write_text(launch, encoding="utf-8")
    print(f"Built {output}")
    print("Mode: interactive" if args.interactive else f"Command marker: {COMMAND_MARKER}")


if __name__ == "__main__":
    main()
