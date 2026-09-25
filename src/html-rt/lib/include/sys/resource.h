#ifndef WASTE_SYS_RESOURCE_H
#define WASTE_SYS_RESOURCE_H

#include <sys/types.h>

typedef long long rlim_t;
#define RLIM_INFINITY ((rlim_t)-1)
struct rlimit { rlim_t rlim_cur; rlim_t rlim_max; };
struct rusage { long long ru_utime; long long ru_stime; long long __reserved[16]; };

#define RLIMIT_CPU 0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_RSS 5
#define RLIMIT_NOFILE 7
#define RLIMIT_AS 9
#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN (-1)

int getrlimit(int, struct rlimit *);
int setrlimit(int, const struct rlimit *);
int getrusage(int, struct rusage *);

#endif
