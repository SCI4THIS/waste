/* helper.h — Shared typedefs, extern declarations, and inline helpers for the
 * WASTE guest libc files.  Each file is compiled together into a single
 * waste-libc.wasm binary, but static helpers defined here give each translation
 * unit its own copy. */

#ifndef WASTE_LIBC_HELPER_H
#define WASTE_LIBC_HELPER_H

#include <stdarg.h>
#include <stddef.h>

typedef unsigned int u32;
typedef signed int i32;
typedef unsigned long long u64;
typedef signed long long i64;
typedef unsigned char u8;

extern void *malloc(size_t size);
extern void *realloc(void *pointer, size_t size);
extern void free(void *pointer);
extern i32 *__errno_location(void);

/* --- Guest POSIX ABI types (wasm32) --- */

#define WASTE_FD_SETSIZE 1024
#define WASTE_NFDBITS    32

typedef struct { u32 fds_bits[WASTE_FD_SETSIZE / WASTE_NFDBITS]; } waste_fd_set;
typedef struct { i64 tv_sec;  i32 tv_usec; } waste_timeval;
typedef struct { i64 tv_sec;  i32 tv_nsec; } waste_timespec;
typedef struct { u32 sig[4]; } waste_sigset_t;

/* Stable Wasm32 pathname/stat ABI; do not use the host struct stat. */
typedef struct {
  u64 st_dev; u64 st_ino;
  u32 st_mode; u32 st_nlink; u32 st_uid; u32 st_gid;
  u64 st_rdev;
  i64 st_size; i64 st_blksize; i64 st_blocks;
  i64 st_atime_sec; i64 st_atime_nsec;
  i64 st_mtime_sec; i64 st_mtime_nsec;
  i64 st_ctime_sec; i64 st_ctime_nsec;
  u8 reserved[16];
} waste_stat;

typedef struct {
  u32 kind; u32 mode; u32 uid; u32 gid; i64 size; u64 inode;
  i64 mtime_sec; i64 mtime_nsec;
} waste_path_metadata;

_Static_assert(sizeof(waste_fd_set)   == 128, "fd_set must be 128 bytes");
_Static_assert(sizeof(waste_timeval)  ==  16, "timeval must be 16 bytes");
_Static_assert(sizeof(waste_timespec) ==  16, "timespec must be 16 bytes");
_Static_assert(sizeof(waste_sigset_t) ==  16, "sigset_t must be 16 bytes");
_Static_assert(sizeof(waste_stat) == 128, "stat must be 128 bytes");
_Static_assert(_Alignof(waste_stat) == 8, "stat alignment");
_Static_assert(offsetof(waste_stat, st_mode) == 16, "stat mode offset");
_Static_assert(offsetof(waste_stat, st_size) == 40, "stat size offset");
_Static_assert(offsetof(waste_stat, st_atime_sec) == 64, "stat atime offset");
_Static_assert(offsetof(waste_stat, st_ctime_nsec) == 104, "stat ctime offset");
_Static_assert(sizeof(waste_path_metadata) == 48, "path metadata must be 48 bytes");

#define WASTE_EFAULT 14
#define WASTE_ENOENT 2
#define WASTE_EACCES 13
#define WASTE_ENOTDIR 20
#define WASTE_ENOSYS 38

/* --- Inline helpers shared across multiple libc files --- */

static u32 c_length(const char *text) {
  u32 length = 0;
  if ((u32)text >= __builtin_wasm_memory_size(0) * 65536U) return 0;
  if (text) while (text[length]) length++;
  return length;
}

static i32 c_compare(const char *left, const char *right) {
  u32 at = 0;
  while (left[at] && left[at] == right[at]) at++;
  return (unsigned char)left[at] - (unsigned char)right[at];
}

static void bytes_copy(void *destination, const void *source, u32 count) {
  unsigned char *to = destination;
  const unsigned char *from = source;
  if (to < from) for (u32 at = 0; at < count; at++) to[at] = from[at];
  else while (count) { count--; to[count] = from[count]; }
}

static void bytes_zero(void *destination, u32 count) {
  unsigned char *to = destination;
  for (u32 at = 0; at < count; at++) to[at] = 0;
}

static i32 valid_pointer(const void *p) {
  return p && (u32)p < __builtin_wasm_memory_size(0) * 65536U;
}

static i32 lower_ascii(i32 c) {
  return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static i32 unsupported(void) {
  *__errno_location() = 38; /* ENOSYS */
  return -1;
}

#endif /* WASTE_LIBC_HELPER_H */
