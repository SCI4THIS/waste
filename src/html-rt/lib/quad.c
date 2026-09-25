/* Minimal compiler-rt quad helpers for the Coreutils Wasm ABI.
 *
 * Wasm clang represents long double as IEEE binary128 in two i64 parameters.
 * These helpers preserve that ABI while using double arithmetic internally;
 * this is sufficient for the current true image, which only needs conversion,
 * comparison, and simple formatting-path arithmetic.
 */

#include "include/helper.h"

static double quad_to_double(u64 low, u64 high) {
  u64 exponent = (high >> 48) & 0x7fffULL;
  u64 fraction_high = high & 0x0000ffffffffffffULL;
  u64 fraction_low = low;
  double value;
  if (exponent == 0x7fffULL) {
    if (fraction_high || fraction_low) return __builtin_nan("");
    return (high >> 63) ? -__builtin_inf() : __builtin_inf();
  }
  if (exponent == 0) {
    if (!fraction_high && !fraction_low) return (high >> 63) ? -0.0 : 0.0;
    value = (double)fraction_high / 0x1000000000000p0;
    value += (double)fraction_low / 0x1000000000000000000000000000000p0;
    value = __builtin_ldexp(value, -16382);
  } else {
    value = 1.0 + (double)fraction_high / 0x1000000000000p0;
    value += (double)fraction_low / 0x1000000000000000000000000000000p0;
    value = __builtin_ldexp(value, (i32)exponent - 16383);
  }
  return (high >> 63) ? -value : value;
}

static void double_to_quad(u64 *out, double value) {
  union { double d; u64 u; } bits;
  bits.d = value;
  u64 sign = bits.u >> 63;
  u64 exponent = (bits.u >> 52) & 0x7ffULL;
  u64 fraction = bits.u & 0xfffffffffffffULL;
  if (exponent == 0) { out[0] = 0; out[1] = sign << 63; return; }
  if (exponent == 0x7ff) {
    out[0] = 0;
    out[1] = (sign << 63) | (0x7fffULL << 48) |
             (fraction ? 1ULL << 47 : 0);
    return;
  }
  out[0] = fraction << 60;
  out[1] = (sign << 63) |
           ((exponent - 1023 + 16383) << 48) | (fraction >> 4);
}

static i32 quad_compare(u64 al, u64 ah, u64 bl, u64 bh) {
  double a = quad_to_double(al, ah), b = quad_to_double(bl, bh);
  if (a != a || b != b) return 2;
  return a < b ? -1 : a > b ? 1 : 0;
}

void __floatsitf(u64 *out, i32 value) { double_to_quad(out, (double)value); }
void __floatunsitf(u64 *out, u32 value) { double_to_quad(out, (double)value); }
i32 __fixtfsi(u64 low, u64 high) { return (i32)quad_to_double(low, high); }
double __trunctfdf2(u64 low, u64 high) { return quad_to_double(low, high); }

i32 __unordtf2(u64 al, u64 ah, u64 bl, u64 bh) {
  return quad_compare(al, ah, bl, bh) == 2;
}
i32 __eqtf2(u64 al, u64 ah, u64 bl, u64 bh) {
  return quad_compare(al, ah, bl, bh) != 0;
}
i32 __netf2(u64 al, u64 ah, u64 bl, u64 bh) {
  return quad_compare(al, ah, bl, bh) == 0 ? 0 : 1;
}
i32 __getf2(u64 al, u64 ah, u64 bl, u64 bh) {
  i32 result = quad_compare(al, ah, bl, bh); return result == 2 ? 1 : result;
}
i32 __lttf2(u64 al, u64 ah, u64 bl, u64 bh) {
  return quad_compare(al, ah, bl, bh) < 0 ? -1 : 0;
}
i32 __gttf2(u64 al, u64 ah, u64 bl, u64 bh) {
  return quad_compare(al, ah, bl, bh) > 0 ? 1 : 0;
}

void __addtf3(u64 *out, u64 al, u64 ah, u64 bl, u64 bh) {
  double_to_quad(out, quad_to_double(al, ah) + quad_to_double(bl, bh));
}
void __subtf3(u64 *out, u64 al, u64 ah, u64 bl, u64 bh) {
  double_to_quad(out, quad_to_double(al, ah) - quad_to_double(bl, bh));
}
void __multf3(u64 *out, u64 al, u64 ah, u64 bl, u64 bh) {
  double_to_quad(out, quad_to_double(al, ah) * quad_to_double(bl, bh));
}

void frexpl(u64 *out, u64 low, u64 high, i32 *exponent) {
  int exp = 0;
  double value = __builtin_frexp(quad_to_double(low, high), &exp);
  double_to_quad(out, value);
  if (exponent) *exponent = exp;
}
