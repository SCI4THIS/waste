#ifndef WASTE_ASSERT_H
#define WASTE_ASSERT_H

#include <stdlib.h>

#if !defined(__cplusplus) && __STDC_VERSION__ >= 201112L && __STDC_VERSION__ < 202311L
# define static_assert _Static_assert
#endif

#ifndef NDEBUG
# ifdef WASTE_LEGACY_DECLARATIONS
#  define assert(expression) ((expression) ? (void)0 : abort())
# else
/* A terminating trap, without a libc diagnostic or SIGABRT delivery. */
#  define assert(expression) ((expression) ? (void)0 : __builtin_trap())
# endif
#else
# define assert(expression) ((void)0)
#endif

#endif
