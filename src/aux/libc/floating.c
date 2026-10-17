/* Guest C-locale floating conversions. Round an integer ratio directly to
 * binary32/64/128; never use the double-based quad arithmetic helpers.
 */
#include <errno.h>
#include <float.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && DBL_MANT_DIG == 53 &&
               LDBL_MANT_DIG == 113 && sizeof(long double) == 16,
               "floating conversion requires the guest IEEE formats");
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "guest word order");

/* Every binary128 rounding midpoint has a terminating decimal expansion
 * shorter than this prefix: its smallest quantum is 2^(MIN_EXP-MANT_DIG-1),
 * and multiplying by the corresponding power of five uses fewer decimal
 * digits than binary bits. A nonzero discarded tail can only break a tie.
 * Four bits per retained decimal digit also bound both scaled ratio operands.
 * Storage is private, fixed, and derived from the widest guest format.
 */
enum {
  FLOAT_DIGITS = LDBL_MANT_DIG - LDBL_MIN_EXP + 3,
  FLOAT_WORDS = (4 * FLOAT_DIGITS + 255) / 32
};

typedef struct { uint32_t word[FLOAT_WORDS]; unsigned used; } float_integer;
typedef struct {
  float_integer number;
  size_t consumed;
  int64_t scale;
  unsigned digits;
  int negative, hexadecimal, special, sticky;
} float_input;
typedef struct { uint64_t low, high; size_t consumed; } float_value;
typedef struct { int precision, minimum, maximum, decimal_min, decimal_max; } float_format;

static const float_format format_float = {
  FLT_MANT_DIG, FLT_MIN_EXP - 1, FLT_MAX_EXP - 1, FLT_MIN_10_EXP, FLT_MAX_10_EXP
};
static const float_format format_double = {
  DBL_MANT_DIG, DBL_MIN_EXP - 1, DBL_MAX_EXP - 1, DBL_MIN_10_EXP, DBL_MAX_10_EXP
};
static const float_format format_long_double = {
  LDBL_MANT_DIG, LDBL_MIN_EXP - 1, LDBL_MAX_EXP - 1, LDBL_MIN_10_EXP, LDBL_MAX_10_EXP
};

static unsigned input_character(const void *text, size_t at, int wide) {
  return wide ? (unsigned)((const wchar_t *)text)[at] :
                ((const unsigned char *)text)[at];
}
static int ascii_space(unsigned c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static unsigned ascii_lower(unsigned c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int digit(unsigned c) {
  if (c >= '0' && c <= '9') return (int)(c - '0');
  c = ascii_lower(c);
  return c >= 'a' && c <= 'f' ? (int)(c - 'a' + 10) : -1;
}
static int matches(const void *text, size_t at, int wide, const char *word) {
  for (; *word; word++, at++)
    if (ascii_lower(input_character(text, at, wide)) != (unsigned char)*word) return 0;
  return 1;
}
static int payload_character(unsigned c) {
  c = ascii_lower(c);
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

static int multiply_add(float_integer *value, uint32_t multiplier, uint32_t add) {
  uint64_t carry = add;
  for (unsigned i = 0; i < value->used; i++) {
    uint64_t product = (uint64_t)value->word[i] * multiplier + carry;
    value->word[i] = (uint32_t)product;
    carry = product >> 32;
  }
  if (carry) {
    if (value->used == FLOAT_WORDS) return 0;
    value->word[value->used++] = (uint32_t)carry;
  }
  return 1;
}

typedef struct { uint32_t value, multiplier; unsigned digits; } float_chunk;
static int append_digit(float_input *input, float_chunk *chunk, unsigned radix,
                        unsigned limit, unsigned value) {
  chunk->value = chunk->value * radix + value;
  chunk->multiplier *= radix;
  if (++chunk->digits == limit) {
    if (!multiply_add(&input->number, chunk->multiplier, chunk->value)) return 0;
    chunk->digits = 0; chunk->value = 0; chunk->multiplier = 1;
  }
  return 1;
}

static int scan_float(float_input *input, const void *text, int wide) {
  size_t at = 0;
  while (ascii_space(input_character(text, at, wide))) at++;
  unsigned c = input_character(text, at, wide);
  if (c == '+' || c == '-') { input->negative = c == '-'; at++; }
  if (matches(text, at, wide, "inf")) {
    input->special = 1;
    input->consumed = at + (matches(text, at, wide, "infinity") ? 8 : 3);
    return 1;
  }
  if (matches(text, at, wide, "nan")) {
    input->special = 2;
    at += 3;
    input->consumed = at;
    if (input_character(text, at, wide) == '(') {
      at++;
      while (payload_character(input_character(text, at, wide))) at++;
      if (input_character(text, at, wide) == ')') input->consumed = at + 1;
    }
    return 1; /* All accepted payload spellings yield the canonical quiet NaN. */
  }
  if (input_character(text, at, wide) == '0' &&
      ascii_lower(input_character(text, at + 1, wide)) == 'x') {
    int first = digit(input_character(text, at + 2, wide));
    if (first >= 0 || (input_character(text, at + 2, wide) == '.' &&
                     digit(input_character(text, at + 3, wide)) >= 0)) {
      input->hexadecimal = 1;
      at += 2;
    }
  }
  unsigned radix = input->hexadecimal ? 16 : 10;
  unsigned chunk_limit = input->hexadecimal ? 7 : 9;
  float_chunk chunk = {0, 1, 0};
  unsigned pending_zeros = 0;
  int any = 0, point = 0;
  int64_t fractional = 0, dropped = 0;
  for (;;) {
    c = input_character(text, at, wide);
    int d = digit(c);
    if (d >= 0 && (unsigned)d < radix) {
      any = 1;
      if (point) fractional++;
      if (input->digits || d) {
        if (input->digits < FLOAT_DIGITS) {
          input->digits++;
          /* Keep trailing prefix zeros in the scale. Even a discarded
             nonzero tail needs only its sticky bit to break an exact tie. */
          if (!d) pending_zeros++;
          else {
            while (pending_zeros) {
              if (!append_digit(input, &chunk, radix, chunk_limit, 0)) return 0;
              pending_zeros--;
            }
            if (!append_digit(input, &chunk, radix, chunk_limit, (unsigned)d)) return 0;
          }
        } else { dropped++; input->sticky |= d != 0; }
      }
      at++;
    } else if (c == '.' && !point) { point = 1; at++; }
    else break;
  }
  if (!any) { input->negative = 0; return 1; }
  if (chunk.digits && !multiply_add(&input->number, chunk.multiplier, chunk.value)) return 0;
  input->digits -= pending_zeros;
  int64_t exponent = 0;
  if (ascii_lower(input_character(text, at, wide)) == (input->hexadecimal ? 'p' : 'e')) {
    size_t mark = at++;
    int negative = 0;
    c = input_character(text, at, wide);
    if (c == '+' || c == '-') { negative = c == '-'; at++; }
    size_t first = at;
    /* This exceeds every possible adjustment from a guest size_t-length
       mantissa. Saturation never prevents a large exponent cancelling it. */
    const int64_t limit = INT64_MAX / 16;
    while ((c = input_character(text, at, wide)) >= '0' && c <= '9') {
      int d = (int)(c - '0');
      exponent = exponent > (limit - d) / 10 ? limit : exponent * 10 + d;
      at++;
    }
    if (at == first) at = mark;
    if (negative) exponent = -exponent;
  }
  input->consumed = at;
  input->scale = exponent + (dropped + pending_zeros - fractional) * (input->hexadecimal ? 4 : 1);
  return 1;
}

static unsigned integer_bits(const float_integer *value) {
  if (!value->used) return 0;
  return 32 * (value->used - 1) + 32 - (unsigned)__builtin_clz(value->word[value->used - 1]);
}
static uint32_t shifted_word(const float_integer *value, unsigned at, unsigned shift) {
  unsigned whole = shift / 32, bits = shift % 32;
  if (at < whole) return 0;
  at -= whole;
  uint32_t result = at < value->used ? value->word[at] << bits : 0;
  if (bits && at && at - 1 < value->used) result |= value->word[at - 1] >> (32 - bits);
  return result;
}
static int compare_shift(const float_integer *a, const float_integer *b, unsigned shift) {
  unsigned count = b->used + shift / 32 + (shift % 32 != 0);
  if (a->used > count) count = a->used;
  while (count) {
    count--;
    uint32_t left = count < a->used ? a->word[count] : 0;
    uint32_t right = shifted_word(b, count, shift);
    if (left != right) return left < right ? -1 : 1;
  }
  return 0;
}
static void subtract_shift(float_integer *a, const float_integer *b, unsigned shift) {
  uint64_t borrow = 0;
  for (unsigned i = 0; i < a->used; i++) {
    uint64_t right = (uint64_t)shifted_word(b, i, shift) + borrow;
    uint32_t left = a->word[i];
    a->word[i] = (uint32_t)((uint64_t)left - right);
    borrow = left < right;
  }
  while (a->used && !a->word[a->used - 1]) a->used--;
}
static int shift_left(float_integer *value, unsigned shift) {
  unsigned count = value->used + shift / 32 + (shift % 32 != 0);
  if (count > FLOAT_WORDS) return 0;
  for (unsigned i = count; i; i--) value->word[i - 1] = shifted_word(value, i - 1, shift);
  value->used = count;
  while (value->used && !value->word[value->used - 1]) value->used--;
  return 1;
}
static int power_five(float_integer *value, unsigned power) {
  /* 5^13 fits a word: bounded word multiplies avoid exponent-sized FP loops. */
  while (power >= 13) {
    if (!multiply_add(value, 1220703125U, 0)) return 0;
    power -= 13;
  }
  uint32_t factor = 1;
  while (power--) factor *= 5;
  return multiply_add(value, factor, 0);
}

static void encode(float_value *value, const float_format *format, int negative,
                   unsigned exponent, const uint32_t *fraction) {
  if (format->precision == 24) {
    value->low = ((uint32_t)negative << 31) | (exponent << 23) | (fraction[0] & 0x7fffffU);
  } else if (format->precision == 53) {
    value->low = ((uint64_t)negative << 63) | ((uint64_t)exponent << 52) |
                 (((uint64_t)fraction[1] << 32 | fraction[0]) & 0xfffffffffffffULL);
  } else {
    value->low = (uint64_t)fraction[1] << 32 | fraction[0];
    value->high = ((uint64_t)negative << 63) | ((uint64_t)exponent << 48) |
                  (((uint64_t)fraction[3] << 32 | fraction[2]) & 0xffffffffffffULL);
  }
}

static float_value convert_float(const void *text, int wide, const float_format *format) {
  float_input input = {0};
  float_integer denominator = {0};
  float_value result = {0};
  uint32_t quotient[4] = {0};
  unsigned bias = (unsigned)(1 - format->minimum);
  if (!scan_float(&input, text, wide)) goto capacity_error;
  result.consumed = input.consumed;
  if (input.special) {
    if (input.special == 2) {
      unsigned quiet = (unsigned)format->precision - 2;
      quotient[quiet / 32] = 1U << (quiet % 32);
    }
    encode(&result, format, input.negative, 2 * bias + 1, quotient);
    return result;
  }
  encode(&result, format, input.negative, 0, quotient);
  if (!input.number.used) return result; /* Exact signed zero, even at huge scale. */
  denominator.word[0] = 1; denominator.used = 1;
  if (!input.hexadecimal) {
    int64_t order = (int64_t)input.digits + input.scale;
    if (order > format->decimal_max + 1) goto overflow;
    /* A conservative decimal bound below half the smallest subnormal. */
    if (order < format->decimal_min - format->precision * 30103 / 100000 - 2)
      goto underflow;
    if (input.scale >= 0) {
      if (!power_five(&input.number, (unsigned)input.scale)) goto capacity_error;
    } else if (!power_five(&denominator, (unsigned)-input.scale)) goto capacity_error;
  }
  int difference = (int)integer_bits(&input.number) - (int)integer_bits(&denominator);
  int comparison = difference >= 0 ? compare_shift(&input.number, &denominator, (unsigned)difference) :
                                  -compare_shift(&denominator, &input.number, (unsigned)-difference);
  int64_t exponent = input.scale + difference - (comparison < 0);
  if (exponent > format->maximum) goto overflow;
  int quantum_min = format->minimum - format->precision + 1;
  if (exponent < quantum_min - 1) goto underflow;
  int64_t quantum = exponent - format->precision + 1;
  if (quantum < quantum_min) quantum = quantum_min;
  int64_t shift = input.scale - quantum;
  if (shift >= 0) {
    if (!shift_left(&input.number, (unsigned)shift)) goto capacity_error;
  } else if (!shift_left(&denominator, (unsigned)-shift)) goto capacity_error;
  difference = (int)integer_bits(&input.number) - (int)integer_bits(&denominator);
  for (int bit = difference; bit >= 0; bit--) {
    if (compare_shift(&input.number, &denominator, (unsigned)bit) >= 0) {
      subtract_shift(&input.number, &denominator, (unsigned)bit);
      quotient[(unsigned)bit / 32] |= 1U << ((unsigned)bit % 32);
    }
  }
  int inexact = input.number.used != 0 || input.sticky;
  comparison = -compare_shift(&denominator, &input.number, 1);
  if (comparison > 0 || (comparison == 0 && ((quotient[0] & 1) || input.sticky))) {
    uint64_t carry = 1;
    for (unsigned i = 0; i < 4 && carry; i++) {
      carry += quotient[i]; quotient[i] = (uint32_t)carry; carry >>= 32;
    }
  }
  if (exponent < format->minimum) {
    if (inexact) errno = ERANGE; /* Tininess before rounding; exact subnormals are valid. */
    exponent = format->minimum;
  }
  unsigned top = (unsigned)format->precision;
  if (quotient[top / 32] & (1U << (top % 32))) {
    for (unsigned i = 0; i < 4; i++)
      quotient[i] = (quotient[i] >> 1) | (i < 3 ? quotient[i + 1] << 31 : 0);
    if (++exponent > format->maximum) goto overflow;
  }
  unsigned hidden = (unsigned)format->precision - 1;
  unsigned field = quotient[hidden / 32] & (1U << (hidden % 32)) ?
                   (unsigned)(exponent + bias) : 0;
  encode(&result, format, input.negative, field, quotient);
  return result;
overflow:
  quotient[0] = quotient[1] = quotient[2] = quotient[3] = 0;
  encode(&result, format, input.negative, 2 * bias + 1, quotient);
  errno = ERANGE;
  return result;
underflow:
  errno = ERANGE;
  return result;
capacity_error:
  /* Bounds above cover every guest format; fail explicitly if that invariant
     changes instead of writing past private storage or returning a partial value. */
  errno = ENOMEM;
  return result;
}

float strtof(const char *text, char **end) {
  float_value value = convert_float(text, 0, &format_float);
  if (end) *end = (char *)text + value.consumed;
  union { uint32_t bits; float value; } result = {(uint32_t)value.low};
  return result.value;
}
double strtod(const char *text, char **end) {
  float_value value = convert_float(text, 0, &format_double);
  if (end) *end = (char *)text + value.consumed;
  union { uint64_t bits; double value; } result = {value.low};
  return result.value;
}
long double strtold(const char *text, char **end) {
  float_value value = convert_float(text, 0, &format_long_double);
  if (end) *end = (char *)text + value.consumed;
  union { uint64_t bits[2]; long double value; } result = {{value.low, value.high}};
  return result.value;
}
float wcstof(const wchar_t *text, wchar_t **end) {
  float_value value = convert_float(text, 1, &format_float);
  if (end) *end = (wchar_t *)text + value.consumed;
  union { uint32_t bits; float value; } result = {(uint32_t)value.low};
  return result.value;
}
double wcstod(const wchar_t *text, wchar_t **end) {
  float_value value = convert_float(text, 1, &format_double);
  if (end) *end = (wchar_t *)text + value.consumed;
  union { uint64_t bits; double value; } result = {value.low};
  return result.value;
}
long double wcstold(const wchar_t *text, wchar_t **end) {
  float_value value = convert_float(text, 1, &format_long_double);
  if (end) *end = (wchar_t *)text + value.consumed;
  union { uint64_t bits[2]; long double value; } result = {{value.low, value.high}};
  return result.value;
}
