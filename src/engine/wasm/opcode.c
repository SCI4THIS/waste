#include "wasm/opcode.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char *name;
    uint16_t opcode;
} simd_name;

/* Opcode spellings follow the official interpreter's text lexer and binary
 * decoder.  Keep this table semantic-free: operand and result checking lives
 * in the C validator/executor. */
static const simd_name simd_names[] = {
    {"v128.load",0x00},{"v128.load8x8_s",0x01},{"v128.load8x8_u",0x02},
    {"v128.load16x4_s",0x03},{"v128.load16x4_u",0x04},
    {"v128.load32x2_s",0x05},{"v128.load32x2_u",0x06},
    {"v128.load8_splat",0x07},{"v128.load16_splat",0x08},
    {"v128.load32_splat",0x09},{"v128.load64_splat",0x0a},
    {"v128.store",0x0b},{"v128.const",0x0c},{"i8x16.shuffle",0x0d},
    {"i8x16.swizzle",0x0e},{"i8x16.splat",0x0f},
    {"i16x8.splat",0x10},{"i32x4.splat",0x11},{"i64x2.splat",0x12},
    {"f32x4.splat",0x13},{"f64x2.splat",0x14},
    {"i8x16.extract_lane_s",0x15},{"i8x16.extract_lane_u",0x16},
    {"i8x16.replace_lane",0x17},{"i16x8.extract_lane_s",0x18},
    {"i16x8.extract_lane_u",0x19},{"i16x8.replace_lane",0x1a},
    {"i32x4.extract_lane",0x1b},{"i32x4.replace_lane",0x1c},
    {"i64x2.extract_lane",0x1d},{"i64x2.replace_lane",0x1e},
    {"f32x4.extract_lane",0x1f},{"f32x4.replace_lane",0x20},
    {"f64x2.extract_lane",0x21},{"f64x2.replace_lane",0x22},
    {"i8x16.eq",0x23},{"i8x16.ne",0x24},{"i8x16.lt_s",0x25},
    {"i8x16.lt_u",0x26},{"i8x16.gt_s",0x27},{"i8x16.gt_u",0x28},
    {"i8x16.le_s",0x29},{"i8x16.le_u",0x2a},{"i8x16.ge_s",0x2b},
    {"i8x16.ge_u",0x2c},{"i16x8.eq",0x2d},{"i16x8.ne",0x2e},
    {"i16x8.lt_s",0x2f},{"i16x8.lt_u",0x30},{"i16x8.gt_s",0x31},
    {"i16x8.gt_u",0x32},{"i16x8.le_s",0x33},{"i16x8.le_u",0x34},
    {"i16x8.ge_s",0x35},{"i16x8.ge_u",0x36},{"i32x4.eq",0x37},
    {"i32x4.ne",0x38},{"i32x4.lt_s",0x39},{"i32x4.lt_u",0x3a},
    {"i32x4.gt_s",0x3b},{"i32x4.gt_u",0x3c},{"i32x4.le_s",0x3d},
    {"i32x4.le_u",0x3e},{"i32x4.ge_s",0x3f},{"i32x4.ge_u",0x40},
    {"f32x4.eq",0x41},{"f32x4.ne",0x42},{"f32x4.lt",0x43},
    {"f32x4.gt",0x44},{"f32x4.le",0x45},{"f32x4.ge",0x46},
    {"f64x2.eq",0x47},{"f64x2.ne",0x48},{"f64x2.lt",0x49},
    {"f64x2.gt",0x4a},{"f64x2.le",0x4b},{"f64x2.ge",0x4c},
    {"v128.not",0x4d},{"v128.and",0x4e},{"v128.andnot",0x4f},
    {"v128.or",0x50},{"v128.xor",0x51},{"v128.bitselect",0x52},
    {"v128.any_true",0x53},{"v128.load8_lane",0x54},
    {"v128.load16_lane",0x55},{"v128.load32_lane",0x56},
    {"v128.load64_lane",0x57},{"v128.store8_lane",0x58},
    {"v128.store16_lane",0x59},{"v128.store32_lane",0x5a},
    {"v128.store64_lane",0x5b},{"v128.load32_zero",0x5c},
    {"v128.load64_zero",0x5d},{"f32x4.demote_f64x2_zero",0x5e},
    {"f64x2.promote_low_f32x4",0x5f},{"i8x16.abs",0x60},
    {"i8x16.neg",0x61},{"i8x16.popcnt",0x62},{"i8x16.all_true",0x63},
    {"i8x16.bitmask",0x64},{"i8x16.narrow_i16x8_s",0x65},
    {"i8x16.narrow_i16x8_u",0x66},{"f32x4.ceil",0x67},
    {"f32x4.floor",0x68},{"f32x4.trunc",0x69},{"f32x4.nearest",0x6a},
    {"i8x16.shl",0x6b},{"i8x16.shr_s",0x6c},{"i8x16.shr_u",0x6d},
    {"i8x16.add",0x6e},{"i8x16.add_sat_s",0x6f},
    {"i8x16.add_sat_u",0x70},{"i8x16.sub",0x71},
    {"i8x16.sub_sat_s",0x72},{"i8x16.sub_sat_u",0x73},
    {"f64x2.ceil",0x74},{"f64x2.floor",0x75},{"i8x16.min_s",0x76},
    {"i8x16.min_u",0x77},{"i8x16.max_s",0x78},{"i8x16.max_u",0x79},
    {"f64x2.trunc",0x7a},{"i8x16.avgr_u",0x7b},
    {"i16x8.extadd_pairwise_i8x16_s",0x7c},
    {"i16x8.extadd_pairwise_i8x16_u",0x7d},
    {"i32x4.extadd_pairwise_i16x8_s",0x7e},
    {"i32x4.extadd_pairwise_i16x8_u",0x7f},
    {"i16x8.abs",0x80},{"i16x8.neg",0x81},{"i16x8.q15mulr_sat_s",0x82},
    {"i16x8.all_true",0x83},{"i16x8.bitmask",0x84},
    {"i16x8.narrow_i32x4_s",0x85},{"i16x8.narrow_i32x4_u",0x86},
    {"i16x8.extend_low_i8x16_s",0x87},{"i16x8.extend_high_i8x16_s",0x88},
    {"i16x8.extend_low_i8x16_u",0x89},{"i16x8.extend_high_i8x16_u",0x8a},
    {"i16x8.shl",0x8b},{"i16x8.shr_s",0x8c},{"i16x8.shr_u",0x8d},
    {"i16x8.add",0x8e},{"i16x8.add_sat_s",0x8f},
    {"i16x8.add_sat_u",0x90},{"i16x8.sub",0x91},
    {"i16x8.sub_sat_s",0x92},{"i16x8.sub_sat_u",0x93},
    {"f64x2.nearest",0x94},{"i16x8.mul",0x95},{"i16x8.min_s",0x96},
    {"i16x8.min_u",0x97},{"i16x8.max_s",0x98},{"i16x8.max_u",0x99},
    {"i16x8.avgr_u",0x9b},{"i16x8.extmul_low_i8x16_s",0x9c},
    {"i16x8.extmul_high_i8x16_s",0x9d},{"i16x8.extmul_low_i8x16_u",0x9e},
    {"i16x8.extmul_high_i8x16_u",0x9f},{"i32x4.abs",0xa0},
    {"i32x4.neg",0xa1},{"i32x4.all_true",0xa3},{"i32x4.bitmask",0xa4},
    {"i32x4.extend_low_i16x8_s",0xa7},{"i32x4.extend_high_i16x8_s",0xa8},
    {"i32x4.extend_low_i16x8_u",0xa9},{"i32x4.extend_high_i16x8_u",0xaa},
    {"i32x4.shl",0xab},{"i32x4.shr_s",0xac},{"i32x4.shr_u",0xad},
    {"i32x4.add",0xae},{"i32x4.sub",0xb1},{"i32x4.mul",0xb5},
    {"i32x4.min_s",0xb6},{"i32x4.min_u",0xb7},{"i32x4.max_s",0xb8},
    {"i32x4.max_u",0xb9},{"i32x4.dot_i16x8_s",0xba},
    {"i32x4.extmul_low_i16x8_s",0xbc},{"i32x4.extmul_high_i16x8_s",0xbd},
    {"i32x4.extmul_low_i16x8_u",0xbe},{"i32x4.extmul_high_i16x8_u",0xbf},
    {"i64x2.abs",0xc0},{"i64x2.neg",0xc1},{"i64x2.all_true",0xc3},
    {"i64x2.bitmask",0xc4},{"i64x2.extend_low_i32x4_s",0xc7},
    {"i64x2.extend_high_i32x4_s",0xc8},{"i64x2.extend_low_i32x4_u",0xc9},
    {"i64x2.extend_high_i32x4_u",0xca},{"i64x2.shl",0xcb},
    {"i64x2.shr_s",0xcc},{"i64x2.shr_u",0xcd},{"i64x2.add",0xce},
    {"i64x2.sub",0xd1},{"i64x2.mul",0xd5},{"i64x2.eq",0xd6},
    {"i64x2.ne",0xd7},{"i64x2.lt_s",0xd8},{"i64x2.gt_s",0xd9},
    {"i64x2.le_s",0xda},{"i64x2.ge_s",0xdb},
    {"i64x2.extmul_low_i32x4_s",0xdc},{"i64x2.extmul_high_i32x4_s",0xdd},
    {"i64x2.extmul_low_i32x4_u",0xde},{"i64x2.extmul_high_i32x4_u",0xdf},
    {"f32x4.abs",0xe0},{"f32x4.neg",0xe1},{"f32x4.sqrt",0xe3},
    {"f32x4.add",0xe4},{"f32x4.sub",0xe5},{"f32x4.mul",0xe6},
    {"f32x4.div",0xe7},{"f32x4.min",0xe8},{"f32x4.max",0xe9},
    {"f32x4.pmin",0xea},{"f32x4.pmax",0xeb},{"f64x2.abs",0xec},
    {"f64x2.neg",0xed},{"f64x2.sqrt",0xef},{"f64x2.add",0xf0},
    {"f64x2.sub",0xf1},{"f64x2.mul",0xf2},{"f64x2.div",0xf3},
    {"f64x2.min",0xf4},{"f64x2.max",0xf5},{"f64x2.pmin",0xf6},
    {"f64x2.pmax",0xf7},{"i32x4.trunc_sat_f32x4_s",0xf8},
    {"i32x4.trunc_sat_f32x4_u",0xf9},{"f32x4.convert_i32x4_s",0xfa},
    {"f32x4.convert_i32x4_u",0xfb},{"i32x4.trunc_sat_f64x2_s_zero",0xfc},
    {"i32x4.trunc_sat_f64x2_u_zero",0xfd},
    {"f64x2.convert_low_i32x4_s",0xfe},{"f64x2.convert_low_i32x4_u",0xff},
    {"i8x16.relaxed_swizzle",0x100},
    {"i32x4.relaxed_trunc_f32x4_s",0x101},
    {"i32x4.relaxed_trunc_f32x4_u",0x102},
    {"i32x4.relaxed_trunc_f64x2_s_zero",0x103},
    {"i32x4.relaxed_trunc_f64x2_u_zero",0x104},
    {"f32x4.relaxed_madd",0x105},{"f32x4.relaxed_nmadd",0x106},
    {"f64x2.relaxed_madd",0x107},{"f64x2.relaxed_nmadd",0x108},
    {"i8x16.relaxed_laneselect",0x109},{"i16x8.relaxed_laneselect",0x10a},
    {"i32x4.relaxed_laneselect",0x10b},{"i64x2.relaxed_laneselect",0x10c},
    {"f32x4.relaxed_min",0x10d},{"f32x4.relaxed_max",0x10e},
    {"f64x2.relaxed_min",0x10f},{"f64x2.relaxed_max",0x110},
    {"i16x8.relaxed_q15mulr_s",0x111},
    {"i16x8.relaxed_dot_i8x16_i7x16_s",0x112},
    {"i32x4.relaxed_dot_i8x16_i7x16_add_s",0x113}
};

static void classify(uint32_t opcode, wast_simd_info *info) {
    info->opcode = opcode;
    info->immediate = WAST_SIMD_IMM_NONE;
    info->natural_alignment = 0;
    info->lane_count = 0;
    if (opcode <= 0x0b || opcode == 0x5c || opcode == 0x5d) {
        static const uint8_t aligns[] = {4,3,3,3,3,3,3,0,1,2,3,4};
        info->immediate = WAST_SIMD_IMM_MEMARG;
        info->natural_alignment = opcode <= 0x0b ? aligns[opcode] :
            (uint8_t)(opcode == 0x5c ? 2 : 3);
    } else if (opcode == 0x0c) {
        info->immediate = WAST_SIMD_IMM_CONST;
    } else if (opcode == 0x0d) {
        info->immediate = WAST_SIMD_IMM_SHUFFLE;
        info->lane_count = 16;
    } else if (opcode >= 0x15 && opcode <= 0x22) {
        static const uint8_t lanes[] = {16,16,16,8,8,8,4,4,2,2,4,4,2,2};
        info->immediate = WAST_SIMD_IMM_LANE;
        info->lane_count = lanes[opcode - 0x15];
    } else if (opcode >= 0x54 && opcode <= 0x5b) {
        static const uint8_t lanes[] = {16,8,4,2,16,8,4,2};
        info->immediate = WAST_SIMD_IMM_MEMARG_LANE;
        info->natural_alignment = (uint8_t)((opcode - 0x54) & 3u);
        info->lane_count = lanes[opcode - 0x54];
    }
}

int wast_simd_lookup(const char *name, wast_simd_info *info) {
    if (!name || !info) return 0;
    for (size_t i = 0; i < sizeof(simd_names) / sizeof(simd_names[0]); i++) {
        if (strcmp(name, simd_names[i].name) == 0) {
            classify(simd_names[i].opcode, info);
            return 1;
        }
    }
    return 0;
}

int wast_simd_get_info(uint32_t opcode, wast_simd_info *info) {
    if (!info) return 0;
    for (size_t i = 0; i < sizeof(simd_names) / sizeof(simd_names[0]); i++) {
        if (simd_names[i].opcode == opcode) {
            classify(opcode, info);
            return 1;
        }
    }
    return 0;
}

/* ---- Unified opcode metadata table ----
 *
 * Mirrors the WebAssembly spec instruction index: each entry captures the
 * type signature and class so that validation and execution can share a
 * single lookup instead of duplicating per-opcode metadata in separate
 * switch statements.
 *
 * Abbreviations used in the initialiser comments:
 *   C = CUSTOM, U = UNARY, B = BINARY, K = CONST, L = LOAD, S = STORE
 *   I32 = WASM_VALTYPE_I32 (0), I64 = 1, F32 = 2, F64 = 3
 */

#include "../include/waste.h"

#define C  WASM_OP_CLASS_CUSTOM
#define U  WASM_OP_CLASS_UNARY
#define B  WASM_OP_CLASS_BINARY
#define K  WASM_OP_CLASS_CONST
#define L  WASM_OP_CLASS_LOAD
#define S  WASM_OP_CLASS_STORE
#define MS WASM_OP_CLASS_MEMORY_SIZE
#define MG WASM_OP_CLASS_MEMORY_GROW
#define X  WASM_OP_CLASS_INVALID

#define I32 WASM_VALTYPE_I32
#define I64 WASM_VALTYPE_I64
#define F32 WASM_VALTYPE_F32
#define F64 WASM_VALTYPE_F64

/* {op_class, operand_type, result_type, load_width, natural_align, sign_extend} */

static const wasm_opcode_info wasm_opcode_table[256] = {
    /* 0x00 unreachable  */ { C,  0,   0,   0, 0, 0 },
    /* 0x01 nop          */ { C,  0,   0,   0, 0, 0 },
    /* 0x02 block        */ { C,  0,   0,   0, 0, 0 },
    /* 0x03 loop         */ { C,  0,   0,   0, 0, 0 },
    /* 0x04 if           */ { C,  0,   0,   0, 0, 0 },
    /* 0x05 else         */ { C,  0,   0,   0, 0, 0 },
    /* 0x06              */ { X,  0,   0,   0, 0, 0 },
    /* 0x07              */ { X,  0,   0,   0, 0, 0 },
    /* 0x08 throw        */ { C,  0,   0,   0, 0, 0 },
    /* 0x09              */ { X,  0,   0,   0, 0, 0 },
    /* 0x0a throw_ref    */ { C,  0,   0,   0, 0, 0 },
    /* 0x0b end          */ { C,  0,   0,   0, 0, 0 },
    /* 0x0c br           */ { C,  0,   0,   0, 0, 0 },
    /* 0x0d br_if        */ { C,  0,   0,   0, 0, 0 },
    /* 0x0e br_table     */ { C,  0,   0,   0, 0, 0 },
    /* 0x0f return       */ { C,  0,   0,   0, 0, 0 },
    /* 0x10 call         */ { C,  0,   0,   0, 0, 0 },
    /* 0x11 call_ind     */ { C,  0,   0,   0, 0, 0 },
    /* 0x12 return_call  */ { C,  0,   0,   0, 0, 0 },
    /* 0x13 ret_call_ind */ { C,  0,   0,   0, 0, 0 },
    /* 0x14 call_ref     */ { C,  0,   0,   0, 0, 0 },
    /* 0x15 ret_call_ref */ { C,  0,   0,   0, 0, 0 },
    /* 0x16              */ { X,  0,   0,   0, 0, 0 },
    /* 0x17              */ { X,  0,   0,   0, 0, 0 },
    /* 0x18              */ { X,  0,   0,   0, 0, 0 },
    /* 0x19              */ { X,  0,   0,   0, 0, 0 },
    /* 0x1a drop         */ { C,  0,   0,   0, 0, 0 },
    /* 0x1b select       */ { C,  0,   0,   0, 0, 0 },
    /* 0x1c select_t     */ { C,  0,   0,   0, 0, 0 },
    /* 0x1d              */ { X,  0,   0,   0, 0, 0 },
    /* 0x1e              */ { X,  0,   0,   0, 0, 0 },
    /* 0x1f try_table    */ { C,  0,   0,   0, 0, 0 },

    /* 0x20 local.get    */ { C,  0,   0,   0, 0, 0 },
    /* 0x21 local.set    */ { C,  0,   0,   0, 0, 0 },
    /* 0x22 local.tee    */ { C,  0,   0,   0, 0, 0 },
    /* 0x23 global.get   */ { C,  0,   0,   0, 0, 0 },
    /* 0x24 global.set   */ { C,  0,   0,   0, 0, 0 },
    /* 0x25 table.get    */ { C,  0,   0,   0, 0, 0 },
    /* 0x26 table.set    */ { C,  0,   0,   0, 0, 0 },
    /* 0x27              */ { X,  0,   0,   0, 0, 0 },

    /* Memory loads: {LOAD, 0, result_type, width, log2_align, sign_extend}
     * operand_type unused for LOAD/STORE — address type is runtime (memory->is_64) */
    /* 0x28 i32.load     */ { L,  0, I32, 4, 2, 0 },
    /* 0x29 i64.load     */ { L,  0, I64, 8, 3, 0 },
    /* 0x2a f32.load     */ { L,  0, F32, 4, 2, 0 },
    /* 0x2b f64.load     */ { L,  0, F64, 8, 3, 0 },
    /* 0x2c i32.load8_s  */ { L,  0, I32, 1, 0, 1 },
    /* 0x2d i32.load8_u  */ { L,  0, I32, 1, 0, 0 },
    /* 0x2e i32.load16_s */ { L,  0, I32, 2, 1, 1 },
    /* 0x2f i32.load16_u */ { L,  0, I32, 2, 1, 0 },
    /* 0x30 i64.load8_s  */ { L,  0, I64, 1, 0, 1 },
    /* 0x31 i64.load8_u  */ { L,  0, I64, 1, 0, 0 },
    /* 0x32 i64.load16_s */ { L,  0, I64, 2, 1, 1 },
    /* 0x33 i64.load16_u */ { L,  0, I64, 2, 1, 0 },
    /* 0x34 i64.load32_s */ { L,  0, I64, 4, 2, 1 },
    /* 0x35 i64.load32_u */ { L,  0, I64, 4, 2, 0 },

    /* Memory stores: {STORE, operand_type, 0, width, log2_align, 0} */
    /* 0x36 i32.store    */ { S, I32, 0, 4, 2, 0 },
    /* 0x37 i64.store    */ { S, I64, 0, 8, 3, 0 },
    /* 0x38 f32.store    */ { S, F32, 0, 4, 2, 0 },
    /* 0x39 f64.store    */ { S, F64, 0, 8, 3, 0 },
    /* 0x3a i32.store8   */ { S, I32, 0, 1, 0, 0 },
    /* 0x3b i32.store16  */ { S, I32, 0, 2, 1, 0 },
    /* 0x3c i64.store8   */ { S, I64, 0, 1, 0, 0 },
    /* 0x3d i64.store16  */ { S, I64, 0, 2, 1, 0 },
    /* 0x3e i64.store32  */ { S, I64, 0, 4, 2, 0 },

    /* 0x3f memory.size  */ { MS, 0,  0,  0, 0, 0 },
    /* 0x40 memory.grow  */ { MG, 0,  0,  0, 0, 0 },

    /* Constants */
    /* 0x41 i32.const    */ { K,  0, I32, 0, 0, 0 },
    /* 0x42 i64.const    */ { K,  0, I64, 0, 0, 0 },
    /* 0x43 f32.const    */ { K,  0, F32, 0, 0, 0 },
    /* 0x44 f64.const    */ { K,  0, F64, 0, 0, 0 },

    /* i32 comparison/test */
    /* 0x45 i32.eqz      */ { U, I32, I32, 0, 0, 0 },
    /* 0x46 i32.eq       */ { B, I32, I32, 0, 0, 0 },
    /* 0x47 i32.ne       */ { B, I32, I32, 0, 0, 0 },
    /* 0x48 i32.lt_s     */ { B, I32, I32, 0, 0, 0 },
    /* 0x49 i32.lt_u     */ { B, I32, I32, 0, 0, 0 },
    /* 0x4a i32.gt_s     */ { B, I32, I32, 0, 0, 0 },
    /* 0x4b i32.gt_u     */ { B, I32, I32, 0, 0, 0 },
    /* 0x4c i32.le_s     */ { B, I32, I32, 0, 0, 0 },
    /* 0x4d i32.le_u     */ { B, I32, I32, 0, 0, 0 },
    /* 0x4e i32.ge_s     */ { B, I32, I32, 0, 0, 0 },
    /* 0x4f i32.ge_u     */ { B, I32, I32, 0, 0, 0 },

    /* i64 comparison/test */
    /* 0x50 i64.eqz      */ { U, I64, I32, 0, 0, 0 },
    /* 0x51 i64.eq       */ { B, I64, I32, 0, 0, 0 },
    /* 0x52 i64.ne       */ { B, I64, I32, 0, 0, 0 },
    /* 0x53 i64.lt_s     */ { B, I64, I32, 0, 0, 0 },
    /* 0x54 i64.lt_u     */ { B, I64, I32, 0, 0, 0 },
    /* 0x55 i64.gt_s     */ { B, I64, I32, 0, 0, 0 },
    /* 0x56 i64.gt_u     */ { B, I64, I32, 0, 0, 0 },
    /* 0x57 i64.le_s     */ { B, I64, I32, 0, 0, 0 },
    /* 0x58 i64.le_u     */ { B, I64, I32, 0, 0, 0 },
    /* 0x59 i64.ge_s     */ { B, I64, I32, 0, 0, 0 },
    /* 0x5a i64.ge_u     */ { B, I64, I32, 0, 0, 0 },

    /* f32 comparison */
    /* 0x5b f32.eq       */ { B, F32, I32, 0, 0, 0 },
    /* 0x5c f32.ne       */ { B, F32, I32, 0, 0, 0 },
    /* 0x5d f32.lt       */ { B, F32, I32, 0, 0, 0 },
    /* 0x5e f32.gt       */ { B, F32, I32, 0, 0, 0 },
    /* 0x5f f32.le       */ { B, F32, I32, 0, 0, 0 },
    /* 0x60 f32.ge       */ { B, F32, I32, 0, 0, 0 },

    /* f64 comparison */
    /* 0x61 f64.eq       */ { B, F64, I32, 0, 0, 0 },
    /* 0x62 f64.ne       */ { B, F64, I32, 0, 0, 0 },
    /* 0x63 f64.lt       */ { B, F64, I32, 0, 0, 0 },
    /* 0x64 f64.gt       */ { B, F64, I32, 0, 0, 0 },
    /* 0x65 f64.le       */ { B, F64, I32, 0, 0, 0 },
    /* 0x66 f64.ge       */ { B, F64, I32, 0, 0, 0 },

    /* i32 unary */
    /* 0x67 i32.clz      */ { U, I32, I32, 0, 0, 0 },
    /* 0x68 i32.ctz      */ { U, I32, I32, 0, 0, 0 },
    /* 0x69 i32.popcnt   */ { U, I32, I32, 0, 0, 0 },

    /* i32 binary */
    /* 0x6a i32.add      */ { B, I32, I32, 0, 0, 0 },
    /* 0x6b i32.sub      */ { B, I32, I32, 0, 0, 0 },
    /* 0x6c i32.mul      */ { B, I32, I32, 0, 0, 0 },
    /* 0x6d i32.div_s    */ { B, I32, I32, 0, 0, 0 },
    /* 0x6e i32.div_u    */ { B, I32, I32, 0, 0, 0 },
    /* 0x6f i32.rem_s    */ { B, I32, I32, 0, 0, 0 },
    /* 0x70 i32.rem_u    */ { B, I32, I32, 0, 0, 0 },
    /* 0x71 i32.and      */ { B, I32, I32, 0, 0, 0 },
    /* 0x72 i32.or       */ { B, I32, I32, 0, 0, 0 },
    /* 0x73 i32.xor      */ { B, I32, I32, 0, 0, 0 },
    /* 0x74 i32.shl      */ { B, I32, I32, 0, 0, 0 },
    /* 0x75 i32.shr_s    */ { B, I32, I32, 0, 0, 0 },
    /* 0x76 i32.shr_u    */ { B, I32, I32, 0, 0, 0 },
    /* 0x77 i32.rotl     */ { B, I32, I32, 0, 0, 0 },
    /* 0x78 i32.rotr     */ { B, I32, I32, 0, 0, 0 },

    /* i64 unary */
    /* 0x79 i64.clz      */ { U, I64, I64, 0, 0, 0 },
    /* 0x7a i64.ctz      */ { U, I64, I64, 0, 0, 0 },
    /* 0x7b i64.popcnt   */ { U, I64, I64, 0, 0, 0 },

    /* i64 binary */
    /* 0x7c i64.add      */ { B, I64, I64, 0, 0, 0 },
    /* 0x7d i64.sub      */ { B, I64, I64, 0, 0, 0 },
    /* 0x7e i64.mul      */ { B, I64, I64, 0, 0, 0 },
    /* 0x7f i64.div_s    */ { B, I64, I64, 0, 0, 0 },
    /* 0x80 i64.div_u    */ { B, I64, I64, 0, 0, 0 },
    /* 0x81 i64.rem_s    */ { B, I64, I64, 0, 0, 0 },
    /* 0x82 i64.rem_u    */ { B, I64, I64, 0, 0, 0 },
    /* 0x83 i64.and      */ { B, I64, I64, 0, 0, 0 },
    /* 0x84 i64.or       */ { B, I64, I64, 0, 0, 0 },
    /* 0x85 i64.xor      */ { B, I64, I64, 0, 0, 0 },
    /* 0x86 i64.shl      */ { B, I64, I64, 0, 0, 0 },
    /* 0x87 i64.shr_s    */ { B, I64, I64, 0, 0, 0 },
    /* 0x88 i64.shr_u    */ { B, I64, I64, 0, 0, 0 },
    /* 0x89 i64.rotl     */ { B, I64, I64, 0, 0, 0 },
    /* 0x8a i64.rotr     */ { B, I64, I64, 0, 0, 0 },

    /* f32 unary */
    /* 0x8b f32.abs      */ { U, F32, F32, 0, 0, 0 },
    /* 0x8c f32.neg      */ { U, F32, F32, 0, 0, 0 },
    /* 0x8d f32.ceil     */ { U, F32, F32, 0, 0, 0 },
    /* 0x8e f32.floor    */ { U, F32, F32, 0, 0, 0 },
    /* 0x8f f32.trunc    */ { U, F32, F32, 0, 0, 0 },
    /* 0x90 f32.nearest  */ { U, F32, F32, 0, 0, 0 },
    /* 0x91 f32.sqrt     */ { U, F32, F32, 0, 0, 0 },

    /* f32 binary */
    /* 0x92 f32.add      */ { B, F32, F32, 0, 0, 0 },
    /* 0x93 f32.sub      */ { B, F32, F32, 0, 0, 0 },
    /* 0x94 f32.mul      */ { B, F32, F32, 0, 0, 0 },
    /* 0x95 f32.div      */ { B, F32, F32, 0, 0, 0 },
    /* 0x96 f32.min      */ { B, F32, F32, 0, 0, 0 },
    /* 0x97 f32.max      */ { B, F32, F32, 0, 0, 0 },
    /* 0x98 f32.copysign */ { B, F32, F32, 0, 0, 0 },

    /* f64 unary */
    /* 0x99 f64.abs      */ { U, F64, F64, 0, 0, 0 },
    /* 0x9a f64.neg      */ { U, F64, F64, 0, 0, 0 },
    /* 0x9b f64.ceil     */ { U, F64, F64, 0, 0, 0 },
    /* 0x9c f64.floor    */ { U, F64, F64, 0, 0, 0 },
    /* 0x9d f64.trunc    */ { U, F64, F64, 0, 0, 0 },
    /* 0x9e f64.nearest  */ { U, F64, F64, 0, 0, 0 },
    /* 0x9f f64.sqrt     */ { U, F64, F64, 0, 0, 0 },

    /* f64 binary */
    /* 0xa0 f64.add      */ { B, F64, F64, 0, 0, 0 },
    /* 0xa1 f64.sub      */ { B, F64, F64, 0, 0, 0 },
    /* 0xa2 f64.mul      */ { B, F64, F64, 0, 0, 0 },
    /* 0xa3 f64.div      */ { B, F64, F64, 0, 0, 0 },
    /* 0xa4 f64.min      */ { B, F64, F64, 0, 0, 0 },
    /* 0xa5 f64.max      */ { B, F64, F64, 0, 0, 0 },
    /* 0xa6 f64.copysign */ { B, F64, F64, 0, 0, 0 },

    /* Conversions */
    /* 0xa7 i32.wrap_i64       */ { U, I64, I32, 0, 0, 0 },
    /* 0xa8 i32.trunc_f32_s    */ { U, F32, I32, 0, 0, 0 },
    /* 0xa9 i32.trunc_f32_u    */ { U, F32, I32, 0, 0, 0 },
    /* 0xaa i32.trunc_f64_s    */ { U, F64, I32, 0, 0, 0 },
    /* 0xab i32.trunc_f64_u    */ { U, F64, I32, 0, 0, 0 },
    /* 0xac i64.extend_i32_s   */ { U, I32, I64, 0, 0, 0 },
    /* 0xad i64.extend_i32_u   */ { U, I32, I64, 0, 0, 0 },
    /* 0xae i64.trunc_f32_s    */ { U, F32, I64, 0, 0, 0 },
    /* 0xaf i64.trunc_f32_u    */ { U, F32, I64, 0, 0, 0 },
    /* 0xb0 i64.trunc_f64_s    */ { U, F64, I64, 0, 0, 0 },
    /* 0xb1 i64.trunc_f64_u    */ { U, F64, I64, 0, 0, 0 },
    /* 0xb2 f32.convert_i32_s  */ { U, I32, F32, 0, 0, 0 },
    /* 0xb3 f32.convert_i32_u  */ { U, I32, F32, 0, 0, 0 },
    /* 0xb4 f32.convert_i64_s  */ { U, I64, F32, 0, 0, 0 },
    /* 0xb5 f32.convert_i64_u  */ { U, I64, F32, 0, 0, 0 },
    /* 0xb6 f32.demote_f64     */ { U, F64, F32, 0, 0, 0 },
    /* 0xb7 f64.convert_i32_s  */ { U, I32, F64, 0, 0, 0 },
    /* 0xb8 f64.convert_i32_u  */ { U, I32, F64, 0, 0, 0 },
    /* 0xb9 f64.convert_i64_s  */ { U, I64, F64, 0, 0, 0 },
    /* 0xba f64.convert_i64_u  */ { U, I64, F64, 0, 0, 0 },
    /* 0xbb f64.promote_f32    */ { U, F32, F64, 0, 0, 0 },
    /* 0xbc i32.reinterpret_f32*/ { U, F32, I32, 0, 0, 0 },
    /* 0xbd i64.reinterpret_f64*/ { U, F64, I64, 0, 0, 0 },
    /* 0xbe f32.reinterpret_i32*/ { U, I32, F32, 0, 0, 0 },
    /* 0xbf f64.reinterpret_i64*/ { U, I64, F64, 0, 0, 0 },

    /* i32 sign-extension */
    /* 0xc0 i32.extend8_s  */ { U, I32, I32, 0, 0, 0 },
    /* 0xc1 i32.extend16_s */ { U, I32, I32, 0, 0, 0 },

    /* i64 sign-extension */
    /* 0xc2 i64.extend8_s  */ { U, I64, I64, 0, 0, 0 },
    /* 0xc3 i64.extend16_s */ { U, I64, I64, 0, 0, 0 },
    /* 0xc4 i64.extend32_s */ { U, I64, I64, 0, 0, 0 },

    /* 0xc5-0xcf unused */
    { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 },
    { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 },
    { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 },

    /* References */
    /* 0xd0 ref.null      */ { C, 0, 0, 0, 0, 0 },
    /* 0xd1 ref.is_null   */ { C, 0, 0, 0, 0, 0 },
    /* 0xd2 ref.func      */ { C, 0, 0, 0, 0, 0 },
    /* 0xd3 ref.eq        */ { C, 0, 0, 0, 0, 0 },
    /* 0xd4 ref.as_non_null */ { C, 0, 0, 0, 0, 0 },
    /* 0xd5 br_on_null    */ { C, 0, 0, 0, 0, 0 },
    /* 0xd6 br_on_non_null*/ { C, 0, 0, 0, 0, 0 },

    /* 0xd7-0xfa unused */
    { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 },
    { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 },
    { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 },
    { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 },
    { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 },
    { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 },
    { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 },
    { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 },
    { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 }, { X,0,0,0,0,0 },

    /* Prefix opcodes */
    /* 0xfb GC prefix     */ { C, 0, 0, 0, 0, 0 },
    /* 0xfc misc prefix   */ { C, 0, 0, 0, 0, 0 },
    /* 0xfd SIMD prefix   */ { C, 0, 0, 0, 0, 0 },
    /* 0xfe              */ { X, 0, 0, 0, 0, 0 },
    /* 0xff              */ { X, 0, 0, 0, 0, 0 },
};

/* 0xFC sub-opcodes 0-7: saturating truncation */
static const wasm_opcode_info wasm_opcode_fc_table[8] = {
    /* 0 i32.trunc_sat_f32_s */ { U, F32, I32, 0, 0, 0 },
    /* 1 i32.trunc_sat_f32_u */ { U, F32, I32, 0, 0, 0 },
    /* 2 i32.trunc_sat_f64_s */ { U, F64, I32, 0, 0, 0 },
    /* 3 i32.trunc_sat_f64_u */ { U, F64, I32, 0, 0, 0 },
    /* 4 i64.trunc_sat_f32_s */ { U, F32, I64, 0, 0, 0 },
    /* 5 i64.trunc_sat_f32_u */ { U, F32, I64, 0, 0, 0 },
    /* 6 i64.trunc_sat_f64_s */ { U, F64, I64, 0, 0, 0 },
    /* 7 i64.trunc_sat_f64_u */ { U, F64, I64, 0, 0, 0 },
};

#undef C
#undef U
#undef B
#undef K
#undef L
#undef S
#undef MS
#undef MG
#undef X
#undef I32
#undef I64
#undef F32
#undef F64

const wasm_opcode_info *wasm_opcode_get_info(uint32_t opcode) {
    if (opcode > 0xff) return NULL;
    return &wasm_opcode_table[opcode];
}

const wasm_opcode_info *wasm_opcode_fc_get_info(uint32_t sub_opcode) {
    if (sub_opcode > 7) return NULL;
    return &wasm_opcode_fc_table[sub_opcode];
}
