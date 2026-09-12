/* common.h — Shared typedefs, extern declarations, and inline helpers for the
 * WASTE guest libc files.  Each file is compiled together into a single
 * waste-libc.wasm binary, but static helpers defined here give each translation
 * unit its own copy. */

#ifndef WASTE_LIBC_COMMON_H
#define WASTE_LIBC_COMMON_H

#include <stdarg.h>

typedef unsigned int u32;
typedef signed int i32;
typedef unsigned long long u64;
typedef signed long long i64;

extern void *malloc(u32 size);
extern void *realloc(void *pointer, u32 size);
extern void free(void *pointer);
extern i32 *__errno_location(void);

/* --- Inline helpers shared across multiple libc files --- */

static u32 c_length(const char *text) {
  u32 length = 0;
  if ((u32)text >= __builtin_wasm_memory_size(0) * 65536U) return 0;
  if (text) while (text[length]) length++;
  return length;
}

static i32 c_compare(const char *left, const char *right) {
  u32 at = 0;
  while (left[at] && left[at] == right[at]) at++;
  return (unsigned char)left[at] - (unsigned char)right[at];
}

static void bytes_copy(void *destination, const void *source, u32 count) {
  unsigned char *to = destination;
  const unsigned char *from = source;
  if (to < from) for (u32 at = 0; at < count; at++) to[at] = from[at];
  else while (count) { count--; to[count] = from[count]; }
}

static void bytes_zero(void *destination, u32 count) {
  unsigned char *to = destination;
  for (u32 at = 0; at < count; at++) to[at] = 0;
}

static i32 valid_pointer(const void *p) {
  return p && (u32)p < __builtin_wasm_memory_size(0) * 65536U;
}

static i32 lower_ascii(i32 c) {
  return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

#endif /* WASTE_LIBC_COMMON_H */
