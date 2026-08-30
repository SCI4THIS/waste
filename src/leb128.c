#include "leb128.h"
#include <stdio.h>

bool leb_(size_t N, const uint8_t **buf, size_t len, uint8_t *val, size_t val_size, bool is_signed)
{
  size_t i     = 0;
  size_t j     = 0;
  size_t k     = 0;
  size_t max_n = ((val_size * 8) + 7) / 7;
  size_t count = 0;

  if (len < max_n) {
    max_n = len;
  }

  for (i=0; i<val_size; i++) {
    val[i] = 0;
  }

  i = 0;
  while (((*buf)[i] & 0x80) && i < max_n ) { i++; }

  if (i == max_n) {
    printf("!!!LEB128 %d == %d\n", i, max_n);
    return false;
  }

  count = i + 1;

  k = 0;
  for (i=0; i<count; i++) {
    for (j=0; j<7; j++, k++) {
      //size_t B = val_size - 1 - k / 8;
      size_t B = k / 8;
      size_t b = k % 8;
      if ((*buf)[i] & (1 << j)) {
        if (B >= val_size && (!is_signed || (is_signed && ((*buf)[i] & 0x40) == 0))) {
          printf("!!!LEB128 %d >= %d .. i = %d, j = %d, k = %d\n", B, val_size, i, j, k);
          return false;
	}
        val[B] |= (1 << b);
      } else {
        if (B >= val_size && is_signed && ((*buf)[i] & 0x40) != 0) {
          printf("!!!LEB128 %d >= %d .. i = %d, j = %d, k = %d\n", B, val_size, i, j, k);
          return false;
	}
      }
    }
  }
  if (is_signed) {
    if ((*buf)[count - 1] & 0x40) {
      size_t num_bits = 8 * val_size;
      for (; k < num_bits; k++) {
        size_t B = k / 8;
        size_t b = k % 8;
        val[B] |= (1 << b);
      }
    }
  }
  *buf += count;
  return true;
}


bool leb_s(size_t N, const uint8_t **buf, size_t len, uint8_t *val, size_t val_size)
{
  return leb_(N, buf, len, val, val_size, true);
}

bool leb_u(size_t N, const uint8_t **buf, size_t len, uint8_t *val, size_t val_size)
{
  return leb_(N, buf, len, val, val_size, false);
}

bool leb_u8(const uint8_t **buf, size_t len, uint8_t *val)
{
  return leb_u(8, buf, len, val, sizeof(*val));
}

bool leb_u32(const uint8_t **buf, size_t len, uint32_t *val)
{
  return leb_u(32, buf, len, (uint8_t*)val, sizeof(*val));
}

bool leb_i32(const uint8_t **buf, size_t len, int32_t *val)
{
  return leb_s(32, buf, len, (uint8_t*)val, sizeof(*val));
}

bool leb_u64(const uint8_t **buf, size_t len, uint64_t *val)
{
  return leb_u(64, buf, len, (uint8_t*)val, sizeof(*val));
}

bool leb_i64(const uint8_t **buf, size_t len, int64_t *val)
{
  return leb_s(64, buf, len, (uint8_t*)val, sizeof(*val));
}

#ifdef TEST
bool test_1()
{
  /* expected value was calculated at https://ifcoltransg.github.io/lebanon-leb128-converter
   * on 2024-12-07 */
  /* int64_t         expected =  -9223372036854775808LL; */
  /* need to do the following, see:
   * https://stackoverflow.com/questions/65007935/integer-constant-is-so-large-that-it-is-unsigned-compiler-warning-rationale */
  int64_t         expected =    (-9223372036854775807LL - 1);
  int64_t         val      = 0;
  const uint8_t   hexbuf[] = { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7F };
  const uint8_t  *bufptr   = hexbuf;
  const uint8_t **buf      = &bufptr;

  if (!leb_i64(buf, sizeof(hexbuf), &val)) {
    printf("test_1: error parsing\n");
    return false;
  }

  if (val != expected) {
    printf("test_1: val mismatch: %lld != %lld\n", val, expected);
    return false;
  }

  return true;
}

bool test_2()
{
  const uint8_t   hexbuf1[] = { 0x83, 0x10 };
  const uint8_t   hexbuf2[] = { 0x83, 0x01 };
  const uint8_t  *bufptr   = hexbuf1;
  const uint8_t **buf      = &bufptr;
  uint8_t         val;
  /* 0x83 0x10 is malformed as a u8 encoding because it would be
   * 0b10000011 0b00010000
   *    0000011    0010000
   * 0b00001000 0b00000011
   * 0x08 0x03, which does not fit in 8 bits.
   * 0x83 0x01, which decodes as 0x83, does.
   */
  if (leb_u8(buf, sizeof(hexbuf1), &val)) {
    return false;
  }
  bufptr = hexbuf2;
  if (!leb_u8(buf, sizeof(hexbuf2), &val)) {
    return false;
  }
  return true;
}

typedef bool (*test_t)(void);

int main (int argc, char ** argv)
{
  int ntests   = 0;
  int nsuccess = 0;
  int i        = 0;
  test_t tests[] = { test_1, test_2 };

  for (i=0; i<(sizeof(tests)/sizeof(tests[0])); i++) {
    ntests++;
    if (tests[i]()) {
      nsuccess++;
    }
  }

  printf("Passed %d / %d\n", nsuccess, ntests);
}
#endif
