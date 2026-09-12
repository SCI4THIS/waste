/*
 * math.c — Portable math functions (IEEE-754 helpers and builtins).
 *
 * Shared by both native and Wasm builds.  Extracted from freestanding_lib.c.
 */

#include <stddef.h>
#include <stdint.h>

/* ---- IEEE-754 power-of-two construction ---- */

static double double_power_of_two(int exp) {
    union { uint64_t bits; double value; } power;
    if (exp > 1023) return __builtin_inf();
    if (exp < -1074) return 0.0;
    if (exp >= -1022)
        power.bits = (uint64_t)(exp + 1023) << 52;
    else
        power.bits = UINT64_C(1) << (exp + 1074);
    return power.value;
}

double ldexp(double x, int exp) {
    while (exp > 1023) {
        x *= double_power_of_two(1023);
        exp -= 1023;
    }
    while (exp < -1074) {
        x *= double_power_of_two(-1074);
        exp += 1074;
    }
    return x * double_power_of_two(exp);
}

static float float_power_of_two(int exp) {
    union { uint32_t bits; float value; } power;
    if (exp > 127) return __builtin_inff();
    if (exp < -149) return 0.0f;
    if (exp >= -126)
        power.bits = (uint32_t)(exp + 127) << 23;
    else
        power.bits = UINT32_C(1) << (exp + 149);
    return power.value;
}

float ldexpf(float x, int exp) {
    while (exp > 127) {
        x *= float_power_of_two(127);
        exp -= 127;
    }
    while (exp < -149) {
        x *= float_power_of_two(-149);
        exp += 149;
    }
    return x * float_power_of_two(exp);
}

double frexp(double x, int *exp) {
    if (x == 0.0) { *exp = 0; return 0.0; }
    union { double d; uint64_t u; } u;
    u.d = x;
    int e = (int)((u.u >> 52) & 0x7FF);
    if (e == 0) {
        u.d = x * 4503599627370496.0; /* 2^52 */
        e = (int)((u.u >> 52) & 0x7FF) - 52;
    }
    *exp = e - 1022;
    u.u = (u.u & 0x800FFFFFFFFFFFFFULL) | 0x3FE0000000000000ULL;
    return u.d;
}

float frexpf(float x, int *exp) {
    if (x == 0.0f) { *exp = 0; return 0.0f; }
    union { float f; uint32_t u; } u;
    u.f = x;
    int e = (int)((u.u >> 23) & 0xFF);
    if (e == 0) {
        u.f = x * 8388608.0f; /* 2^23 */
        e = (int)((u.u >> 23) & 0xFF) - 23;
    }
    *exp = e - 126;
    u.u = (u.u & 0x807FFFFFU) | 0x3F000000U;
    return u.f;
}

/* ---- copysign, fmin, fmax ---- */

#ifdef __wasm__
double copysign(double x, double y) { return __builtin_copysign(x, y); }
double fmin(double a, double b) { return __builtin_fmin(a, b); }
double fmax(double a, double b) { return __builtin_fmax(a, b); }
#else
double copysign(double x, double y) {
    union { double d; uint64_t u; } ux = {x}, uy = {y};
    ux.u = (ux.u & UINT64_C(0x7FFFFFFFFFFFFFFF)) |
           (uy.u & UINT64_C(0x8000000000000000));
    return ux.d;
}
double fmin(double a, double b) {
    if (a != a) return a;
    if (b != b) return b;
    return a < b ? a : b;
}
double fmax(double a, double b) {
    if (a != a) return a;
    if (b != b) return b;
    return a > b ? a : b;
}
#endif

/* ---- FMA (relaxed semantics) ---- */

float fmaf(float a, float b, float c) {
    return (float)((double)a * (double)b + (double)c);
}

double fma(double a, double b, double c) {
    return a * b + c;
}

/* ---- Builtins (compile to native SSE/Wasm instructions) ---- */

#ifdef __wasm__
float  fabsf(float x)      { return __builtin_fabsf(x); }
float  ceilf(float x)      { return __builtin_ceilf(x); }
float  floorf(float x)     { return __builtin_floorf(x); }
float  truncf(float x)     { return __builtin_truncf(x); }
float  nearbyintf(float x) { return __builtin_nearbyintf(x); }
float  sqrtf(float x)      { return __builtin_sqrtf(x); }
double fabs(double x)      { return __builtin_fabs(x); }
double ceil(double x)      { return __builtin_ceil(x); }
double floor(double x)     { return __builtin_floor(x); }
double trunc(double x)     { return __builtin_trunc(x); }
double nearbyint(double x) { return __builtin_nearbyint(x); }
double sqrt(double x)      { return __builtin_sqrt(x); }
#else
/* x86_64: portable C implementations for math functions */
float fabsf(float x) {
    union { float f; uint32_t u; } u = {x};
    u.u &= 0x7FFFFFFFU;
    return u.f;
}
float sqrtf(float x) {
    float r;
    __asm__ ("sqrtss %1, %0" : "=x"(r) : "x"(x));
    return r;
}
float ceilf(float x) {
    if (x != x) return x;  /* NaN */
    if (x >= 8388608.0f || x <= -8388608.0f) return x;  /* already integral or inf */
    int i = (int)x;
    float r = (float)i;
    r = r < x ? r + 1.0f : r;
    /* Preserve negative zero: ceil(-0.5) = -0.0, ceil(-0.0) = -0.0 */
    if (r == 0.0f) { union { float f; uint32_t u; } u = {x}; if (u.u & 0x80000000U) return -0.0f; }
    return r;
}
float floorf(float x) {
    if (x != x) return x;
    if (x >= 8388608.0f || x <= -8388608.0f) return x;
    int i = (int)x;
    float r = (float)i;
    r = r > x ? r - 1.0f : r;
    if (r == 0.0f) { union { float f; uint32_t u; } u = {x}; if (u.u & 0x80000000U) return -0.0f; }
    return r;
}
float truncf(float x) {
    if (x != x) return x;
    if (x >= 8388608.0f || x <= -8388608.0f) return x;
    float r = (float)(int)x;
    /* Preserve negative zero: trunc(-0.5) = -0.0, trunc(-0.0) = -0.0 */
    if (r == 0.0f) { union { float f; uint32_t u; } u = {x}; if (u.u & 0x80000000U) return -0.0f; }
    return r;
}
float nearbyintf(float x) {
    /* Round to nearest even (default rounding mode) */
    if (x != x) return x;
    if (x >= 8388608.0f || x <= -8388608.0f) return x;
    /* Use the "add then subtract" trick to get hardware rounding */
    union { float f; uint32_t u; } u = {x};
    float sign = (u.u & 0x80000000U) ? -1.0f : 1.0f;
    float ax = x * sign;
    float r = (ax + 8388608.0f) - 8388608.0f;
    return r * sign;
}
double fabs(double x) {
    union { double d; uint64_t u; } u = {x};
    u.u &= UINT64_C(0x7FFFFFFFFFFFFFFF);
    return u.d;
}
double sqrt(double x) {
    double r;
    __asm__ ("sqrtsd %1, %0" : "=x"(r) : "x"(x));
    return r;
}
double ceil(double x) {
    if (x != x) return x;
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;
    long i = (long)x;
    double r = (double)i;
    r = r < x ? r + 1.0 : r;
    if (r == 0.0) { union { double d; uint64_t u; } u = {x}; if (u.u & UINT64_C(0x8000000000000000)) return -0.0; }
    return r;
}
double floor(double x) {
    if (x != x) return x;
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;
    long i = (long)x;
    double r = (double)i;
    r = r > x ? r - 1.0 : r;
    if (r == 0.0) { union { double d; uint64_t u; } u = {x}; if (u.u & UINT64_C(0x8000000000000000)) return -0.0; }
    return r;
}
double trunc(double x) {
    if (x != x) return x;
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;
    double r = (double)(long)x;
    if (r == 0.0) { union { double d; uint64_t u; } u = {x}; if (u.u & UINT64_C(0x8000000000000000)) return -0.0; }
    return r;
}
double nearbyint(double x) {
    if (x != x) return x;
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;
    union { double d; uint64_t u; } u = {x};
    double sign = (u.u & UINT64_C(0x8000000000000000)) ? -1.0 : 1.0;
    double ax = x * sign;
    double r = (ax + 4503599627370496.0) - 4503599627370496.0;
    return r * sign;
}
#endif
