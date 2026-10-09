/* Discover the current Linux directory for both native C runtimes. Host
 * filesystem calls stay in cli-rt; the engine receives owned node records. */
#include "native_vfs.h"
#include "lib/include/syscall.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define HOST_DIRECTORY 00200000
#define HOST_NOFOLLOW 00400000
#define HOST_CLOEXEC 02000000
#define HOST_NONBLOCK 00004000

static int open_at(int fd, const char *name, int directory) {
    return (int)syscall6(SYS_openat, fd, (long)name,
        HOST_NOFOLLOW | HOST_CLOEXEC | HOST_NONBLOCK | (directory ? HOST_DIRECTORY : 0), 0, 0, 0);
}

/* Allowlisted entries that may be symbolic links. The blessed symlinks serve
 * live .wast files from checked-out source trees; everywhere else O_NOFOLLOW
 * stays enforced. Keep in sync with BLESSED_SYMLINKS in src/html-rt/tools/vfs.py. */
static int is_blessed_symlink(const char *parent, const char *name) {
    if (strcmp(parent, "/root/test/wasm-spec")) return 0;
    return !strcmp(name, "core") || !strcmp(name, "custom");
}

static int open_blessed(int fd, const char *name) {
    return (int)syscall6(SYS_openat, fd, (long)name,
        HOST_CLOEXEC | HOST_NONBLOCK | HOST_DIRECTORY, 0, 0, 0);
}

static int wast_leaf(const char *name) {
    size_t length = strlen(name);
    return length >= 5 && !memcmp(name + length - 5, ".wast", 5);
}

int native_vfs_open_root(const char *root) {
    return (int)sys_open(root, HOST_DIRECTORY | HOST_CLOEXEC, 0);
}

/* Read a regular file from its already confined descriptor. */
static uint8_t *read_file(int fd, size_t limit, size_t *length,
                          struct __kernel_stat *info) {
    struct __kernel_stat before, after;
    if (sys_fstat(fd, &before) || (before.st_mode & 0170000) != 0100000 ||
        before.st_size < 0 || (uint64_t)before.st_size > limit) return NULL;
    size_t size = (size_t)before.st_size, at = 0;
    uint8_t *bytes = malloc(size + 1u);
    if (!bytes) return NULL;
    while (at < size) {
        long n = sys_read(fd, bytes + at, size - at);
        if (n == -4 /* EINTR */) continue;
        if (n <= 0) goto invalid;
        at += (size_t)n;
    }
    uint8_t extra;
    long tail;
    do { tail = sys_read(fd, &extra, 1); } while (tail == -4);
    if (tail || sys_fstat(fd, &after) || before.st_size != after.st_size ||
        before.st_mtime_sec != after.st_mtime_sec || before.st_mtime_nsec != after.st_mtime_nsec)
        goto invalid;
    bytes[size] = 0;
    *length = size;
    *info = after;
    return bytes;
invalid:
    free(bytes);
    return NULL;
}

typedef struct {
    waste_vfs vfs;
    char path[POSIX_PATH_NODE_NAME_MAX];
} tree_reader;

static int add_node(tree_reader *reader, const char *path,
                     const struct __kernel_stat *info, uint8_t *bytes, size_t length) {
    posix_path_metadata metadata = {0};
    metadata.kind = (info->st_mode & 0170000) == 0040000 ? POSIX_NODE_DIRECTORY : POSIX_NODE_REGULAR;
    metadata.mode = info->st_mode & 0777;
    /* Guest ownership is root; host ownership never leaks into the guest. */
    metadata.inode = reader->vfs.count + 1u;
    metadata.size = length;
    metadata.mtime_sec = (int64_t)info->st_mtime_sec;
    metadata.mtime_nsec = (uint32_t)info->st_mtime_nsec;
    return waste_vfs_append(&reader->vfs, path, &metadata, bytes, length, 0);
}

/* Linux getdents64 records are bounded before reading any component. Opening
 * every child relative to its parent with O_NOFOLLOW confines traversal to the
 * selected tree, including intermediate directory components. The blessed-mode
 * flag is set once we enter the resolved wasm-spec subtree: it filters files
 * to .wast and suppresses directory entries that contain no retained children. */
static int read_directory(tree_reader *reader, int fd, const char *parent, int blessed_mode) {
    unsigned char records[4096];
    for (;;) {
        long n = syscall3(SYS_getdents64, fd, (long)records, sizeof(records));
        if (n == -4) continue;
        if (n < 0) return -1;
        if (!n) return 0;
        size_t at = 0;
        while (at < (size_t)n) {
            uint16_t size;
            if ((size_t)n - at < 20) return -1;
            memcpy(&size, records + at + 16, sizeof(size));
            if (size < 20 || size > (size_t)n - at) return -1;
            const char *name = (const char *)records + at + 19;
            size_t length = 0;
            while (length < size - 19u && name[length]) length++;
            if (!length || length == size - 19u) return -1;
            at += size;
            if (!strcmp(name, ".") || !strcmp(name, "..") ||
                (!strcmp(parent, "/") && !strcmp(name, ".inventory.json"))) continue;
            char path[POSIX_PATH_NODE_NAME_MAX];
            int used = snprintf(path, sizeof(path), "%s%s%s", parent,
                                !strcmp(parent, "/") ? "" : "/", name);
            if (used < 0 || (size_t)used >= sizeof(path)) return -1;
            strcpy(reader->path, path);
            int blessed_link = is_blessed_symlink(parent, name);
            int child = blessed_link ? open_blessed(fd, name) : open_at(fd, name, 0);
            if (child < 0) {
                /* Blessed symlink target may be absent when the submodule is
                 * not checked out; skip silently rather than failing the load. */
                if (blessed_link) continue;
                return -1;
            }
            struct __kernel_stat info;
            int status = -1;
            if (!sys_fstat(child, &info)) {
                unsigned kind = info.st_mode & 0170000;
                if (kind == 0040000) {
                    if (blessed_mode || blessed_link) {
                        /* Add the directory first so descendants can validate
                         * their parent; drop it afterwards if no .wast files
                         * were retained anywhere beneath it. */
                        uint32_t before = reader->vfs.count;
                        status = add_node(reader, path, &info, NULL, 0);
                        if (!status) {
                            status = read_directory(reader, child, path, 1);
                            if (!status && reader->vfs.count == before + 1u)
                                reader->vfs.count = before;
                        }
                    } else {
                        status = add_node(reader, path, &info, NULL, 0);
                        if (!status) status = read_directory(reader, child, path, 0);
                    }
                } else if (kind == 0100000) {
                    if (blessed_mode && !wast_leaf(name)) {
                        status = 0;
                    } else {
                        size_t bytes_length = 0;
                        uint8_t *bytes = read_file(child, WASTE_VFS_MAX_BYTES - reader->vfs.bytes,
                                                   &bytes_length, &info);
                        if (bytes) {
                            status = add_node(reader, path, &info, bytes, bytes_length);
                            if (status) free(bytes);
                        }
                    }
                }
            }
            sys_close(child);
            if (status) return -1;
        }
    }
}

static int add_virtual(tree_reader *reader, const char *path, int directory, uint32_t mode) {
    for (uint32_t i = 0; i < reader->vfs.count; i++)
        if (!strcmp(path, reader->vfs.entries[i].path)) return 0;
    posix_path_metadata metadata = {0};
    metadata.kind = directory ? POSIX_NODE_DIRECTORY : POSIX_NODE_REGULAR;
    metadata.mode = mode;
    metadata.inode = reader->vfs.count + 1u;
    strcpy(reader->path, path);
    return waste_vfs_append(&reader->vfs, path, &metadata, NULL, 0, !directory);
}

static unsigned depth(const char *path) {
    unsigned n = 0;
    for (; *path; path++) if (*path == '/') n++;
    return n;
}

static void order_entries(waste_vfs *vfs) {
    /* Bounded insertion sort avoids a host-libc dependency in the CLI. */
    for (uint32_t i = 1; i < vfs->count; i++) {
        waste_vfs_entry entry = vfs->entries[i];
        uint32_t j = i;
        while (j > 0) {
            const char *previous = vfs->entries[j-1].path;
            unsigned a = depth(previous), b = depth(entry.path);
            if (a < b || (a == b && strcmp(previous, entry.path) <= 0)) break;
            vfs->entries[j] = vfs->entries[j-1];
            j--;
        }
        vfs->entries[j] = entry;
    }
    for (uint32_t i = 0; i < vfs->count; i++) vfs->entries[i].metadata.inode = i + 1u;
}

int native_vfs_load_at(int root_fd, waste_vfs *vfs, char *error, size_t capacity) {
    tree_reader reader = {0};
    struct __kernel_stat info;
    strcpy(reader.path, "/");
    int fd = root_fd >= 0 ? open_at(root_fd, ".", 1) : -1;
    int status = -1;
    if (fd >= 0 && !sys_fstat(fd, &info) && !add_node(&reader, "/", &info, NULL, 0)) {
        status = read_directory(&reader, fd, "/", 0);
        if (!status) status = add_virtual(&reader, "/bin", 1, 0755);
        if (!status) status = add_virtual(&reader, "/root", 1, 0755);
        if (!status) status = add_virtual(&reader, "/tmp", 1, 0777);
        if (!status) status = add_virtual(&reader, "/bin/wat", 0, 0755);
        if (!status) status = add_virtual(&reader, "/bin/wast", 0, 0755);
    }
    if (fd >= 0) sys_close(fd);
    if (status) {
        if (error && capacity) snprintf(error, capacity, "cannot load VFS path: %s", reader.path);
        waste_vfs_free(&reader.vfs);
        return -1;
    }
    order_entries(&reader.vfs);
    waste_vfs_free(vfs);
    *vfs = reader.vfs;
    return 0;
}

int native_vfs_load(const char *root, waste_vfs *vfs, char *error, size_t capacity) {
    int fd = native_vfs_open_root(root);
    int result = native_vfs_load_at(fd, vfs, error, capacity);
    if (fd >= 0) sys_close(fd);
    return result;
}
