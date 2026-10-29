/* Force each allocation on the command-session success path to fail once.
 * Linker wrappers and tracking live only in this retained native host probe. */
#include "include/waste.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);

typedef struct { void *pointer; size_t size, at; } tracked;
static tracked pointers[65536];
static size_t live, allocation_index, fail_at;
static int enabled, injected;
static waste_error error;
static unsigned checks;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr, "embedding allocation:%d: %s (fault=%zu, %s)\n", \
        __LINE__, #condition, fail_at, error.message); exit(1); \
} } while (0)

static size_t pointer_hash(void *pointer) {
    return ((uintptr_t)pointer >> 4) * (size_t)UINT32_C(2654435761) & 65535;
}
static tracked *find_pointer(void *pointer) {
    size_t slot = pointer_hash(pointer);
    for (size_t i = 0; i < 65536; i++, slot = (slot + 1) & 65535) {
        if (pointers[slot].pointer == pointer) return &pointers[slot];
        if (!pointers[slot].pointer) return NULL;
    }
    return NULL;
}
static void remember(void *pointer, size_t size) {
    if (!pointer) return;
    size_t slot = pointer_hash(pointer);
    while (pointers[slot].pointer && pointers[slot].pointer != (void *)(uintptr_t)1)
        slot = (slot + 1) & 65535;
    pointers[slot] = (tracked){pointer, size, allocation_index};
    live++;
}
static int fail_allocation(size_t size) {
    if (!enabled || !size) return 0;
    if (allocation_index++ == fail_at) { injected = 1; return 1; }
    return 0;
}
void *__wrap_malloc(size_t size) {
    if (fail_allocation(size)) return NULL;
    void *pointer = __real_malloc(size);
    if (enabled) remember(pointer, size);
    return pointer;
}
void *__wrap_calloc(size_t count, size_t size) {
    if (fail_allocation(count && size ? size : 0)) return NULL;
    void *pointer = __real_calloc(count, size);
    if (enabled) remember(pointer, count * size);
    return pointer;
}
void *__wrap_realloc(void *pointer, size_t size) {
    if (fail_allocation(size)) return NULL;
    tracked *old = pointer ? find_pointer(pointer) : NULL;
    void *next = __real_realloc(pointer, size);
    if (!next && size) return NULL;
    int was_tracked = old != NULL;
    if (old) { old->pointer = (void *)(uintptr_t)1; live--; }
    if (enabled || was_tracked) remember(next, size);
    return next;
}
void __wrap_free(void *pointer) {
    tracked *old = pointer ? find_pointer(pointer) : NULL;
    if (old) { old->pointer = (void *)(uintptr_t)1; live--; }
    __real_free(pointer);
}
static uint64_t monotonic(void *unused) { (void)unused; return UINT64_C(1000000); }
static uint8_t *load(const char *path, uint32_t *length) {
    FILE *file = fopen(path, "rb"); CHECK(file != NULL);
    CHECK(!fseek(file, 0, SEEK_END));
    long size = ftell(file); CHECK(size > 0 && (unsigned long)size <= UINT32_MAX);
    CHECK(!fseek(file, 0, SEEK_SET));
    uint8_t *bytes = malloc((size_t)size); CHECK(bytes != NULL);
    CHECK(fread(bytes, 1, (size_t)size, file) == (size_t)size);
    CHECK(!fclose(file)); *length = (uint32_t)size; return bytes;
}
static size_t exercise(size_t fault, const uint8_t *libc, uint32_t libc_size,
                        const uint8_t *module, uint32_t module_size) {
    CHECK(!live);
    memset(pointers, 0, sizeof(pointers));
    allocation_index = 0; injected = 0; fail_at = fault; enabled = 1;
    waste_session_options options;
    waste_session_options_init(&options);
    options.monotonic_ns = monotonic;
    options.max_instructions = 1000000;
    waste_session *session = NULL;
    waste_status status = waste_session_create(&options, &session, &error);
    if (status == WASTE_OK) status = waste_session_put_file(session, "/lib/libc.so.wasm", libc, libc_size, 0644, &error);
    if (status == WASTE_OK) status = waste_session_put_file(session, "/usr/bin/output", module, module_size, 0755, &error);
    if (status == WASTE_OK) status = waste_session_input(session, (const uint8_t *)"input", 5, 1, &error);
    if (status == WASTE_OK) status = waste_session_start(session, "/usr/bin/output", NULL, 0, NULL, 0, &error);
    if (status == WASTE_OK) {
        waste_session_result result;
        CHECK(waste_session_inspect(session, &result) == WASTE_OK);
        unsigned steps = 0;
        while (result.state < WASTE_SESSION_EXITED) {
            CHECK(steps++ < 100);
            CHECK(waste_session_step(session, &error) == WASTE_OK);
            CHECK(waste_session_inspect(session, &result) == WASTE_OK);
        }
        if (result.state == WASTE_SESSION_TRAPPED)
            fprintf(stderr, "allocation became guest trap: %s\n", result.message);
        CHECK(result.state == WASTE_SESSION_EXITED || result.state == WASTE_SESSION_FAILED);
        if (!injected) {
            CHECK(result.state == WASTE_SESSION_EXITED && result.exit_code == 0 && result.stdout_bytes == 6);
            uint8_t output[6]; uint32_t copied;
            CHECK(waste_session_copy_output(session, 1, 0, output, sizeof(output), &copied, &error) == WASTE_OK);
            CHECK(copied == 6 && !memcmp(output, "output", 6));
        }
    }
    waste_session_delete(&session);
    enabled = 0;
    if (live) for (size_t i = 0; i < 65536; i++)
        if (pointers[i].pointer && pointers[i].pointer != (void *)(uintptr_t)1)
            fprintf(stderr, "retained allocation: call=%zu bytes=%zu\n", pointers[i].at, pointers[i].size);
    CHECK(!live);
    if (fault != SIZE_MAX) CHECK(injected);
    return allocation_index;
}
int main(int argc, char **argv) {
    CHECK(argc == 3);
    uint32_t libc_size, module_size;
    uint8_t *libc = load(argv[1], &libc_size), *module = load(argv[2], &module_size);
    size_t total = exercise(SIZE_MAX, libc, libc_size, module, module_size);
    CHECK(total > 0 && total < 4096);
    for (size_t i = 0; i < total; i++) exercise(i, libc, libc_size, module, module_size);
    free(libc); free(module);
    printf("embedding allocation failures: %zu failure points, %u checks passed\n", total, checks);
    return 0;
}
