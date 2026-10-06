#include "native_vfs.h"
#include "lib/include/kernel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void capacity_check(void) {
    size_t capacity = ((size_t)WASTE_VFS_MAX_ENTRIES + 1) * 256 + 128, at = 0;
    char *json = malloc(capacity);
    assert(json);
    at += (size_t)snprintf(json+at, capacity-at, "{\"version\":1,\"entries\":[");
    waste_vfs vfs = {0};
    for (uint32_t i = 0; i <= WASTE_VFS_MAX_ENTRIES; i++) {
        char path[32];
        if (i < 4) {
            static const char *const base[] = {"/", "/bin", "/usr", "/usr/bin"};
            strcpy(path, base[i]);
        } else snprintf(path, sizeof(path), "/capacity-%u", i);
        at += (size_t)snprintf(json+at, capacity-at,
            "%s{\"path\":\"%s\",\"role\":\"directory\",\"kind\":2,\"mode\":511,"
            "\"uid\":0,\"gid\":0,\"size\":0,\"inode\":%u,\"mtime_sec\":0,\"mtime_nsec\":0}",
            i ? "," : "", path, i+1);
        assert(at+3 < capacity);
        if (i+1 == WASTE_VFS_MAX_ENTRIES) {
            strcpy(json+at, "]}");
            assert(!waste_vfs_inventory(json, at+2, &vfs));
        }
    }
    strcpy(json+at, "]}");
    assert(waste_vfs_inventory(json, at+2, &vfs));
    assert(vfs.count == WASTE_VFS_MAX_ENTRIES); // Failed replacement preserves input.
    posix_kernel *kernel = posix_kernel_create(0);
    assert(kernel && !waste_vfs_mount(kernel, &vfs));
    for (uint32_t i = 0; i < VFS_RUNTIME_NODE_RESERVE; i++) {
        char path[32];
        int n = snprintf(path, sizeof(path), "/runtime-%u", i);
        int fd = posix_kernel_open(kernel, (const uint8_t *)path, (size_t)n,
                                   POSIX_O_CREAT | POSIX_O_WRONLY, 0600);
        assert(fd >= 0 && !posix_kernel_close(kernel, fd));
    }
    assert(kernel->path_node_count == POSIX_PATH_NODE_MAX);
    assert(posix_kernel_open(kernel, (const uint8_t *)"/overflow", 9,
                             POSIX_O_CREAT | POSIX_O_WRONLY, 0600) < 0);
    posix_kernel_destroy(kernel);
    waste_vfs_free(&vfs);
    free(json);
    puts("PASS inventory capacity: installed nodes, runtime reserve, overflow rejection");
}

int main(int argc, char **argv) {
    assert(argc == 2);
    waste_vfs vfs = {0};
    char error[256];
    assert(!native_vfs_load(argv[1], &vfs, error, sizeof(error)));
    posix_kernel *first = posix_kernel_create(0), *second = posix_kernel_create(0);
    assert(first && second && !waste_vfs_mount(first, &vfs) && !waste_vfs_mount(second, &vfs));
    for (uint32_t i = 0; i < vfs.count; i++) {
        const waste_vfs_entry *e = &vfs.entries[i];
        posix_path_metadata actual;
        assert(!posix_kernel_path_stat(first, (const uint8_t *)e->path, strlen(e->path), 1, &actual));
        assert(!memcmp(&actual, &e->metadata, sizeof(actual)));
        if (actual.kind == POSIX_NODE_REGULAR) {
            uint8_t *bytes = NULL;
            size_t length = 0;
            assert(!posix_kernel_path_read_snapshot(first, (const uint8_t *)e->path,
                strlen(e->path), WASTE_VFS_MAX_BYTES, &bytes, &length, &actual));
            assert(length == (size_t)e->metadata.size);
            assert(!length || !memcmp(bytes, e->data, length));
            free(bytes);
        }
    }
    int fd = posix_kernel_open(first, (const uint8_t *)"/usr/bin/echo", 13,
                               POSIX_O_WRONLY | POSIX_O_TRUNC, 0);
    assert(fd >= 0 && posix_kernel_write(first, fd, "edited", 6) == 6);
    assert(!posix_kernel_close(first, fd));
    posix_path_metadata a, b;
    assert(!posix_kernel_path_stat(first, (const uint8_t *)"/usr/bin/echo", 13, 1, &a));
    assert(!posix_kernel_path_stat(second, (const uint8_t *)"/usr/bin/echo", 13, 1, &b));
    assert(a.size == 6 && b.size > 6);
    waste_vfs reread = {0};
    assert(!native_vfs_load(argv[1], &reread, error, sizeof(error)));
    for (uint32_t i = 0; i < vfs.count; i++) {
        assert(!memcmp(&reread.entries[i].metadata, &vfs.entries[i].metadata, sizeof(a)));
        size_t length = (size_t)vfs.entries[i].metadata.size;
        assert(!length || !memcmp(reread.entries[i].data, vfs.entries[i].data, length));
    }
    waste_vfs_free(&reread);
    const char *bad[] = {"", "{}", "{\"version\":2,\"entries\":[]}",
        "{\"version\":1,\"entries\":[]}", "{\"version\":1,\"entries\":[,]}",
        "{\"version\":1,\"version\":1,\"entries\":[]}"};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(*bad); i++)
        assert(waste_vfs_inventory(bad[i], strlen(bad[i]), &vfs));
    assert(waste_vfs_inventory("{}", WASTE_VFS_INVENTORY_MAX+1u, &vfs));
    unsigned count = vfs.count;
    waste_vfs_free(&vfs);
    posix_kernel_destroy(first); posix_kernel_destroy(second);
    capacity_check();
    printf("PASS installed VFS: %u nodes, exact bytes/metadata, host preservation and kernel isolation\n", count);
    return 0;
}
