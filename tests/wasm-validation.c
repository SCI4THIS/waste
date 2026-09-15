#include "runtime_internal.h"
#include "op/validate.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int condition, const char *message) {
    if (condition) return;
    fprintf(stderr, "FAIL: %s\n", message);
    failures++;
}

static void init_engine(waste_exec_engine *engine, exec_func_type *type,
                        exec_func *function) {
    memset(engine, 0, sizeof(*engine));
    memset(type, 0, sizeof(*type));
    memset(function, 0, sizeof(*function));
    type->kind = WAST_TYPE_FUNC;
    type->rec_group_size = 1;
    type->supertype = -1;
    type->is_final = 1;
    engine->types = type;
    engine->type_count = 1;
    engine->funcs = function;
    engine->func_count = 1;
}

static void test_structured_results(void) {
    waste_exec_engine engine;
    exec_func_type type;
    exec_func function;
    exec_instr invalid[] = {{.opcode = 0x1a}, {.opcode = 0x0b}};
    exec_instr unsupported[] = {{.opcode = 0xfe}, {.opcode = 0x0b}};

    init_engine(&engine, &type, &function);
    wasm_validation_result result = wasm_validate_function(
        &engine, &function, invalid, 2);
    check(result.status == WASM_VALIDATION_INVALID,
          "stack underflow is invalid");
    check(result.instruction == 0 && result.opcode == 0x1a,
          "invalid result identifies its instruction and opcode");

    init_engine(&engine, &type, &function);
    result = wasm_validate_function(&engine, &function, unsupported, 2);
    check(result.status == WASM_VALIDATION_UNSUPPORTED,
          "uncovered opcode is explicitly unsupported");
    check(result.instruction == 0 && result.opcode == 0xfe,
          "unsupported result identifies its instruction and opcode");
}

static void test_resolved_metadata(void) {
    waste_exec_engine engine;
    exec_func_type type;
    exec_func function;
    exec_instr code[] = {
        {.opcode = 0x02, .simd_op = 1, .block_type_index = -1},
        {.opcode = 0x0b, .block_type_index = -1},
        {.opcode = 0x10, .u32_imm = 0, .block_type_index = -1},
        {.opcode = 0x0b, .block_type_index = -1},
    };

    init_engine(&engine, &type, &function);
    wasm_validation_result result = wasm_validate_function(
        &engine, &function, code, 4);
    check(result.status == WASM_VALIDATION_VALID,
          "valid control and call instructions validate");
    check(function.validated, "function records one-time validation");
    check((code[0].validation_flags & EXEC_INSTR_RESOLVED_BRANCH) != 0 &&
              code[0].resolved_target == 1,
          "block records its resolved end instruction");
    check((code[2].validation_flags & EXEC_INSTR_RESOLVED_TYPE) != 0 &&
              code[2].resolved_type_index == 0,
          "direct call records its resolved function type");

    code[0].opcode = 0xfe;
    result = wasm_validate_function(&engine, &function, code, 4);
    check(result.status == WASM_VALIDATION_VALID,
          "an already validated function is not validated a second time");
}

static void test_local_metadata(void) {
    waste_exec_engine engine;
    exec_func_type type;
    exec_func function;
    exec_instr code[] = {
        {.opcode = 0x20, .u32_imm = 0, .block_type_index = -1},
        {.opcode = 0x1a, .block_type_index = -1},
        {.opcode = 0x0b, .block_type_index = -1},
    };

    init_engine(&engine, &type, &function);
    type.params[0] = WASM_VALTYPE_I32;
    type.param_count = 1;
    wasm_validation_result result = wasm_validate_function(
        &engine, &function, code, 3);
    check(result.status == WASM_VALIDATION_VALID,
          "local access validates");
    check((code[0].validation_flags & EXEC_INSTR_RESOLVED_TYPE) != 0 &&
              code[0].resolved_value_type == WASM_VALTYPE_I32,
          "local access records its resolved value type");
}

int main(void) {
    test_structured_results();
    test_resolved_metadata();
    test_local_metadata();
    if (failures) return 1;
    puts("wasm validation tests passed");
    return 0;
}
