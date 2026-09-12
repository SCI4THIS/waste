/*
 * stdio_1.c — Portable snprintf/vsnprintf implementation.
 *
 * Shared by both native and Wasm builds.  Extracted from freestanding_lib.c.
 */

#include <stddef.h>
#include <stdint.h>

/* ---- snprintf helpers ---- */

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

/* ---- snprintf / vsnprintf ---- */

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
