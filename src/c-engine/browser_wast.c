#include "waste_exec.h"
#include "wast_types.h"
#include "wast_runner.h"
#include "wast_encode.h"
#include "wast_stream.h"

#include <stddef.h>
#include <stdint.h>

/* ---- Freestanding stubs ---- */

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

__attribute__((import_module("waste_host"), import_name("strtod")))
extern double waste_host_strtod(const char *text, size_t length);

__attribute__((import_module("waste_host"), import_name("strtof")))
extern float waste_host_strtof(const char *text, size_t length);

static const char *scan_float_end(const char *s) {
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

double strtod(const char *s, char **endptr) {
    const char *start = s;
    const char *number_start;
    int negative = 0;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    number_start = s;
    if (*s == '-') { negative = 1; s++; }
    else if (*s == '+') { s++; }

    /* Check for inf/nan */
    if ((s[0] == 'i' || s[0] == 'I') && (s[1] == 'n' || s[1] == 'N') &&
        (s[2] == 'f' || s[2] == 'F')) {
        s += 3;
        if ((s[0] == 'i' || s[0] == 'I') && (s[1] == 'n' || s[1] == 'N') &&
            (s[2] == 'i' || s[2] == 'I') && (s[3] == 't' || s[3] == 'T') &&
            (s[4] == 'y' || s[4] == 'Y')) s += 5;
        if (endptr) *endptr = (char *)s;
        return negative ? -__builtin_inf() : __builtin_inf();
    }
    if ((s[0] == 'n' || s[0] == 'N') && (s[1] == 'a' || s[1] == 'A') &&
        (s[2] == 'n' || s[2] == 'N')) {
        s += 3;
        if (*s == '(') { while (*s && *s != ')') s++; if (*s == ')') s++; }
        if (endptr) *endptr = (char *)s;
        return __builtin_nan("");
    }

    s = scan_float_end(s);
    if (s == number_start ||
        (s == number_start + 1 && (*number_start == '+' ||
                                  *number_start == '-'))) {
        if (endptr) *endptr = (char *)start;
        return 0.0;
    }
    if (endptr) *endptr = (char *)s;
    return waste_host_strtod(number_start, (size_t)(s - number_start));
}

float strtof(const char *s, char **endptr) {
    const char *start = s;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    const char *number_start = s;
    if (*s == '+' || *s == '-') s++;
    if ((s[0] == 'i' || s[0] == 'I') ||
        (s[0] == 'n' || s[0] == 'N'))
        return (float)strtod(start, endptr);
    const char *end = scan_float_end(s);
    if (end == s) {
        if (endptr) *endptr = (char *)start;
        return 0.0f;
    }
    if (endptr) *endptr = (char *)end;
    return waste_host_strtof(number_start,
                             (size_t)(end - number_start));
}

char *getenv(const char *name) { (void)name; return (void *)0; }
int isatty(int fd) { (void)fd; return 0; }
int abs(int x) { return x < 0 ? -x : x; }

_Noreturn void exit(int status) { (void)status; __builtin_trap(); }

/* File I/O no-ops */
typedef struct { int dummy; } FILE;
int fprintf(FILE *f, const char *fmt, ...) { (void)f; (void)fmt; return 0; }
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f) { (void)p; (void)sz; (void)n; (void)f; return 0; }
size_t fread(void *p, size_t sz, size_t n, FILE *f) { (void)p; (void)sz; (void)n; (void)f; return 0; }
int fputc(int c, FILE *f) { (void)c; (void)f; return 0; }
int fputs(const char *s, FILE *f) { (void)s; (void)f; return 0; }
int ferror(FILE *f) { (void)f; return 0; }
FILE *fopen(const char *path, const char *mode) { (void)path; (void)mode; return (void *)0; }
int fseek(FILE *f, long off, int whence) { (void)f; (void)off; (void)whence; return 0; }
long ftell(FILE *f) { (void)f; return 0; }
int fclose(FILE *f) { (void)f; return 0; }
int putchar(int c) { (void)c; return 0; }
int printf(const char *fmt, ...) { (void)fmt; return 0; }
int getc(FILE *f) { (void)f; return -1; }
void clearerr(FILE *f) { (void)f; }
int fileno(FILE *f) { (void)f; return -1; }

int errno = 0;
int yydebug = 0;

/* Math stubs.  __builtin_ldexp is not a Wasm instruction: Clang lowers it
 * back to a call to ldexp, so using it here recursively called this shim.
 * Construct exact powers of two from their IEEE-754 representation instead. */
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
        /* denormal: multiply up to normalize */
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

double copysign(double x, double y) { return __builtin_copysign(x, y); }
double fmin(double a, double b) { return __builtin_fmin(a, b); }
double fmax(double a, double b) { return __builtin_fmax(a, b); }

/* ---- snprintf: working implementation ---- */

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
        /* inf or nan */
        const char *s = mant ? "nan" : "inf";
        while (*s && pos < n - 1) buf[pos++] = *s++;
        return (int)pos;
    }

    if (pos < n - 1) buf[pos++] = '0';
    if (pos < n - 1) buf[pos++] = 'x';

    if (biased_exp == 0 && mant == 0) {
        /* zero: 0x0p+0 */
        if (pos < n - 1) buf[pos++] = '0';
        if (pos < n - 1) buf[pos++] = 'p';
        if (pos < n - 1) buf[pos++] = '+';
        if (pos < n - 1) buf[pos++] = '0';
        return (int)pos;
    }

    int exponent;
    if (biased_exp == 0) {
        /* denormal: 0x0.XXXXXp-1022 */
        if (pos < n - 1) buf[pos++] = '0';
        if (pos < n - 1) buf[pos++] = '.';
        /* output mantissa as 13 hex digits */
        for (int i = 12; i >= 0; i--) {
            int nibble = (int)((mant >> (i * 4)) & 0xF);
            if (pos < n - 1) buf[pos++] = hex[nibble];
        }
        /* strip trailing zeros */
        while (pos > 0 && buf[pos - 1] == '0') pos--;
        if (pos > 0 && buf[pos - 1] == '.') pos--;
        exponent = -1022;
    } else {
        /* normal: 1.XXXXXXXXXXXXXpEEE */
        if (pos < n - 1) buf[pos++] = '1';
        if (mant != 0) {
            if (pos < n - 1) buf[pos++] = '.';
            /* output mantissa as 13 hex digits */
            for (int i = 12; i >= 0; i--) {
                int nibble = (int)((mant >> (i * 4)) & 0xF);
                if (pos < n - 1) buf[pos++] = hex[nibble];
            }
            /* strip trailing zeros */
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
    /* write exponent digits */
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
        /* Parse length modifier */
        int length = 0; /* 0=int, 1=long, 2=long long */
        if (*fmt == 'l') { length = 1; fmt++; if (*fmt == 'l') { length = 2; fmt++; } }
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
            if (length == 2) val = __builtin_va_arg(ap, unsigned long long);
            else if (length == 1) val = __builtin_va_arg(ap, unsigned long);
            else val = __builtin_va_arg(ap, unsigned int);
            pos = (size_t)snprintf_uint(buf, n, pos, val, width, zero_pad);
            break;
        }
        case 'x': {
            uint64_t val;
            if (length == 2) val = __builtin_va_arg(ap, unsigned long long);
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

/* ---- Freestanding heap allocator ---- */
extern unsigned char __heap_base;
static uintptr_t heap_cursor;

typedef struct heap_block {
    size_t size;
    struct heap_block *next;
    uint32_t is_free;
    uint32_t reserved;
} heap_block;

static heap_block *heap_blocks;

static size_t heap_align(size_t size) {
    return (size + 15u) & ~(size_t)15u;
}

static int heap_ensure(uintptr_t limit) {
    uintptr_t memory_size =
        (uintptr_t)__builtin_wasm_memory_size(0) * 65536u;
    if (limit <= memory_size) return 1;
    uintptr_t missing = limit - memory_size;
    uint32_t pages = (uint32_t)((missing + 65535u) / 65536u);
    return __builtin_wasm_memory_grow(0, pages) != (size_t)-1;
}

static void heap_split(heap_block *block, size_t size) {
    const size_t header_size = heap_align(sizeof(heap_block));
    if (block->size < size + header_size + 16u) return;
    heap_block *rest =
        (heap_block *)((unsigned char *)(block + 1) + size);
    rest->size = block->size - size - header_size;
    rest->next = block->next;
    rest->is_free = 1;
    rest->reserved = 0;
    block->size = size;
    block->next = rest;
}

void *malloc(size_t size) {
    const size_t header_size = heap_align(sizeof(heap_block));
    if (size == 0) size = 1;
    if (size > (size_t)-1 - 15u) return (void *)0;
    size = heap_align(size);
    for (heap_block *block = heap_blocks; block; block = block->next) {
        if (block->is_free && block->size >= size) {
            heap_split(block, size);
            block->is_free = 0;
            return block + 1;
        }
    }
    if (heap_cursor == 0) heap_cursor = (uintptr_t)&__heap_base;
    uintptr_t start = (heap_cursor + 15u) & ~(uintptr_t)15u;
    if (start > (uintptr_t)-1 - header_size ||
        size > (uintptr_t)-1 - start - header_size)
        return (void *)0;
    uintptr_t limit = start + header_size + size;
    if (!heap_ensure(limit)) return (void *)0;
    heap_block *block = (heap_block *)start;
    block->size = size;
    block->next = (void *)0;
    block->is_free = 0;
    block->reserved = 0;
    if (!heap_blocks) {
        heap_blocks = block;
    } else {
        heap_block *last = heap_blocks;
        while (last->next) last = last->next;
        last->next = block;
    }
    heap_cursor = limit;
    return block + 1;
}

void *calloc(size_t count, size_t size) {
    unsigned char *result;
    size_t total;
    if (count != 0 && size > (size_t)-1 / count) return (void *)0;
    total = count * size;
    result = (unsigned char *)malloc(total);
    if (!result) return (void *)0;
    for (size_t i = 0; i < total; i++) result[i] = 0;
    return result;
}

void free(void *ptr) {
    if (!ptr) return;
    const size_t header_size = heap_align(sizeof(heap_block));
    heap_block *block = (heap_block *)ptr - 1;
    block->is_free = 1;
    while (block->next && block->next->is_free) {
        block->size += header_size + block->next->size;
        block->next = block->next->next;
    }
    heap_block *previous = (void *)0;
    for (heap_block *at = heap_blocks; at && at != block; at = at->next)
        previous = at;
    if (previous && previous->is_free) {
        previous->size += header_size + block->size;
        previous->next = block->next;
    }
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

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return (void *)0; }
    const size_t header_size = heap_align(sizeof(heap_block));
    heap_block *block = (heap_block *)ptr - 1;
    size_t aligned = heap_align(size);
    if (block->size >= aligned) {
        heap_split(block, aligned);
        return ptr;
    }
    if (block->next && block->next->is_free &&
        block->size + header_size + block->next->size >= aligned) {
        block->size += header_size + block->next->size;
        block->next = block->next->next;
        heap_split(block, aligned);
        return ptr;
    }
    void *fresh = malloc(size);
    if (!fresh) return (void *)0;
    memcpy(fresh, ptr, block->size < size ? block->size : size);
    free(ptr);
    return fresh;
}

/* ---- Math stubs (relaxed semantics are sufficient) ---- */

float fmaf(float a, float b, float c) {
    /* Relaxed FMA: a*b+c without strict rounding guarantee */
    return (float)((double)a * (double)b + (double)c);
}

double fma(double a, double b, double c) {
    return a * b + c;
}

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

/* ---- Engine state (existing per-module API) ---- */

static waste_exec_engine *g_engine = (void *)0;
static exec_error         g_error;

#define LINK_MAX_MODULES 64
#define LINK_MAX_IMPORTS 512
typedef struct {
    waste_exec_engine *engine;
    char id[WAST_MAX_EXPORT_NAME];
    char registered[WAST_MAX_EXPORT_NAME];
} linked_module;
typedef struct { waste_exec_engine *engine; uint32_t func_idx; } linked_func;
typedef struct { const uint8_t *p, *end; } bin_reader;
typedef struct { char module[WAST_MAX_EXPORT_NAME], name[WAST_MAX_EXPORT_NAME]; uint8_t kind; } import_request;
static linked_module g_modules[LINK_MAX_MODULES];
static linked_func g_linked_funcs[LINK_MAX_IMPORTS];
static import_request g_import_requests[LINK_MAX_IMPORTS];
static exec_host_import g_func_imports[LINK_MAX_IMPORTS];
static exec_global_import g_global_imports[LINK_MAX_IMPORTS];
static exec_memory_import g_memory_imports[LINK_MAX_IMPORTS];
static exec_table_import g_table_imports[LINK_MAX_IMPORTS];
static uint32_t g_module_count, g_linked_func_count, g_current_module;

static void set_error(const char *message) {
    size_t i = 0;
    memset(&g_error, 0, sizeof(g_error));
    while (message[i] && i + 1 < sizeof(g_error.message)) {
        g_error.message[i] = message[i]; i++;
    }
}
static int read_u8(bin_reader *r, uint8_t *v) { if (r->p >= r->end) return 0; *v=*r->p++; return 1; }
static int read_leb(bin_reader *r, uint32_t *v) {
    uint32_t out=0; int shift=0; uint8_t b;
    do { if(shift>=35||!read_u8(r,&b))return 0; out|=(uint32_t)(b&0x7f)<<shift; shift+=7; } while(b&0x80);
    *v=out; return 1;
}
static int read_leb64(bin_reader *r, uint64_t *v) {
    uint64_t out=0; int shift=0; uint8_t b;
    do {
        if(shift>=70||!read_u8(r,&b))return 0;
        if(shift==63&&(b&0xfeu))return 0;
        out|=(uint64_t)(b&0x7f)<<shift; shift+=7;
    } while(b&0x80);
    *v=out; return 1;
}
static int read_name(bin_reader *r, char *out) {
    uint32_t n; if(!read_leb(r,&n)||n>=WAST_MAX_EXPORT_NAME||(size_t)(r->end-r->p)<n)return 0;
    memcpy(out,r->p,n);out[n]='\0';r->p+=n;return 1;
}
static int skip_limits(bin_reader *r) {
    uint32_t flags; uint64_t value;
    if(!read_leb(r,&flags)||!read_leb64(r,&value))return 0;
    if(flags&1u) return read_leb64(r,&value); return 1;
}
static int skip_valtype(bin_reader *r) {
    uint8_t type, byte;
    if(!read_u8(r,&type))return 0;
    if(type!=0x63&&type!=0x64)return 1;
    do { if(!read_u8(r,&byte))return 0; } while(byte&0x80);
    return 1;
}
static int scan_imports(const uint8_t *bytes,size_t size,import_request *req,uint32_t *count) {
    bin_reader r={bytes,bytes+size}; uint32_t section_size,n;
    if(size<8){return 0;} r.p+=8;
    while(r.p<r.end){uint8_t id;if(!read_u8(&r,&id)||!read_leb(&r,&section_size)||(size_t)(r.end-r.p)<section_size)return 0;
        bin_reader s={r.p,r.p+section_size};r.p+=section_size;if(id!=2)continue;
        if(!read_leb(&s,&n)||n>LINK_MAX_IMPORTS)return 0;
        for(uint32_t i=0;i<n;i++){uint8_t kind;uint32_t ignored;if(*count>=LINK_MAX_IMPORTS||!read_name(&s,req[*count].module)||!read_name(&s,req[*count].name)||!read_u8(&s,&kind))return 0;
            req[*count].kind=kind;(*count)++;
            if(kind==0){if(!read_leb(&s,&ignored))return 0;}
            else if(kind==1){if(!skip_valtype(&s)||!skip_limits(&s))return 0;}
            else if(kind==2){if(!skip_limits(&s))return 0;}
            else if(kind==3){if(!skip_valtype(&s)||!read_u8(&s,&kind))return 0;}
            else return 0;
        }
        return s.p==s.end;
    }
    return 1;
}
static linked_module *registered_module(const char *name) {
    for(uint32_t i=g_module_count;i>0;i--)if(strcmp(g_modules[i-1].registered,name)==0)return &g_modules[i-1];
    return (void *)0;
}
static exec_status linked_call(void *data,const wasm_value *args,int argc,wasm_value *results,int *result_count,exec_error *error) {
    linked_func *f=(linked_func *)data;return exec_invoke(f->engine,f->func_idx,args,argc,results,result_count,error);
}
static exec_status spectest_noop(void *data,const wasm_value *args,int argc,
                                 wasm_value *results,int *result_count,
                                 exec_error *error) {
    (void)data;(void)args;(void)argc;(void)results;(void)error;*result_count=0;return EXEC_OK;
}

/* ---- native_store infrastructure (ported from main_wast.c) ---- */

typedef struct {
    waste_exec_engine *engine;
    uint32_t func_idx;
} native_linked_func;

typedef struct native_call_block {
    native_linked_func *calls;
    struct native_call_block *next;
} native_call_block;

#define NATIVE_STORE_MAX_MODULES 128

typedef struct {
    waste_exec_engine *engine;
    const wast_module *module;
    char id[WAST_MAX_EXPORT_NAME];
    char registered[WAST_MAX_EXPORT_NAME];
} native_linked_module;

typedef struct {
    native_linked_module modules[NATIVE_STORE_MAX_MODULES];
    int module_count;
    native_call_block *call_blocks;
    waste_exec_engine **orphan_engines;
    int orphan_count;
    int orphan_capacity;
    exec_memory spectest_memory;
    exec_table spectest_table;
    exec_global spectest_i32;
    exec_global spectest_i64;
    exec_global spectest_f32;
    exec_global spectest_f64;
} native_store;

static exec_status native_linked_call(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error) {
    native_linked_func *function = (native_linked_func *)data;
    return exec_invoke(function->engine, function->func_idx, args, arg_count,
                       results, result_count, error);
}

static exec_status native_spectest_noop(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error) {
    (void)data;
    (void)args;
    (void)arg_count;
    (void)results;
    (void)error;
    *result_count = 0;
    return EXEC_OK;
}

static int native_spectest_has_function(const char *name) {
    return strcmp(name, "print") == 0 ||
           strcmp(name, "print_i32") == 0 ||
           strcmp(name, "print_i64") == 0 ||
           strcmp(name, "print_f32") == 0 ||
           strcmp(name, "print_f64") == 0 ||
           strcmp(name, "print_i32_f32") == 0 ||
           strcmp(name, "print_f64_f64") == 0;
}

static void native_store_init(native_store *store) {
    memset(store, 0, sizeof(*store));
    store->spectest_memory.pages = 1;
    store->spectest_memory.max_pages = 2;
    store->spectest_memory.has_max = 1;
    store->spectest_memory.data = calloc(65536, 1);
    store->spectest_table.size = 10;
    store->spectest_table.max_size = 20;
    store->spectest_table.has_max = 1;
    store->spectest_table.element_type = WASM_VALTYPE_FUNCREF;
    store->spectest_table.elements = calloc(10, sizeof(exec_table_element));
    store->spectest_i32.value.type = WASM_VALTYPE_I32;
    store->spectest_i32.value.i32 = 666;
    store->spectest_i64.value.type = WASM_VALTYPE_I64;
    store->spectest_i64.value.i64 = 666;
    store->spectest_f32.value.type = WASM_VALTYPE_F32;
    store->spectest_f32.value.f32 = 666.6f;
    store->spectest_f64.value.type = WASM_VALTYPE_F64;
    store->spectest_f64.value.f64 = 666.6;
}

static void native_store_free(native_store *store) {
    for (int i = store->module_count; i > 0; i--)
        exec_free(store->modules[i - 1].engine);
    for (int i = store->orphan_count; i > 0; i--)
        exec_free(store->orphan_engines[i - 1]);
    /* In bump allocator free is a no-op, but call it for correctness */
    native_call_block *block = store->call_blocks;
    while (block) {
        native_call_block *next = block->next;
        free(block->calls);
        free(block);
        block = next;
    }
    free(store->spectest_memory.data);
    free(store->spectest_table.elements);
    free(store->orphan_engines);
    memset(store, 0, sizeof(*store));
}

static int native_store_keep_orphan(native_store *store,
                                    waste_exec_engine *engine) {
    if (store->orphan_count == store->orphan_capacity) {
        int capacity = store->orphan_capacity ?
                       store->orphan_capacity * 2 : 16;
        waste_exec_engine **engines = realloc(
            store->orphan_engines, (size_t)capacity * sizeof(*engines));
        if (!engines) return 0;
        store->orphan_engines = engines;
        store->orphan_capacity = capacity;
    }
    store->orphan_engines[store->orphan_count++] = engine;
    return 1;
}

static native_linked_module *native_registered_module(native_store *store,
                                                       const char *name) {
    for (int i = store->module_count; i > 0; i--)
        if (strcmp(store->modules[i - 1].registered, name) == 0)
            return &store->modules[i - 1];
    return (void *)0;
}

static waste_exec_engine *native_selected_engine(native_store *store,
                                                  const char *id) {
    if (!id || !id[0])
        return store->module_count ?
               store->modules[store->module_count - 1].engine : (void *)0;
    for (int i = store->module_count; i > 0; i--)
        if (strcmp(store->modules[i - 1].id, id) == 0)
            return store->modules[i - 1].engine;
    return (void *)0;
}

static native_linked_module *native_selected_module(native_store *store,
                                                     const char *id) {
    if (!id || !id[0])
        return store->module_count ?
               &store->modules[store->module_count - 1] : (void *)0;
    for (int i = store->module_count; i > 0; i--)
        if (strcmp(store->modules[i - 1].id, id) == 0)
            return &store->modules[i - 1];
    return (void *)0;
}

static int native_store_add(native_store *store, waste_exec_engine *engine,
                            const wast_module *identity,
                            const wast_module *metadata) {
    if (store->module_count >= NATIVE_STORE_MAX_MODULES) return 0;
    native_linked_module *linked = &store->modules[store->module_count++];
    memset(linked, 0, sizeof(*linked));
    linked->engine = engine;
    linked->module = metadata;
    snprintf(linked->id, sizeof(linked->id), "%s", identity->id);
    snprintf(linked->registered, sizeof(linked->registered), "%s",
             identity->register_name);
    return 1;
}

static const wast_module *native_find_definition(const wast_script *script,
                                                 int before_group,
                                                 const char *id) {
    for (int i = before_group - 1; i >= 0; i--) {
        const wast_module *module = &script->groups[i].module;
        if (module->is_definition && strcmp(module->id, id) == 0)
            return module;
    }
    return (void *)0;
}

static exec_global *native_spectest_global(native_store *store,
                                           const char *name) {
    if (strcmp(name, "global_i32") == 0) return &store->spectest_i32;
    if (strcmp(name, "global_i64") == 0) return &store->spectest_i64;
    if (strcmp(name, "global_f32") == 0) return &store->spectest_f32;
    if (strcmp(name, "global_f64") == 0) return &store->spectest_f64;
    return (void *)0;
}

static const wast_tag *native_find_exported_tag(
        const native_linked_module *provider, const char *name) {
    if (!provider || !provider->module) return (void *)0;
    for (int i = 0; i < provider->module->export_count; i++) {
        const wast_export *export_ = &provider->module->exports[i];
        if (export_->kind == 4 &&
            export_->index < (uint32_t)provider->module->tag_count &&
            strcmp(export_->name, name) == 0)
            return &provider->module->tags[export_->index];
    }
    for (int i = 0; i < provider->module->tag_count; i++) {
        const wast_tag *tag = &provider->module->tags[i];
        if (tag->has_export_name && strcmp(tag->export_name, name) == 0)
            return tag;
    }
    return (void *)0;
}

static int native_tag_signature_matches(const wast_tag *left,
                                        const wast_tag *right) {
    return left->param_count == right->param_count &&
           memcmp(left->params, right->params,
                  (size_t)left->param_count * sizeof(left->params[0])) == 0;
}

static exec_status native_load_module(native_store *store,
                                      const wast_module *module,
                                      const uint8_t *bytes, size_t size,
                                      waste_exec_engine **engine_out,
                                      exec_error *error) {
    /* Resolve tag imports before loading so the executor can preserve tag
     * identity across module boundaries. */
    for (int i = 0; i < module->tag_count; i++) {
        const wast_tag *tag = &module->tags[i];
        if (!tag->is_import) continue;
        native_linked_module *provider = native_registered_module(
            store, tag->import_module);
        const wast_tag *provided = native_find_exported_tag(
            provider, tag->import_name);
        if (!provided) {
            error->status = EXEC_ERROR_NOT_FOUND;
            snprintf(error->message, sizeof(error->message),
                     "unresolved tag import %s.%s", tag->import_module,
                     tag->import_name);
            return error->status;
        }
        if (!native_tag_signature_matches(tag, provided)) {
            error->status = EXEC_ERROR_FORMAT;
            snprintf(error->message, sizeof(error->message),
                     "incompatible tag import type for %s.%s",
                     tag->import_module, tag->import_name);
            return error->status;
        }
    }
    size_t function_count = 0, global_count = 0;
    size_t memory_count = 0, table_count = 0, tag_count = 0;
    import_request *binary_imports = (void *)0;
    uint32_t binary_import_count = 0;
    for (int i = 0; i < module->func_count; i++)
        function_count += module->funcs[i].is_import != 0;
    for (int i = 0; i < module->global_count; i++)
        global_count += module->globals[i].is_import != 0;
    for (int i = 0; i < module->memory_count; i++)
        memory_count += module->memories[i].is_import != 0;
    for (int i = 0; i < module->table_count; i++)
        table_count += module->tables[i].is_import != 0;
    for (int i = 0; i < module->tag_count; i++)
        tag_count += module->tags[i].is_import != 0;

    /* A `(module binary ...)` deliberately bypasses the text module model,
     * so derive its imports from the embedded bytes.  The ordinary text path
     * keeps using its richer metadata (including typed tags). */
    if (function_count == 0 && global_count == 0 && memory_count == 0 &&
        table_count == 0 && tag_count == 0) {
        binary_imports = calloc(LINK_MAX_IMPORTS, sizeof(*binary_imports));
        if (!binary_imports ||
            !scan_imports(bytes, size, binary_imports,
                          &binary_import_count)) {
            free(binary_imports);
            error->status = EXEC_ERROR_FORMAT;
            snprintf(error->message, sizeof(error->message),
                     "invalid binary module import section");
            return error->status;
        }
        for (uint32_t i = 0; i < binary_import_count; i++) {
            function_count += binary_imports[i].kind == 0;
            table_count += binary_imports[i].kind == 1;
            memory_count += binary_imports[i].kind == 2;
            global_count += binary_imports[i].kind == 3;
        }
    }

    /* Use bump-allocated arrays; max 512 entries each (static would
       conflict with the per-module API statics, so we allocate). */
    exec_host_import *functions = calloc(function_count ? function_count : 1,
                                         sizeof(*functions));
    exec_global_import *globals = calloc(global_count ? global_count : 1,
                                          sizeof(*globals));
    exec_memory_import *memories = calloc(memory_count ? memory_count : 1,
                                           sizeof(*memories));
    exec_table_import *tables = calloc(table_count ? table_count : 1,
                                        sizeof(*tables));
    exec_tag_import *tags = calloc(tag_count ? tag_count : 1, sizeof(*tags));
    native_call_block *call_block = calloc(1, sizeof(*call_block));
    if (function_count)
        call_block->calls = calloc(function_count, sizeof(*call_block->calls));
    if ((function_count && (!functions || !call_block->calls)) ||
        (global_count && !globals) || (memory_count && !memories) ||
        (table_count && !tables) || (tag_count && !tags) || !call_block) {
        free(functions); free(globals); free(memories); free(tables); free(tags);
        if (call_block) { free(call_block->calls); free(call_block); }
        free(binary_imports);
        error->status = EXEC_ERROR_FORMAT;
        snprintf(error->message, sizeof(error->message),
                 "out of memory linking module");
        return EXEC_ERROR_FORMAT;
    }

    size_t nf = 0, ng = 0, nm = 0, nt = 0, ntag = 0;
    for (int i = 0; i < module->func_count; i++) if (module->funcs[i].is_import) {
        const wast_func *function = &module->funcs[i];
        native_linked_module *provider = native_registered_module(
            store, function->import_module);
        functions[nf].module = function->import_module;
        functions[nf].name = function->import_name;
        if (!provider && strcmp(function->import_module, "spectest") == 0 &&
            native_spectest_has_function(function->import_name)) {
            functions[nf].function = native_spectest_noop;
        } else if (provider) {
            uint32_t index = 0, type_index = 0;
            exec_status status = exec_find_export(
                provider->engine, function->import_name, &index, error);
            if (status != EXEC_OK) goto fail;
            status = exec_get_func_type_index(
                provider->engine, index, &type_index, error);
            if (status != EXEC_OK) goto fail;
            call_block->calls[nf].engine = provider->engine;
            call_block->calls[nf].func_idx = index;
            functions[nf].function = native_linked_call;
            functions[nf].host_data = &call_block->calls[nf];
            functions[nf].type_owner = provider->engine;
            functions[nf].type_index = type_index;
            functions[nf].has_wasm_type = 1;
        }
        nf++;
    }
    for (int i = 0; i < module->global_count; i++) if (module->globals[i].is_import) {
        const wast_global *global = &module->globals[i];
        native_linked_module *provider = native_registered_module(
            store, global->import_module);
        exec_global *value = (void *)0;
        if (!provider && strcmp(global->import_module, "spectest") == 0)
            value = native_spectest_global(store, global->import_name);
        else if (provider) {
            exec_status status = exec_find_export_global(
                provider->engine, global->import_name, &value, error);
            if (status != EXEC_OK) goto fail;
        }
        globals[ng++] = (exec_global_import){global->import_module,
                                             global->import_name, value};
    }
    for (int i = 0; i < module->memory_count; i++) if (module->memories[i].is_import) {
        const wast_memory *memory = &module->memories[i];
        native_linked_module *provider = native_registered_module(
            store, memory->import_module);
        exec_memory *value = (void *)0;
        if (!provider && strcmp(memory->import_module, "spectest") == 0 &&
            strcmp(memory->import_name, "memory") == 0)
            value = &store->spectest_memory;
        else if (provider) {
            exec_status status = exec_find_export_memory(
                provider->engine, memory->import_name, &value, error);
            if (status != EXEC_OK) goto fail;
        }
        memories[nm++] = (exec_memory_import){memory->import_module,
                                              memory->import_name, value};
    }
    for (int i = 0; i < module->table_count; i++) if (module->tables[i].is_import) {
        const wast_table *table = &module->tables[i];
        native_linked_module *provider = native_registered_module(
            store, table->import_module);
        exec_table *value = (void *)0;
        if (!provider && strcmp(table->import_module, "spectest") == 0 &&
            strcmp(table->import_name, "table") == 0)
            value = &store->spectest_table;
        else if (provider) {
            exec_status status = exec_find_export_table(
                provider->engine, table->import_name, &value, error);
            if (status != EXEC_OK) goto fail;
        }
        tables[nt++] = (exec_table_import){table->import_module,
                                           table->import_name, value};
    }
    for (int i = 0; i < module->tag_count; i++) if (module->tags[i].is_import) {
        const wast_tag *tag = &module->tags[i];
        native_linked_module *provider = native_registered_module(
            store, tag->import_module);
        exec_tag *value = (void *)0;
        if (provider) {
            exec_status status = exec_find_export_tag(
                provider->engine, tag->import_name, &value, error);
            if (status != EXEC_OK) goto fail;
        }
        tags[ntag++] = (exec_tag_import){tag->import_module,
                                         tag->import_name, value};
    }
    for (uint32_t i = 0; i < binary_import_count; i++) {
        import_request *request = &binary_imports[i];
        native_linked_module *provider = native_registered_module(
            store, request->module);
        if (request->kind == 0) {
            functions[nf].module = request->module;
            functions[nf].name = request->name;
            if (!provider && strcmp(request->module, "spectest") == 0 &&
                native_spectest_has_function(request->name)) {
                functions[nf].function = native_spectest_noop;
            } else if (provider) {
                uint32_t index = 0, type_index = 0;
                exec_status status = exec_find_export(
                    provider->engine, request->name, &index, error);
                if (status != EXEC_OK) goto fail;
                status = exec_get_func_type_index(
                    provider->engine, index, &type_index, error);
                if (status != EXEC_OK) goto fail;
                call_block->calls[nf].engine = provider->engine;
                call_block->calls[nf].func_idx = index;
                functions[nf].function = native_linked_call;
                functions[nf].host_data = &call_block->calls[nf];
                functions[nf].type_owner = provider->engine;
                functions[nf].type_index = type_index;
                functions[nf].has_wasm_type = 1;
            }
            nf++;
        } else if (request->kind == 1) {
            exec_table *value = provider ? (void *)0 :
                (strcmp(request->module, "spectest") == 0 &&
                 strcmp(request->name, "table") == 0 ?
                 &store->spectest_table : (void *)0);
            if (provider && exec_find_export_table(
                    provider->engine, request->name, &value, error) != EXEC_OK)
                goto fail;
            tables[nt++] = (exec_table_import){request->module,
                                               request->name, value};
        } else if (request->kind == 2) {
            exec_memory *value = provider ? (void *)0 :
                (strcmp(request->module, "spectest") == 0 &&
                 strcmp(request->name, "memory") == 0 ?
                 &store->spectest_memory : (void *)0);
            if (provider && exec_find_export_memory(
                    provider->engine, request->name, &value, error) != EXEC_OK)
                goto fail;
            memories[nm++] = (exec_memory_import){request->module,
                                                  request->name, value};
        } else if (request->kind == 3) {
            exec_global *value = provider ? (void *)0 :
                (strcmp(request->module, "spectest") == 0 ?
                 native_spectest_global(store, request->name) : (void *)0);
            if (provider && exec_find_export_global(
                    provider->engine, request->name, &value, error) != EXEC_OK)
                goto fail;
            globals[ng++] = (exec_global_import){request->module,
                                                  request->name, value};
        }
    }

    {
        exec_imports imports = {functions, nf, globals, ng,
                                memories, nm, tables, nt, tags, ntag};
        exec_status status = exec_load_with_imports(bytes, size, &imports,
                                                    engine_out, error);
        if (status != EXEC_OK) goto fail;
    }
    free(functions); free(globals); free(memories); free(tables); free(tags);
    free(binary_imports);
    if (function_count) {
        call_block->next = store->call_blocks;
        store->call_blocks = call_block;
    } else {
        free(call_block);
    }
    return EXEC_OK;

fail:
    free(functions); free(globals); free(memories); free(tables); free(tags);
    free(binary_imports);
    free(call_block->calls); free(call_block);
    return error->status;
}

static uint8_t *encode_group_module(const wast_group *group, size_t *size_out,
                                    char *error) {
    if (group->raw_module.kind == WAST_RAW_BINARY) {
        size_t allocation = group->raw_module.length ?
                            group->raw_module.length : 1;
        uint8_t *copy = malloc(allocation);
        if (!copy) {
            snprintf(error, 256, "out of memory copying binary module");
            return (void *)0;
        }
        if (group->raw_module.length)
            memcpy(copy, group->raw_module.bytes, group->raw_module.length);
        *size_out = group->raw_module.length;
        return copy;
    }
    if (group->raw_module.kind == WAST_RAW_QUOTE) {
        static const char prefix[] = "(module ";
        size_t length = group->raw_module.length;
        size_t source_length = sizeof(prefix) - 1 + length + 1;
        char *source = malloc(source_length);
        if (!source) {
            snprintf(error, 256, "out of memory copying quoted module");
            return (void *)0;
        }
        memcpy(source, prefix, sizeof(prefix) - 1);
        if (length)
            memcpy(source + sizeof(prefix) - 1,
                   group->raw_module.bytes, length);
        source[source_length - 1] = ')';
        uint8_t *wasm = (void *)0;
        if (waste_wat_compile(source, source_length, &wasm, size_out,
                              error, 256) != 0)
            wasm = (void *)0;
        free(source);
        return wasm;
    }
    return wast_encode_module(&group->module, size_out, error);
}

/* ---- Result buffer for waste_wast_run_script ---- */

typedef struct {
    uint8_t pass;
    char func[63];
    char error[192];
} wast_browser_result;

#define MAX_BROWSER_RESULTS 16384
static wast_browser_result g_browser_results[MAX_BROWSER_RESULTS];
static int g_browser_result_count = 0;
static int g_browser_result_passed = 0;

static void add_result(int pass, const char *func, const char *err) {
    if (g_browser_result_count >= MAX_BROWSER_RESULTS) return;
    wast_browser_result *r = &g_browser_results[g_browser_result_count++];
    r->pass = pass ? 1 : 0;
    /* Copy func name (null-terminated, max 62 chars + null) */
    int i = 0;
    if (func) for (; i < 62 && func[i]; i++) r->func[i] = func[i];
    r->func[i] = '\0';
    /* Copy error (null-terminated, max 191 chars + null) */
    i = 0;
    if (err) for (; i < 191 && err[i]; i++) r->error[i] = err[i];
    r->error[i] = '\0';
    if (pass) g_browser_result_passed++;
}

/* ---- Exported API ---- */

__attribute__((export_name("waste_wast_alloc")))
uint32_t waste_wast_alloc(uint32_t size) {
    return (uint32_t)(uintptr_t)malloc((size_t)size);
}

__attribute__((export_name("waste_wast_load_module")))
uint32_t waste_wast_load_module(uint32_t ptr, uint32_t size) {
    if (g_engine) { exec_free(g_engine); g_engine = (void *)0; }
    memset(&g_error, 0, sizeof(g_error));
    exec_status st = exec_load((const uint8_t *)(uintptr_t)ptr, (size_t)size,
                               &g_engine, &g_error);
    return (uint32_t)st;
}

__attribute__((export_name("waste_wast_reset")))
void waste_wast_reset(void) {
    for(uint32_t i=g_module_count;i>0;i--)exec_free(g_modules[i-1].engine);
    memset(g_modules,0,sizeof(g_modules));g_module_count=0;g_linked_func_count=0;g_engine=(void *)0;
}

__attribute__((export_name("waste_wast_load_linked_module")))
uint32_t waste_wast_load_linked_module(uint32_t ptr,uint32_t size,uint32_t id_ptr,uint32_t id_len) {
    uint32_t count=0,nf=0,ng=0,nm=0,nt=0,linked_start=g_linked_func_count;exec_imports imports;waste_exec_engine *engine=(void *)0;
    if(g_module_count>=LINK_MAX_MODULES||!scan_imports((const uint8_t *)(uintptr_t)ptr,size,g_import_requests,&count)){set_error("invalid module import section");return EXEC_ERROR_FORMAT;}
    for(uint32_t i=0;i<count;i++){import_request *req=&g_import_requests[i];linked_module *provider=registered_module(req->module);exec_status st;
        if(!provider&&req->kind==0&&strcmp(req->module,"spectest")==0){
            g_func_imports[nf]=(exec_host_import){req->module,req->name,spectest_noop,(void *)0,(void *)0,0,0};nf++;continue;
        }
        if(!provider){set_error("unresolved registered module import");return EXEC_ERROR_NOT_FOUND;}
        memset(&g_error,0,sizeof(g_error));
        if(req->kind==0){uint32_t idx,type_index;if(g_linked_func_count>=LINK_MAX_IMPORTS)return EXEC_ERROR_FORMAT;st=exec_find_export(provider->engine,req->name,&idx,&g_error);if(st!=EXEC_OK)return st;
            st=exec_get_func_type_index(provider->engine,idx,&type_index,&g_error);if(st!=EXEC_OK)return st;
            linked_func *f=&g_linked_funcs[g_linked_func_count++];f->engine=provider->engine;f->func_idx=idx;g_func_imports[nf]=(exec_host_import){req->module,req->name,linked_call,f,provider->engine,type_index,1};nf++;}
        else if(req->kind==1){exec_table *v;st=exec_find_export_table(provider->engine,req->name,&v,&g_error);if(st!=EXEC_OK)return st;g_table_imports[nt++]=(exec_table_import){req->module,req->name,v};}
        else if(req->kind==2){exec_memory *v;st=exec_find_export_memory(provider->engine,req->name,&v,&g_error);if(st!=EXEC_OK)return st;g_memory_imports[nm++]=(exec_memory_import){req->module,req->name,v};}
        else {exec_global *v;st=exec_find_export_global(provider->engine,req->name,&v,&g_error);if(st!=EXEC_OK)return st;g_global_imports[ng++]=(exec_global_import){req->module,req->name,v};}
    }
    imports=(exec_imports){g_func_imports,nf,g_global_imports,ng,g_memory_imports,nm,g_table_imports,nt};memset(&g_error,0,sizeof(g_error));
    exec_status st=exec_load_with_imports((const uint8_t *)(uintptr_t)ptr,size,&imports,&engine,&g_error);if(st!=EXEC_OK){g_linked_func_count=linked_start;return st;}
    linked_module *m=&g_modules[g_module_count];m->engine=engine;if(id_len>=WAST_MAX_EXPORT_NAME)id_len=WAST_MAX_EXPORT_NAME-1;
    memcpy(m->id,(const void *)(uintptr_t)id_ptr,id_len);m->id[id_len]='\0';g_current_module=g_module_count++;g_engine=engine;return EXEC_OK;
}

__attribute__((export_name("waste_wast_register_current")))
uint32_t waste_wast_register_current(uint32_t ptr,uint32_t len) {
    if(!g_module_count)return EXEC_ERROR_FORMAT;if(len>=WAST_MAX_EXPORT_NAME)len=WAST_MAX_EXPORT_NAME-1;
    linked_module *m=&g_modules[g_current_module];memcpy(m->registered,(const void *)(uintptr_t)ptr,len);m->registered[len]='\0';return EXEC_OK;
}

__attribute__((export_name("waste_wast_select_module")))
uint32_t waste_wast_select_module(uint32_t ptr,uint32_t len) {
    char id[WAST_MAX_EXPORT_NAME];if(len>=WAST_MAX_EXPORT_NAME)len=WAST_MAX_EXPORT_NAME-1;memcpy(id,(const void *)(uintptr_t)ptr,len);id[len]='\0';
    if(len==0&&g_module_count){g_current_module=g_module_count-1;g_engine=g_modules[g_current_module].engine;return EXEC_OK;}
    for(uint32_t i=g_module_count;i>0;i--)if(strcmp(g_modules[i-1].id,id)==0){g_current_module=i-1;g_engine=g_modules[i-1].engine;return EXEC_OK;}
    set_error("unknown module id");return EXEC_ERROR_NOT_FOUND;
}

__attribute__((export_name("waste_wast_find_export")))
int32_t waste_wast_find_export(uint32_t name_ptr, uint32_t name_len) {
    if (!g_engine) return -1;
    /* Build a null-terminated name (name_ptr must be in engine memory) */
    char name[WAST_MAX_EXPORT_NAME];
    if (name_len >= WAST_MAX_EXPORT_NAME) name_len = WAST_MAX_EXPORT_NAME - 1;
    memcpy(name, (const void *)(uintptr_t)name_ptr, name_len);
    name[name_len] = '\0';
    uint32_t func_idx = 0;
    memset(&g_error, 0, sizeof(g_error));
    exec_status st = exec_find_export(g_engine, name, &func_idx, &g_error);
    if (st != EXEC_OK) return -1;
    return (int32_t)func_idx;
}

/*
 * waste_wast_run: run a function.
 * args_ptr points to packed wasm_value structs in engine memory.
 * results_ptr points to output buffer.
 * Returns status (0 = ok).
 */
__attribute__((export_name("waste_wast_run")))
uint32_t waste_wast_run(uint32_t func_idx,
                         uint32_t args_ptr, uint32_t arg_count,
                         uint32_t results_ptr) {
    if (!g_engine) return (uint32_t)EXEC_ERROR_FORMAT;
    memset(&g_error, 0, sizeof(g_error));

    wasm_value *args    = (wasm_value *)(uintptr_t)args_ptr;
    wasm_value *results = (wasm_value *)(uintptr_t)results_ptr;
    int result_count = 0;

    exec_status st = exec_invoke(g_engine, func_idx,
                                 args, (int)arg_count,
                                 results, &result_count,
                                 &g_error);
    return (uint32_t)st;
}

/* ---- error access ---- */

__attribute__((export_name("waste_wast_error_ptr")))
uint32_t waste_wast_error_ptr(void) {
    return (uint32_t)(uintptr_t)g_error.message;
}

/* ---- flat value comparison helpers ---- */

/*
 * Flat value layout (33 bytes):
 *   [0]     : type byte (0=i32,1=i64,2=f32,3=f64,4=v128)
 *   [1..16] : 16 data bytes (little-endian for scalars, raw for v128)
 *   [17..32]: 16 nan_mode bytes
 */
#define FLAT_VALUE_SIZE 33

static int v128_matches_flat(const uint8_t *actual_bytes,
                              const uint8_t *exp_data,
                              const uint8_t *exp_nan_mode) {
    int i = 0;
    while (i < 16) {
        uint8_t mode = exp_nan_mode[i];
        if (mode == NAN_MATCH_EXACT) {
            if (actual_bytes[i] != exp_data[i]) return 0;
            i++;
        } else if (mode == NAN_MATCH_F32_CANON || mode == NAN_MATCH_F32_ARITH) {
            uint32_t ab;
            memcpy(&ab, &actual_bytes[i], 4);
            int is_nan = ((ab & 0x7F800000u) == 0x7F800000u) && (ab & 0x007FFFFFu);
            if (!is_nan) return 0;
            if (mode == NAN_MATCH_F32_CANON && (ab & 0x007FFFFFu) != 0x00400000u) return 0;
            i += 4;
        } else if (mode == NAN_MATCH_F64_CANON || mode == NAN_MATCH_F64_ARITH) {
            uint64_t ab;
            memcpy(&ab, &actual_bytes[i], 8);
            int is_nan = ((ab & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) &&
                         (ab & 0x000FFFFFFFFFFFFFULL);
            if (!is_nan) return 0;
            if (mode == NAN_MATCH_F64_CANON &&
                (ab & 0x000FFFFFFFFFFFFFULL) != 0x0008000000000000ULL) return 0;
            i += 8;
        } else {
            if (actual_bytes[i] != exp_data[i]) return 0;
            i++;
        }
    }
    return 1;
}

static void unpack_flat_value(const uint8_t *flat, wasm_value *v) {
    memset(v, 0, sizeof(*v));
    v->type = (wasm_valtype)flat[0];
    memcpy(v->nan_mode, flat + 17, 16);
    switch (v->type) {
        case WASM_VALTYPE_I32: memcpy(&v->i32,       flat + 1, 4);  break;
        case WASM_VALTYPE_I64: memcpy(&v->i64,       flat + 1, 8);  break;
        case WASM_VALTYPE_F32: memcpy(&v->f32,       flat + 1, 4);  break;
        case WASM_VALTYPE_F64: memcpy(&v->f64,       flat + 1, 8);  break;
        case WASM_VALTYPE_V128: memcpy(v->v128.bytes, flat + 1, 16); break;
        case WASM_VALTYPE_FUNCREF:
        case WASM_VALTYPE_EXTERNREF:
        case WASM_VALTYPE_FUNCREF_NONNULL:
        case WASM_VALTYPE_EXTERNREF_NONNULL:
        case WASM_VALTYPE_ANYREF:
        case WASM_VALTYPE_EQREF:
        case WASM_VALTYPE_I31REF:
        case WASM_VALTYPE_STRUCTREF:
        case WASM_VALTYPE_ARRAYREF:
        case WASM_VALTYPE_ANYREF_NONNULL:
        case WASM_VALTYPE_EQREF_NONNULL:
        case WASM_VALTYPE_I31REF_NONNULL:
        case WASM_VALTYPE_STRUCTREF_NONNULL:
        case WASM_VALTYPE_ARRAYREF_NONNULL: memcpy(&v->ref, flat + 1, 4); break;
    }
}

static int flat_value_matches(const wasm_value *actual, const uint8_t *flat_exp) {
    uint8_t exp_type = flat_exp[0];
    const uint8_t *exp_data     = flat_exp + 1;
    const uint8_t *exp_nan_mode = flat_exp + 17;
    if ((uint32_t)actual->type != (uint32_t)exp_type) return 0;
    switch (actual->type) {
        case WASM_VALTYPE_V128:
            return v128_matches_flat(actual->v128.bytes, exp_data, exp_nan_mode);
        case WASM_VALTYPE_I32: {
            int32_t ev; memcpy(&ev, exp_data, 4);
            return actual->i32 == ev;
        }
        case WASM_VALTYPE_I64: {
            int64_t ev; memcpy(&ev, exp_data, 8);
            return actual->i64 == ev;
        }
        case WASM_VALTYPE_F32: {
            uint32_t ab, eb;
            memcpy(&ab, &actual->f32, 4); memcpy(&eb, exp_data, 4);
            return ab == eb;
        }
        case WASM_VALTYPE_F64: {
            uint64_t ab, eb;
            memcpy(&ab, &actual->f64, 8); memcpy(&eb, exp_data, 8);
            return ab == eb;
        }
        case WASM_VALTYPE_FUNCREF:
        case WASM_VALTYPE_EXTERNREF:
        case WASM_VALTYPE_FUNCREF_NONNULL:
        case WASM_VALTYPE_EXTERNREF_NONNULL:
        case WASM_VALTYPE_ANYREF:
        case WASM_VALTYPE_EQREF:
        case WASM_VALTYPE_I31REF:
        case WASM_VALTYPE_STRUCTREF:
        case WASM_VALTYPE_ARRAYREF:
        case WASM_VALTYPE_ANYREF_NONNULL:
        case WASM_VALTYPE_EQREF_NONNULL:
        case WASM_VALTYPE_I31REF_NONNULL:
        case WASM_VALTYPE_STRUCTREF_NONNULL:
        case WASM_VALTYPE_ARRAYREF_NONNULL: {
            uint32_t expected; memcpy(&expected, exp_data, 4);
            return actual->ref == expected;
        }
    }
    return 0;
}

__attribute__((export_name("waste_wast_assert_global")))
uint32_t waste_wast_assert_global(uint32_t name_ptr, uint32_t name_len,
                                  uint32_t alts_ptr, uint32_t alt_count,
                                  uint32_t result_count) {
    char name[WAST_MAX_EXPORT_NAME];
    exec_global *global = (void *)0;
    if (!g_engine) { set_error("no module loaded"); return 0; }
    if (name_len >= WAST_MAX_EXPORT_NAME) name_len = WAST_MAX_EXPORT_NAME - 1;
    memcpy(name, (const void *)(uintptr_t)name_ptr, name_len);
    name[name_len] = '\0';
    memset(&g_error, 0, sizeof(g_error));
    if (exec_find_export_global(g_engine, name, &global, &g_error) != EXEC_OK)
        return 0;
    if (result_count != 1) { set_error("global action requires one result"); return 0; }
    const uint8_t *alts = (const uint8_t *)(uintptr_t)alts_ptr;
    for (uint32_t i = 0; i < alt_count; i++)
        if (flat_value_matches(&global->value, alts + i * FLAT_VALUE_SIZE))
            return 1;
    set_error("global result mismatch");
    return 0;
}

__attribute__((export_name("waste_wast_assert_trap")))
uint32_t waste_wast_assert_trap(uint32_t name_ptr, uint32_t name_len,
                                uint32_t args_ptr, uint32_t arg_count) {
    char name[WAST_MAX_EXPORT_NAME];
    uint32_t func_idx;
    wasm_value args[WAST_MAX_ARGS], results[WAST_MAX_RESULTS];
    int result_count = 0;
    if (!g_engine) { set_error("no module loaded"); return 0; }
    if (name_len >= WAST_MAX_EXPORT_NAME) name_len = WAST_MAX_EXPORT_NAME - 1;
    memcpy(name, (const void *)(uintptr_t)name_ptr, name_len); name[name_len] = '\0';
    memset(&g_error, 0, sizeof(g_error));
    if (exec_find_export(g_engine, name, &func_idx, &g_error) != EXEC_OK) return 0;
    uint32_t count = arg_count < WAST_MAX_ARGS ? arg_count : WAST_MAX_ARGS;
    const uint8_t *flat_args = (const uint8_t *)(uintptr_t)args_ptr;
    for (uint32_t i = 0; i < count; i++)
        unpack_flat_value(flat_args + i * FLAT_VALUE_SIZE, &args[i]);
    exec_status status = exec_invoke(g_engine, func_idx, args, (int)count,
                                     results, &result_count, &g_error);
    if (status == EXEC_ERROR_TRAP) { memset(&g_error, 0, sizeof(g_error)); return 1; }
    if (status == EXEC_OK) set_error("expected invocation to trap");
    return 0;
}

/*
 * waste_wast_assert_return: run one assert_return.
 *
 * name_ptr/name_len : exported function name (not null-terminated required)
 * args_ptr          : arg_count flat values (FLAT_VALUE_SIZE bytes each)
 * alts_ptr          : alt_count * result_count flat values
 * Returns 1 on pass, 0 on fail (error in g_error.message).
 */
__attribute__((export_name("waste_wast_assert_return")))
uint32_t waste_wast_assert_return(
        uint32_t name_ptr,  uint32_t name_len,
        uint32_t args_ptr,  uint32_t arg_count,
        uint32_t alts_ptr,  uint32_t alt_count,
        uint32_t result_count) {

    if (!g_engine) {
        memset(&g_error, 0, sizeof(g_error));
        const char *msg = "no module loaded";
        int i = 0;
        while (msg[i] && i < 255) { g_error.message[i] = msg[i]; i++; }
        g_error.message[i] = '\0';
        return 0;
    }

    /* Build null-terminated export name */
    char name[WAST_MAX_EXPORT_NAME];
    if (name_len >= WAST_MAX_EXPORT_NAME) name_len = WAST_MAX_EXPORT_NAME - 1;
    memcpy(name, (const void *)(uintptr_t)name_ptr, name_len);
    name[name_len] = '\0';

    /* Find export */
    uint32_t func_idx = 0;
    memset(&g_error, 0, sizeof(g_error));
    exec_status st = exec_find_export(g_engine, name, &func_idx, &g_error);
    if (st != EXEC_OK) return 0;

    /* Unpack args */
    wasm_value args[WAST_MAX_ARGS];
    uint32_t nargs = arg_count < WAST_MAX_ARGS ? arg_count : WAST_MAX_ARGS;
    const uint8_t *flat_args = (const uint8_t *)(uintptr_t)args_ptr;
    for (uint32_t i = 0; i < nargs; i++)
        unpack_flat_value(flat_args + i * FLAT_VALUE_SIZE, &args[i]);

    /* Invoke */
    wasm_value results[WAST_MAX_RESULTS];
    int nresults = 0;
    memset(&g_error, 0, sizeof(g_error));
    st = exec_invoke(g_engine, func_idx, args, (int)nargs,
                     results, &nresults, &g_error);
    if (st != EXEC_OK) return 0;

    /* No expected results -> pass */
    if (alt_count == 0 || result_count == 0) return 1;
    if ((uint32_t)nresults != result_count) {
        const char *msg = "result count mismatch";
        int i = 0;
        while (msg[i] && i < 255) { g_error.message[i] = msg[i]; i++; }
        g_error.message[i] = '\0';
        return 0;
    }

    /* Check each alternative */
    const uint8_t *flat_alts = (const uint8_t *)(uintptr_t)alts_ptr;
    uint32_t stride = result_count * FLAT_VALUE_SIZE;
    for (uint32_t a = 0; a < alt_count; a++) {
        const uint8_t *alt = flat_alts + a * stride;
        int all_ok = 1;
        for (uint32_t r = 0; r < result_count; r++) {
            if (!flat_value_matches(&results[r], alt + r * FLAT_VALUE_SIZE)) {
                all_ok = 0;
                break;
            }
        }
        if (all_ok) return 1;
    }

    /* Mismatch -- compose error */
    {
        const char *prefix = "result mismatch: ";
        int pos = 0;
        for (; *prefix && pos < 254; pos++, prefix++) g_error.message[pos] = *prefix;
        for (int k = 0; name[k] && pos < 255; k++, pos++) g_error.message[pos] = name[k];
        g_error.message[pos] = '\0';
    }
    return 0;
}

/* ---- waste_wast_run_script: run an entire WAST script ---- */

typedef struct {
    native_store store;
    wast_script **retained;
    uint32_t retained_count;
    uint32_t retained_capacity;
    wast_script *current_anonymous_script;
} browser_wast_context;

static uint32_t g_browser_command_line;

static void add_command_failure(const char *name, const char *message) {
    char located[256];
    snprintf(located, sizeof(located), "line %u: %s",
             g_browser_command_line, message ? message : "failure");
    add_result(0, name, located);
}

static void browser_forget_retained(browser_wast_context *context,
                                    wast_script *script) {
    if (!script) return;
    for (uint32_t i = 0; i < context->retained_count; i++) {
        if (context->retained[i] == script) {
            context->retained[i] = (void *)0;
            break;
        }
    }
    wast_script_free(script);
    free(script);
}

static void browser_release_superseded_anonymous(
        browser_wast_context *context) {
    if (!context->current_anonymous_script ||
        context->store.module_count == 0)
        return;
    native_linked_module *current =
        &context->store.modules[context->store.module_count - 1];
    if (current->id[0] || current->registered[0]) {
        context->current_anonymous_script = (void *)0;
        return;
    }
    int imports_table = 0;
    if (current->module) {
        for (int i = 0; i < current->module->table_count; i++)
            imports_table |= current->module->tables[i].is_import != 0;
    }
    if (imports_table) {
        /* Functions installed into an imported table outlive the instance
         * that supplied them.  Keep its decoded code as an unaddressable
         * store-owned orphan so those funcrefs remain valid. */
        if (!native_store_keep_orphan(&context->store, current->engine))
            return;
    } else {
        exec_free(current->engine);
    }
    memset(current, 0, sizeof(*current));
    context->store.module_count--;
    browser_forget_retained(context,
                            context->current_anonymous_script);
    context->current_anonymous_script = (void *)0;
}

static int browser_retain_script(browser_wast_context *context,
                                 wast_script *parsed) {
    if (parsed->group_count > 0 &&
        parsed->group_capacity != parsed->group_count) {
        wast_group *groups = realloc(
            parsed->groups,
            (size_t)parsed->group_count * sizeof(*parsed->groups));
        if (!groups) return 0;
        parsed->groups = groups;
        parsed->group_capacity = parsed->group_count;
    }
    if (context->retained_count == context->retained_capacity) {
        uint32_t capacity = context->retained_capacity ?
                            context->retained_capacity * 2u : 16u;
        wast_script **retained = realloc(
            context->retained, (size_t)capacity * sizeof(*retained));
        if (!retained) return 0;
        context->retained = retained;
        context->retained_capacity = capacity;
    }
    wast_script *holder = malloc(sizeof(*holder));
    if (!holder) return 0;
    *holder = *parsed;
    memset(parsed, 0, sizeof(*parsed));
    context->retained[context->retained_count++] = holder;
    return 1;
}

static const wast_module *browser_find_definition(
        const browser_wast_context *context, const char *id) {
    for (uint32_t script_index = context->retained_count;
         script_index > 0; script_index--) {
        const wast_script *script = context->retained[script_index - 1];
        if (!script) continue;
        for (int group_index = script->group_count; group_index > 0;
             group_index--) {
            const wast_module *module =
                &script->groups[group_index - 1].module;
            if (module->is_definition && strcmp(module->id, id) == 0)
                return module;
        }
    }
    return (void *)0;
}

static void browser_run_assertions(browser_wast_context *context,
                                   const wast_script *script,
                                   const wast_group *group) {
    for (int i = 0; i < group->assertion_count; i++) {
        const wast_assertion *assertion =
            &script->assertions[group->assertion_start + i];
        waste_exec_engine *selected =
            native_selected_engine(&context->store, assertion->module_id);
        exec_error error;
        memset(&error, 0, sizeof(error));
        exec_status status;
        if (!selected) {
            error.status = EXEC_ERROR_NOT_FOUND;
            snprintf(error.message, sizeof(error.message), "unknown module id");
            status = EXEC_ERROR_NOT_FOUND;
        } else {
            status = wast_run_assertion(selected, assertion, &error);
        }
        add_result(status == EXEC_OK, assertion->func_name,
                   status == EXEC_OK ? (void *)0 : error.message);
    }
}

static void browser_process_module(browser_wast_context *context,
                                   wast_script *script,
                                   wast_group *group) {
    const wast_module *load_module = &group->module;
    if (group->module.instance_of[0]) {
        load_module = browser_find_definition(context,
                                              group->module.instance_of);
        if (!load_module) {
            add_result(0, "(module)", "unknown module definition");
            return;
        }
    }

    if (group->has_module_assertion && group->has_validation_error) {
        int ok = group->module_assert_kind == WAST_ASSERT_INVALID ||
                 group->module_assert_kind == WAST_ASSERT_MALFORMED;
        add_result(ok, "(module)", ok ? (void *)0 : group->validation_error);
        return;
    }

    char encode_error[256] = {0};
    size_t binary_size = 0;
    wast_group encode_group = *group;
    encode_group.module = *load_module;
    uint8_t *binary = encode_group_module(&encode_group, &binary_size,
                                          encode_error);
    if (!binary) {
        if (group->has_module_assertion) {
            int ok = group->module_assert_kind != WAST_ASSERT_TRAP;
            add_result(ok, "(module)", ok ? (void *)0 : encode_error);
        } else {
            add_command_failure("(module)", encode_error);
        }
        return;
    }

    waste_exec_engine *engine = (void *)0;
    exec_error error;
    memset(&error, 0, sizeof(error));
    exec_status status = native_load_module(&context->store, load_module,
                                            binary, binary_size, &engine,
                                            &error);
    free(binary);
    if (group->has_module_assertion) {
        int ok = group->module_assert_kind == WAST_ASSERT_TRAP ?
                 status == EXEC_ERROR_TRAP : status != EXEC_OK;
        add_result(ok, "(module)", ok ? (void *)0 :
                   (status == EXEC_OK ? "module unexpectedly instantiated" :
                    error.message));
        if (engine && !native_store_keep_orphan(&context->store, engine)) {
            /* The worker's store is discarded at the end of this file.  If
             * bookkeeping allocation fails, leaking here is safer than
             * invalidating funcrefs already written into imported tables. */
        }
        return;
    }
    if (status != EXEC_OK) {
        /* A bare module (no assertion) that fails to load is not a test
         * failure — the native runner also prints the error to stderr and
         * continues.  Subsequent assertions that reference this module will
         * fail on their own. */
        return;
    }
    if (!native_store_add(&context->store, engine, &group->module,
                          load_module)) {
        exec_free(engine);
        add_command_failure("(module)",
                            "out of memory retaining module instance");
        return;
    }
    browser_run_assertions(context, script, group);
}

static int browser_process_command(wast_stream_command_kind kind,
                                   const char *bytes, size_t length,
                                   size_t offset, unsigned line,
                                   wast_script *parsed, void *opaque) {
    browser_wast_context *context = (browser_wast_context *)opaque;
    (void)bytes;
    (void)length;
    (void)offset;
    g_browser_command_line = line;
    if (parsed->error[0]) {
        add_result(0, "(parse)", parsed->error);
        return 0;
    }
    if (kind == WAST_STREAM_REGISTER) {
        if (parsed->group_count != 1) {
            add_result(0, "(register)", "invalid register command");
            return 0;
        }
        wast_module *registration = &parsed->groups[0].module;
        native_linked_module *target = native_selected_module(
            &context->store, registration->register_target);
        if (!target) {
            add_result(0, "(register)", "unknown module id");
            return 0;
        }
        snprintf(target->registered, sizeof(target->registered), "%s",
                 registration->register_name);
        return 0;
    }
    if ((kind == WAST_STREAM_ASSERTION || kind == WAST_STREAM_INVOKE) &&
        parsed->group_count == 1 &&
        !parsed->groups[0].has_module_assertion) {
        browser_run_assertions(context, parsed, &parsed->groups[0]);
        return 0;
    }
    if (parsed->group_count == 0) return 0;

    /* A module with neither an id nor a registration can only be selected as
     * the current module.  Once another ordinary module command arrives it is
     * no longer addressable by any later WAST command, so keeping its large
     * parser model and engine merely turns streaming into whole-file storage. */
    if (kind == WAST_STREAM_MODULE &&
        !parsed->groups[0].has_module_assertion &&
        !parsed->groups[0].module.is_definition)
        browser_release_superseded_anonymous(context);

    int must_retain = 0;
    for (int i = 0; i < parsed->group_count; i++)
        if (parsed->groups[i].module.is_definition ||
            (!parsed->groups[i].has_module_assertion &&
             kind == WAST_STREAM_MODULE))
            must_retain = 1;
    if (must_retain && !browser_retain_script(context, parsed)) {
        add_result(0, "(module)", "out of memory retaining WAST command");
        return 0;
    }
    wast_script *script = must_retain ?
        context->retained[context->retained_count - 1] : parsed;
    for (int i = 0; i < script->group_count; i++) {
        wast_group *group = &script->groups[i];
        if (group->module.is_definition) continue;
        browser_process_module(context, script, group);
    }
    if (must_retain && script->group_count == 1 &&
        !script->groups[0].has_module_assertion &&
        !script->groups[0].module.is_definition &&
        !script->groups[0].module.id[0] &&
        !script->groups[0].module.register_name[0] &&
        context->store.module_count > 0 &&
        context->store.modules[context->store.module_count - 1].module ==
            &script->groups[0].module)
        context->current_anonymous_script = script;
    return 0;
}

__attribute__((export_name("waste_wast_run_script")))
uint32_t waste_wast_run_script(uint32_t text_ptr, uint32_t text_len) {
    /* text_ptr is allocated from this same bump heap by waste_wast_alloc().
     * Resetting here made subsequent parser allocations overwrite the WAST
     * source while Flex was still reading it.  A dashboard worker creates a
     * fresh engine instance per file, so its heap is already pristine. */
    g_browser_result_count = 0;
    g_browser_result_passed = 0;
    g_browser_command_line = 1;
    /* Reset old per-module API state */
    memset(g_modules, 0, sizeof(g_modules));
    g_module_count = 0;
    g_linked_func_count = 0;
    g_engine = (void *)0;

    browser_wast_context context;
    memset(&context, 0, sizeof(context));
    native_store_init(&context.store);
    wast_stream stream;
    wast_stream_init(&stream, (const char *)(uintptr_t)text_ptr, text_len);
    for (;;) {
        int status = wast_stream_next(&stream, browser_process_command,
                                      &context);
        if (status == 0) break;
        if (status < 0) {
            add_result(0, "(parse)", stream.error);
            break;
        }
    }
    native_store_free(&context.store);
    for (uint32_t i = 0; i < context.retained_count; i++) {
        if (!context.retained[i]) continue;
        wast_script_free(context.retained[i]);
        free(context.retained[i]);
    }
    free(context.retained);
    return 0;
}

/* ---- Result accessor exports ---- */

__attribute__((export_name("waste_wast_results_ptr")))
uint32_t waste_wast_results_ptr(void) {
    return (uint32_t)(uintptr_t)g_browser_results;
}

__attribute__((export_name("waste_wast_results_total")))
uint32_t waste_wast_results_total(void) {
    return (uint32_t)g_browser_result_count;
}

__attribute__((export_name("waste_wast_command_line")))
uint32_t waste_wast_command_line(void) {
    return g_browser_command_line;
}

__attribute__((export_name("waste_wast_results_passed")))
uint32_t waste_wast_results_passed(void) {
    return (uint32_t)g_browser_result_passed;
}
