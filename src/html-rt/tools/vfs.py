#!/usr/bin/env python3
"""Copy guest build outputs and package the current VFS directory.

The directory is authoritative. Mount catalogues are generated from current
files; no checked-in inventory or previously approved hashes are consulted.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from itertools import chain
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile

from runtime_config import read_config, frontend_bytes
import test_distribution

REPO = Path(__file__).resolve().parents[3]
ROOT = REPO / "src/vfs"
MANIFEST = ".inventory.json"  # Ignored legacy bookkeeping, never a guest node.
# Blessed symlinks pointing from the VFS into checked-out source trees. Each
# entry serves live .wast files from the submodule, replacing a committed copy
# with a self-documenting link.
BLESSED_SYMLINKS = {
    "/root/test/wasm-spec/core": "submodules/wasm-spec/test/core",
    "/root/test/wasm-spec/custom": "submodules/wasm-spec/test/custom",
}
COREUTILS = "true false pwd echo printf basename dirname cat chmod wc ls date".split()
COMMANDS = COREUTILS + ["bash", "rogue", "ldd", "upload", "download", "waste-probe"]
OPTIONAL_COMMANDS = ["waste-test"]
BIN_COMMANDS = {"waste-probe", "waste-test"}
COMPONENTS = COMMANDS + OPTIONAL_COMMANDS + ["libncurses", "libc", "launch", "app"]
APP_FILES = {name: name for name in (
    "app.js", "worker.js", "test-suite.js", "style.css", "terminal/model.js",
    "terminal/glf.js", "terminal/renderer.js", "terminal/render-test.js")}
CONFIG = read_config()
MAX_BYTES = CONFIG["VFS_MAX_BYTES"]
MAX_ENTRIES = CONFIG["VFS_MAX_ENTRIES"]
MAX_INVENTORY = CONFIG["VFS_INVENTORY_MAX_BYTES"]


def command_path(name):
    return ("/bin/" if name in BIN_COMMANDS else "/usr/bin/") + name


def digest(data):
    return hashlib.sha256(data).hexdigest()


def guest_path(value):
    p = PurePosixPath(value)
    if (not value.startswith("/") or "//" in value or str(p) != value or ".." in p.parts
            or "\0" in value or "\\" in value or any(ord(c) < 32 for c in value)
            or len(value.encode()) >= CONFIG["VFS_PATH_MAX_BYTES"]):
        raise ValueError(f"unsafe guest path: {value!r}")
    return value


def _under_blessed(path):
    for prefix in BLESSED_SYMLINKS:
        if path == prefix or path.startswith(prefix + "/"):
            return True
    return False


def local(root, path):
    guest_path(path)
    result = root / path.lstrip("/")
    if _under_blessed(path):
        resolved = result.resolve()
        if not resolved.is_relative_to(REPO.resolve()):
            raise ValueError(f"escaping blessed host path: {path}")
        return result
    if not result.resolve().is_relative_to(root.resolve()) or result.is_symlink():
        raise ValueError(f"escaping or symlink host path: {path}")
    return result


def wasm_contract(source, library=False):
    # Validation/inspection only; no guest code is instantiated or executed.
    program = r'''
const fs=require('fs'),b=fs.readFileSync(process.argv[1]);
const m=new WebAssembly.Module(b);
console.log(JSON.stringify({imports:WebAssembly.Module.imports(m),exports:WebAssembly.Module.exports(m)}));
'''
    result = subprocess.run(["node", "-e", program, str(source)],
                            capture_output=True, text=True, check=True)
    contract = json.loads(result.stdout)
    names = [i["module"] + ":" + i["name"] + ":" + i["kind"] for i in contract["imports"]]
    if any("asyncify" in n.lower() for n in names + [e["name"] for e in contract["exports"]]):
        raise ValueError(f"Asyncify is forbidden: {source}")
    exports = {e["name"]: e["kind"] for e in contract["exports"]}
    required = (["malloc", "free", "memcpy", "strcpy", "printf", "dlopen", "dlsym", "dlclose"]
                if library == "libc" else ["initscr", "endwin", "wrefresh"] if library else ["_start"])
    if any(exports.get(n) != "function" for n in required):
        raise ValueError(f"missing required function exports {required}: {source}")
    return {"imports": sorted(names), "required_exports": required}


def _emit_entry(entries, total, path, info, directory, data=None):
    size = 0 if directory else len(data) if data is not None else info.st_size
    total[0] += size
    if size < 0 or total[0] > MAX_BYTES:
        raise ValueError(f"VFS exceeds byte capacity: {path}")
    entry = dict(path=path, kind=2 if directory else 1, mode=info.st_mode & 0o777,
                 uid=0, gid=0, size=size, mtime_sec=info.st_mtime_ns // 10**9,
                 mtime_nsec=info.st_mtime_ns % 10**9, role="directory" if directory else "file")
    if not directory:
        entry["sha256"] = digest(data)
    entries[path] = entry
    if len(entries) > MAX_ENTRIES:
        raise ValueError("VFS exceeds entry capacity")


def load(root):
    """Discover current files, including additions and edits, without a ledger."""
    root = Path(root)
    entries = {}
    total = [0]
    for p in chain((root,), root.rglob("*")):
        if p == root / MANIFEST:
            continue
        path = "/" + p.relative_to(root).as_posix() if p != root else "/"
        local(root, path)
        if p.is_symlink():
            if path not in BLESSED_SYMLINKS:
                raise ValueError(f"unsupported VFS node: {path}")
            target = (REPO / BLESSED_SYMLINKS[path]).resolve()
            if p.resolve() != target:
                raise ValueError(f"blessed symlink wrong target: {path}")
            if not target.is_dir():
                print(f"INFO: wasm-spec submodule missing for {path}; "
                      "mounted .wast files from this tree will be skipped.",
                      file=sys.stderr)
                continue
            _emit_entry(entries, total, path, target.stat(), True)
            for sub in sorted(target.rglob("*.wast")):
                relative_sub = sub.relative_to(target)
                if "_output" in relative_sub.parts or sub.is_symlink():
                    continue
                parts = relative_sub.parts
                for i in range(1, len(parts)):
                    dir_path = path + "/" + "/".join(parts[:i])
                    if dir_path not in entries:
                        _emit_entry(entries, total, dir_path, (target / "/".join(parts[:i])).stat(), True)
                sub_path = path + "/" + relative_sub.as_posix()
                data = sub.read_bytes()
                info = sub.stat()
                if len(data) != info.st_size:
                    raise ValueError(f"blessed symlink target changed while reading: {sub_path}")
                _emit_entry(entries, total, sub_path, info, False, data)
            continue
        info = p.stat(follow_symlinks=False)
        directory = stat.S_ISDIR(info.st_mode)
        if not directory and not stat.S_ISREG(info.st_mode):
            raise ValueError(f"unsupported VFS node: {path}")
        if directory:
            _emit_entry(entries, total, path, info, True)
        else:
            data = p.read_bytes()
            if len(data) != info.st_size or p.stat().st_mtime_ns != info.st_mtime_ns:
                raise ValueError(f"VFS file changed while reading: {path}")
            _emit_entry(entries, total, path, info, False, data)
    # Minimal guest boot directories and engine-provided interpreter commands.
    for path in ("/bin", "/root", "/tmp"):
        entries.setdefault(path, dict(path=path, kind=2, mode=0o777 if path == "/tmp" else 0o755,
                                     uid=0, gid=0, size=0, mtime_sec=0, mtime_nsec=0, role="directory"))
    for path in ("/bin/wat", "/bin/wast"):
        entries.setdefault(path, dict(path=path, kind=1, mode=0o755, uid=0, gid=0, size=0,
                                     mtime_sec=0, mtime_nsec=0, role="interpreter"))
    ordered = sorted(entries.values(), key=lambda e: (e["path"].count("/"), e["path"]))
    for inode, entry in enumerate(ordered, 1):
        entry["inode"] = inode
    manifest = dict(version=1, entries=ordered)
    audit(manifest, lambda path: local(root, path).read_bytes())
    return manifest


def audit(manifest, read, physical=None, upgrading_app=False):
    """Validate bounded metadata and the bytes of this generated snapshot."""
    if manifest.get("version") != 1:
        raise ValueError("unsupported VFS metadata version")
    if len(json.dumps(manifest, sort_keys=True).encode()) > MAX_INVENTORY:
        raise ValueError("VFS metadata exceeds byte capacity")
    entries = manifest["entries"]
    if not entries or len(entries) > MAX_ENTRIES:
        raise ValueError("VFS exceeds entry capacity")
    seen, inodes, total = {}, set(), 0
    for index, entry in enumerate(entries):
        path = guest_path(entry["path"])
        if path in seen:
            raise ValueError(f"conflicting destination: {path}")
        if index == 0:
            if path != "/" or entry["kind"] != 2:
                raise ValueError("first VFS node must be root directory")
        elif seen.get(str(PurePosixPath(path).parent), {}).get("kind") != 2:
            raise ValueError(f"missing directory parent: {path}")
        if entry["kind"] not in (1, 2) or (entry["kind"] == 2) != (entry["role"] == "directory"):
            raise ValueError(f"invalid node kind: {path}")
        for key, low, high in (("mode", 0, 0o777), ("uid", 0, 2**32-1), ("gid", 0, 2**32-1),
                               ("inode", 1, 2**64-1), ("size", 0, MAX_BYTES),
                               ("mtime_sec", -(2**63), 2**63-1), ("mtime_nsec", 0, 10**9-1)):
            if type(entry[key]) is not int or not low <= entry[key] <= high:
                raise ValueError(f"invalid metadata: {path} ({key})")
        if entry["inode"] in inodes:
            raise ValueError(f"duplicate inode: {path}")
        inodes.add(entry["inode"])
        total += entry["size"]
        if total > MAX_BYTES:
            raise ValueError("VFS exceeds byte capacity")
        if entry["role"] in ("directory", "interpreter"):
            if entry["size"]:
                raise ValueError(f"invalid empty node: {path}")
        else:
            data = read(path)
            if len(data) != entry["size"] or digest(data) != entry["sha256"]:
                raise ValueError(f"incomplete or changed package file: {path}")
        seen[path] = entry
    if physical is not None:
        expected = {p for p, e in seen.items() if p != "/" and e["role"] != "interpreter"}
        if physical != expected:
            raise ValueError("archive contents differ from generated metadata")
    return seen


def audit_tree(root, manifest=None, upgrading_app=False):
    current = load(root)
    return {e["path"]: e for e in current["entries"]}


def publish(root, changes, remove=()):
    """Preflight inputs and publish one complete directory, preserving other files."""
    if root.exists():
        load(root)
    root.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix="vfs-install-", dir=root.parent))
    backup = None
    try:
        if root.exists():
            shutil.copytree(root, stage, dirs_exist_ok=True, copy_function=shutil.copy2, symlinks=True)
        saved_links = {}
        for vfs_path in BLESSED_SYMLINKS:
            staged_link = stage / vfs_path.lstrip("/")
            if staged_link.is_symlink():
                saved_links[vfs_path] = os.readlink(staged_link)
        for path in remove:
            target = stage / path.lstrip("/")
            if target.is_symlink() or target.is_file():
                target.unlink()
            elif target.is_dir():
                shutil.rmtree(target)
        for path, data, mode in changes:
            if _under_blessed(path):
                raise ValueError(f"refusing to write into blessed symlink tree: {path}")
            destination = local(stage, path)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            destination.chmod(mode)
        for vfs_path, link_target in saved_links.items():
            staged_link = stage / vfs_path.lstrip("/")
            staged_link.parent.mkdir(parents=True, exist_ok=True)
            if staged_link.is_symlink() or staged_link.exists():
                staged_link.unlink()
            staged_link.symlink_to(link_target)
        (stage / MANIFEST).unlink(missing_ok=True)
        manifest = load(stage)
        if root.exists():
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
        print(f"Installed {len(changes)} files; checked {len(manifest['entries'])} current VFS nodes")
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def install(root, legacy=None, component=None, source=None):
    if legacy:
        raise ValueError("legacy distribution migration is retired; copy files or use --component")
    if not component or not source or len(component) != len(source) or len(set(component)) != len(component):
        raise ValueError("matching unique --component and --source options are required")
    changes = []
    for name, src in zip(component, source):
        if name == "app":
            for guest_name, source_name in APP_FILES.items():
                changes.append(("/root/app/" + guest_name, frontend_bytes(src / source_name), 0o644))
        else:
            if not src.is_file():
                raise ValueError(f"missing compiler result: {src}")
            if name != "launch":
                wasm_contract(src, name if name in ("libc", "libncurses") else False)
            paths = (["/lib/" + name + ".so.wasm", "/usr/lib/" + name + ".so.wasm"]
                     if name in ("libc", "libncurses") else ["/usr/share/waste/launch.wast"]
                     if name == "launch" else [command_path(name)])
            changes.extend((path, src.read_bytes(), 0o644 if name in ("libc", "libncurses", "launch") else 0o755)
                           for path in paths)
    publish(root, changes)


def install_sdk(root, source, sdk, library=None):
    changes = [(e["path"], local(source, e["path"]).read_bytes(), 0o644) for e in sdk["headers"]]
    changes.append(("/usr/share/waste/sdk.json", local(source, "/usr/share/waste/sdk.json").read_bytes(), 0o644))
    if library:
        wasm_contract(library, library=True)
        changes.extend((p, library.read_bytes(), 0o644) for p in ("/lib/libncurses.so.wasm", "/usr/lib/libncurses.so.wasm"))
    publish(root, changes)


def install_tests(root, source, corpus):
    # Validate candidate corpus coherence as an explicit fixture operation.
    # Existing local edits or changed selections do not prevent its installation.
    test_distribution.audit(corpus, lambda p: local(source, p).read_bytes(), audit_tree(source))
    paths = [e["path"] for e in corpus["files"] if not _under_blessed(e["path"])]
    paths.append(test_distribution.MANIFEST)
    publish(root, [(p, local(source, p).read_bytes(), 0o644) for p in paths],
            remove=(test_distribution.TEST_ROOT, test_distribution.LEGACY_TEST_ROOT))


def package(root, output):
    manifest = load(root)
    output.mkdir(parents=True, exist_ok=True)
    dir_modes = []
    for entry in manifest["entries"]:
        path = entry["path"]
        if path == "/" or entry["role"] == "interpreter":
            continue
        destination = local(output, path)
        if destination.exists():
            raise ValueError(f"package destination conflict: {path}")
        if entry["kind"] == 2:
            destination.mkdir(parents=True, exist_ok=True)
            dir_modes.append((destination, entry["mode"]))
        else:
            shutil.copy2(local(root, path), destination)
    # Apply directory modes after writes so read-only parents don't block children.
    for destination, mode in reversed(dir_modes):
        destination.chmod(mode)
    # Verify the copied bytes against their own freshly generated catalogue.
    audit(manifest, lambda p: local(output, p).read_bytes())
    (output / "vfs-manifest.json").write_text(json.dumps(manifest, sort_keys=True) + "\n")


def archive_audit(path):
    with tarfile.open(path) as archive:
        members = {}
        for member in archive.getmembers():
            name = member.name.removeprefix("./").rstrip("/")
            if name in ("", "."):
                continue
            guest_path("/" + name)
            if name in members or not (member.isfile() or member.isdir()):
                raise ValueError(f"conflicting or unsupported archive member: {name}")
            members[name] = member
        def read(path):
            return archive.extractfile(members[path.lstrip("/")]).read()
        for name in ("waste-wast.wasm", "launch.wast", "vfs-manifest.json"):
            if name not in members or not members[name].isfile():
                raise ValueError(f"missing host bootstrap: {name}")
        manifest = json.loads(read("/vfs-manifest.json"))
        physical = {"/" + n for n in members if n not in (
            "waste-wast.wasm", "launch.wast", "vfs-manifest.json", "browser-corpus-expected-failures.txt")}
        audit(manifest, read, physical)
        for entry in manifest["entries"]:
            if entry["path"] != "/" and entry["role"] != "interpreter":
                if members[entry["path"].lstrip("/")].mode != entry["mode"]:
                    raise ValueError(f"archive mode differs: {entry['path']}")
        if read("/launch.wast") != read("/usr/share/waste/launch.wast"):
            raise ValueError("host and guest launch inputs differ")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("install", "audit", "manifest", "package", "archive-audit"))
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--component", choices=COMPONENTS, action="append")
    parser.add_argument("--source", type=Path, action="append")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--archive", type=Path)
    args = parser.parse_args()
    try:
        if args.action == "install":
            install(args.root.resolve(), component=args.component, source=args.source)
        elif args.action == "archive-audit":
            archive_audit(args.archive)
        elif args.action == "package":
            if not args.output:
                raise ValueError("--output required")
            package(args.root.resolve(), args.output)
        else:
            manifest = load(args.root.resolve())
            if args.action == "manifest":
                data = json.dumps(manifest, sort_keys=True) + "\n"
                if args.output:
                    args.output.parent.mkdir(parents=True, exist_ok=True)
                    args.output.write_text(data)
                else:
                    print(data, end="")
                return
        print(f"VFS {args.action} passed")
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"VFS error: {error}") from error


if __name__ == "__main__":
    main()
