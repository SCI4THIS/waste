/* stdlib_2_native.c -- Native getenv, exit, heap allocator, strtod/strtof. */
#include "syscall_native.h"

/* Forward declarations for functions from category 1 files */
int snprintf(char *buf, size_t n, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap);
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
size_t strlen(const char *s);
const char *scan_float_end(const char *s);
double ldexp(double x, int exp);

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

static double parse_hex_float(const char *s, const char *end) {
    /* Parse 0xH.HHHpE format */
    int negative = 0;
    if (*s == '-') { negative = 1; s++; }
    else if (*s == '+') { s++; }

    /* Skip 0x prefix */
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;

    uint64_t mantissa = 0;
    int mantissa_bits = 0;
    int exponent = 0;
    int has_dot = 0;
    int frac_digits = 0;

    /* Parse hex digits before and after dot */
    while (s < end && (hex_digit_value(*s) >= 0 || *s == '.')) {
        if (*s == '.') { has_dot = 1; s++; continue; }
        int digit = hex_digit_value(*s);
        if (mantissa_bits < 60) {
            mantissa = (mantissa << 4) | (uint64_t)digit;
            mantissa_bits += 4;
        } else {
            if (!has_dot) exponent += 4;
        }
        if (has_dot) frac_digits++;
        s++;
    }
    (void)has_dot;

    exponent -= frac_digits * 4;

    /* Parse 'p' exponent */
    if (s < end && (*s == 'p' || *s == 'P')) {
        s++;
        int exp_sign = 1;
        if (s < end && *s == '-') { exp_sign = -1; s++; }
        else if (s < end && *s == '+') { s++; }
        int exp_val = 0;
        while (s < end && *s >= '0' && *s <= '9') {
            exp_val = exp_val * 10 + (*s - '0');
            s++;
        }
        exponent += exp_sign * exp_val;
    }

    if (mantissa == 0) return negative ? -0.0 : 0.0;

    double result = ldexp((double)mantissa, exponent);
    return negative ? -result : result;
}

static double parse_decimal_float(const char *s, const char *end) {
    int negative = 0;
    if (*s == '-') { negative = 1; s++; }
    else if (*s == '+') { s++; }

    /* Accumulate integer part */
    double result = 0.0;
    while (s < end && *s >= '0' && *s <= '9') {
        result = result * 10.0 + (double)(*s - '0');
        s++;
    }

    /* Fractional part */
    if (s < end && *s == '.') {
        s++;
        double place = 0.1;
        while (s < end && *s >= '0' && *s <= '9') {
            result += (double)(*s - '0') * place;
            place *= 0.1;
            s++;
        }
    }

    /* Exponent */
    if (s < end && (*s == 'e' || *s == 'E')) {
        s++;
        int exp_sign = 1;
        if (s < end && *s == '-') { exp_sign = -1; s++; }
        else if (s < end && *s == '+') { s++; }
        int exp_val = 0;
        while (s < end && *s >= '0' && *s <= '9') {
            exp_val = exp_val * 10 + (*s - '0');
            s++;
        }
        /* Apply exponent via repeated multiply/divide */
        int e = exp_val * exp_sign;
        double factor = 10.0;
        if (e < 0) { factor = 0.1; e = -e; }
        while (e > 0) {
            if (e & 1) result *= factor;
            factor *= factor;
            e >>= 1;
        }
    }

    return negative ? -result : result;
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

    /* Detect hex float */
    const char *digits = (s[0] == '+' || s[0] == '-') ? s + 1 : s;
    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
        return parse_hex_float(s, end);
    return parse_decimal_float(s, end);
}

float strtof(const char *s, char **endptr) {
    return (float)strtod(s, endptr);
}
