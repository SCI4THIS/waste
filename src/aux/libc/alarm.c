/* Process alarms use the same injected clock and scheduler as nanosleep. */
#include <unistd.h>
#include <errno.h>

extern long long waste_kernel_alarm_v1(unsigned int)
  __attribute__((import_module("waste_kernel"), import_name("alarm_v1")));

unsigned int alarm(unsigned int seconds) {
  /* The widened boundary distinguishes every unsigned result from errno.
   * Production runtimes always install a clock. A clockless private sandbox
   * reports ENOSYS instead of silently advertising a functioning timer. */
  long long result = waste_kernel_alarm_v1(seconds);
  if (result < 0) { errno = (int)-result; return 0; }
  return (unsigned int)result;
}
