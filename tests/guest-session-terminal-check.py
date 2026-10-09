#!/usr/bin/env python3
"""Native terminal control validation, framing, EOF and capability boundaries."""
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
    parser.add_argument("--wasm", default="build/html-rt/waste-wast.wasm")
    parser.add_argument("--page")
    args = parser.parse_args()
    command = ["python3", "tests/guest-session-check.py", "--native", args.native,
               "--vfs-root", args.vfs_root, "--wasm", args.wasm, "--scenario", "terminal-control"]
    if args.page:
        command += ["--page", args.page]
    subprocess.run(command, check=True)
    spec = importlib.util.spec_from_file_location("session_check", "tests/guest-session-check.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    valid = struct.pack("<4sIII", b"WSC1", 1, 120, 40)
    invalid = [struct.pack("<4sIII", magic, op, first, second) for magic, op, first, second in (
        (b"WSC0", 1, 120, 40), (b"WSC2", 1, 120, 40),
        (b"WSC1", 0, 120, 40), (b"WSC1", 3, 120, 40),
        (b"WSC1", 1, 0, 40), (b"WSC1", 1, 120, 0),
        (b"WSC1", 1, 65536, 40), (b"WSC1", 1, 120, 65536),
        (b"WSC1", 1, 0xffffffff, 40), (b"WSC1", 2, 0, 0),
        (b"WSC1", 2, 129, 0), (b"WSC1", 2, 0xffffffff, 0),
        (b"WSC1", 2, 12, 1))]
    invalid += [valid[:length] for length in range(1, 16)]
    with tempfile.TemporaryDirectory(prefix="session-terminal-", dir="build/engine") as temporary:
        for index, record in enumerate(invalid):
            code, output, report = helper.native_session(args.native, args.vfs_root,
                "tests/guest-session-terminal-control.wast",
                [dict(after="RESIZE\n", waitKind=2, pid=1,
                      controlBytes=list(record), controlEof=True)],
                Path(temporary) / f"invalid-{index}.json", timeout=15,
                expected_error="invalid native session control record")
            assert code == 1 and not report["exited"], report
            assert report["error"] == "invalid native session control record", report
            assert report["passed"] == 1 and report["total"] == 2, report
            assert report["inputBytes"] == report["resizeEvents"] == report["signalEvents"] == 0, report
            assert output == b"RESIZE\n", output
        # Clean control EOF disables only that channel, not terminal input.
        scenario = json.loads(Path("tests/guest-session-io.json").read_text())
        scenario["events"][0]["controlEof"] = True
        code, output, report = helper.native_session(args.native, args.vfs_root,
            scenario["fixture"], scenario["events"], Path(temporary) / "clean-eof.json", timeout=15)
        assert code == 7 and report["passed"] == report["total"] == 7, report
        assert report["inputBytes"] == 2 and not report["error"], report
        assert output == scenario["output"].encode(), output
        scenario = json.loads(Path("tests/guest-session-terminal-control.json").read_text())
        batch = valid + valid + struct.pack("<4sIII", b"WSC1", 2, 28, 0)
        events = [dict(after="RESIZE\n", controlBytes=list(batch)), *scenario["events"][3:]]
        code, output, report = helper.native_session(args.native, args.vfs_root,
            scenario["fixture"], events, Path(temporary) / "coalesced.json", timeout=15)
        assert code == 0 and report["passed"] == report["total"] == 9, report
        assert report["resizeEvents"] == 4 and report["signalEvents"] == 3, report
        assert output == scenario["output"].encode(), output
        # Malformed control data while a child is selected is a host failure,
        # not a fabricated child exit or successful root assertion.
        events = [*scenario["events"][:-1], dict(after="CHILD\n", pid=2,
            controlBytes=list(invalid[0]), controlEof=True)]
        code, output, report = helper.native_session(args.native, args.vfs_root,
            scenario["fixture"], events, Path(temporary) / "child-invalid.json", timeout=15,
            expected_error="invalid native session control record")
        assert code == 1 and report["passed"] == 6 and report["total"] == 7, report
        assert report["forks"] == 1 and report["childExits"] == 0 and not report["exited"], report
        assert report["error"] == "invalid native session control record", report
    for value in ("0", "1", "2", "2147483648", "-1", "not-a-fd"):
        result = subprocess.run([args.native, "--vfs-root", args.vfs_root,
                                 "--control-fd", value], capture_output=True, timeout=15)
        assert result.returncode == 2, result.stderr
    reader, writer = os.pipe()
    try:
        result = subprocess.run([args.native, "--vfs-root", args.vfs_root,
                                 "--control-fd", str(writer)], pass_fds=(writer,),
                                capture_output=True, timeout=15)
        assert result.returncode == 2, result.stderr
    finally:
        os.close(reader)
        os.close(writer)
    if args.page:
        subprocess.run(["node", "tests/guest-session-terminal-browser.cjs", args.page], check=True)
    print(f"PASS native terminal controls: {len(invalid)} malformed/truncated records, clean EOF, coalesced records, child failure, 7 descriptor rejections")


if __name__ == "__main__":
    main()
