#!/usr/bin/env python3
"""Open every mounted test WAST path through the Bash worker probe."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("page", nargs="?", default="build/html-rt/bash.html")
    parser.add_argument("--limit", type=int,
                        help="check only the first N manifest paths (diagnosis)")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    manifest = json.loads((repo / "src/vfs/root/waste/tests/manifest.json").read_text())
    # Restrict expansion to precisely the manifest's group directories. Hidden
    # support files and descendants must not be counted as additional tests.
    patterns = " ".join(shlex.quote("/root/waste/tests/" + group) + "/*.wast"
                        for group in manifest["counts"]["groups"])
    count = manifest["counts"]["tests"]
    if args.limit is not None:
        if args.limit < 1:
            parser.error("--limit must be positive")
        count = min(count, args.limit)
    marker = "__CORPUS_OPEN_OK__:"
    command = (f"n=0; visited=0; for f in {patterns}; do "
               f'if ((visited >= {count})); then break; fi; '
               'visited=$((visited+1)); '
               'if : < "$f"; then n=$((n+1)); '
               'else echo "__CORPUS_OPEN_FAIL__:$f"; fi; done; '
               f'echo "{marker}$n"')
    env = dict(os.environ, WASTE_COREUTILS_LS_COMMAND=command,
               WASTE_COREUTILS_LS_EXPECT=marker + str(count), WASTE_COREUTILS_LS_EXPECT_COUNT="1")
    result = subprocess.run(["node", "tests/c-engine-bash-browser-runtime.cjs", args.page,
                             "--coreutils-ls", "--full-package"], cwd=repo, env=env,
                            capture_output=True, text=True, timeout=90)
    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="")
    if result.returncode or "\n__CORPUS_OPEN_FAIL__" in result.stdout:
        raise SystemExit(result.returncode or 1)
    if "\n" + marker + str(count) + "\n" not in result.stdout:
        raise SystemExit("Bash corpus count missing from command output")
    print(f"PASS mounted Bash corpus: all {count} WAST files readable through guest redirection")


if __name__ == "__main__":
    main()
