#ifndef WASTE_FCNTL_H
#define WASTE_FCNTL_H

#include <waste/abi/availability.h>

#include <sys/types.h>

#define O_RDONLY 0
#define O_BINARY 0
#define O_TEXT 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_ACCMODE 3
#define O_CREAT 0100
#define O_EXCL 0200
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_SEARCH 04000000
#define O_EXEC O_SEARCH
#define O_DIRECTORY 0200000
#define O_NOCTTY 0400000
#define O_NOFOLLOW 01000000
#define O_CLOEXEC 02000000
#define F_DUPFD_CLOEXEC 1030
#define F_GETFL 3
#define F_SETFL 4
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define FD_CLOEXEC 1
#define POSIX_FADV_NORMAL 0
#define POSIX_FADV_RANDOM 1
#define POSIX_FADV_SEQUENTIAL 2
#define POSIX_FADV_WILLNEED 3
#define POSIX_FADV_DONTNEED 4
#define POSIX_FADV_NOREUSE 5
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_SYMLINK_FOLLOW 0
#define AT_FDCWD (-100)
#define AT_EACCESS 0x200
#define AT_REMOVEDIR 0x200

int open(const char *, int, ...);
int creat(const char *, unsigned int) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int openat(int, const char *, int, ...);
int linkat(int, const char *, int, const char *, int);
int fcntl(int, int, ...);
int fchmodat(int, const char *, unsigned int, int);
int fchownat(int, const char *, unsigned int, unsigned int, int) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_fadvise(int, off_t, off_t, int);

#endif
