#ifndef WASTE_GUEST_POSIX_H
#define WASTE_GUEST_POSIX_H

#include "store.h"

/* Host capabilities belong to the runtime. Descriptor/path/process semantics
 * stay in the per-store kernel; absent callbacks never access host files. */
typedef struct guest_posix_platform {
    int32_t (*open)(void *, const char *, size_t, int32_t, int32_t);
    int32_t (*close)(void *, int32_t);
    int32_t (*read)(void *, int32_t, void *, uint32_t);
    int32_t (*write)(void *, int32_t, const void *, uint32_t);
    void (*trace)(void *, const char *);
} guest_posix_platform;

int guest_posix_host_resolver(const char *module, const char *name,
                              void *context, native_host_binding *out);

/* Write host-owned report bytes through the active guest descriptor table. */
int32_t guest_posix_write_bytes(native_store *, int32_t, const uint8_t *, uint32_t);
int guest_posix_enqueue_input(native_store *, int, const uint8_t *, int);

/* Bounded ABI helpers for platform-specific capabilities (e.g. file pickers). */
exec_status guest_posix_memory(const waste_exec_engine *caller,
                               exec_memory **memory_out, exec_error *error);
int guest_posix_read_guest(exec_memory *memory, uint32_t offset,
                           void *destination, size_t length);
exec_status guest_posix_result(int32_t value, wasm_value *results,
                               int *result_count);

#endif
