/*
 * freestanding_lib.c — Portable freestanding C library implementations.
 *
 * Shared by both native and Wasm builds.  Contains string operations,
 * integer conversions, snprintf, and math functions.  No platform-specific
 * code — only compiler-provided headers (<stddef.h>, <stdint.h>).
 *
 * Platform-specific code (allocator, I/O, strtod) lives in the platform
 * backends: freestanding_native.c (Linux) and browser_wast.c (Wasm).
 */

#include <stddef.h>
#include <stdint.h>

/* ---- String operations ---- */

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = a, *q = b;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != q[i]) return p[i] < q[i] ? -1 : 1;
    }
    return 0;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) ;
    return dst;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

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

/* ---- snprintf ---- */

static int snprintf_uint(char *buf, size_t n, size_t pos,
                         uint64_t val, int width, int zero_pad) {
    char tmp[20];
    int len = 0;
    if (val == 0) { tmp[len++] = '0'; }
    else { while (val) { tmp[len++] = '0' + (int)(val % 10); val /= 10; } }
    int pad = width > len ? width - len : 0;
    for (int i = 0; i < pad; i++)
        if (pos < n - 1) buf[pos++] = zero_pad ? '0' : ' ';
    for (int i = len - 1; i >= 0; i--)
        if (pos < n - 1) buf[pos++] = tmp[i];
    return (int)pos;
}

static int snprintf_int(char *buf, size_t n, size_t pos,
                        int64_t val, int width, int zero_pad) {
    if (val < 0) {
        if (pos < n - 1) buf[pos++] = '-';
        if (width > 0) width--;
        val = -val;
    }
    return snprintf_uint(buf, n, pos, (uint64_t)val, width, zero_pad);
}

static int snprintf_hex(char *buf, size_t n, size_t pos,
                        uint64_t val, int width, int zero_pad) {
    static const char digits[] = "0123456789abcdef";
    char tmp[16];
    int len = 0;
    if (val == 0) { tmp[len++] = '0'; }
    else { while (val) { tmp[len++] = digits[val & 0xF]; val >>= 4; } }
    int pad = width > len ? width - len : 0;
    for (int i = 0; i < pad; i++)
        if (pos < n - 1) buf[pos++] = zero_pad ? '0' : ' ';
    for (int i = len - 1; i >= 0; i--)
        if (pos < n - 1) buf[pos++] = tmp[i];
    return (int)pos;
}

static int snprintf_hex_float(char *buf, size_t n, size_t pos, double val) {
    /* %a format: [-]0x1.XXXXXXXXXXXXXp[+-]DDD */
    static const char hex[] = "0123456789abcdef";
    union { double d; uint64_t u; } u;
    u.d = val;
    int sign = (int)(u.u >> 63);
    int biased_exp = (int)((u.u >> 52) & 0x7FF);
    uint64_t mant = u.u & 0x000FFFFFFFFFFFFFULL;

    if (sign && pos < n - 1) buf[pos++] = '-';

    if (biased_exp == 0x7FF) {
        const char *s = mant ? "nan" : "inf";
        while (*s && pos < n - 1) buf[pos++] = *s++;
        return (int)pos;
    }

    if (pos < n - 1) buf[pos++] = '0';
    if (pos < n - 1) buf[pos++] = 'x';

    if (biased_exp == 0 && mant == 0) {
        if (pos < n - 1) buf[pos++] = '0';
        if (pos < n - 1) buf[pos++] = 'p';
        if (pos < n - 1) buf[pos++] = '+';
        if (pos < n - 1) buf[pos++] = '0';
        return (int)pos;
    }

    int exponent;
    if (biased_exp == 0) {
        if (pos < n - 1) buf[pos++] = '0';
        if (pos < n - 1) buf[pos++] = '.';
        for (int i = 12; i >= 0; i--) {
            int nibble = (int)((mant >> (i * 4)) & 0xF);
            if (pos < n - 1) buf[pos++] = hex[nibble];
        }
        while (pos > 0 && buf[pos - 1] == '0') pos--;
        if (pos > 0 && buf[pos - 1] == '.') pos--;
        exponent = -1022;
    } else {
        if (pos < n - 1) buf[pos++] = '1';
        if (mant != 0) {
            if (pos < n - 1) buf[pos++] = '.';
            for (int i = 12; i >= 0; i--) {
                int nibble = (int)((mant >> (i * 4)) & 0xF);
                if (pos < n - 1) buf[pos++] = hex[nibble];
            }
            while (pos > 0 && buf[pos - 1] == '0') pos--;
        }
        exponent = biased_exp - 1023;
    }
    if (pos < n - 1) buf[pos++] = 'p';
    if (exponent < 0) {
        if (pos < n - 1) buf[pos++] = '-';
        exponent = -exponent;
    } else {
        if (pos < n - 1) buf[pos++] = '+';
    }
    if (exponent == 0) {
        if (pos < n - 1) buf[pos++] = '0';
    } else {
        char etmp[8];
        int elen = 0;
        while (exponent) { etmp[elen++] = '0' + (exponent % 10); exponent /= 10; }
        for (int i = elen - 1; i >= 0; i--)
            if (pos < n - 1) buf[pos++] = etmp[i];
    }
    return (int)pos;
}

static int snprintf_double(char *buf, size_t n, size_t pos,
                           double val, int width, int precision,
                           int has_precision) {
    /* Simple %f implementation */
    union { double d; uint64_t u; } u;
    u.d = val;
    int sign = (int)(u.u >> 63);
    int biased_exp = (int)((u.u >> 52) & 0x7FF);
    uint64_t mant = u.u & 0x000FFFFFFFFFFFFFULL;

    if (biased_exp == 0x7FF) {
        if (sign && pos < n - 1) buf[pos++] = '-';
        const char *s = mant ? "nan" : "inf";
        while (*s && pos < n - 1) buf[pos++] = *s++;
        return (int)pos;
    }

    if (sign) {
        if (pos < n - 1) buf[pos++] = '-';
        val = -val;
        if (width > 0) width--;
    }

    if (!has_precision) precision = 6;

    /* Split into integer and fractional parts */
    uint64_t int_part = (uint64_t)val;
    double frac = val - (double)int_part;

    /* Round the fractional part */
    double rounding = 0.5;
    for (int i = 0; i < precision; i++) rounding /= 10.0;
    frac += rounding;
    if (frac >= 1.0) {
        int_part++;
        frac -= 1.0;
    }

    /* Print integer part */
    pos = (size_t)snprintf_uint(buf, n, pos, int_part, 0, 0);

    /* Print fractional part */
    if (precision > 0) {
        if (pos < n - 1) buf[pos++] = '.';
        for (int i = 0; i < precision; i++) {
            frac *= 10.0;
            int digit = (int)frac;
            if (digit > 9) digit = 9;
            frac -= (double)digit;
            if (pos < n - 1) buf[pos++] = '0' + digit;
        }
    }

    (void)width;
    return (int)pos;
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    if (n == 0) return 0;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    size_t pos = 0;
    while (*fmt && pos < n - 1) {
        if (*fmt != '%') { buf[pos++] = *fmt++; continue; }
        fmt++;
        /* Parse flags */
        int zero_pad = 0;
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        /* Parse width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        /* Parse precision */
        int precision = 0;
        int has_precision = 0;
        if (*fmt == '.') {
            fmt++;
            has_precision = 1;
            while (*fmt >= '0' && *fmt <= '9') { precision = precision * 10 + (*fmt - '0'); fmt++; }
        }
        /* Parse length modifier */
        int length = 0; /* 0=int, 1=long, 2=long long, 3=size_t */
        if (*fmt == 'z') { length = 3; fmt++; }
        else if (*fmt == 'l') { length = 1; fmt++; if (*fmt == 'l') { length = 2; fmt++; } }
        /* Conversion */
        switch (*fmt) {
        case 'd': {
            int64_t val;
            if (length == 2) val = __builtin_va_arg(ap, long long);
            else if (length == 1) val = __builtin_va_arg(ap, long);
            else val = __builtin_va_arg(ap, int);
            pos = (size_t)snprintf_int(buf, n, pos, val, width, zero_pad);
            break;
        }
        case 'u': {
            uint64_t val;
            if (length == 3) val = __builtin_va_arg(ap, size_t);
            else if (length == 2) val = __builtin_va_arg(ap, unsigned long long);
            else if (length == 1) val = __builtin_va_arg(ap, unsigned long);
            else val = __builtin_va_arg(ap, unsigned int);
            pos = (size_t)snprintf_uint(buf, n, pos, val, width, zero_pad);
            break;
        }
        case 'x': {
            uint64_t val;
            if (length == 3) val = __builtin_va_arg(ap, size_t);
            else if (length == 2) val = __builtin_va_arg(ap, unsigned long long);
            else if (length == 1) val = __builtin_va_arg(ap, unsigned long);
            else val = __builtin_va_arg(ap, unsigned int);
            pos = (size_t)snprintf_hex(buf, n, pos, val, width, zero_pad);
            break;
        }
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && pos < n - 1) buf[pos++] = *s++;
            break;
        }
        case 'c': {
            int c = __builtin_va_arg(ap, int);
            if (pos < n - 1) buf[pos++] = (char)c;
            break;
        }
        case 'p': {
            uintptr_t val = (uintptr_t)__builtin_va_arg(ap, void *);
            if (pos < n - 1) buf[pos++] = '0';
            if (pos < n - 1) buf[pos++] = 'x';
            pos = (size_t)snprintf_hex(buf, n, pos, (uint64_t)val, 0, 0);
            break;
        }
        case 'a': {
            double val = __builtin_va_arg(ap, double);
            pos = (size_t)snprintf_hex_float(buf, n, pos, val);
            break;
        }
        case 'f': {
            double val = __builtin_va_arg(ap, double);
            pos = (size_t)snprintf_double(buf, n, pos, val, width, precision,
                                          has_precision);
            break;
        }
        case '%':
            if (pos < n - 1) buf[pos++] = '%';
            break;
        case '\0':
            goto done;
        default:
            if (pos < n - 1) buf[pos++] = *fmt;
            break;
        }
        fmt++;
    }
done:
    buf[pos] = '\0';
    __builtin_va_end(ap);
    return (int)pos;
}

int vsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap) {
    if (n == 0) return 0;
    size_t pos = 0;
    while (*fmt && pos < n - 1) {
        if (*fmt != '%') { buf[pos++] = *fmt++; continue; }
        fmt++;
        int zero_pad = 0;
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        int precision = 0;
        int has_precision = 0;
        if (*fmt == '.') {
            fmt++;
            has_precision = 1;
            while (*fmt >= '0' && *fmt <= '9') { precision = precision * 10 + (*fmt - '0'); fmt++; }
        }
        int length = 0;
        if (*fmt == 'z') { length = 3; fmt++; }
        else if (*fmt == 'l') { length = 1; fmt++; if (*fmt == 'l') { length = 2; fmt++; } }
        switch (*fmt) {
        case 'd': {
            int64_t val;
            if (length == 2) val = __builtin_va_arg(ap, long long);
            else if (length == 1) val = __builtin_va_arg(ap, long);
            else val = __builtin_va_arg(ap, int);
            pos = (size_t)snprintf_int(buf, n, pos, val, width, zero_pad);
            break;
        }
        case 'u': {
            uint64_t val;
            if (length == 3) val = __builtin_va_arg(ap, size_t);
            else if (length == 2) val = __builtin_va_arg(ap, unsigned long long);
            else if (length == 1) val = __builtin_va_arg(ap, unsigned long);
            else val = __builtin_va_arg(ap, unsigned int);
            pos = (size_t)snprintf_uint(buf, n, pos, val, width, zero_pad);
            break;
        }
        case 'x': {
            uint64_t val;
            if (length == 3) val = __builtin_va_arg(ap, size_t);
            else if (length == 2) val = __builtin_va_arg(ap, unsigned long long);
            else if (length == 1) val = __builtin_va_arg(ap, unsigned long);
            else val = __builtin_va_arg(ap, unsigned int);
            pos = (size_t)snprintf_hex(buf, n, pos, val, width, zero_pad);
            break;
        }
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && pos < n - 1) buf[pos++] = *s++;
            break;
        }
        case 'c': {
            int c = __builtin_va_arg(ap, int);
            if (pos < n - 1) buf[pos++] = (char)c;
            break;
        }
        case 'p': {
            uintptr_t val = (uintptr_t)__builtin_va_arg(ap, void *);
            if (pos < n - 1) buf[pos++] = '0';
            if (pos < n - 1) buf[pos++] = 'x';
            pos = (size_t)snprintf_hex(buf, n, pos, (uint64_t)val, 0, 0);
            break;
        }
        case 'a': {
            double val = __builtin_va_arg(ap, double);
            pos = (size_t)snprintf_hex_float(buf, n, pos, val);
            break;
        }
        case 'f': {
            double val = __builtin_va_arg(ap, double);
            pos = (size_t)snprintf_double(buf, n, pos, val, width, precision,
                                          has_precision);
            break;
        }
        case '%':
            if (pos < n - 1) buf[pos++] = '%';
            break;
        case '\0':
            goto vdone;
        default:
            if (pos < n - 1) buf[pos++] = *fmt;
            break;
        }
        fmt++;
    }
vdone:
    buf[pos] = '\0';
    return (int)pos;
}

/* ---- Math: IEEE-754 power-of-two construction ---- */

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

/* ---- Math: copysign, fmin, fmax ---- */

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

/* ---- Math: FMA (relaxed semantics) ---- */

float fmaf(float a, float b, float c) {
    return (float)((double)a * (double)b + (double)c);
}

double fma(double a, double b, double c) {
    return a * b + c;
}

/* ---- Math: builtins (compile to native SSE/Wasm instructions) ---- */

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
