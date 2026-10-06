"""Prepare and audit installed browser-test snapshots, without executing tests."""
import base64
from collections import Counter
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess

from test_corpus import collect_layout_tests, layout_roots
from libc_sources import libc_source_paths

MANIFEST = "/tests/manifest.json"
LICENSE = "/usr/share/licenses/wasm-spec-tests/LICENSE"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def spec_sha(spec):
    # Same serialization as the retained Stage 1 inventory.
    return sha(json.dumps(spec, sort_keys=True).encode())


def entries(repo):
    result = []
    for root, suite, prefix in layout_roots(repo):
        if not root.is_dir():
            raise ValueError(f"missing test source directory: {root}")
        result.extend(collect_layout_tests(root, suite, prefix))
    return result


def selection(tests):
    return [{k: t[k] for k in ("id", "group", "suite", "expectFailure", "unsupported", "unsupportedReason")}
            | {"mode": t["executionSpec"]["mode"]} for t in tests]


def prepare(repo, output, payload_path, baseline=None):
    source_entries = entries(repo)
    payload = json.loads(payload_path.read_text())["tests"]
    indexed = {t["path"]: t for t in payload}
    if len(indexed) != len(payload) or set(indexed) != {e["relative"] for e in source_entries}:
        raise ValueError("worker payload/corpus identities differ or duplicate")
    tests, files = [], {}
    previous = {}
    repairs = []
    if baseline:
        previous = {t["id"]: t for t in json.loads(baseline.read_text())["corpus"]}
    elif (repo / "src/vfs/tests/manifest.json").is_file():
        installed = json.loads((repo / "src/vfs/tests/manifest.json").read_text())
        previous = {t["id"]: dict(sha256=t["source"]["sha256"], packaged_spec=t["executionSpec"])
                    for t in installed["tests"]}
        repairs = installed.get("baselineRepairs", [])

    def add(path, data, role, source):
        if path in files:
            raise ValueError(f"conflicting test snapshot: {path}")
        dest = output / path.lstrip("/")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        dest.chmod(0o644)
        files[path] = dict(path=path, size=len(data), sha256=sha(data), role=role, source=source)

    features = {"bulk-memory": "bulk-memory", "exceptions": "exceptions", "gc": "gc",
                "memory64": "memory64", "multi-memory": "multi-memory", "relaxed-simd": "relaxed-simd",
                "simd": "simd", "custom": "custom-annotations", "legacy": "legacy-exceptions"}
    for entry in source_entries:
        identity = entry["relative"]
        supplied = indexed[identity]
        for field in ("group", "suite", "expectFailure", "unsupported", "unsupportedReason"):
            if supplied[field] != entry[field]:
                raise ValueError(f"stale installed-test policy: {identity}: {field}")
        source = entry["path"].relative_to(repo).as_posix()
        if entry["path"].is_symlink() or not entry["path"].resolve().is_relative_to(repo):
            raise ValueError(f"escaping or symlinked test source: {identity}")
        data = entry["path"].read_bytes()
        spec = dict(supplied["spec"])
        retained = previous.get(identity, {})
        if retained.get("sha256") == sha(data) and "assertionCount" in retained.get("packaged_spec", {}):
            # Preserve previously parsed counts only for exactly unchanged
            # source bytes. Execution results, never lexical guesses, remain
            # authoritative; a changed source keeps the generator's unknown 0.
            spec["assertionCount"] = retained["packaged_spec"]["assertionCount"]
        if spec["mode"] == "wast-stream":
            if spec.get("sourceBytes") != len(data):
                raise ValueError(f"stale installed-test source length: {identity}")
            if "wastText" in spec and spec["wastText"].encode() != data:
                raise ValueError(f"stale embedded WAST: {identity}")
            if "sourcePath" in spec and (repo / spec["sourcePath"]).resolve() != entry["path"].resolve():
                raise ValueError(f"wrong installed-test source: {identity}")
        elif spec["mode"] != "browser-native" or spec.get("error"):
            raise ValueError(f"unusable browser execution specification: {identity}")
        path = "/tests/" + identity
        add(path, data, "test-input", source)
        assets = []
        for index, module in enumerate(spec.get("modules", [])):
            binary = base64.b64decode(module["wasmB64"], validate=True)
            if binary[:8] != b"\0asm\x01\0\0\0":
                raise ValueError(f"invalid DIY Wasm asset: {identity}")
            asset = f"/tests/.support/{identity[:-5]}/module-{index}.wasm"
            add(asset, binary, "test-support", "assembled:" + source)
            assets.append(dict(kind="module", index=index, path=asset, moduleId=module["id"]))
        for index, file in enumerate(spec.get("vfsFiles", [])):
            binary = base64.b64decode(file["dataB64"], validate=True)
            asset = f"/tests/.support/{identity[:-5]}/file-{index}.bin"
            add(asset, binary, "test-support", "fixture:" + source)
            assets.append(dict(kind="vfs-file", index=index, path=asset, mountPath=file["path"], mode=file["mode"]))
        tests.append(dict(id=identity, path=path, name=entry["path"].name,
                          **{k: entry[k] for k in ("group", "suite", "expectFailure", "unsupported", "unsupportedReason")},
                          source=dict(path=source, size=len(data), sha256=sha(data)),
                          executionSpec=spec, spec_sha256=spec_sha(spec), assets=assets,
                          requiredFeatures=sorted({features[p] for p in PurePosixPath(identity).parts[:-1] if p in features}),
                          runtime="c-browser-compat" if spec["mode"] == "browser-native" else "c-wast-stream",
                          nativeParity=("shared-engine" if entry["suite"] in ("wasm-spec", "engine-regressions")
                                        or (entry["suite"] == "diy-posix-test" and spec["mode"] == "wast-stream")
                                        else "pending-stage-6")))
    if baseline:
        old = previous
        if set(old) != {t["id"] for t in tests}:
            raise ValueError("test selection differs from retained baseline")
        for t in tests:
            previous = old[t["id"]]
            expected_spec = dict(previous["packaged_spec"])
            if (expected_spec.get("sourceBytes", previous["size"]) != previous["size"]
                    and previous["sha256"] == t["source"]["sha256"]):
                repairs.append(dict(id=t["id"], field="sourceBytes", recorded=expected_spec["sourceBytes"],
                                    installed=previous["size"], reason="Stale staging length; retained source hash unchanged"))
                expected_spec["sourceBytes"] = previous["size"]
            if (previous["sha256"] != t["source"]["sha256"] or spec_sha(expected_spec) != t["spec_sha256"]
                    or previous["group"] != t["group"] or previous["suite"] != t["suite"]
                    or previous["expect_failure"] != t["expectFailure"] or previous["unsupported"] != t["unsupported"]):
                raise ValueError(f"test bytes/policy differ from retained baseline: {t['id']}")
    inputs = []
    for directory in ("tests/diy-posix-test", "tests/libc-test"):
        for path in sorted((repo / directory).rglob("*")):
            if not path.is_file() or path.suffix == ".wast":
                continue
            relative = path.relative_to(repo).as_posix()
            data = path.read_bytes()
            inputs.append(dict(path=relative, size=len(data), sha256=sha(data), hostOnly=not path.name.endswith(".wast.inc")))
            if path.name.endswith(".wast.inc"):
                add("/tests/.support/libc-test/" + path.name, data, "test-support", relative)
    for path in (repo / "src/html-rt/lib/stdlib.wat", repo / "build/html-rt/waste-libc/waste-libc.wasm",
                 repo / "src/vfs/usr/share/waste/sdk.json", repo / "src/html-rt/tools/build-waste-libc.py",
                 repo / "src/html-rt/lib/include/helper.h", repo / "submodules/wasm-spec-i31-int32.patch",
                 *libc_source_paths(repo)):
        data = path.read_bytes()
        inputs.append(dict(path=path.relative_to(repo).as_posix(), size=len(data), sha256=sha(data), hostOnly=True))
    license_source = "submodules/wasm-spec/test/LICENSE"
    add(LICENSE, (repo / license_source).read_bytes(), "notice", license_source)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo / "submodules/wasm-spec", text=True).strip()
    groups = dict(sorted(Counter(t["group"] for t in tests).items()))
    manifest = dict(format=1, status="distribution snapshots; native runtime/harness parity remains Stage 6",
                    oracle=dict(source="submodules/wasm-spec", revision=revision, license=LICENSE,
                                patch="submodules/wasm-spec-i31-int32.patch", patchScope="oracle build only; installed test sources unchanged"),
                    counts=dict(tests=len(tests), supported=sum(not t["unsupported"] for t in tests), groups=groups),
                    baselineRepairs=repairs,
                    selection_sha256=sha(encoded(selection(tests))), tests=tests,
                    files=sorted(files.values(), key=lambda e: e["path"]), sourceInputs=inputs,
                    runtimeProfiles={
                        "c-wast-stream": dict(backend="C engine", timeoutMs=None, timeoutScope="existing dashboard has no per-test deadline"),
                        "c-browser-compat": dict(backend="browser-native WebAssembly with compatibility imports", timeoutMs=None,
                                                 nativeEquivalent=False, note="Do not report these as C-engine or native CLI passes"),
                        "ocaml-oracle": dict(backend="official OCaml interpreter", variants=["direct", "threaded"],
                                             dashboardVariant="threaded", quantum=10000, timeoutMs=1800000,
                                             artifacts="build/ocaml/dist{,-threaded}; host oracle dependencies, not guest programs")})
    (output / MANIFEST.lstrip("/")).write_bytes(encoded(manifest))
    return manifest


def audit(manifest, read, declared):
    """Archive/tree audit, independent of local source trees and build tools."""
    if manifest.get("format") != 1:
        raise ValueError("unsupported test corpus manifest")
    tests, seen, files = manifest["tests"], set(), {}
    for entry in manifest["files"]:
        path = entry["path"]
        if path in files or path not in declared or not (path.startswith("/tests/") or path == LICENSE):
            raise ValueError(f"conflicting or omitted test asset: {path}")
        data = read(path)
        if len(data) != entry["size"] or sha(data) != entry["sha256"]:
            raise ValueError(f"modified test distribution snapshot: {path}")
        files[path] = entry
    if LICENSE not in files or manifest["oracle"]["license"] != LICENSE:
        raise ValueError("missing test corpus license/provenance")
    for test in tests:
        identity = test["id"]
        if identity in seen or test["path"] != "/tests/" + identity or test["path"] not in files:
            raise ValueError(f"conflicting or omitted corpus identity: {identity}")
        seen.add(identity)
        if files[test["path"]]["role"] != "test-input" or declared[test["path"]]["mode"] != 0o644:
            raise ValueError(f"invalid installed test role/mode: {identity}")
        if (test["source"]["sha256"] != files[test["path"]]["sha256"] or
                test["source"]["size"] != files[test["path"]]["size"] or
                test["spec_sha256"] != spec_sha(test["executionSpec"])):
            raise ValueError(f"test source/execution metadata mismatch: {identity}")
        spec = test["executionSpec"]
        if spec["mode"] not in ("wast-stream", "browser-native") or test["runtime"] != (
                "c-browser-compat" if spec["mode"] == "browser-native" else "c-wast-stream"):
            raise ValueError(f"test backend metadata differs: {identity}")
        if spec["mode"] == "wast-stream":
            if spec["sourceBytes"] != test["source"]["size"] or (
                    "wastText" in spec and spec["wastText"].encode() != read(test["path"])):
                raise ValueError(f"embedded test source differs: {identity}")
        expected_assets = {("module", i) for i in range(len(spec.get("modules", [])))} | {
            ("vfs-file", i) for i in range(len(spec.get("vfsFiles", [])))}
        if len(test["assets"]) != len(expected_assets) or {(a["kind"], a["index"]) for a in test["assets"]} != expected_assets:
            raise ValueError(f"missing or duplicate test companion reference: {identity}")
        for asset in test["assets"]:
            if asset["path"] not in files:
                raise ValueError(f"missing test companion: {identity}")
            data = read(asset["path"])
            original = (spec["modules"][asset["index"]]["wasmB64"] if asset["kind"] == "module"
                        else spec["vfsFiles"][asset["index"]]["dataB64"])
            if data != base64.b64decode(original, validate=True):
                raise ValueError(f"test companion bytes differ: {identity}")
    actual = dict(tests=len(tests), supported=sum(not t["unsupported"] for t in tests),
                  groups=dict(sorted(Counter(t["group"] for t in tests).items())))
    if actual != manifest["counts"] or sha(encoded(selection(tests))) != manifest["selection_sha256"]:
        raise ValueError("test selection/count/policy metadata differs")
    if {p for p, e in declared.items() if e["kind"] == 1 and p.startswith("/tests/")} != set(files) - {LICENSE} | {MANIFEST}:
        raise ValueError("unlisted or omitted test corpus files")
    return manifest


def verify_sources(repo, manifest):
    selected = {e["relative"]: e for e in entries(repo)}
    if set(selected) != {t["id"] for t in manifest["tests"]}:
        raise ValueError("installed test selection is stale")
    for test in manifest["tests"]:
        entry = selected[test["id"]]
        if sha(entry["path"].read_bytes()) != test["source"]["sha256"]:
            raise ValueError(f"installed test source is stale: {test['id']}")
        for field in ("group", "suite", "expectFailure", "unsupported", "unsupportedReason"):
            if test[field] != entry[field]:
                raise ValueError(f"installed test policy is stale: {test['id']}")
    for entry in manifest["sourceInputs"]:
        if sha((repo / entry["path"]).read_bytes()) != entry["sha256"]:
            raise ValueError(f"installed test generation input is stale: {entry['path']}")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo / "submodules/wasm-spec", text=True).strip()
    if manifest["oracle"]["revision"] != revision:
        raise ValueError("installed test oracle revision is stale")
