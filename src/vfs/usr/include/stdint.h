#ifndef WASTE_STDINT_H
#define WASTE_STDINT_H
/* wasm32 C11 fixed-width interface. Compiler predefined integer types are
 * part of the documented target profile; no host or include_next fallback. */
#ifdef intmax_t
# undef intmax_t
#endif
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef int8_t int_least8_t; typedef uint8_t uint_least8_t;
typedef int16_t int_least16_t; typedef uint16_t uint_least16_t;
typedef int32_t int_least32_t; typedef uint32_t uint_least32_t;
typedef int64_t int_least64_t; typedef uint64_t uint_least64_t;
typedef __INT_FAST8_TYPE__ int_fast8_t; typedef __UINT_FAST8_TYPE__ uint_fast8_t;
typedef __INT_FAST16_TYPE__ int_fast16_t; typedef __UINT_FAST16_TYPE__ uint_fast16_t;
typedef __INT_FAST32_TYPE__ int_fast32_t; typedef __UINT_FAST32_TYPE__ uint_fast32_t;
typedef __INT_FAST64_TYPE__ int_fast64_t; typedef __UINT_FAST64_TYPE__ uint_fast64_t;
typedef __INTPTR_TYPE__ intptr_t; typedef __UINTPTR_TYPE__ uintptr_t;
typedef long long intmax_t; typedef unsigned long long uintmax_t;
#define INT8_MIN (-127-1)
#define INT8_MAX 127
#define UINT8_MAX 255
#define INT16_MIN (-32767-1)
#define INT16_MAX 32767
#define UINT16_MAX 65535
#define INT32_MIN (-2147483647-1)
#define INT32_MAX 2147483647
#define UINT32_MAX 4294967295U
#define INT64_MIN (-9223372036854775807LL-1)
#define INT64_MAX 9223372036854775807LL
#define UINT64_MAX 18446744073709551615ULL
#define INT_LEAST8_MIN INT8_MIN
#define INT_LEAST8_MAX INT8_MAX
#define UINT_LEAST8_MAX UINT8_MAX
#define INT_LEAST16_MIN INT16_MIN
#define INT_LEAST16_MAX INT16_MAX
#define UINT_LEAST16_MAX UINT16_MAX
#define INT_LEAST32_MIN INT32_MIN
#define INT_LEAST32_MAX INT32_MAX
#define UINT_LEAST32_MAX UINT32_MAX
#define INT_LEAST64_MIN INT64_MIN
#define INT_LEAST64_MAX INT64_MAX
#define UINT_LEAST64_MAX UINT64_MAX
#define INT_FAST8_MIN (-__INT_FAST8_MAX__-1)
#define INT_FAST8_MAX __INT_FAST8_MAX__
#define UINT_FAST8_MAX __UINT_FAST8_MAX__
#define INT_FAST16_MIN (-__INT_FAST16_MAX__-1)
#define INT_FAST16_MAX __INT_FAST16_MAX__
#define UINT_FAST16_MAX __UINT_FAST16_MAX__
#define INT_FAST32_MIN (-__INT_FAST32_MAX__-1)
#define INT_FAST32_MAX __INT_FAST32_MAX__
#define UINT_FAST32_MAX __UINT_FAST32_MAX__
#define INT_FAST64_MIN (-__INT_FAST64_MAX__-1)
#define INT_FAST64_MAX __INT_FAST64_MAX__
#define UINT_FAST64_MAX __UINT_FAST64_MAX__
#define INTPTR_MIN (-__INTPTR_MAX__-1)
#define INTPTR_MAX __INTPTR_MAX__
#define UINTPTR_MAX __UINTPTR_MAX__
#define INTMAX_MIN INT64_MIN
#define INTMAX_MAX INT64_MAX
#define UINTMAX_MAX UINT64_MAX
#define PTRDIFF_MIN (-__PTRDIFF_MAX__-1)
#define PTRDIFF_MAX __PTRDIFF_MAX__
#define SIZE_MAX __SIZE_MAX__
#define SIG_ATOMIC_MIN (-__SIG_ATOMIC_MAX__-1)
#define SIG_ATOMIC_MAX __SIG_ATOMIC_MAX__
#define WCHAR_MIN __WCHAR_MIN__
#define WCHAR_MAX __WCHAR_MAX__
#define WINT_MIN __WINT_MIN__
#define WINT_MAX __WINT_MAX__
#define INT8_C(v) v
#define UINT8_C(v) v
#define INT16_C(v) v
#define UINT16_C(v) v
#define INT32_C(v) v
#define UINT32_C(v) v##U
#define INT64_C(v) v##LL
#define UINT64_C(v) v##ULL
#define INTMAX_C(v) v##LL
#define UINTMAX_C(v) v##ULL
#endif
