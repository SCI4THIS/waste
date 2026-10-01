#ifndef WASTE_CTYPE_H
#define WASTE_CTYPE_H

static inline int waste_ctype_ascii(int c) { return (unsigned char)c; }
static inline int isalnum(int c) {
  c = waste_ctype_ascii(c);
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z');
}
static inline int isalpha(int c) {
  c = waste_ctype_ascii(c);
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
static inline int isblank(int c) { return c == ' ' || c == '\t'; }
static inline int iscntrl(int c) {
  c = waste_ctype_ascii(c); return c < 0x20 || c == 0x7f;
}
static inline int isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int isgraph(int c) { return c > 0x20 && c < 0x7f; }
static inline int islower(int c) { return c >= 'a' && c <= 'z'; }
static inline int isprint(int c) { return c >= 0x20 && c < 0x7f; }
static inline int ispunct(int c) { return isgraph(c) && !isalnum(c); }
static inline int isspace(int c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
         c == '\f' || c == '\v';
}
static inline int isupper(int c) { return c >= 'A' && c <= 'Z'; }
static inline int isxdigit(int c) {
  return isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}
static inline int tolower(int c) { return isupper(c) ? c + ('a' - 'A') : c; }
static inline int toupper(int c) { return islower(c) ? c - ('a' - 'A') : c; }
int toascii(int);

#endif
