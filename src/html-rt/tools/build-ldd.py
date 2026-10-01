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

static unsigned int read_leb128(const unsigned char *data, int size, int *pos) {
    unsigned int value = 0;
    int shift = 0;
    while (*pos < size) {
        unsigned char b = data[*pos]; (*pos)++;
        value |= (unsigned int)(b & 0x7f) << shift;
        shift += 7;
        if (!(b & 0x80)) break;
    }
    return value;
}

/* Skip a type descriptor in the import section (varies by kind). */
static void skip_import_desc(const unsigned char *data, int size,
                             int *pos, int kind) {
    if (kind == 0) {
        /* function: typeidx */
        read_leb128(data, size, pos);
    } else if (kind == 1) {
        /* table: elemtype + limits */
        read_leb128(data, size, pos);  /* elem type */
        unsigned int flags = read_leb128(data, size, pos);
        read_leb128(data, size, pos);  /* min */
        if (flags & 1) read_leb128(data, size, pos);  /* max */
    } else if (kind == 2) {
        /* memory: limits */
        unsigned int flags = read_leb128(data, size, pos);
        read_leb128(data, size, pos);  /* min */
        if (flags & 1) read_leb128(data, size, pos);  /* max */
    } else if (kind == 3) {
        /* global: type + mutability */
        read_leb128(data, size, pos);
        read_leb128(data, size, pos);
    }
}

static int parse_imports(const unsigned char *data, int size) {
    if (size < 8) return -1;
    /* Check magic: \0asm */
    if (data[0] != 0x00 || data[1] != 0x61 ||
        data[2] != 0x73 || data[3] != 0x6d) {
        return -1;
    }

    int pos = 8;  /* skip magic + version */
    while (pos < size) {
        int section_id = data[pos]; pos++;
        unsigned int section_size = read_leb128(data, size, &pos);
        int section_end = pos + (int)section_size;
        if (section_end > size) break;

        if (section_id == 2) {  /* import section */
            unsigned int count = read_leb128(data, size, &pos);
            for (unsigned int i = 0; i < count && pos < section_end; i++) {
                unsigned int mod_len = read_leb128(data, size, &pos);
                if (pos + (int)mod_len > section_end) break;
                add_dep((const char *)data + pos, (int)mod_len);
                pos += (int)mod_len;

                /* skip import name */
                unsigned int name_len = read_leb128(data, size, &pos);
                pos += (int)name_len;

                /* skip kind + descriptor */
                if (pos < section_end) {
                    int kind = data[pos]; pos++;
                    skip_import_desc(data, size, &pos, kind);
                }
            }
            break;  /* only need the import section */
        }
        pos = section_end;
    }
    return 0;
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
    struct stat st;
    if (stat(target, &st) != 0) {
        fprintf(stderr, "\tldd: %s: not found\n", target);
        return 1;
    }

    int file_size = (int)st.st_size;
    if (file_size < 8) {
        fprintf(stderr, "\tldd: %s: too small to be a wasm binary\n", target);
        return 1;
    }

    unsigned char *data = (unsigned char *)malloc(file_size);
    if (!data) {
        fprintf(stderr, "\tldd: out of memory\n");
        return 1;
    }

    int fd = open(target, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "\tldd: %s: cannot open\n", target);
        free(data);
        return 1;
    }

    int total = 0;
    while (total < file_size) {
        int n = read(fd, data + total, file_size - total);
        if (n <= 0) break;
        total += n;
    }
    close(fd);

    if (total < 8) {
        fprintf(stderr, "\tldd: %s: read error\n", target);
        free(data);
        return 1;
    }

    if (parse_imports(data, total) != 0) {
        fprintf(stderr, "\tldd: %s: not a valid wasm binary\n", target);
        free(data);
        return 1;
    }
    free(data);

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
    """Build or locate the WASTE application sysroot."""
    for candidate in [
        output_base / "sysroot",
        repo_root / "build" / "coreutils" / "sysroot",
    ]:
        if (candidate / "bin" / "waste-wasm-clang").is_file():
            return candidate
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
