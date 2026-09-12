/* fcntl_2_native.c -- Native open() implementation via raw syscalls. */
#include "syscall_native.h"

int open(const char *path, int flags, ...) {
    /* Simplified: always pass mode 0 (adequate for O_RDONLY) */
    return (int)sys_open(path, flags, 0);
}
