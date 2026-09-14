#ifndef WASTE_WASM_VALIDATE_H
#define WASTE_WASM_VALIDATE_H

#include "../runtime/runtime_internal.h"

typedef enum {
    WASM_VALIDATION_INVALID = -1,
    WASM_VALIDATION_UNSUPPORTED = 0,
    WASM_VALIDATION_VALID = 1,
} wasm_validation_status;

typedef struct {
    wasm_validation_status status;
    uint32_t instruction;
    uint32_t opcode;
    uint32_t subopcode;
} wasm_validation_result;

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
