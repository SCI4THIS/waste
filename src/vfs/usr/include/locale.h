#ifndef WASTE_LOCALE_H
#define WASTE_LOCALE_H

#include <waste/abi/availability.h>

#include <stddef.h>

#define LC_CTYPE 0
#define LC_NUMERIC 1
#define LC_TIME 2
#define LC_COLLATE 3
#define LC_MONETARY 4
#define LC_MESSAGES 5
#define LC_ALL 6

/* Locale objects are opaque to applications. The existing uselocale
 * compatibility stub does not expose gnulib-owned record layouts. */
#ifdef WASTE_LEGACY_DECLARATIONS
#include <waste-gnulib-locale.h>
#else
typedef struct waste_locale *locale_t;
#endif
#define LC_GLOBAL_LOCALE ((locale_t)(-1))

struct lconv {
  char *decimal_point;
  char *thousands_sep;
  char *grouping;
  char *int_curr_symbol;
  char *currency_symbol;
  char *mon_decimal_point;
  char *mon_thousands_sep;
  char *mon_grouping;
  char *positive_sign;
  char *negative_sign;
  char int_frac_digits, frac_digits;
  char p_cs_precedes, p_sep_by_space, n_cs_precedes, n_sep_by_space;
  char p_sign_posn, n_sign_posn;
};

char *setlocale(int, const char *);
const struct lconv *localeconv(void);
locale_t newlocale(int, const char *, locale_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
locale_t duplocale(locale_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
void freelocale(locale_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
locale_t uselocale(locale_t);

#define int_p_cs_precedes p_cs_precedes
#define int_p_sign_posn p_sign_posn
#define int_p_sep_by_space p_sep_by_space
#define int_n_cs_precedes n_cs_precedes
#define int_n_sign_posn n_sign_posn
#define int_n_sep_by_space n_sep_by_space

#endif
