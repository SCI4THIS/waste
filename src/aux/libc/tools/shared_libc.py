"""Assign guest function imports to the installed, signature-checked libc.

Memory, table, relocation globals and kernel imports retain their namespaces.
The binary rewrite preserves function indices and every non-import section.
"""
from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO_ROOT / "src/html-rt/tools"))
from wasm_signatures import Reader, inspect


def leb(value):
    result = bytearray()
    while True:
        byte = value & 127
        value >>= 7
        result.append(byte | (128 if value else 0))
        if not value:
            return bytes(result)


def name_bytes(value):
    encoded = value.encode("utf-8")
    return leb(len(encoded)) + encoded


def rewrite(path, provider, output=None, kernel_functions=()):
    """Route matching env functions to libc; reject incompatible guest ABIs."""
    path, provider = Path(path), Path(provider)
    signatures = inspect(path)["imports"]
    exports = inspect(provider)["exports"]
    data = path.read_bytes()
    reader = Reader(data[8:])
    result = bytearray(data[:8])
    rewritten = 0
    while reader.at < len(reader.data):
        section_id = reader.byte()
        payload = reader.take(reader.integer())
        if section_id == 2:
            section = Reader(payload)
            count = section.integer()
            rebuilt = bytearray(leb(count))
            for _ in range(count):
                module, name, kind = section.name(), section.name(), section.byte()
                start = section.at
                if kind == 0:
                    section.integer()
                elif kind == 1:
                    section.byte()
                    section.limits()
                elif kind == 2:
                    section.limits()
                elif kind == 3:
                    section.take(2)
                elif kind == 4:
                    section.byte()
                    section.integer()
                else:
                    raise ValueError("unsupported guest import kind")
                descriptor = section.data[start:section.at]
                if (kind == 0 and module == "env" and name in exports
                        and name not in kernel_functions):
                    if signatures[module + ":" + name] != exports[name]:
                        raise ValueError(f"{path}: incompatible libc signature for {name}")
                    module = "libc"
                    rewritten += 1
                rebuilt.extend(name_bytes(module) + name_bytes(name) + bytes([kind]) + descriptor)
            if section.at != len(section.data):
                raise ValueError("trailing guest import bytes")
            payload = rebuilt
        result.extend(bytes([section_id]) + leb(len(payload)) + payload)
    Path(output or path).write_bytes(result)
    return rewritten


def check_imports(paths, provider, namespace="libc"):
    """Check library imports against the current provider's function types."""
    exports = inspect(provider)["exports"]
    count = 0
    for path in paths:
        for identity, signature in inspect(path)["imports"].items():
            module, name = identity.split(":", 1)
            if module == namespace:
                if exports.get(name) != signature:
                    raise ValueError(f"{path}: incompatible {namespace} import {name}")
                count += 1
    return count


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--check-imports", nargs="+", type=Path)
    action.add_argument("--rewrite", type=Path)
    action.add_argument("--configure-cache", type=Path,
                        help="Write function availability answers from the actual provider")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--provider", type=Path,
                        default=REPO_ROOT / "src/vfs/lib/libc.so.wasm")
    parser.add_argument("--namespace", default="libc")
    args = parser.parse_args()
    if args.configure_cache:
        names = inspect(args.provider)["exports"]
        args.configure_cache.parent.mkdir(parents=True, exist_ok=True)
        args.configure_cache.write_text("# Generated from the current shared libc function exports.\n" +
            "".join(f"ac_cv_func_{name}=yes\n" for name in sorted(names)
                    if name.isascii() and name.isidentifier()))
        print(f"Wrote provider function answers to {args.configure_cache}")
    elif args.rewrite:
        print(f"Assigned {rewrite(args.rewrite, args.provider, args.output)} shared libc imports")
    else:
        print(f"Verified {check_imports(args.check_imports, args.provider, args.namespace)} {args.namespace} imports")
