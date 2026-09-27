#!/usr/bin/env python3
"""Build and audit the deterministic GNU Coreutils corresponding-source bundle."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import stat
import tarfile
from pathlib import Path


UTILITY_NAMES = [
    "true", "false", "pwd", "echo", "printf", "basename", "dirname", "cat", "wc",
    "ls",
]
REPOSITORY_INPUTS = [
    "start.sh",
    "docs/coreutils-source-distribution.md",
    "submodules/bootstrap-coreutils.sh",
    "submodules/coreutils-waste.patch",
    "src/engine",
    "src/html-rt",
]


def archive_name(path: Path, root: Path, prefix: str) -> str:
    return f"{prefix}/{path.relative_to(root).as_posix()}"


def add_path(archive: tarfile.TarFile, path: Path, root: Path, prefix: str) -> None:
    info = tarfile.TarInfo(archive_name(path, root, prefix))
    metadata = path.lstat()
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    info.mtime = 0
    info.mode = stat.S_IMODE(metadata.st_mode)
    if path.is_symlink():
        info.type = tarfile.SYMTYPE
        info.linkname = os.readlink(path)
        archive.addfile(info)
    elif path.is_dir():
        info.type = tarfile.DIRTYPE
        archive.addfile(info)
    elif path.is_file():
        info.size = metadata.st_size
        with path.open("rb") as source:
            archive.addfile(info, source)


def paths_below(root: Path) -> list[Path]:
    paths = [root]
    paths.extend(root.rglob("*"))
    return sorted(paths, key=lambda path: path.relative_to(root).as_posix())


def include_path(path: Path) -> bool:
    if "__pycache__" in path.parts:
        return False
    if path.suffix in {".pyc", ".wasm"}:
        return False
    return True


def manifest_bytes(provenance: dict[str, object]) -> bytes:
    manifest = {
        "format": 1,
        "purpose": "GNU Coreutils corresponding source for the WASTE browser package",
        "utilities": UTILITY_NAMES,
        "source_commit": provenance["source"]["commit"],
        "managed_patch": provenance["managed_patch"],
        "contents": {
            "coreutils-source/": "patched, bootstrapped source tree used by the build",
            "waste/": "WASTE engine, runtime, build scripts, interface headers, and instructions",
            "SOURCE-BUNDLE.json": "binary-to-source and provenance mapping",
        },
    }
    return (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()


def add_bytes(archive: tarfile.TarFile, name: str, data: bytes) -> None:
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = 0o644
    info.uid = info.gid = info.mtime = 0
    info.uname = info.gname = ""
    from io import BytesIO
    archive.addfile(info, BytesIO(data))


def audit_bundle(output: Path) -> None:
    required = {
        "SOURCE-BUNDLE.json",
        "coreutils-source/COPYING",
        "coreutils-source/configure",
        "waste/submodules/coreutils-waste.patch",
        "waste/submodules/bootstrap-coreutils.sh",
        "waste/src/html-rt/Makefile",
        "waste/src/html-rt/tools/build-coreutils-runtime.py",
        "waste/src/html-rt/tools/coreutils-waste.config.site",
        "waste/src/html-rt/lib/waste-crt.c",
        "waste/src/html-rt/lib/include/unistd.h",
        "waste/docs/coreutils-source-distribution.md",
        "waste/build/coreutils/provenance.json",
        "waste/build/coreutils/configure/configure-report.json",
    }
    with tarfile.open(output, "r:gz") as archive:
        members = archive.getmembers()
        names = {member.name for member in members}
        missing = sorted(required - names)
        if missing:
            raise RuntimeError("source bundle missing: " + ", ".join(missing))
        if [member.name for member in members] != sorted(member.name for member in members):
            raise RuntimeError("source bundle entries are not sorted")
        for member in members:
            if member.uid or member.gid or member.uname or member.gname or member.mtime:
                raise RuntimeError(f"non-deterministic source metadata: {member.name}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--provenance", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mapping", type=Path, required=True)
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    source = args.source.resolve()
    provenance = json.loads(args.provenance.read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)

    with args.output.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.GNU_FORMAT) as archive:
                add_bytes(archive, "SOURCE-BUNDLE.json", manifest_bytes(provenance))
                entries: list[tuple[Path, Path, str]] = []
                entries.extend((path, source, "coreutils-source")
                               for path in paths_below(source) if include_path(path))
                for relative in REPOSITORY_INPUTS:
                    selected = repo_root / relative
                    selected_paths = paths_below(selected) if selected.is_dir() else [selected]
                    entries.extend((path, repo_root, "waste") for path in selected_paths
                                   if include_path(path))
                for generated in [args.provenance,
                                  repo_root / "build/coreutils/configure/configure-report.json"]:
                    if not generated.is_file():
                        raise SystemExit(f"required source-package metadata missing: {generated}")
                    entries.append((generated.resolve(), repo_root, "waste"))
                for path, root, prefix in sorted(entries, key=lambda item: archive_name(*item)):
                    add_path(archive, path, root, prefix)

    audit_bundle(args.output)
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    mapping = {
        "format": 1,
        "archive": args.output.name,
        "sha256": digest,
        "utilities": UTILITY_NAMES,
        "source_commit": provenance["source"]["commit"],
        "instructions": "docs/coreutils-source-distribution.md",
    }
    args.mapping.write_text(json.dumps(mapping, indent=2, sort_keys=True) + "\n")
    print(f"Coreutils corresponding-source bundle: {args.output}")
    print(f"SHA-256: {digest}")


if __name__ == "__main__":
    main()
