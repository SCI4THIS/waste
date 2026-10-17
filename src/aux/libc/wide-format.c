/* Bounded C.UTF-8 wide formatting. Floating output is derived from the exact
 * IEEE significand using integer decimal digits, including binary128; no
 * double-based quad helpers, allocations, mutable globals or host locale.
 */
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <wchar.h>
#include "include/format.h"

_Static_assert(FLT_RADIX == 2 && DBL_MANT_DIG == 53 && DBL_MIN_EXP == -1021 &&
               DBL_MAX_EXP == 1024 && LDBL_MANT_DIG == 113 &&
               LDBL_MIN_EXP == -16381 && LDBL_MAX_EXP == 16384 &&
               sizeof(long double) == 16 && sizeof(wchar_t) == 4 &&
               sizeof(size_t) == 4 && sizeof(ptrdiff_t) == 4 &&
               sizeof(long) == 4 && sizeof(intmax_t) == 8,
               "wide formatter requires guest IEEE and wchar formats");
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "guest word order");

/* An exact decimal expansion of m*2^e needs fewer than p+abs(e) digits.
 * These are private arithmetic bounds, derived from the widest guest type.
 * Decimal limbs leave room for a multiplication carry and rounding digit.
 */
enum { DECIMAL_DIGITS = LDBL_MANT_DIG - LDBL_MIN_EXP + 4,
       DECIMAL_WORDS = (DECIMAL_DIGITS + 8) / 9 + 1 };
typedef struct { uint32_t word[DECIMAL_WORDS]; unsigned used; } decimal_integer;
typedef struct { uint32_t word[4]; int exponent, negative, special; } binary_value;
typedef struct {
  wchar_t *text;
  size_t capacity, used;
  int error;
  char *bytes;
  int narrow;
} wide_output;

static void fail(wide_output *out, int error) { if (!out->error) out->error = error; }
static void emit(wide_output *out, wchar_t value) {
  if (out->error) return;
  if (out->used == INT_MAX ||
      (!out->narrow && out->used >= out->capacity - 1)) {
    fail(out, EOVERFLOW); return;
  }
  if (out->narrow) {
    if (out->capacity && out->used < out->capacity - 1)
      out->bytes[out->used] = (char)value;
    out->used++;
  } else out->text[out->used++] = value;
}
static void repeat(wide_output *out, wchar_t value, uint64_t count) {
  if (out->narrow) {
    while (count && !out->error && out->capacity && out->used < out->capacity - 1) {
      emit(out, value); count--;
    }
    if (out->error) return;
    if (count > (uint64_t)INT_MAX - out->used) fail(out, EOVERFLOW);
    else out->used += (size_t)count;
    return;
  }
  while (count-- && !out->error) emit(out, value);
}
static void ascii(wide_output *out, const char *text, size_t count) {
  for (size_t at = 0; at < count && !out->error; at++) emit(out, (unsigned char)text[at]);
}
static wchar_t sign_of(const format_spec *spec, int negative) {
  return negative ? '-' : spec->plus ? '+' : spec->space ? ' ' : 0;
}
static uint64_t padding(const format_spec *spec, uint64_t length) {
  return (uint64_t)spec->width > length ? (uint64_t)spec->width - length : 0;
}

static binary_value floating_argument(va_list *args, int extended) {
  binary_value value = {{0}, 0, 0, 0};
  unsigned exponent;
  if (extended) {
    long double number = va_arg(*args, long double);
    const unsigned char *bytes = (const unsigned char *)&number;
    for (unsigned at = 0; at < 16; at++) value.word[at / 4] |= (uint32_t)bytes[at] << (8 * (at % 4));
    value.negative = value.word[3] >> 31;
    exponent = (value.word[3] >> 16) & 0x7fff;
    value.word[3] &= 0xffff;
    if (exponent == 0x7fff) value.special = (value.word[0] | value.word[1] | value.word[2] | value.word[3]) ? 2 : 1;
    else {
      value.exponent = (exponent ? (int)exponent - 16383 : -16382) - 112;
      if (exponent) value.word[3] |= 0x10000;
    }
  } else {
    double number = va_arg(*args, double);
    const unsigned char *bytes = (const unsigned char *)&number;
    for (unsigned at = 0; at < 8; at++) value.word[at / 4] |= (uint32_t)bytes[at] << (8 * (at % 4));
    value.negative = value.word[1] >> 31;
    exponent = (value.word[1] >> 20) & 0x7ff;
    value.word[1] &= 0xfffff;
    if (exponent == 0x7ff) value.special = (value.word[0] | value.word[1]) ? 2 : 1;
    else {
      value.exponent = (exponent ? (int)exponent - 1023 : -1022) - 52;
      if (exponent) value.word[1] |= 0x100000;
    }
  }
  return value;
}
static int decimal_multiply(decimal_integer *number, uint32_t factor, uint32_t add) {
  uint64_t carry = add;
  for (unsigned at = 0; at < number->used; at++) {
    uint64_t next = (uint64_t)number->word[at] * factor + carry;
    number->word[at] = (uint32_t)(next % 1000000000);
    carry = next / 1000000000;
  }
  while (carry) {
    if (number->used == DECIMAL_WORDS) return 0;
    number->word[number->used++] = (uint32_t)(carry % 1000000000);
    carry /= 1000000000;
  }
  return 1;
}
static unsigned decimal_expansion(binary_value value, char *digits, int *scale) {
  decimal_integer number = {{0}, 1};
  for (unsigned at = 4; at--;) {
    if (!decimal_multiply(&number, 65536, 0) ||
        !decimal_multiply(&number, 65536, 0) ||
        !decimal_multiply(&number, 1, value.word[at])) return 0;
  }
  *scale = value.exponent < 0 ? -value.exponent : 0;
  if (!(value.word[0] | value.word[1] | value.word[2] | value.word[3])) {
    digits[0] = '0'; *scale = 0; return 1;
  }
  int remaining = value.exponent < 0 ? -value.exponent : value.exponent;
  while (remaining) {
    int step = value.exponent < 0 ? (remaining > 12 ? 12 : remaining) : (remaining > 29 ? 29 : remaining);
    uint32_t factor = 1;
    for (int at = 0; at < step; at++) factor *= value.exponent < 0 ? 5 : 2;
    if (!decimal_multiply(&number, factor, 0)) return 0;
    remaining -= step;
  }
  unsigned count = 0;
  for (unsigned at = number.used; at--;) {
    uint32_t word = number.word[at], divisor = 100000000;
    if (at == number.used - 1) while (divisor > 1 && word < divisor) divisor /= 10;
    do {
      if (count == DECIMAL_DIGITS) return 0;
      digits[count++] = (char)('0' + word / divisor);
      word %= divisor; divisor /= 10;
    } while (divisor);
  }
  return count;
}
/* Round at a decimal index, keeping the original scientific exponent unless
 * a carry introduces a leading digit. A negative index always rounds to zero.
 */
static void decimal_round(char *digits, unsigned *count, int64_t keep, int *exponent) {
  if (keep >= (int64_t)*count) return;
  int up = 0;
  if (keep >= 0) {
    unsigned at = (unsigned)keep;
    up = digits[at] > '5';
    if (digits[at] == '5') {
      int sticky = 0;
      for (unsigned tail = at + 1; tail < *count; tail++) sticky |= digits[tail] != '0';
      up = sticky || (at && ((digits[at - 1] - '0') & 1));
    }
  }
  if (keep <= 0) {
    digits[0] = up ? '1' : '0'; *count = 1;
    if (up) *exponent += 1 - (int)keep;
    else *exponent = 0;
    return;
  }
  *count = (unsigned)keep;
  if (up) {
    unsigned at = *count;
    while (at && digits[at - 1] == '9') digits[--at] = '0';
    if (at) digits[at - 1]++;
    else { digits[0] = '1'; (*exponent)++; }
  }
}
static char decimal_digit(const char *digits, unsigned count, int64_t index) {
  return index < 0 || index >= count ? '0' : digits[index];
}
static void decimal_range(wide_output *out, const char *digits, unsigned count,
                          int64_t start, uint64_t length) {
  if (start < 0) {
    uint64_t zeros = length < (uint64_t)-start ? length : (uint64_t)-start;
    repeat(out, '0', zeros);
    length -= zeros;
    start += (int64_t)zeros;
  }
  uint64_t available = (uint64_t)start < count ? count - (uint64_t)start : 0;
  uint64_t actual = length < available ? length : available;
  for (uint64_t at = 0; at < actual && !out->error; at++)
    emit(out, decimal_digit(digits, count, start + (int64_t)at));
  repeat(out, '0', length - actual);
}
static unsigned exponent_text(char *text, int exponent, char marker, unsigned minimum) {
  char reversed[12]; unsigned count = 0, result = 0;
  text[result++] = marker; text[result++] = exponent < 0 ? '-' : '+';
  unsigned magnitude = exponent < 0 ? (unsigned)-exponent : (unsigned)exponent;
  do { reversed[count++] = (char)('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
  while (count < minimum) reversed[count++] = '0';
  while (count) text[result++] = reversed[--count];
  return result;
}
static void floating_decimal(wide_output *out, const format_spec *spec, binary_value value, wchar_t conversion) {
  char digits[DECIMAL_DIGITS], suffix[16]; int scale;
  unsigned count = decimal_expansion(value, digits, &scale);
  if (!count) { fail(out, ENOMEM); return; }
  int exponent = (int)count - scale - 1;
  int64_t precision = spec->precision < 0 ? 6 : spec->precision;
  wchar_t lower = conversion >= 'A' && conversion <= 'Z' ? conversion + 32 : conversion;
  int scientific = lower == 'e';
  if (lower == 'g' && precision == 0) precision = 1;
  int64_t keep = lower == 'f' ? (int64_t)exponent + 1 + precision :
                 lower == 'e' ? (int64_t)precision + 1 : precision;
  decimal_round(digits, &count, keep, &exponent);
  if (lower == 'g') {
    scientific = exponent < -4 || exponent >= precision;
    precision = scientific ? precision - 1 : precision - (exponent + 1);
    if (!spec->alternate) {
      unsigned significant = count;
      while (significant > 1 && digits[significant - 1] == '0') significant--;
      int needed = scientific ? (int)significant - 1 : (int)significant - (exponent + 1);
      if (needed < 0) needed = 0;
      if (precision > needed) precision = needed;
    }
  }
  unsigned suffix_count = scientific ? exponent_text(suffix, exponent, conversion < 'a' ? 'E' : 'e', 2) : 0;
  uint64_t integral = scientific || exponent < 0 ? 1 : (uint64_t)exponent + 1;
  wchar_t sign = sign_of(spec, value.negative);
  uint64_t length = integral + (uint64_t)precision + (precision || spec->alternate) + suffix_count + !!sign;
  uint64_t pad = padding(spec, length);
  if (out->narrow && length + pad > INT_MAX) { fail(out, EOVERFLOW); return; }
  if (!spec->left && !spec->zero) repeat(out, ' ', pad);
  if (sign) emit(out, sign);
  if (!spec->left && spec->zero) repeat(out, '0', pad);
  decimal_range(out, digits, count, exponent < 0 && !scientific ? -1 : 0, integral);
  if (precision || spec->alternate) emit(out, '.');
  decimal_range(out, digits, count, scientific ? 1 : (int64_t)exponent + 1,
                (uint64_t)precision);
  ascii(out, suffix, suffix_count);
  if (spec->left) repeat(out, ' ', pad);
}

static unsigned binary_bit(binary_value value, int bit) {
  return bit < 0 ? 0 : (value.word[(unsigned)bit / 32] >> (bit % 32)) & 1;
}
static void floating_hex(wide_output *out, const format_spec *spec, binary_value value, int upper) {
  unsigned char digits[30] = {0}; char suffix[16];
  int top = 127;
  while (top >= 0 && !binary_bit(value, top)) top--;
  int exponent = top < 0 ? 0 : value.exponent + top;
  unsigned exact = top < 0 ? 0 : ((unsigned)top + 3) / 4;
  digits[0] = top < 0 ? 0 : 1;
  for (unsigned at = 0; at < exact; at++)
    for (int bit = 0; bit < 4; bit++)
      digits[at + 1] = (unsigned char)((digits[at + 1] << 1) | binary_bit(value, top - 1 - (int)(at * 4) - bit));
  int precision = spec->precision;
  if (precision < 0) {
    while (exact && !digits[exact]) exact--;
    precision = (int)exact;
  } else if ((unsigned)precision < exact) {
    unsigned next = digits[precision + 1]; int sticky = 0;
    for (unsigned at = (unsigned)precision + 2; at <= exact; at++) sticky |= digits[at];
    int up = next > 8 || (next == 8 && (sticky || (digits[precision] & 1)));
    if (up) {
      int at = precision;
      while (at && digits[at] == 15) digits[at--] = 0;
      digits[at]++;
    }
    exact = (unsigned)precision;
  }
  wchar_t sign = sign_of(spec, value.negative);
  unsigned suffix_count = exponent_text(suffix, exponent, upper ? 'P' : 'p', 1);
  uint64_t length = 3 + (uint64_t)precision + (precision || spec->alternate) + suffix_count + !!sign;
  uint64_t pad = padding(spec, length);
  if (out->narrow && length + pad > INT_MAX) { fail(out, EOVERFLOW); return; }
  if (!spec->left && !spec->zero) repeat(out, ' ', pad);
  if (sign) emit(out, sign);
  emit(out, '0'); emit(out, upper ? 'X' : 'x');
  if (!spec->left && spec->zero) repeat(out, '0', pad);
  emit(out, (wchar_t)('0' + digits[0]));
  if (precision || spec->alternate) emit(out, '.');
  unsigned actual = (unsigned)precision < exact ? (unsigned)precision : exact;
  for (unsigned at = 0; at < actual && !out->error; at++) {
    unsigned digit = digits[at + 1];
    emit(out, digit < 10 ? (wchar_t)('0' + digit) : (wchar_t)((upper ? 'A' : 'a') + digit - 10));
  }
  repeat(out, '0', (uint64_t)precision - actual);
  ascii(out, suffix, suffix_count);
  if (spec->left) repeat(out, ' ', pad);
}
static void floating(wide_output *out, const format_spec *spec, binary_value value, wchar_t conversion) {
  if (value.special) {
    int upper = conversion < 'a';
    wchar_t sign = sign_of(spec, value.negative);
    uint64_t pad = padding(spec, 3 + !!sign);
    if (!spec->left) repeat(out, ' ', pad);
    if (sign) emit(out, sign);
    ascii(out, value.special == 1 ? (upper ? "INF" : "inf") : (upper ? "NAN" : "nan"), 3);
    if (spec->left) repeat(out, ' ', pad);
  } else if (conversion == 'a' || conversion == 'A') floating_hex(out, spec, value, conversion == 'A');
  else floating_decimal(out, spec, value, conversion);
}

int __waste_format_float(char *destination, size_t capacity,
                         const format_spec *spec, int conversion, va_list *args) {
  wide_output out = {0, capacity, 0, 0, destination, 1};
  if (!destination && capacity) { errno = EINVAL; return -1; }
  binary_value value = floating_argument(args, spec->length == LEN_BIG_L);
  floating(&out, spec, value, (wchar_t)conversion);
  if (capacity) destination[out.used < capacity ? out.used : capacity - 1] = 0;
  if (out.error) { errno = out.error; return -1; }
  return (int)out.used;
}

static void integer(wide_output *out, const format_spec *spec, uint64_t value, int negative, wchar_t conversion) {
  char digits[64]; unsigned count = 0;
  unsigned radix = conversion == 'o' ? 8 : conversion == 'x' || conversion == 'X' || conversion == 'p' ? 16 : 10;
  uint64_t original = value;
  if (value || spec->precision != 0) do {
    unsigned digit = (unsigned)(value % radix);
    digits[count++] = digit < 10 ? (char)('0' + digit) : (char)((conversion == 'X' ? 'A' : 'a') + digit - 10);
    value /= radix;
  } while (value);
  uint64_t zeros = spec->precision > (int)count ? (uint64_t)spec->precision - count : 0;
  if (conversion == 'o' && spec->alternate && !zeros && (!count || digits[count - 1] != '0')) zeros = 1;
  int prefix = conversion == 'p' || (radix == 16 && spec->alternate && original);
  wchar_t sign = conversion == 'd' || conversion == 'i' ? sign_of(spec, negative) : 0;
  uint64_t pad = padding(spec, count + zeros + 2 * prefix + !!sign);
  int zero_pad = spec->zero && spec->precision < 0;
  if (!spec->left && !zero_pad) repeat(out, ' ', pad);
  if (sign) emit(out, sign);
  if (prefix) { emit(out, '0'); emit(out, conversion == 'X' ? 'X' : 'x'); }
  if (!spec->left && zero_pad) repeat(out, '0', pad);
  repeat(out, '0', zeros);
  while (count && !out->error) emit(out, (unsigned char)digits[--count]);
  if (spec->left) repeat(out, ' ', pad);
}
static uint64_t unsigned_argument(va_list *args, int length) {
  switch (length) {
    case LEN_HH: return (unsigned char)va_arg(*args, int);
    case LEN_H: return (unsigned short)va_arg(*args, int);
    case LEN_L: return va_arg(*args, unsigned long);
    case LEN_LL: return va_arg(*args, unsigned long long);
    case LEN_J: return va_arg(*args, uintmax_t);
    case LEN_Z: return va_arg(*args, size_t);
    case LEN_T: return va_arg(*args, unsigned int); /* wasm32 unsigned ptrdiff_t */
    default: return va_arg(*args, unsigned int);
  }
}
static int64_t signed_argument(va_list *args, int length) {
  switch (length) {
    case LEN_HH: return (signed char)va_arg(*args, int);
    case LEN_H: return (short)va_arg(*args, int);
    case LEN_L: return va_arg(*args, long);
    case LEN_LL: return va_arg(*args, long long);
    case LEN_J: return va_arg(*args, intmax_t);
    case LEN_Z: return va_arg(*args, int); /* wasm32 signed size_t */
    case LEN_T: return va_arg(*args, ptrdiff_t);
    default: return va_arg(*args, int);
  }
}
/* Decode only complete UTF-8 characters, without reading beyond string NUL.
 * Precision and width count output wchar elements, including supplementary
 * characters. Wide %ls/%lc never narrow or transcode their input elements.
 */
static int utf8_character(const unsigned char **source, wchar_t *result) {
  const unsigned char *text = *source;
  unsigned first = *text++, value, remaining, minimum;
  if (first < 128) { *result = (wchar_t)first; *source = text; return 1; }
  if (first >= 0xc2 && first <= 0xdf) { remaining = 1; minimum = 0x80; value = first & 31; }
  else if (first >= 0xe0 && first <= 0xef) { remaining = 2; minimum = 0x800; value = first & 15; }
  else if (first >= 0xf0 && first <= 0xf4) { remaining = 3; minimum = 0x10000; value = first & 7; }
  else return 0;
  while (remaining--) {
    unsigned next = *text++;
    if ((next & 0xc0) != 0x80) return 0;
    value = (value << 6) | (next & 63);
  }
  if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return 0;
  *result = (wchar_t)value; *source = text; return 1;
}
static void string(wide_output *out, const format_spec *spec, const void *source, int wide) {
  if (!source) { fail(out, EINVAL); return; }
  size_t count = 0; const unsigned char *bytes = source;
  while (spec->precision < 0 || count < (size_t)spec->precision) {
    if (wide) { if (!((const wchar_t *)source)[count]) break; }
    else {
      wchar_t value;
      if (!*bytes) break;
      if (!utf8_character(&bytes, &value)) { fail(out, EILSEQ); return; }
    }
    if (count == INT_MAX) { fail(out, EOVERFLOW); return; }
    count++;
  }
  uint64_t pad = padding(spec, count);
  if (!spec->left) repeat(out, ' ', pad);
  bytes = source;
  for (size_t at = 0; at < count && !out->error; at++) {
    wchar_t value;
    if (wide) value = ((const wchar_t *)source)[at];
    else if (!utf8_character(&bytes, &value)) { fail(out, EILSEQ); return; }
    emit(out, value);
  }
  if (spec->left) repeat(out, ' ', pad);
}
static void store_count(va_list *args, int length, size_t count) {
  switch (length) {
    case LEN_HH: *va_arg(*args, signed char *) = (signed char)count; break;
    case LEN_H: *va_arg(*args, short *) = (short)count; break;
    case LEN_L: *va_arg(*args, long *) = (long)count; break;
    case LEN_LL: *va_arg(*args, long long *) = (long long)count; break;
    case LEN_J: *va_arg(*args, intmax_t *) = (intmax_t)count; break;
    case LEN_Z: *va_arg(*args, int *) = (int)count; break;
    case LEN_T: *va_arg(*args, ptrdiff_t *) = (ptrdiff_t)count; break;
    default: *va_arg(*args, int *) = (int)count; break;
  }
}
static int decimal_field(wide_output *out, const wchar_t **format) {
  int value = 0;
  while (**format >= '0' && **format <= '9') {
    int digit = *(*format)++ - '0';
    if (value > (INT_MAX - digit) / 10) { fail(out, EOVERFLOW); return 0; }
    value = value * 10 + digit;
  }
  return value;
}
static int format_wide(wchar_t *destination, size_t capacity, const wchar_t *format, va_list *args) {
  wide_output out = {destination, capacity, 0, 0, 0, 0};
  if (!format || (!destination && capacity)) { errno = EINVAL; return -1; }
  if (!capacity) { errno = EOVERFLOW; return -1; }
  for (; *format && !out.error; format++) {
    if (*format != '%') { emit(&out, *format); continue; }
    format++;
    if (*format == '%') { emit(&out, '%'); continue; }
    format_spec spec = {0, 0, 0, 0, 0, 0, -1, LEN_DEFAULT};
    for (;;) {
      if (*format == '-') spec.left = 1;
      else if (*format == '+') spec.plus = 1;
      else if (*format == ' ') spec.space = 1;
      else if (*format == '#') spec.alternate = 1;
      else if (*format == '0') spec.zero = 1;
      else break;
      format++;
    }
    if (*format == '*') {
      int width = va_arg(*args, int); format++;
      if (width == INT_MIN) fail(&out, EOVERFLOW);
      else { spec.width = width < 0 ? -width : width; if (width < 0) spec.left = 1; }
    } else spec.width = decimal_field(&out, &format);
    if (*format == '.') {
      format++;
      if (*format == '*') { spec.precision = va_arg(*args, int); format++; }
      else spec.precision = decimal_field(&out, &format);
    }
    if (*format == 'h') { spec.length = LEN_H; if (*++format == 'h') { spec.length = LEN_HH; format++; } }
    else if (*format == 'l') { spec.length = LEN_L; if (*++format == 'l') { spec.length = LEN_LL; format++; } }
    else if (*format == 'j') { spec.length = LEN_J; format++; }
    else if (*format == 'z') { spec.length = LEN_Z; format++; }
    else if (*format == 't') { spec.length = LEN_T; format++; }
    else if (*format == 'L') { spec.length = LEN_BIG_L; format++; }
    wchar_t conversion = *format;
    if (out.error) break;
    if (!conversion) { fail(&out, EINVAL); break; }
    if (conversion == 'd' || conversion == 'i') {
      if (spec.length == LEN_BIG_L) { fail(&out, EINVAL); break; }
      int64_t value = signed_argument(args, spec.length);
      uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
      integer(&out, &spec, magnitude, value < 0, conversion);
    } else if (conversion == 'u' || conversion == 'o' || conversion == 'x' || conversion == 'X') {
      if (spec.length == LEN_BIG_L) { fail(&out, EINVAL); break; }
      integer(&out, &spec, unsigned_argument(args, spec.length), 0, conversion);
    } else if (conversion == 'p' && spec.length == LEN_DEFAULT) {
      integer(&out, &spec, (uintptr_t)va_arg(*args, void *), 0, conversion);
    } else if ((conversion == 'c' || conversion == 's') && (spec.length == LEN_DEFAULT || spec.length == LEN_L)) {
      if (conversion == 's') {
        const void *text = spec.length == LEN_L ? (const void *)va_arg(*args, const wchar_t *) : (const void *)va_arg(*args, const char *);
        string(&out, &spec, text, spec.length == LEN_L);
      } else {
        wchar_t value = spec.length == LEN_L ? (wchar_t)va_arg(*args, wint_t) : (unsigned char)va_arg(*args, int);
        uint64_t pad = padding(&spec, 1);
        if (!spec.left) repeat(&out, ' ', pad);
        emit(&out, value);
        if (spec.left) repeat(&out, ' ', pad);
      }
    } else if (conversion == 'n' && spec.length != LEN_BIG_L && spec.precision < 0 && !spec.width &&
               !spec.left && !spec.plus && !spec.space && !spec.alternate && !spec.zero) {
      store_count(args, spec.length, out.used);
    } else if ((conversion == 'f' || conversion == 'F' || conversion == 'e' || conversion == 'E' ||
                conversion == 'g' || conversion == 'G' || conversion == 'a' || conversion == 'A') &&
               (spec.length == LEN_DEFAULT || spec.length == LEN_L || spec.length == LEN_BIG_L)) {
      binary_value value = floating_argument(args, spec.length == LEN_BIG_L);
      floating(&out, &spec, value, conversion);
    } else fail(&out, EINVAL);
  }
  destination[out.used] = 0;
  if (out.error) { errno = out.error; return -1; }
  return (int)out.used;
}

int vswprintf(wchar_t *destination, size_t capacity, const wchar_t *format, va_list arguments) {
  va_list copy; va_copy(copy, arguments);
  int result = format_wide(destination, capacity, format, &copy);
  va_end(copy); return result;
}
int swprintf(wchar_t *destination, size_t capacity, const wchar_t *format, ...) {
  va_list arguments; va_start(arguments, format);
  int result = format_wide(destination, capacity, format, &arguments);
  va_end(arguments); return result;
}

