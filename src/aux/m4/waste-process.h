#ifndef WASTE_M4_PROCESS_H
#define WASTE_M4_PROCESS_H
#include <sys/types.h>
/* Only the shell execution forms used by M4, through the existing kernel. */
int m4_waste_execute(const char *const [], int *);
pid_t m4_waste_pipe_in(const char *const [], int *);
int m4_waste_wait(pid_t, int *);
#endif
