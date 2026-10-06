#!/usr/bin/env python3
"""Check the native host boundary; guest assertions stay in WAST fixtures."""
import copy
import json
import os
from pathlib import Path
import signal
import hashlib
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src/html-rt/tools"))
from runtime_config import read_config
CONFIG = read_config()
RUNNER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/cli-rt/waste-test").resolve()


def deep_json():
    value = 0
    for _ in range(CONFIG["JSON_MAX_DEPTH"] + 1):
        value = [value]
    return value


def entry(identity, source, **changes):
    test = dict(id=identity, group=identity.rsplit("/", 1)[0], path="/root/waste/tests/" + identity,
                executionSpec=dict(mode="wast-stream", file=identity), assets=[],
                unsupported=False, expectFailure=False, unsupportedReason=None)
    test.update(changes)
    return test, source


with tempfile.TemporaryDirectory(prefix="waste-native-suite-") as temporary:
    temp = Path(temporary)
    vfs_root = temp / "vfs"
    report = temp / "results.json"
    baseline = temp / "xfail.txt"
    baseline.write_text("# an intentional guest assertion failure\nsynthetic/fail.wast\n")
    definitions = [entry("synthetic/" + name + ".wast", (ROOT / "tests" / source).read_bytes())
                   for name, source in [
                       ("pass", "test-suite-pass.wast"), ("fail", "test-suite-fail.wast"),
                       ("writer", "test-suite-kernel-isolation-a.wast"),
                       ("reader", "test-suite-kernel-isolation-b.wast"),
                       ("memory-writer", "diy-posix-test/spectest-isolation-a.wast"),
                       ("memory-reader", "diy-posix-test/spectest-isolation-b.wast")]]
    definitions += [entry("legacy/skip.wast", b"invalid input", unsupported=True,
                          unsupportedReason="unsupported syntax"),
                    entry("compat/skip.wast", b"invalid input",
                          executionSpec=dict(mode="browser-native", file="skip.wast"))]

    def install(items=definitions, transform=None, extra_files=None):
        manifest = dict(format=1, tests=[copy.deepcopy(t) for t, _ in items])
        if transform:
            transform(manifest)
        files = {"/root/waste/tests/manifest.json": json.dumps(manifest).encode()}
        files.update({t["path"]: source for t, source in items if source is not None})
        files.update(extra_files or {})
        directories = {"/", "/root", "/tmp", "/root/waste", "/root/waste/tests"}
        for name in files:
            parent = str(Path(name).parent)
            while parent != "/":
                directories.add(parent)
                parent = str(Path(parent).parent)
        paths = sorted(directories | set(files), key=lambda p: (len(Path(p).parts), p))
        if vfs_root.exists():
            shutil.rmtree(vfs_root)
        vfs_root.mkdir()
        entries = []
        for inode, name in enumerate(paths, 1):
            content = files.get(name, b"")
            directory = name in directories
            entry = dict(path=name, role="directory" if directory else "file",
                         kind=2 if directory else 1, mode=0o755 if directory else 0o644,
                         uid=0, gid=0, size=len(content), inode=inode, mtime_sec=0, mtime_nsec=0)
            dest = vfs_root/name.lstrip("/")
            if directory:
                dest.mkdir(exist_ok=True)
            else:
                entry['sha256'] = hashlib.sha256(content).hexdigest()
                dest.write_bytes(content)
            entries.append(entry)
        (vfs_root/'.inventory.json').write_text(json.dumps(dict(version=1, entries=entries)))

    def run(*args, expected=0, results=True):
        command = [str(RUNNER), "--vfs-root=" + str(vfs_root)]
        if results:
            command += ["--results=" + str(report)]
        result = subprocess.run(command + list(args), capture_output=True, text=True,
                                timeout=30, env={**os.environ, "ASAN_OPTIONS":
                                    "detect_leaks=0:malloc_fill_byte=165", "MALLOC_PERTURB_": "165"})
        assert result.returncode == expected, (command, args, result.returncode, result.stderr)
        assert "AddressSanitizer" not in result.stderr and "runtime error:" not in result.stderr, result.stderr
        return json.loads(report.read_text()) if results else result

    install()
    for jobs in (1, 3):
        result = run("--jobs=" + str(jobs), "--expected-failures=" + str(baseline))
        assert [t["identity"] for t in result["tests"]] == [t["id"] for t, _ in definitions]
        assert [t["status"] for t in result["tests"]] == ["PASS", "XFAIL", "PASS", "PASS", "PASS", "PASS", "SKIP", "SKIP"]
        assert result["summary"] == {"pass": 5, "fail": 0, "xfail": 1, "xpass": 0, "skip": 2,
                                      "failures": [], "unexpectedPasses": []}
        assertions = result["tests"][0]["nativeReport"]["assertions"]
        assert len(assertions) == 3 and all(a["pass"] for a in assertions)
        assert result["tests"][1]["nativeReport"]["assertions"][0]["pass"] is False
    listed = run("--list", results=False)
    assert "SKIP legacy/skip.wast: unsupported syntax" in listed.stdout
    # Imported memory aliases within a script; scheduling a different script
    # must allocate a fresh host memory, in either order and at either job count.
    memory_tests = definitions[4:6]
    for ordered in (memory_tests, list(reversed(memory_tests))):
        install(ordered)
        for jobs in (1, 3):
            result = run("--jobs=" + str(jobs))
            assert [t["identity"] for t in result["tests"]] == [t["id"] for t, _ in ordered]
            for test in result["tests"]:
                expected = 4 if test["identity"].endswith("memory-writer.wast") else 1
                assert test["status"] == "PASS" and test["passed"] == test["total"] == expected
    install()
    filtered = run("--group=synthetic", "--exclude=fail.wast", "--exclude-group=compat")
    assert len(filtered["tests"]) == 5 and filtered["summary"]["pass"] == 5
    assert len(run("/root/waste/tests/synthetic/pass.wast")["tests"]) == 1
    run("--group=missing", expected=2, results=False)
    run("--jobs=NaN", expected=2, results=False)
    run("--timeout-ms=0", expected=2, results=False)
    run("--timeout-group=synthetic:NaN", expected=2, results=False)
    run("--unknown", expected=2, results=False)

    baseline.write_text("synthetic/pass.wast\n")
    result = run("pass.wast", "--expected-failures=" + str(baseline), expected=1)
    assert result["tests"][0]["status"] == "XPASS"
    assert result["summary"]["unexpectedPasses"] == ["synthetic/pass.wast"]
    baseline.write_text("synthetic/not-installed.wast\n")
    run("--expected-failures=" + str(baseline), expected=2, results=False)

    hang = entry("waiting/hang.wast", (ROOT / "tests/test-suite-timeout.wast").read_bytes())
    install([hang, definitions[0]])
    result = run("--timeout-group=waiting:200", expected=1)
    assert [t["status"] for t in result["tests"]] == ["TIMEOUT", "PASS"]
    assert "nativeReport" not in result["tests"][0]
    # Timed waits use the same reporting and host bounds as runnable guests.
    timed = entry("waiting/timed.wast", (ROOT / "tests/test-suite-select-timeout.wast").read_bytes())
    install([timed])
    assert run()["tests"][0]["passed"] == 2
    blocked = entry("waiting/blocked.wast", (ROOT / "tests/test-suite-select-deadline.wast").read_bytes())
    install([blocked, definitions[0]])
    result = run("--timeout-group=waiting:200", expected=1)
    assert [t["status"] for t in result["tests"]] == ["TIMEOUT", "PASS"]
    assert "nativeReport" not in result["tests"][0]
    assert result["tests"][0]["elapsedMs"] < 5000
    # Cancellation is supplied by the host, outside the WAST guest boundary.
    for interrupted in (hang, blocked):
        install([interrupted, definitions[0]])
        process = subprocess.Popen([str(RUNNER), "--vfs-root=" + str(vfs_root), "--timeout-ms=10000",
                                    "--results=" + str(report)], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
        time.sleep(0.3)
        process.send_signal(signal.SIGINT)
        stdout, stderr = process.communicate(timeout=10)
        assert process.returncode == 130, (stdout, stderr)
        result = json.loads(report.read_text())
        assert all(t["status"] == "CANCELLED" for t in result["tests"])

    missing = entry("synthetic/missing.wast", None, expectFailure=True)
    install([missing, definitions[0]])
    result = run(expected=1)
    assert [t["status"] for t in result["tests"]] == ["FAIL", "PASS"]
    assert "cannot read mounted WAST" in result["tests"][0]["error"]
    install([entry("synthetic/bad.wast", b"(module"), definitions[0]])
    result = run(expected=1)
    assert [t["status"] for t in result["tests"]] == ["FAIL", "PASS"]
    assert result["tests"][0]["nativeReport"]["error"]

    # The guest owns module/action expectations; this gate checks that every
    # command result survives serialization, including recovery and failures.
    stream_items = [entry("synthetic/" + name + ".wast",
                          (ROOT / "tests" / source).read_bytes()) for name, source in [
        ("stream", "test-suite-command-stream.wast"),
        ("stream-fail", "test-suite-command-stream-fail.wast"),
        ("stream-recovery", "test-suite-command-stream-recovery.wast")]]
    install(stream_items)
    result = run(expected=1)
    assert [t["status"] for t in result["tests"]] == ["PASS", "FAIL", "FAIL"]
    positive, negative, recovery = [t["nativeReport"] for t in result["tests"]]
    assert positive["passed"] == positive["total"] == 18
    assert [(a["func"], a["pass"]) for a in negative["assertions"]] == [
        ("(module)", True), ("answer", False), ("answer", True)]
    assert [(a["func"], a["pass"]) for a in recovery["assertions"]] == [
        ("answer", True), ("(parse)", False), ("answer", True)]
    assert recovery["error"]
    for native in (positive, negative, recovery):
        assertions = native["assertions"]
        assert [a["index"] for a in assertions] == list(range(len(assertions)))
        assert [a["line"] for a in assertions] == sorted(a["line"] for a in assertions)
    assert [a["line"] for a in negative["assertions"]] == [3, 4, 5]
    # Setup errors must affect completion independently of assertion results.
    setup_cases = [("empty", 0, 0, 0), ("valid", 3, 3, 0),
                   ("invalid", 1, 0, 0), ("unlinkable", 1, 0, 0),
                   ("start-trap", 1, 0, 0), ("definition", 1, 0, 0), ("encode", 1, 0, 0),
                   ("recovery", 4, 2, 3)]
    setup_items = [entry("synthetic/setup-" + name + ".wast",
                        (ROOT / ("tests/test-suite-setup-" + name + ".wast")).read_bytes())
                   for name, _, _, _ in setup_cases]
    install(setup_items)
    setup_results = run(expected=1)
    for test, (name, total, passed, assertions) in zip(setup_results["tests"], setup_cases):
        detail = test["nativeReport"]
        assert test["status"] == ("PASS" if passed == total else "FAIL")
        assert detail["completed"] and detail["setup"]["complete"]
        assert detail["setup"]["total"] == total and detail["setup"]["passed"] == passed
        assert len(detail["setup"]["failures"]) == total - passed
        assert detail["total"] == detail["passed"] == assertions
        if passed != total:
            failure = detail["setup"]["failures"][0]
            assert failure["line"] == (4 if name == "recovery" else 2)
            assert failure["status"] != 0 and failure["error"]
            assert failure["phase"] == (name if name in ("definition", "encode") else "load")
            if name == "recovery":
                assert [(f["line"], f["phase"]) for f in detail["setup"]["failures"]] == [
                    (4, "load"), (5, "encode")]
    setup_baseline = temp / "setup-xfail.txt"
    setup_baseline.write_text("synthetic/setup-invalid.wast\nsynthetic/setup-valid.wast\n")
    policy = run("--expected-failures=" + str(setup_baseline), expected=1)
    assert policy["tests"][1]["status"] == "XPASS" and policy["tests"][2]["status"] == "XFAIL"
    if len(sys.argv) > 2:
        Path(sys.argv[2]).write_text(json.dumps(dict(setupReport=setup_results,
                                                    setupPolicy=policy), indent=2) + "\n")
    # Segment behavior stays in portable WAST; these checks guard host acceptance.
    install([entry("synthetic/segment-indices.wast",
                   (ROOT / "tests/test-suite-segment-indices.wast").read_bytes())])
    segments = run()
    detail = segments["tests"][0]["nativeReport"]
    assert detail["completed"] and detail["setup"]["passed"] == detail["setup"]["total"] == 2
    assert detail["passed"] == detail["total"] == 64
    capacity_reports = []
    for kind, label in (("data", "data"), ("elem", "element")):
        limit = CONFIG["WAST_MAX_DATA_SEGS" if kind == "data" else "WAST_MAX_ELEM_SEGS"]
        segment = '(data "x")' if kind == "data" else '(elem func)'
        source = ("(module\n" + (segment + "\n") * (limit + 1) + ")").encode()
        for wrapped in (False, True):
            probe = b'(assert_invalid ' + source + b' "invalid")' if wrapped else source
            install([entry("synthetic/capacity.wast", probe)])
            result = run(expected=1)
            detail = result["tests"][0]["nativeReport"]
            assert any(a["func"] == "(parse)" and
                       f"{label} segment capacity exceeded ({limit})" in a["error"]
                       for a in detail["assertions"])
            assert result["tests"][0]["status"] == "FAIL"
            capacity_reports.append(result)
    if len(sys.argv) > 2:
        destination = Path(sys.argv[2])
        saved = json.loads(destination.read_text())
        saved.update(segments=segments, capacity=capacity_reports)
        destination.write_text(json.dumps(saved, indent=2) + "\n")
    install([entry("synthetic/element-types.wast",
                   (ROOT / "tests/test-suite-element-types.wast").read_bytes())])
    elements = run()
    detail = elements["tests"][0]["nativeReport"]
    assert detail["completed"] and detail["setup"]["passed"] == detail["setup"]["total"] == 11
    assert detail["passed"] == detail["total"] == 82
    if len(sys.argv) > 2:
        destination = Path(sys.argv[2])
        saved = json.loads(destination.read_text())
        saved.update(elements=elements)
        destination.write_text(json.dumps(saved, indent=2) + "\n")
    table_items = [entry("synthetic/table64-" + name + ".wast",
                         (ROOT / "tests" / source).read_bytes())
                   for name, source in [("writer", "test-suite-spectest-table64.wast"),
                                        ("reader", "test-suite-spectest-table64-isolation.wast")]]
    tables = []
    for ordered in (table_items, list(reversed(table_items))):
        for jobs in (1, 3):
            install(ordered)
            result = run("--jobs=" + str(jobs))
            assert [t["identity"] for t in result["tests"]] == [t["id"] for t, _ in ordered]
            for test in result["tests"]:
                writer = test["identity"].endswith("writer.wast")
                detail = test["nativeReport"]
                assert test["status"] == "PASS"
                assert detail["passed"] == detail["total"] == (36 if writer else 3)
                assert detail["setup"]["passed"] == detail["setup"]["total"] == (3 if writer else 1)
            tables.append(result)
    if len(sys.argv) > 2:
        destination = Path(sys.argv[2])
        saved = json.loads(destination.read_text())
        saved.update(tables=tables)
        destination.write_text(json.dumps(saved, indent=2) + "\n")
    install([entry("synthetic/flat-bulk.wast",
                   (ROOT / "tests/test-suite-flat-bulk.wast").read_bytes())])
    flat_bulk = run()
    detail = flat_bulk["tests"][0]["nativeReport"]
    assert detail["completed"] and detail["setup"]["passed"] == detail["setup"]["total"] == 3
    assert detail["passed"] == detail["total"] == 190
    if len(sys.argv) > 2:
        destination = Path(sys.argv[2])
        saved = json.loads(destination.read_text())
        saved.update(flatBulk=flat_bulk)
        destination.write_text(json.dumps(saved, indent=2) + "\n")
    install([entry("synthetic/table-copy-fill.wast",
                   (ROOT / "tests/test-suite-table-copy-fill.wast").read_bytes())])
    table_bulk = run()
    detail = table_bulk["tests"][0]["nativeReport"]
    assert detail["completed"] and detail["setup"]["passed"] == detail["setup"]["total"] == 7
    assert detail["passed"] == detail["total"] == 611
    if len(sys.argv) > 2:
        destination = Path(sys.argv[2])
        saved = json.loads(destination.read_text())
        saved.update(tableBulk=table_bulk)
        destination.write_text(json.dumps(saved, indent=2) + "\n")
    # Unterminated tails retain already executed results and fail the file.
    install([entry("synthetic/tail.wast", stream_items[0][1] + b"\n(module")])
    tail = run(expected=1)["tests"][0]["nativeReport"]
    assert tail["passed"] == 18 and tail["total"] == 19 and tail["error"]
    assert tail["assertions"][-1]["func"] == "(parse)"
    assert not tail["completed"]
    install([entry("synthetic/names.wast", (ROOT / "tests/test-suite-result-names.wast").read_bytes())])
    names = run()["tests"][0]["nativeReport"]["assertions"]
    assert [(a["func"], a["pass"]) for a in names] == [
        ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqr", True),
        ("\ufeff", True)]

    support = "/root/waste/tests/.support/test-suite/file.bin"
    companion = entry("synthetic/companion.wast", (ROOT / "tests/test-suite-companion.wast").read_bytes(),
                      assets=[dict(kind="vfs-file", path=support, mountPath="/companion", mode=0o644)])
    install([companion], extra_files={support: b"tail"})
    assert run()["tests"][0]["status"] == "PASS"
    install([companion])
    assert "cannot stage mounted companion files" in run(expected=1)["tests"][0]["error"]
    # Escaped JSON identities survive decoding and UTF-8 report serialization.
    unicode_test = entry("synthetic/\u03c0.wast", definitions[0][1])
    install([unicode_test])
    assert run()["tests"][0]["identity"] == "synthetic/\u03c0.wast"

    for transform in [
        lambda m: m.update(format=2),
        lambda m: m.update(tests=m["tests"] * 2),
        lambda m: m["tests"][0].update(path="/root/waste/tests/../escape.wast"),
        lambda m: m["tests"][0].update(id="../escape.wast", path="/root/waste/tests/../escape.wast"),
        lambda m: m["tests"][0].update(unsupported="false"),
        lambda m: m["tests"][0].update(id="bad\0name"),
        lambda m: m["tests"][0].update(group="other"),
        lambda m: m["tests"][0].update(assets=[dict(kind="vfs-file", path=support,
                                                 mountPath="/../escape", mode=0o644)]),
        lambda m: m["tests"][0].update(assets=[dict(kind="vfs-file", path=support,
                                                 mountPath="/companion", mode=1024)]),
        lambda m: m.update(extra=deep_json()),
    ]:
        install([definitions[0]], transform)
        run(expected=2, results=False)
    for malformed in (b'{"format":1,"tests":[]}', b'{"format":1,"tests":[]} garbage',
                      b'{"format":1,"extra":"\xff","tests":[]}',
                      b'{"format":1,"extra":"\xe0\x80\x80","tests":[]}',
                      b'{"format":1,"extra":"\\u12zz","tests":[]}',
                      b'{"format":1,"format":1,"tests":[]}',
                      b'{"format":01,"tests":[]}', b'{"format":1,"tests":[,]}'):
        install(extra_files={"/root/waste/tests/manifest.json": malformed})
        run(expected=2, results=False)
    install()
    inventory_path = vfs_root/".inventory.json"
    original = inventory_path.read_bytes()
    run("--results=" + str(inventory_path), expected=2, results=False)
    assert inventory_path.read_bytes() == original
    install([entry("synthetic/nested.wast", (ROOT / "tests/guest-test-nested-capability.wast").read_bytes())])
    assert run()["tests"][0]["passed"] == 1
    install([entry("synthetic/i64-min.wast", (ROOT / "tests/test-suite-i64-min-literal.wast").read_bytes())])
    assert run()["tests"][0]["passed"] == 2
    print("PASS native suite selection, assertion reporting, XFAIL/XPASS/SKIP, isolation, timeout, cancellation and manifest rejection")
