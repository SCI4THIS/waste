/* locale.c — Locale support, gettext, iconv, and wctype for the WASTE guest
 * libc. */

#include "common.h"

/* Character classification declared in wchar.c */
extern i32 iswalnum(u32 c);
extern i32 iswlower(u32 c);
extern i32 iswupper(u32 c);
extern i32 iswprint(u32 c);

static char *locale_name;
static char *empty_text;
static char *decimal_point;
static u32 *locale_record;
static char *domain_name;

static void locale_initialize(void) {
  if (locale_name) return;
  locale_name = malloc(8); empty_text = malloc(1); decimal_point = malloc(2);
  locale_name[0]='C'; locale_name[1]='.'; locale_name[2]='U'; locale_name[3]='T';
  locale_name[4]='F'; locale_name[5]='-'; locale_name[6]='8'; locale_name[7]=0;
  empty_text[0]=0; decimal_point[0]='.'; decimal_point[1]=0;
  locale_record = malloc(56); bytes_zero(locale_record, 56);
  locale_record[0]=(u32)decimal_point;
  for (u32 at=1; at<10; at++) locale_record[at]=(u32)empty_text;
  unsigned char *characters=(unsigned char *)(locale_record+10);
  for (u32 at=0; at<14; at++) characters[at]=127;
  domain_name=empty_text;
}

char *setlocale(i32 category, const char *locale) { (void)category; locale_initialize(); (void)locale; return locale_name; }
u32 *localeconv(void) { locale_initialize(); return locale_record; }
char *nl_langinfo(i32 item) { (void)item; locale_initialize(); return locale_name; }
char *gettext(const char *message) { return (char *)message; }
char *dgettext(const char *domain, const char *message) { (void)domain; return (char *)message; }
char *ngettext(const char *one, const char *many, u32 count) { return (char *)(count == 1 ? one : many); }
char *bindtextdomain(const char *domain, const char *directory) { (void)domain; return (char *)directory; }
char *textdomain(const char *domain) { locale_initialize(); if (domain && *domain) domain_name=(char *)domain; return domain_name; }
i32 mbsinit(const void *state) { (void)state; return 1; }
char *locale_charset(void) { locale_initialize(); return locale_name + 2; }
u32 iconv_open(const char *to_encoding, const char *from_encoding) {
  (void)to_encoding; (void)from_encoding; locale_initialize(); return 1;
}
i32 iconv_close(u32 descriptor) { return descriptor == 1 ? 0 : -1; }
u32 iconv(u32 descriptor, char **input, u32 *input_left, char **output, u32 *output_left) {
  if (descriptor != 1) { *__errno_location() = 22; return 0xffffffffU; }
  if (!input || !*input) return 0;
  u32 copied = *input_left < *output_left ? *input_left : *output_left;
  bytes_copy(*output, *input, copied);
  *input += copied; *input_left -= copied; *output += copied; *output_left -= copied;
  if (*input_left) { *__errno_location() = 7; return 0xffffffffU; }
  return 0;
}
u32 wctype(const char *name) {
  if (!name) return 0; if (!c_compare(name,"alnum")) return 1; if (!c_compare(name,"lower")) return 2;
  if (!c_compare(name,"upper")) return 3; if (!c_compare(name,"print")) return 4; return 0;
}
i32 iswctype(u32 value, u32 descriptor) {
  return descriptor==1 ? iswalnum(value) : descriptor==2 ? iswlower(value) :
    descriptor==3 ? iswupper(value) : descriptor==4 ? iswprint(value) : 0;
}
