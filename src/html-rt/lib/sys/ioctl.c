/* sys/ioctl.c — Device control stub for the WASTE guest libc. */

#include "../include/helper.h"
#include "../include/sys_ioctl.h"

__attribute__((import_module("waste_kernel"), import_name("ioctl_v1")))
extern i32 waste_kernel_ioctl_v1(i32 descriptor, u32 request, void *argument);

i32 ioctl(i32 fd,u32 request,void*argument){
  return waste_kernel_ioctl_v1(fd, request, argument);
}
