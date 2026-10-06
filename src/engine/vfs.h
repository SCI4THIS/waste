#ifndef WASTE_VFS_H
#define WASTE_VFS_H
#include <stddef.h>
#include <stdint.h>
#include "lib/include/path.h"
struct posix_kernel;
#define WASTE_VFS_MAX_BYTES (64u * 1024u * 1024u)
#define WASTE_VFS_MAX_ENTRIES 960u
#define WASTE_VFS_INVENTORY_MAX (2u * 1024u * 1024u)
/* Installed inventory with separately supplied native/tree or browser/tar
 * bytes. This is an owned in-memory catalogue, not a serialized format. */
typedef struct {
    char path[POSIX_PATH_NODE_NAME_MAX], sha256[65];
    posix_path_metadata metadata;
    uint8_t *data;
    int ready, interpreter;
} waste_vfs_entry;
typedef struct {
    waste_vfs_entry *entries;
    uint32_t count;
    size_t bytes;
} waste_vfs;
int waste_vfs_inventory(const char *json, size_t length, waste_vfs *vfs);
int waste_vfs_set_file(waste_vfs *vfs, uint32_t index, const uint8_t *bytes, size_t length);
/* Transfer an allocated native input buffer on success only. */
int waste_vfs_take_file(waste_vfs *vfs, uint32_t index, uint8_t *bytes, size_t length);
int waste_vfs_ready(const waste_vfs *vfs);
int waste_vfs_mount(struct posix_kernel *kernel, const waste_vfs *vfs);
void waste_vfs_free(waste_vfs *vfs);
#endif
