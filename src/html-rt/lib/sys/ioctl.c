/* sys/ioctl.c — Device control stub for the WASTE guest libc. */

#include "../include/helper.h"
#include <sys/ioctl.h>

__attribute__((import_module("waste_kernel"), import_name("ioctl_v1")))
extern i32 waste_kernel_ioctl_v1(i32 descriptor, u32 request, void *argument);

i32 ioctl(i32 fd, u32 request, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, request);
  void *argument = __builtin_va_arg(ap, void *);
  __builtin_va_end(ap);
  i32 result = waste_kernel_ioctl_v1(fd, request, argument);
  if (result < -1) { *__errno_location() = -result; return -1; }
  return result;
}
