#!/usr/bin/env python3
"""Deliver the actual guest TargetParser Host observer; WAST owns assertions."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile


def main():
    repo, vfs, build, runner = map(Path, sys.argv[1:])
    scenario = json.loads((Path(__file__).with_name("browser.json")).read_text())
    spec = importlib.util.spec_from_file_location(
        "guest_session_check", repo / "src/engine/test/guest-session/guest-session-check.py")
    bridge = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bridge)
    files = [dict(file, source=str(repo / file["source"]))
             for file in scenario["files"]]
    files[0]["source"] = str(build / "probe.wasm")
    suffix = "sanitize" if "sanitize" in runner.name else "native"
    run = Path(tempfile.mkdtemp(prefix=f"target-host-{suffix}-", dir=build))
    result_path = run / "results.json"
    code, output, result = bridge.native_session(
        str(runner), str(vfs), str(repo / scenario["fixture"]),
        scenario["events"], result_path, timeout=45, files=files,
        asan_options="detect_leaks=0:halt_on_error=1")
    (run / "output.txt").write_bytes(output)
    (build / f"target-host-{suffix}.txt").write_bytes(output)
    (build / f"target-host-{suffix}.json").write_bytes(result_path.read_bytes())
    assert code == scenario["exitStatus"], result
    for marker in scenario["contains"]:
        assert marker.encode() in output, output
    assert result["passed"] == result["total"] == scenario["assertions"], result
    print(f"PASS LLVM guest target host {suffix}: {result['passed']}/{result['total']}; {result_path}")


if __name__ == "__main__":
    main()
