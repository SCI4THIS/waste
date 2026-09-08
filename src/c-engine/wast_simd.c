#include "wast_simd.h"

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
