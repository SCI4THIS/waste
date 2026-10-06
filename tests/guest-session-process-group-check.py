#!/usr/bin/env python3
"""Native process/foreground groups, queued terminal signals and wait guards."""
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
    contract = json.loads(Path("tests/guest-session-process-groups.json").read_text())
    with tempfile.TemporaryDirectory(prefix="session-process-groups-", dir="build/engine") as temporary:
        code, output, report = helper.native_session(args.native, args.vfs_root,
            contract["fixture"], contract["events"], Path(temporary) / "groups.json",
            asan_options=args.asan_options)
        assert code == contract["exitStatus"] and report["exited"] and not report["error"], report
        assert report["passed"] == report["total"] == contract["assertions"], report
        assert output.decode() == contract["output"], output
        for key, value in contract["nativeCounts"].items():
            assert report[key] == value, report
        # VINTR followed by a normal byte satisfies each background wait
        # immediately, preserving the queued signal for later guest assertions.
        # The host guard must reject this input before sending another event.
        for index, (event_index, byte) in enumerate(((0, 120), (2, 89))):
            events = copy.deepcopy(contract["events"])
            events[event_index]["bytes"] = [3, byte]
            try:
                helper.native_session(args.native, args.vfs_root, contract["fixture"],
                    events, Path(temporary) / f"premature-{index}.json",
                    asan_options=args.asan_options)
            except AssertionError as error:
                assert "guest advanced before its wait was satisfied" in str(error), str(error)
            else:
                raise AssertionError("background-interrupt guard accepted ordinary data")
        if args.results:
            args.results.write_text(json.dumps(dict(native=report, output=output.decode(),
                prematureInputControls=2), indent=2) + "\n")
    print("PASS native process groups: 71 checks, 6 SELECT yields, queued/foreground VINTR, ISIG data, 2 premature-input controls")


if __name__ == "__main__":
    main()
