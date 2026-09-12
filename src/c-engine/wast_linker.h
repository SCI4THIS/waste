#ifndef WAST_LINKER_H
#define WAST_LINKER_H

#include "waste_exec.h"
#include "wast_types.h"
#include <stddef.h>

typedef struct {
    waste_exec_engine *engine;
    uint32_t func_idx;
} native_linked_func;

typedef struct native_call_block {
    native_linked_func *calls;
    struct native_call_block *next;
} native_call_block;

typedef struct {
    waste_exec_engine *engine;
    const wast_module *module;
    char id[WAST_MAX_EXPORT_NAME];
    char registered[WAST_MAX_EXPORT_NAME];
} native_linked_module;

/* Host function binding returned by a resolver callback. */
typedef struct {
    exec_host_func function;
    void *host_data;
    exec_host_control control;
} native_host_binding;

/* Optional callback for resolving host-provided function imports that no
   registered Wasm module provides (e.g. POSIX stubs in the browser build).
   Returns 1 if resolved, 0 otherwise. */
typedef int (*native_host_resolver)(const char *module, const char *name,
                                     void *context, native_host_binding *out);

typedef struct {
    native_linked_module *modules;
    int module_count;
    int module_capacity;
    native_call_block *call_blocks;
    waste_exec_engine **orphan_engines;
    int orphan_count;
    int orphan_capacity;
    /* Spectest provider (for WAST script conformance tests). */
    exec_memory spectest_memory;
    exec_table spectest_table;
    exec_global spectest_i32;
    exec_global spectest_i64;
    exec_global spectest_f32;
    exec_global spectest_f64;
    /* Optional host function resolver for imports not satisfied by any
       registered Wasm module. */
    native_host_resolver host_resolver;
    void *host_context;
} native_store;

void native_store_init(native_store *store);
void native_store_free(native_store *store);

int native_store_add(native_store *store, waste_exec_engine *engine,
                     const wast_module *identity,
                     const wast_module *metadata);

int native_store_keep_orphan(native_store *store,
                              waste_exec_engine *engine);

native_linked_module *native_registered_module(native_store *store,
                                                const char *name);

waste_exec_engine *native_selected_engine(native_store *store,
                                           const char *id);

native_linked_module *native_selected_module(native_store *store,
                                              const char *id);

const wast_module *native_find_definition(const wast_script *script,
                                           int before_group,
                                           const char *id);

exec_status native_load_module(native_store *store,
                                const wast_module *module,
                                const uint8_t *bytes, size_t size,
                                waste_exec_engine **engine_out,
                                exec_error *error);

uint8_t *encode_group_module(const wast_group *group, size_t *size_out,
                              char *error);

#endif /* WAST_LINKER_H */
