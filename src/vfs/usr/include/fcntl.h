#ifndef WASTE_FCNTL_H
#define WASTE_FCNTL_H

#include <waste/abi/availability.h>

#include <sys/types.h>
#include <waste/abi/fcntl.h>

#define F_GETLK WASTE_F_GETLK
#define F_SETLK WASTE_F_SETLK
#define F_SETLKW WASTE_F_SETLKW
#define F_RDLCK WASTE_F_RDLCK
#define F_WRLCK WASTE_F_WRLCK
#define F_UNLCK WASTE_F_UNLCK

struct flock {
    short l_type;
    short l_whence;
    off_t l_start;
    off_t l_len;
    pid_t l_pid;
};

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
#define F_DUPFD_CLOEXEC WASTE_F_DUPFD_CLOEXEC
#define F_GETFL WASTE_F_GETFL
#define F_SETFL WASTE_F_SETFL
#define F_DUPFD WASTE_F_DUPFD
#define F_GETFD WASTE_F_GETFD
#define F_SETFD WASTE_F_SETFD
#define FD_CLOEXEC 1
#define POSIX_FADV_NORMAL 0
#define POSIX_FADV_RANDOM 1
#define POSIX_FADV_SEQUENTIAL 2
#define POSIX_FADV_WILLNEED 3
#define POSIX_FADV_DONTNEED 4
#define POSIX_FADV_NOREUSE 5
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_SYMLINK_FOLLOW 0x400
#define AT_FDCWD (-100)
#define AT_EACCESS 0x200
#define AT_REMOVEDIR 0x200

#ifdef __cplusplus
extern "C" {
#endif

int open(const char *, int, ...);
int creat(const char *, unsigned int) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int openat(int, const char *, int, ...);
int linkat(int, const char *, int, const char *, int);
int fcntl(int, int, ...);
int fchmodat(int, const char *, unsigned int, int);
int fchownat(int, const char *, unsigned int, unsigned int, int) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_fadvise(int, off_t, off_t, int);

#ifdef __cplusplus
}
#endif

#endif
