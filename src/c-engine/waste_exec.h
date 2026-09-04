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

/*
 * Load a binary Wasm module (limited to our SIMD subset).
 * On success, *engine_out is set and EXEC_OK returned.
 * On failure, error is populated and NULL engine returned.
 */
exec_status exec_load(const uint8_t *bytes, size_t size,
                      waste_exec_engine **engine_out, exec_error *error);

void exec_free(waste_exec_engine *engine);

/* Find an exported function by name -> function index */
exec_status exec_find_export(const waste_exec_engine *engine,
                             const char *name, uint32_t *func_idx,
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
