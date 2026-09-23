#!/usr/bin/env python3
"""Stage pinned GNU coreutils source without dirtying its git submodule."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path


PATCH_RELATIVE = Path("submodules/coreutils-waste.patch")
COREUTILS_RELATIVE = Path("submodules/coreutils")
DEFAULT_OUTPUT = Path("build/html-rt/coreutils")


def git(repo: Path, *args: str, check: bool = True) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if check and result.returncode != 0:
        raise RuntimeError(
            f"git {' '.join(args)} failed in {repo}: {result.stderr.strip()}"
        )
    return result.stdout


def patch_is_empty(patch_path: Path) -> bool:
    return not patch_path.read_text(encoding="utf-8").strip()


def patch_check(coreutils: Path, patch_path: Path, reverse: bool = False) -> bool:
    if patch_is_empty(patch_path):
        return True
    command = ["apply"]
    if reverse:
        command.append("--reverse")
    command.extend(["--check", str(patch_path.resolve())])
    result = subprocess.run(
        ["git", "-C", str(coreutils), *command],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return result.returncode == 0


def submodule_lines(coreutils: Path) -> list[dict[str, str | bool]]:
    output = git(coreutils, "submodule", "status", "--recursive", check=False)
    entries: list[dict[str, str | bool]] = []
    for line in output.splitlines():
        if not line.strip():
            continue
        marker = line[0]
        fields = line[1:].strip().split(maxsplit=2)
        if len(fields) < 2:
            continue
        entries.append({
            "commit": fields[0],
            "path": fields[1],
            "description": fields[2] if len(fields) == 3 else "",
            "initialized": marker not in {"-", "U"},
            "status_marker": marker,
        })
    return entries


def license_files(coreutils: Path) -> list[dict[str, str | int]]:
    candidates: list[Path] = []
    for path in coreutils.rglob("*"):
        if not path.is_file() or ".git" in path.parts:
            continue
        name = path.name.lower()
        if (
            name == "authors"
            or name.startswith("copying")
            or name.startswith("license")
            or name.startswith("notice")
            or name == "dependencies"
            or name == "packaging"
        ):
            candidates.append(path)
    records = []
    for path in sorted(candidates):
        data = path.read_bytes()
        records.append({
            "path": path.relative_to(coreutils).as_posix(),
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })
    return records


def provenance(repo_root: Path) -> dict[str, object]:
    coreutils = repo_root / COREUTILS_RELATIVE
    patch_path = repo_root / PATCH_RELATIVE
    if not coreutils.is_dir():
        raise RuntimeError(f"missing coreutils submodule: {coreutils}")
    if not patch_path.is_file():
        raise RuntimeError(f"missing managed patch file: {patch_path}")

    return {
        "source": {
            "path": COREUTILS_RELATIVE.as_posix(),
            "commit": git(coreutils, "rev-parse", "HEAD").strip(),
            "describe": git(coreutils, "describe", "--tags", "--always").strip(),
            "remote": git(coreutils, "remote", "get-url", "origin", check=False).strip(),
        },
        "managed_patch": {
            "path": PATCH_RELATIVE.as_posix(),
            "sha256": hashlib.sha256(patch_path.read_bytes()).hexdigest(),
            "empty": patch_is_empty(patch_path),
        },
        "submodules": submodule_lines(coreutils),
        "license_files": license_files(coreutils),
        "patch_state": patch_state(repo_root),
    }


def patch_state(repo_root: Path) -> str:
    coreutils = repo_root / COREUTILS_RELATIVE
    patch_path = repo_root / PATCH_RELATIVE
    status = git(coreutils, "status", "--porcelain=v1", check=False).strip()
    if status:
        if patch_check(coreutils, patch_path, reverse=True) and not patch_is_empty(patch_path):
            return "applied"
        return "conflict"
    if patch_check(coreutils, patch_path):
        return "available"
    if patch_check(coreutils, patch_path, reverse=True):
        return "applied"
    return "conflict"


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def stage_source(repo_root: Path, output: Path) -> None:
    coreutils = repo_root / COREUTILS_RELATIVE
    patch_path = repo_root / PATCH_RELATIVE
    state = patch_state(repo_root)
    if state == "conflict":
        raise RuntimeError(
            "coreutils patch state is conflict; resolve unrelated submodule "
            "changes before staging"
        )

    missing = [entry["path"] for entry in submodule_lines(coreutils)
               if not bool(entry["initialized"])]
    if missing:
        raise RuntimeError(
            "coreutils nested submodule(s) are uninitialized: "
            + ", ".join(str(path) for path in missing)
            + "; run git submodule update --init --recursive before staging"
        )

    applied_here = False
    if state == "available" and not patch_is_empty(patch_path):
        subprocess.run(
            ["git", "-C", str(coreutils), "apply", str(patch_path.resolve())],
            check=True,
        )
        applied_here = True
    try:
        source = output / "source"
        if output.exists():
            shutil.rmtree(output)
        source.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(
            coreutils,
            source,
            ignore=shutil.ignore_patterns(".git"),
        )
        report = provenance(repo_root)
        report["staged_source"] = "build/html-rt/coreutils/source"
        write_json(output / "provenance.json", report)
    finally:
        if applied_here:
            subprocess.run(
                [
                    "git", "-C", str(coreutils), "apply", "--reverse",
                    str(patch_path.resolve()),
                ],
                check=True,
            )
    if patch_state(repo_root) != "available":
        raise RuntimeError("coreutils submodule was not restored to a clean state")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument(
        "--action", choices=("status", "provenance", "stage"), default="status"
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    try:
        if args.action == "status":
            print(patch_state(repo_root))
        elif args.action == "provenance":
            output = args.output or repo_root / DEFAULT_OUTPUT / "provenance.json"
            write_json(output, provenance(repo_root))
            print(output)
        else:
            output = args.output or repo_root / DEFAULT_OUTPUT
            stage_source(repo_root, output)
            print(output)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
