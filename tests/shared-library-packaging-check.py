#!/usr/bin/env python3
"""Check installed production consumers against the shared libc ABI."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src/html-rt/tools"))
from wasm_signatures import inspect


def check(path, provider):
    info = inspect(path)
    imports = {name.split(":", 1)[1]: signature
               for name, signature in info["imports"].items()
               if name.startswith("libc:")}
    assert imports, f"missing shared libc dependency: {path}"
    for name, signature in imports.items():
        assert provider.get(name) == signature, (path, name, signature)
    # These exported implementations identified the former full-libc merge.
    assert not {"malloc", "free", "memcpy", "strcpy", "printf"} & info["exports"].keys(), path
    return len(imports)


def main():
    provider = inspect(ROOT / "src/vfs/lib/libc.so.wasm")["exports"]
    names = ("true", "false", "pwd", "echo", "printf", "basename", "dirname",
             "cat", "wc", "ls", "date", "rogue", "ldd", "upload", "download")
    for name in names:
        count = check(ROOT / "src/vfs/usr/bin" / name, provider)
        print(f"{name}: {count} shared libc function imports")
    check(ROOT / "src/vfs/lib/libncurses.so.wasm", provider)
    # Bash is an embedded executable module in the installed launch script.
    launch = (ROOT / "src/vfs/usr/share/waste/launch.wast").read_text()
    binaries = re.findall(r'\(module \$bash binary "([^"]+)"\)', launch)
    assert len(binaries) == 1
    import tempfile
    with tempfile.TemporaryDirectory(dir=ROOT / "build/engine") as temporary:
        bash = Path(temporary) / "bash.wasm"
        bash.write_bytes(bytes.fromhex(binaries[0].replace("\\", "")))
        check(bash, provider)
    freestanding = inspect(ROOT / "src/vfs/bin/waste-test")
    assert not any(name.startswith("libc:") for name in freestanding["imports"])
    print("Production shared-library packaging passed; freestanding runner has no libc dependency")


if __name__ == "__main__":
    main()
