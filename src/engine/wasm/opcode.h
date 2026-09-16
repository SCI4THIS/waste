#ifndef WASTE_WASM_OPCODE_H
#define WASTE_WASM_OPCODE_H

#include <stdint.h>

typedef enum {
    WAST_SIMD_IMM_NONE = 0,
    WAST_SIMD_IMM_MEMARG,
    WAST_SIMD_IMM_LANE,
    WAST_SIMD_IMM_MEMARG_LANE,
    WAST_SIMD_IMM_SHUFFLE,
    WAST_SIMD_IMM_CONST
} wast_simd_immediate;

typedef struct {
    uint32_t opcode;
    wast_simd_immediate immediate;
    uint8_t natural_alignment;
    uint8_t lane_count;
} wast_simd_info;

int wast_simd_lookup(const char *name, wast_simd_info *info);
int wast_simd_get_info(uint32_t opcode, wast_simd_info *info);

/* ---- Unified opcode metadata ---- */

typedef enum {
    WASM_OP_CLASS_INVALID = 0,
    WASM_OP_CLASS_CUSTOM,
    WASM_OP_CLASS_UNARY,
    WASM_OP_CLASS_BINARY,
    WASM_OP_CLASS_CONST,
    WASM_OP_CLASS_LOAD,
    WASM_OP_CLASS_STORE,
    WASM_OP_CLASS_MEMORY_SIZE,
    WASM_OP_CLASS_MEMORY_GROW,
} wasm_op_class;

typedef struct {
    uint8_t op_class;
    uint8_t operand_type;
    uint8_t result_type;
    uint8_t load_width;
    uint8_t natural_align;
    uint8_t sign_extend;
} wasm_opcode_info;

const wasm_opcode_info *wasm_opcode_get_info(uint32_t opcode);
const wasm_opcode_info *wasm_opcode_fc_get_info(uint32_t sub_opcode);

#endif /* WASTE_WASM_OPCODE_H */
