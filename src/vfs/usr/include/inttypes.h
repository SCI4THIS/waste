#ifndef WASTE_INTTYPES_H
#define WASTE_INTTYPES_H

#include <waste/abi/availability.h>

#include <stdint.h>

#define PRId8 "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRId64 "lld"
#define PRIi32 "i"
#define PRIu32 "u"
#define PRIu64 "llu"
#define PRIdMAX "lld"
#define PRIuMAX "llu"
#define PRIx32 "x"
#define PRIx64 "llx"
#define SCNd32 "d"
#define SCNu32 "u"

long long imaxabs(long long) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
long long strtoimax(const char *, char **, int);
unsigned long long strtoumax(const char *, char **, int);

#endif
