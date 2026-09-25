#!/usr/bin/env python3
"""Run or explain the out-of-tree WASTE coreutils configure probe."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path


CONFIG_SITE = Path("src/html-rt/tools/coreutils-waste.config.site")

FORCED_ANSWERS = [
    {
        "name": "ac_cv_prog_cc_cross",
        "value": "yes",
        "reason": "the wasm32 compiler wrapper cannot execute target binaries",
        "test": "wrapper --target=wasm32; target probes are not run",
    },
    {
        "name": "ac_cv_c_cross",
        "value": "yes",
        "reason": "the application target is wasm32-unknown-none",
        "test": "wrapper --target=wasm32; target probes are not run",
    },
    {
        "name": "--disable-year2038",
        "value": "configured-out",
        "reason": "the current guest sysroot does not yet advertise post-2038 time_t support",
        "test": "configure fails without this option; Stage 8E must add the time ABI before enabling it",
    },
    {
        "name": "ac_cv_c_bigendian",
        "value": "no",
        "reason": "WebAssembly linear memory is specified as little-endian",
        "test": "WebAssembly core semantics; target probe cannot execute on the host",
    },
    {
        "name": "gl_cv_socklen_t_equiv",
        "value": "int",
        "reason": "the current WASTE socket ABI uses 32-bit lengths",
        "test": "Stage 8E must validate this against the complete sys/socket.h ABI",
    },
    {
        "name": "ac_cv_header_stdio_ext_h/ac_cv_have_decl___fpending",
        "value": "yes/yes",
        "reason": "guest libc provides stdio_ext entry points for gnulib",
        "test": "waste-libc exports the declarations; no host FILE layout is used",
    },
    {
        "name": "ac_cv_func__set_invalid_parameter_handler",
        "value": "no",
        "reason": "the guest has no MSVC invalid-parameter runtime",
        "test": "freestanding wasm32 target; no Windows runtime is linked",
    },
    {
        "name": "ac_cv_func_sched_getaffinity/ac_cv_func_sched_getaffinity_np/ac_cv_func_pstat_getdynamic/ac_cv_func_pstat_getstatic/ac_cv_func_pstat_getprocvm",
        "value": "no/no/no/no/no",
        "reason": "the guest kernel does not expose host CPU-affinity or HP-UX pstat APIs",
        "test": "engine-owned process ABI; unsupported host probes are disabled",
    },
]


def rel(path: Path, root: Path) -> str:
    return path.resolve().relative_to(root.resolve()).as_posix()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--sysroot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.repo_root.resolve()
    source = args.source.resolve()
    sysroot = args.sysroot.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    site = output / "config.site"
    shutil.copyfile(root / CONFIG_SITE, site)

    prerequisite_names = (
        "autoconf", "automake", "autopoint", "bison", "gettext", "git",
        "gperf", "gzip", "libtoolize", "m4", "makeinfo", "patch", "perl",
        "tar", "texi2pdf", "wget", "xz",
    )
    prerequisites = {
        name: bool(shutil.which(name)) for name in prerequisite_names
    }
    configure = source / "configure"
    report = {
        "format": 1,
        "status": "ready" if configure.is_file() else "blocked",
        "source": rel(source, root),
        "build": rel(output, root),
        "sysroot": rel(sysroot, root),
        "target": "wasm32-unknown-none",
        "compiler": rel(sysroot / "bin" / "waste-wasm-clang", root),
        "config_site": rel(site, root),
        "forced_answers": FORCED_ANSWERS,
        "bootstrap_prerequisites": prerequisites,
        "configure_present": configure.is_file(),
        "configure_command": [
            "configure", "--host=wasm32-unknown-none",
            "--disable-nls", "--disable-gcc-warnings", "--disable-year2038",
        ],
        "configure_log": rel(output / "configure.log", root),
        "blockers": [],
    }
    if not configure.is_file():
        report["blockers"].append(
            "generated configure script is absent; bootstrap must run in the staged source"
        )
    missing = [name for name, present in prerequisites.items() if not present]
    if missing:
        report["blockers"].append(
            "bootstrap prerequisites unavailable: " + ", ".join(missing)
        )

    if configure.is_file() and not report["blockers"]:
        environment = os.environ.copy()
        environment.update({
            "CONFIG_SITE": str(site),
            "CC": str(sysroot / "bin" / "waste-wasm-clang"),
            "CPP": str(sysroot / "bin" / "waste-wasm-clang") + " -E",
        })
        command = [
            str(configure), "--host=wasm32-unknown-none",
            "--disable-nls", "--disable-gcc-warnings", "--disable-year2038",
        ]
        with (output / "configure.log").open("w", encoding="utf-8") as log:
            result = subprocess.run(command, cwd=output, env=environment,
                                    stdout=log, stderr=subprocess.STDOUT,
                                    check=False)
        report["status"] = "passed" if result.returncode == 0 else "failed"
        report["returncode"] = result.returncode

    (output / "configure-report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["status"] in ("ready", "passed", "blocked") else 1


if __name__ == "__main__":
    raise SystemExit(main())
