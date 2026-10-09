#ifndef WASTE_NATIVE_TERMINAL_H
#define WASTE_NATIVE_TERMINAL_H
#include "lib/include/kernel.h"
#include <signal.h>
#include <sys/stat.h>
#include <termios.h>

/* Private duplicates retain only inherited stdio capabilities. Guest aliases
 * refer to IDs 0..2 in their OFDs, never these host descriptors. */
typedef struct {
    int enabled, handles[3], tty[3], changed[3];
    struct stat metadata[3];
    struct termios saved[3];
    struct sigaction previous[6];
    int signal_count;
} native_terminal;

int native_terminal_open(native_terminal *, posix_kernel *);
int native_terminal_close(native_terminal *);
int native_terminal_resize(native_terminal *, posix_kernel *);
int native_terminal_signal(void);
int native_terminal_take_signal(void);
int native_terminal_resized(void);
#endif
