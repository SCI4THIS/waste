#!/usr/bin/env python3
"""Native terminal session assertions and post-yield partial-input boundaries."""
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
    contract = json.loads(Path("tests/guest-session-terminal-readiness.json").read_text())
    with tempfile.TemporaryDirectory(prefix="session-readiness-", dir="build/engine") as temporary:
        code, output, report = helper.native_session(args.native, args.vfs_root,
            contract["fixture"], contract["events"], Path(temporary) / "readiness.json",
            asan_options=args.asan_options)
        assert code == contract["exitStatus"] and report["exited"] and not report["error"], report
        assert report["passed"] == report["total"] == contract["assertions"], report
        assert output.decode() == contract["output"], output
        for key, value in contract["nativeCounts"].items():
            assert report[key] == value, report
        # Negative controls prove the driver notices a premature continuation,
        # rather than merely checking the final assertion count/transcript.
        for index, (event_index, completing_input) in enumerate(((1, "abc\n"), (5, "xyz"))):
            events = copy.deepcopy(contract["events"])
            events[event_index]["text"] = completing_input
            try:
                helper.native_session(args.native, args.vfs_root, contract["fixture"],
                    events, Path(temporary) / f"premature-{index}.json",
                    asan_options=args.asan_options)
            except AssertionError as error:
                assert "guest advanced before its wait was satisfied" in str(error), str(error)
            else:
                raise AssertionError("partial-input guard accepted completing input")
        if args.results:
            args.results.write_text(json.dumps(dict(native=report, output=output.decode(),
                prematureInputControls=2), indent=2) + "\n")
    print(f"PASS native terminal readiness: {contract['assertions']} checks, 6 SELECT/2 READ yields, 32 input bytes, 2 premature-input controls")


if __name__ == "__main__":
    main()
