/* Public convenience calls reuse the existing engine-owned process boundary. */
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <waste/process.h>

extern char **environ;
extern pid_t waste_env_waitpid(pid_t, int *, int)
  __attribute__((import_module("env"), import_name("waitpid")));

extern int waste_kernel_exec_limits_v1(struct waste_exec_limits *, size_t)
  __attribute__((import_module("waste_kernel"), import_name("exec_limits_v1")));
extern pid_t waste_kernel_getsid_v1(pid_t)
  __attribute__((import_module("waste_kernel"), import_name("getsid_v1")));
extern pid_t waste_kernel_getpgid_v1(pid_t)
  __attribute__((import_module("waste_kernel"), import_name("getpgid_v1")));
extern pid_t waste_kernel_setsid_v1(void)
  __attribute__((import_module("waste_kernel"), import_name("setsid_v1")));
extern pid_t waste_kernel_tcgetpgrp_v1(int)
  __attribute__((import_module("waste_kernel"), import_name("tcgetpgrp_v1")));
extern pid_t waste_kernel_waitpid_until_v1(pid_t, int *, int, uint64_t)
  __attribute__((import_module("waste_kernel"), import_name("waitpid_until_v1")));

static pid_t process_result(pid_t result) {
  if (result < 0) { errno = -result; return -1; }
  return result;
}

pid_t getsid(pid_t pid) { return process_result(waste_kernel_getsid_v1(pid)); }
pid_t getpgid(pid_t pid) { return process_result(waste_kernel_getpgid_v1(pid)); }
pid_t getpgrp(void) { return getpgid(0); }
pid_t setsid(void) { return process_result(waste_kernel_setsid_v1()); }
pid_t tcgetpgrp(int fd) { return process_result(waste_kernel_tcgetpgrp_v1(fd)); }
pid_t waste_waitpid_until(pid_t pid, int *status, int options,
                         uint64_t deadline_ns) {
  return process_result(waste_kernel_waitpid_until_v1(pid, status, options,
                                                    deadline_ns));
}

int waste_get_exec_limits(struct waste_exec_limits *limits) {
  int result = waste_kernel_exec_limits_v1(limits, sizeof(*limits));
  if (result < 0) { errno = -result; return -1; }
  return result;
}

int execv(const char *path, char *const argv[]) {
  return execve(path, argv, environ);
}

pid_t wait(int *status) {
  /* The env adapter already returns -1 and sets guest errno on failure. */
  return waste_env_waitpid(-1, status, 0);
}
