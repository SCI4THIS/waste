#!/usr/bin/env python3
"""Publish the library, matching public headers, configuration and license."""
import argparse
from pathlib import Path
import sys

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "src/html-rt/tools"))
from vfs import publish, wasm_contract


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    library = args.build.parent / "libcrypto.so.wasm"
    wasm_contract(library, library="libcrypto")
    changes = [(p, library.read_bytes(), 0o644) for p in
               ("/lib/libcrypto.so.wasm", "/usr/lib/libcrypto.so.wasm")]
    headers = {}
    for directory in (args.source / "include/openssl", args.build / "include/openssl"):
        for header in sorted(directory.glob("*.h")):
            headers[header.name] = header.read_bytes()
    changes.extend(("/usr/include/openssl/" + name, content, 0o644)
                   for name, content in sorted(headers.items()))
    changes.append(("/etc/ssl/openssl.cnf", Path(__file__).with_name("openssl.cnf").read_bytes(), 0o644))
    changes.append(("/usr/share/licenses/openssl/LICENSE.txt", (args.source / "LICENSE.txt").read_bytes(), 0o644))
    changes = [(path, data, mode) for path, data, mode in changes
               if not (args.root / path.lstrip("/")).is_file()
               or (args.root / path.lstrip("/")).read_bytes() != data
               or (args.root / path.lstrip("/")).stat().st_mode & 0o777 != mode]
    if changes:
        publish(args.root, changes)
    else:
        print("OpenSSL library/SDK installation is unchanged")


if __name__ == "__main__":
    main()
