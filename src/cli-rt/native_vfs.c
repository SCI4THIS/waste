/* Linux directory loading also supports the freestanding CLI. Host filesystem
 * calls stay in cli-rt; the engine receives owned inventory/file records. */
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

int native_vfs_open_root(const char *root) {
    return (int)sys_open(root, HOST_DIRECTORY | HOST_CLOEXEC, 0);
}

/* Walk components beneath the open root. Neither leaf nor intermediate host
 * symlinks can redirect an installed guest path. */
static int open_path(int root, const char *path, int directory) {
    char copy[POSIX_PATH_NODE_NAME_MAX];
    if (strlen(path) >= sizeof(copy)) return -1;
    memcpy(copy, path, strlen(path)+1);
    int fd = open_at(root, ".", 1);
    char *part = copy + (copy[0] == '/');
    while (fd >= 0 && *part) {
        char *slash = part;
        while (*slash && *slash != '/') slash++;
        if (!*slash) slash = NULL;
        if (slash) *slash = 0;
        int next = open_at(fd, part, slash || directory);
        sys_close(fd);
        fd = next;
        if (!slash) break;
        part = slash + 1;
    }
    return fd;
}

static uint8_t *read_path(int root, const char *path, size_t limit, size_t *length) {
    int fd = open_path(root, path, 0);
    struct __kernel_stat before, after;
    uint8_t *bytes = NULL;
    if (fd < 0) return NULL;
    if (sys_fstat(fd, &before) || (before.st_mode & 0170000) != 0100000 ||
        before.st_size < 0 || (uint64_t)before.st_size > limit) goto finish;
    size_t size = (size_t)before.st_size, at = 0;
    bytes = malloc(size + 1u);
    if (!bytes) goto finish;
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
    goto finish;
invalid:
    free(bytes); bytes = NULL;
finish:
    sys_close(fd);
    return bytes;
}

int native_vfs_load_at(int root_fd, waste_vfs *vfs, char *error, size_t capacity) {
    waste_vfs loaded = {0};
    size_t length = 0;
    const char *path = "/.inventory.json";
    uint8_t *json = root_fd >= 0 ? read_path(root_fd, path, WASTE_VFS_INVENTORY_MAX, &length) : NULL;
    if (!json || waste_vfs_inventory((const char *)json, length, &loaded)) {
        free(json); goto invalid;
    }
    free(json);
    for (uint32_t i = 0; i < loaded.count; i++) {
        waste_vfs_entry *e = &loaded.entries[i];
        path = e->path;
        if (e->interpreter) continue;
        if (e->metadata.kind == POSIX_NODE_DIRECTORY) {
            int fd = open_path(root_fd, path, 1);
            if (fd < 0 && fd != -2 /* missing inventory-only empty directory */) goto invalid;
            if (fd >= 0) sys_close(fd);
        } else {
            uint8_t *bytes = read_path(root_fd, path, (size_t)e->metadata.size, &length);
            if (!bytes || waste_vfs_take_file(&loaded, i, bytes, length)) {
                free(bytes);
                goto invalid;
            }
        }
    }
    waste_vfs_free(vfs);
    *vfs = loaded;
    return 0;
invalid:
    if (error && capacity) snprintf(error, capacity, "cannot load installed VFS path: %s", path);
    waste_vfs_free(&loaded);
    return -1;
}

int native_vfs_load(const char *root, waste_vfs *vfs, char *error, size_t capacity) {
    int fd = native_vfs_open_root(root);
    int result = native_vfs_load_at(fd, vfs, error, capacity);
    if (fd >= 0) sys_close(fd);
    return result;
}
