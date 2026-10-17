#ifndef WASTE_INTTYPES_H
#define WASTE_INTTYPES_H

#include <waste/abi/availability.h>

#include <stdint.h>
#include <stddef.h>

#define PRId8 "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRId64 "lld"
#define PRIi32 "i"
#define PRIu8 "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIu64 "llu"
#define PRIdMAX "lld"
#define PRIuMAX "llu"
/* Small fixed-width values promote to int; uint64_t is unsigned long long. */
#define PRIx8 "x"
#define PRIx16 "x"
#define PRIx32 "x"
#define PRIx64 "llx"
#define PRIX8 "X"
#define PRIX16 "X"
#define PRIX32 "X"
#define PRIX64 "llX"
/* Match the compiler pointer aliases used by the public stdint.h profile. */
#define PRIdPTR __INTPTR_FMTd__
#define PRIiPTR __INTPTR_FMTi__
#define PRIuPTR __UINTPTR_FMTu__
#define PRIoPTR __UINTPTR_FMTo__
#define PRIxPTR __UINTPTR_FMTx__
#define PRIXPTR __UINTPTR_FMTX__
#define SCNd32 "d"
#define SCNu32 "u"

#ifdef __cplusplus
extern "C" {
#endif

long long imaxabs(long long) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
intmax_t strtoimax(const char *, char **, int);
uintmax_t strtoumax(const char *, char **, int);
intmax_t wcstoimax(const wchar_t *, wchar_t **, int);
uintmax_t wcstoumax(const wchar_t *, wchar_t **, int);

#ifdef __cplusplus
}
#endif

#endif
