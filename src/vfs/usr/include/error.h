#ifndef WASTE_ERROR_H
#define WASTE_ERROR_H

#include <waste/abi/availability.h>

#include <stdarg.h>

extern unsigned int error_message_count WASTE_UNAVAILABLE("Package-owned gnulib error state");
extern void (*error_print_progname)(void) WASTE_UNAVAILABLE("Package-owned gnulib error state");
void error(int, int, const char *, ...) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
void error_at_line(int, int, const char *, unsigned int, const char *, ...) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");

#endif
