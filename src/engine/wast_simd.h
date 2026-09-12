#ifndef WAST_SIMD_H
#define WAST_SIMD_H

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

#endif
