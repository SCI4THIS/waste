#!/usr/bin/env python3
"""Shared-adapter parity: real post-yield input, binary bytes, files and EOF."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import selectors
import struct
import subprocess
import tempfile
import time


def native_session(executable, vfs_root, script, events, result, timeout=10, expected_error=None, files=(), extra_args=(), before_event=None, asan_options=None):
    env = dict(os.environ, ASAN_OPTIONS=asan_options or os.environ.get("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1"),
               UBSAN_OPTIONS="halt_on_error=1")
    command = [executable, "--vfs-root", vfs_root, "--result-file", str(result), "--trace-waits"]
    if script is not None:
        command += ["--script", script]
    command += list(extra_args)
    controls = any("resize" in event or "signal" in event or "monotonicNs" in event or "controlBytes" in event or
                   event.get("controlEof") for event in events)
    control_read, control_write = os.pipe() if controls else (-1, -1)
    if controls:
        command += ["--control-fd", str(control_read)]
    for file in files:
        command += ["--stage-file", file["path"], format(file["mode"], "o"), file["source"]]
    try:
        process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
                                   pass_fds=(control_read,) if controls else ())
    except BaseException:
        if controls:
            os.close(control_read)
            os.close(control_write)
        raise
    if controls:
        os.close(control_read)
    output = bytearray()
    consumed = 0
    last_wait = 0
    waits = []
    diagnostics = bytearray()
    deadline = time.monotonic() + timeout
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        selector.register(process.stderr, selectors.EVENT_READ)
        try:
            for event_index, event in enumerate(events):
                marker = event.get("after", "").encode()
                while not ((event.get("duringBatch") or len(waits) > last_wait) and output.find(marker, consumed) >= 0):
                    if time.monotonic() >= deadline:
                        raise AssertionError(f"native output timeout: {output!r}")
                    for key, _ in selector.select(0.1):
                        chunk = os.read(key.fileobj.fileno(), 4096)
                        if not chunk:
                            if key.fileobj is process.stdout:
                                raise AssertionError(f"native exited before marker: {output!r}; "
                                                     f"results: {result.read_text() if result.exists() else diagnostics}")
                            selector.unregister(key.fileobj)
                            continue
                        if key.fileobj is process.stdout:
                            output.extend(chunk)
                        else:
                            diagnostics.extend(chunk)
                            while b"\n" in diagnostics:
                                record, _, remainder = diagnostics.partition(b"\n")
                                diagnostics = bytearray(remainder)
                                try:
                                    wait = json.loads(record)
                                except json.JSONDecodeError as error:
                                    raise AssertionError(
                                        f"unexpected native diagnostic: {record!r}; "
                                        f"remaining stderr: {diagnostics!r}; output: {output!r}") from error
                                assert wait["kind"] in (1, 2), wait
                                waits.append(wait)
                if event_index and events[event_index - 1].get("stillWaiting"):
                    assert len(output) == consumed, "guest advanced before its wait was satisfied"
                if "pid" in event:
                    assert waits[-1]["pid"] == event["pid"], waits[-1]
                if "waitKind" in event:
                    assert waits[-1]["kind"] == event["waitKind"], waits[-1]
                consumed = len(output)
                last_wait = len(waits)
                if before_event:
                    before_event(event_index, event)
                # Input is not preloaded: it follows a guest output marker and
                # an explicit delay, so a real pending read must be resumed.
                time.sleep(0.03)
                if event.get("controlEof") and "controlBytes" not in event:
                    os.close(control_write)
                    control_write = -1
                if "resize" in event or "signal" in event or "monotonicNs" in event:
                    if "resize" in event:
                        operation, first, second = 1, *event["resize"]
                    elif "monotonicNs" in event:
                        value = event["monotonicNs"]
                        assert isinstance(value, int) and 0 <= value <= (1 << 64) - 1
                        operation, first, second = 4, value & 0xffffffff, value >> 32
                    else:
                        # Operation 2 is per-PID signal routing (second == 0
                        # queues to the active kernel; a positive signalPid
                        # routes to that specific store process).  Operation 3
                        # fans the signal across every live member of signalPgid
                        # via native_store_signal_process_group.
                        if "signalPgid" in event:
                            operation, first, second = 3, event["signal"], event["signalPgid"]
                        else:
                            operation, first, second = 2, event["signal"], event.get("signalPid", 0)
                    record = struct.pack("<4sIII", b"WSC1", operation, first, second)
                    # Exercise partial records without creating fake guest waits.
                    for start, stop in ((0, 3), (3, 11), (11, 16)):
                        os.write(control_write, record[start:stop])
                        time.sleep(0.01)
                elif "controlBytes" in event:
                    os.write(control_write, bytes(event["controlBytes"]))
                    if event.get("controlEof"):
                        os.close(control_write)
                        control_write = -1
                elif event.get("eof"):
                    process.stdin.close()
                    process.stdin = None
                else:
                    process.stdin.write(event["text"].encode() if "text" in event else bytes(event["bytes"]))
                    process.stdin.flush()
            remaining, errors = process.communicate(timeout=max(0.1, deadline - time.monotonic()))
            output.extend(remaining)
            diagnostics.extend(errors)
            for record in diagnostics.splitlines():
                if expected_error and record.decode() == expected_error:
                    continue
                assert record.startswith(b"{"), diagnostics.decode(errors="replace")
                assert json.loads(record)["kind"] in (1, 2), record
            return process.returncode, bytes(output), json.loads(result.read_text())
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
            if control_write >= 0:
                os.close(control_write)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", default="build/cli-rt/waste-session")
    parser.add_argument("--vfs-root", default="src/vfs")
    parser.add_argument("--wasm", default="build/html-rt/waste-wast.wasm")
    parser.add_argument("--scenario", action="append", choices=("shared-dependencies", "shared-libc", "io", "diy-control", "terminal-control", "terminal-readiness", "terminal-timing", "process-groups", "terminal-descriptors", "clock", "waits", "transfer", "signal-pid", "signal-pid-backgrounded", "signal-pgid", "signal-pgid-fork", "signal-pgid-backgrounded", "bash", "matrix", "pipeline", "heredoc", "heredoc-long", "exec-fail", "rogue-fresh", "rogue", "handlers", "handler-start"))
    parser.add_argument("--page", help="Also check identical scenarios through the packaged production worker")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="guest-session-", dir="build/engine") as temporary:
        for name in args.scenario or ("shared-dependencies", "shared-libc", "io", "diy-control", "terminal-control", "terminal-readiness", "terminal-timing", "process-groups", "terminal-descriptors", "clock", "waits", "transfer", "signal-pid", "signal-pid-backgrounded", "signal-pgid", "signal-pgid-fork", "signal-pgid-backgrounded", "bash", "matrix", "pipeline", "heredoc", "heredoc-long", "exec-fail", "rogue-fresh", "rogue", "handlers", "handler-start"):
            contract = f"tests/guest-session-{name}.json"
            scenario = json.loads(Path(contract).read_text())
            # Bash exercises the default mounted bootstrap, not a host copy.
            script = scenario["fixture"] if name in ("shared-libc", "io", "diy-control", "terminal-control", "terminal-readiness", "terminal-timing", "process-groups", "terminal-descriptors", "clock", "waits", "transfer", "signal-pid", "signal-pid-backgrounded", "signal-pgid", "signal-pgid-fork", "signal-pgid-backgrounded") else None
            extra_args = []
            if "clockRealtimeNs" in scenario:
                extra_args += ["--clock-realtime-ns", str(scenario["clockRealtimeNs"])]
            if "clockMonotonicNs" in scenario:
                extra_args += ["--clock-monotonic-ns", str(scenario["clockMonotonicNs"])]
            upload_count = download_count = 0
            for index, io in enumerate(scenario.get("hostIo", [])):
                if io["kind"] == "upload":
                    upload_count += 1
                    if io.get("cancel"):
                        extra_args += ["--host-upload-cancel"]
                    else:
                        upload_path = Path(temporary) / f"{name}-upload-{index}.bin"
                        upload_path.write_bytes(bytes(io["bytes"]))
                        extra_args += ["--host-upload-reply", str(upload_path)]
                else:
                    download_count += 1
                    extra_args += ["--host-download-cancel" if io.get("cancel")
                                   else "--host-download-complete"]
            code, output, result = native_session(args.native, args.vfs_root, script,
                                                 scenario["events"], Path(temporary) / f"{name}.json", 60,
                                                 files=scenario.get("files", ()), extra_args=extra_args)
            assert code == scenario["exitStatus"], result
            if "output" in scenario:
                assert output == scenario["output"].encode(), output
            for marker in scenario.get("contains", []):
                assert marker.encode() in output, output
            for marker, count in scenario.get("outputCounts", {}).items():
                assert output.count(marker.encode()) == count, (marker, count, output)
            assert result["total"] == scenario["assertions"], result
            assert result["passed"] == scenario.get("expectedPassed", result["total"]), result
            for key, value in scenario.get("nativeCounts", {}).items():
                assert result[key] == value, result
            if "expectedHandlerError" in scenario:
                assert result["handlerFailures"] == result["total"] - result["passed"], result
                assert result["handlerError"] == scenario["expectedHandlerError"], result
            if name == "io":
                assert result["readWaits"] == 3 and result["selectWaits"] == 0, result
                assert result["inputBytes"] == 2, result
            elif name == "terminal-control":
                assert result["inputBytes"] == 1, result
                assert result["resizeEvents"] == 4 and result["signalEvents"] == 3, result
            elif name == "transfer":
                assert result["uploadEvents"] == upload_count, result
                assert result["downloadEvents"] == download_count, result
            else:
                assert result["readWaits"] + result["selectWaits"] >= min(4, len(scenario["events"])), result
            assert result["exited"] == scenario.get("exited", True) and result["exitStatus"] == code and not result["error"], result
            if "processes" in scenario:
                assert result["forks"] == result["childExits"] == scenario["processes"], result
            if "execs" in scenario:
                assert result["execs"] == scenario["execs"], result
            browser = json.loads(subprocess.check_output([
                "node", "tests/guest-session-browser.cjs", args.wasm, args.vfs_root, contract], text=True))
            if scenario.get("comparison", "exact") == "exact":
                assert browser["output"] == output.decode(), browser
            assert browser["passed"] == result["passed"], browser
            assert browser["exited"] == result["exited"] and browser["exitStatus"] == code, browser
            if args.page and name not in ("shared-libc", "io", "clock", "transfer", "signal-pid", "signal-pid-backgrounded", "signal-pgid", "signal-pgid-fork", "signal-pgid-backgrounded"):
                worker = json.loads(subprocess.check_output([
                    "node", "tests/guest-session-worker.cjs", args.page, contract], text=True))
                assert worker["passed"] == result["passed"] and worker["exitStatus"] == code, worker
                inventory = json.loads(subprocess.check_output(["python3", "src/html-rt/tools/vfs.py",
                    "manifest", "--root", args.vfs_root], text=True))
                canonical = json.dumps(inventory, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()
                assert worker["manifestSha256"] == hashlib.sha256(canonical).hexdigest(), worker
                if scenario.get("comparison", "exact") == "exact":
                    assert worker["output"] == output.decode(), worker
            print(json.dumps(dict(scenario=name, native=result, browser=browser,
                                  nativeOutputBase64=base64.b64encode(output).decode(),
                                  **({"worker": worker} if args.page and name not in ("shared-libc", "io", "clock", "transfer", "signal-pid", "signal-pid-backgrounded", "signal-pgid", "signal-pgid-fork", "signal-pgid-backgrounded") else {})), indent=2))
        # An independent multi-module probe catches child provider mutations
        # even when a particular Bash allocation layout happens not to trap.
        contract = "tests/guest-session-linked-fork.json"
        scenario = json.loads(Path(contract).read_text())
        code, output, report = native_session(args.native, args.vfs_root,
            scenario["fixture"], [], Path(temporary) / "linked-fork.json", 30)
        assert code == 0 and output == scenario["output"].encode() and not report["error"], report
        assert report["passed"] == report["total"] == 3, report
        assert report["forks"] == report["childExits"] == 3, report
        browser = json.loads(subprocess.check_output([
            "node", "tests/guest-session-browser.cjs", args.wasm, args.vfs_root, contract], text=True))
        assert browser["output"] == output.decode() and browser["passed"] == 3, browser
        print(json.dumps(dict(scenario="linked-provider fork isolation", native=report, browser=browser)))
        negative = Path(temporary) / "fork.json"
        process = subprocess.Popen([args.native, "--vfs-root", args.vfs_root,
                                    "--script", "tests/guest-session-boundaries.wast",
                                    "--result-file", str(negative)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        # Keep stdin open: SELECT must wake on its own guest deadline, not EOF.
        try:
            assert process.wait(timeout=10) == 0
            report = json.loads(negative.read_text())
            assert report["passed"] == report["total"] == 2, report
            assert report["selectWaits"] == 1 and report["inputBytes"] == 0, report
            assert report["forks"] == report["childExits"] == 1 and not report["error"], report
        finally:
            if process.poll() is None:
                process.kill()
            process.communicate()
        negative = Path(temporary) / "timeout.json"
        process = subprocess.Popen([args.native, "--vfs-root", args.vfs_root,
                                    "--script", "tests/guest-session-io.wast", "--timeout-ms", "200",
                                    "--result-file", str(negative)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            assert process.wait(timeout=10) == 124
            report = json.loads(negative.read_text())
            assert report["timedOut"] and report["inputBytes"] == 0, report
        finally:
            if process.poll() is None:
                process.kill()
            process.communicate()
        # Existing result files, including aliases of an input, cannot be truncated.
        collision = Path(temporary) / "existing.json"
        collision.touch()
        process = subprocess.run([args.native, "--vfs-root", args.vfs_root,
                                  "--script", "tests/guest-session-boundaries.wast",
                                  "--result-file", str(collision)], input=b"", capture_output=True)
        assert process.returncode == 1 and collision.read_bytes() == b""
    print("PASS selected shared guest sessions: input/EOF/files, fork/exec utilities, pipelines/heredocs, Rogue, WAT/WAST handlers and exit-status parity")
    print("PASS native boundaries: SELECT deadline, fork/isolation/reaping, bounded input timeout, non-destructive results")


if __name__ == "__main__":
    main()
