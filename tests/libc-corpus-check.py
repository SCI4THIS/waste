#!/usr/bin/env python3
"""Check installed libc coverage and completion with real C runtime providers."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
# These are existing C profile mismatches, not missing OCaml capabilities.
# An additional failure within either XFAIL file must fail this gate too.
EXPECTED_FAILURES = {
    "libc-test/environment-boundaries.wast": {
        "boundary-execve", "boundary-readlink", "boundary-opendir", "boundary-ioctl"},
    "libc-test/terminal.wast": {"terminal"},
}


def contract(parser):
    clients = sorted((ROOT / "tests/libc-test").glob("*-client.wast.inc"))
    assert clients, "no authored libc clients"
    manifest = json.loads((ROOT / "src/vfs/tests/manifest.json").read_text())
    installed = [t for t in manifest["tests"] if t["group"] == "libc-test"]
    identities = ["libc-test/" + p.name.replace("-client.wast.inc", ".wast") for p in clients]
    assert [t["id"] for t in installed] == identities, "libc selection differs from authored clients"
    expectations = {}
    for client, test in zip(clients, installed):
        assert not test["unsupported"] and test["executionSpec"]["mode"] == "wast-stream", test["id"]
        source = ROOT / "src/vfs" / test["path"].lstrip("/")
        data = source.read_bytes()
        assert data.endswith(client.read_bytes()), f"stale installed client: {client}"
        result = subprocess.run([str(parser), "--browser-spec", str(source)],
                                capture_output=True, text=True, check=True, timeout=30)
        assert not result.stderr, result.stderr
        spec = json.loads(result.stdout)
        assert not spec.get("error"), spec
        assert all(g["module_hex"] and g["module_assertion"] is None for g in spec["groups"]), test["id"]
        names = [a["func"] for g in spec["groups"] for a in g["assertions"]]
        assert names, f"no parsed assertions: {test['id']}"
        assert EXPECTED_FAILURES.get(test["id"], set()) <= set(names), "stale failure baseline"
        expectations[test["id"]] = names
    assert set(EXPECTED_FAILURES) <= set(expectations), "missing expected-failure fixture"
    return expectations


def verify(report, expectations):
    assert report["exitCode"] == 0, report.get("summary")
    assert [t["identity"] for t in report["tests"]] == list(expectations), "incomplete or reordered libc selection"
    failed_files = 0
    total = 0
    for test in report["tests"]:
        identity = test["identity"]
        names = expectations[identity]
        failures = EXPECTED_FAILURES.get(identity, set())
        assert test["status"] == ("XFAIL" if failures else "PASS"), test
        assert test["group"] == "libc-test" and test["mode"] == "wast-stream", test
        assert not test.get("error") and not test.get("stderrPreview"), test
        details = test.get("nativeReport", test.get("browserReport"))
        assert details is not None and not details.get("error"), test
        outcomes = details.get("assertions", details.get("results"))
        assert outcomes is not None, test
        assert [(a["func"], a["pass"]) for a in outcomes] == [
            (name, name not in failures) for name in names], identity
        if "nativeReport" in test:
            assert [a["index"] for a in outcomes] == list(range(len(names))), identity
        else:
            assert details["type"] == "done" and not details["timedOut"] and not details["cancelled"], test
            assert not details["exited"] and details["ok"] == (not failures), test
            # The browser stores ordinal order in the results array.
            assert "load error" not in details.get("outputPreview", ""), test
            assert "encode error" not in details.get("outputPreview", ""), test
        for outcome in outcomes:
            if not outcome["pass"]:
                assert outcome.get("error") == (
                    f"result mismatch for {outcome['func']} (actual 0, expected 1)"), outcome
        passed = sum(name not in failures for name in names)
        assert test["passed"] == details["passed"] == passed, identity
        assert test["total"] == details["total"] == len(names), identity
        failed_files += bool(failures)
        total += len(names)
    assert report["summary"] == {
        "pass": len(expectations) - failed_files, "fail": 0, "xfail": failed_files,
        "xpass": 0, "skip": 0, "failures": [], "unexpectedPasses": []}, report["summary"]
    print(f"PASS libc corpus: {len(expectations)} fixtures / {total} ordered assertions; "
          f"{sum(map(len, EXPECTED_FAILURES.values()))} exact expected mismatches; complete reported results")


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("--runner", type=Path, default=ROOT / "build/cli-rt/waste-test")
    args.add_argument("--parser", type=Path, default=ROOT / "build/cli-rt/waste-cli")
    args.add_argument("--results", type=Path, help="save native report outside the installed tree")
    args.add_argument("--report", type=Path, help="verify an existing native or browser libc group report")
    options = args.parse_args()
    expectations = contract(options.parser.resolve())
    if options.report:
        assert options.results is None, "--report and --results are mutually exclusive"
        verify(json.loads(options.report.read_text()), expectations)
        return
    with tempfile.TemporaryDirectory(prefix="waste-libc-corpus-") as temporary:
        destination = (options.results or Path(temporary) / "results.json").resolve()
        result = subprocess.run([
            str(options.runner.resolve()), "--vfs-root=" + str(ROOT / "src/vfs"),
            "--group=libc-test", "--jobs=1", "--json", "--results=" + str(destination),
            "--expected-failures=" + str(ROOT / "tests/native-corpus-expected-failures.txt")],
            capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        assert "AddressSanitizer" not in result.stderr and "runtime error:" not in result.stderr, result.stderr
        verify(json.loads(destination.read_text()), expectations)


if __name__ == "__main__":
    main()
