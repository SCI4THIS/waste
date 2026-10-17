#ifndef WASTE_PROCESS_H
#define WASTE_PROCESS_H
#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Copy the active image's resolved guest pathname, including its terminating
 * NUL. Return the length excluding NUL, or -1 with errno. A short buffer fails
 * with ERANGE without writing. The result is independent of argv, cwd, and
 * subsequent rename/unlink; this does not expose a host path or a DSO path. */
ssize_t waste_get_executable_path(char *buffer, size_t capacity);

/* String bytes include argv + environment terminating NULs, excluding path
 * and pointer-vector storage. Counts exclude each vector's NULL sentinel.
 * Interpreter expansion is checked again against these same limits. */
struct waste_exec_limits {
  uint32_t max_args;
  uint32_t max_env;
  uint32_t max_bytes;
};
/* Failure returns -1 with errno and leaves the entire output unchanged. */
int waste_get_exec_limits(struct waste_exec_limits *limits);

/* Wait for an exited child until an absolute CLOCK_MONOTONIC nanosecond
 * deadline. Return its PID after reaping, zero on expiry/WNOHANG, or -1 with
 * errno. Only options 0 and WNOHANG are supported. The status destination is
 * validated before consuming an exit and remains unchanged on zero/error.
 * Caught signals obey SA_RESTART; retries retain the same absolute deadline.
 * This does not reserve SIGALRM or change an existing alarm. */
pid_t waste_waitpid_until(pid_t pid, int *status, int options,
                         uint64_t deadline_ns);

#ifdef __cplusplus
}
#endif
#endif
