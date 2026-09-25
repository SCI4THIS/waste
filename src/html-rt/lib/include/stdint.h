#ifndef WASTE_STDINT_H
#define WASTE_STDINT_H

/* configure may define intmax_t as a fallback macro before coreutils includes
   the compiler's fixed-width types. Remove only that stale fallback. */
#ifdef intmax_t
# undef intmax_t
#endif
#include_next <stdint.h>

#ifndef SIZE_MAX
# define SIZE_MAX 0xffffffffUL
#endif
#ifndef SSIZE_MAX
# define SSIZE_MAX 0x7fffffffL
#endif

#endif
