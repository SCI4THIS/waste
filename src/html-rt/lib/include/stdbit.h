#ifndef WASTE_STDBIT_H
#define WASTE_STDBIT_H

/* C23 <stdbit.h> compatibility for the freestanding guest sysroot.  The
   current coreutils sources only require the declarations/macros below while
   the implementation remains in gnulib when a utility needs one. */
#include <stdint.h>

#define stdc_has_single_bit(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)
#define stdc_bit_ceil(x) ((x) <= 1 ? 1 : (x))
#define stdc_bit_floor(x) (x)
#define stdc_bit_width(x) ((x) ? (unsigned) (sizeof(x) * 8) : 0)
#define stdc_rotate_right(x, s) \
  (((x) >> (s)) | ((x) << (sizeof(x) * 8 - (s))))
#define stdc_rotate_left(x, s) \
  (((x) << (s)) | ((x) >> (sizeof(x) * 8 - (s))))
#define stdc_leading_zeros(x) \
  ((x) == 0 ? (unsigned) (sizeof(x) * 8) : (unsigned) __builtin_clz((unsigned) (x)))
#define stdc_trailing_zeros(x) \
  ((x) == 0 ? (unsigned) (sizeof(x) * 8) : (unsigned) __builtin_ctz((unsigned) (x)))
#define stdc_leading_zeros_ull(x) \
  ((x) == 0 ? 64u : (unsigned) __builtin_clzll((unsigned long long) (x)))

#endif
