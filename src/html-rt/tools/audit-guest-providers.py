#!/usr/bin/env python3
"""Verify default SDK declarations, unavailable APIs and compiled Wasm signatures."""
import argparse
import json
from pathlib import Path

import guest_sdk
from guest_providers import inspect_sdk
import vfs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=vfs.REPO / "build/engine/guest-sdk/providers.json")
    args = parser.parse_args()
    sdk = guest_sdk.audit(vfs.ROOT)
    report = inspect_sdk(vfs.REPO, vfs.ROOT, sdk)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    available = sum(not e["unavailable"] for e in report["functions"].values())
    unavailable = len(report["functions"]) - available
    print(f"Guest provider gate: {available} available functions; {unavailable} unavailable; "
          f"{len(report['variables'])} globals; Wasm signatures verified; {args.output}")


if __name__ == "__main__":
    main()
