/* Public signal registration uses the engine-owned process signal state. */
#include <errno.h>
#include <signal.h>

extern int waste_kernel_sigaction_v1(int, const struct sigaction *, struct sigaction *)
  __attribute__((import_module("waste_kernel"), import_name("sigaction_v1")));

int sigaction(int signum, const struct sigaction *action, struct sigaction *old) {
  int result = waste_kernel_sigaction_v1(signum, action, old);
  if (result < 0) { errno = -result; return -1; }
  return result;
}

sighandler_t signal(int signum, sighandler_t handler) {
  struct sigaction action = {0}, old;
  if (handler == SIG_ERR) { errno = EINVAL; return SIG_ERR; }
  action.sa_handler = handler;
  action.sa_flags = SA_RESTART;
  if (sigaction(signum, &action, &old) < 0) return SIG_ERR;
  return old.sa_handler;
}
