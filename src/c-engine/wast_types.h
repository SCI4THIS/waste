#ifndef WAST_TYPES_H
#define WAST_TYPES_H

#include <stdint.h>

/* 16-byte v128 value */
typedef struct { uint8_t bytes[16]; } wasm_v128;

/* Value type enum */
typedef enum {
    WASM_VALTYPE_I32 = 0,
    WASM_VALTYPE_I64,
    WASM_VALTYPE_F32,
    WASM_VALTYPE_F64,
    WASM_VALTYPE_V128
} wasm_valtype;

/* NaN match mode stored in first byte of each float lane */
#define NAN_MATCH_EXACT      0  /* byte-exact comparison */
#define NAN_MATCH_F32_CANON  1  /* f32 lane: canonical NaN */
#define NAN_MATCH_F32_ARITH  2  /* f32 lane: arithmetic NaN */
#define NAN_MATCH_F64_CANON  3  /* f64 lane: canonical NaN */
#define NAN_MATCH_F64_ARITH  4  /* f64 lane: arithmetic NaN */

/* Value union for test args and expected results */
typedef struct {
    wasm_valtype type;
    union {
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;
        wasm_v128 v128;
    };
    /* Per-lane NaN matching mode (for v128 only, 16 bytes max lanes) */
    uint8_t nan_mode[16];
} wasm_value;

/*
 * A single decoded instruction (flat, post-folding-expansion).
 * opcode=0x20 -> local.get (u32_imm=local index)
 * opcode=0xFD -> SIMD (simd_op=sub-opcode; if simd_op==12, v128_imm holds 16 bytes)
 * opcode=0x0B -> end
 */
typedef struct {
    uint32_t opcode;
    uint32_t simd_op;   /* for SIMD */
    uint32_t u32_imm;   /* for local.get, etc. */
    wasm_v128 v128_imm; /* for v128.const */
} wasm_instr;

#define WAST_MAX_PARAMS       4
#define WAST_MAX_INSTRS       128
#define WAST_MAX_EXPORT_NAME  128
#define WAST_MAX_FUNCS        64
#define WAST_MAX_ASSERTIONS   512
#define WAST_MAX_ARGS         4
#define WAST_MAX_RESULTS      1
#define WAST_MAX_ALTERNATIVES 8

/* A parsed WAT function */
typedef struct {
    char export_name[WAST_MAX_EXPORT_NAME];
    wasm_valtype params[WAST_MAX_PARAMS];
    int param_count;
    wasm_valtype result; /* single result type */
    int has_result;
    wasm_instr instrs[WAST_MAX_INSTRS];
    int instr_count;
} wast_func;

/* Parsed WAT module (list of functions) */
typedef struct {
    wast_func funcs[WAST_MAX_FUNCS];
    int func_count;
} wast_module;

/* A single assert_return command */
typedef struct {
    char func_name[WAST_MAX_EXPORT_NAME];
    wasm_value args[WAST_MAX_ARGS];
    int arg_count;
    /* expected: alternatives[i] is one valid result set */
    wasm_value alternatives[WAST_MAX_ALTERNATIVES][WAST_MAX_RESULTS];
    int alt_count;
    int result_count; /* number of results per alternative (always 1 here) */
} wast_assertion;

/* A module group: one module + its associated assertions */
#define WAST_MAX_GROUPS 8

typedef struct {
    wast_module    module;
    wast_assertion assertions[WAST_MAX_ASSERTIONS];
    int            assertion_count;
} wast_group;

/* Complete parsed WAST script */
typedef struct {
    wast_group groups[WAST_MAX_GROUPS];
    int        group_count;
    char       error[256];
} wast_script;

#endif /* WAST_TYPES_H */
