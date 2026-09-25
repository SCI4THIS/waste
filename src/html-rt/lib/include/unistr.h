#ifndef WASTE_UNISTR_H
#define WASTE_UNISTR_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t ucs4_t;
int u8_uctomb(unsigned char *, uint32_t, size_t);

/* The coreutils build does not enable the optional libunistring module for
 * the initial utility wave. Keep the public include available without
 * importing a host Unicode library. */

#endif
