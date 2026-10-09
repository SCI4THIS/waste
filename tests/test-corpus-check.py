#!/usr/bin/env python3
"""Snapshot/policy/provenance and fail-before-publish tests for the mounted corpus."""
import copy
import json
from pathlib import Path
import shutil
import sys
import tempfile

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "src/html-rt/tools"))
import test_distribution as corpus
from test_corpus import collect_layout_tests, layout_roots
import vfs


def main():
    inventory = vfs.load(vfs.ROOT)
    declared = vfs.audit_tree(vfs.ROOT, inventory)
    manifest = json.loads(vfs.local(vfs.ROOT, corpus.MANIFEST).read_text())
    corpus.verify_sources(REPO, manifest)
    peer = [t for root, suite, prefix in layout_roots(REPO)
            for t in collect_layout_tests(root, suite, prefix)]
    assert {t["relative"] for t in peer} == {t["id"] for t in manifest["tests"]}
    by_id = {t["id"]: t for t in manifest["tests"]}
    for test in peer:
        snapshot = by_id[test["relative"]]
        expected_path = corpus.TEST_ROOT + "/" + test["mountRelative"]
        assert snapshot["path"] == expected_path
        assert test["path"].read_bytes() == vfs.local(vfs.ROOT, expected_path).read_bytes()
        for key in ("suite", "group", "expectFailure", "unsupported"):
            assert test[key] == snapshot[key]
    read = lambda path: vfs.local(vfs.ROOT, path).read_bytes()

    def copy_vfs(destination):
        shutil.copytree(vfs.ROOT, destination, symlinks=True)
        for path, target in vfs.BLESSED_SYMLINKS.items():
            link = destination / path.lstrip("/")
            link.unlink()
            link.symlink_to((REPO / target).resolve())

    for mutation in (
        lambda m: m["tests"].append(copy.deepcopy(m["tests"][0])),
        lambda m: m["tests"].pop(),
        lambda m: m["counts"].update(supported=m["counts"]["supported"] + 1),
        lambda m: m["files"].pop(),
        lambda m: next(t for t in m["tests"] if t["assets"])["assets"].pop(),
        lambda m: m["tests"][0]["executionSpec"].update(mode="browser-native"),
    ):
        candidate = copy.deepcopy(manifest)
        mutation(candidate)
        try:
            corpus.audit(candidate, read, declared)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid corpus metadata accepted")
    with tempfile.TemporaryDirectory(prefix="corpus-check-", dir=REPO / "build/engine") as temporary:
        root = Path(temporary) / "vfs"
        copy_vfs(root)
        sample_path = next(e["path"] for e in manifest["files"] if e["role"] == "test-runner")
        sample = root / sample_path.lstrip("/")
        original = sample.read_bytes()
        sample.write_bytes(b"edited installed test")
        # Explicit fixture installation may replace an edited snapshot.
        vfs.install_tests(root, vfs.ROOT, manifest)
        assert sample.read_bytes() == original
        before = vfs.load(root)
        # Failure after candidate staging also cannot publish a partial tree.
        invalid = copy.deepcopy(manifest)
        invalid["files"][0]["sha256"] = "0" * 64
        # Write candidate metadata only into the owned scratch tree, not VFS.
        source = Path(temporary) / "source"
        copy_vfs(source)
        (source / corpus.MANIFEST.lstrip("/")).write_bytes(corpus.encoded(invalid))
        try:
            vfs.install_tests(root, source, invalid)
        except ValueError:
            pass
        else:
            raise AssertionError("partial staged corpus published")
        assert vfs.load(root) == before and sample.read_bytes() == original
        vfs.install_tests(root, vfs.ROOT, manifest)
        after = vfs.load(root)
        assert {e["path"] for e in after["entries"]} == {e["path"] for e in inventory["entries"]}
        vfs.install(root, component=["echo"], source=[vfs.ROOT / "usr/bin/echo"])
        refreshed = vfs.load(root)
        corpus.audit(manifest, lambda path: vfs.local(root, path).read_bytes(), vfs.audit_tree(root, refreshed))
    print("PASS test corpus: sources, shared C/OCaml identities, exact bytes/policy/assets")
    print("PASS test corpus guards: missing/duplicate/stale candidates, explicit replacement of edits, atomic failure/refresh")


if __name__ == "__main__":
    main()
