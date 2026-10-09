"""Shared installed-test identities and selection; no execution policy rewrite."""
import sys
from pathlib import Path


def browser_bash_snapshot(suite, relative):
    """Return the browser-only installed path for tests excluded from CLI batches."""
    if suite == "system" and relative == "cli-runtime/wast/unlinkable.wast":
        return None
    if suite == "diy-posix-test" and relative == "posix-kernel.wast":
        return "html-rt/posix-kernel.wast"
    if suite == "render" and relative == "terminal.wast":
        return "html-rt/terminal.wast"
    if suite == "system" and (
            relative in {f"cli-runtime/wast/{name}.wast" for name in
                         ("input", "language", "output", "providers", "runtime")} or
            relative in {f"wast/{name}.wast" for name in ("pass", "report-files", "report.fail")}):
        return "html-rt/" + Path(relative).name
    return None


def layout_roots(repo):
    roots = [(repo / "submodules/wasm-spec/test", "wasm-spec", "wasm-spec"),
             (repo / "tests/diy-posix-test", "diy-posix-test", "diy-posix-test"),
             (repo / "tests/engine-regressions", "engine-regressions", "engine-regressions"),
             (repo / "tests/render", "render", "render")]
    roots.extend((package / "tests", "aux", "aux/" + package.name)
                 for package in sorted((repo / "src/aux").iterdir())
                 if (package / "tests").is_dir())
    roots.append((repo / "src/system-tests", "system", "system"))
    return roots


def collect_layout_tests(test_root: Path, suite: str, path_prefix: str = "", evaluator="engine"):
    if suite == "wasm-spec" and not test_root.is_dir():
        print(f"INFO: wasm-spec submodule not checked out at {test_root}; "
              "spec tests will not be mounted into the VFS.", file=sys.stderr)
        return []
    tests = []
    for path in sorted(test_root.rglob("*.wast")):
        relative_path = path.relative_to(test_root)
        if "_output" in relative_path.parts:
            continue
        local_relative = relative_path.as_posix()
        if suite == "wasm-spec" and local_relative.startswith("legacy/"):
            continue
        relative = "/".join(part for part in (path_prefix, local_relative) if part)
        browser_path = browser_bash_snapshot(suite, local_relative)
        parent = path.parent.relative_to(test_root).as_posix()
        group = "/".join(part for part in (path_prefix, parent if parent != "." else "") if part) or "root"
        unsupported = suite in ("render", "aux", "system")
        tests.append(dict(path=path, relative=relative, mountRelative=browser_path or relative, group=group, suite=suite,
                          expectFailure=".fail." in path.name, unsupported=unsupported,
                          unsupportedReason=("Requires Bash process context; run /bin/wast --verbose" if suite in ("aux", "system") else
                                             "Requires the interactive bash.html renderer; run with /bin/wast" if suite == "render" else None)))
    return tests
