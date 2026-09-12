/* stdlib_2_native.c -- Native getenv, exit, heap allocator, strtod/strtof. */
#include "syscall_native.h"

/* Forward declarations for functions from category 1 files */
int snprintf(char *buf, size_t n, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap);
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
size_t strlen(const char *s);
const char *scan_float_end(const char *s);

/* Forward declarations */
void *malloc(size_t size);
void  free(void *ptr);

/* ---- Stubs ---- */

char *getenv(const char *name) { (void)name; return (void *)0; }

_Noreturn void exit(int status) {
    sys_exit_group(status);
}

/* ---- Memory allocator (mmap-backed) ---- */

/* We use mmap to get memory from the OS.  The allocator algorithm is the
 * same free-list + bump design used by browser_wast.c for Wasm. */

typedef struct heap_block {
    size_t size;
    struct heap_block *next;
    uint32_t is_free;
    uint32_t reserved;
    uint64_t alignment_padding;
} heap_block;

static heap_block *heap_blocks;
static uintptr_t heap_cursor;
static uintptr_t heap_limit;

static size_t heap_align(size_t size) {
    return (size + 15u) & ~(size_t)15u;
}

static int heap_ensure(size_t amount) {
    /* Ensure at least 'amount' bytes are available from heap_cursor.
     * If the current region is insufficient, mmap a new one. */
    if (heap_cursor + amount <= heap_limit) return 1;
    /* Round up to page boundary (4096) */
    size_t needed = (amount + 4095u) & ~(size_t)4095u;
    /* Request at least 256KB at a time to reduce syscalls */
    if (needed < 262144u) needed = 262144u;
    void *hint = heap_limit ? (void *)heap_limit : (void *)0;
    void *p = sys_mmap(hint, needed,
                       0x3 /* PROT_READ | PROT_WRITE */,
                       0x22 /* MAP_PRIVATE | MAP_ANONYMOUS */,
                       -1, 0);
    if (p == (void *)-1) return 0;
    if ((uintptr_t)p == heap_limit) {
        /* Contiguous extension: just advance the limit */
        heap_limit += needed;
    } else {
        /* Non-contiguous region: move cursor to the new mapping.
         * Existing free-list blocks in the old region remain valid
         * and reachable; new bump allocations come from here. */
        heap_cursor = (uintptr_t)p;
        heap_limit = (uintptr_t)p + needed;
    }
    return 1;
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
    rest->alignment_padding = 0;
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
    /* Ensure enough space: alignment padding + header + payload */
    size_t total_needed = 16u + header_size + size;
    if (!heap_ensure(total_needed)) return (void *)0;
    uintptr_t start = (heap_cursor + 15u) & ~(uintptr_t)15u;
    uintptr_t limit = start + header_size + size;
    heap_block *block = (heap_block *)start;
    block->size = size;
    block->next = (void *)0;
    block->is_free = 0;
    block->reserved = 0;
    block->alignment_padding = 0;
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

static int heap_blocks_adjacent(heap_block *a, heap_block *b) {
    /* block payload starts at (a + 1), extends a->size bytes */
    return (unsigned char *)(a + 1) + a->size == (unsigned char *)b;
}

void free(void *ptr) {
    if (!ptr) return;
    heap_block *block = (heap_block *)ptr - 1;
    block->is_free = 1;
    /* Coalesce with following blocks only if physically adjacent */
    while (block->next && block->next->is_free &&
           heap_blocks_adjacent(block, block->next)) {
        block->size += sizeof(heap_block) + block->next->size;
        block->next = block->next->next;
    }
    /* Coalesce with preceding block only if physically adjacent */
    heap_block *previous = (void *)0;
    for (heap_block *at = heap_blocks; at && at != block; at = at->next)
        previous = at;
    if (previous && previous->is_free &&
        heap_blocks_adjacent(previous, block)) {
        previous->size += sizeof(heap_block) + block->size;
        previous->next = block->next;
    }
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
        heap_blocks_adjacent(block, block->next) &&
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

/* ---- strtod / strtof (pure C implementation) ---- */

static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* The lexer accepts literals substantially longer than a machine integer.
 * Keep the significand exact until the one and only IEEE-754 rounding step.
 * 640 limbs cover the lexer's 4096-byte numeric scratch buffer. */
#define FLOAT_BIG_LIMBS 640
typedef struct {
    uint32_t limb[FLOAT_BIG_LIMBS];
    int length;
    int overflow;
} float_bigint;

static void big_normalize(float_bigint *value) {
    while (value->length && value->limb[value->length - 1] == 0)
        value->length--;
}

static void big_set_one(float_bigint *value) {
    memset(value, 0, sizeof(*value));
    value->limb[0] = 1;
    value->length = 1;
}

static void big_multiply_small(float_bigint *value, uint32_t factor) {
    uint64_t carry = 0;
    for (int i = 0; i < value->length; i++) {
        uint64_t product = (uint64_t)value->limb[i] * factor + carry;
        value->limb[i] = (uint32_t)product;
        carry = product >> 32;
    }
    if (carry) {
        if (value->length == FLOAT_BIG_LIMBS) {
            value->overflow = 1;
            return;
        }
        value->limb[value->length++] = (uint32_t)carry;
    }
}

static void big_add_small(float_bigint *value, uint32_t addend) {
    uint64_t carry = addend;
    int i = 0;
    while (carry && i < value->length) {
        carry += value->limb[i];
        value->limb[i++] = (uint32_t)carry;
        carry >>= 32;
    }
    if (carry) {
        if (value->length == FLOAT_BIG_LIMBS) value->overflow = 1;
        else value->limb[value->length++] = (uint32_t)carry;
    }
}

static int big_bit_length(const float_bigint *value) {
    if (!value->length) return 0;
    return (value->length - 1) * 32 +
           32 - __builtin_clz(value->limb[value->length - 1]);
}

static uint32_t big_shifted_limb(const float_bigint *value, int index,
                                 int word_shift, int bit_shift) {
    int source = index - word_shift;
    uint32_t result = 0;
    if (source >= 0 && source < value->length)
        result = value->limb[source] << bit_shift;
    if (bit_shift && source > 0 && source - 1 < value->length)
        result |= value->limb[source - 1] >> (32 - bit_shift);
    return result;
}

/* Compare left with right * 2^shift.  shift is non-negative. */
static int big_compare_shift(const float_bigint *left,
                             const float_bigint *right, int shift) {
    int word_shift = shift / 32;
    int bit_shift = shift % 32;
    int right_length = right->length + word_shift;
    if (bit_shift && right->length &&
        (right->limb[right->length - 1] >> (32 - bit_shift)))
        right_length++;
    if (left->length != right_length)
        return left->length < right_length ? -1 : 1;
    for (int i = left->length - 1; i >= 0; i--) {
        uint32_t r = big_shifted_limb(right, i, word_shift, bit_shift);
        if (left->limb[i] != r) return left->limb[i] < r ? -1 : 1;
    }
    return 0;
}

static int big_compare(const float_bigint *left,
                       const float_bigint *right) {
    return big_compare_shift(left, right, 0);
}

/* Subtract right * 2^shift.  The caller has established left >= right. */
static void big_subtract_shift(float_bigint *left,
                               const float_bigint *right, int shift) {
    int word_shift = shift / 32;
    int bit_shift = shift % 32;
    uint64_t borrow = 0;
    for (int i = 0; i < left->length; i++) {
        uint64_t subtrahend =
            (uint64_t)big_shifted_limb(right, i, word_shift, bit_shift) +
            borrow;
        uint64_t original = left->limb[i];
        left->limb[i] = (uint32_t)(original - subtrahend);
        borrow = original < subtrahend;
    }
    big_normalize(left);
}

static int big_shift_left(float_bigint *output,
                          const float_bigint *input, int shift) {
    int word_shift = shift / 32;
    int bit_shift = shift % 32;
    memset(output, 0, sizeof(*output));
    if (!input->length) return 1;
    int output_length = input->length + word_shift + (bit_shift != 0);
    if (output_length > FLOAT_BIG_LIMBS) return 0;
    for (int i = 0; i < output_length; i++)
        output->limb[i] = big_shifted_limb(input, i, word_shift, bit_shift);
    output->length = output_length;
    big_normalize(output);
    return 1;
}

static int ratio_at_least_power_two(const float_bigint *numerator,
                                    const float_bigint *denominator,
                                    int exponent) {
    if (exponent >= 0)
        return big_compare_shift(numerator, denominator, exponent) >= 0;
    return big_compare_shift(denominator, numerator, -exponent) <= 0;
}

static int ratio_floor_log2(const float_bigint *numerator,
                            const float_bigint *denominator) {
    int exponent = big_bit_length(numerator) - big_bit_length(denominator);
    if (!ratio_at_least_power_two(numerator, denominator, exponent))
        exponent--;
    return exponent;
}

/* Round numerator * 2^shift / denominator to uint64_t, ties to even. */
static uint64_t round_scaled_ratio(const float_bigint *numerator,
                                   const float_bigint *denominator,
                                   int shift) {
    float_bigint remainder;
    float_bigint divisor;
    if (shift >= 0) {
        if (!big_shift_left(&remainder, numerator, shift)) return UINT64_MAX;
        divisor = *denominator;
    } else {
        remainder = *numerator;
        if (!big_shift_left(&divisor, denominator, -shift)) return 0;
    }
    int quotient_bits = big_bit_length(&remainder) - big_bit_length(&divisor);
    uint64_t quotient = 0;
    for (int bit = quotient_bits; bit >= 0; bit--) {
        if (bit >= 64) return UINT64_MAX;
        if (big_compare_shift(&remainder, &divisor, bit) >= 0) {
            big_subtract_shift(&remainder, &divisor, bit);
            quotient |= UINT64_C(1) << bit;
        }
    }
    float_bigint twice_remainder = remainder;
    big_multiply_small(&twice_remainder, 2);
    int halfway = big_compare(&twice_remainder, &divisor);
    if (halfway > 0 || (halfway == 0 && (quotient & 1))) quotient++;
    return quotient;
}

/* Return an IEEE bit pattern.  precision includes the implicit leading bit. */
static uint64_t ratio_to_ieee(const float_bigint *numerator,
                              const float_bigint *denominator,
                              int binary_exponent, int negative,
                              int precision, int minimum_exponent,
                              int maximum_exponent, int exponent_bits) {
    uint64_t sign = (uint64_t)negative << (precision - 1 + exponent_bits);
    if (!numerator->length) return sign;
    int exponent = ratio_floor_log2(numerator, denominator) + binary_exponent;
    uint64_t infinity = ((UINT64_C(1) << exponent_bits) - 1)
                        << (precision - 1);
    if (exponent > maximum_exponent) return sign | infinity;

    int quantum_exponent = exponent >= minimum_exponent ?
                           exponent - (precision - 1) :
                           minimum_exponent - (precision - 1);
    int shift = binary_exponent - quantum_exponent;
    uint64_t significand = round_scaled_ratio(numerator, denominator, shift);
    uint64_t implicit_bit = UINT64_C(1) << (precision - 1);
    if (significand >= (implicit_bit << 1)) {
        significand >>= 1;
        exponent++;
    }
    if (exponent > maximum_exponent) return sign | infinity;
    if (exponent < minimum_exponent) {
        if (significand >= implicit_bit)
            return sign | (UINT64_C(1) << (precision - 1));
        return sign | significand;
    }
    uint64_t exponent_field =
        (uint64_t)(exponent + maximum_exponent) << (precision - 1);
    return sign | exponent_field | (significand - implicit_bit);
}

static int parse_bounded_exponent(const char **cursor, const char *end) {
    int negative = 0;
    int value = 0;
    if (*cursor < end && (**cursor == '+' || **cursor == '-')) {
        negative = **cursor == '-';
        (*cursor)++;
    }
    while (*cursor < end && **cursor >= '0' && **cursor <= '9') {
        if (value < 100000) value = value * 10 + (**cursor - '0');
        (*cursor)++;
    }
    return negative ? -value : value;
}

static uint64_t parse_finite_bits(const char *s, const char *end,
                                  int precision, int minimum_exponent,
                                  int maximum_exponent, int exponent_bits) {
    int negative = 0;
    if (*s == '-' || *s == '+') { negative = *s == '-'; s++; }
    float_bigint numerator = {{0}, 0, 0};
    float_bigint denominator;
    big_set_one(&denominator);

    if (s + 2 <= end && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        int fractional_digits = 0;
        int after_point = 0;
        while (s < end && *s != 'p' && *s != 'P') {
            if (*s == '.') { after_point = 1; s++; continue; }
            int digit = hex_digit_value(*s++);
            if (digit < 0) break;
            big_multiply_small(&numerator, 16);
            big_add_small(&numerator, (uint32_t)digit);
            if (after_point) fractional_digits++;
        }
        int exponent = -4 * fractional_digits;
        if (s < end && (*s == 'p' || *s == 'P')) {
            s++;
            exponent += parse_bounded_exponent(&s, end);
        }
        if (!numerator.length)
            return (uint64_t)negative <<
                   (precision - 1 + exponent_bits);
        int magnitude = big_bit_length(&numerator) - 1 + exponent;
        if (magnitude > maximum_exponent + 2)
            exponent = maximum_exponent + 2;
        else if (magnitude < minimum_exponent - precision - 2)
            return (uint64_t)negative <<
                   (precision - 1 + exponent_bits);
        return ratio_to_ieee(&numerator, &denominator, exponent, negative,
                             precision, minimum_exponent, maximum_exponent,
                             exponent_bits);
    }

    int fractional_digits = 0;
    int after_point = 0;
    int significant_digits = 0;
    int seen_nonzero = 0;
    while (s < end && *s != 'e' && *s != 'E') {
        if (*s == '.') { after_point = 1; s++; continue; }
        int digit = *s++ - '0';
        if (digit < 0 || digit > 9) break;
        big_multiply_small(&numerator, 10);
        big_add_small(&numerator, (uint32_t)digit);
        if (after_point) fractional_digits++;
        if (digit || seen_nonzero) { seen_nonzero = 1; significant_digits++; }
    }
    int decimal_exponent = -fractional_digits;
    if (s < end && (*s == 'e' || *s == 'E')) {
        s++;
        decimal_exponent += parse_bounded_exponent(&s, end);
    }
    if (!numerator.length)
        return (uint64_t)negative << (precision - 1 + exponent_bits);
    int decimal_magnitude = significant_digits + decimal_exponent - 1;
    if (decimal_magnitude > 400) {
        uint64_t infinity = ((UINT64_C(1) << exponent_bits) - 1)
                            << (precision - 1);
        return ((uint64_t)negative << (precision - 1 + exponent_bits)) |
               infinity;
    }
    if (decimal_magnitude < -500)
        return (uint64_t)negative << (precision - 1 + exponent_bits);

    int binary_exponent = decimal_exponent;
    if (decimal_exponent >= 0) {
        for (int i = 0; i < decimal_exponent; i++)
            big_multiply_small(&numerator, 5);
    } else {
        for (int i = 0; i < -decimal_exponent; i++)
            big_multiply_small(&denominator, 5);
    }
    return ratio_to_ieee(&numerator, &denominator, binary_exponent, negative,
                         precision, minimum_exponent, maximum_exponent,
                         exponent_bits);
}

static double bits_to_double(uint64_t bits) {
    double result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static float bits_to_float(uint32_t bits) {
    float result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

double strtod(const char *s, char **endptr) {
    const char *start = s;
    const char *number_start;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    number_start = s;
    int negative = 0;
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

    /* Reset s to number_start for full parsing */
    s = number_start;
    const char *end = scan_float_end(s[0] == '+' || s[0] == '-' ? s + 1 : s);
    if (end == s || (end == s + 1 && (s[0] == '+' || s[0] == '-'))) {
        if (endptr) *endptr = (char *)start;
        return 0.0;
    }
    if (endptr) *endptr = (char *)end;

    return bits_to_double(parse_finite_bits(s, end, 53, -1022, 1023, 11));
}

float strtof(const char *s, char **endptr) {
    const char *start = s;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    const char *number_start = s;
    int negative = 0;
    if (*s == '-') { negative = 1; s++; }
    else if (*s == '+') { s++; }
    if ((s[0] == 'i' || s[0] == 'I') && (s[1] == 'n' || s[1] == 'N') &&
        (s[2] == 'f' || s[2] == 'F')) {
        s += 3;
        if ((s[0] == 'i' || s[0] == 'I') &&
            (s[1] == 'n' || s[1] == 'N') &&
            (s[2] == 'i' || s[2] == 'I') &&
            (s[3] == 't' || s[3] == 'T') &&
            (s[4] == 'y' || s[4] == 'Y'))
            s += 5;
        if (endptr) *endptr = (char *)s;
        return negative ? -__builtin_inff() : __builtin_inff();
    }
    if ((s[0] == 'n' || s[0] == 'N') && (s[1] == 'a' || s[1] == 'A') &&
        (s[2] == 'n' || s[2] == 'N')) {
        s += 3;
        if (*s == '(') {
            while (*s && *s != ')') s++;
            if (*s == ')') s++;
        }
        if (endptr) *endptr = (char *)s;
        return __builtin_nanf("");
    }
    s = number_start;
    const char *end = scan_float_end(s[0] == '+' || s[0] == '-' ? s + 1 : s);
    if (end == s || (end == s + 1 && (s[0] == '+' || s[0] == '-'))) {
        if (endptr) *endptr = (char *)start;
        return 0.0f;
    }
    if (endptr) *endptr = (char *)end;
    return bits_to_float((uint32_t)parse_finite_bits(
        s, end, 24, -126, 127, 8));
}
