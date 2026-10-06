#ifndef WASTE_ABI_POSIX_H
#define WASTE_ABI_POSIX_H
#include <stdint.h>
#include <stddef.h>
/* --- Guest POSIX ABI types (wasm32) --- */

#define WASTE_FD_SETSIZE 1024
#define WASTE_NFDBITS    32

typedef struct { uint32_t fds_bits[WASTE_FD_SETSIZE / WASTE_NFDBITS]; } waste_fd_set;
typedef struct { int64_t tv_sec;  int32_t tv_usec; } waste_timeval;
typedef struct { int64_t tv_sec;  int32_t tv_nsec; } waste_timespec;
typedef struct { uint32_t sig[4]; } waste_sigset_t;

/* Stable Wasm32 pathname/stat ABI; do not use the host struct stat. */
typedef struct {
  uint64_t st_dev; uint64_t st_ino;
  uint32_t st_mode; uint32_t st_nlink; uint32_t st_uid; uint32_t st_gid;
  uint64_t st_rdev;
  int64_t st_size; int64_t st_blksize; int64_t st_blocks;
  int64_t st_atime_sec; int64_t st_atime_nsec;
  int64_t st_mtime_sec; int64_t st_mtime_nsec;
  int64_t st_ctime_sec; int64_t st_ctime_nsec;
  uint8_t reserved[16];
} waste_stat;

typedef struct {
  uint32_t kind; uint32_t mode; uint32_t uid; uint32_t gid; int64_t size; uint64_t inode;
  int64_t mtime_sec; int64_t mtime_nsec;
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
#endif
