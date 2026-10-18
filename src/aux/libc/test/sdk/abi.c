#include <stdint.h>
#include <limits.h>
#include <stdarg.h>
#include <stdalign.h>
#include <float.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <termios.h>
#include <signal.h>
#include <waste/abi/posix.h>
#include <assert.h>

#define SIZE(type, n) _Static_assert(sizeof(type) == (n), #type " size")
#define OFFSET(type, member, n) _Static_assert(offsetof(type, member) == (n), #member " offset")
SIZE(void *, 4); SIZE(int, 4); SIZE(long, 4); SIZE(long long, 8);
SIZE(time_t, 8); SIZE(off_t, 4); SIZE(struct stat, 128);
SIZE(struct tm, 36); SIZE(struct timeval, 16); SIZE(struct timespec, 16);
SIZE(fd_set, 128); SIZE(sigset_t, 16); SIZE(struct sigaction, 24);
SIZE(struct termios, 60); SIZE(struct winsize, 8); SIZE(struct dirent, 280);
OFFSET(struct stat, st_size, 40); OFFSET(struct stat, st_ctime_nsec, 104);
OFFSET(struct dirent, d_name, 19); OFFSET(struct termios, c_ispeed, 52);
_Static_assert(alignof(struct stat) == 8, "stat alignment");
_Static_assert(INT64_MAX == 9223372036854775807LL, "integer limits");
_Static_assert(SIZE_MAX == UINT32_MAX, "pointer limits");

/* Verify the compiler's wasm32 builtin va_list and default promotions. */
int sdk_varargs(int count, ...) {
  va_list args;
  va_start(args, count);
  int result = va_arg(args, int);
  long long wide = va_arg(args, long long);
  double real = va_arg(args, double);
  va_end(args);
  return result == 37 && wide == 0x1122334455667788LL && real == 1.5;
}

int sdk_check(void) { return sdk_varargs(3, (char)37, 0x1122334455667788LL, 1.5); }

void sdk_assert_fail(void) { assert(0); }
