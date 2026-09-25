#ifndef WASTE_LOCALE_H
#define WASTE_LOCALE_H

#include <stddef.h>

#define LC_CTYPE 0
#define LC_NUMERIC 1
#define LC_TIME 2
#define LC_COLLATE 3
#define LC_MONETARY 4
#define LC_MESSAGES 5
#define LC_ALL 6

/* Match gnulib's portable locale object.  The guest currently uses the C
   locale, but keeping the category names here lets locale-aware sources build
   without exposing a host libc object. */
#include <stdbool.h>
struct gl_locale_category_t {
  char *name;
  bool is_c_locale;
};
struct gl_locale_t {
  struct gl_locale_category_t category[6];
};
typedef struct gl_locale_t *locale_t;
#define LC_GLOBAL_LOCALE ((locale_t)(-1))
#define GNULIB_defined_locale_t 1

/* gnulib uses these helpers to map locale categories into its six-entry
   category array.  WASTE's category constants are already zero-based. */
#define _gl_log2_lc_mask(category) (category)
#define _gl_log2_lcmask_to_index(category) (category)
#define _gl_index_to_log2_lcmask(index) (index)

#define SETLOCALE_NULL_MAX (256 + 1)
int setlocale_null_r(int, char *, size_t);

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
locale_t newlocale(int, const char *, locale_t);
locale_t duplocale(locale_t);
void freelocale(locale_t);
locale_t uselocale(locale_t);

#define int_p_cs_precedes p_cs_precedes
#define int_p_sign_posn p_sign_posn
#define int_p_sep_by_space p_sep_by_space
#define int_n_cs_precedes n_cs_precedes
#define int_n_sign_posn n_sign_posn
#define int_n_sep_by_space n_sep_by_space

#endif
