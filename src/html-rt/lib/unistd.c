/* Browser interpreter POSIX stubs; guest wrappers live in src/aux/libc. */

#include "../../aux/libc/include/helper.h"

struct timespec { i64 tv_sec; long tv_nsec; };


extern i32 waste_kernel_open_v1(const char *path, i32 flags, i32 mode)
  __attribute__((import_module("waste_kernel"), import_name("open_v1")));
extern i32 waste_env_close(i32 descriptor)
  __attribute__((import_module("env"), import_name("close")));
extern i32 waste_kernel_chdir(const char *path) __attribute__((import_module("waste_kernel"), import_name("chdir")));
extern i32 waste_kernel_getcwd(char *buffer, i32 capacity) __attribute__((import_module("waste_kernel"), import_name("getcwd")));
extern i32 waste_kernel_fcntl_v1(i32 fd, i32 cmd, i32 arg) __attribute__((import_module("waste_kernel"), import_name("fcntl_v1")));
extern i32 waste_kernel_pipe_v1(i32 pipefd[2]) __attribute__((import_module("waste_kernel"), import_name("pipe_v1")));
extern i64 waste_kernel_lseek(i32 fd, i64 offset, i32 whence)
  __attribute__((import_module("waste_kernel"), import_name("lseek")));
static i32 env_result(i32 result) {
  if (result < -1) { *__errno_location() = -result; return -1; }
  return result;
}
i32 open(const char *path, i32 flags, ...) {
  i32 mode = 0;
  if (flags & 0100) {
    __builtin_va_list ap;
    __builtin_va_start(ap, flags);
    mode = __builtin_va_arg(ap, i32);
    __builtin_va_end(ap);
  }
  return env_result(waste_kernel_open_v1(path, flags, mode));
}
i32 close(i32 descriptor) { return env_result(waste_env_close(descriptor)); }
i32 chdir(const char *path) { return env_result(waste_kernel_chdir(path)); }
char *getcwd(char *buffer, u32 capacity) {
  /* Keep the allocation in the calling image.  The host import only knows
   * how to fill a guest buffer; asking it to allocate would either require a
   * cross-image pointer or a re-entrant malloc call while an import is active. */
  if (!buffer) {
    capacity = 4096;
    buffer = malloc(capacity);
    if (!buffer) { *__errno_location() = 12; return 0; }
  }
  i32 result = waste_kernel_getcwd(buffer, (i32)capacity);
  if (result < -1) { *__errno_location() = -result; return 0; }
  return result ? buffer : 0;
}
i32 fcntl(i32 fd, i32 cmd, ...) {
  /* Extract the optional third argument from the va_list.  All fcntl
   * commands that take an argument expect a single int. */
  i32 arg = 0;
  __builtin_va_list ap;
  __builtin_va_start(ap, cmd);
  arg = __builtin_va_arg(ap, i32);
  __builtin_va_end(ap);
  return env_result(waste_kernel_fcntl_v1(fd, cmd, arg));
}

/* Decode the engine-owned startup block for the guest libc entry shim.  The
 * returned pointers remain in this image's linear memory for its lifetime. */
i32 __waste_startup_view(i32 *argc, char ***argv, i32 *envc, char ***envp) {
  (void)argc; (void)argv; (void)envc; (void)envp;
  return -1;
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


i32 isatty(i32 fd){(void)fd;return 0;}

static char tty_path[9]={'/','d','e','v','/','t','t','y',0};
char *ttyname(i32 descriptor){return descriptor>=0&&descriptor<=2?tty_path:0;}

i32 setdtablesize(i64 size){return size>0&&size<=0x7fffffff?(i32)size:-1;}
const char *getprogname(void){return "waste";}
i32 getpagesize(void){return 65536;}
i32 isapipe(i32 fd) {
    if (fd < 0) { *__errno_location() = 9; return -1; }
    return 0;
}

i32 lseek(i32 fd, i32 offset, i32 whence) {
    i64 result = waste_kernel_lseek(fd, (i64)offset, whence);
    if (result < -1) { *__errno_location() = (i32)-result; return -1; }
    if (result > 0x7fffffffLL) { *__errno_location() = 75; return -1; }
    return (i32)result;
}

i32 pipe(i32 pipefd[2]) {
    return env_result(waste_kernel_pipe_v1(pipefd));
}

i32 pipe2(i32 pipefd[2], i32 flags) {
    (void)pipefd;
    (void)flags;
    *__errno_location() = 38;
    return -1;
}

i32 copy_file_range(i32 input_fd, i64 *input_offset,
                    i32 output_fd, i64 *output_offset,
                    u32 length, u32 flags) {
    (void)input_fd;
    (void)input_offset;
    (void)output_fd;
    (void)output_offset;
    (void)length;
    (void)flags;
    *__errno_location() = 38;
    return -1;
}

i32 splice(i32 input_fd, i64 *input_offset,
           i32 output_fd, i64 *output_offset,
           u32 length, u32 flags) {
    (void)input_fd;
    (void)input_offset;
    (void)output_fd;
    (void)output_offset;
    (void)length;
    (void)flags;
    *__errno_location() = 38;
    return -1;
}
i32 tcgetpgrp(i32 fd){(void)fd;return 1;}
i32 sysconf(i32 name){if(name==30)return 65536;return 1024;} /* 30 = _SC_PAGESIZE */
i32 pathconf(const char*path,i32 name){(void)path;(void)name;return 255;}
u32 confstr(i32 name,char*buffer,u32 capacity){(void)name;const char*value="/bin:/usr/bin";u32 needed=c_length(value)+1;if(buffer&&capacity){u32 n=needed<capacity?needed:capacity;bytes_copy(buffer,value,n);buffer[n-1]=0;}return needed;}

i32 execve(const char*p,char*const*a,char*const*e){(void)p;(void)a;(void)e;return unsupported();}
i32 chown(const char*p,u32 u,u32 g){(void)p;(void)u;(void)g;return unsupported();}
i32 lchmod(const char*p,u32 mode){(void)p;(void)mode;return unsupported();}
i32 mkfifo(const char*p,u32 mode){(void)p;(void)mode;return unsupported();}
i32 mkfifoat(i32 directory,const char*p,u32 mode){(void)directory;(void)p;(void)mode;return unsupported();}
i32 mknod(const char*p,u32 mode,unsigned long device){(void)p;(void)mode;(void)device;return unsupported();}
i32 mknodat(i32 directory,const char*p,u32 mode,unsigned long device){(void)directory;(void)p;(void)mode;(void)device;return unsupported();}
i32 linkat(i32 old_directory,const char*old_path,i32 new_directory,const char*new_path,i32 flags){(void)old_directory;(void)old_path;(void)new_directory;(void)new_path;(void)flags;return unsupported();}
i32 lchown(const char*p,u32 u,u32 g){return chown(p,u,g);}
i32 fchown(i32 fd,u32 u,u32 g){(void)fd;(void)u;(void)g;return unsupported();}
extern i32 waste_kernel_fchdir_v1(i32 fd) __attribute__((import_module("waste_kernel"), import_name("fchdir_v1")));
i32 fchdir(i32 fd){(void)fd;return unsupported();}
i32 fchmodat(i32 directory,const char*path,u32 mode,i32 flags){(void)directory;(void)path;(void)mode;(void)flags;return unsupported();}
i32 utimensat(i32 directory,const char*path,const struct timespec*times,i32 flags){(void)directory;(void)path;(void)times;(void)flags;return unsupported();}
i32 futimens(i32 fd,const struct timespec times[2]){(void)fd;(void)times;return unsupported();}
i32 posix_fadvise(i32 fd, i32 offset, i32 length, i32 advice) {
  (void)fd; (void)offset; (void)length; (void)advice; return 0;
}
extern i32 waste_kernel_readlink_v1(const char *path, char *buffer, u32 capacity)
  __attribute__((import_module("waste_kernel"), import_name("readlink_v1")));
i32 readlink(const char *path, char *buffer, u32 capacity) {
  return env_result(waste_kernel_readlink_v1(path, buffer, capacity));
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

static u32 stat_type_mode(u32 kind) {
  /* POSIX file-type bits for the stable Wasm32 struct stat ABI. */
  if (kind == 1) return 0100000u;
  if (kind == 2) return 0040000u;
  if (kind == 3) return 0120000u;
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
  output->st_mode = metadata.mode | stat_type_mode(metadata.kind);
  output->st_nlink = metadata.kind == 2 ? 2 : 1;
  output->st_uid = metadata.uid;
  output->st_gid = metadata.gid;
  output->st_size = metadata.size;
  output->st_blksize = 4096;
  output->st_blocks = (metadata.size + 511) / 512;
  output->st_atime_sec = metadata.mtime_sec;
  output->st_atime_nsec = metadata.mtime_nsec;
  output->st_mtime_sec = metadata.mtime_sec;
  output->st_mtime_nsec = metadata.mtime_nsec;
  output->st_ctime_sec = metadata.mtime_sec;
  output->st_ctime_nsec = metadata.mtime_nsec;
  return 0;
}

i32 stat(const char *path, waste_stat *output) { return stat_query(path, output, 1); }
i32 lstat(const char *path, waste_stat *output) { return stat_query(path, output, 0); }
i32 fstatat(i32 directory, const char *path, waste_stat *output, i32 flags) {
  (void)directory;
  return stat_query(path, output, !(flags & 0x100));
}
