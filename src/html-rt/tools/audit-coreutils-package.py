#!/usr/bin/env python3
"""Audit the deterministic offline Coreutils package manifest."""

from __future__ import annotations

import argparse
import json
import tarfile
from pathlib import Path


REQUIRED_FILES = {
    "true.wasm", "false.wasm", "pwd.wasm", "echo.wasm", "printf.wasm",
    "basename.wasm", "dirname.wasm", "cat.wasm", "wc.wasm", "ls.wasm",
    "date.wasm",
    "usr/share/waste/coreutils-provenance.json",
    "usr/share/waste/coreutils-source-package.json",
    "usr/share/waste/waste-interpreters.json",
    "usr/share/licenses/coreutils/COPYING",
}
REQUIRED_INTERPRETERS = {"/bin/wat", "/bin/wast"}


def audit_members(members: list[tarfile.TarInfo]) -> None:
    names = {member.name.removeprefix("./") for member in members}
    missing = sorted(REQUIRED_FILES - names)
    if missing:
        raise SystemExit("package missing: " + ", ".join(missing))
    ordered = [member.name for member in members]
    if ordered != sorted(ordered):
        raise SystemExit("package entries are not sorted")
    for member in members:
        if member.uid != 0 or member.gid != 0 or member.uname or member.gname:
            raise SystemExit(f"non-deterministic ownership metadata: {member.name}")
        if member.mtime != 0:
            raise SystemExit(f"non-deterministic timestamp: {member.name}")


def audit_archive(path: Path) -> None:
    with tarfile.open(path, "r:gz") as archive:
        members = archive.getmembers()
        audit_members(members)
        by_name = {member.name.removeprefix("./"): member for member in members}
        handlers = json.loads(archive.extractfile(
            by_name["usr/share/waste/waste-interpreters.json"]).read())
        if set(handlers.get("interpreters", [])) != REQUIRED_INTERPRETERS:
            raise SystemExit("interpreter manifest does not list /bin/wat and /bin/wast")
        provenance = json.loads(archive.extractfile(
            by_name["usr/share/waste/coreutils-provenance.json"]).read())
        if not provenance.get("source", {}).get("commit"):
            raise SystemExit("Coreutils provenance has no source commit")
        if not provenance.get("managed_patch", {}).get("path"):
            raise SystemExit("Coreutils provenance has no managed patch")
        source_mapping = json.loads(archive.extractfile(
            by_name["usr/share/waste/coreutils-source-package.json"]).read())
        if set(source_mapping.get("utilities", [])) != {
                "true", "false", "pwd", "echo", "printf", "basename", "dirname", "cat",
                "wc", "ls", "date"}:
            raise SystemExit("source-package mapping does not cover every utility")
        if not source_mapping.get("archive") or not source_mapping.get("sha256"):
            raise SystemExit("source-package mapping is incomplete")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    args = parser.parse_args()
    audit_archive(args.archive)
    print(f"Coreutils package audit passed: {args.archive}")


if __name__ == "__main__":
    main()
