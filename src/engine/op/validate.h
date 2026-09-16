#ifndef WASTE_WASM_VALIDATE_H
#define WASTE_WASM_VALIDATE_H

#include "../runtime_internal.h"

typedef enum {
    WASM_VALIDATION_INVALID = -1,
    WASM_VALIDATION_UNSUPPORTED = 0,
    WASM_VALIDATION_VALID = 1,
    /* Internal: instruction validated, continue to next instruction.
     * Only used by dispatch handlers; never escapes the validation loop. */
    WASM_VALIDATION_CONTINUE = 2,
} wasm_validation_status;

typedef struct {
    wasm_validation_status status;
    uint32_t instruction;
    uint32_t opcode;
    uint32_t subopcode;
} wasm_validation_result;

/* ---- Validation stack and control types ---- */

#define WASM_VALIDATION_STACK 256
#define WASM_BOTTOM_TYPE ((wasm_valtype)0x7fff)

typedef struct {
    const waste_exec_engine *engine;
    int height;
    int unreachable;
    int tail_call_seen;
    uint8_t kind;
    uint8_t has_else;
    int param_count;
    int result_count;
    wasm_valtype params[WAST_MAX_PARAMS];
    wasm_valtype results[WAST_MAX_RESULTS];
    uint8_t entry_initialized[(EXEC_MAX_LOCALS + 7) / 8];
} wasm_validation_control;

/* Bounded view of the validation state, passed to per-opcode handlers. */
typedef struct {
    const waste_exec_engine *engine;
    const exec_func_type *signature;
    const exec_func *function;
    wasm_valtype *stack;
    int *top;
    wasm_validation_control *controls;
    int *control_top;
    uint8_t *initialized;
} wasm_validate_context;

/* ---- Public interface ---- */

int value_type_is_defined(const waste_exec_engine *engine,
                          wasm_valtype type);
int same_value_type(const waste_exec_engine *left_engine,
                    wasm_valtype left,
                    const waste_exec_engine *right_engine,
                    wasm_valtype right,
                    unsigned depth);
int same_func_type(const waste_exec_engine *left_engine,
                   uint32_t left_index,
                   const waste_exec_engine *right_engine,
                   uint32_t right_index);
int type_index_is_subtype(const waste_exec_engine *actual_engine,
                          uint32_t actual_index,
                          const waste_exec_engine *required_engine,
                          uint32_t required_index);
int func_type_is_subtype(const waste_exec_engine *actual_engine,
                         uint32_t actual_index,
                         const waste_exec_engine *required_engine,
                         uint32_t required_index);
int global_type_is_compat(const waste_exec_engine *actual_engine,
                          wasm_valtype actual,
                          const waste_exec_engine *required_engine,
                          wasm_valtype required,
                          int mutable_);
int is_reference_type(wasm_valtype type);
int is_nullable_reference_type(wasm_valtype type);
int is_function_reference_type(wasm_valtype type);
int is_eq_reference_type(const waste_exec_engine *engine,
                         wasm_valtype type);
wasm_valtype nonnullable_reference_type(wasm_valtype type);
int nullable_reference_for_heap(const waste_exec_engine *engine,
                                int32_t heap_type,
                                wasm_valtype *type);
int validate_declared_subtype(waste_exec_engine *engine, uint32_t index);

wasm_validation_result wasm_validate_function(
    const waste_exec_engine *engine, exec_func *function,
    exec_instr *code, uint32_t code_size);

#endif
