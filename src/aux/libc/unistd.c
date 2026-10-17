/* unistd.c — POSIX unistd stubs for the WASTE guest libc. */

#include "include/helper.h"
#include <waste/abi/fcntl.h>

struct timespec { i64 tv_sec; long tv_nsec; };

__attribute__((import_module("waste_kernel"), import_name("startup_v1")))
extern i32 waste_kernel_startup_v1(void);

extern i32 waste_kernel_executable_path_v1(char *, u32)
  __attribute__((import_module("waste_kernel"), import_name("executable_path_v1")));

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
long waste_get_executable_path(char *buffer, u32 capacity) {
  i32 result = waste_kernel_executable_path_v1(buffer, capacity);
  if (result < 0) { *__errno_location() = -result; return -1; }
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
extern i32 waste_kernel_openat_v1(i32 directory, const char *path,
                                   u32 length, i32 flags, i32 mode)
  __attribute__((import_module("waste_kernel"), import_name("openat_v1")));
i32 openat(i32 directory, const char *path, i32 flags, ...) {
  if (!path) { *__errno_location() = 14; return -1; }
  i32 mode = 0;
  if (flags & 0100) {
    __builtin_va_list ap;
    __builtin_va_start(ap, flags);
    mode = __builtin_va_arg(ap, i32);
    __builtin_va_end(ap);
  }
  return env_result(waste_kernel_openat_v1(
      directory, path, c_length(path), flags, mode));
}
i32 close(i32 descriptor) { return env_result(waste_env_close(descriptor)); }
extern i32 unlink(const char *path);
extern i32 rmdir(const char *path);
i32 remove(const char *path) {
  i32 result = unlink(path);
  if (result < 0 && *__errno_location() == 21) return rmdir(path);
  return result;
}
extern i32 waste_kernel_fsync_v1(i32 descriptor)
  __attribute__((import_module("waste_kernel"), import_name("fsync_v1")));
extern i32 waste_kernel_ftruncate_v1(i32 descriptor, i64 length)
  __attribute__((import_module("waste_kernel"), import_name("ftruncate_v1")));
i32 fsync(i32 descriptor) { return waste_kernel_fsync_v1(descriptor); }
i32 ftruncate(i32 descriptor, long length) {
  i32 result = waste_kernel_ftruncate_v1(descriptor, (i64)length);
  return result < 0 ? -1 : 0;
}
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
  /* Keep record pointers distinct from integer flags and argument-free calls. */
  i32 arg = 0;
  __builtin_va_list ap;
  __builtin_va_start(ap, cmd);
  if (cmd == WASTE_F_GETLK || cmd == WASTE_F_SETLK || cmd == WASTE_F_SETLKW)
    arg = (i32)(u32)__builtin_va_arg(ap, void *);
  else if (cmd == WASTE_F_DUPFD || cmd == WASTE_F_DUPFD_CLOEXEC ||
           cmd == WASTE_F_SETFD || cmd == WASTE_F_SETFL)
    arg = __builtin_va_arg(ap, i32);
  __builtin_va_end(ap);
  return env_result(waste_kernel_fcntl_v1(fd, cmd, arg));
}

/* Decode the engine-owned startup block for the guest libc entry shim.  The
 * returned pointers remain in this image's linear memory for its lifetime. */
i32 __waste_startup_view(i32 *argc, char ***argv, i32 *envc, char ***envp) {
  i32 block = waste_kernel_startup_v1();
  if (block < 0 || !argc || !argv || !envc || !envp) return -1;
  *argc = *(i32 *)(unsigned long)(u32)block;
  *argv = (char **)(unsigned long)*(u32 *)(unsigned long)((u32)block + 4);
  *envc = *(i32 *)(unsigned long)((u32)block + 8);
  *envp = (char **)(unsigned long)*(u32 *)(unsigned long)((u32)block + 12);
  return 0;
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

__attribute__((import_module("waste_kernel"), import_name("isatty_v1")))
extern i32 waste_kernel_isatty_v1(i32 descriptor);

i32 isatty(i32 fd){return waste_kernel_isatty_v1(fd);}
__attribute__((import_module("env"), import_name("execve")))
extern i32 waste_env_execve(const char *path, char *const *argv,
                            char *const *envp);

extern char **environ;
extern char *getenv(const char *name);
i32 execvp(const char *file, char *const *argv) {
  if (!file || !*file) { *__errno_location() = 2; return -1; }
  for (const char *at = file; *at; at++)
    if (*at == '/') return env_result(waste_env_execve(file, argv, environ));
  const char *path = getenv("PATH");
  if (!path) path = "/usr/bin:/bin";
  u32 file_length = c_length(file);
  int denied = 0;
  do {
    const char *end = path;
    while (*end && *end != ':') end++;
    size_t prefix = (size_t)(end - path);
    if (prefix > (size_t)-1 - file_length - 2) {
      *__errno_location() = 36; return -1;
    }
    char *candidate = malloc(prefix + file_length + 2);
    if (!candidate) { *__errno_location() = 12; return -1; }
    bytes_copy(candidate, path, prefix);
    if (prefix) candidate[prefix++] = '/';
    bytes_copy(candidate + prefix, file, file_length + 1);
    env_result(waste_env_execve(candidate, argv, environ));
    int saved_error = *__errno_location();
    free(candidate);
    if (saved_error == 13) denied = 1;
    else if (saved_error != 2 && saved_error != 20) {
      *__errno_location() = saved_error; return -1;
    }
    path = *end ? end + 1 : 0;
  } while (path);
  *__errno_location() = denied ? 13 : 2;
  return -1;
}

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
i32 pathconf(const char*path,i32 name){(void)path;(void)name;return 255;}
u32 confstr(i32 name,char*buffer,u32 capacity){(void)name;const char*value="/bin:/usr/bin";u32 needed=c_length(value)+1;if(buffer&&capacity){u32 n=needed<capacity?needed:capacity;bytes_copy(buffer,value,n);buffer[n-1]=0;}return needed;}

i32 execve(const char*p,char*const*a,char*const*e){return env_result(waste_env_execve(p,a,e));}
i32 chown(const char*p,u32 u,u32 g){(void)p;(void)u;(void)g;return unsupported();}
i32 lchmod(const char*p,u32 mode){(void)p;(void)mode;return unsupported();}
i32 mkfifo(const char*p,u32 mode){(void)p;(void)mode;return unsupported();}
i32 mkfifoat(i32 directory,const char*p,u32 mode){(void)directory;(void)p;(void)mode;return unsupported();}
i32 mknod(const char*p,u32 mode,unsigned long device){(void)p;(void)mode;(void)device;return unsupported();}
i32 mknodat(i32 directory,const char*p,u32 mode,unsigned long device){(void)directory;(void)p;(void)mode;(void)device;return unsupported();}
extern i32 waste_kernel_linkat_v1(i32 old_directory, const char *old_path,
                                   i32 new_directory, const char *new_path, i32 flags)
  __attribute__((import_module("waste_kernel"), import_name("linkat_v1")));
i32 linkat(i32 old_directory, const char *old_path,
            i32 new_directory, const char *new_path, i32 flags) {
  i32 result = waste_kernel_linkat_v1(
      old_directory, old_path, new_directory, new_path, flags);
  if (result < 0) { *__errno_location() = -result; return -1; }
  return result;
}
i32 link(const char *old_path, const char *new_path) {
  return linkat(-100, old_path, -100, new_path, 0);
}
i32 lchown(const char*p,u32 u,u32 g){return chown(p,u,g);}
i32 fchown(i32 fd,u32 u,u32 g){(void)fd;(void)u;(void)g;return unsupported();}
extern i32 waste_kernel_fchdir_v1(i32 fd) __attribute__((import_module("waste_kernel"), import_name("fchdir_v1")));
i32 fchdir(i32 fd){return env_result(waste_kernel_fchdir_v1(fd));}
extern i32 waste_kernel_fchmodat_v1(i32 directory, const char *path,
                                    u32 length, u32 mode, i32 flags)
  __attribute__((import_module("waste_kernel"), import_name("fchmodat_v1")));
i32 fchmodat(i32 directory, const char *path, u32 mode, i32 flags) {
  if (!path) { *__errno_location() = 14; return -1; }
  return env_result(waste_kernel_fchmodat_v1(
      directory, path, c_length(path), mode, flags));
}
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
extern i32 waste_kernel_symlinkat_v1(const char *target, i32 directory,
                                      const char *path)
  __attribute__((import_module("waste_kernel"), import_name("symlinkat_v1")));
i32 symlinkat(const char *target, i32 directory, const char *path) {
  i32 result = waste_kernel_symlinkat_v1(target, directory, path);
  if (result < 0) { *__errno_location() = -result; return -1; }
  return result;
}
i32 symlink(const char *target, const char *path) {
  return symlinkat(target, -100, path);
}

/* Path queries cross the versioned engine-owned kernel boundary. The kernel
 * receives a bounded span and compact metadata; these wrappers retain the
 * ordinary guest POSIX ABI and own guest errno translation. */
__attribute__((import_module("waste_kernel"), import_name("path_access_v1")))
extern i32 waste_kernel_path_access_v1(const char *path, u32 length,
                                        i32 mode, i32 flags);
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

extern i32 waste_kernel_fstatat_v2(i32 directory, const char *path,
                                    u32 length, i32 flags, waste_stat *output)
  __attribute__((import_module("waste_kernel"), import_name("fstatat_v2")));
static i32 stat_query_at(i32 directory, const char *path,
                         waste_stat *output, i32 flags) {
  if (!path || !output) { *__errno_location() = 14; return -1; }
  return env_result(waste_kernel_fstatat_v2(
      directory, path, c_length(path), flags, output));
}

i32 stat(const char *path, waste_stat *output) { return stat_query_at(-100, path, output, 0); }
i32 lstat(const char *path, waste_stat *output) { return stat_query_at(-100, path, output, 0x100); }
i32 fstatat(i32 directory, const char *path, waste_stat *output, i32 flags) {
  return stat_query_at(directory, path, output, flags);
}
