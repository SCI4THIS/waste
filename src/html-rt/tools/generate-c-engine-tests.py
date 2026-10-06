#!/usr/bin/env python3
"""Generate payload metadata for C-engine WAST worker conformance tests.

Production browser batches run from bash.html. This payload feeds focused Node
worker tests without generating a dashboard. Audited DIY fixtures use shared
WAST command streams; the legacy kernel fixture retains browser-native
compatibility for that host-boundary probe.
"""

import argparse
import base64
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from test_corpus import collect_layout_tests, layout_roots


def tokenize_wast(source: str) -> list[str]:
    """Tokenize the small, ordinary-WAT subset used by repository DIY probes."""
    tokens = []
    i = 0
    while i < len(source):
        if source.startswith(";;", i):
            i = source.find("\n", i)
            if i < 0:
                break
        elif source.startswith("(;", i):
            depth = 1
            i += 2
            while depth and i < len(source):
                if source.startswith("(;", i): depth += 1; i += 2
                elif source.startswith(";)", i): depth -= 1; i += 2
                else: i += 1
        elif source[i].isspace():
            i += 1
        elif source[i] in "()":
            tokens.append(source[i]); i += 1
        elif source[i] == '"':
            start = i
            i += 1
            while i < len(source):
                if source[i] == "\\": i += 2
                elif source[i] == '"': i += 1; break
                else: i += 1
            tokens.append(source[start:i])
        else:
            start = i
            while i < len(source) and not source[i].isspace() and source[i] not in "()":
                i += 1
            tokens.append(source[start:i])
    return tokens


def parse_wast_forms(source: str):
    tokens = tokenize_wast(source)
    pos = 0
    def one():
        nonlocal pos
        if pos >= len(tokens): raise ValueError("unexpected end of WAST")
        token = tokens[pos]; pos += 1
        if token != "(": return token
        result = []
        while pos < len(tokens) and tokens[pos] != ")": result.append(one())
        if pos >= len(tokens): raise ValueError("unterminated WAST form")
        pos += 1
        return result
    forms = []
    while pos < len(tokens): forms.append(one())
    return forms


def wat_text(node) -> str:
    if isinstance(node, str): return node
    return "(" + " ".join(wat_text(item) for item in node) + ")"


def const_spec(node) -> dict:
    if not isinstance(node, list) or len(node) != 2 or node[0] not in ("i32.const", "i64.const"):
        raise ValueError(f"unsupported DIY constant: {wat_text(node)}")
    return {"type": node[0][:3], "value": int(node[1], 0)}


def invoke_spec(node) -> dict:
    if not isinstance(node, list) or not node or node[0] != "invoke":
        raise ValueError(f"unsupported DIY action: {wat_text(node)}")
    at = 1
    module = None
    if at < len(node) and isinstance(node[at], str) and node[at].startswith("$"):
        module = node[at][1:]; at += 1
    name = node[at].strip('"'); at += 1
    return {"module": module, "func": name, "args": [const_spec(x) for x in node[at:]]}


def run_wasm_as(module, wasm_as: str) -> str:
    # POSIX imports need access to the module's otherwise-private memory.
    if any(isinstance(x, list) and x[:2] == ["memory", "1"] for x in module[1:]):
        module.append(["export", '"__waste_memory"', ["memory", "0"]])
    with tempfile.TemporaryDirectory(prefix="waste-c-engine-") as temp:
        wat = Path(temp) / "module.wat"
        wasm = Path(temp) / "module.wasm"
        wat.write_text(wat_text(module), encoding="utf-8")
        result = subprocess.run([
            wasm_as, "--enable-bulk-memory", "--enable-bulk-memory-opt",
            "--enable-reference-types", str(wat), "-o", str(wasm)
        ], capture_output=True, text=True)
        if result.returncode:
            raise ValueError(result.stderr.strip() or "wasm-as failed")
        return base64.b64encode(wasm.read_bytes()).decode("ascii")


def build_diy_spec(wast_file: Path, wasm_as: str) -> dict:
    """Use shared WAST execution for audited DIY fixtures; retain compatibility."""
    if wast_file.name in {"bulk-operations.wast", "spectest-isolation-a.wast",
                          "spectest-isolation-b.wast"}:
        wast_text = wast_file.read_text(encoding="utf-8")
        return {
            "file": wast_file.name,
            "mode": "wast-stream",
            "wastText": wast_text,
            "sourceBytes": len(wast_text.encode("utf-8")),
            "assertionCount": 0,
        }
    # Keep the VM ABI fixture on the C-engine stream path.  Unlike the older
    # compatibility fixtures below, this test must resolve env.mmap/munmap/
    # mprotect through browser_host_resolver so the browser worker exercises the
    # engine-owned process capsule rather than a JavaScript substitute.
    if wast_file.name == "mmap.wast":
        wast_text = wast_file.read_text(encoding="utf-8")
        wast_bytes = wast_text.encode("utf-8")
        file_bytes = bytes((109, 97, 112, 33)) + bytes(65536 - 4)
        return {
            "file": wast_file.name,
            "mode": "wast-stream",
            "wastText": wast_text,
            "sourceBytes": len(wast_bytes),
            "assertionCount": 0,
            "vfsFiles": [{
                "path": "/mmap-file",
                "mode": 0o666,
                "dataB64": base64.b64encode(file_bytes).decode("ascii"),
            }, {
                "path": "/mmap-short",
                "mode": 0o666,
                "dataB64": base64.b64encode(b"tail").decode("ascii"),
            }],
        }
    forms = parse_wast_forms(wast_file.read_text(encoding="utf-8"))
    modules = []
    steps = []
    current = None
    for form in forms:
        if not isinstance(form, list) or not form: continue
        if form[0] == "module":
            module_id = form[1][1:] if len(form) > 1 and isinstance(form[1], str) and form[1].startswith("$") else None
            modules.append({"id": module_id, "wasmB64": run_wasm_as(form, wasm_as)})
            current = module_id
        elif form[0] == "invoke":
            action = invoke_spec(form); action["expect"] = []
            steps.append(action)
        elif form[0] == "assert_return":
            action = invoke_spec(form[1]); action["expect"] = [const_spec(x) for x in form[2:]]
            steps.append(action)
    if not modules or not steps:
        raise ValueError("DIY script produced no modules or actions")
    return {"file": wast_file.name, "mode": "browser-native", "modules": modules, "steps": steps}


def total_assertions(spec: dict) -> int:
    return spec.get("assertionCount", 0)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate C-engine worker-test payload metadata"
    )
    parser.add_argument("--repo-root", type=Path,
                        default=Path(__file__).resolve().parents[3],
                        help="Repository root used for build-path validation")
    parser.add_argument("--runner", type=Path, default=None,
                        help="Path to the waste-wast native binary (needed for --count)")
    parser.add_argument("--wasm", type=Path, required=True,
                        help="Path to waste-wast.wasm (browser C engine)")
    parser.add_argument("--tests", type=Path, action="append", default=[],
                        help="Directory containing .wast test files (repeatable)")
    parser.add_argument("--mounted-corpus", action="store_true",
                        help="Generate the installed manifest corpus for worker conformance")
    parser.add_argument("--output-dir", type=Path, required=True,
                        help="Generated payload directory under repository build/")
    parser.add_argument("--wasm-as", default="wasm-as",
                        help="Binaryen wasm-as used for repository DIY fixtures")
    parser.add_argument("--count", action="store_true",
                        help="Use native runner to count assertions per file")
    args = parser.parse_args()

    if not args.tests and not args.mounted_corpus:
        parser.error("provide --tests or --mounted-corpus")

    root = args.repo_root.resolve()
    if args.output_dir and not args.output_dir.resolve().is_relative_to(root / "build"):
        parser.error("--output-dir must be under repository build/")
    if args.mounted_corpus:
        roots = layout_roots(root)
        missing = [path for path, _, _ in roots if not path.is_dir()]
        if missing:
            print(f"error: mounted test source directory not found: {missing[0]}",
                  file=sys.stderr)
            return 1
        test_entries = [entry for path, suite, prefix in roots
                        for entry in collect_layout_tests(path, suite, prefix)]
        test_dirs = [path for path, _, _ in roots]
    else:
        test_entries = [
            {
                "path": wast_file,
                "relative": f"{test_dir.name}/{wast_file.name}",
                "group": test_dir.name,
                "suite": ("diy-posix-test"
                          if test_dir.name == "diy-posix-test"
                          else "wasm-spec"),
                "expectFailure": ".fail." in wast_file.name,
                "unsupported": False,
                "unsupportedReason": None,
            }
            for test_dir in args.tests
            for wast_file in sorted(test_dir.glob("*.wast"))
        ]
        test_dirs = args.tests

    checks = [
        ("wasm", args.wasm, "file"),
        *((f"tests[{i}]", p, "dir") for i, p in enumerate(test_dirs)),
    ]
    if args.runner:
        checks.insert(0, ("runner", args.runner, "file"))
    for label, p, kind in checks:
        if kind == "file" and not p.is_file():
            print(f"error: {label} not found: {p}", file=sys.stderr)
            return 1
        if kind == "dir" and not p.is_dir():
            print(f"error: {label} directory not found: {p}", file=sys.stderr)
            return 1

    if not test_entries:
        print("error: no .wast files found", file=sys.stderr)
        return 1

    # Optionally count assertions per spec file using the native runner
    assertion_counts = {}
    if args.count and args.runner:
        for entry in test_entries:
            wast_file = entry["path"]
            try:
                result = subprocess.run(
                    [str(args.runner), "--count", str(wast_file)],
                    capture_output=True, text=True, timeout=60,
                )
                if result.returncode == 0:
                    assertion_counts[wast_file] = int(result.stdout.strip())
            except (subprocess.TimeoutExpired, ValueError):
                pass

    print(f"Collecting {len(test_entries)} test files…")
    tests = []
    for entry in test_entries:
        wast_file = entry["path"]
        if entry["suite"] == "diy-posix-test":
            try:
                spec = build_diy_spec(wast_file, args.wasm_as)
                spec["assertionCount"] = (
                    assertion_counts.get(wast_file, 0)
                    if spec["mode"] == "wast-stream"
                    else len(spec.get("steps", [])))
            except (OSError, ValueError) as exc:
                spec = {"file": wast_file.name, "mode": "browser-native",
                        "error": str(exc), "assertionCount": 0}
        else:
            wast_bytes = wast_file.read_bytes()
            n_assert = assertion_counts.get(wast_file, 0)
            source_path = wast_file.resolve()
            source_rel = (source_path.relative_to(root).as_posix()
                          if source_path.is_relative_to(root) else str(source_path))
            spec = {
                "file": entry["relative"],
                "mode": "wast-stream",
                "sourcePath": source_rel,
                "sourceBytes": len(wast_bytes),
                "assertionCount": n_assert,
            }
        n_display = total_assertions(spec) or len(spec.get("steps", []))
        print(f"  {wast_file.name}: {n_display or '?'} checks")
        tests.append({
            "path": entry["relative"],
            "name": wast_file.name,
            "file": entry["relative"],
            "group": entry["group"],
            "suite": entry["suite"],
            "expectFailure": entry["expectFailure"],
            "unsupported": entry["unsupported"],
            "unsupportedReason": entry["unsupportedReason"],
            "spec": spec,
        })

    total_files = len(tests)
    supported_files = sum(not test["unsupported"] for test in tests)
    total_a = sum(total_assertions(t["spec"]) for t in tests)

    def write_payload(out_dir: Path) -> Path:
        out_dir.mkdir(parents=True, exist_ok=True)

        # Write payload.json — WAST files are referenced by sourcePath and
        # loaded directly from their original locations (submodule, tests/).
        payload = {"tests": tests}
        payload_path = out_dir / "payload.json"
        payload_path.write_text(
            json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        wast_count = sum(1 for t in tests
                         if t["spec"].get("mode") == "wast-stream")
        print(f"Generated {payload_path} ({payload_path.stat().st_size:,} bytes)")
        print(f"{total_files} test files ({supported_files} supported) · "
              f"{total_a} total assertions · {wast_count} wast files")
        return payload_path

    write_payload(args.output_dir)

    return 0


if __name__ == "__main__":
    sys.exit(main())
