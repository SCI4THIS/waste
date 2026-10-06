#ifndef WASTE_TERMIOS_H
#define WASTE_TERMIOS_H

#include <stddef.h>

/* Input mode flags. */
#define ICRNL 0x0100u
#define IGNBRK 0x0001u
#define BRKINT 0x0002u
#define IGNPAR 0x0004u
#define INPCK 0x0010u
#define ISTRIP 0x0020u
#define INLCR 0x0040u
#define IGNCR 0x0080u
#define IXON  0x0400u
#define IXOFF 0x1000u
#define PARMRK 0x0008u
#define IXANY  0x0800u
#define IUTF8  0x4000u

/* Output mode flags. */
#define OPOST 0x0001u
#define ONLCR 0x0004u

/* Control mode flags. */
#define CSIZE 0x0030u
#define CS5   0x0000u
#define CS6   0x0010u
#define CS7   0x0020u
#define CS8   0x0030u
#define CSTOPB 0x0040u
#define CREAD  0x0080u
#define PARENB 0x0100u
#define HUPCL  0x0400u
#define CLOCAL 0x0800u

/* Local mode flags. */
#define ISIG 0x0001u
#define ICANON 0x0002u
#define ECHO 0x0008u
#define ECHOE 0x0010u
#define ECHOK 0x0020u
#define ECHONL 0x0040u
#define NOFLSH 0x0080u
#define TOSTOP 0x0100u
#define IEXTEN 0x8000u
#define ECHOCTL 0x0200u
#define ECHOKE 0x0800u

/* tcsetattr actions. */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* tcflow actions. */
#define TCOOFF 0
#define TCOON  1
#define TCIOFF 2
#define TCION  3

/* tcflush queue selectors. */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

/* Control character indices. */
#define VINTR  0
#define VQUIT  1
#define VERASE 2
#define VKILL  3
#define VEOF   4
#define VTIME  5
#define VMIN   6
#define VSTART 8
#define VSTOP  9
#define VSUSP  10
#define NCCS   32

/* Baud rates (symbolic constants — the guest delegates to the kernel). */
#define B0     0
#define B50    1
#define B75    2
#define B110   3
#define B134   4
#define B150   5
#define B200   6
#define B300   7
#define B600   8
#define B1200  9
#define B1800  10
#define B2400  11
#define B4800  12
#define B9600  13
#define B19200 14
#define B38400 15

/* POSIX types. */
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

typedef struct termios {
  tcflag_t c_iflag;
  tcflag_t c_oflag;
  tcflag_t c_cflag;
  tcflag_t c_lflag;
  cc_t c_line;
  cc_t c_cc[NCCS];
  unsigned char _pad[3];
  speed_t c_ispeed;
  speed_t c_ospeed;
} waste_termios;

int tcgetattr(int descriptor, struct termios *termios_p);
int tcsetattr(int descriptor, int action, const struct termios *termios_p);
int tcflow(int descriptor, int action);
int tcdrain(int descriptor);
int tcflush(int descriptor, int queue_selector);
speed_t cfgetospeed(const struct termios *termios_p);
speed_t cfgetispeed(const struct termios *termios_p);
int cfsetospeed(struct termios *termios_p, speed_t speed);
int cfsetispeed(struct termios *termios_p, speed_t speed);

#endif
