#ifndef WASTE_VFS_H
#define WASTE_VFS_H
#include <stddef.h>
#include <stdint.h>
#include "../config.h"
#include "lib/include/path.h"
struct posix_kernel;
#define WASTE_VFS_MAX_BYTES VFS_MAX_BYTES
#define WASTE_VFS_MAX_ENTRIES VFS_MAX_ENTRIES
#define WASTE_VFS_INVENTORY_MAX VFS_INVENTORY_MAX_BYTES
/* Current tree or generated browser snapshot. The catalogue owns file bytes. */
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
/* Append a discovered node, taking bytes on success only. Native metadata is
 * obtained from the current filesystem, with no stored hash prerequisite. */
int waste_vfs_append(waste_vfs *vfs, const char *path, const posix_path_metadata *metadata,
                     uint8_t *bytes, size_t length, int interpreter);
int waste_vfs_set_file(waste_vfs *vfs, uint32_t index, const uint8_t *bytes, size_t length);
/* Transfer an allocated native input buffer on success only. */
int waste_vfs_take_file(waste_vfs *vfs, uint32_t index, uint8_t *bytes, size_t length);
int waste_vfs_ready(const waste_vfs *vfs);
int waste_vfs_mount(struct posix_kernel *kernel, const waste_vfs *vfs);
void waste_vfs_free(waste_vfs *vfs);
#endif
