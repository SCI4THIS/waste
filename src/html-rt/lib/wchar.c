/* wchar.c — UTF-8 encoding/decoding, multibyte/wide char conversion,
 * wide string operations, and character classification for the WASTE guest
 * libc. */

#include "common.h"

static i32 utf8_decode(const unsigned char *source, u32 available, u32 *codepoint) {
  if (!source) return 0;
  u32 first = source[0];
  if (first < 0x80) { *codepoint = first; return first ? 1 : 0; }
  u32 count = first < 0xe0 ? 2 : first < 0xf0 ? 3 : first < 0xf5 ? 4 : 0;
  if (!count || available < count) return -2;
  u32 value = first & (0x7fU >> count);
  for (u32 at = 1; at < count; at++) {
    if ((source[at] & 0xc0) != 0x80) return -1;
    value = (value << 6) | (source[at] & 0x3f);
  }
  if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) ||
      (count == 4 && value < 0x10000) || value > 0x10ffff ||
      (value >= 0xd800 && value <= 0xdfff)) return -1;
  *codepoint = value; return (i32)count;
}

static i32 utf8_encode(unsigned char *destination, u32 codepoint) {
  if (codepoint <= 0x7f) { destination[0] = (unsigned char)codepoint; return 1; }
  if (codepoint <= 0x7ff) {
    destination[0] = 0xc0 | (codepoint >> 6); destination[1] = 0x80 | (codepoint & 63); return 2;
  }
  if (codepoint >= 0xd800 && codepoint <= 0xdfff) return -1;
  if (codepoint <= 0xffff) {
    destination[0] = 0xe0 | (codepoint >> 12); destination[1] = 0x80 | ((codepoint >> 6) & 63);
    destination[2] = 0x80 | (codepoint & 63); return 3;
  }
  if (codepoint <= 0x10ffff) {
    destination[0] = 0xf0 | (codepoint >> 18); destination[1] = 0x80 | ((codepoint >> 12) & 63);
    destination[2] = 0x80 | ((codepoint >> 6) & 63); destination[3] = 0x80 | (codepoint & 63); return 4;
  }
  return -1;
}

i32 __ctype_get_mb_cur_max(void) { return 4; }
i32 mbrtowc(u32 *wide, const char *source, u32 available, void *state) {
  (void)state; u32 value = 0; i32 result = utf8_decode((const unsigned char *)source, available, &value);
  if (result == -1) *__errno_location() = 84; if (result >= 0 && wide) *wide = value; return result;
}
i32 mbtowc(u32 *wide, const char *source, u32 available) { return mbrtowc(wide, source, available, 0); }
i32 mbrlen(const char *source, u32 available, void *state) { return mbrtowc(0, source, available, state); }
i32 mblen(const char *source, u32 available) { return mbrtowc(0, source, available, 0); }
i32 wcrtomb(char *destination, u32 value, void *state) {
  (void)state; if (!destination) return 1; i32 result = utf8_encode((unsigned char *)destination, value);
  if (result < 0) *__errno_location() = 84; return result;
}

u32 mbstowcs(u32 *destination, const char *source, u32 capacity) {
  u32 count = 0, offset = 0;
  while (source[offset]) {
    u32 value; i32 used = utf8_decode((const unsigned char *)source + offset, 4, &value);
    if (used < 0) return 0xffffffffU;
    if (destination && count < capacity) destination[count] = value;
    count++; offset += (u32)used;
    if (destination && count == capacity) break;
  }
  if (destination && count < capacity) destination[count] = 0;
  return count;
}

u32 mbsrtowcs(u32 *destination, const char **source, u32 capacity, void *state) {
  (void)state; if (!source || !*source) return 0;
  u32 result = mbstowcs(destination, *source, capacity);
  if (result != 0xffffffffU && destination) *source = 0; return result;
}

u32 mbsnrtowcs(u32 *destination, const char **source, u32 bytes, u32 capacity, void *state) {
  (void)state; if (!source || !*source) return 0;
  u32 count = 0, offset = 0;
  while (offset < bytes && (*source)[offset] && count < capacity) {
    u32 value; i32 used = utf8_decode((const unsigned char *)*source + offset, bytes - offset, &value);
    if (used < 0) return 0xffffffffU;
    if (destination) destination[count] = value; count++; offset += (u32)used;
  }
  if (!(*source)[offset]) *source = 0; else *source += offset; return count;
}

u32 wcsrtombs(char *destination, const u32 **source, u32 capacity, void *state) {
  (void)state; if (!source || !*source) return 0; u32 written = 0;
  while (**source) {
    unsigned char encoded[4]; i32 count = utf8_encode(encoded, **source);
    if (count < 0) return 0xffffffffU;
    if (destination && written + (u32)count > capacity) break;
    if (destination) bytes_copy(destination + written, encoded, (u32)count);
    written += (u32)count; (*source)++;
  }
  if (**source == 0) { if (destination && written < capacity) destination[written] = 0; *source = 0; }
  return written;
}

u32 wcslen(const u32 *text) { u32 n = 0; while (text[n]) n++; return n; }
i32 wcscmp(const u32 *a, const u32 *b) { while (*a && *a == *b) { a++; b++; } return *a < *b ? -1 : *a > *b; }
i32 wcsncmp(const u32 *a, const u32 *b, u32 n) { while (n && *a && *a == *b) { a++; b++; n--; } return !n ? 0 : *a < *b ? -1 : *a > *b; }
i32 wcscoll(const u32 *a, const u32 *b) { return wcscmp(a, b); }
i32 strcoll(const char *a, const char *b) { return c_compare(a, b); }
u32 *wcschr(const u32 *text, u32 value) { while (*text && *text != value) text++; return *text == value ? (u32 *)text : 0; }
u32 *wmemchr(const u32 *text, u32 value, u32 n) { while (n--) { if (*text == value) return (u32 *)text; text++; } return 0; }
u32 *wcsdup(const u32 *text) { u32 n = wcslen(text) + 1, *copy = malloc(n * 4); if (copy) bytes_copy(copy, text, n * 4); return copy; }

i32 iscntrl(i32 c) { return (c >= 0 && c < 32) || c == 127; }
i32 isblank(i32 c) { return c == ' ' || c == '\t'; }
i32 isalnum(i32 c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
i32 ispunct(i32 c) { return c >= 33 && c <= 126 && !isalnum(c); }
i32 isxdigit(i32 c) { return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'f'); }
i32 toupper(i32 c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
i32 tolower(i32 c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
i32 iswalnum(u32 c) { return c < 128 ? isalnum((i32)c) : c <= 0x10ffff; }
i32 iswupper(u32 c) { return c >= 'A' && c <= 'Z'; }
i32 iswlower(u32 c) { return c >= 'a' && c <= 'z'; }
i32 iswprint(u32 c) { return c >= 32 && c != 127 && c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff); }
u32 towupper(u32 c) { return (u32)toupper((i32)c); }
u32 towlower(u32 c) { return (u32)tolower((i32)c); }
i32 wctob(u32 c) { return c <= 0x7f ? (i32)c : -1; }
i32 wcwidth(u32 c) {
  if (!c) return 0; if (!iswprint(c)) return -1;
  if ((c >= 0x300 && c <= 0x36f) || (c >= 0x1ab0 && c <= 0x1aff)) return 0;
  if ((c >= 0x1100 && c <= 0x115f) || (c >= 0x2e80 && c <= 0xa4cf) ||
      (c >= 0xac00 && c <= 0xd7a3) || (c >= 0xf900 && c <= 0xfaff) || c >= 0x1f300) return 2;
  return 1;
}
i32 wcswidth(const u32 *text, u32 count) { i32 width = 0; while (count-- && *text) { i32 n = wcwidth(*text++); if (n < 0) return -1; width += n; } return width; }
