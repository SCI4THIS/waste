/* Private runtime boundary: exercise the browser allocator itself, not libc. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#define TEST_HEAP_BYTES (8u * 1024u * 1024u)
_Alignas(65536) unsigned char browser_test_heap[TEST_HEAP_BYTES];
void *browser_test_malloc(size_t);
void *browser_test_calloc(size_t, size_t);
void *browser_test_realloc(void *, size_t);
void browser_test_free(void *);
size_t browser_test_memory_size(unsigned index) {
    assert(index == 0);
    return ((uintptr_t)browser_test_heap + sizeof(browser_test_heap)) / 65536u;
}
size_t browser_test_memory_grow(unsigned index, size_t pages) {
    assert(index == 0 && pages != 0);
    return (size_t)-1;
}

int main(void) {
    unsigned char *p = browser_test_malloc(33);
    assert(p && (uintptr_t)p % 16u == 0);
    memset(p, 0x5a, 33);
    unsigned char *q = browser_test_realloc(p, 4097);
    assert(q == p); /* Tail growth preserves both pointer and contents. */
    for (size_t i = 0; i < 33; ++i) assert(q[i] == 0x5a);
    assert(browser_test_realloc(q, SIZE_MAX) == NULL && q[0] == 0x5a);
    browser_test_free(q);
    assert(browser_test_calloc(SIZE_MAX, 2) == NULL);
    assert(browser_test_malloc(SIZE_MAX) == NULL);
    assert(browser_test_malloc(TEST_HEAP_BYTES) == NULL);
    p = browser_test_realloc(NULL, 0);
    assert(p && (uintptr_t)p % 16u == 0);
    browser_test_free(p);
    p = browser_test_calloc(17, 19);
    assert(p);
    for (size_t i = 0; i < 323; ++i) assert(p[i] == 0);
    browser_test_free(p);

    /* Merge both neighbours, then split and merge again while a peer is live. */
    void *a = browser_test_malloc(256), *b = browser_test_malloc(256);
    void *c = browser_test_malloc(256), *guard = browser_test_malloc(32);
    assert(a && b && c && guard);
    memset(guard, 0x71, 32);
    browser_test_free(a); browser_test_free(c); browser_test_free(b);
    p = browser_test_malloc(700);
    assert(p == a);
    p = browser_test_realloc(p, 64);
    assert(p == a);
    q = browser_test_malloc(512);
    assert(q);
    memset(q, 0x64, 512);
    assert(browser_test_realloc(q, 640) == q);
    unsigned char *moved = browser_test_realloc(q, 4096);
    assert(moved);
    for (size_t i = 0; i < 512; ++i) assert(moved[i] == 0x64);
    browser_test_free(moved); browser_test_free(p);
    for (size_t i = 0; i < 32; ++i) assert(((unsigned char *)guard)[i] == 0x71);
    browser_test_free(guard);

    /* Many live blocks and fragmented frees must retain unrelated bytes. */
    enum { COUNT = 8192 };
    void *blocks[COUNT];
    for (unsigned i = 0; i < COUNT; ++i) {
        size_t size = 16u + (i % 31u) * 16u;
        blocks[i] = browser_test_malloc(size);
        assert(blocks[i] && (uintptr_t)blocks[i] % 16u == 0);
        memset(blocks[i], (unsigned char)i, size);
    }
    for (unsigned i = 0; i < COUNT; i += 2) browser_test_free(blocks[i]);
    for (unsigned i = 0; i < COUNT; i += 2) {
        blocks[i] = browser_test_malloc(32u + (i % 7u) * 32u);
        assert(blocks[i]);
        memset(blocks[i], 0x4b, 32u + (i % 7u) * 32u);
    }
    for (unsigned i = 1; i < COUNT; i += 2)
        for (size_t j = 0; j < 16u + (i % 31u) * 16u; ++j)
            assert(((unsigned char *)blocks[i])[j] == (unsigned char)i);
    for (unsigned i = COUNT; i; --i) browser_test_free(blocks[i - 1]);
    p = browser_test_malloc(6u * 1024u * 1024u);
    assert(p == a);
    memset(p, 0x39, 6u * 1024u * 1024u);
    assert(browser_test_realloc(p, TEST_HEAP_BYTES) == NULL);
    assert(p[0] == 0x39 && p[6u * 1024u * 1024u - 1] == 0x39);
    assert(browser_test_realloc(p, 0) == NULL);
    browser_test_free(NULL);
    puts("Browser allocator: ownership, alignment, coalescing, growth and failure checks pass");
    return 0;
}
