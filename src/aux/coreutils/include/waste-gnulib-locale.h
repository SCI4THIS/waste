#ifndef WASTE_GNULIB_LOCALE_H
#define WASTE_GNULIB_LOCALE_H
#include <stddef.h>
#include <stdbool.h>
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
#define GNULIB_defined_locale_t 1

/* gnulib uses these helpers to map locale categories into its six-entry
   category array.  WASTE's category constants are already zero-based. */
#define _gl_log2_lc_mask(category) (category)
#define _gl_log2_lcmask_to_index(category) (category)
#define _gl_index_to_log2_lcmask(index) (index)

#define SETLOCALE_NULL_MAX (256 + 1)
int setlocale_null_r(int, char *, size_t) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");


#endif
