#!/usr/bin/env python3
"""Stage one lld-diagnostics artifact and drive the existing controlled-clock adapter.

Functional expectations stay in diagnostics.wast; this checks only suite delivery.
"""
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
    # Native evidence deliberately refuses to overwrite an existing path.
    # Keep each original result and publish a latest-run convenience copy.
    run = Path(tempfile.mkdtemp(prefix=f"lld-diagnostics-{suffix}-", dir=build))
    result_path = run / "results.json"
    code, output, result = bridge.native_session(
        str(runner), str(vfs), str(repo / scenario["fixture"]),
        scenario["events"], result_path, timeout=60, files=files,
        extra_args=["--clock-realtime-ns", str(scenario["clockRealtimeNs"]),
                    "--clock-monotonic-ns", str(scenario["clockMonotonicNs"])],
        asan_options="detect_leaks=0:halt_on_error=1")
    (build / f"lld-diagnostics-{suffix}.txt").write_bytes(output)
    (run / "output.txt").write_bytes(output)
    (build / f"lld-diagnostics-{suffix}.json").write_bytes(result_path.read_bytes())
    assert code == scenario["exitStatus"], result
    for marker in scenario["contains"]:
        assert marker.encode() in output, output
    assert result["total"] == scenario["assertions"], result
    assert result["passed"] == result["total"], result
    print(f"PASS LLD diagnostics {suffix}: {result['passed']}/{result['total']}, "
          f"exit={code}; {result_path}")


if __name__ == "__main__":
    main()
