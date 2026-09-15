#include "wat/literal.h"

#include <math.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

static int number_digit(unsigned char c, int base) {
    int digit = c >= '0' && c <= '9' ? c - '0' :
                c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    return digit >= 0 && digit < base ? digit : -1;
}

static int digit_sequence(const char *source, size_t length, size_t *at,
                          int base, int required) {
    int count = 0;
    while (*at < length && number_digit((unsigned char)source[*at], base) >= 0) {
        (*at)++;
        count++;
        if (*at < length && source[*at] == '_') {
            if (*at + 1 >= length ||
                number_digit((unsigned char)source[*at + 1], base) < 0)
                return 0;
            (*at)++;
        }
    }
    return !required || count > 0;
}

const char *wast_literal_strip_underscores(wat_context *context,
                                           const char *source) {
    size_t output = 0;
    for (size_t input = 0; source[input] != '\0'; input++) {
        if (source[input] == '_') continue;
        if (output + 1 >= sizeof(context->literal_scratch)) {
            context->lex.offset_overflow = 1;
            break;
        }
        context->literal_scratch[output++] = source[input];
    }
    context->literal_scratch[output] = '\0';
    return context->literal_scratch;
}

uint64_t wast_literal_parse_hex_payload(const char *source) {
    uint64_t value = 0;
    while (*source) {
        if (*source != '_') {
            int digit = *source >= '0' && *source <= '9' ? *source - '0' :
                *source >= 'a' && *source <= 'f' ? *source - 'a' + 10 :
                *source >= 'A' && *source <= 'F' ? *source - 'A' + 10 : -1;
            if (digit < 0) break;
            value = (value << 4) | (uint64_t)digit;
        }
        source++;
    }
    return value;
}

int wast_literal_integer_is_valid(const char *source, size_t length, int bits) {
    size_t at = 0;
    int negative = 0;
    if (at < length && (source[at] == '+' || source[at] == '-'))
        negative = source[at++] == '-';
    int base = 10;
    if (at + 1 < length && source[at] == '0' &&
        (source[at + 1] == 'x' || source[at + 1] == 'X')) {
        base = 16;
        at += 2;
    }
    size_t digits = at;
    if (!digit_sequence(source, length, &at, base, 1) || at != length)
        return 0;
    uint64_t limit = bits == 32 ?
        (negative ? UINT64_C(0x80000000) : UINT64_C(0xffffffff)) :
        (negative ? UINT64_C(0x8000000000000000) : UINT64_MAX);
    uint64_t value = 0;
    for (at = digits; at < length; at++) {
        if (source[at] == '_') continue;
        unsigned digit = (unsigned)number_digit((unsigned char)source[at], base);
        if (value > (limit - digit) / (unsigned)base) return 0;
        value = value * (unsigned)base + digit;
    }
    return 1;
}

static int nan_is_valid(const char *source, size_t length, int bits) {
    size_t at = 0;
    if (at < length && (source[at] == '+' || source[at] == '-')) at++;
    static const char prefix[] = "nan:0x";
    if (length - at < sizeof(prefix) - 1 ||
        memcmp(source + at, prefix, sizeof(prefix) - 1) != 0)
        return 0;
    at += sizeof(prefix) - 1;
    size_t digits = at;
    if (!digit_sequence(source, length, &at, 16, 1) || at != length)
        return 0;
    uint64_t limit = bits == 32 ? UINT64_C(0x7fffff) :
                                  UINT64_C(0xfffffffffffff);
    uint64_t value = 0;
    for (at = digits; at < length; at++) {
        if (source[at] == '_') continue;
        unsigned digit =
            (unsigned)number_digit((unsigned char)source[at], 16);
        if (value > (limit - digit) / 16u) return 0;
        value = value * 16u + digit;
    }
    return value != 0;
}

static int float_syntax_is_valid(const char *source, size_t length) {
    size_t at = 0;
    if (at < length && (source[at] == '+' || source[at] == '-')) at++;
    int base = 10;
    char exponent = 'e';
    if (at + 1 < length && source[at] == '0' &&
        (source[at + 1] == 'x' || source[at + 1] == 'X')) {
        base = 16;
        exponent = 'p';
        at += 2;
    }
    if (!digit_sequence(source, length, &at, base, 1)) return 0;
    if (at < length && source[at] == '.') {
        at++;
        if (!digit_sequence(source, length, &at, base, 0)) return 0;
    }
    if (at < length && (source[at] | 0x20) == exponent) {
        at++;
        if (at < length && (source[at] == '+' || source[at] == '-')) at++;
        if (!digit_sequence(source, length, &at, 10, 1)) return 0;
    }
    return at == length;
}

int wast_literal_float_is_valid(wat_context *context, const char *source,
                                size_t length, int bits) {
    if ((length == 3 && memcmp(source, "inf", 3) == 0) ||
        (length == 4 && (source[0] == '+' || source[0] == '-') &&
         memcmp(source + 1, "inf", 3) == 0) ||
        (length == 3 && memcmp(source, "nan", 3) == 0) ||
        (length == 4 && (source[0] == '+' || source[0] == '-') &&
         memcmp(source + 1, "nan", 3) == 0))
        return 1;
    if ((length >= 6 && memcmp(source, "nan:0x", 6) == 0) ||
        (length >= 7 && (source[0] == '+' || source[0] == '-') &&
         memcmp(source + 1, "nan:0x", 6) == 0))
        return nan_is_valid(source, length, bits);
    if (!float_syntax_is_valid(source, length) ||
        length + 1 > WAST_LITERAL_SCRATCH_SIZE)
        return 0;
    const char *clean = wast_literal_strip_underscores(context, source);
    char *end = NULL;
    if (bits == 32) {
        float value = strtof(clean, &end);
        return end && *end == '\0' && !isinf(value);
    }
    double value = strtod(clean, &end);
    return end && *end == '\0' && !isinf(value);
}

double wast_literal_parse_float(wat_context *context, const char *source,
                                int bits) {
    const char *clean = wast_literal_strip_underscores(context, source);
    return bits == 32 ? (double)strtof(clean, NULL) : strtod(clean, NULL);
}
