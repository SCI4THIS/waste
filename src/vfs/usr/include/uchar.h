#ifndef WASTE_UCHAR_H
#define WASTE_UCHAR_H

#include <waste/abi/availability.h>

#include <stddef.h>

typedef unsigned short char16_t;
typedef unsigned int char32_t;
typedef unsigned int wint_t;
#ifndef mbstate_t
typedef struct { unsigned long state[4]; } mbstate_t;
#endif
int mbsinit(const mbstate_t *);

size_t c16rtomb(char *, char16_t, mbstate_t *);
size_t c32rtomb(char *, char32_t, mbstate_t *);
size_t mbrtoc16(char16_t *, const char *, size_t, mbstate_t *);
size_t mbrtoc32(char32_t *, const char *, size_t, mbstate_t *);
#ifdef WASTE_LEGACY_DECLARATIONS
#include <waste-gnulib-uchar.h>
#endif

#endif
