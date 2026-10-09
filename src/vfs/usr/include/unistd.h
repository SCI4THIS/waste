#ifndef WASTE_UNISTD_H
#define WASTE_UNISTD_H

#include <waste/abi/availability.h>

#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#define _POSIX_VERSION 200809L
#define _POSIX2_VERSION 200809L

int access(const char *, int);
int eaccess(const char *, int);
int faccessat(int, const char *, int, int);
int close(int);
void _exit(int);
int dup(int);
int dup2(int, int);
int pipe(int [2]);
int pipe2(int [2], int);
ssize_t copy_file_range(int, off_t *, int, off_t *, size_t, unsigned int);
ssize_t splice(int, off_t *, int, off_t *, size_t, unsigned int);
int execve(const char *, char *const [], char *const []);
int isatty(int);
pid_t tcgetpgrp(int);
int link(const char *, const char *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int symlink(const char *, const char *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int symlinkat(const char *, int, const char *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int linkat(int, const char *, int, const char *, int);
off_t lseek(int, off_t, int);
ssize_t read(int, void *, size_t);
ssize_t readlink(const char *, char *, size_t);
ssize_t readlinkat(int, const char *, char *, size_t) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
ssize_t write(int, const void *, size_t);
int unlink(const char *);
int unlinkat(int, const char *, int) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int mkstemp(char *);
int mkostemp(char *, int);
int rmdir(const char *);
int rename(const char *, const char *);
int renameat(int, const char *, int, const char *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int chdir(const char *);
int chown(const char *, unsigned int, unsigned int);
int lchown(const char *, unsigned int, unsigned int);
int fchown(int, unsigned int, unsigned int);
int fchdir(int);
int getgroups(int, int *);
struct timespec;
int utimensat(int, const char *, const struct timespec *, int);
char *getcwd(char *, size_t);
unsigned int sleep(unsigned int);
int pause(void) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int getpid(void);
uid_t getuid(void);
uid_t geteuid(void);
pid_t fork(void);
pid_t vfork(void) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int getdtablesize(void);
int getpagesize(void);
const char *getprogname(void);
int gethostname(char *, size_t);

#endif
