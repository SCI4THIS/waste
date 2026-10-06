#!/usr/bin/env python3
"""Execution-policy limits are not guest traps, invalidity, or child exits."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", default="build/cli-rt/waste-session-sanitize")
    parser.add_argument("--vfs-root", default="src/vfs")
    parser.add_argument("--wasm", default="build/html-rt/waste-wast.wasm")
    parser.add_argument("--page")
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("session_check", "tests/guest-session-check.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
               UBSAN_OPTIONS="halt_on_error=1")
    with tempfile.TemporaryDirectory(prefix="session-control-", dir="build/engine") as temporary:
        for fixture in ("loop", "linked", "fork", "tail", "start"):
            for cancel in (False, True):
                result = Path(temporary) / f"{fixture}-{cancel}.json"
                limits = ["--timeout-ms", "2000"] if not cancel else [
                    "--timeout-ms", "10000", "--cancel-after-ms", "2000"]
                process = subprocess.run([args.native, "--vfs-root", args.vfs_root,
                    "--script", f"tests/guest-session-control-{fixture}.wast",
                    "--result-file", str(result), *limits], capture_output=True,
                    timeout=15, env=env)
                report = json.loads(result.read_text())
                assert process.returncode == (125 if cancel else 124), report
                assert report["cancelled"] == cancel and report["timedOut"] != cancel, report
                assert not report["exited"] and report["passed"] == 0, report
                assert report["error"].startswith("execution cancelled" if cancel else "execution deadline exceeded"), report
                assert b"Sanitizer" not in process.stderr, process.stderr
                if fixture == "fork":
                    assert report["forks"] == 1 and report["childExits"] == 0, report
        for kind, source in (("exec", "tests/guest-session-control-exec.wat"),
                             ("handler", "tests/guest-session-control-loop.wast"),
                             ("handler-start", "tests/guest-session-control-start-negative.wast")):
            for cancel in (False, True):
                message = "execution cancelled" if cancel else "execution deadline exceeded"
                limits = ["--timeout-ms", "2000"] if not cancel else [
                    "--timeout-ms", "10000", "--cancel-after-ms", "2000"]
                suffix = "wat" if kind == "exec" else "wast"
                code, output, report = helper.native_session(args.native, args.vfs_root,
                    f"tests/guest-session-control-spawn-{suffix}.wast", [],
                    Path(temporary) / f"child-{kind}-{cancel}.json", timeout=15,
                    expected_error=message, files=[dict(path=f"/bin/control.{suffix}", mode=0o755,
                                                        source=source)], extra_args=limits)
                assert code == (125 if cancel else 124), report
                assert not output, output
                assert report["forks"] == report["execs"] == 1, report
                assert report["childExits"] == 0 and not report["exited"], report
                assert report["error"] == message, report
        for cancel in (False, True):
            result = Path(temporary) / f"blocked-{cancel}.json"
            limits = ["--timeout-ms", "500"] if not cancel else [
                "--timeout-ms", "10000", "--cancel-after-ms", "500"]
            process = subprocess.Popen([args.native, "--vfs-root", args.vfs_root,
                "--script", "tests/guest-session-io.wast", "--result-file", str(result), *limits],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
            try:
                assert process.wait(timeout=15) == (125 if cancel else 124)
                report = json.loads(result.read_text())
                assert report["readWaits"] == 1 and report["inputBytes"] == 0, report
                assert report["cancelled"] == cancel and report["timedOut"] != cancel, report
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate()
    command = ["node", "tests/guest-session-control-browser.cjs", args.wasm, args.vfs_root]
    if args.page:
        command.append(args.page)
    subprocess.run(command, check=True, timeout=90)
    print("PASS native execution controls: 18 runnable/blocked interrupts, fork/exec/handler teardown, sanitizer cleanup")


if __name__ == "__main__":
    main()
