#ifndef WASTE_NATIVE_VFS_H
#define WASTE_NATIVE_VFS_H
#include "vfs.h"
int native_vfs_open_root(const char *root);
int native_vfs_load_at(int root_fd, waste_vfs *vfs, char *error, size_t capacity);
int native_vfs_load(const char *root, waste_vfs *vfs, char *error, size_t capacity);
#endif
