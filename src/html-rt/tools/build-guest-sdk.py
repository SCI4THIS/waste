#!/usr/bin/env python3
"""Prepare/audit the selected guest SDK; --install explicitly publishes it."""
import argparse
from pathlib import Path
import tempfile

import guest_sdk
import vfs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", action="store_true")
    parser.add_argument("--audit", action="store_true")
    parser.add_argument("--library", type=Path, help="Publish matching ncurses DSO and headers atomically")
    args = parser.parse_args()
    if args.audit:
        guest_sdk.audit(vfs.ROOT)
        print("Current guest SDK headers discovered")
        return
    output = vfs.REPO / "build/engine/guest-sdk"
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="prepare-", dir=output) as temporary:
        stage = Path(temporary)
        manifest = guest_sdk.prepare(vfs.REPO, stage, args.library)
        guest_sdk.audit(stage)
        if args.install:
            vfs.install_sdk(vfs.ROOT, stage, manifest, args.library)
        else:
            print(f"Prepared and audited {len(manifest['headers'])} SDK files; use --install to publish")


if __name__ == "__main__":
    main()
