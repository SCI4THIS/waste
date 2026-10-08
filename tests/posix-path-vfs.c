#include "include/kernel.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int checks;
static int failures;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { fprintf(stderr, "path VFS check failed: %s\n", #condition); failures++; } \
} while (0)

int main(void) {
    posix_kernel *first = posix_kernel_create(0);
    posix_kernel *second = posix_kernel_create(0);
    CHECK(first != NULL && second != NULL);
    if (!first || !second) return 1;

    const posix_path_metadata regular = {
        POSIX_NODE_REGULAR, 0755, 0, 0, 12, 3, 0, 0
    };
    const posix_path_metadata directory = {
        POSIX_NODE_DIRECTORY, 0755, 0, 0, 0, 6, 0, 0
    };
    const posix_path_metadata readonly = {
        POSIX_NODE_REGULAR, 0444, 0, 0, 1, 4, 0, 0
    };
    const posix_path_metadata inaccessible = {
        POSIX_NODE_REGULAR, 0000, 0, 0, 1, 5, 0, 0
    };
    const uint8_t hello[] = "hello";
    posix_path_metadata actual;
    CHECK(posix_kernel_path_add(first, "/bin/tool", &regular) == 0);
    CHECK(posix_kernel_path_add(first, "/data", &directory) == 0);
    CHECK(posix_kernel_path_add(first, "/data/readme", &readonly) == 0);
    CHECK(posix_kernel_path_add(first, "/data/secret", &inaccessible) == 0);
    CHECK(posix_kernel_path_add_data(first, "/data/readme", &readonly,
                                     hello, sizeof(hello) - 1) == 0);
    const posix_path_metadata symlink = {
        POSIX_NODE_SYMLINK, 0777, 0, 0, 0, 7, 0, 0
    };
    CHECK(posix_kernel_path_add_symlink(first, "/data/alias", &symlink,
                                       "/data/readme") == 0);
    int fd = posix_kernel_open(first, (const uint8_t *)"/data/readme", 12, 0, 0);
    CHECK(fd >= 0);
    uint8_t readback[8] = {0};
    CHECK(posix_kernel_read(first, fd, readback, 5) == 5);
    CHECK(memcmp(readback, "hello", 5) == 0);
    CHECK(posix_kernel_close(first, fd) == 0);
    fd = posix_kernel_open(first, (const uint8_t *)"/tmp/new", 8,
                           POSIX_O_RDWR | POSIX_O_CREAT, 0666);
    CHECK(fd >= 0);
    CHECK(posix_kernel_write(first, fd, "abc", 3) == 3);
    CHECK(posix_kernel_close(first, fd) == 0);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"/tmp/new", 8, 1,
                                 &actual) == 0 && actual.size == 3);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"/data/alias", 11,
                                 0, &actual) == 0 && actual.kind == POSIX_NODE_SYMLINK);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"/data/alias", 11,
                                 1, &actual) == 0 && actual.size == 5);
    char link_target[32] = {0};
    CHECK(posix_kernel_path_readlink(first, (const uint8_t *)"/data/alias", 11,
                                     link_target, sizeof(link_target)) == 12 &&
          memcmp(link_target, "/data/readme", 12) == 0);
    fd = posix_kernel_open(first, (const uint8_t *)"/data/readme", 12, 0, 0);
    int64_t position = -1;
    CHECK(fd >= 0 && posix_kernel_lseek(first, fd, 2, 0, &position) == 0 &&
          position == 2);
    CHECK(posix_kernel_lseek(first, fd, 1, 1, &position) == 0 && position == 3);
    CHECK(posix_kernel_close(first, fd) == 0);
    int directory_fd = posix_kernel_open(first, (const uint8_t *)"/data", 5, 0, 0);
    CHECK(directory_fd >= 0);
    char entry[POSIX_PATH_NODE_NAME_MAX];
    int entries = 0;
    while (directory_fd >= 0 && posix_kernel_readdir(first, directory_fd, entry,
                                                     sizeof(entry), &actual) > 0)
        entries++;
    CHECK(entries == 5);
    CHECK(posix_kernel_close(first, directory_fd) == 0);
    CHECK(posix_kernel_path_mkdir(first, (const uint8_t *)"/tmp/work", 9,
                                  0777) == 0);
    CHECK(posix_kernel_path_rename(first, (const uint8_t *)"/tmp/new", 8,
                                   (const uint8_t *)"/tmp/work/file", 14) == 0);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"/tmp/work/file", 14,
                                 1, &actual) == 0 && actual.size == 3);
    CHECK(posix_kernel_path_unlink(first, (const uint8_t *)"/tmp/work/file",
                                   14, 0) == 0);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"/tmp/work/file", 14,
                                 1, &actual) == -POSIX_ENOENT);
    CHECK(posix_kernel_path_unlink(first, (const uint8_t *)"/tmp/work", 9, 1) == 0);

    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"/bin/tool", 9, 1, &actual) == 0);
    CHECK(actual.size == 0 && actual.kind == POSIX_NODE_REGULAR);
    CHECK(posix_kernel_path_set_mtime(first, (const uint8_t *)"/bin/tool", 9,
                                      1700000123, 456789012) == 0);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"/bin/tool", 9, 1,
                                 &actual) == 0 &&
          actual.mtime_sec == 1700000123 &&
          actual.mtime_nsec == 456789012);
    CHECK(posix_kernel_path_set_mtime(first, (const uint8_t *)"/missing", 8,
                                      1, 0) == -POSIX_ENOENT);
    CHECK(posix_kernel_path_set_mtime(first, (const uint8_t *)"/bin/tool", 9,
                                      1, 1000000000) == -POSIX_EINVAL);
    CHECK(posix_kernel_path_access(first, (const uint8_t *)"/bin/tool", 9, POSIX_X_OK, 0) == 0);
    uint8_t *snapshot = NULL;
    size_t snapshot_size = 0;
    CHECK(posix_kernel_path_snapshot(first, (const uint8_t *)"/data/readme",
                                     12, 64, &snapshot, &snapshot_size,
                                     &actual) == -POSIX_EACCES);
    CHECK(posix_kernel_path_add_data(first, "/bin/tool", &regular,
                                     hello, sizeof(hello) - 1) == 0);
    CHECK(posix_kernel_path_snapshot(first, (const uint8_t *)"/bin/tool", 9,
                                     64, &snapshot, &snapshot_size,
                                     &actual) == 0 && snapshot_size == 5 &&
          memcmp(snapshot, "hello", 5) == 0 && actual.inode == 3);
    free(snapshot);
    snapshot = NULL;
    CHECK(posix_kernel_path_snapshot(first, (const uint8_t *)"/bin/tool", 9,
                                     3, &snapshot, &snapshot_size,
                                     &actual) == -POSIX_E2BIG);
    CHECK(posix_kernel_path_snapshot(first, (const uint8_t *)"/data", 5,
                                     64, &snapshot, &snapshot_size,
                                     &actual) == -POSIX_EISDIR);
    CHECK(posix_kernel_path_snapshot(first, (const uint8_t *)"/missing", 8,
                                     64, &snapshot, &snapshot_size,
                                     &actual) == -POSIX_ENOENT);
    CHECK(posix_kernel_path_access(first, (const uint8_t *)"/data/readme", 12, POSIX_W_OK, 0) == -POSIX_EACCES);
    CHECK(posix_kernel_path_access(first, (const uint8_t *)"/data/secret", 12, POSIX_F_OK, 0) == 0);
    CHECK(posix_kernel_path_access(first, (const uint8_t *)"/missing", 8, POSIX_F_OK, 0) == -POSIX_ENOENT);
    /* Traversal through a regular file is now exercised through guest
     * imports in engine-regressions/path-vfs.wast. Keep the mtime boundary:
     * the guest libc does not yet provide an equivalent supported import. */
    CHECK(posix_kernel_path_set_mtime(first, (const uint8_t *)"/bin/tool/child", 15,
                                      1700000456, 0) == -POSIX_ENOTDIR);
    /* open() must follow symlinks at the leaf (path_find does not): the
     * previous behavior returned ENOENT because POSIX_NODE_SYMLINK was
     * neither REGULAR nor DIRECTORY.  Now /data/alias -> /data/readme
     * resolves to a readable fd. */
    int alias_fd = posix_kernel_open(first, (const uint8_t *)"/data/alias", 11, 0, 0);
    CHECK(alias_fd >= 0);
    uint8_t alias_readback[8] = {0};
    CHECK(alias_fd >= 0 &&
          posix_kernel_read(first, alias_fd, alias_readback, 5) == 5 &&
          memcmp(alias_readback, "hello", 5) == 0);
    CHECK(alias_fd < 0 || posix_kernel_close(first, alias_fd) == 0);

    CHECK(posix_kernel_path_set_cwd(first, "/data") == 0);
    char cwd[POSIX_PATH_NODE_NAME_MAX];
    CHECK(posix_kernel_getcwd(first, cwd, sizeof(cwd)) == 0 &&
          strcmp(cwd, "/data") == 0);
    /* Relative normalization, rejected chdir and invalid access modes now
     * run through the real guest ABI. Empty/overlong spans stay C checks. */
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"", 0, 1, &actual) == -POSIX_EINVAL);

    /* A relative link target is relative to its containing directory, even
     * when the caller's cwd differs. NOFOLLOW must not chmod the target. */
    CHECK(posix_kernel_path_add_symlink(first, "/data/relative", &symlink,
                                       "readme") == 0);
    CHECK(posix_kernel_path_set_cwd(first, "/") == 0);
    CHECK(posix_kernel_path_chmod(first, (const uint8_t *)"/data/relative",
                                  14, 0600) == 0);
    CHECK(posix_kernel_fchmodat(first, POSIX_AT_FDCWD,
                                (const uint8_t *)"/data/relative", 14,
                                0777, POSIX_AT_SYMLINK_NOFOLLOW) == -POSIX_EOPNOTSUPP);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"/data/readme", 12,
                                 1, &actual) == 0 && actual.mode == 0600);
    CHECK(posix_kernel_fchmodat(first, POSIX_AT_FDCWD,
                                (const uint8_t *)"/data/readme/", 13,
                                0777, 0) == -POSIX_ENOTDIR);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"/data/readme", 12,
                                 1, &actual) == 0 && actual.mode == 0600);

    CHECK(posix_kernel_path_stat(second, (const uint8_t *)"/bin/tool", 9, 1, &actual) == -POSIX_ENOENT);
    CHECK(posix_kernel_path_add(second, "/bin/tool", &regular) == 0);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"/bin/tool", 9, 1, &actual) == 0);

    char overlong[POSIX_PATH_MAX];
    memset(overlong, 'x', sizeof(overlong));
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)overlong,
                                 sizeof(overlong), 1, &actual) == -POSIX_EINVAL);
    posix_kernel_destroy(first);
    posix_kernel_destroy(second);
    printf("POSIX path VFS: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
