#ifndef WASTE_SYS_WAIT_H
#define WASTE_SYS_WAIT_H

#include <sys/types.h>

#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 8

int waitpid(pid_t, int *, int);
pid_t wait(int *);

#define WIFEXITED(status) (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFSIGNALED(status) (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#define WTERMSIG(status) ((status) & 0x7f)
#define WIFSTOPPED(status) (((status) & 0xff) == 0x7f)
#define WSTOPSIG(status) WEXITSTATUS(status)
#define WIFCONTINUED(status) ((status) == 0xffff)

#endif
