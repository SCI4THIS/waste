#!/usr/bin/env python3
"""Prepare a Coreutils executable that imports the installed shared guest libc."""
from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path

from shared_libc import rewrite


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--utility", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--libc", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    provider = args.libc or args.repo_root / "src/vfs/lib/libc.so.wasm"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Configure builds retain DWARF. Publish executable bytes without those
    # build-only sections; do not merge the provider into this image.
    with tempfile.TemporaryDirectory(dir=args.output.parent) as temporary:
        stripped = Path(temporary) / "utility.wasm"
        subprocess.run(["wasm-opt", str(args.utility), "--strip-debug",
                        "-o", str(stripped)], check=True)
        count = rewrite(stripped, provider, args.output)
    print(f"Built {args.output}: {count} shared libc imports")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
