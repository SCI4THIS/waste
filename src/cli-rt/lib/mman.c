/* mman.c -- Native mmap, munmap, and fstat via raw syscalls. */
#include "include/syscall.h"

/* fstat: copy from kernel stat to our freestanding stat layout.
 * Both layouts are identical on x86_64 so we can cast directly. */
int fstat(int fd, void *buf) {
    return (int)sys_fstat(fd, (struct __kernel_stat *)buf);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd,
           long offset) {
    return sys_mmap(addr, length, prot, flags, fd, offset);
}

int munmap(void *addr, size_t length) {
    return (int)sys_munmap(addr, length);
}
