/* sys/select.c — Guest-facing select wrappers.
 *
 * The engine owns descriptor state and the fd_set ABI.  Keep the import
 * versioned so the host can evolve this operation without silently changing
 * the calling convention, and translate the engine's negative errno result
 * into the libc convention (-1 plus errno).
 */

#include "../include/helper.h"

__attribute__((import_module("waste_kernel"), import_name("select_v1")))
extern i32 waste_kernel_select_v1(i32 nfds, waste_fd_set *readfds,
                                   waste_fd_set *writefds,
                                   waste_fd_set *exceptfds,
                                   waste_timeval *timeout);

__attribute__((import_module("waste_kernel"), import_name("pselect_v1")))
extern i32 waste_kernel_pselect_v1(i32 nfds, waste_fd_set *readfds,
                                    waste_fd_set *writefds,
                                    waste_fd_set *exceptfds,
                                    waste_timespec *timeout,
                                    waste_sigset_t *sigmask);

static i32 select_result(i32 result) {
  if (result < 0) {
    *__errno_location() = 0 - result;
    return -1;
  }
  return result;
}

i32 select(i32 nfds, void *readfds, void *writefds, void *exceptfds,
           void *timeout) {
  return select_result(waste_kernel_select_v1(
      nfds, (waste_fd_set *)readfds, (waste_fd_set *)writefds,
      (waste_fd_set *)exceptfds, (waste_timeval *)timeout));
}

i32 pselect(i32 nfds, void *readfds, void *writefds, void *exceptfds,
            const void *timeout, const void *sigmask) {
  return select_result(waste_kernel_pselect_v1(
      nfds, (waste_fd_set *)readfds, (waste_fd_set *)writefds,
      (waste_fd_set *)exceptfds, (waste_timespec *)timeout,
      (waste_sigset_t *)sigmask));
}
