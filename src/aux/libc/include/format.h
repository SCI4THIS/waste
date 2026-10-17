#ifndef WASTE_LIBC_FORMAT_H
#define WASTE_LIBC_FORMAT_H

#include <stddef.h>
#include <stdarg.h>

/* Private parsed formatting fields shared by the byte and wchar sinks. */
typedef struct {
  int left, plus, space, alternate, zero, width, precision, length;
} format_spec;
enum { LEN_DEFAULT, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T, LEN_BIG_L };

__attribute__((visibility("hidden")))
int __waste_format_float(char *destination, size_t capacity,
                         const format_spec *spec, int conversion, va_list *args);

#endif
