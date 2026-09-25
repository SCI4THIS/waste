/* unistd.c — POSIX unistd stubs for the WASTE guest libc. */

#include "include/helper.h"

struct timespec { i64 tv_sec; long tv_nsec; };

#ifndef WASTE_ENGINE
__attribute__((import_module("waste_kernel"), import_name("startup_v1")))
extern i32 waste_kernel_startup_v1(void);
#endif

extern i32 open(const char *path, i32 flags, i32 mode);
extern i32 close(i32 descriptor);
extern i32 waste_env_chdir(const char *path) __attribute__((import_module("env"), import_name("chdir")));
extern i32 waste_env_getcwd(char *buffer, i32 capacity) __attribute__((import_module("env"), import_name("getcwd")));
static i32 env_result(i32 result) {
  if (result < -1) { *__errno_location() = -result; return -1; }
  return result;
}
i32 chdir(const char *path) { return env_result(waste_env_chdir(path)); }
char *getcwd(char *buffer, u32 capacity) {
  i32 result = waste_env_getcwd(buffer, (i32)capacity);
  if (result < -1) { *__errno_location() = -result; return 0; }
  return result ? buffer : 0;
}

/* Decode the engine-owned startup block for the guest libc entry shim.  The
 * returned pointers remain in this image's linear memory for its lifetime. */
i32 __waste_startup_view(i32 *argc, char ***argv, i32 *envc, char ***envp) {
#ifdef WASTE_ENGINE
  (void)argc; (void)argv; (void)envc; (void)envp;
  return -1;
#else
  i32 block = waste_kernel_startup_v1();
  if (block < 0 || !argc || !argv || !envc || !envp) return -1;
  *argc = *(i32 *)(unsigned long)(u32)block;
  *argv = (char **)(unsigned long)*(u32 *)(unsigned long)((u32)block + 4);
  *envc = *(i32 *)(unsigned long)((u32)block + 8);
  *envp = (char **)(unsigned long)*(u32 *)(unsigned long)((u32)block + 12);
  return 0;
#endif
}

/* CRT bridge used by application-specific _start shims.  The callback is a
 * guest function pointer, so no engine or JavaScript function address crosses
 * the ABI boundary. */
typedef i32 (*waste_main_entry)(i32 argc, char **argv, char **envp);

i32 __waste_startup_call(waste_main_entry entry) {
  i32 argc, envc;
  char **argv, **envp;
  (void)envc;
  if (!entry || __waste_startup_view(&argc, &argv, &envc, &envp) != 0)
    return -1;
  return entry(argc, argv, envp);
}

#ifndef WASTE_ENGINE
__attribute__((import_module("waste_kernel"), import_name("isatty_v1")))
extern i32 waste_kernel_isatty_v1(i32 descriptor);
#endif

#ifdef WASTE_ENGINE
i32 isatty(i32 fd){(void)fd;return 0;}
#else
i32 isatty(i32 fd){return waste_kernel_isatty_v1(fd);}
__attribute__((import_module("env"), import_name("execve")))
extern i32 waste_env_execve(const char *path, char *const *argv,
                            char *const *envp);
#endif

static char tty_path[9]={'/','d','e','v','/','t','t','y',0};
char *ttyname(i32 descriptor){return descriptor>=0&&descriptor<=2?tty_path:0;}

i32 setdtablesize(i64 size){return size>0&&size<=0x7fffffff?(i32)size:-1;}
const char *getprogname(void){return "waste";}
i32 sysconf(i32 name){(void)name;return 1024;}
i32 pathconf(const char*path,i32 name){(void)path;(void)name;return 255;}
u32 confstr(i32 name,char*buffer,u32 capacity){(void)name;const char*value="/bin:/usr/bin";u32 needed=c_length(value)+1;if(buffer&&capacity){u32 n=needed<capacity?needed:capacity;bytes_copy(buffer,value,n);buffer[n-1]=0;}return needed;}

#ifdef WASTE_ENGINE
i32 execve(const char*p,char*const*a,char*const*e){(void)p;(void)a;(void)e;return unsupported();}
#else
i32 execve(const char*p,char*const*a,char*const*e){return env_result(waste_env_execve(p,a,e));}
#endif
i32 chown(const char*p,u32 u,u32 g){(void)p;(void)u;(void)g;return unsupported();}
i32 lchmod(const char*p,u32 mode){(void)p;(void)mode;return unsupported();}
i32 mkfifo(const char*p,u32 mode){(void)p;(void)mode;return unsupported();}
i32 mkfifoat(i32 directory,const char*p,u32 mode){(void)directory;(void)p;(void)mode;return unsupported();}
i32 mknod(const char*p,u32 mode,unsigned long device){(void)p;(void)mode;(void)device;return unsupported();}
i32 mknodat(i32 directory,const char*p,u32 mode,unsigned long device){(void)directory;(void)p;(void)mode;(void)device;return unsupported();}
i32 linkat(i32 old_directory,const char*old_path,i32 new_directory,const char*new_path,i32 flags){(void)old_directory;(void)old_path;(void)new_directory;(void)new_path;(void)flags;return unsupported();}
i32 lchown(const char*p,u32 u,u32 g){return chown(p,u,g);}
i32 fchown(i32 fd,u32 u,u32 g){(void)fd;(void)u;(void)g;return unsupported();}
i32 fchdir(i32 fd){(void)fd;return unsupported();}
i32 fchmodat(i32 directory,const char*path,u32 mode,i32 flags){(void)directory;(void)path;(void)mode;(void)flags;return unsupported();}
i32 utimensat(i32 directory,const char*path,const struct timespec*times,i32 flags){(void)directory;(void)path;(void)times;(void)flags;return unsupported();}
i32 futimens(i32 fd,const struct timespec times[2]){(void)fd;(void)times;return unsupported();}
i32 posix_fadvise(i32 fd, i64 offset, i64 length, i32 advice) {
  (void)fd; (void)offset; (void)length; (void)advice; return 0;
}
extern i32 waste_env_readlink(const char *path, char *buffer, u32 capacity)
  __attribute__((import_module("env"), import_name("readlink")));
i32 readlink(const char *path, char *buffer, u32 capacity) {
  return env_result(waste_env_readlink(path, buffer, capacity));
}

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
i32 fstatat(i32 directory, const char *path, waste_stat *output, i32 flags) {
  (void)directory;
  return stat_query(path, output, !(flags & 0x100));
}
