#ifndef WASTE_EXEC_H
#define WASTE_EXEC_H

#include "wast_types.h"
#include <stddef.h>

typedef struct waste_exec_engine waste_exec_engine;

typedef enum {
    EXEC_OK = 0,
    EXEC_ERROR_FORMAT,
    EXEC_ERROR_UNSUPPORTED,
    EXEC_ERROR_TRAP,
    EXEC_ERROR_NOT_FOUND,
} exec_status;

typedef struct {
    exec_status status;
    char message[256];
} exec_error;

typedef exec_status (*exec_host_func)(void *host_data,
                                      const wasm_value *args, int arg_count,
                                      wasm_value *results, int *result_count,
                                      exec_error *error);

typedef struct {
    const char *module;
    const char *name;
    exec_host_func function;
    void *host_data;
    const waste_exec_engine *type_owner;
    uint32_t type_index;
    uint8_t has_wasm_type;
} exec_host_import;

typedef struct {
    wasm_value value;
    uint8_t mutable_;
    const waste_exec_engine *type_owner; /* engine owning the value type (for TYPE_REF) */
} exec_global;

typedef struct {
    uint8_t *data;
    uint32_t pages;
    uint32_t max_pages;
    uint8_t has_max;
} exec_memory;

/* A single slot in a table: tracks the owning engine and its local function index.
 * owner==NULL means the slot is uninitialized (null reference). */
typedef struct {
    waste_exec_engine *owner;
    uint32_t func_idx;
} exec_table_element;

typedef struct {
    exec_table_element *elements;
    uint32_t size;
    uint32_t max_size;
    uint8_t has_max;
    wasm_valtype element_type;
    const waste_exec_engine *type_owner;
} exec_table;

typedef struct {
    const char *module;
    const char *name;
    exec_global *global;
} exec_global_import;

typedef struct {
    const char *module;
    const char *name;
    exec_memory *memory;
} exec_memory_import;

typedef struct {
    const char *module;
    const char *name;
    exec_table *table;
} exec_table_import;

typedef struct {
    const exec_host_import *functions;
    size_t function_count;
    const exec_global_import *globals;
    size_t global_count;
    const exec_memory_import *memories;
    size_t memory_count;
    const exec_table_import *tables;
    size_t table_count;
} exec_imports;

/*
 * Load a binary Wasm module supported by the developing C executor.
 * On success, *engine_out is set and EXEC_OK returned.
 * On failure, error is populated and NULL engine returned.
 */
exec_status exec_load(const uint8_t *bytes, size_t size,
                      waste_exec_engine **engine_out, exec_error *error);

exec_status exec_load_with_imports(const uint8_t *bytes, size_t size,
                                   const exec_imports *imports,
                                   waste_exec_engine **engine_out,
                                   exec_error *error);

/*
 * Exported extern pointers are stable for the lifetime of their owning engine.
 * They may be passed directly in exec_imports to another engine. Consumers must
 * be destroyed before the engine that owns a shared extern.
 */

void exec_free(waste_exec_engine *engine);

/* Find an exported function by name -> function index */
exec_status exec_find_export(const waste_exec_engine *engine,
                             const char *name, uint32_t *func_idx,
                             exec_error *error);

exec_status exec_get_func_type_index(const waste_exec_engine *engine,
                                     uint32_t func_idx, uint32_t *type_index,
                                     exec_error *error);

exec_status exec_find_export_global(const waste_exec_engine *engine,
                                    const char *name, exec_global **global,
                                    exec_error *error);
exec_status exec_find_export_memory(const waste_exec_engine *engine,
                                    const char *name, exec_memory **memory,
                                    exec_error *error);
exec_status exec_find_export_table(const waste_exec_engine *engine,
                                   const char *name, exec_table **table,
                                   exec_error *error);

/*
 * Invoke a function with given arguments and collect results.
 * args/results are wasm_value arrays.
 */
exec_status exec_invoke(waste_exec_engine *engine,
                        uint32_t func_idx,
                        const wasm_value *args, int arg_count,
                        wasm_value *results, int *result_count,
                        exec_error *error);

#endif /* WASTE_EXEC_H */
