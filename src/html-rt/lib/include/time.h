#ifndef WASTE_TIME_H
#define WASTE_TIME_H

#include <sys/types.h>

typedef long clock_t;
typedef void *timezone_t;

struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
};

struct timespec {
  time_t tv_sec;
  long tv_nsec;
};

#define TIME_UTC 1
static inline int timespec_getres(struct timespec *resolution, int base)
{
  if (base != TIME_UTC)
    return 0;
  if (resolution) {
    resolution->tv_sec = 1;
    resolution->tv_nsec = 0;
  }
  return base;
}

time_t time(time_t *);
clock_t clock(void);
int nanosleep(const struct timespec *, struct timespec *);
struct tm *gmtime(const time_t *);
struct tm *gmtime_r(const time_t *, struct tm *);
struct tm *localtime(const time_t *);
struct tm *localtime_r(const time_t *, struct tm *);
struct tm *localtime_rz(timezone_t, const time_t *, struct tm *);
time_t mktime_z(timezone_t, struct tm *);
time_t mktime(struct tm *);
void tzset(void);
double difftime(time_t, time_t);
timezone_t tzalloc(const char *);
void tzfree(timezone_t);
size_t strftime_z(timezone_t, char *, size_t, const char *, const struct tm *);
size_t strftime(char *, size_t, const char *, const struct tm *);

#endif
