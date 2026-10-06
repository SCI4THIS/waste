#!/usr/bin/env python3
"""Native terminal descriptor aliases, close lifetimes and minimum-input guard."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", default="build/cli-rt/waste-session-sanitize")
    parser.add_argument("--vfs-root", default="src/vfs")
    parser.add_argument("--asan-options", default="detect_leaks=1:halt_on_error=1")
    parser.add_argument("--results", type=Path)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("sessions", "tests/guest-session-check.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    contract = json.loads(Path("tests/guest-session-terminal-descriptors.json").read_text())
    with tempfile.TemporaryDirectory(prefix="session-terminal-descriptors-", dir="build/engine") as temporary:
        code, output, report = helper.native_session(args.native, args.vfs_root,
            contract["fixture"], contract["events"], Path(temporary) / "descriptors.json",
            asan_options=args.asan_options)
        assert code == contract["exitStatus"] and report["exited"] and not report["error"], report
        assert report["passed"] == report["total"] == contract["assertions"], report
        assert output.decode() == contract["output"], output
        for key, value in contract["nativeCounts"].items():
            assert report[key] == value, report
        # Complete the VMIN=2 read at the first input boundary. Its guest value
        # checks remain valid, but the required lack of progress must fail.
        events = copy.deepcopy(contract["events"])
        events[0]["text"] = "hi"
        try:
            helper.native_session(args.native, args.vfs_root, contract["fixture"],
                events, Path(temporary) / "premature.json",
                asan_options=args.asan_options)
        except AssertionError as error:
            assert "guest advanced before its wait was satisfied" in str(error), str(error)
        else:
            raise AssertionError("minimum-input guard accepted completing input")
        if args.results:
            args.results.write_text(json.dumps(dict(native=report, output=output.decode(),
                prematureInputControls=1), indent=2) + "\n")
    print("PASS native terminal descriptors: 120 checks, 4 READ/3 SELECT yields, alias/close/stdin restoration, 1 premature-input control")


if __name__ == "__main__":
    main()
