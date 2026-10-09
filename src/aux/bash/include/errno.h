#ifndef WASTE_BASH_ERRNO_H
#define WASTE_BASH_ERRNO_H
#include_next <errno.h>
/* Guest kernel errno values, absent from the minimal public SDK. */
#define E2BIG 7
#define ENOEXEC 8
#define ESPIPE 29
#define ECHILD 10
#define ESRCH 3
#endif
