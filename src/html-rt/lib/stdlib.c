/* Browser interpreter allocation and JS-hosted float conversion. */

/* ---- Engine build: strtod/strtof via JS host, heap allocator ---- */
#include <stddef.h>
#include <stdint.h>

void *realloc(void *, size_t);

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

#ifndef WASTE_ALLOCATOR_ONLY
_Noreturn void exit(int status) { (void)status; __builtin_trap(); }
_Noreturn void _exit(int status) { exit(status); }
_Noreturn void abort(void) { __builtin_trap(); }
#endif

/* ---- Freestanding heap allocator ---- */
extern unsigned char __heap_base;
static uintptr_t heap_cursor;

typedef struct heap_block {
    size_t size;
    struct heap_block *next, *previous;
    uint32_t is_free;
} heap_block;

/* Headers and the smallest payload both preserve 16-byte alignment. Free-list
 * links occupy only free payloads, so live allocations carry no search links. */
_Static_assert(sizeof(heap_block) % 16u == 0, "aligned heap header");
typedef struct { heap_block *next, *previous; } heap_free_links;
static heap_block *heap_last;
static heap_block *heap_bins[sizeof(size_t) * 8u];

static size_t heap_align(size_t size) {
    return (size + 15u) & ~(size_t)15u;
}

static unsigned heap_bin(size_t size) {
    unsigned bin = 0;
    while (size >>= 1u) ++bin;
    return bin;
}

static heap_free_links *heap_links(heap_block *block) {
    return (heap_free_links *)(block + 1);
}

static void heap_insert_free(heap_block *block) {
    unsigned bin = heap_bin(block->size);
    heap_free_links *links = heap_links(block);
    links->previous = (void *)0;
    links->next = heap_bins[bin];
    if (links->next) heap_links(links->next)->previous = block;
    heap_bins[bin] = block;
}

static void heap_remove_free(heap_block *block) {
    heap_free_links *links = heap_links(block);
    if (links->previous) heap_links(links->previous)->next = links->next;
    else heap_bins[heap_bin(block->size)] = links->next;
    if (links->next) heap_links(links->next)->previous = links->previous;
}

static int heap_ensure(uintptr_t limit) {
    uintptr_t memory_size =
        (uintptr_t)__builtin_wasm_memory_size(0) * 65536u;
    if (limit <= memory_size) return 1;
    uintptr_t missing = limit - memory_size;
    uint32_t pages = (uint32_t)((missing + 65535u) / 65536u);
    return __builtin_wasm_memory_grow(0, pages) != (size_t)-1;
}

/* The caller removes the next free block from its size bin before joining. */
static void heap_join_next(heap_block *block) {
    heap_block *next = block->next;
    block->size += sizeof(heap_block) + next->size;
    block->next = next->next;
    if (block->next) block->next->previous = block;
    else heap_last = block;
}

static void heap_split(heap_block *block, size_t size) {
    if (block->size - size < sizeof(heap_block) + 16u) return;
    heap_block *rest = (heap_block *)((unsigned char *)(block + 1) + size);
    rest->size = block->size - size - sizeof(heap_block);
    rest->next = block->next;
    rest->previous = block;
    rest->is_free = 1;
    if (rest->next) rest->next->previous = rest;
    else heap_last = rest;
    block->size = size;
    block->next = rest;
    if (rest->next && rest->next->is_free) {
        heap_remove_free(rest->next);
        heap_join_next(rest);
    }
    heap_insert_free(rest);
}

void *malloc(size_t size) {
    if (size == 0) size = 1;
    if (size > (size_t)-1 - 15u) return (void *)0;
    size = heap_align(size);
    for (unsigned bin = heap_bin(size); bin < sizeof(heap_bins) / sizeof(*heap_bins); ++bin) {
        for (heap_block *block = heap_bins[bin]; block; block = heap_links(block)->next) {
            if (block->size < size) continue;
            heap_remove_free(block);
            heap_split(block, size);
            block->is_free = 0;
            return block + 1;
        }
    }
    /* Reuse the free tail before reserving a second, larger block after it. */
    if (heap_last && heap_last->is_free &&
        size <= (uintptr_t)-1 - (uintptr_t)(heap_last + 1) &&
        heap_ensure((uintptr_t)(heap_last + 1) + size)) {
        heap_remove_free(heap_last);
        heap_last->size = size;
        heap_last->is_free = 0;
        heap_cursor = (uintptr_t)(heap_last + 1) + size;
        return heap_last + 1;
    }
    if (heap_cursor == 0) heap_cursor = (uintptr_t)&__heap_base;
    uintptr_t start = (heap_cursor + 15u) & ~(uintptr_t)15u;
    if (start > (uintptr_t)-1 - sizeof(heap_block) ||
        size > (uintptr_t)-1 - start - sizeof(heap_block))
        return (void *)0;
    uintptr_t limit = start + sizeof(heap_block) + size;
    if (!heap_ensure(limit)) return (void *)0;
    heap_block *block = (heap_block *)start;
    block->size = size;
    block->next = (void *)0;
    block->previous = heap_last;
    block->is_free = 0;
    if (heap_last) heap_last->next = block;
    heap_last = block;
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

#ifndef WASTE_ALLOCATOR_ONLY
int posix_memalign(void **result, size_t alignment, size_t size) {
    if (!result || alignment < sizeof(void *) ||
        (alignment & (alignment - 1)) != 0 || alignment > 16)
        return 22; /* EINVAL: this allocator guarantees 16-byte alignment. */
    void *memory = malloc(size);
    if (!memory) return 12; /* ENOMEM */
    *result = memory;
    return 0;
}

void *reallocarray(void *pointer, size_t count, size_t size) {
    if (count && size > (size_t)-1 / count) return (void *)0;
    return realloc(pointer, count * size);
}
#endif /* WASTE_ALLOCATOR_ONLY */

void free(void *ptr) {
    if (!ptr) return;
    heap_block *block = (heap_block *)ptr - 1;
    block->is_free = 1;
    if (block->next && block->next->is_free) {
        heap_remove_free(block->next);
        heap_join_next(block);
    }
    if (block->previous && block->previous->is_free) {
        heap_block *previous = block->previous;
        heap_remove_free(previous);
        heap_join_next(previous);
        block = previous;
    }
    heap_insert_free(block);
}

/* Forward declaration for use by realloc */
void *memcpy(void *dst, const void *src, size_t n);

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return (void *)0; }
    if (size > (size_t)-1 - 15u) return (void *)0;
    heap_block *block = (heap_block *)ptr - 1;
    size_t aligned = heap_align(size);
    if (block->size >= aligned) {
        heap_split(block, aligned);
        return ptr;
    }
    if (block->next && block->next->is_free &&
        block->size + sizeof(heap_block) + block->next->size >= aligned) {
        heap_remove_free(block->next);
        heap_join_next(block);
        heap_split(block, aligned);
        return ptr;
    }
    /* Growing the final live block needs no new search, copy or free. */
    if (block == heap_last && aligned <= (uintptr_t)-1 - (uintptr_t)ptr &&
        heap_ensure((uintptr_t)ptr + aligned)) {
        heap_cursor = (uintptr_t)ptr + aligned;
        block->size = aligned;
        return ptr;
    }
    void *fresh = malloc(size);
    if (!fresh) return (void *)0;
    memcpy(fresh, ptr, block->size);
    free(ptr);
    return fresh;
}
