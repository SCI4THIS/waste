"""Versioned guest header selection; no host system-header fallback."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

COMPILER_VERSION = "22.1.8"
COMPILER_ROOTS = ("stddef.h", "stdarg.h", "stdbool.h", "limits.h", "float.h",
                  "stdalign.h", "stdnoreturn.h", "stdckdint.h", "stdcountof.h")
GENERATED_PUBLIC = {"term.h", "unctrl.h", "ncurses_dll.h", "waste/ncurses/curses.h"}


def api_policy():
    return json.loads(Path(__file__).with_name("sdk-api-policy.json").read_text())


def unavailable_policy():
    result = {}
    for group in api_policy()["unavailable_groups"]:
        for name in group["names"].split():
            if name in result:
                raise ValueError(f"duplicate SDK API policy: {name}")
            result[name] = group["reason"]
    return result


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def include_flags(root):
    return ["-ffreestanding", "-nostdinc", "-I", str(root / "usr/include"),
            "-isystem", str(root / "usr/lib/waste/cc/include")]


def prepare(repo, output, library=None):
    """Produce an explicit SDK install input using reviewed upstream sources."""
    version = subprocess.check_output(["clang", "-dumpversion"], text=True).strip()
    if version != COMPILER_VERSION:
        raise ValueError(f"review compiler headers before changing {COMPILER_VERSION} to {version}")
    resource = Path(subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip()) / "include"
    entries = []

    def copy(source, path, role, origin):
        target = output / path.lstrip("/")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if role == "sdk-ncurses":
            text = target.read_text()
            for name, reason in unavailable_policy().items():
                # Adapt declarations only; never patch definitions or macro
                # expansions. This adds diagnostics without changing DSO ABI.
                pattern = r'(^extern\s+[^;\n]*\b' + re.escape(name) + r'\s*\([^;]*?\)[^;]*);'
                text = re.sub(pattern, lambda m: m[1] + ' WASTE_UNAVAILABLE("' + reason + '");', text, flags=re.M)
                if name.endswith("_sp"):
                    pattern = r'(^extern\s+[^;\n]*NCURSES_SP_NAME\(' + re.escape(name[:-3]) + r'\)\s*\([^;]*?\)[^;]*);'
                    text = re.sub(pattern, lambda m: m[1] + ' WASTE_UNAVAILABLE("' + reason + '");', text, flags=re.M)
            target.write_text('#include <waste/abi/availability.h>\n' + text)
        target.chmod(0o644)
        entry = dict(path=path, sha256=sha(target), role=role, origin=origin)
        if role == "sdk-ncurses":
            entry.update(upstream_sha256=sha(source), adaptation="unavailable declarations: sdk-api-policy.json")
        entries.append(entry)

    public = repo / "src/vfs/usr/include"
    for p in sorted(public.rglob("*.h")):
        relative = p.relative_to(public).as_posix()
        if relative not in GENERATED_PUBLIC:
            copy(p, "/usr/include/" + relative, "sdk-public", "repository:" + str(p.relative_to(repo)))
    pending = list(COMPILER_ROOTS)
    selected = set()
    while pending:
        name = pending.pop()
        if name in selected:
            continue
        selected.add(name)
        source = resource / name
        copy(source, "/usr/lib/waste/cc/include/" + name, "sdk-compiler", "clang:" + COMPILER_VERSION)
        for child in re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', source.read_text(), re.M):
            if (resource / child).is_file():
                pending.append(child)
    license_source = Path("/usr/share/licenses/clang/LICENSE")
    copy(license_source, "/usr/share/licenses/clang/LICENSE", "notice", "clang:" + COMPILER_VERSION)
    ncurses = repo / "build/ncurses/build/include"
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo / "submodules/ncurses", text=True).strip()
    for name in ("curses.h", "term.h", "unctrl.h", "ncurses_dll.h"):
        path = "waste/ncurses/curses.h" if name == "curses.h" else name
        copy(ncurses / name, "/usr/include/" + path, "sdk-ncurses", "ncurses:" + revision)
    manifest = dict(format=1, status="experimental: provider/signature audit incomplete",
                    target="wasm32-unknown-unknown", language="C11 freestanding with selected C23 support headers",
                    compiler=dict(name="clang", version=COMPILER_VERSION, roots=COMPILER_ROOTS,
                                  parser_requirements=["compiler predefined type/limit macros", "builtin va_list",
                                                       "push_macro/pop_macro", "GNU attributes",
                                                       "enforced unavailable declarations", "__builtin_trap"]),
                    abi=dict(pointer=4, int=4, long=4, long_long=8, time_t=8, off_t=4,
                             stat=128, dirent=280, termios=60, timespec=16,
                             FILE="opaque", mmap_offset="signed i64 import (not off_t)"),
                    ncurses=dict(revision=revision, boolean="unsigned, 4 bytes",
                                 configuration_sha256=sha(ncurses / "ncurses_cfg.h"),
                                 library_sha256=sha(library or repo / "src/vfs/lib/libncurses.so.wasm")),
                    headers=sorted(entries, key=lambda e: e["path"]),
                    scope="Existing guest ABI, not full ISO C or POSIX conformance. Function-provider audit pending.",
                    api_policy=api_policy(),
                    private_exclusions=["helper.h", "waste.h", "kernel.h", "syscall.h", "ncurses_cfg.h"],
                    link_profile="Existing WASTE guest libc plus env/waste_kernel adapters; waste-crt.c startup. No host libc.")
    path = output / "usr/share/waste/sdk.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    # Installation is explicit and may use the reviewed local toolchain. A
    # normal image audit below needs neither a compiler nor build artifacts.
    from guest_providers import inspect_sdk
    manifest["api"] = inspect_sdk(repo, output, manifest, library)
    manifest["status"] = "verified partial guest ABI"
    manifest["scope"] = "Declared availability and Wasm signatures verified; not full ISO C/POSIX conformance."
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return manifest


def audit(root):
    """Check every public/compiler header and provenance without consulting host includes."""
    manifest = json.loads((root / "usr/share/waste/sdk.json").read_text())
    declared = set()
    for e in manifest["headers"]:
        path = e["path"]
        if not path.startswith(("/usr/include/", "/usr/lib/waste/cc/include/", "/usr/share/licenses/clang/")):
            raise ValueError(f"invalid SDK destination: {path}")
        if ".." in Path(path).parts or path in declared:
            raise ValueError(f"duplicate or escaping SDK destination: {path}")
        declared.add(path)
        p = root / path.lstrip("/")
        if p.is_symlink() or sha(p) != e["sha256"]:
            raise ValueError(f"modified SDK header: {path}")
        if e["role"] == "sdk-public" and re.search(r'include\s*[<"](?:.*helper\.h|\.\./)', p.read_text()):
            raise ValueError(f"private dependency in public SDK: {path}")
    physical = {"/" + p.relative_to(root).as_posix() for base in ("usr/include", "usr/lib/waste/cc/include")
                for p in (root / base).rglob("*") if p.is_file()}
    if physical != {p for p in declared if "/include/" in p}:
        raise ValueError("SDK has omitted or unlisted headers")
    library = root / "lib/libncurses.so.wasm"
    if library.exists() and sha(library) != manifest["ncurses"]["library_sha256"]:
        raise ValueError("ncurses headers do not match the recorded installed DSO")
    api = manifest.get("api", {})
    if not api.get("functions") or not api.get("variables"):
        raise ValueError("missing SDK provider metadata")
    if api["providers"]["ncurses_sha256"] != manifest["ncurses"]["library_sha256"]:
        raise ValueError("SDK provider/header DSO metadata mismatch")
    policy = {n: g["reason"] for g in manifest["api_policy"]["unavailable_groups"] for n in g["names"].split()}
    for name, entry in api["functions"].items():
        if entry["unavailable"]:
            if entry.get("provider") != "unavailable" or entry.get("reason") != policy.get(name):
                raise ValueError(f"invalid SDK unavailable policy: {name}")
        elif entry.get("provider") not in ("header-inline", "guest-libc", "ncurses", "browser-env"):
            raise ValueError(f"invalid SDK provider: {name}")
        elif entry["provider"] != "header-inline" and "wasm" not in entry:
            raise ValueError(f"missing SDK Wasm signature: {name}")
    return manifest
