/* time.c -- Native clock_gettime via raw syscall. */
#include "include/syscall.h"

int clock_gettime(int clk_id, void *tp) {
    return (int)sys_clock_gettime(clk_id, (struct __kernel_timespec *)tp);
}
