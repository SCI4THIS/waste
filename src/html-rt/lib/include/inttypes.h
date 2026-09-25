#ifndef WASTE_INTTYPES_H
#define WASTE_INTTYPES_H

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

#ifndef uintmax_t
typedef unsigned long long uintmax_t;
#endif
#ifndef WASTE_INTMAX_T_DEFINED
# define WASTE_INTMAX_T_DEFINED 1
typedef signed long long intmax_t;
#endif

#ifndef INTMAX_MAX
# define INTMAX_MAX 9223372036854775807LL
# define INTMAX_MIN (-INTMAX_MAX - 1LL)
#endif

long long imaxabs(long long);
long long strtoimax(const char *, char **, int);
unsigned long long strtoumax(const char *, char **, int);

#endif
