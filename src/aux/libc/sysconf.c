/* Process-stable guest configuration queries. */
#include <errno.h>
#include <unistd.h>
#include <waste/process.h>

long sysconf(int name) {
  switch (name) {
  case _SC_ARG_MAX: {
    struct waste_exec_limits limits;
    if (waste_get_exec_limits(&limits) != 0) return -1;
    return limits.max_bytes;
  }
  case _SC_PAGESIZE:
    return getpagesize();
  case _SC_GETPW_R_SIZE_MAX:
    /* Configured account strings have no fixed size limit. POSIX permits an
     * indeterminate limit: -1 without changing errno. Callers can grow their
     * buffer after ERANGE; LLVM starts with its own fallback capacity. */
    return -1;
  default:
    errno = EINVAL;
    return -1;
  }
}
