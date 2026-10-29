#!/usr/bin/env python3
"""Install command notices; extract credits deterministically from staged inputs.

Run under the aux publication lock. Never configure or modify upstream sources.
"""
import argparse
from pathlib import Path
import re


def publish(root, package, name, data):
    path = root / "usr/share/licenses" / package / name
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    path.chmod(0o644)


def notices(package, source, commands):
    if package == "coreutils":
        text = (source / "AUTHORS").read_text()
        extracts = []
        for command in commands:
            matches = re.findall(rf"^{re.escape(command)}: .+$", text, re.M)
            if len(matches) != 1:
                raise ValueError(f"expected one Coreutils AUTHORS entry for {command}")
            entry = f"Extracted from GNU Coreutils AUTHORS.\n\n{matches[0]}\n".encode()
            extracts.append(matches[0])
            yield f"coreutils/{command}", "AUTHORS", entry
        yield package, "AUTHORS", ("GNU Coreutils commands shipped with WASTE.\n"
                                    "Extracted from upstream AUTHORS.\n\n" +
                                    "\n".join(extracts) + "\n").encode()
        yield package, "COPYING", (source / "COPYING").read_bytes()
        return
    if package == "vim":
        text = (source / "runtime/doc/intro.txt").read_text()
        match = re.search(r"^3\. Credits[^\n]*\*credits\*[^\n]*\n(.*?)^={10,}$",
                          text, re.M | re.S)
        if not match:
            raise ValueError("Vim intro.txt credits section not found")
        authors = ("Vim credits, extracted from runtime/doc/intro.txt, section 3.\n" +
                   match[1]).encode()
        license_name = "LICENSE"
    elif package == "rogue":
        text = (source / "LICENSE.TXT").read_text()
        parts = re.split(r"\n={10,}\n", text)
        if len(parts) != 3 or any("Copyright" not in part for part in parts):
            raise ValueError("Rogue license attribution sections changed")
        authors = ("Rogue credits, extracted from LICENSE.TXT.\n\n" +
                   "\n\n".join(part.split("\nRedistribution and use", 1)[0].strip()
                                 for part in parts) + "\n").encode()
        license_name = "LICENSE.TXT"
    elif package == "llvm":
        # Upstream explicitly calls CREDITS a partial list. Keep that qualifier
        # and distinguish current maintainers from historical contributors.
        authors = b"LLVM tools (clang, wasm-ld, lld-link): upstream credits and maintainers.\n\n"
        for name in ("llvm/CREDITS.TXT", "clang/Maintainers.md", "lld/Maintainers.md"):
            authors += f"--- Extracted from {name} ---\n".encode()
            authors += (source / name).read_bytes().rstrip() + b"\n\n"
        license_name = "LICENSE.TXT"
    elif package in ("clipboard", "upload", "download", "ldd"):
        repo = Path(__file__).resolve().parents[3]
        authors = (repo / "src/aux" / package / "AUTHORS").read_bytes()
        yield package, "AUTHORS", authors
        yield package, "LICENSE", (repo / "LICENSE").read_bytes()
        return
    else:
        author_name = "AUTHORS.md" if package == "openssl" else "AUTHORS"
        authors = (source / author_name).read_bytes()
        license_name = "LICENSE.txt" if package == "openssl" else "COPYING"
    yield package, "AUTHORS", authors
    yield package, license_name, (source / license_name).read_bytes()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--package", required=True, choices=(
        "awk", "bash", "m4", "dot", "openssl", "vim", "rogue", "llvm", "coreutils",
        "clipboard", "upload", "download", "ldd"))
    parser.add_argument("--source", type=Path)
    parser.add_argument("--commands", nargs="+", default=[])
    args = parser.parse_args()
    # Validate/extract every input before publishing any of this package.
    outputs = list(notices(args.package, args.source, args.commands))
    for package, name, data in outputs:
        publish(args.root, package, name, data)


if __name__ == "__main__":
    main()
