#!/usr/bin/env python3
"""Explicit distribution installation and inventory-based packaging (no compilation).

The inventory is host-side metadata, never a guest file. Alias copies are
intentional until symlink execution has its own acceptance gate. Compiler
outputs stay in build/; only this named install step publishes snapshots.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import tempfile

import guest_sdk
from runtime_config import read_config, frontend_bytes
import test_distribution

REPO = Path(__file__).resolve().parents[3]
ROOT = REPO / "src/vfs"
MANIFEST = ".inventory.json"
COREUTILS = "true false pwd echo printf basename dirname cat wc ls date".split()
COMMANDS = COREUTILS + ["rogue", "ldd", "upload", "download", "waste-probe"]
OPTIONAL_COMMANDS = ["waste-test"]
BIN_COMMANDS = {"waste-probe", "waste-test"}


def command_path(name):
    return ("/bin/" if name in BIN_COMMANDS else "/usr/bin/") + name
COMPONENTS = COMMANDS + OPTIONAL_COMMANDS + ["libncurses", "libc", "launch", "app"]
APP_FILES = {
    "app.js": "app.js",
    "worker.js": "worker.js",
    "test-suite.js": "test-suite.js",
    "style.css": "style.css",
    "terminal/model.js": "terminal/model.js",
    "terminal/glf.js": "terminal/glf.js",
    "terminal/renderer.js": "terminal/renderer.js",
    "terminal/render-test.js": "terminal/render-test.js",
}
CONFIG = read_config()
MAX_BYTES = CONFIG["VFS_MAX_BYTES"]
MAX_ENTRIES = CONFIG["VFS_MAX_ENTRIES"]
MAX_INVENTORY = CONFIG["VFS_INVENTORY_MAX_BYTES"]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def guest_path(value):
    p = PurePosixPath(value)
    if (not value.startswith("/") or "//" in value or str(p) != value or ".." in p.parts
            or "\0" in value or "\\" in value or any(ord(c) < 32 for c in value)
            or len(value.encode()) >= CONFIG["VFS_PATH_MAX_BYTES"]):
        raise ValueError(f"unsafe guest path: {value!r}")
    return value


def local(root, path):
    guest_path(path)
    result = root / path.lstrip("/")
    if not result.resolve().is_relative_to(root.resolve()):
        raise ValueError(f"escaping host path: {path}")
    if result.is_symlink():
        raise ValueError(f"host symlink is not an installed copy: {path}")
    return result


def wasm_contract(source, library=False):
    # Node compiles but does not instantiate: no guest start code is executed.
    program = r'''
const fs=require('fs'),b=fs.readFileSync(process.argv[1]);
const m=new WebAssembly.Module(b);
console.log(JSON.stringify({imports:WebAssembly.Module.imports(m),exports:WebAssembly.Module.exports(m)}));
'''
    result = subprocess.run(["node", "-e", program, str(source)],
                            capture_output=True, text=True, check=True)
    contract = json.loads(result.stdout)
    names = [i["module"] + ":" + i["name"] + ":" + i["kind"]
             for i in contract["imports"]]
    if any("asyncify" in n.lower() for n in names + [e["name"] for e in contract["exports"]]):
        raise ValueError(f"Asyncify is forbidden: {source}")
    allowed = {"env", "waste_kernel", "libncurses", "libc", "GOT.mem", "GOT.func"}
    if any(i["module"] not in allowed for i in contract["imports"]):
        raise ValueError(f"unsupported import namespace: {source}")
    exports = {e["name"]: e["kind"] for e in contract["exports"]}
    required = (["malloc", "free", "memcpy", "strcpy", "printf", "dlopen", "dlsym", "dlclose"]
                if library == "libc" else ["initscr", "endwin", "wrefresh"] if library else ["_start"])
    if any(exports.get(n) != "function" for n in required):
        raise ValueError(f"missing required function exports {required}: {source}")
    return {"imports": sorted(names), "required_exports": required}


def load(root):
    manifest = json.loads((root / MANIFEST).read_text())
    if manifest.get("version") != 1:
        raise ValueError("unsupported VFS inventory version")
    return manifest


def audit(manifest, read, physical=None, upgrading_app=False):
    if len(json.dumps(manifest, sort_keys=True).encode()) > MAX_INVENTORY:
        raise ValueError("inventory exceeds bounded metadata size")
    seen = {}
    total_bytes = 0
    inodes = set()
    entries = manifest["entries"]
    if len(entries) > MAX_ENTRIES:
        raise ValueError("installed VFS exceeds reserved inode capacity")
    for index, e in enumerate(entries):
        path = guest_path(e["path"])
        if path in seen:
            raise ValueError(f"conflicting destination: {path}")
        if index == 0:
            if path != "/" or e["kind"] != 2:
                raise ValueError("first VFS node must be root directory")
        elif seen.get(str(PurePosixPath(path).parent), {}).get("kind") != 2:
            raise ValueError(f"directory parent must precede child: {path}")
        seen[path] = e
        total_bytes += e["size"]
        if e["size"] < 0 or total_bytes > MAX_BYTES:
            raise ValueError("installed VFS exceeds bounded mount size")
        if e["kind"] not in (1, 2) or e["mode"] & ~0o777:
            raise ValueError(f"invalid kind/mode: {path}")
        if not 0 <= e["mtime_nsec"] < 1000000000 or e["inode"] <= 0 or e["inode"] in inodes:
            raise ValueError(f"invalid metadata: {path}")
        for key, low, high in (("uid", 0, 2**32-1), ("gid", 0, 2**32-1),
                               ("inode", 1, 2**64-1), ("mtime_sec", -(2**63), 2**63-1)):
            if type(e[key]) is not int or not low <= e[key] <= high:
                raise ValueError(f"invalid metadata: {path} ({key})")
        inodes.add(e["inode"])
        if (e["kind"] == 2) != (e["role"] == "directory"):
            raise ValueError(f"invalid node role: {path}")
        if e["role"] not in ("directory", "interpreter"):
            data = read(path)
            if len(data) != e["size"] or digest(data) != e["sha256"]:
                raise ValueError(f"modified or partial installed file: {path}")
        elif e["size"] != 0:
            raise ValueError(f"invalid empty entry: {path}")
    for path, e in seen.items():
        if path != "/" and seen.get(str(PurePosixPath(path).parent), {}).get("kind") != 2:
            raise ValueError(f"missing directory parent: {path}")
        if "alias_of" in e:
            other = seen.get(e["alias_of"], {})
            for field in ("sha256", "size", "mode", "mtime_sec", "mtime_nsec"):
                if e.get(field) != other.get(field):
                    raise ValueError(f"alias differs: {path}")
    required = [command_path(n) for n in COMMANDS] + [
        "/lib/libncurses.so.wasm", "/lib/libc.so.wasm", "/bin/wat", "/bin/wast",
        "/usr/share/waste/launch.wast", "/usr/share/waste/coreutils-provenance.json",
        "/usr/share/waste/coreutils-source-package.json", "/usr/share/licenses/coreutils/COPYING",
        "/usr/share/licenses/ncurses/COPYING", "/usr/share/licenses/rogue/LICENSE.TXT"]
    for path in required:
        if path not in seen:
            raise ValueError(f"missing mandatory VFS path: {path}")
    app_paths = [{prefix + name for name in APP_FILES
                  if not (upgrading_app and name == "terminal/render-test.js")}
                 for prefix in ("/root/waste/app/", "/waste/app/")]
    if not any(paths <= seen.keys() for paths in app_paths):
        raise ValueError("missing mandatory Bash webapp paths under /root/waste/app")
    for n in COMMANDS + [n for n in OPTIONAL_COMMANDS if command_path(n) in seen]:
        canonical = seen.get(command_path(n), {})
        if canonical.get("kind") != 1 or canonical.get("mode") != 0o755:
            raise ValueError(f"invalid executable metadata: {n}")
    for name in ("libncurses", "libc"):
        canonical = f"/lib/{name}.so.wasm"
        if canonical in seen and seen.get(f"/usr/lib/{name}.so.wasm", {}).get("alias_of") != canonical:
            raise ValueError(f"missing library compatibility alias: {name}")
    for path in ("/bin/wat", "/bin/wast"):
        if seen[path]["role"] != "interpreter" or seen[path]["mode"] != 0o755:
            raise ValueError(f"invalid virtual interpreter: {path}")
    if physical is not None:
        expected = {p for p, e in seen.items() if e["role"] != "interpreter" and p != "/"}
        if physical != expected:
            raise ValueError(f"unlisted or omitted VFS nodes: {sorted(physical ^ expected)}")
    corpus_manifest_path = next((path for path in
                                 (test_distribution.MANIFEST, test_distribution.LEGACY_MANIFEST)
                                 if path in seen), None)
    if corpus_manifest_path:
        corpus = test_distribution.audit(json.loads(read(corpus_manifest_path)), read, seen)
        expected = dict(path=corpus_manifest_path, tests=corpus["counts"]["tests"],
                        supported=corpus["counts"]["supported"], selection_sha256=corpus["selection_sha256"])
        if manifest.get("test_corpus") != expected:
            raise ValueError("VFS test corpus metadata differs")
    elif "test_corpus" in manifest or any(
            p.startswith(prefix + "/") for prefix in
            (test_distribution.TEST_ROOT, test_distribution.LEGACY_TEST_ROOT) for p in seen):
        raise ValueError("missing mandatory test corpus manifest")
    return seen


def audit_tree(root, manifest, upgrading_app=False):
    physical = set()
    for p in root.rglob("*"):
        if p == root / MANIFEST:
            continue
        relative = "/" + p.relative_to(root).as_posix()
        local(root, relative)
        physical.add(relative)
    # Git cannot retain empty directories or filesystem mtimes. The inventory
    # is authoritative for those guest values; do not require a checkout to
    # reproduce the installing machine's host timestamps.
    declared_dirs = {e["path"] for e in manifest["entries"] if e["kind"] == 2 and e["path"] != "/"}
    result = audit(manifest, lambda p: local(root, p).read_bytes(), physical | declared_dirs,
                   upgrading_app=upgrading_app)
    for p, e in result.items():
        if e["role"] == "interpreter" or e["kind"] == 2:
            continue
        stat = local(root, p).stat()
        if stat.st_mode & 0o777 != e["mode"] or not local(root, p).is_file():
            raise ValueError(f"installed metadata differs: {p}")
    for name, contract in manifest["contracts"].items():
        library = name in ("libncurses", "libc")
        path = "/lib/" + name + ".so.wasm" if library else command_path(name)
        actual = wasm_contract(local(root, path), name if library else False)
        if actual != contract:
            raise ValueError(f"installed Wasm contract differs: {name}")
    if set(manifest["contracts"]) != set(COMMANDS + [n for n in OPTIONAL_COMMANDS
                                           if command_path(n) in result] + ["libncurses", "libc"]):
        raise ValueError("missing mandatory Wasm import/export contract")
    if any(e["path"] == "/usr/share/waste/sdk.json" for e in manifest["entries"]):
        guest_sdk.audit(root)
    return result


def install_sdk(root, source, sdk, library=None, review_import=()):
    """Refresh authored public headers and explicitly selected upstream snapshots.

    Unrelated binary snapshots remain immutable. Existing generated SDK files
    must also match the previous inventory before they can be replaced.
    """
    old = load(root)
    contracts = dict(old["contracts"])
    if library:
        contract = wasm_contract(library, library=True)
        if not set(contract["imports"]) <= set(contracts["libncurses"]["imports"]) | set(review_import):
            raise ValueError("new ncurses imports require explicit ABI review")
        contracts["libncurses"] = contract
    paths = {e["path"] for e in sdk["headers"]} | {"/usr/share/waste/sdk.json"}
    for e in old["entries"]:
        if e["role"] in ("sdk-compiler", "sdk-ncurses"):
            if digest(local(root, e["path"]).read_bytes()) != e["sha256"]:
                raise ValueError(f"edited upstream SDK snapshot: {e['path']}")
    # New authored headers are intentionally unregistered until this explicit
    # install. Audit all previous non-SDK content, including Wasm contracts.
    directories = {str(p) for path in paths for p in PurePosixPath(path).parents}
    retained = {e["path"] for e in old["entries"] if e["path"] not in paths}
    physical = {"/" + p.relative_to(root).as_posix() for p in root.rglob("*")
                if p != root / MANIFEST}
    expected = {e["path"] for e in old["entries"] if e["role"] != "interpreter" and e["path"] != "/"}
    if physical - expected - paths - directories:
        raise ValueError("unexpected files outside selected SDK destinations")
    for e in old["entries"]:
        if e["path"] in retained and e["role"] not in ("directory", "interpreter"):
            p = local(root, e["path"])
            if digest(p.read_bytes()) != e["sha256"] or p.stat().st_mode & 0o777 != e["mode"]:
                raise ValueError(f"edited non-SDK snapshot: {e['path']}")
    guest_sdk.audit(source)
    stage = Path(tempfile.mkdtemp(prefix="vfs-sdk-", dir=root.parent))
    backup = None
    try:
        shutil.copytree(root, stage, dirs_exist_ok=True)
        entries = {e["path"]: dict(e) for e in old["entries"]}
        if library:
            for path in ("/lib/libncurses.so.wasm", "/usr/lib/libncurses.so.wasm"):
                dst = local(stage, path)
                shutil.copyfile(library, dst)
                dst.chmod(0o644)
                entries[path].update(size=library.stat().st_size, sha256=digest(library.read_bytes()))
        sources = {e["path"]: e for e in sdk["headers"]}
        for path in sorted(paths):
            p = local(source, path)
            dst = local(stage, path)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(p, dst)
            dst.chmod(0o644)
            origin = sources.get(path, {})
            previous = entries.get(path, {})
            entries[path] = dict(path=path, kind=1, mode=0o644, uid=0, gid=0,
                                 size=p.stat().st_size, sha256=digest(p.read_bytes()),
                                 mtime_sec=previous.get("mtime_sec", 0), mtime_nsec=0,
                                 role=origin.get("role", "sdk-manifest"),
                                 source=origin.get("origin", "generated:guest_sdk.py"))
            if "inode" in previous:
                entries[path]["inode"] = previous["inode"]
        for p in sorted(directories):
            if p not in entries:
                entries[p] = dict(path=p, kind=2, mode=0o755, uid=0, gid=0, size=0,
                                  mtime_sec=0, mtime_nsec=0, role="directory")
                local(stage, p).mkdir(parents=True, exist_ok=True)
        next_inode = max(e["inode"] for e in old["entries"]) + 1
        ordered = sorted(entries.values(), key=lambda e: (e["path"].count("/"), e["path"]))
        for e in ordered:
            if "inode" not in e:
                e["inode"] = next_inode
                next_inode += 1
        manifest = dict(old, entries=ordered, contracts=contracts)
        (stage / MANIFEST).write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        audit_tree(stage, manifest)
        backup = stage.with_name(stage.name + "-previous")
        os.replace(root, backup)
        try:
            os.replace(stage, root)
        except BaseException:
            os.replace(backup, root)
            backup = None
            raise
        shutil.rmtree(backup)
        backup = None
        print(f"Installed {len(paths)} SDK files; audited {len(ordered)} VFS nodes")
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def install(root, legacy=None, component=None, source=None, review_import=()):
    old = load(root) if root.exists() else None
    if old:
        # Permit the previous asset set only during an explicit app upgrade;
        # all existing snapshot hashes must still match.
        audit_tree(root, old, upgrading_app="app" in (component or []))
    elif not legacy:
        raise ValueError("initial installation requires --legacy-dir; subsequent installs use --component")
    inputs = {}
    if legacy:
        if old:
            raise ValueError("legacy migration is only allowed for a new VFS tree")
        for n in COMMANDS:
            inputs[n] = legacy / (n + ".wasm")
        probe = REPO / "build/cli-rt/waste-probe.wasm"
        if probe.is_file():
            inputs["waste-probe"] = probe
        inputs["libncurses"] = legacy / "libncurses.so.wasm"
        inputs["libc"] = legacy / "libc.so.wasm"
        inputs["launch"] = REPO / "build/html-rt/bash-runtime.wast"
    else:
        if not component or not source or len(component) != len(source):
            raise ValueError("matching --component and --source options are required")
        if len(set(component)) != len(component):
            raise ValueError("conflicting component destinations")
        inputs.update(zip(component, source))
    # Preflight the complete input set before publishing anything.
    contracts = dict(old["contracts"]) if old else {}
    for n, src in inputs.items():
        if n == "app":
            if not src.is_dir() or any(not (src / name).is_file() for name in APP_FILES.values()):
                raise ValueError(f"incomplete app source directory: {src}")
            continue
        if not src.is_file():
            raise ValueError(f"missing mandatory compiler result: {src}")
        if n != "launch":
            contract = wasm_contract(src, n if n in ("libncurses", "libc") else False)
            if old and not set(contract["imports"]) <= set(contracts.get(n, {}).get("imports", [])) | set(review_import):
                raise ValueError(f"new imports require an explicit ABI review: {n}")
            contracts[n] = contract
    stage = Path(tempfile.mkdtemp(prefix="vfs-install-", dir=root.parent))
    backup = None
    try:
        if old:
            shutil.copytree(root, stage, dirs_exist_ok=True, copy_function=shutil.copy2)
        entries = {e["path"]: dict(e) for e in old["entries"]} if old else {}

        # The webapp now lives below the guest root user's home directory.
        # Drop the previous /waste tree from both the manifest and staged copy.
        for path in [p for p in entries if p == "/waste" or p.startswith("/waste/")]:
            del entries[path]
        stale_webapp = stage / "waste"
        if stale_webapp.exists():
            shutil.rmtree(stale_webapp)
        # /bin now holds only wat, wast, waste-probe, waste-test; coreutils
        # live under /usr/bin and are reached through PATH resolution. Drop
        # stale /bin coreutils aliases and the former /usr/bin copies of the
        # engine-adjacent utilities when upgrading an older inventory.
        for path in [p for p in entries if p.startswith("/bin/")
                     and p not in ("/bin/wat", "/bin/wast",
                                   "/bin/waste-probe", "/bin/waste-test")] + [
                "/usr/bin/waste-probe", "/usr/bin/waste-test"]:
            if path in entries:
                del entries[path]
            stale = local(stage, path)
            if stale.exists():
                stale.unlink()

        def put(path, src, role, mode, alias=None):
            guest_path(path)
            data = frontend_bytes(src) if role == "webapp" else src.read_bytes()
            stamp = src.stat().st_mtime_ns
            target = local(stage, path)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            shutil.copystat(src, target)
            target.chmod(mode)
            e = dict(path=path, kind=1, mode=mode, uid=0, gid=0, size=len(data),
                     mtime_sec=stamp // 1000000000, mtime_nsec=stamp % 1000000000,
                     sha256=digest(data), role=role, source=str(src.resolve().relative_to(REPO))
                     if src.resolve().is_relative_to(REPO) else str(src.resolve()))
            if alias:
                e["alias_of"] = alias
            if path in entries:
                e["inode"] = entries[path]["inode"]
            entries[path] = e

        for n, src in inputs.items():
            if n == "app":
                for guest_name, source_name in APP_FILES.items():
                    put("/root/waste/app/" + guest_name, src / source_name,
                        "webapp", 0o644)
            elif n == "launch":
                put("/usr/share/waste/launch.wast", src, "bootstrap", 0o644)
            elif n in ("libncurses", "libc"):
                path = "/lib/" + n + ".so.wasm"
                put(path, src, "library", 0o644)
                put("/usr" + path, src, "library", 0o644, path)
            else:
                put(command_path(n), src, "test-executable" if n == "waste-probe" else "executable", 0o755)
        for path, src in [
            ("/usr/share/waste/waste-interpreters.json", REPO / "src/html-rt/tools/vfs-interpreters.json"),
            ("/usr/share/waste/coreutils-provenance.json", REPO / "build/coreutils/provenance.json"),
            ("/usr/share/waste/coreutils-source-package.json", REPO / "build/coreutils/coreutils-source-package.json"),
            ("/usr/share/licenses/coreutils/COPYING", REPO / "submodules/coreutils/COPYING"),
            ("/usr/share/licenses/ncurses/COPYING", REPO / "submodules/ncurses/COPYING"),
            ("/usr/share/licenses/rogue/LICENSE.TXT", REPO / "submodules/rogue/LICENSE.TXT"),
        ]:
            if not old or any(n in COREUTILS for n in inputs) or path not in entries:
                put(path, src, "notice" if "/licenses/" in path else "provenance", 0o644)
        stamp = (REPO / "build/html-rt/waste-wast.wasm").stat().st_mtime_ns if not old else 0
        for path in ["/", "/tmp", "/root", "/bin", "/usr", "/usr/bin", "/usr/lib", "/lib"] + [
                "/" + p.relative_to(stage).as_posix() for p in stage.rglob("*") if p.is_dir()]:
            if path not in entries:
                entries[path] = dict(path=path, kind=2, mode=0o777 if path == "/tmp" else 0o755,
                                     uid=0, gid=0, size=0, mtime_sec=stamp // 1000000000,
                                     mtime_nsec=stamp % 1000000000, role="directory")
                local(stage, path).mkdir(parents=True, exist_ok=True)
        for path in ("/bin/wat", "/bin/wast"):
            if path not in entries:
                entries[path] = dict(path=path, kind=1, mode=0o755, uid=0, gid=0, size=0,
                                     mtime_sec=stamp // 1000000000, mtime_nsec=stamp % 1000000000,
                                     role="interpreter")
        ordered = sorted(entries.values(), key=lambda e: (e["path"].count("/"), e["path"]))
        next_inode = max((e.get("inode", 999) for e in ordered), default=999) + 1
        for e in ordered:
            if "inode" not in e:
                e["inode"] = next_inode
                next_inode += 1
        manifest = dict(old or {}, version=1, entries=ordered, contracts=contracts,
                        revision=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip())
        (stage / MANIFEST).write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        # Directory mtimes are set last, including root after the inventory write.
        for e in ordered:
            if e["kind"] == 2:
                p = local(stage, e["path"])
                p.mkdir(parents=True, exist_ok=True)
                p.chmod(e["mode"])
                ns = e["mtime_sec"] * 1000000000 + e["mtime_nsec"]
                os.utime(p, ns=(ns, ns))
        audit_tree(stage, manifest)
        if old:
            backup = stage.with_name(stage.name + "-previous")
            os.replace(root, backup)
        try:
            os.replace(stage, root)
        except BaseException:
            if backup:
                os.replace(backup, root)
                backup = None
            raise
        if backup:
            shutil.rmtree(backup)
        print(f"Installed and audited {len(ordered)} VFS nodes: {root}")
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def install_tests(root, source, corpus, review_selection=False):
    """Replace only audited test snapshots; preserve all other VFS content."""
    old = load(root)
    audit_tree(root, old)  # Refuse edited snapshots, including old tests.
    if "test_corpus" in old and old["test_corpus"]["selection_sha256"] != corpus["selection_sha256"] and not review_selection:
        raise ValueError("test selection changed; review it and use --review-selection")
    stage = Path(tempfile.mkdtemp(prefix="vfs-tests-", dir=root.parent))
    backup = None
    try:
        shutil.copytree(root, stage, dirs_exist_ok=True)
        for subtree in (stage / "tests", local(stage, test_distribution.TEST_ROOT)):
            if subtree.exists():
                shutil.rmtree(subtree)  # Only the validated managed subtree.
        previous = {e["path"]: e for e in old["entries"]}
        entries = {p: dict(e) for p, e in previous.items()
                   if not any(p == prefix or p.startswith(prefix + "/") for prefix in
                              (test_distribution.TEST_ROOT, test_distribution.LEGACY_TEST_ROOT))
                   and p != test_distribution.LICENSE}
        paths = [e["path"] for e in corpus["files"]] + [test_distribution.MANIFEST]
        files = {e["path"]: e for e in corpus["files"]}
        for path in paths:
            src, dst = local(source, path), local(stage, path)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
            dst.chmod(0o644)
            entry = files.get(path, {})
            entries[path] = dict(path=path, kind=1, mode=0o644, uid=0, gid=0,
                                 size=dst.stat().st_size, sha256=digest(dst.read_bytes()),
                                 mtime_sec=previous.get(path, {}).get("mtime_sec", 0), mtime_nsec=0,
                                 role=entry.get("role", "test-manifest"),
                                 source=entry.get("source", "generated:test_distribution.py"))
            if "inode" in previous.get(path, {}):
                entries[path]["inode"] = previous[path]["inode"]
            for parent in PurePosixPath(path).parents:
                name = str(parent)
                if name not in entries:
                    local(stage, name).mkdir(parents=True, exist_ok=True)
                    entries[name] = dict(path=name, kind=2, mode=0o755, uid=0, gid=0, size=0,
                                         mtime_sec=0, mtime_nsec=0, role="directory")
                    if "inode" in previous.get(name, {}):
                        entries[name]["inode"] = previous[name]["inode"]
        next_inode = max(e["inode"] for e in old["entries"]) + 1
        ordered = sorted(entries.values(), key=lambda e: (e["path"].count("/"), e["path"]))
        for e in ordered:
            if "inode" not in e:
                e["inode"] = next_inode
                next_inode += 1
        inventory = dict(old, entries=ordered, test_corpus=dict(path=test_distribution.MANIFEST,
                          tests=corpus["counts"]["tests"], supported=corpus["counts"]["supported"],
                          selection_sha256=corpus["selection_sha256"]))
        (stage / MANIFEST).write_text(json.dumps(inventory, indent=2, sort_keys=True) + "\n")
        audit_tree(stage, inventory)
        # Check the byte limit before publication, not at a later HTML build.
        audit(inventory, lambda path: local(stage, path).read_bytes())
        backup = stage.with_name(stage.name + "-previous")
        os.replace(root, backup)
        try:
            os.replace(stage, root)
        except BaseException:
            os.replace(backup, root)
            backup = None
            raise
        shutil.rmtree(backup)
        print(f"Installed {corpus['counts']['tests']} tests; audited {len(ordered)} VFS nodes")
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=["install", "audit", "package", "archive-audit"])
    p.add_argument("--root", type=Path, default=ROOT)
    p.add_argument("--legacy-dir", type=Path)
    p.add_argument("--component", choices=COMPONENTS, action="append")
    p.add_argument("--source", type=Path, action="append")
    p.add_argument("--review-import", action="append", default=[],
                   help="Explicitly reviewed module:name:kind addition for this install batch")
    p.add_argument("--output", type=Path)
    p.add_argument("--archive", type=Path)
    args = p.parse_args()
    try:
        if args.action == "install":
            if args.legacy_dir and (args.component or args.source):
                raise ValueError("legacy migration cannot be combined with component updates")
            install(args.root.resolve(), args.legacy_dir, args.component, args.source, args.review_import)
        elif args.action == "archive-audit":
            with tarfile.open(args.archive) as tar:
                members = {}
                for m in tar.getmembers():
                    name = m.name.removeprefix("./").rstrip("/")
                    if name in ("", "."):
                        continue
                    guest_path("/" + name)
                    if name in members or not (m.isfile() or m.isdir()):
                        raise ValueError(f"conflicting or unsupported archive member: {name}")
                    members[name] = m
                read = lambda path: tar.extractfile(members[path.lstrip("/")]).read()
                for name in ("waste-wast.wasm", "launch.wast", "vfs-manifest.json"):
                    if name not in members or not members[name].isfile():
                        raise ValueError(f"missing mandatory host bootstrap: {name}")
                manifest = json.loads(read("/vfs-manifest.json"))
                physical = {"/" + n for n in members if n not in (
                    "waste-wast.wasm", "launch.wast", "vfs-manifest.json",
                    "browser-corpus-expected-failures.txt")}
                audit(manifest, read, physical)
                for e in manifest["entries"]:
                    if e["path"] != "/" and e["role"] != "interpreter":
                        if members[e["path"].lstrip("/")].mode != e["mode"]:
                            raise ValueError(f"archive mode differs: {e['path']}")
                if read("/launch.wast") != read("/usr/share/waste/launch.wast"):
                    raise ValueError("host and guest launch input differ")
        else:
            manifest = load(args.root)
            audit_tree(args.root, manifest)
            if args.action == "package":
                if not args.output:
                    raise ValueError("--output required")
                args.output.mkdir(parents=True, exist_ok=True)
                for e in manifest["entries"]:
                    if e["role"] == "interpreter" or e["path"] == "/":
                        continue
                    dst = local(args.output, e["path"])
                    if dst.exists():
                        raise ValueError(f"package destination conflict: {dst}")
                    if e["kind"] == 2:
                        dst.mkdir(parents=True, exist_ok=True)
                        dst.chmod(e["mode"])
                    else:
                        shutil.copy2(local(args.root, e["path"]), dst)
                (args.output / "vfs-manifest.json").write_text(json.dumps(manifest, sort_keys=True) + "\n")
            print(f"VFS {args.action} audit passed")
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as e:
        raise SystemExit(f"VFS error: {e}") from e


if __name__ == "__main__":
    main()
