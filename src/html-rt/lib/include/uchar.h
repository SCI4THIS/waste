#ifndef WASTE_UCHAR_H
#define WASTE_UCHAR_H

#include <stddef.h>

typedef unsigned short char16_t;
typedef unsigned int char32_t;
typedef unsigned int wint_t;
typedef int (*c32_type_test_t)(wint_t);
#ifndef mbstate_t
typedef struct { unsigned long state[4]; } mbstate_t;
#endif
int mbsinit(const mbstate_t *);

int c32isspace(char32_t);
static inline void mbszero(mbstate_t *state) {
  if (state) {
#ifdef mbstate_t
    *state = 0;
#else
    for (unsigned int i = 0; i < 4; i++) state->state[i] = 0;
#endif
  }
}
int c32isalnum(wint_t);
int c32isalpha(wint_t);
int c32isblank(wint_t);
int c32iscntrl(wint_t);
int c32isdigit(wint_t);
int c32width(char32_t);
int c32isgraph(wint_t);
int c32islower(wint_t);
int c32isprint(wint_t);
int c32ispunct(wint_t);
int c32isupper(wint_t);
int c32isxdigit(wint_t);
#ifndef IN_C32TOLOWER
unsigned int c32tolower(wint_t);
#endif
#ifndef IN_C32TOUPPER
unsigned int c32toupper(wint_t);
#endif

size_t c16rtomb(char *, char16_t, mbstate_t *);
size_t c32rtomb(char *, char32_t, mbstate_t *);
size_t mbrtoc16(char16_t *, const char *, size_t, mbstate_t *);
size_t mbrtoc32(char32_t *, const char *, size_t, mbstate_t *);
wint_t btoc32(int);
typedef int (*c32_type_test_t)(wint_t);
c32_type_test_t c32_get_type_test(const char *);
int c32_apply_type_test(wint_t, c32_type_test_t);

#endif
