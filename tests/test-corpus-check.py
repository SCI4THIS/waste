#!/usr/bin/env python3
"""Snapshot/policy/provenance and fail-before-publish tests for the mounted corpus."""
import copy
import json
from pathlib import Path
import re
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
        assert test["path"].read_bytes() == vfs.local(vfs.ROOT, snapshot["path"]).read_bytes()
        for key in ("suite", "group", "expectFailure", "unsupported"):
            assert test[key] == snapshot[key]
    assert manifest["counts"]["tests"] == 296 and manifest["counts"]["supported"] == 292
    assert {t["id"] for t in manifest["tests"] if t["group"] == "engine-regressions"} == {
        "engine-regressions/shared-memory.wast", "engine-regressions/directory-umask.wast", "engine-regressions/descriptor-flags.wast", "engine-regressions/i32-smoke.wast", "engine-regressions/extern-aliases.wast",
        "engine-regressions/instance-isolation.wast", "engine-regressions/caller-memory.wast",
        "engine-regressions/continuation-waits.wast", "engine-regressions/path-vfs.wast", "engine-regressions/pipe-descriptors.wast", "engine-regressions/signal-masks.wast", "engine-regressions/select-polling.wast"}
    assert {t["id"] for t in manifest["tests"] if t["runtime"] == "c-browser-compat"} == {
        "diy-posix-test/posix-kernel.wast"}
    assert manifest["runtimeProfiles"]["ocaml-oracle"]["variants"] == ["direct", "threaded"]
    assert len(declared) < vfs.MAX_ENTRIES
    bound = int(re.search(r'WASTE_VFS_MAX_ENTRIES (\d+)u', (REPO / "src/engine/vfs.h").read_text())[1])
    capacity = int(re.search(r'POSIX_PATH_NODE_MAX (\d+)', (REPO / "src/engine/lib/include/path.h").read_text())[1])
    assert bound == vfs.MAX_ENTRIES and capacity - bound == 64
    read = lambda path: vfs.local(vfs.ROOT, path).read_bytes()
    for mutation in (
        lambda m: m["tests"].append(copy.deepcopy(m["tests"][0])),
        lambda m: m["tests"].pop(),
        lambda m: m["counts"].update(supported=296),
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
        shutil.copytree(vfs.ROOT, root)
        before = (root / vfs.MANIFEST).read_bytes()
        sample = root / manifest["tests"][0]["path"].lstrip("/")
        original = sample.read_bytes()
        sample.write_bytes(b"edited installed test")
        try:
            vfs.install_tests(root, vfs.ROOT, manifest)
        except ValueError as error:
            assert "modified or partial installed file" in str(error)
        else:
            raise AssertionError("edited test snapshot was overwritten")
        assert (root / vfs.MANIFEST).read_bytes() == before and sample.read_bytes() == b"edited installed test"
        sample.write_bytes(original)
        invalid = copy.deepcopy(manifest)
        invalid["selection_sha256"] = "0" * 64
        try:
            vfs.install_tests(root, vfs.ROOT, invalid)
        except ValueError as error:
            assert "selection changed" in str(error)
        else:
            raise AssertionError("unreviewed selection refresh accepted")
        assert (root / vfs.MANIFEST).read_bytes() == before
        # Failure after candidate staging also cannot publish a partial tree.
        invalid = copy.deepcopy(manifest)
        invalid["files"][0]["sha256"] = "0" * 64
        # Write candidate metadata only into the owned scratch tree, not VFS.
        source = Path(temporary) / "source"
        shutil.copytree(vfs.ROOT, source)
        (source / corpus.MANIFEST.lstrip("/")).write_bytes(corpus.encoded(invalid))
        try:
            vfs.install_tests(root, source, invalid)
        except ValueError:
            pass
        else:
            raise AssertionError("partial staged corpus published")
        assert (root / vfs.MANIFEST).read_bytes() == before and sample.read_bytes() == original
        vfs.install_tests(root, vfs.ROOT, manifest)
        after = vfs.load(root)
        assert after == inventory, "unchanged refresh altered paths/inodes/metadata"
        vfs.install(root, component=["echo"], source=[vfs.ROOT / "usr/bin/echo"])
        refreshed = vfs.load(root)
        assert refreshed["test_corpus"] == inventory["test_corpus"]
        corpus.audit(manifest, lambda path: vfs.local(root, path).read_bytes(), vfs.audit_tree(root, refreshed))
    print("PASS test corpus: 296 sources, 292 supported, shared C/OCaml identities, exact bytes/policy/assets")
    print("PASS test corpus guards: missing/duplicate/stale inputs, edited snapshots, atomic failure/refresh, capacity agreement")


if __name__ == "__main__":
    main()
