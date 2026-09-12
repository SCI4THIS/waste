#ifndef FREESTANDING_MATH_H
#define FREESTANDING_MATH_H

#define isnan(x)    __builtin_isnan(x)
#define isinf(x)    __builtin_isinf(x)
#define signbit(x)  __builtin_signbit(x)
#define INFINITY    __builtin_inf()
#define NAN         __builtin_nan("")
#define HUGE_VAL    __builtin_huge_val()

float  fabsf(float x);
float  ceilf(float x);
float  floorf(float x);
float  truncf(float x);
float  nearbyintf(float x);
float  sqrtf(float x);
float  fmaf(float a, float b, float c);
float  ldexpf(float x, int exp);
float  frexpf(float x, int *exp);

double fabs(double x);
double ceil(double x);
double floor(double x);
double trunc(double x);
double nearbyint(double x);
double sqrt(double x);
double fma(double a, double b, double c);
double ldexp(double x, int exp);
double frexp(double x, int *exp);
double copysign(double x, double y);
double fmin(double x, double y);
double fmax(double x, double y);

#endif
