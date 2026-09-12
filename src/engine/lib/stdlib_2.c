/* Wasm stdlib implementation (category 2: emulated in-browser) */
#include <stddef.h>
#include <stdint.h>

/* Declarations from freestanding_lib.c used by strtod/strtof */
const char *scan_float_end(const char *s);

__attribute__((import_module("waste_host"), import_name("strtod")))
extern double waste_host_strtod(const char *text, size_t length);

__attribute__((import_module("waste_host"), import_name("strtof")))
extern float waste_host_strtof(const char *text, size_t length);

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

_Noreturn void exit(int status) { (void)status; __builtin_trap(); }

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

/* Forward declaration for use by realloc */
void *memcpy(void *dst, const void *src, size_t n);

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
