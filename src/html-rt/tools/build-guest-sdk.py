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
    parser.add_argument("--root", type=Path, default=vfs.ROOT,
                        help="VFS tree to audit or publish into")
    parser.add_argument("--library", type=Path, help="Publish matching ncurses DSO and headers atomically")
    parser.add_argument("--ncurses-build", type=Path,
                        help="Configured ncurses build tree supplying matching public headers")
    args = parser.parse_args()
    if args.audit:
        guest_sdk.audit(args.root)
        print("Current guest SDK headers discovered")
        return
    output = vfs.REPO / "build/engine/guest-sdk"
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="prepare-", dir=output) as temporary:
        stage = Path(temporary)
        manifest = guest_sdk.prepare(vfs.REPO, stage, args.library, args.ncurses_build)
        guest_sdk.audit(stage)
        if args.install:
            vfs.install_sdk(args.root, stage, manifest, args.library)
        else:
            print(f"Prepared and audited {len(manifest['headers'])} SDK files; use --install to publish")


if __name__ == "__main__":
    main()
