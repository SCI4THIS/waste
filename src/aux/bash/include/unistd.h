#ifndef WASTE_BASH_UNISTD_H
#define WASTE_BASH_UNISTD_H
#include_next <unistd.h>
pid_t getpgrp(void);
int setpgid(pid_t, pid_t);
int tcsetpgrp(int, pid_t);
#endif
