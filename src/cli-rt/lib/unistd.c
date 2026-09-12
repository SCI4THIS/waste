/* unistd.c -- Native close, isatty, and _start entry point. */
#include "include/syscall.h"

int close(int fd) {
    return (int)sys_close(fd);
}

int isatty(int fd) {
    /* Use TIOCGWINSZ ioctl to check */
    char buf[64];
    long ret = syscall3(SYS_ioctl, fd, 0x5413 /* TIOCGWINSZ */, (long)buf);
    return ret == 0 ? 1 : 0;
}

/* ---- Program entry point ---- */

int main(int argc, char **argv);

__attribute__((naked, noreturn))
void _start(void) {
    __asm__ volatile (
        "xor %%ebp, %%ebp\n"        /* Clear frame pointer */
        "mov (%%rsp), %%rdi\n"       /* argc = *rsp */
        "lea 8(%%rsp), %%rsi\n"      /* argv = rsp + 8 */
        "call main\n"                /* main(argc, argv) */
        "mov %%eax, %%edi\n"         /* exit status = return value */
        "mov $231, %%eax\n"          /* SYS_exit_group */
        "syscall\n"
        : : : "memory"
    );
}
