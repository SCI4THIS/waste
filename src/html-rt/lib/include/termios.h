#ifndef WASTE_TERMIOS_H
#define WASTE_TERMIOS_H

#include "helper.h"

#define ICRNL 0x0001u
#define ISIG 0x0001u
#define ICANON 0x0002u
#define ECHO 0x0008u
#define IEXTEN 0x8000u
#define TCSANOW 0

#define VINTR 0
#define VEOF 4
#define VERASE 2
#define VKILL 3
#define VMIN 6
#define VTIME 5

typedef struct {
  u32 c_iflag;
  u32 c_oflag;
  u32 c_cflag;
  u32 c_lflag;
  u8 c_cc[20];
} waste_termios;

i32 tcgetattr(i32 descriptor, waste_termios *termios);
i32 tcsetattr(i32 descriptor, i32 action, const waste_termios *termios);
i32 tcflow(i32 descriptor, i32 action);

#endif
