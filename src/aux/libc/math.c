/* Guest-libc math, reusing the engine's portable IEEE-754 helpers. */

#include <errno.h>
#include <math.h>
#include "runtime/math.c"

/* Nearest integer, with halfway cases away from zero. Values at or above
 * 2^52 are already integral. Subtract before adding to avoid rounding a
 * value immediately below a half-integer up to that half-integer. */
double round(double value) {
    if (__builtin_isnan(value)) return value + value;
    if (!__builtin_isfinite(value) || fabs(value) >= 0x1p52) return value;
    double magnitude = fabs(value);
    double integer = trunc(magnitude);
    if (magnitude - integer >= 0.5) integer += 1.0;
    return copysign(integer, value);
}

/* Wasm float-to-integer conversion traps outside the destination range.
 * Report a domain error before converting; LONG_MIN is the chosen result
 * for non-finite or unrepresentable inputs. Floating exceptions are not
 * emulated by the guest's fixed rounding environment. */
long lround(double value) {
    double integer = round(value);
    if (!isfinite(integer) || integer < (double)LONG_MIN ||
        integer >= -(double)LONG_MIN) {
        errno = EDOM;
        return LONG_MIN;
    }
    return (long)integer;
}

/* Reduce positive binary64 to m * 2^exponent, with m around one. The
 * atanh series has |(m-1)/(m+1)| <= 0.172; its omitted tail is below
 * 2^-80 after the term with denominator 31. Arithmetic rounding remains
 * binary64: this is not a promise of correctly rounded transcendental math. */
double log2(double value) {
    if (__builtin_isnan(value)) return value + value;
    if (value == 0.0) {
        errno = ERANGE;
        return -__builtin_inf();
    }
    if (value < 0.0) {
        errno = EDOM;
        return __builtin_nan("");
    }
    if (__builtin_isinf(value)) return value;

    union { double value; uint64_t bits; } input = {value};
    int exponent = (int)((input.bits >> 52) & 0x7ff) - 1023;
    if (exponent == -1023) {
        input.value *= 0x1p54;
        exponent = (int)((input.bits >> 52) & 0x7ff) - 1023 - 54;
    }
    input.bits = (input.bits & UINT64_C(0x000fffffffffffff)) |
                 UINT64_C(0x3ff0000000000000);
    double mantissa = input.value;
    if (mantissa > 0x1.6a09e667f3bcdp0) {
        mantissa *= 0.5;
        ++exponent;
    }
    if (mantissa == 1.0) return (double)exponent;
    double z = (mantissa - 1.0) / (mantissa + 1.0);
    double squared = z * z;
    double power = z;
    double sum = z;
    for (int denominator = 3; denominator <= 31; denominator += 2) {
        power *= squared;
        sum += power / (double)denominator;
    }
    return (double)exponent + 0x1.71547652b82fep1 * sum;
}

/* Binary32 inputs promote exactly; share range reduction and errno semantics.
 * Narrow the binary64 approximation once using Wasm's nearest-even conversion.
 * As with log2, this is not a correctly rounded transcendental guarantee. */
float log2f(float value) {
    return (float)log2((double)value);
}

/* Both results have the input's sign, including integral values and infinity.
 * Wasm truncation is exact and does not depend on a host rounding mode. */
double modf(double value, double *integer) {
    *integer = trunc(value);
    if (__builtin_isnan(value)) return value;
    if (__builtin_isinf(value)) return copysign(0.0, value);
    return copysign(value - *integer, value);
}

/* C min/max prefer a numeric operand to NaN. Keep these explicit: Clang's
 * Wasm __builtin_fmin/fmax can lower to a call to this same library function.
 * Resolve zero ties by sign so argument order does not change the result. */
double fmin(double a, double b) {
    if (__builtin_isnan(a)) return b;
    if (__builtin_isnan(b)) return a;
    if (a == 0.0 && b == 0.0) {
        union { double value; uint64_t bits; } x = {a}, y = {b};
        x.bits |= y.bits;
        return x.value;
    }
    return a < b ? a : b;
}

double fmax(double a, double b) {
    if (__builtin_isnan(a)) return b;
    if (__builtin_isnan(b)) return a;
    if (a == 0.0 && b == 0.0) {
        union { double value; uint64_t bits; } x = {a}, y = {b};
        x.bits &= y.bits;
        return x.value;
    }
    return a > b ? a : b;
}
