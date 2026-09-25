#!/usr/bin/env python3
"""Compile one staged coreutils utility and classify the first ABI boundary."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


MISSING_HEADER = re.compile(r"fatal error: ['\"]([^'\"]+)['\"] file not found")
UNDEFINED_SYMBOL = re.compile(
    r"(?:undefined symbol|undefined reference to)[^A-Za-z0-9_]*['\"]?"
    r"([A-Za-z_][A-Za-z0-9_]*)"
)

WASTE_STARTUP_IMPORTS = ("env:exit", "waste_kernel:startup_v1")
WASTE_RUNTIME_IMPORTS = (
    "env:close", "env:open", "env:readdir_v1",
    "waste_kernel:ioctl_v1", "waste_kernel:isatty_v1",
    "waste_kernel:path_access_v1", "waste_kernel:path_stat_v1",
    "waste_kernel:pselect_v1", "waste_kernel:select_v1",
    "waste_kernel:tcgetattr_v1", "waste_kernel:tcsetattr_v1",
)
RUNTIME_BUILDER = "src/html-rt/tools/build-coreutils-runtime.py"


def relative(path: Path, root: Path) -> str:
    return path.resolve().relative_to(root.resolve()).as_posix()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--utility", default="true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    root = args.repo_root.resolve()
    build = args.build.resolve()
    coreutils_build = build.parent if build.name == "configure" else build
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    log_path = output.parent / f"{args.utility}.build.log"
    generated_headers = ["lib/configmake.h", "src/version.h", "lib/unitypes.h",
                         "lib/unictype.h", "lib/unicase.h", "lib/uninorm.h",
                         "lib/uniwidth.h", "lib/malloc/scratch_buffer.gl.h"]
    generated_headers.extend(["lib/crc-sliceby8.h", "lib/fts_.h"])
    preflight = ["make", "-C", str(build), "-o", "config.status",
                 "V=1", "-j1"] + generated_headers
    missing_generated = [name for name in generated_headers
                         if not (build / name).is_file()]
    command = ["make", "-C", str(build), "-o", "config.status",
               "V=1", "-j1", f"src/{args.utility}"]
    artifact = build / "src" / args.utility
    crt_object = coreutils_build / "sysroot" / "lib" / "waste-crt.o"
    crt_source = root / "src/html-rt/lib/waste-crt.c"
    allocator_object = coreutils_build / "sysroot" / "lib" / "coreutils-allocator.o"
    allocator_source = root / "src/html-rt/lib/stdlib.c"
    audit_path = output.parent / f"{args.utility}-import-audit.json"
    linked_artifact = output.parent / f"{args.utility}-linked.wasm"
    linked_audit_path = output.parent / f"{args.utility}-linked-import-audit.json"
    link_flags = ("-Wl,--no-entry,--export-memory,--export-table,--export=_start,"
                  "--allow-undefined,--gc-sections")
    with log_path.open("w", encoding="utf-8") as log:
        if missing_generated:
            preflight_result = subprocess.run(
                preflight, stdout=log, stderr=subprocess.STDOUT, check=False,
            )
        else:
            log.write("generated headers already present; skipped Make preflight\n")
            preflight_result = subprocess.CompletedProcess(preflight, 0)
        if preflight_result.returncode == 0:
            crt_object.parent.mkdir(parents=True, exist_ok=True)
            crt_result = subprocess.run(
                [str(coreutils_build / "sysroot/bin/waste-wasm-clang"), "-c",
                 "-DWASTE_MAIN_TWO_ARGS", str(crt_source), "-o", str(crt_object)],
                stdout=log, stderr=subprocess.STDOUT, check=False,
            )
            if crt_result.returncode != 0:
                result = crt_result
            else:
                allocator_result = subprocess.run(
                    [str(coreutils_build / "sysroot/bin/waste-wasm-clang"),
                     "-DWASTE_ENGINE", "-DWASTE_ALLOCATOR_ONLY",
                     "-Isrc/html-rt/lib", "-c", str(allocator_source),
                     "-o", str(allocator_object)],
                    cwd=root, stdout=log, stderr=subprocess.STDOUT,
                    check=False,
                )
                if allocator_result.returncode != 0:
                    result = allocator_result
                    allocator_object.unlink(missing_ok=True)
                else:
                    # The normal configure link line has no host entry point.
                    # Remove only this generated target so make performs the
                    # link while retaining its normal object/library deps.
                    artifact.unlink(missing_ok=True)
                    result = subprocess.run(
                        command + [
                            f"LDFLAGS={link_flags}",
                            f"LIBS={crt_object} {allocator_object}",
                        ], stdout=log, stderr=subprocess.STDOUT, check=False,
                    )
        else:
            result = preflight_result
    log_text = log_path.read_text(encoding="utf-8", errors="replace")
    missing_headers = sorted(set(MISSING_HEADER.findall(log_text)))
    undefined_symbols = sorted(set(UNDEFINED_SYMBOL.findall(log_text)))
    audit = None
    link_result = None
    if result.returncode == 0 and artifact.is_file():
        link_result = subprocess.run(
            ["python3", str(root / RUNTIME_BUILDER), "--repo-root", str(root),
             "--utility", str(artifact), "--name", args.utility,
            "--libc", str(root / "build/html-rt/waste-libc/waste-libc.wasm"),
             "--output", str(linked_artifact)],
            cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            check=False,
        )
        if link_result.returncode == 0 and linked_artifact.is_file():
            artifact = linked_artifact
            audit_path = linked_audit_path
            subprocess.run(
                ["python3", str(root / "src/html-rt/tools/audit-wasm-imports.py"),
                 str(artifact), "--json", str(audit_path),
                 *sum((["--allow", name] for name in
                       WASTE_STARTUP_IMPORTS + WASTE_RUNTIME_IMPORTS), [])],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                check=False,
            )
        if audit_path.is_file():
            audit = json.loads(audit_path.read_text(encoding="utf-8"))
        else:
            audit = {"ok": False, "imports": [], "unknown": [],
                     "asyncifySymbols": [], "error": link_result.stdout}

    if result.returncode == 0 and audit and audit.get("ok"):
        status = "passed"
        blockers: list[str] = []
    elif result.returncode == 0:
        status = "blocked"
        blockers = (["one-memory Coreutils/libc relink failed"]
                    if link_result is not None and link_result.returncode != 0 else
                    ["linked utility imports are not yet mapped to the WASTE ABI"])
    elif missing_headers:
        status = "blocked"
        blockers = [
            "guest sysroot is missing public headers: " + ", ".join(missing_headers),
        ]
    else:
        status = "failed"
        blockers = ["utility compilation failed; inspect the captured build log"]

    report = {
        "format": 1,
        "status": status,
        "utility": args.utility,
        "target": f"src/{args.utility}",
        "source": relative(coreutils_build / "source", root),
        "build": relative(build, root),
        "sysroot": relative(coreutils_build / "sysroot", root),
        "command": ["make", "-C", relative(build, root), "V=1", "-j1",
                     f"src/{args.utility}"],
        "generated_headers": generated_headers,
        "returncode": result.returncode,
        "build_log": relative(log_path, root),
        "artifact": relative(artifact, root) if artifact.is_file() else None,
        "artifact_format": "wasm" if artifact.is_file() else None,
        "source_artifact": relative(build / "src" / args.utility, root),
        "relink": {
            "artifact": relative(linked_artifact, root) if linked_artifact.is_file() else None,
            "returncode": link_result.returncode if link_result is not None else None,
            "output": link_result.stdout[-4000:] if link_result is not None else None,
        },
        "missing_headers": missing_headers,
        "undefined_symbols": undefined_symbols,
        "unknown_imports": audit.get("unknown", []) if audit else [],
        "asyncify_symbols": audit.get("asyncifySymbols", []) if audit else [],
        "import_audit": relative(audit_path, root) if audit_path.is_file() else None,
        "blockers": blockers,
        "next_stage": "8F" if status == "blocked" else None,
    }
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if status in ("passed", "blocked") else 1


if __name__ == "__main__":
    raise SystemExit(main())
