#ifndef WASTE_MATH_H
#define WASTE_MATH_H

#include <waste/abi/availability.h>
#include <limits.h>

/* Implementation-defined classification codes, shared by C and libc++.
 * Compiler builtins preserve the operand type and evaluate it once. */
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define fpclassify(x) \
  __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, (x))
#define isfinite(x) __builtin_isfinite(x)
#define isnormal(x) __builtin_isnormal(x)
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define signbit(x) __builtin_signbit(x)
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
#define HUGE_VAL __builtin_huge_val()
#define HUGE_VALF __builtin_huge_valf()
#define HUGE_VALL __builtin_huge_vall()

/* Integer exponent sentinels. These constants do not declare an ilogb
 * provider; LLVM's private floating-point helpers also consume them. */
#define FP_ILOGB0 INT_MIN
#define FP_ILOGBNAN INT_MAX

#ifdef __cplusplus
extern "C" {
#endif

float fabsf(float); double fabs(double);
float ceilf(float); double ceil(double);
float floorf(float); double floor(double);
float truncf(float); double trunc(double);
float nearbyintf(float); double nearbyint(double);
double round(double);
long lround(double);
float log2f(float); double log2(double);
float sqrtf(float); double sqrt(double);
float fmaf(float, float, float); double fma(double, double, double);
float ldexpf(float, int); double ldexp(double, int);
float frexpf(float, int *); double frexp(double, int *);
long double frexpl(long double, int *);
double copysign(double, double);
double modf(double, double *);
double fmin(double, double); double fmax(double, double);
double pow(double, double) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability"); double log(double) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability"); double exp(double) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");

#ifdef __cplusplus
}
#endif

#endif
