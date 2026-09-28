#ifndef WASTE_SYS_IOCTL_H
#define WASTE_SYS_IOCTL_H

#include "helper.h"

#define TIOCGWINSZ 0x5413u
#define TIOCSWINSZ 0x5414u

typedef struct {
  unsigned short ws_row;
  unsigned short ws_col;
  unsigned short ws_xpixel;
  unsigned short ws_ypixel;
} waste_winsize;

i32 ioctl(i32 descriptor, u32 request, ...);

#endif
