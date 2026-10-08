#ifndef WASTE_GNULIB_UCHAR_H
#define WASTE_GNULIB_UCHAR_H
typedef int (*c32_type_test_t)(wint_t);
int c32isspace(char32_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
static inline void mbszero(mbstate_t *state) {
  if (state) {
#ifdef mbstate_t
    *state = 0;
#else
    for (unsigned int i = 0; i < 4; i++) state->state[i] = 0;
#endif
  }
}
int c32isalnum(wint_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int c32isalpha(wint_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int c32isblank(wint_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int c32iscntrl(wint_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int c32isdigit(wint_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int c32width(char32_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int c32isgraph(wint_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int c32islower(wint_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int c32isprint(wint_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int c32ispunct(wint_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int c32isupper(wint_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int c32isxdigit(wint_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
#ifndef IN_C32TOLOWER
unsigned int c32tolower(wint_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
#endif
#ifndef IN_C32TOUPPER
unsigned int c32toupper(wint_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
#endif

wint_t btoc32(int) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
c32_type_test_t c32_get_type_test(const char *) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int c32_apply_type_test(wint_t, c32_type_test_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");

#endif
