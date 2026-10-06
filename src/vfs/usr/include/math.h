#ifndef WASTE_MATH_H
#define WASTE_MATH_H

#include <waste/abi/availability.h>

#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define signbit(x) __builtin_signbit(x)
#define INFINITY __builtin_inf()
#define NAN __builtin_nan("")
#define HUGE_VAL __builtin_huge_val()

float fabsf(float); double fabs(double);
float ceilf(float); double ceil(double);
float floorf(float); double floor(double);
float truncf(float); double trunc(double);
float nearbyintf(float); double nearbyint(double);
float sqrtf(float); double sqrt(double);
float fmaf(float, float, float); double fma(double, double, double);
float ldexpf(float, int); double ldexp(double, int);
float frexpf(float, int *); double frexp(double, int *);
long double frexpl(long double, int *);
double copysign(double, double);
double fmin(double, double); double fmax(double, double);
double pow(double, double) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability"); double log(double) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability"); double exp(double) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");

#endif
