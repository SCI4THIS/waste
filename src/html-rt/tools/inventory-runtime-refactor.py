#!/usr/bin/env python3
"""Record the pre-refactor paths, corpus, and optional execution baseline."""

from __future__ import annotations

import argparse
from collections import Counter
import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import time


FRONTEND = Path("src/html-rt/src")
LEGACY_PATHS = (
    "src/html-rt/src/bash", "src/html-rt/src/tests",
    "src/html-rt/src/shared", "src/html-rt/lib/include",
    "src/engine/lib/include", "src/engine/include", "lib/include/",
    "../shared/", "SRC_DIR", "PAGE_DIR", "HEADER_ROOT",
    '"bash"', '"shared"', '"tests"',
)
BASH_SCENARIOS = (
    (), ("--missing-command",), ("--readline-echo",),
    ("--readline-completion", "--full-package"), ("--readline-arrow",),
    ("--heredoc", "--full-package"), ("--coreutils-matrix", "--full-package"),
    ("--shared-library",),
)


def fingerprint(root: Path, path: Path) -> dict:
    record = {"source": path.relative_to(root).as_posix()}
    if path.is_symlink():
        record["symlink"] = str(path.readlink())
    if path.is_file():
        stat = path.stat()
        record.update(size=stat.st_size, mode=oct(stat.st_mode & 0o777),
                      mtime_ns=stat.st_mtime_ns,
                      sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    else:
        record["missing"] = True
    return record


def frontend_destination(relative: Path) -> tuple[str, str]:
    family, *parts = relative.parts
    name = relative.name
    if name == "waste-wast.wasm":
        return "build/html-rt/waste-wast.wasm", "host-engine"
    if name == "launch.wast":
        return "build/html-rt/bash-runtime.wast", "runtime-bootstrap"
    if name == "payload.json":
        return "build/html-rt/tests/payload.json", "generated-test-payload"
    if name == "tarball.js":
        return "src/html-rt/src/tarball.js", "vendor-link"
    if name.endswith(".so.wasm"):
        return f"src/vfs/lib/{name}", "guest-library"
    if name.endswith(".wasm"):
        return f"src/vfs/usr/bin/{name[:-5]}", "guest-executable"
    if family == "tests":
        return f"build/html-rt/tests/{'/'.join(parts)}", "test-staging"
    if family == "terminal" or len(relative.parts) == 1:
        role = "test-worker-source" if name == "tests-worker.js" else "frontend-source"
        return f"src/html-rt/src/{relative.as_posix()}", role
    return f"src/html-rt/src/{'/'.join(parts)}", "frontend-source"


def collect(root: Path) -> dict:
    frontend = []
    for path in sorted((root / FRONTEND).rglob("*")):
        if not (path.is_file() or path.is_symlink()):
            continue
        destination, role = frontend_destination(path.relative_to(root / FRONTEND))
        frontend.append({**fingerprint(root, path), "destination": destination,
                         "role": role})
        if role == "guest-executable":
            frontend[-1]["guest_paths"] = (["/bin/waste-probe"] if
                path.stem == "waste-probe" else ["/usr/bin/" + path.stem,
                                                "/bin/" + path.stem])
        elif role == "vendor-link":
            frontend[-1]["target"] = "../../../submodules/tarballjs/tarball.js"
        elif role == "guest-library":
            frontend[-1]["guest_paths"] = ["/lib/" + path.name,
                                             "/usr/lib/" + path.name]
    destinations = Counter(item["destination"] for item in frontend)
    aliases = []
    collisions = []
    for destination, count in sorted(destinations.items()):
        if count <= 1:
            continue
        entries = [item for item in frontend if item["destination"] == destination]
        if all(item["role"] == "host-engine" for item in entries) and len(
                {item.get("sha256") for item in entries}) == 1:
            aliases.append({"destination": destination,
                            "sources": [item["source"] for item in entries]})
        else:
            collisions.append(destination)

    installed_vfs = json.loads((root / "src/vfs/.inventory.json").read_text()) if (root / "src/vfs/.inventory.json").is_file() else None
    headers = []
    for directory in ("src/engine", "src/cli-rt", "src/html-rt", "src/vfs/usr/include", "src/vfs/usr/lib/waste/cc/include"):
        for path in sorted((root / directory).rglob("*.h")):
            relative = path.relative_to(root).as_posix()
            if "/build/" in relative:
                continue
            public = relative.startswith("src/vfs/usr/include/")
            decision = "mounted-guest-sdk" if public else "retain-private"
            if relative.startswith("src/vfs/usr/lib/waste/cc/include/"):
                decision = "mounted-compiler-support"
            elif relative.startswith("src/html-rt/profiles/"):
                decision = "optional-package-profile"
            if path.name in ("helper.h", "waste-gnulib-compat.h") and public:
                decision = "split-private-or-build-profile"
            elif path.name == "waste.h":
                decision = "optional-embedding-sdk"
            headers.append({**fingerprint(root, path), "decision": decision})

    generator_path = root / "src/html-rt/tools/generate-c-engine-tests.py"
    module_spec = importlib.util.spec_from_file_location("corpus", generator_path)
    generator = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(generator)
    roots = (("submodules/wasm-spec/test", "wasm-spec", ""),
             ("tests/diy-posix-test", "diy-posix-test", "diy-posix-test"),
             ("build/html-rt/waste-libc/tests", "libc-test", "libc-test"))
    payload_path = root / "build/html-rt/tests/payload.json"
    payload = json.loads(payload_path.read_text())["tests"] if payload_path.is_file() else []
    packaged = {test["path"]: test for test in payload}
    corpus = []
    for directory, suite, prefix in roots:
        if not (root / directory).is_dir():
            raise RuntimeError(f"missing baseline corpus: {directory}")
        for entry in generator.collect_layout_tests(root / directory, suite, prefix):
            test = packaged.get(entry["relative"], {})
            spec = test.get("spec", {})
            corpus.append({**fingerprint(root, entry["path"]),
                           "id": entry["relative"], "group": entry["group"],
                           "suite": suite, "unsupported": entry["unsupported"],
                           "unsupported_reason": entry["unsupportedReason"],
                           "expect_failure": entry["expectFailure"],
                           "mode": spec.get("mode"),
                           "assertion_count": spec.get("assertionCount"),
                           "spec_keys": sorted(spec),
                           "packaged_spec": spec,
                           "spec_sha256": hashlib.sha256(json.dumps(
                               spec, sort_keys=True).encode()).hexdigest(),
                           "destination": "src/vfs/root/waste/tests/" + entry["relative"]})

    consumers = []
    files = subprocess.run(["rg", "--files", "src", "tests", "docs",
                            "start.sh", "AGENTS.md", "README.md"], cwd=root,
                           capture_output=True, text=True, check=True).stdout.splitlines()
    for name in sorted(files):
        path = root / name
        if path.suffix in (".wasm", ".json", ".wat", ".wast"):
            continue
        matches = []
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if any(old in line for old in LEGACY_PATHS):
                matches.append({"line": number, "text": line.strip()})
        if matches:
            consumers.append({"source": name, "references": matches})

    def import_names(path: Path) -> list[str]:
        text = path.read_text()
        names = set(re.findall(r'strcmp\(name,\s*"([^"\n]+)"', text))
        # Include versioned path ABI identifiers referenced by their macros.
        for macro in re.findall(r'strcmp\(name,\s*(POSIX_\w+)', text):
            for header in (root / "src/engine/lib/include").rglob("*.h"):
                names.update(re.findall(r'#define\s+' + macro + r'\s+"([^"]+)"',
                                        header.read_text()))
        return sorted(names)

    native_imports = import_names(root / "src/cli-rt/main.c")
    browser_imports = import_names(root / "src/html-rt/posix_stubs.c")
    ids = {test["id"] for test in corpus}
    stale_specs = [{"id": test["id"], "source_bytes": test.get("size"),
                    "packaged_bytes": test["packaged_spec"].get("sourceBytes")}
                   for test in corpus if test["mode"] == "wast-stream" and
                   test.get("size") != test["packaged_spec"].get("sourceBytes")]
    return {"format": 1,
            "revision": subprocess.run(["git", "rev-parse", "HEAD"], cwd=root,
                capture_output=True, text=True, check=True).stdout.strip(),
            "oracle_revision": subprocess.run(["git", "-C", "submodules/wasm-spec",
                "rev-parse", "HEAD"], cwd=root, capture_output=True,
                text=True, check=True).stdout.strip(),
            "frontend": frontend, "destination_collisions": collisions,
            "destination_aliases": aliases,
            "headers": headers, "consumers": consumers, "corpus": corpus,
            "payload": fingerprint(root, payload_path),
            "payload_only": sorted(set(packaged) - ids),
            "corpus_only": sorted(ids - set(packaged)),
            "stale_packaged_specs": stale_specs,
            "groups": dict(sorted(Counter(t["group"] for t in corpus).items())),
            "summary": {"frontend_files": len(frontend), "headers": len(headers),
                        "tests": len(corpus), "supported": sum(
                            not t["unsupported"] for t in corpus)},
            "imports": {"method": "literal name comparisons plus path ABI macros; not signatures",
                        "native": native_imports, "browser": browser_imports,
                        "browser_only": sorted(set(browser_imports) - set(native_imports))},
            "bootstrap": [fingerprint(root, root / name) for name in (
                "examples/bash.wat", "build/html-rt/bash-runtime.wast",
                "build/html-rt/waste-wast.wasm", "build/cli-rt/waste-cli")],
            "build_rules": [fingerprint(root, root / name) for name in (
                "start.sh", "src/engine/Makefile", "src/cli-rt/Makefile",
                "src/html-rt/Makefile", "src/html-rt/tools/build.sh")],
            "package_files": [{**fingerprint(root, root / source),
                "guest_path": guest, "destination": "src/vfs" + guest,
                "guest_mode": "0o644"} for source, guest in (
                ("build/coreutils/provenance.json",
                 "/usr/share/waste/coreutils-provenance.json"),
                ("build/coreutils/coreutils-source-package.json",
                 "/usr/share/waste/coreutils-source-package.json"),
                ("submodules/coreutils/COPYING",
                 "/usr/share/licenses/coreutils/COPYING"))],
            "test_support": [fingerprint(root, path) for directory in (
                "tests/diy-posix-test", "tests/libc-test")
                for path in sorted((root / directory).rglob("*"))
                if path.is_file() and path.suffix != ".wast"],
            "installed_vfs": installed_vfs,
            "bash_scenarios": [list(args) for args in BASH_SCENARIOS]}


def measure(root: Path, output: Path, report: dict, selection: str | None) -> None:
    results_dir = output.parent / "results"
    results_dir.mkdir(parents=True, exist_ok=True)
    runs = report.get("runs", []) if selection else []
    commands = [("native-core-" + Path(test["source"]).stem,
                 [str(root / "build/cli-rt/waste-cli"), test["source"]], 60)
                for test in report["corpus"] if test["group"] == "core"]
    commands += [("bash-" + str(index),
                  ["node", "tests/c-engine-bash-browser-runtime.cjs",
                   "build/html-rt/bash.html", *args], 45)
                 for index, args in enumerate(BASH_SCENARIOS)]
    vfs = json.loads((root / "src/vfs/.inventory.json").read_text())
    date_mtime = next(e["mtime_sec"] for e in vfs["entries"] if e["path"] == "/usr/bin/date")
    root_mtime = next(e["mtime_sec"] for e in vfs["entries"] if e["path"] == "/")
    engine = root / "build/html-rt/waste-wast.wasm"
    for name, guest_command, expected, count in (
            ("clock", "/bin/date -u +%Y", str(datetime.datetime.now(
                datetime.timezone.utc).year), 1),
            ("file-mtime", "/bin/ls -l /bin/date", datetime.datetime.fromtimestamp(
                date_mtime, datetime.timezone.utc).strftime("%b %e %H:%M"), 1),
            ("build-mtime", "/bin/ls -ld / /bin /bin/wat /bin/wast",
             datetime.datetime.fromtimestamp(root_mtime,
                datetime.timezone.utc).strftime("%b %e %H:%M"), 4)):
        commands.append(("bash-" + name, ["env",
            "WASTE_COREUTILS_LS_COMMAND=" + guest_command,
            "WASTE_COREUTILS_LS_EXPECT=" + expected,
            "WASTE_COREUTILS_LS_EXPECT_COUNT=" + str(count),
            "node", "tests/c-engine-bash-browser-runtime.cjs",
            "build/html-rt/bash.html",
            "--coreutils-ls", "--full-package"], 45))
    commands += [("browser-workers",
                  ["node", "tests/c-engine-browser-runtime.cjs"], 300),
                 ("terminal-model", ["node", "tests/c-engine-terminal-model.cjs"], 30),
                 ("terminal-glf", ["node", "tests/c-engine-terminal-glf.cjs"], 30)]
    for name, command, timeout in commands:
        if selection and selection not in name:
            continue
        start = time.monotonic()
        log = results_dir / (name + ".log")
        stderr = results_dir / (name + ".stderr.log")
        with log.open("w") as stream, stderr.open("w") as error_stream:
            try:
                result = subprocess.run(command, cwd=root, stdout=stream,
                                        stderr=error_stream, timeout=timeout)
                status = result.returncode
            except subprocess.TimeoutExpired:
                status = "timeout"
        record = {"name": name, "command": command, "status": status,
                  "seconds": round(time.monotonic() - start, 3),
                  "log": str(log.relative_to(root)),
                  "stderr_log": str(stderr.relative_to(root))}
        if name.startswith("native-core-"):
            try:
                native = json.loads(log.read_text())
                record.update(passed=native["passed"], total=native["total"])
            except (ValueError, KeyError):
                record["result_parse_error"] = True
        runs = [run for run in runs if run["name"] != name] + [record]
        print(f"{name}: {status} ({record['seconds']}s)", flush=True)
        report["runs"] = runs
        output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--measure", action="store_true")
    parser.add_argument("--measure-filter", help="Recheck matching run names, retaining other results")
    args = parser.parse_args()
    root = args.repo_root.resolve()
    output = args.output.resolve()
    if not output.is_relative_to(root / "build"):
        parser.error("inventory output must be under repository build/")
    output.parent.mkdir(parents=True, exist_ok=True)
    report = collect(root)
    if args.measure_filter:
        if not args.measure:
            parser.error("--measure-filter requires --measure")
        if output.is_file():
            previous = json.loads(output.read_text())
            if previous.get("revision") != report["revision"]:
                parser.error("cannot retain measurements from another repository revision")
            if previous.get("bootstrap") != report["bootstrap"]:
                parser.error("cannot retain measurements after bootstrap artifacts change")
            for key in ("frontend", "headers", "corpus", "payload", "test_support", "installed_vfs"):
                if previous.get(key) != report[key]:
                    parser.error(f"cannot retain measurements after {key} changes")
            report["runs"] = previous.get("runs", [])
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report["summary"], sort_keys=True), flush=True)
    if report["destination_collisions"]:
        raise RuntimeError(f"destination collisions: {report['destination_collisions']}")
    if args.measure:
        measure(root, output, report, args.measure_filter)


if __name__ == "__main__":
    main()
