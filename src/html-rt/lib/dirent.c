/* dirent.c — Engine-owned directory traversal. */

#include "include/helper.h"
#include "include/dirent.h"

extern i32 open(const char *path, i32 flags, i32 mode);
extern i32 close(i32 descriptor);
extern i32 waste_env_readdir(i32 descriptor, char *name, i32 capacity,
                             waste_path_metadata *metadata)
  __attribute__((import_module("env"), import_name("readdir_v1")));

struct waste_dir {
  i32 descriptor;
  struct dirent entry;
};

DIR *opendir(const char *path) {
  i32 descriptor = open(path, 0, 0);
  if (descriptor < -1) { *__errno_location() = -descriptor; return 0; }
  if (descriptor < 0) return 0;
  DIR *directory = malloc((u32)sizeof(DIR));
  if (!directory) { close(descriptor); *__errno_location() = 12; return 0; }
  directory->descriptor = descriptor;
  directory->entry.d_off = 0;
  return directory;
}

i32 closedir(DIR *directory) {
  if (!directory) { *__errno_location() = 14; return -1; }
  i32 result = close(directory->descriptor);
  free(directory);
  if (result < -1) { *__errno_location() = -result; return -1; }
  return result;
}

i32 dirfd(DIR *directory) {
  return directory ? directory->descriptor : -1;
}

struct dirent *readdir(DIR *directory) {
  waste_path_metadata metadata;
  if (!directory) { *__errno_location() = 14; return 0; }
  i32 result = waste_env_readdir(directory->descriptor, directory->entry.d_name,
                                 (i32)sizeof(directory->entry.d_name), &metadata);
  if (result < -1) { *__errno_location() = -result; return 0; }
  if (result <= 0) return 0;
  directory->entry.d_ino = metadata.inode;
  directory->entry.d_off++;
  directory->entry.d_reclen = (unsigned short)sizeof(directory->entry);
  directory->entry.d_type = metadata.kind == 2 ? 4 :
                            metadata.kind == 3 ? 10 : 8;
  return &directory->entry;
}
