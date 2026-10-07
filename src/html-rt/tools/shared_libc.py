"""Assign guest function imports to the installed, signature-checked libc.

Memory, table, relocation globals and kernel imports retain their namespaces.
The binary rewrite preserves function indices and every non-import section.
"""
from pathlib import Path

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


def review_flags(paths, provider):
    """Explicit install review for this migration's checked libc imports."""
    exports = inspect(provider)["exports"]
    names = set()
    for path in paths:
        for identity, signature in inspect(path)["imports"].items():
            module, name = identity.split(":", 1)
            if module == "libc":
                if exports.get(name) != signature:
                    raise ValueError(f"{path}: incompatible libc import {name}")
                names.add(identity + ":function")
    names.update(("env:memory:memory", "env:__indirect_function_table:table"))
    return [argument for name in sorted(names) for argument in ("--review-import", name)]


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--review-imports", nargs="+", type=Path, required=True)
    parser.add_argument("--provider", type=Path,
                        default=Path(__file__).resolve().parents[3] / "src/vfs/lib/libc.so.wasm")
    args = parser.parse_args()
    print(" ".join(review_flags(args.review_imports, args.provider)))
