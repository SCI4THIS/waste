/* Integer conversions for the guest C ABI, independent of host locale/state. */
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct {
  uintmax_t magnitude;
  size_t consumed;
  int negative;
  int overflow;
} integer_value;

static int digit_value(unsigned int byte) {
  if (byte >= '0' && byte <= '9') return byte - '0';
  if (byte >= 'a' && byte <= 'z') return byte - 'a' + 10;
  if (byte >= 'A' && byte <= 'Z') return byte - 'A' + 10;
  return -1;
}

static int space_character(unsigned int byte) {
  return byte == ' ' || (byte >= '\t' && byte <= '\r');
}

static unsigned int input_character(const void *text, size_t at, int wide) {
  /* Do not narrow wchar_t to a byte: a non-ASCII character whose low byte is
     a digit, sign, space or NUL must not acquire that ASCII meaning. */
  if (wide) return (unsigned int)((const wchar_t *)text)[at];
  return ((const unsigned char *)text)[at];
}

/* Check before multiplying, latch overflow, and still consume every digit.
   The signed negative range permits one more magnitude than the positive range. */
static integer_value parse_integer(const void *text, int wide, int base,
                                   uintmax_t positive_limit, int is_signed) {
  integer_value result = {0, 0, 0, 0};
  size_t cursor = 0;
  if (base != 0 && (base < 2 || base > 36)) {
    errno = EINVAL;
    return result;
  }
  while (space_character(input_character(text, cursor, wide))) cursor++;
  unsigned int character = input_character(text, cursor, wide);
  if (character == '+' || character == '-') {
    result.negative = character == '-';
    cursor++;
  }
  if ((base == 0 || base == 16) && input_character(text, cursor, wide) == '0' &&
      (input_character(text, cursor + 1, wide) == 'x' ||
       input_character(text, cursor + 1, wide) == 'X') &&
      digit_value(input_character(text, cursor + 2, wide)) >= 0 &&
      digit_value(input_character(text, cursor + 2, wide)) < 16) {
    base = 16;
    cursor += 2;
  } else if (base == 0) {
    base = input_character(text, cursor, wide) == '0' ? 8 : 10;
  }
  size_t digits = cursor;
  uintmax_t limit = positive_limit + (is_signed && result.negative);
  uintmax_t cutoff = limit / (unsigned)base;
  unsigned remainder = (unsigned)(limit % (unsigned)base);
  int digit;
  while ((digit = digit_value(input_character(text, cursor, wide))) >= 0 && digit < base) {
    if (!result.overflow) {
      if (result.magnitude > cutoff ||
          (result.magnitude == cutoff && (unsigned)digit > remainder)) {
        result.overflow = 1;
        result.magnitude = limit;
        errno = ERANGE;
      } else {
        result.magnitude = result.magnitude * (unsigned)base + (unsigned)digit;
      }
    }
    cursor++;
  }
  if (cursor != digits) result.consumed = cursor;
  return result;
}

static integer_value parse_bytes(const char *text, char **end, int base,
                                 uintmax_t positive_limit, int is_signed) {
  integer_value result = parse_integer(text, 0, base, positive_limit, is_signed);
  if (end) *end = (char *)text + result.consumed;
  return result;
}

static integer_value parse_wide(const wchar_t *text, wchar_t **end, int base,
                                uintmax_t positive_limit, int is_signed) {
  integer_value result = parse_integer(text, 1, base, positive_limit, is_signed);
  if (end) *end = (wchar_t *)text + result.consumed;
  return result;
}

static intmax_t signed_value(integer_value value, intmax_t maximum) {
  if (!value.negative) return (intmax_t)value.magnitude;
  if (value.magnitude == (uintmax_t)maximum + 1) return -maximum - 1;
  return -(intmax_t)value.magnitude;
}

static uintmax_t unsigned_value(integer_value value) {
  /* Overflow saturates even when the input has a minus sign. */
  return value.negative && !value.overflow ? 0 - value.magnitude : value.magnitude;
}

long strtol(const char *text, char **end, int base) {
  return (long)signed_value(parse_bytes(text, end, base, LONG_MAX, 1), LONG_MAX);
}
unsigned long strtoul(const char *text, char **end, int base) {
  return (unsigned long)unsigned_value(parse_bytes(text, end, base, ULONG_MAX, 0));
}
long long strtoll(const char *text, char **end, int base) {
  return (long long)signed_value(parse_bytes(text, end, base, LLONG_MAX, 1), LLONG_MAX);
}
unsigned long long strtoull(const char *text, char **end, int base) {
  return (unsigned long long)unsigned_value(parse_bytes(text, end, base, ULLONG_MAX, 0));
}
intmax_t strtoimax(const char *text, char **end, int base) {
  return signed_value(parse_bytes(text, end, base, INTMAX_MAX, 1), INTMAX_MAX);
}
uintmax_t strtoumax(const char *text, char **end, int base) {
  return unsigned_value(parse_bytes(text, end, base, UINTMAX_MAX, 0));
}

long wcstol(const wchar_t *text, wchar_t **end, int base) {
  return (long)signed_value(parse_wide(text, end, base, LONG_MAX, 1), LONG_MAX);
}
unsigned long wcstoul(const wchar_t *text, wchar_t **end, int base) {
  return (unsigned long)unsigned_value(parse_wide(text, end, base, ULONG_MAX, 0));
}
long long wcstoll(const wchar_t *text, wchar_t **end, int base) {
  return (long long)signed_value(parse_wide(text, end, base, LLONG_MAX, 1), LLONG_MAX);
}
unsigned long long wcstoull(const wchar_t *text, wchar_t **end, int base) {
  return (unsigned long long)unsigned_value(parse_wide(text, end, base, ULLONG_MAX, 0));
}
intmax_t wcstoimax(const wchar_t *text, wchar_t **end, int base) {
  return signed_value(parse_wide(text, end, base, INTMAX_MAX, 1), INTMAX_MAX);
}
uintmax_t wcstoumax(const wchar_t *text, wchar_t **end, int base) {
  return unsigned_value(parse_wide(text, end, base, UINTMAX_MAX, 0));
}
