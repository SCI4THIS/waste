/* Read-only guest clocks; platform time is injected into the shared kernel. */
#include <time.h>
#include <errno.h>

extern int waste_kernel_clock_gettime_v1(clockid_t, struct timespec *)
  __attribute__((import_module("waste_kernel"), import_name("clock_gettime_v1")));

int clock_gettime(clockid_t id, struct timespec *value) {
  /* Let the bounded engine writer reject invalid guest destinations with
   * EFAULT instead of performing an unchecked Wasm store here in libc. */
  int status = waste_kernel_clock_gettime_v1(id, value);
  if (status < 0) { errno = -status; return -1; }
  return 0;
}
