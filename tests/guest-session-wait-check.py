#!/usr/bin/env python3
"""Native real wait events and frozen-clock capability/deadline boundaries."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", default="build/cli-rt/private/guest-session-sanitize")
    parser.add_argument("--vfs-root", default="src/vfs")
    parser.add_argument("--asan-options", default="detect_leaks=1:halt_on_error=1")
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("sessions", "tests/guest-session-check.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    scenario = json.loads(Path("tests/guest-session-waits.json").read_text())
    frozen = ["--clock-monotonic-ns", str(scenario["clockMonotonicNs"])]
    with tempfile.TemporaryDirectory(prefix="session-waits-", dir="build/engine") as temporary:
        code, output, report = helper.native_session(args.native, args.vfs_root,
            scenario["fixture"], scenario["events"], Path(temporary) / "waits.json",
            extra_args=frozen, asan_options=args.asan_options)
        assert code == scenario["exitStatus"] and report["exited"] and not report["error"], report
        assert report["passed"] == report["total"] == scenario["assertions"], report
        assert output.decode() == scenario["output"], output
        for key, value in scenario["nativeCounts"].items():
            assert report[key] == value, report
        # Clock records cannot create a frozen override, reset it to zero,
        # or move it backward. Failures leave the event counter unchanged.
        for index, (value, options) in enumerate(((4295467290, []),
                (4295467290, ["--clock-monotonic-ns", "0"]),
                (0, frozen), (scenario["clockMonotonicNs"] - 1, frozen))):
            record = struct.pack("<4sIII", b"WSC1", 4, value & 0xffffffff, value >> 32)
            code, output, report = helper.native_session(args.native, args.vfs_root,
                scenario["fixture"], [dict(after="INPUT\n", waitKind=2, pid=1,
                    controlBytes=list(record), controlEof=True)],
                Path(temporary) / f"invalid-{index}.json", extra_args=options,
                expected_error="invalid native session control record", asan_options=args.asan_options)
            assert code == 1 and not report["exited"], report
            assert report["passed"] == 4 and report["total"] == 5, report
            assert report["clockEvents"] == report["inputBytes"] == 0, report
            assert report["error"] == "invalid native session control record", report
            assert output == b"INPUT\n", output
        # Keep stdin open: a frozen guest clock cannot freeze host policy time.
        result = Path(temporary) / "deadline.json"
        env = dict(os.environ, ASAN_OPTIONS=args.asan_options,
                   UBSAN_OPTIONS="halt_on_error=1")
        process = subprocess.Popen([args.native, "--vfs-root", args.vfs_root,
            "--script", scenario["fixture"], "--result-file", str(result),
            "--timeout-ms", "5000", *frozen], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        try:
            assert process.wait(timeout=15) == 124
            output, errors = process.communicate()
            report = json.loads(result.read_text())
            assert report["timedOut"] and not report["cancelled"] and not report["exited"], report
            assert report["passed"] == 4 and report["total"] == 5, report
            assert report["selectWaits"] == 1 and report["clockEvents"] == 0, report
            assert output == b"INPUT\n" and b"Sanitizer" not in errors, (output, errors)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
    print("PASS native wait session: 24 assertions, 6 SELECT yields, 4 clock rejections, frozen guest/real host deadline")


if __name__ == "__main__":
    main()
