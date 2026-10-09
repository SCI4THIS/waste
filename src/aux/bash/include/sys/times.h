#ifndef WASTE_BASH_SYS_TIMES_H
#define WASTE_BASH_SYS_TIMES_H
#include <time.h>
struct tms {
  clock_t tms_utime, tms_stime, tms_cutime, tms_cstime;
};
#endif
