#!/usr/bin/env python3
"""Compile one staged coreutils utility and classify the first ABI boundary."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

from wasm_signatures import inspect


MISSING_HEADER = re.compile(r"fatal error: ['\"]([^'\"]+)['\"] file not found")
UNDEFINED_SYMBOL = re.compile(
    r"(?:undefined symbol|undefined reference to)[^A-Za-z0-9_]*['\"]?"
    r"([A-Za-z_][A-Za-z0-9_]*)"
)

WASTE_STARTUP_IMPORTS = ("env:exit", "env:_exit", "env:abort", "env:atexit",
                         "waste_kernel:startup_v1")
WASTE_RUNTIME_IMPORTS = (
    "env:close", "env:fstat", "env:open", "env:read", "env:readdir_v1",
    "env:write",
    "env:gettimeofday", "env:raise",
    "env:sigaction", "env:sigaddset", "env:sigemptyset",
    "env:sigismember", "env:sigprocmask",
    "waste_kernel:chdir", "waste_kernel:fchdir_v1",
    "waste_kernel:fcntl_v1", "waste_kernel:getcwd",
    "waste_kernel:lseek", "waste_kernel:readlink_v1",
    "waste_kernel:ioctl_v1", "waste_kernel:isatty_v1",
    "waste_kernel:path_access_v1", "waste_kernel:path_stat_v1",
    "waste_kernel:pselect_v1", "waste_kernel:select_v1",
    "waste_kernel:open_v1", "waste_kernel:pipe_v1",
    "waste_kernel:realtime_v1",
    # Shared guest-libc now retains its already implemented dl* wrappers.
    # These versioned imports are bound by the existing browser kernel loader.
    "waste_kernel:dlopen_v1", "waste_kernel:dlsym_v1", "waste_kernel:dlclose_v1",
    "waste_kernel:tcgetattr_v1", "waste_kernel:tcsetattr_v1",
)
RUNTIME_BUILDER = "src/html-rt/tools/build-coreutils-runtime.py"

LS_FEATURE_LEDGER = {
    "supported": [
        "default terminal column output",
        "default non-terminal one-name-per-line output",
        "-1 one-name-per-line output",
        "-A hidden-name output excluding dot and dot-dot",
        "-l long format with file type, mode, ownership, size, and symlink target",
        "multiple directory operands",
        "missing-path diagnostics and status 2",
    ],
    "browser_acceptance": [
        "/ and /bin directory listings",
        "empty directory",
        "hidden regular file",
        "symbolic link and link target",
        "terminal and redirected stdout layouts",
        "child-created redirected output visible to the parent shell",
        "second command after success and failure",
    ],
    "intentional_deviations": [
        "collation is bytewise because the guest locale model has no locale-specific strcoll data",
        "owner and group names are not claimed until account-name lookup is covered by the ls gate",
        "color and locale-specific quoting are outside the accepted option subset",
        "timestamps are deterministic engine metadata rather than host filesystem timestamps",
    ],
}


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
    generated_targets = list(generated_headers)
    generated_paths = [build / name for name in generated_headers]
    if args.utility == "ls":
        # dircolors.h is generated into the source tree by the native dcgen
        # build helper, so its Make target is the expanded absolute path.
        generated_headers.append("src/dircolors.h")
        dircolors_header = coreutils_build / "source/src/dircolors.h"
        generated_targets.append(str(dircolors_header))
        generated_paths.append(dircolors_header)
    preflight = ["make", "-C", str(build), "-o", "config.status",
                 "V=1", "-j1"] + generated_targets
    missing_generated = [name for name, path in zip(generated_headers, generated_paths)
                         if not path.is_file()]
    command = ["make", "-C", str(build), "-o", "config.status",
               "V=1", "-j1", f"src/{args.utility}"]
    artifact = build / "src" / args.utility
    crt_object = coreutils_build / "sysroot" / "lib" / "waste-crt.o"
    crt_source = root / "src/libc/waste-crt.c"
    audit_path = output.parent / f"{args.utility}-import-audit.json"
    linked_artifact = output.parent / f"{args.utility}-linked.wasm"
    linked_audit_path = output.parent / f"{args.utility}-linked-import-audit.json"
    link_flags = ("-Wl,--no-entry,--import-memory,--import-table,--export=_start,"
                  "--export=__stack_pointer,--export=__heap_base,"
                  "--allow-undefined,--gc-sections,--global-base=655360,--table-base=1024,"
                  "--initial-memory=1048576,"
                  "-z,stack-size=524288")
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
                [str(coreutils_build / "sysroot/bin/waste-coreutils-clang"), "-c",
                 "-DWASTE_MAIN_TWO_ARGS", str(crt_source), "-o", str(crt_object)],
                stdout=log, stderr=subprocess.STDOUT, check=False,
            )
            if crt_result.returncode != 0:
                result = crt_result
            else:
                # First, let make build everything (including the archive)
                # normally.  Then strip conflicting gnulib objects and
                # manually relink.
                artifact.unlink(missing_ok=True)
                result = subprocess.run(
                    command + [
                        f"LDFLAGS={link_flags}",
                        f"LIBS={crt_object}",
                    ], stdout=log, stderr=subprocess.STDOUT, check=False,
                )
                # gnulib's fcntl.c defines a replacement fcntl() that
                # internally calls the "real" fcntl (via #undef).  In
                # WASTE there is no real system fcntl—the guest libc
                # provides one via a kernel import—so gnulib's version
                # recurses infinitely.  Remove it from libcoreutils.a.
                #
                # gnulib's *zprintf family (vfzprintf, vzprintf, etc.)
                # routes through vasnprintf which crashes in the Wasm
                # environment due to unresolved internal data.  Remove
                # them so the waste-libc implementations win instead.
                #
                # Strip AFTER make finishes (make rebuilds the archive,
                # undoing any earlier strip), then manually relink.
                libcoreutils = build / "lib" / "libcoreutils.a"
                gnulib_strip = [
                    "libcoreutils_a-fcntl.o",
                    "libcoreutils_a-open.o",
                    "libcoreutils_a-stat.o",
                    "libcoreutils_a-lstat.o",
                    "libcoreutils_a-fstatat.o",
                    "libcoreutils_a-localeconv.o",
                    "libcoreutils_a-vfzprintf.o",
                    "libcoreutils_a-vzprintf.o",
                    "libcoreutils_a-vsnzprintf.o",
                    "libcoreutils_a-vszprintf.o",
                    "libcoreutils_a-vaszprintf.o",
                    "libcoreutils_a-aszprintf.o",
                    "libcoreutils_a-vasnprintf.o",
                    "libcoreutils_a-vasprintf.o",
                ]
                if result.returncode == 0 and libcoreutils.is_file():
                    for obj in gnulib_strip:
                        subprocess.run(
                            ["ar", "d", str(libcoreutils), obj],
                            stdout=log, stderr=subprocess.STDOUT,
                            check=False,
                        )
                    # Relink after stripping conflicting objects.
                    # Do NOT link a separate allocator — waste-libc
                    # provides malloc/free/calloc/realloc.
                    cc = str(coreutils_build / "sysroot/bin/waste-coreutils-clang")
                    utility_objects = [build / "src" / f"{args.utility}.o"]
                    if args.utility in ("true", "false"):
                        # Coreutils builds true and false from their shared
                        # implementation plus a command-specific selector.
                        utility_objects = [
                            build / "src" / "true.o",
                            build / "src" / f"true-{args.utility}.o",
                        ]
                    elif args.utility == "date":
                        utility_objects.append(build / "src" / "show-date.o")
                    relink = subprocess.run(
                        [cc, "-std=gnu23", link_flags,
                         "-o", str(artifact),
                         *(str(path) for path in utility_objects),
                         str(build / "src" / "libver.a"),
                         str(libcoreutils), str(libcoreutils),
                         str(crt_object)],
                        stdout=log, stderr=subprocess.STDOUT, check=False,
                    )
                    if relink.returncode != 0:
                        result = relink
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
            "--libc", str(root / "src/vfs/lib/libc.so.wasm"),
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
                       WASTE_STARTUP_IMPORTS + WASTE_RUNTIME_IMPORTS
                       + ("env:memory", "env:__indirect_function_table")
                       + tuple("libc:" + name for name in inspect(root / "src/vfs/lib/libc.so.wasm")["exports"])), [])],
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
        "feature_ledger": LS_FEATURE_LEDGER if args.utility == "ls" else None,
        "blockers": blockers,
        "next_stage": "8F" if status == "blocked" else None,
    }
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if status in ("passed", "blocked") else 1


if __name__ == "__main__":
    raise SystemExit(main())
