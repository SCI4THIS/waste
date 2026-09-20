/* unistd.c — POSIX unistd stubs for the WASTE guest libc. */

#include "include/helper.h"

i32 isatty(i32 fd){(void)fd;return 0;}

static char tty_path[9]={'/','d','e','v','/','t','t','y',0};
char *ttyname(i32 descriptor){return descriptor>=0&&descriptor<=2?tty_path:0;}

i32 setdtablesize(i64 size){return size>0&&size<=0x7fffffff?(i32)size:-1;}
i32 sysconf(i32 name){(void)name;return 1024;}
i32 pathconf(const char*path,i32 name){(void)path;(void)name;return 255;}
u32 confstr(i32 name,char*buffer,u32 capacity){(void)name;const char*value="/bin:/usr/bin";u32 needed=c_length(value)+1;if(buffer&&capacity){u32 n=needed<capacity?needed:capacity;bytes_copy(buffer,value,n);buffer[n-1]=0;}return needed;}

i32 execve(const char*p,char*const*a,char*const*e){(void)p;(void)a;(void)e;return unsupported();}
i32 chown(const char*p,u32 u,u32 g){(void)p;(void)u;(void)g;return unsupported();}
i32 readlink(const char*p,char*b,u32 n){(void)p;(void)b;(void)n;return unsupported();}

/* Path queries cross the versioned engine-owned kernel boundary. The kernel
 * receives a bounded span and compact metadata; these wrappers retain the
 * ordinary guest POSIX ABI and own guest errno translation. */
__attribute__((import_module("waste_kernel"), import_name("path_access_v1")))
extern i32 waste_kernel_path_access_v1(const char *path, u32 length,
                                        i32 mode, i32 flags);
__attribute__((import_module("waste_kernel"), import_name("path_stat_v1")))
extern i32 waste_kernel_path_stat_v1(const char *path, u32 length,
                                      i32 follow, waste_path_metadata *metadata);

static i32 path_result(i32 result) {
  if (result < 0) { *__errno_location() = -result; return -1; }
  return 0;
}

i32 access(const char *path, i32 mode) {
  if (!path) { *__errno_location() = 14; return -1; }
  return path_result(waste_kernel_path_access_v1(path, c_length(path), mode, 0));
}

i32 eaccess(const char *path, i32 mode) { return access(path, mode); }

i32 faccessat(i32 directory, const char *path, i32 mode, i32 flags) {
  if (directory != -100) { *__errno_location() = 9; return -1; }
  if (!path) { *__errno_location() = 14; return -1; }
  return path_result(waste_kernel_path_access_v1(
      path, c_length(path), mode, flags));
}

static i32 stat_query(const char *path, waste_stat *output, i32 follow) {
  if (!path || !output) { *__errno_location() = 14; return -1; }
  waste_path_metadata metadata;
  i32 result = waste_kernel_path_stat_v1(path, c_length(path), follow, &metadata);
  if (result < 0) { *__errno_location() = -result; return -1; }
  bytes_zero(output, sizeof(*output));
  output->st_ino = metadata.inode;
  output->st_mode = metadata.mode;
  output->st_nlink = metadata.kind == 2 ? 2 : 1;
  output->st_uid = metadata.uid;
  output->st_gid = metadata.gid;
  output->st_size = metadata.size;
  output->st_blksize = 4096;
  output->st_blocks = (metadata.size + 511) / 512;
  return 0;
}

i32 stat(const char *path, waste_stat *output) { return stat_query(path, output, 1); }
i32 lstat(const char *path, waste_stat *output) { return stat_query(path, output, 0); }
