#!/usr/bin/env python3
"""Reject incomplete completion and regressions hidden inside libc XFAILs."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location("libc_corpus", Path(__file__).with_name("libc-corpus-check.py"))
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


def main():
    report = json.loads(Path(sys.argv[1]).read_text())
    parser = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else gate.ROOT / "build/cli-rt/waste-cli"
    expectations = gate.contract(parser)
    with contextlib.redirect_stdout(io.StringIO()):
        gate.verify(report, expectations)
    identity = "libc-test/environment-boundaries.wast"
    position = next(i for i, t in enumerate(report["tests"]) if t["identity"] == identity)
    variants = []

    def variant(name):
        changed = copy.deepcopy(report)
        variants.append((name, changed))
        return changed

    variant("empty selection")["tests"] = []
    variant("missing fixture")["tests"].pop()
    variant("duplicate fixture")["tests"].append(copy.deepcopy(report["tests"][0]))
    variant("reordered fixtures")["tests"].reverse()
    variant("unsuccessful completion")["exitCode"] = 1
    variant("skipped XFAIL")["tests"][position]["status"] = "SKIP"
    variant("setup load diagnostic")["tests"][position]["stderrPreview"] = "load error: unknown import"
    variant("parse error")["tests"][position]["nativeReport"]["error"] = "unterminated module"
    variant("omitted assertion")["tests"][position]["nativeReport"]["assertions"].pop()
    variant("reordered assertions")["tests"][position]["nativeReport"]["assertions"].reverse()
    variant("unknown assertion")["tests"][position]["nativeReport"]["assertions"][0]["func"] = "unknown"
    variant("wrong totals")["tests"][position]["total"] += 1
    # Keep the file XFAIL while changing a previously passing expectation.
    variant("additional failure in XFAIL")["tests"][position]["nativeReport"]["assertions"][1]["pass"] = False
    variant("unexpected assertion pass")["tests"][position]["nativeReport"]["assertions"][0]["pass"] = True
    variant("different failure phase")["tests"][position]["nativeReport"]["assertions"][0]["error"] = "unknown module id"
    variant("changed mismatched value")["tests"][position]["nativeReport"]["assertions"][0]["error"] = (
        "result mismatch for boundary-execve (actual 2, expected 1)")
    for name, changed in variants:
        with contextlib.redirect_stdout(io.StringIO()):
            try:
                gate.verify(changed, expectations)
            except AssertionError:
                continue
        raise AssertionError(f"accepted {name}")
    print(f"PASS libc completion contract: {len(variants)} rejection controls, including extra XFAIL failure")


if __name__ == "__main__":
    main()
