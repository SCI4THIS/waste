#!/usr/bin/env python3
"""Audit a Wasm module's imports against an explicit WASTE ABI allowlist."""

import argparse
import json
import re
import sys
from pathlib import Path


IMPORT_RE = re.compile(r'^\s*\(import "([^"]+)" "([^"]+)" ', re.MULTILINE)
ASYNCIFY_RE = re.compile(r'(?i)asyncify|unwind|rewind')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("--allow", action="append", default=[],
                        metavar="MODULE:NAME")
    parser.add_argument("--json", dest="json_path", type=Path)
    args = parser.parse_args()

    wat_path = args.module.with_suffix(".audit.wat")
    import subprocess
    subprocess.run(["wasm-dis", str(args.module), "-o", str(wat_path)],
                   check=True)
    source = wat_path.read_text(encoding="utf-8")
    imports = sorted({f"{module}:{name}"
                      for module, name in IMPORT_RE.findall(source)})
    asyncify = sorted(set(ASYNCIFY_RE.findall(source)))
    allowed = sorted(set(args.allow))
    unknown = sorted(set(imports) - set(allowed))
    report = {"module": str(args.module), "imports": imports,
              "allowed": allowed, "unknown": unknown,
              "asyncifySymbols": asyncify, "ok": not unknown and not asyncify}
    if args.json_path:
        args.json_path.write_text(json.dumps(report, indent=2) + "\n",
                                  encoding="utf-8")
    print(json.dumps(report, indent=2))
    wat_path.unlink()
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
