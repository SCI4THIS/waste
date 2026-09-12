/*
 * stdlib_1.c — Integer conversions, float scanning helper, and misc utilities.
 *
 * Shared by both native and Wasm builds.  Extracted from freestanding_lib.c.
 */

#include <stddef.h>
#include <stdint.h>

/* ---- Integer conversions ---- */

long strtol(const char *s, char **endptr, int base) {
    const char *start = s;
    long result = 0;
    int negative = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '-') { negative = 1; s++; }
    else if (*s == '+') { s++; }
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; }
        else { base = 10; }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    const char *digits_start = s;
    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * base + digit;
        s++;
    }
    if (s == digits_start && endptr) { *endptr = (char *)start; return 0; }
    if (endptr) *endptr = (char *)s;
    return negative ? -result : result;
}

unsigned long strtoul(const char *s, char **endptr, int base) {
    const char *start = s;
    unsigned long result = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '+') s++;
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; }
        else { base = 10; }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    const char *digits_start = s;
    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * (unsigned long)base + (unsigned long)digit;
        s++;
    }
    if (s == digits_start && endptr) { *endptr = (char *)start; return 0; }
    if (endptr) *endptr = (char *)s;
    return result;
}

unsigned long long strtoull(const char *s, char **endptr, int base) {
    const char *start = s;
    unsigned long long result = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '+') s++;
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; }
        else { base = 10; }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    const char *digits_start = s;
    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * (unsigned long long)base + (unsigned long long)digit;
        s++;
    }
    if (s == digits_start && endptr) { *endptr = (char *)start; return 0; }
    if (endptr) *endptr = (char *)s;
    return result;
}

/* ---- Float scanning helper (shared by strtod/strtof implementations) ---- */

const char *scan_float_end(const char *s) {
    int hexadecimal = s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
    if (hexadecimal) s += 2;
    while ((*s >= '0' && *s <= '9') ||
           (hexadecimal && ((*s >= 'a' && *s <= 'f') ||
                            (*s >= 'A' && *s <= 'F'))))
        s++;
    if (*s == '.') {
        s++;
        while ((*s >= '0' && *s <= '9') ||
               (hexadecimal && ((*s >= 'a' && *s <= 'f') ||
                                (*s >= 'A' && *s <= 'F'))))
            s++;
    }
    if ((!hexadecimal && (*s == 'e' || *s == 'E')) ||
        (hexadecimal && (*s == 'p' || *s == 'P'))) {
        const char *exponent = s++;
        if (*s == '+' || *s == '-') s++;
        const char *digits = s;
        while (*s >= '0' && *s <= '9') s++;
        if (s == digits) s = exponent;
    }
    return s;
}

/* ---- Misc ---- */

int abs(int x) { return x < 0 ? -x : x; }

/* GCC runtime helper for __builtin_popcountll with -nostdlib */
long long __popcountdi2(long long a) {
    unsigned long long x = (unsigned long long)a;
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (long long)((x * 0x0101010101010101ULL) >> 56);
}
