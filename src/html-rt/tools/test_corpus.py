"""Shared installed-test identities and selection; no execution policy rewrite."""
from pathlib import Path


def layout_roots(repo):
    return [(repo / "submodules/wasm-spec/test", "wasm-spec", ""),
            (repo / "tests/diy-posix-test", "diy-posix-test", "diy-posix-test"),
            (repo / "build/html-rt/waste-libc/tests", "libc-test", "libc-test"),
            (repo / "tests/engine-regressions", "engine-regressions", "engine-regressions")]


def collect_layout_tests(test_root: Path, suite: str, path_prefix: str = "", evaluator="engine"):
    tests = []
    for path in sorted(test_root.rglob("*.wast")):
        relative_path = path.relative_to(test_root)
        if "_output" in relative_path.parts:
            continue
        local_relative = relative_path.as_posix()
        relative = "/".join(part for part in (path_prefix, local_relative) if part)
        parent = path.parent.relative_to(test_root).as_posix()
        group = "/".join(part for part in (path_prefix, parent if parent != "." else "") if part) or "root"
        unsupported = suite == "wasm-spec" and local_relative.startswith("legacy/")
        tests.append(dict(path=path, relative=relative, group=group, suite=suite,
                          expectFailure=".fail." in path.name, unsupported=unsupported,
                          unsupportedReason=("Legacy exception syntax is not supported by the current "
                                             f"WebAssembly 3.0 {evaluator}" if unsupported else None)))
    return tests
