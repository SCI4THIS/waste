/* helper.h — Shared typedefs, extern declarations, and inline helpers for the
 * WASTE guest libc files.  Each file is compiled together into a single
 * waste-libc.wasm binary, but static helpers defined here give each translation
 * unit its own copy. */

#ifndef WASTE_LIBC_HELPER_H
#define WASTE_LIBC_HELPER_H

#include <stdarg.h>
#include <stddef.h>

typedef unsigned int u32;
typedef signed int i32;
typedef unsigned long long u64;
typedef signed long long i64;
typedef unsigned char u8;

extern void *malloc(size_t size);
extern void *realloc(void *pointer, size_t size);
extern void free(void *pointer);
extern i32 *__errno_location(void);

#include <waste/abi/posix.h>

/* --- Inline helpers shared across multiple libc files --- */

static u32 c_length(const char *text) {
  u32 length = 0;
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
  /* Shared objects occupy sparse high virtual pages beyond the contiguous
     memory.size heap.  The engine validates every actual load/store against
     the process page map, so non-null DSO pointers are valid libc inputs. */
  return p != 0;
}

static i32 lower_ascii(i32 c) {
  return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static i32 unsupported(void) {
  *__errno_location() = 38; /* ENOSYS */
  return -1;
}

#endif /* WASTE_LIBC_HELPER_H */
