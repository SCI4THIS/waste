/* termios.c — Terminal control for the WASTE guest libc. */

#include "include/helper.h"
#include "include/termios.h"

__attribute__((import_module("waste_kernel"), import_name("tcgetattr_v1")))
extern i32 waste_kernel_tcgetattr_v1(i32 descriptor, waste_termios *termios);
__attribute__((import_module("waste_kernel"), import_name("tcsetattr_v1")))
extern i32 waste_kernel_tcsetattr_v1(i32 descriptor, i32 action,
                                      const waste_termios *termios);

i32 tcgetattr(i32 descriptor, waste_termios *termios) {
  return waste_kernel_tcgetattr_v1(descriptor, termios);
}

i32 tcsetattr(i32 descriptor, i32 action, const waste_termios *termios) {
  return waste_kernel_tcsetattr_v1(descriptor, action, termios);
}

i32 tcflow(i32 descriptor,i32 action){(void)descriptor;(void)action;return 0;}
