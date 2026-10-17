#ifndef WASTE_SYS_RESOURCE_H
#define WASTE_SYS_RESOURCE_H

#include <sys/types.h>
#include <sys/time.h>
#include <waste/abi/resource.h>

typedef long long rlim_t;
#define RLIM_INFINITY ((rlim_t)-1)
struct rlimit { rlim_t rlim_cur; rlim_t rlim_max; };
/* Wasm32 time64 timestamps and 32-bit counters; never a host struct rusage. */
struct rusage {
  struct timeval ru_utime, ru_stime;
  long ru_maxrss, ru_ixrss, ru_idrss, ru_isrss;
  long ru_minflt, ru_majflt, ru_nswap, ru_inblock, ru_oublock;
  long ru_msgsnd, ru_msgrcv, ru_nsignals, ru_nvcsw, ru_nivcsw;
};

#define RLIMIT_CPU 0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_RSS 5
#define RLIMIT_NOFILE 7
#define RLIMIT_AS 9
#define RUSAGE_SELF WASTE_RUSAGE_SELF
#define RUSAGE_CHILDREN WASTE_RUSAGE_CHILDREN

#ifdef __cplusplus
extern "C" {
#endif
int getrlimit(int, struct rlimit *);
int setrlimit(int, const struct rlimit *);
/* CPU accounting is unavailable: valid selectors return -1/ENOSYS without
 * reading or writing the output; invalid selectors return -1/EINVAL. */
int getrusage(int, struct rusage *);
#ifdef __cplusplus
}
#endif

#endif
