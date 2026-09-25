#ifndef WASTE_ASSERT_H
#define WASTE_ASSERT_H

#include <stdlib.h>

#ifndef NDEBUG
# define assert(expression) ((expression) ? (void)0 : abort())
#else
# define assert(expression) ((void)0)
#endif

#endif
