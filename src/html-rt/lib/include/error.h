#ifndef WASTE_ERROR_H
#define WASTE_ERROR_H

#include <stdarg.h>

extern unsigned int error_message_count;
extern void (*error_print_progname)(void);
void error(int, int, const char *, ...);
void error_at_line(int, int, const char *, unsigned int, const char *, ...);

#endif
