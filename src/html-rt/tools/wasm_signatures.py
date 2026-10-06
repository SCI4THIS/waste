"""Bounded core-Wasm function import/export signature inspection (no execution)."""
from pathlib import Path


class Reader:
    def __init__(self, data):
        self.data = data
        self.at = 0

    def take(self, size):
        if size < 0 or size > len(self.data) - self.at:
            raise ValueError("truncated Wasm signature input")
        result = self.data[self.at:self.at + size]
        self.at += size
        return result

    def byte(self):
        return self.take(1)[0]

    def integer(self):
        result = 0
        for shift in range(0, 70, 7):
            byte = self.byte()
            result |= (byte & 127) << shift
            if byte < 128:
                return result
        raise ValueError("oversized Wasm LEB integer")

    def name(self):
        return self.take(self.integer()).decode("utf-8")

    def limits(self):
        flags = self.integer()
        self.integer()
        if flags & 1:
            self.integer()


def inspect(path):
    data = Path(path).read_bytes()
    if len(data) > 64 * 1024 * 1024 or data[:8] != b"\0asm\x01\0\0\0":
        raise ValueError("unsupported Wasm signature input")
    reader = Reader(data[8:])
    types, indices, imports, exports, globals_ = [], [], [], [], []
    while reader.at < len(reader.data):
        kind = reader.byte()
        section = Reader(reader.take(reader.integer()))
        if kind not in (1, 2, 3, 7):
            continue
        count = section.integer()
        if count > 1000000:
            raise ValueError("oversized signature table")
        for _ in range(count):
            if kind == 1:
                if section.byte() != 0x60:
                    raise ValueError("SDK signature inspector supports core function types only")
                params = list(section.take(section.integer()))
                results = list(section.take(section.integer()))
                types.append(dict(params=params, results=results))
            elif kind == 2:
                module, name, import_kind = section.name(), section.name(), section.byte()
                if import_kind == 0:
                    indices.append(section.integer())
                    imports.append((module, name, len(indices) - 1))
                elif import_kind == 1:
                    section.byte()
                    section.limits()
                elif import_kind == 2:
                    section.limits()
                elif import_kind == 3:
                    section.take(2)
                elif import_kind == 4:
                    section.byte()
                    section.integer()
                else:
                    raise ValueError("invalid Wasm import kind")
            elif kind == 3:
                indices.append(section.integer())
            else:
                name, export_kind, index = section.name(), section.byte(), section.integer()
                if export_kind == 0:
                    exports.append((name, index))
                elif export_kind == 3:
                    globals_.append(name)
        if section.at != len(section.data):
            raise ValueError("unexpected trailing signature-section data")
    try:
        return dict(imports={module + ":" + name: types[indices[index]] for module, name, index in imports},
                    exports={name: types[indices[index]] for name, index in exports},
                    globals=globals_)
    except IndexError as error:
        raise ValueError("invalid Wasm function/type index") from error
