/* Minimal compiler-rt quad helpers for the Coreutils Wasm ABI.
 *
 * Wasm clang represents long double as IEEE binary128 in two i64 parameters.
 * Comparisons inspect the full representation without narrowing. Arithmetic
 * and conversion helpers still use double internally; they do not provide
 * general correctly rounded binary128 arithmetic.
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
  u64 a_magnitude = ah & 0x7fffffffffffffffULL;
  u64 b_magnitude = bh & 0x7fffffffffffffffULL;
  const u64 infinity = 0x7fff000000000000ULL;
  if (a_magnitude > infinity || (a_magnitude == infinity && al) ||
      b_magnitude > infinity || (b_magnitude == infinity && bl)) return 2;
  if (!(a_magnitude | al | b_magnitude | bl)) return 0; /* Signed zeros. */
  if ((ah ^ bh) >> 63) return ah >> 63 ? -1 : 1;
  i32 order = a_magnitude < b_magnitude ? -1 : a_magnitude > b_magnitude ? 1 :
              al < bl ? -1 : al > bl ? 1 : 0;
  return ah >> 63 ? -order : order;
}

void __floatsitf(u64 *out, i32 value) { double_to_quad(out, (double)value); }
void __floatunsitf(u64 *out, u32 value) { double_to_quad(out, (double)value); }
i32 __fixtfsi(u64 low, u64 high) { return (i32)quad_to_double(low, high); }
double __trunctfdf2(u64 low, u64 high) { return quad_to_double(low, high); }

i32 __unordtf2(u64 al, u64 ah, u64 bl, u64 bh) {
  return quad_compare(al, ah, bl, bh) == 2;
}
i32 __eqtf2(u64 al, u64 ah, u64 bl, u64 bh) {
  i32 result = quad_compare(al, ah, bl, bh); return result == 2 ? 1 : result;
}
i32 __netf2(u64 al, u64 ah, u64 bl, u64 bh) {
  return __eqtf2(al, ah, bl, bh);
}
i32 __getf2(u64 al, u64 ah, u64 bl, u64 bh) {
  i32 result = quad_compare(al, ah, bl, bh); return result == 2 ? -1 : result;
}
i32 __lttf2(u64 al, u64 ah, u64 bl, u64 bh) {
  return __eqtf2(al, ah, bl, bh);
}
i32 __gttf2(u64 al, u64 ah, u64 bl, u64 bh) {
  return __getf2(al, ah, bl, bh);
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
void __divtf3(u64 *out, u64 al, u64 ah, u64 bl, u64 bh) {
  double_to_quad(out, quad_to_double(al, ah) / quad_to_double(bl, bh));
}
i32 __letf2(u64 al, u64 ah, u64 bl, u64 bh) {
  i32 result = quad_compare(al, ah, bl, bh);
  return result == 2 ? 1 : result;
}

void frexpl(u64 *out, u64 low, u64 high, i32 *exponent) {
  int exp = 0;
  double value = __builtin_frexp(quad_to_double(low, high), &exp);
  double_to_quad(out, value);
  if (exponent) *exponent = exp;
}
