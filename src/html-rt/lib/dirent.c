/* dirent.c — Engine-owned directory traversal. */

#include "include/helper.h"

extern i32 open(const char *path, i32 flags, i32 mode);
extern i32 close(i32 descriptor);
extern i32 waste_env_readdir(i32 descriptor, char *name, i32 capacity,
                             waste_path_metadata *metadata)
  __attribute__((import_module("env"), import_name("readdir_v1")));

typedef struct WasteDirent {
  u64 d_ino;
  i64 d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[256];
} WasteDirent;

typedef struct WasteDIR {
  i32 descriptor;
  WasteDirent entry;
} WasteDIR;

void *opendir(const char *path) {
  i32 descriptor = open(path, 0, 0);
  if (descriptor < -1) { *__errno_location() = -descriptor; return 0; }
  if (descriptor < 0) return 0;
  WasteDIR *directory = malloc((u32)sizeof(WasteDIR));
  if (!directory) { close(descriptor); *__errno_location() = 12; return 0; }
  directory->descriptor = descriptor;
  return directory;
}

i32 closedir(void *value) {
  WasteDIR *directory = value;
  if (!directory) { *__errno_location() = 14; return -1; }
  i32 result = close(directory->descriptor);
  free(directory);
  if (result < -1) { *__errno_location() = -result; return -1; }
  return result;
}

i32 dirfd(void *value) {
  WasteDIR *directory = value;
  return directory ? directory->descriptor : -1;
}

WasteDirent *readdir(void *value) {
  WasteDIR *directory = value;
  waste_path_metadata metadata;
  if (!directory) { *__errno_location() = 14; return 0; }
  i32 result = waste_env_readdir(directory->descriptor, directory->entry.d_name,
                                 (i32)sizeof(directory->entry.d_name), &metadata);
  if (result < -1) { *__errno_location() = -result; return 0; }
  if (result <= 0) return 0;
  directory->entry.d_ino = metadata.inode;
  directory->entry.d_off++;
  directory->entry.d_reclen = (unsigned short)(sizeof(directory->entry));
  directory->entry.d_type = metadata.kind == 2 ? 4 :
                            metadata.kind == 3 ? 10 : 8;
  return &directory->entry;
}
