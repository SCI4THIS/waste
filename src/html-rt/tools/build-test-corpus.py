#!/usr/bin/env python3
"""Explicitly prepare/install the shared browser-test corpus under /root/waste/tests."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

import test_distribution
import vfs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", action="store_true")
    parser.add_argument("--audit", action="store_true", help="Also verify current source hashes/revision")
    parser.add_argument("--baseline", type=Path, help="Retained inventory: require identical identities, bytes and execution specs")
    parser.add_argument("--review-selection", action="store_true", help="Explicitly accept a changed existing corpus selection")
    args = parser.parse_args()
    if args.audit:
        inventory = vfs.load(vfs.ROOT)
        vfs.audit_tree(vfs.ROOT, inventory)
        manifest = json.loads(vfs.local(vfs.ROOT, test_distribution.MANIFEST).read_text())
        test_distribution.verify_sources(vfs.REPO, manifest)
        print(f"Test corpus source/provenance audit passed: {manifest['counts']}")
        return
    output = vfs.REPO / "build/engine/test-corpus"
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="prepare-", dir=output) as temporary:
        stage = Path(temporary)
        payload = stage / "payload"
        subprocess.run(["python3", str(Path(__file__).with_name("generate-c-engine-tests.py")),
                        "--mounted-corpus", "--wasm", str(vfs.REPO / "build/html-rt/waste-wast.wasm"),
                        "--output-dir", str(payload)], check=True)
        manifest = test_distribution.prepare(vfs.REPO, stage, payload / "payload.json", args.baseline)
        if args.install:
            vfs.install_tests(vfs.ROOT, stage, manifest, args.review_selection)
        else:
            print(f"Prepared {manifest['counts']['tests']} tests; use --install to publish")


if __name__ == "__main__":
    main()
