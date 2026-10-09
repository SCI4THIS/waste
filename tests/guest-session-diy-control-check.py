#!/usr/bin/env python3
"""Native module-start handler, external SIGINT and completion boundaries."""
import argparse
import importlib.util
import json
from pathlib import Path
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", default="build/cli-rt/private/guest-session-sanitize")
    parser.add_argument("--vfs-root", default="src/vfs")
    parser.add_argument("--results", type=Path)
    parser.add_argument("--asan-options", default="detect_leaks=1:halt_on_error=1")
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("sessions", "tests/guest-session-check.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    contract = json.loads(Path("tests/guest-session-diy-control.json").read_text())
    with tempfile.TemporaryDirectory(prefix="diy-control-", dir="build/engine") as temporary:
        directory = Path(temporary)
        code, output, report = helper.native_session(args.native, args.vfs_root,
            contract["fixture"], contract["events"], directory / "positive.json",
            asan_options=args.asan_options)
        assert code == contract["exitStatus"] and report["exited"] and not report["error"], report
        assert report["passed"] == report["total"] == contract["assertions"], report
        assert output.decode() == contract["output"], output
        for key, value in contract["nativeCounts"].items():
            assert report[key] == value, report
        assert report["forks"] == report["childExits"] == 0, report
        positive_output = output.decode()

        # No signal must leave the wait pending until the host deadline, with
        # no fabricated successful return or guest exit.
        missing_event = dict(after=contract["output"], waitKind=2, pid=1, controlBytes=[])
        code, output, missing = helper.native_session(args.native, args.vfs_root,
            contract["fixture"], [missing_event], directory / "missing.json", timeout=15,
            expected_error="execution deadline exceeded", extra_args=["--timeout-ms", "10000"],
            asan_options=args.asan_options)
        assert code == 124 and missing["timedOut"] and not missing["cancelled"], missing
        assert not missing["exited"] and missing["passed"] == 3 and missing["total"] == 4, missing
        # Closing host stdin can wake and rearm a zero-fd SELECT. It must
        # still remain pending; this is not a second completed guest call.
        assert missing["selectWaits"] >= 1 and missing["signalEvents"] == 0, missing
        assert output.decode() == contract["output"], output

        # Keep the actual host event unchanged, but demand the wrong guest
        # result: completion must propagate a mismatch instead of an exit 0.
        original = Path(contract["fixture"]).read_text()
        assertion = '(assert_return (invoke "run") (i32.const 2))'
        assert original.count(assertion) == 1
        mismatch = (directory / "mismatch.wast").resolve()
        mismatch.write_text(original.replace(assertion, '(assert_return (invoke "run") (i32.const 12))'))
        error = "result mismatch for run (actual 2, expected 12)"
        code, output, wrong = helper.native_session(args.native, args.vfs_root,
            str(mismatch), contract["events"], directory / "wrong.json",
            expected_error=error, asan_options=args.asan_options)
        assert code == 1 and wrong["error"] == error and not wrong["exited"], wrong
        assert wrong["passed"] == 3 and wrong["total"] == 4 and wrong["signalEvents"] == 1, wrong
        assert not wrong["timedOut"] and not wrong["cancelled"], wrong
        assert output.decode() == contract["output"], output
        if args.results:
            args.results.write_text(json.dumps(dict(report=report, output=positive_output,
                missingEvent=missing, mismatch=wrong), indent=2) + "\n")
    print("PASS native DIY control: 7 checks, start-installed handler, 1 SIGINT/SELECT yield; missing-event deadline and mismatch rejection")


if __name__ == "__main__":
    main()
