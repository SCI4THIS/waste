/* allocator.c — Boundary-tag allocator for the WASTE guest libc.
 *
 * This implements malloc/calloc/realloc/free
 * using a boundary-tag free-list allocator with 16-byte aligned blocks.
 * Each block has a 16-byte header (size|flags, prev, next, padding) and a
 * 4-byte boundary tag at the end.  The allocator calls memory.grow via
 * compiler builtins when it needs more memory.
 *
 * When building as a PIC shared library (-fPIC, wasm-ld -shared), the
 * globals are relocated via __memory_base.
 */

#include "include/helper.h"

/* ---- internal state --------------------------------------------------- */

static i32 initialized;
static i32 errno_address;
static i32 heap_base;
static i32 program_break;
static i32 free_head;
static i32 grow_calls;

/* ---- helpers ---------------------------------------------------------- */

static inline i32 align16(i32 value) {
    return (value + 15) & ~15;
}

static void set_errno(i32 value) {
    if (!errno_address) return;
    *(i32 *)(u32)errno_address = value;
}

i32 *__errno_location(void) {
    return (i32 *)(u32)errno_address;
}

/* Read a 32-bit value at address a. */
static inline i32 ld32(i32 a) { return *(i32 *)(u32)a; }
/* Write a 32-bit value at address a. */
static inline void st32(i32 a, i32 v) { *(i32 *)(u32)a = v; }

/* ---- memory management ------------------------------------------------ */

static i32 ensure_capacity(i32 end) {
    for (;;) {
        u64 bound = (u64)(u32)__builtin_wasm_memory_size(0) * 65536ULL;
        if (bound >= (u64)(u32)end)
            return 1;
        if (__builtin_wasm_memory_grow(0, 1) == -1) {
            set_errno(12); /* ENOMEM */
            return 0;
        }
        grow_calls++;
    }
}

i32 waste_allocator_init(i32 base) {
    i32 err = (base + 3) & ~3;
    i32 first = align16(err + 4);
    if (first < base) return 0;
    if (!ensure_capacity(first)) return 0;
    errno_address = err;
    heap_base = first;
    program_break = first;
    free_head = 0;
    grow_calls = 0;
    st32(errno_address, 0);
    initialized = 1;
    return 1;
}

/* sbrk — returns the previous break or -1 on failure. */
i32 sbrk(i32 increment) {
    if (!initialized) {
        set_errno(22); /* EINVAL */
        return -1;
    }
    i32 old = program_break;
    i64 next = (i64)(u32)old + (i64)increment;
    if (next < (i64)(u32)heap_base) {
        set_errno(12);
        return -1;
    }
    if ((u64)next > 0xffffffffULL) {
        set_errno(12);
        return -1;
    }
    if (!ensure_capacity((i32)next)) return -1;
    program_break = (i32)next;
    return old;
}

/* ---- block management ------------------------------------------------- */

/* Block layout (16-byte header):
 *   offset 0: size | allocated_flag   (4 bytes)
 *   offset 4: prev free pointer       (4 bytes)
 *   offset 8: next free pointer       (4 bytes)
 *   offset 12: padding                (4 bytes)
 *   ...user data...
 *   offset size-4: size | allocated_flag  (boundary tag, 4 bytes)
 */

static void write_block(i32 block, i32 size, i32 allocated) {
    i32 tag = size | allocated;
    st32(block, tag);
    st32(block + size - 4, tag);
}

static void remove_free(i32 block) {
    i32 prev = ld32(block + 4);
    i32 next = ld32(block + 8);
    if (prev)
        st32(prev + 8, next);
    else
        free_head = next;
    if (next)
        st32(next + 4, prev);
}

static void insert_free(i32 block) {
    st32(block + 4, 0);
    st32(block + 8, free_head);
    if (free_head)
        st32(free_head + 4, block);
    free_head = block;
}

static void release_block(i32 block) {
    i32 size = ld32(block) & ~1;

    /* Coalesce with next block if free. */
    i32 next = block + size;
    if ((u32)next < (u32)program_break) {
        if (!(ld32(next) & 1)) {
            remove_free(next);
            size += ld32(next) & ~1;
        }
    }

    /* Coalesce with previous block if free. */
    if ((u32)block > (u32)heap_base) {
        i32 prev_size = ld32(block - 4) & ~1;
        if (prev_size >= 32) {
            i32 prev = block - prev_size;
            if ((u32)prev >= (u32)heap_base) {
                if (!(ld32(prev) & 1)) {
                    remove_free(prev);
                    block = prev;
                    size += prev_size;
                }
            }
        }
    }

    write_block(block, size, 0);
    insert_free(block);
}

/* ---- public API ------------------------------------------------------- */

void *malloc(size_t requested_sz) {
    i32 requested = (i32)requested_sz;
    if (!initialized) { set_errno(22); return (void *)0; }
    if ((u32)requested > 0xffffffe0u) { set_errno(12); return (void *)0; }
    if (requested == 0) requested = 1;

    i32 total = align16(requested + 20);
    if (total < 32) total = 32;

    /* Search free list. */
    i32 block = free_head;
    while (block) {
        i32 block_size = ld32(block) & ~1;
        if (block_size >= total) {
            remove_free(block);
            i32 remaining = block_size - total;
            if (remaining >= 32) {
                write_block(block, total, 1);
                write_block(block + total, remaining, 0);
                insert_free(block + total);
            } else {
                write_block(block, block_size, 1);
            }
            return (void *)(u32)(block + 16);
        }
        block = ld32(block + 8);
    }

    /* No suitable free block found — extend the heap. */
    i32 aligned_break = align16(program_break);
    i32 old_break = sbrk(aligned_break - program_break + total);
    if (old_break == -1) return (void *)0;
    write_block(aligned_break, total, 1);
    return (void *)(u32)(aligned_break + 16);
}

void free(void *ptr) {
    if (!ptr) return;
    i32 pointer = (i32)(u32)ptr;
    i32 block = pointer - 16;
    if ((u32)block < (u32)heap_base) { set_errno(22); return; }
    if ((u32)block >= (u32)program_break) { set_errno(22); return; }
    if (!(ld32(block) & 1)) { set_errno(22); return; }
    release_block(block);
}

void *calloc(size_t count, size_t size) {
    u64 bytes64 = (u64)count * (u64)size;
    if (bytes64 > 0xffffffffULL) { set_errno(12); return (void *)0; }
    void *ptr = malloc((size_t)(i32)bytes64);
    if (ptr) __builtin_memset(ptr, 0, (size_t)(i32)bytes64);
    return ptr;
}

void *realloc(void *ptr, size_t requested_sz) {
    i32 requested = (i32)requested_sz;
    if (!ptr) return malloc(requested_sz);
    if (requested == 0) { free(ptr); return (void *)0; }
    i32 pointer = (i32)(u32)ptr;
    i32 block = pointer - 16;
    i32 old_capacity = (ld32(block) & ~1) - 20;
    void *replacement = malloc(requested_sz);
    if (!replacement) return (void *)0;
    i32 copy_size = old_capacity < requested ? old_capacity : requested;
    __builtin_memcpy(replacement, ptr, (size_t)copy_size);
    free(ptr);
    return replacement;
}

i32 waste_malloc_usable_size(i32 pointer) {
    if (!pointer) return 0;
    return (ld32(pointer - 16) & ~1) - 20;
}

i32 waste_heap_base(void) { return heap_base; }
i32 waste_heap_end(void) { return program_break; }
i32 waste_memory_pages(void) { return __builtin_wasm_memory_size(0); }
i32 waste_memory_grow_calls(void) { return grow_calls; }
