#!/usr/bin/env python3
"""Native terminal output/EOF and exact frozen-clock VTIME session boundaries."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", default="build/cli-rt/private/guest-session-sanitize")
    parser.add_argument("--vfs-root", default="src/vfs")
    parser.add_argument("--asan-options", default="detect_leaks=1:halt_on_error=1")
    parser.add_argument("--results", type=Path)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("sessions", "tests/guest-session-check.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    contract = json.loads(Path("tests/guest-session-terminal-timing.json").read_text())
    frozen = ["--clock-monotonic-ns", str(contract["clockMonotonicNs"])]
    with tempfile.TemporaryDirectory(prefix="session-timing-", dir="build/engine") as temporary:
        code, output, report = helper.native_session(args.native, args.vfs_root,
            contract["fixture"], contract["events"], Path(temporary) / "timing.json",
            extra_args=frozen, asan_options=args.asan_options)
        assert code == contract["exitStatus"] and report["exited"] and not report["error"], report
        assert report["passed"] == report["total"] == contract["assertions"], report
        assert output.decode() == contract["output"], output
        assert output.startswith("".join(s["text"] for s in contract["outputSegments"]).encode()), output
        for key, value in contract["nativeCounts"].items():
            assert report[key] == value, report
        # Supply the exact deadline at each one-nanosecond-early boundary.
        # All guest assertions still succeed, but the required lack of progress
        # must reject these premature completions in the host contract.
        for index, event_index in enumerate((0, 3, 6)):
            events = copy.deepcopy(contract["events"])
            events[event_index]["monotonicNs"] += 1
            try:
                helper.native_session(args.native, args.vfs_root, contract["fixture"],
                    events, Path(temporary) / f"premature-{index}.json", extra_args=frozen,
                    asan_options=args.asan_options)
            except AssertionError as error:
                assert "guest advanced before its wait was satisfied" in str(error), str(error)
            else:
                raise AssertionError("subdeadline guard accepted the exact deadline")
        if args.results:
            args.results.write_text(json.dumps(dict(native=report, output=output.decode(),
                prematureDeadlineControls=3), indent=2) + "\n")
    print("PASS native terminal timing: 46 checks, 8 READ/1 SELECT yields, 6 clock events, exact output/EOF, 3 premature-deadline controls")


if __name__ == "__main__":
    main()
