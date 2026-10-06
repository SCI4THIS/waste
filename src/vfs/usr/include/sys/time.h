#ifndef WASTE_SYS_TIME_H
#define WASTE_SYS_TIME_H

#include <sys/types.h>

#ifndef NULL
# define NULL ((void *) 0)
#endif

struct timeval {
  time_t tv_sec;
  long tv_usec;
};

struct timezone {
  int tz_minuteswest;
  int tz_dsttime;
};

int gettimeofday(struct timeval *, void *);
int settimeofday(const struct timeval *, const struct timezone *);

#endif
