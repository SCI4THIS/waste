#!/usr/bin/env python3
"""Build the upload and download utilities for wasm32.

Produces upload.wasm and download.wasm — small executables that bridge
the browser VFS and host OS via browser file dialogs.  The guest
programs call waste_kernel host functions that yield the engine so the
browser can show a file picker (upload) or trigger a download.
"""

from __future__ import annotations

import argparse
import subprocess
import shutil
import sys
from pathlib import Path


UPLOAD_SOURCE = r"""
/* upload.c — Copy a host file into the browser VFS via a file picker.
 *
 * Calls the waste_kernel host_upload_v1 function which yields the
 * engine, triggers a browser file picker on the main thread, and
 * resumes with the selected file data written to the VFS.
 *
 * Usage: upload [--verbose] <dest-path>
 */

#include <stdio.h>
#include <string.h>

__attribute__((import_module("waste_kernel"), import_name("host_upload_v1")))
int host_upload_v1(const char *path, int path_len, int flags);

int main(int argc, char **argv, char **envp) {
    (void)envp;
    int verbose = 0;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (strcmp(argv[argi], "--verbose") == 0) { verbose = 1; argi++; }
        else break;
    }
    if (argi >= argc) {
        fprintf(stderr, "usage: upload [--verbose] <dest-path>\n");
        return 1;
    }
    const char *path = argv[argi];
    int flags = verbose ? 1 : 0;
    if (verbose) fprintf(stderr, "upload: requesting upload to '%s' (flags=%d)\n", path, flags);
    int result = host_upload_v1(path, (int)strlen(path), flags);
    if (result < 0) {
        fprintf(stderr, "upload: cancelled\n");
        return 1;
    }
    printf("uploaded %d bytes to %s\n", result, path);
    return 0;
}
"""

DOWNLOAD_SOURCE = r"""
/* download.c — Download a VFS file to the host via a browser save dialog.
 *
 * Reads the file from the VFS using standard POSIX calls, then calls
 * the waste_kernel host_download_v1 function which yields the engine
 * and triggers a browser download on the main thread.
 *
 * Usage: download [--verbose] <file>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

__attribute__((import_module("waste_kernel"), import_name("host_download_v1")))
int host_download_v1(const char *name, int name_len,
                     const void *data, int data_len, int flags);

int main(int argc, char **argv, char **envp) {
    (void)envp;
    int verbose = 0;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (strcmp(argv[argi], "--verbose") == 0) { verbose = 1; argi++; }
        else break;
    }
    if (argi >= argc) {
        fprintf(stderr, "usage: download [--verbose] <file>\n");
        return 1;
    }

    const char *filepath = argv[argi];
    if (verbose) fprintf(stderr, "download: opening '%s'\n", filepath);

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "download: %s: not found\n", filepath);
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        fprintf(stderr, "download: %s: cannot stat\n", filepath);
        close(fd);
        return 1;
    }

    int file_size = (int)st.st_size;
    if (verbose) fprintf(stderr, "download: file size = %d bytes\n", file_size);
    char *buf = NULL;
    if (file_size > 0) {
        buf = (char *)malloc(file_size);
        if (!buf) {
            fprintf(stderr, "download: out of memory\n");
            close(fd);
            return 1;
        }
        int total = 0;
        while (total < file_size) {
            int n = read(fd, buf + total, file_size - total);
            if (n <= 0) break;
            total += n;
        }
        file_size = total;
    }
    close(fd);

    /* Extract basename for the download filename. */
    const char *name = strrchr(filepath, '/');
    name = name ? name + 1 : filepath;

    int flags = verbose ? 1 : 0;
    if (verbose) fprintf(stderr, "download: calling host_download_v1('%s', %d bytes, flags=%d)\n", name, file_size, flags);
    int result = host_download_v1(name, (int)strlen(name),
                                  buf ? buf : "", file_size, flags);
    free(buf);

    if (result < 0) {
        fprintf(stderr, "download: failed\n");
        return 1;
    }
    if (verbose) fprintf(stderr, "download: complete\n");
    return 0;
}
"""


def ensure_sysroot(repo_root: Path, output_base: Path) -> Path:
    """Refresh from the mounted SDK; never reuse an unchecked old sysroot."""
    build_sysroot = repo_root / "src" / "html-rt" / "tools" / "build-waste-sysroot.py"
    sysroot = output_base / "sysroot"
    subprocess.run(
        [sys.executable, str(build_sysroot),
         "--repo-root", str(repo_root),
         "--output", str(sysroot)],
        check=True,
    )
    return sysroot


def build_utility(name: str, source: str, cc: str, crt_obj: Path,
                  output: Path) -> bool:
    """Compile and link a single utility. Returns True on success."""
    src = output / f"{name}.c"
    src.write_text(source.lstrip(), encoding="utf-8")

    obj = output / f"{name}.o"
    cmd = [cc, "-c", "-O2", "-DNDEBUG", "-fPIC", str(src), "-o", str(obj)]
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        print(f"compile {name} failed:\n{result.stderr}")
        return False
    print(f"compile: {name}.o ok")

    wasm = output / f"{name}.wasm"
    link_cmd = [
        "wasm-ld",
        "--no-entry",
        "--import-memory",
        "--import-table",
        "--export=_start",
        "--export=main",
        "--export=__heap_base",
        "--export=__data_end",
        "--experimental-pic",
        "--unresolved-symbols=import-dynamic",
        "--global-base=327680",
        "--table-base=1024",
        "-o", str(wasm),
        str(crt_obj),
        str(obj),
    ]
    result = subprocess.run(link_cmd, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        print(f"link {name} failed:\n{result.stderr}")
        return False
    size = wasm.stat().st_size
    print(f"link: {name}.wasm ({size} bytes)")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", action="store_true", help="Explicitly install audited outputs into src/vfs")
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path,
                        default=Path("build/upload-download"))
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    sysroot = ensure_sysroot(repo_root, output)
    cc = str(sysroot / "bin" / "waste-wasm-clang")
    print(f"sysroot: {sysroot}")

    # Build CRT.
    crt_src = sysroot / "lib" / "waste-crt.c"
    crt_obj = output / "waste-crt.o"
    result = subprocess.run(
        [cc, "-c", "-O2", "-fPIC", str(crt_src), "-o", str(crt_obj)],
        capture_output=True, text=True, check=False,
    )
    if result.returncode != 0:
        print(f"CRT compile failed:\n{result.stderr}")
        return 1
    print("compile: waste-crt.o ok")

    ok = True
    ok = build_utility("upload", UPLOAD_SOURCE, cc, crt_obj, output) and ok
    ok = build_utility("download", DOWNLOAD_SOURCE, cc, crt_obj, output) and ok

    if not ok:
        return 1

    # Stage for VFS.
    vfs_stage = output / "vfs"
    vfs_stage.mkdir(parents=True, exist_ok=True)
    for name in ("upload", "download"):
        shutil.copyfile(output / f"{name}.wasm", vfs_stage / name)
        print(f"staged: {vfs_stage / name}")

    print(f"\nVFS paths: /bin/upload, /bin/download")
    if args.install:
        subprocess.run(["python3", str(repo_root / "src/html-rt/tools/vfs.py"),
                        "install", "--component", "upload", "--source",
                        str(vfs_stage / "upload"), "--component", "download", "--source",
                        str(vfs_stage / "download")], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
