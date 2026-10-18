"""Shared installed-test identities and selection; no execution policy rewrite."""
import sys
from pathlib import Path


def browser_bash_snapshot(suite, relative):
    """Return the browser-only installed path for tests excluded from CLI batches."""
    if suite == "system" and relative in {"cli-runtime/unlinkable.wast", "cli-runtime/wast/unlinkable.wast"}:
        return None
    if suite == "diy-posix-test" and relative == "posix-kernel.wast":
        return "html-rt/posix-kernel.wast"
    if suite == "render" and relative == "terminal.wast":
        return "html-rt/terminal.wast"
    if suite == "system" and (
            relative in {f"{prefix}/{name}.wast" for prefix in ("cli-runtime", "cli-runtime/wast")
                         for name in ("input", "language", "output", "providers", "runtime")} or
            relative in {f"wast/{name}.wast" for name in ("pass", "report-files", "report.fail")}):
        return "html-rt/" + Path(relative).name
    return None


def _preferred_root(repo, legacy, canonical):
    """Return the legacy source during transition; fall back to canonical VFS."""
    legacy_path = repo / legacy
    if legacy_path.is_dir():
        return legacy_path
    return repo / canonical


def layout_roots(repo):
    roots = [(repo / "submodules/wasm-spec/test", "wasm-spec", "wasm-spec"),
             (_preferred_root(repo, "tests/diy-posix-test",
                              "src/vfs/root/test/diy-posix-test"),
              "diy-posix-test", "diy-posix-test"),
             (_preferred_root(repo, "tests/engine-regressions",
                              "src/vfs/root/test/engine-regressions"),
              "engine-regressions", "engine-regressions"),
             (_preferred_root(repo, "tests/render",
                              "src/vfs/root/test/render"),
              "render", "render")]
    for package in sorted((repo / "src/aux").iterdir()):
        legacy = package / "tests"
        canonical = repo / "src/vfs/root/test/aux" / package.name
        chosen = legacy if legacy.is_dir() else canonical
        if chosen.is_dir():
            roots.append((chosen, "aux", "aux/" + package.name))
    system_root = repo / "src/vfs/root/test/system"
    if system_root.is_dir():
        roots.append((system_root, "system", "system"))
    binary_handler = repo / "src/cli-rt/test/binary-handler"
    if binary_handler.is_dir():
        roots.append((binary_handler, "system", "system/cli-runtime/binary-handler"))
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
