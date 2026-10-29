/* syscall.h -- Linux syscall interface for native platform files. */
#ifndef WASTE_SYSCALL_H
#define WASTE_SYSCALL_H

#include <stddef.h>
#include <stdint.h>

/* ---- Linux x86_64 and AArch64 syscall interfaces ---- */

#if defined(__aarch64__)
static inline long syscall6(long nr, long a1, long a2, long a3, long a4,
                            long a5, long a6) {
    register long x0 __asm__("x0") = a1;
    register long x1 __asm__("x1") = a2;
    register long x2 __asm__("x2") = a3;
    register long x3 __asm__("x3") = a4;
    register long x4 __asm__("x4") = a5;
    register long x5 __asm__("x5") = a6;
    register long x8 __asm__("x8") = nr;
    __asm__ volatile ("svc 0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3),
        "r"(x4), "r"(x5), "r"(x8) : "memory", "cc");
    return x0;
}
static inline long syscall1(long nr, long a1) { return syscall6(nr, a1, 0, 0, 0, 0, 0); }
static inline long syscall2(long nr, long a1, long a2) { return syscall6(nr, a1, a2, 0, 0, 0, 0); }
static inline long syscall3(long nr, long a1, long a2, long a3) { return syscall6(nr, a1, a2, a3, 0, 0, 0); }

/* Linux asm-generic syscall numbers used by AArch64. */
#define SYS_read 63
#define SYS_write 64
#define SYS_close 57
#define SYS_fstat 80
#define SYS_lseek 62
#define SYS_mmap 222
#define SYS_munmap 215
#define SYS_ioctl 29
#define SYS_nanosleep 101
#define SYS_clock_gettime 113
#define SYS_getdents64 61
#define SYS_exit_group 94
#define SYS_openat 56

#elif defined(__x86_64__)

static inline long syscall1(long nr, long a1) {
    long ret;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1)
        : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall2(long nr, long a1, long a2) {
    long ret;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall3(long nr, long a1, long a2, long a3) {
    long ret;
    register long r10 __asm__("r10") = a3;
    (void)r10;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall6(long nr, long a1, long a2, long a3, long a4,
                     long a5, long a6) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}

/* Syscall numbers (x86_64) */
#define SYS_read        0
#define SYS_write       1
#define SYS_open        2
#define SYS_close       3
#define SYS_fstat       5
#define SYS_lseek       8
#define SYS_mmap        9
#define SYS_munmap      11
#define SYS_ioctl       16
#define SYS_nanosleep   35
#define SYS_clock_gettime 228
#define SYS_getdents64   217
#define SYS_exit_group  231
#define SYS_openat      257
#else
#error "The native syscall backend requires Linux x86_64 or AArch64"
#endif

/* ---- Kernel structures ---- */

struct __kernel_stat {
    uint64_t st_dev;
    uint64_t st_ino;
#if defined(__aarch64__)
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint64_t __pad1;
    int64_t st_size;
    int32_t st_blksize;
    int32_t __pad2;
    int64_t st_blocks;
#else
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
#endif
    uint64_t st_atime_sec;
    uint64_t st_atime_nsec;
    uint64_t st_mtime_sec;
    uint64_t st_mtime_nsec;
    uint64_t st_ctime_sec;
    uint64_t st_ctime_nsec;
#if defined(__aarch64__)
    uint32_t __unused[2];
#else
    int64_t __unused[3];
#endif
};

#if defined(__aarch64__)
_Static_assert(sizeof(struct __kernel_stat) == 128, "AArch64 kernel stat size");
_Static_assert(offsetof(struct __kernel_stat, st_size) == 48, "AArch64 kernel stat size offset");
_Static_assert(offsetof(struct __kernel_stat, st_mtime_sec) == 88, "AArch64 kernel stat mtime offset");
#endif

struct __kernel_timespec {
    long tv_sec;
    long tv_nsec;
};

/* ---- Raw syscall wrappers ---- */

static inline long sys_write(int fd, const void *buf, size_t count) {
    return syscall3(SYS_write, fd, (long)buf, (long)count);
}

static inline long sys_read(int fd, void *buf, size_t count) {
    return syscall3(SYS_read, fd, (long)buf, (long)count);
}

static inline long sys_open(const char *path, int flags, int mode) {
#if defined(__aarch64__)
    return syscall6(SYS_openat, -100 /* AT_FDCWD */, (long)path, flags, mode, 0, 0);
#else
    return syscall3(SYS_open, (long)path, flags, mode);
#endif
}

static inline long sys_close(int fd) {
    return syscall1(SYS_close, fd);
}

static inline long sys_fstat(int fd, struct __kernel_stat *buf) {
    return syscall2(SYS_fstat, fd, (long)buf);
}

static inline long sys_lseek(int fd, long offset, int whence) {
    return syscall3(SYS_lseek, fd, offset, whence);
}

static inline void *sys_mmap(void *addr, size_t length, int prot, int flags,
                      int fd, long offset) {
    return (void *)syscall6(SYS_mmap, (long)addr, (long)length, prot, flags,
                            fd, offset);
}

static inline long sys_munmap(void *addr, size_t length) {
    return syscall2(SYS_munmap, (long)addr, (long)length);
}

static inline long sys_clock_gettime(int clk_id, struct __kernel_timespec *tp) {
    return syscall2(SYS_clock_gettime, clk_id, (long)tp);
}

static inline long sys_nanosleep(const struct __kernel_timespec *request) {
    return syscall2(SYS_nanosleep, (long)request, 0);
}

_Noreturn static inline void sys_exit_group(int status) {
    syscall1(SYS_exit_group, status);
    __builtin_unreachable();
}

#endif /* WASTE_SYSCALL_H */
