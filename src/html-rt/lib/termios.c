/* termios.c — Terminal control for the WASTE guest libc. */

#include "include/helper.h"
#include "include/termios.h"

__attribute__((import_module("waste_kernel"), import_name("tcgetattr_v1")))
extern int waste_kernel_tcgetattr_v1(int descriptor, struct termios *t);
__attribute__((import_module("waste_kernel"), import_name("tcsetattr_v1")))
extern int waste_kernel_tcsetattr_v1(int descriptor, int action,
                                      const struct termios *t);

int tcgetattr(int descriptor, struct termios *termios_p) {
  return waste_kernel_tcgetattr_v1(descriptor, termios_p);
}

int tcsetattr(int descriptor, int action, const struct termios *termios_p) {
  return waste_kernel_tcsetattr_v1(descriptor, action, termios_p);
}

int tcflow(int descriptor, int action) {
  (void)descriptor; (void)action; return 0;
}

int tcdrain(int descriptor) { (void)descriptor; return 0; }

int tcflush(int descriptor, int queue_selector) {
  (void)descriptor; (void)queue_selector; return 0;
}

speed_t cfgetospeed(const struct termios *t) {
  return t ? t->c_ospeed : 0;
}

speed_t cfgetispeed(const struct termios *t) {
  return t ? t->c_ispeed : 0;
}

int cfsetospeed(struct termios *t, speed_t speed) {
  if (!t) return -1;
  t->c_ospeed = speed;
  return 0;
}

int cfsetispeed(struct termios *t, speed_t speed) {
  if (!t) return -1;
  t->c_ispeed = speed;
  return 0;
}
