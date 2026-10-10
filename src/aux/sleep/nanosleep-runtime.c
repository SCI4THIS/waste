#include <errno.h>
#include <stdint.h>
#include <time.h>

__attribute__((import_module("libc"), import_name("strtod")))
extern double waste_libc_strtod(const char *text, char **end);

__attribute__((import_module("waste_kernel"), import_name("pselect_v1")))
extern int waste_kernel_pselect_v1(int nfds, void *readfds, void *writefds,
                                   void *exceptfds,
                                   const struct timespec *timeout,
                                   const void *sigmask);

double waste_sleep_strtod(const char *text, char **end) {
  return waste_libc_strtod(text, end);
}

int waste_sleep_seconds(double seconds) {
  if (!(seconds >= 0.0)) {
    errno = EINVAL;
    return -1;
  }
  struct timespec requested;
  if (seconds >= 9223372036854775807.0) {
    requested.tv_sec = INT64_MAX;
    requested.tv_nsec = 999999999;
  } else {
    requested.tv_sec = (time_t)seconds;
    double fraction = seconds - (double)requested.tv_sec;
    requested.tv_nsec = (long)(fraction * 1000000000.0);
    if (requested.tv_nsec < 0) requested.tv_nsec = 0;
    if (requested.tv_nsec >= 1000000000) {
      if (requested.tv_sec < INT64_MAX) requested.tv_sec++;
      requested.tv_nsec = 0;
    }
  }
  int result = waste_kernel_pselect_v1(0, 0, 0, 0, &requested, 0);
  if (result < 0) {
    errno = -result;
    return -1;
  }
  return 0;
}
