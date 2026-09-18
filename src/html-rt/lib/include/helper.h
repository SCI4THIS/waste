/* helper.h — Shared typedefs, extern declarations, and inline helpers for the
 * WASTE guest libc files.  Each file is compiled together into a single
 * waste-libc.wasm binary, but static helpers defined here give each translation
 * unit its own copy. */

#ifndef WASTE_LIBC_HELPER_H
#define WASTE_LIBC_HELPER_H

#include <stdarg.h>

typedef unsigned int u32;
typedef signed int i32;
typedef unsigned long long u64;
typedef signed long long i64;

extern void *malloc(u32 size);
extern void *realloc(void *pointer, u32 size);
extern void free(void *pointer);
extern i32 *__errno_location(void);

/* --- Guest POSIX ABI types (wasm32) --- */

#define WASTE_FD_SETSIZE 1024
#define WASTE_NFDBITS    32

typedef struct { u32 fds_bits[WASTE_FD_SETSIZE / WASTE_NFDBITS]; } waste_fd_set;
typedef struct { i64 tv_sec;  i32 tv_usec; } waste_timeval;
typedef struct { i64 tv_sec;  i32 tv_nsec; } waste_timespec;
typedef struct { u32 sig[4]; } waste_sigset_t;

_Static_assert(sizeof(waste_fd_set)   == 128, "fd_set must be 128 bytes");
_Static_assert(sizeof(waste_timeval)  ==  16, "timeval must be 16 bytes");
_Static_assert(sizeof(waste_timespec) ==  16, "timespec must be 16 bytes");
_Static_assert(sizeof(waste_sigset_t) ==  16, "sigset_t must be 16 bytes");

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

static i32 unsupported(void) {
  *__errno_location() = 38; /* ENOSYS */
  return -1;
}

#endif /* WASTE_LIBC_HELPER_H */
