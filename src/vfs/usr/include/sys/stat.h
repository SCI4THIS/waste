#ifndef WASTE_SYS_STAT_H
#define WASTE_SYS_STAT_H

#include <waste/abi/availability.h>

#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include <stddef.h>

struct stat {
  unsigned long long st_dev;
  unsigned long long st_ino;
  unsigned int st_mode;
  unsigned int st_nlink;
  unsigned int st_uid;
  unsigned int st_gid;
  unsigned long long st_rdev;
  long long st_size;
  long long st_blksize;
  long long st_blocks;
  long long st_atime;
  long long st_atime_nsec;
  long long st_mtime;
  long long st_mtime_nsec;
  long long st_ctime;
  long long st_ctime_nsec;
  unsigned char __reserved[16];
};

_Static_assert(sizeof(struct stat) == 128, "stat must match the runtime write size");
_Static_assert(_Alignof(struct stat) == 8, "wasm32 stat alignment");
_Static_assert(offsetof(struct stat, st_mode) == 16, "stat mode offset");
_Static_assert(offsetof(struct stat, st_size) == 40, "stat size offset");
_Static_assert(offsetof(struct stat, st_atime) == 64, "stat atime offset");
_Static_assert(offsetof(struct stat, st_ctime_nsec) == 104, "stat ctime offset");

#define S_IFMT   0170000u
#define S_IFREG  0100000u
#define S_IFDIR  0040000u
#define S_IFLNK  0120000u
#define S_IFIFO  0010000u
#define S_IFCHR  0020000u
#define S_IFBLK  0060000u
#define S_IFSOCK 0140000u
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_ISLNK(mode) (((mode) & S_IFMT) == S_IFLNK)
#define S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFIFO)
#define S_ISCHR(mode) (((mode) & S_IFMT) == S_IFCHR)
#define S_ISBLK(mode) (((mode) & S_IFMT) == S_IFBLK)
#define S_ISSOCK(mode) (((mode) & S_IFMT) == S_IFSOCK)
#define S_TYPEISSHM(mode) 0
#define S_TYPEISTMO(mode) 0
#define S_TYPEISMQ(mode) 0
#define S_TYPEISSEM(mode) 0
#define S_ISCTG(mode) 0
#define S_ISDOOR(mode) 0
#define S_ISMPB(mode) 0
#define S_ISMPC(mode) 0
#define S_ISMPX(mode) 0
#define S_ISNAM(mode) 0
#define S_ISNWK(mode) 0
#define S_ISOFD(mode) 0
#define S_ISOFL(mode) 0
#define S_ISPORT(mode) 0
#define S_ISWHT(mode) 0
#define UTIME_NOW  ((1l << 30) - 1)
#define UTIME_OMIT ((1l << 30) - 2)
#define S_IRUSR 0400u
#define S_IWUSR 0200u
#define S_IXUSR 0100u
#define S_IRGRP 0040u
#define S_IWGRP 0020u
#define S_IXGRP 0010u
#define S_IROTH 0004u
#define S_IWOTH 0002u
#define S_IXOTH 0001u
#define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)
#define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)
#define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)
#define S_IXUGO (S_IXUSR | S_IXGRP | S_IXOTH)
#define S_IRWXUGO (S_IRWXU | S_IRWXG | S_IRWXO)
#define S_ISUID 04000u
#define S_ISGID 02000u
#define S_ISVTX 01000u

int stat(const char *, struct stat *);
int lstat(const char *, struct stat *);
int fstat(int, struct stat *);
int fstatat(int, const char *, struct stat *, int);
int chmod(const char *, unsigned int);
int lchmod(const char *, unsigned int);
int fchmod(int, unsigned int);
int fchmodat(int, const char *, unsigned int, int);
int utimensat(int, const char *, const struct timespec *, int);
int futimens(int, const struct timespec [2]);
int futimesat(int, const char *, const struct timeval [2]) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int utimens(const char *, const struct timespec [2]) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int lutimens(const char *, const struct timespec [2]) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int mkdir(const char *, unsigned int);
int mkfifo(const char *, unsigned int);
int mkfifoat(int, const char *, unsigned int);
int mknod(const char *, unsigned int, dev_t);
int mknodat(int, const char *, unsigned int, dev_t);

#endif
