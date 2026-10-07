#!/usr/bin/env python3
"""Build the ldd utility for wasm32.

Produces ldd.wasm, a small executable that reads a wasm binary's import
section, groups imports by module name, resolves each module against
/usr/lib, and prints a dependency listing in standard ldd format.
"""

from __future__ import annotations

import argparse
import subprocess
import shutil
import sys
from pathlib import Path

from shared_libc import rewrite, review_flags


LDD_SOURCE = r"""
/* ldd.c — List dynamic dependencies of a WebAssembly binary.
 *
 * Reads the target's import section, collects unique module names,
 * resolves each against /usr/lib, and prints in standard ldd format.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* Maximum number of unique module dependencies we track. */
#define MAX_DEPS 64
/* Maximum module name length. */
#define MAX_NAME 128

struct dep {
    char name[MAX_NAME];
    int count;
};

static struct dep deps[MAX_DEPS];
static int ndeps;

static int add_dep(const char *name, int len) {
    if (len >= MAX_NAME) len = MAX_NAME - 1;
    for (int i = 0; i < ndeps; i++) {
        if ((int)strlen(deps[i].name) == len &&
            memcmp(deps[i].name, name, len) == 0) {
            deps[i].count++;
            return i;
        }
    }
    if (ndeps >= MAX_DEPS) return -1;
    memcpy(deps[ndeps].name, name, len);
    deps[ndeps].name[len] = '\0';
    deps[ndeps].count = 1;
    return ndeps++;
}

/* A failed bounded read poisons the cursor; callers reject the section. */
static unsigned int read_leb128(const unsigned char *data, int size, int *pos) {
    unsigned int value = 0;
    for (int shift = 0; shift <= 28 && *pos < size; shift += 7) {
        unsigned char b = data[(*pos)++];
        if (shift == 28 && (b & 0xf0)) break;
        value |= (unsigned int)(b & 0x7f) << shift;
        if (!(b & 0x80)) return value;
    }
    *pos = size + 1;
    return 0;
}

static int skip_import_desc(const unsigned char *data, int size,
                            int *pos, int kind) {
    if (kind == 0) read_leb128(data, size, pos);
    else if (kind == 3) {
        if (size - *pos < 2) return -1;
        *pos += 2;
    } else if (kind == 1 || kind == 2) {
        if (kind == 1) {
            if (*pos >= size || data[(*pos)++] != 0x70) return -1;
        }
        unsigned int flags = read_leb128(data, size, pos);
        if (flags > 1) return -1;
        read_leb128(data, size, pos);
        if (flags & 1) read_leb128(data, size, pos);
    } else return -1;
    return *pos <= size ? 0 : -1;
}

static int parse_imports(const unsigned char *data, int size) {
    if (size < 8 || memcmp(data, "\0asm\1\0\0\0", 8)) return -1;
    int pos = 8;
    while (pos < size) {
        int section_id = data[pos++];
        unsigned int section_size = read_leb128(data, size, &pos);
        if (pos > size || section_size > (unsigned int)(size - pos)) return -1;
        int end = pos + (int)section_size;
        if (section_id == 2) {
            unsigned int count = read_leb128(data, end, &pos);
            for (unsigned int i = 0; i < count && pos <= end; i++) {
                unsigned int len = read_leb128(data, end, &pos);
                if (pos > end || len >= MAX_NAME || len > (unsigned int)(end-pos)) return -1;
                if (add_dep((const char *)data + pos, (int)len) < 0) return -1;
                pos += (int)len;
                len = read_leb128(data, end, &pos);
                if (pos > end || len > (unsigned int)(end-pos)) return -1;
                pos += (int)len;
                if (pos >= end) return -1;
                int kind = data[pos++];
                if (skip_import_desc(data, end, &pos, kind)) return -1;
            }
            return pos == end ? 0 : -1;
        }
        pos = end;
    }
    return 0;
}

static int scan_file(const char *target) {
    struct stat st;
    int fd = open(target, O_RDONLY);
    if (fd < 0) return -1;
    if (fstat(fd, &st) || st.st_size < 8 || st.st_size > 64 * 1024 * 1024) {
        close(fd);
        return -1;
    }
    int size = (int)st.st_size;
    unsigned char *data = malloc(size);
    if (!data) { close(fd); return -1; }
    int total = 0;
    while (total < size) {
        int n = read(fd, data + total, size - total);
        if (n <= 0) break;
        total += n;
    }
    close(fd);
    int status = total == size ? parse_imports(data, size) : -1;
    free(data);
    return status;
}

static int try_resolve(const char *name, char *path, int pathsz) {
    /* Try /usr/lib/<name>.so.wasm */
    snprintf(path, pathsz, "/usr/lib/%s.so.wasm", name);
    if (access(path, F_OK) == 0) return 1;
    /* Try /usr/lib/<name>.wasm */
    snprintf(path, pathsz, "/usr/lib/%s.wasm", name);
    if (access(path, F_OK) == 0) return 1;
    /* Try /usr/lib/lib<name>.so.wasm */
    snprintf(path, pathsz, "/usr/lib/lib%s.so.wasm", name);
    if (access(path, F_OK) == 0) return 1;
    /* Try /lib/<name>.so.wasm */
    snprintf(path, pathsz, "/lib/%s.so.wasm", name);
    if (access(path, F_OK) == 0) return 1;
    return 0;
}

static int is_internal(const char *name) {
    /* PIC relocation modules — internal, not real dependencies. */
    if (strcmp(name, "GOT.mem") == 0) return 1;
    if (strcmp(name, "GOT.func") == 0) return 1;
    return 0;
}

static int is_builtin(const char *name) {
    /* Modules provided by the runtime, not loadable libraries. */
    if (strcmp(name, "env") == 0) return 1;
    if (strcmp(name, "waste_kernel") == 0) return 1;
    if (strcmp(name, "waste-runtime") == 0) return 1;
    if (strcmp(name, "wasi_snapshot_preview1") == 0) return 1;
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc < 2) {
        fprintf(stderr, "usage: ldd <wasm-binary>\n");
        return 1;
    }

    const char *target = argv[1];
    if (scan_file(target)) {
        fprintf(stderr, "\tldd: %s: cannot read Wasm imports\n", target);
        return 1;
    }

    if (ndeps == 0) {
        printf("\tstatically linked\n");
        return 0;
    }

    char path[256];
    int unresolved = 0;

    for (int i = 0; i < ndeps; i++) {
        if (is_internal(deps[i].name))
            continue;

        if (is_builtin(deps[i].name)) {
            printf("\t%s (%d imports, built-in)\n",
                   deps[i].name, deps[i].count);
            continue;
        }

        if (try_resolve(deps[i].name, path, sizeof(path))) {
            printf("\t%s => %s (%d imports)\n",
                   deps[i].name, path, deps[i].count);
            /* Newly discovered names extend this worklist. Unique names
             * bound the closure and terminate cycles without loading code. */
            if (scan_file(path)) {
                fprintf(stderr, "\tldd: %s: cannot read Wasm imports\n", path);
                unresolved++;
            }
        } else {
            printf("\t%s => not found (%d imports)\n",
                   deps[i].name, deps[i].count);
            unresolved++;
        }
    }

    return unresolved ? 1 : 0;
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", action="store_true", help="Explicitly install audited outputs into src/vfs")
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path,
                        default=Path("build/ldd"))
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    # Write source.
    src = output / "ldd.c"
    src.write_text(LDD_SOURCE.lstrip(), encoding="utf-8")

    # Ensure sysroot.
    sysroot = ensure_sysroot(repo_root, output)
    cc = str(sysroot / "bin" / "waste-wasm-clang")
    print(f"sysroot: {sysroot}")

    # Compile.
    obj = output / "ldd.o"
    cmd = [cc, "-c", "-O2", "-DNDEBUG", "-fPIC",
           str(src), "-o", str(obj)]
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        print(f"compile failed:\n{result.stderr}")
        return 1
    print("compile: ldd.o ok")

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

    # Link.
    ldd_wasm = output / "ldd.wasm"
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
        # External-image data must not overlap the resident Bash/libc region.
        "--global-base=327680",
        "--table-base=1024",
        "-o", str(ldd_wasm),
        str(crt_obj),
        str(obj),
    ]
    result = subprocess.run(link_cmd, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        print(f"link failed:\n{result.stderr}")
        return 1
    rewrite(ldd_wasm, repo_root / "src/vfs/lib/libc.so.wasm")
    size = ldd_wasm.stat().st_size
    print(f"link: ldd.wasm ({size} bytes)")

    # Count imports.
    dis = subprocess.run(
        ["wasm-dis", str(ldd_wasm)],
        capture_output=True, text=True, check=False,
    )
    if dis.returncode == 0:
        imports = [l for l in dis.stdout.splitlines() if "(import" in l]
        print(f"imports: {len(imports)}")

    # Stage for VFS.
    vfs_stage = output / "vfs"
    vfs_stage.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ldd_wasm, vfs_stage / "ldd")
    print(f"\nstaged: {vfs_stage / 'ldd'}")
    print("VFS path: /usr/bin/ldd")
    if args.install:
        subprocess.run(["python3", str(repo_root / "src/html-rt/tools/vfs.py"),
                        "install", "--component", "ldd", "--source",
                        str(vfs_stage / "ldd"),
                        *review_flags([vfs_stage / "ldd"], repo_root / "src/vfs/lib/libc.so.wasm"), "--review-import", "env:fstat:function"], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
