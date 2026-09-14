#ifndef WASTE_VALUE_H
#define WASTE_VALUE_H

#include <stdint.h>

typedef struct { uint8_t bytes[16]; } wasm_v128;

typedef enum {
    WASM_VALTYPE_I32 = 0,
    WASM_VALTYPE_I64,
    WASM_VALTYPE_F32,
    WASM_VALTYPE_F64,
    WASM_VALTYPE_V128,
    WASM_VALTYPE_FUNCREF,
    WASM_VALTYPE_EXTERNREF,
    WASM_VALTYPE_FUNCREF_NONNULL,
    WASM_VALTYPE_EXTERNREF_NONNULL,
    WASM_VALTYPE_ANYREF,
    WASM_VALTYPE_EQREF,
    WASM_VALTYPE_I31REF,
    WASM_VALTYPE_STRUCTREF,
    WASM_VALTYPE_ARRAYREF,
    WASM_VALTYPE_ANYREF_NONNULL,
    WASM_VALTYPE_EQREF_NONNULL,
    WASM_VALTYPE_I31REF_NONNULL,
    WASM_VALTYPE_STRUCTREF_NONNULL,
    WASM_VALTYPE_ARRAYREF_NONNULL,
    WASM_VALTYPE_EXNREF,
    WASM_VALTYPE_EXNREF_NONNULL,
    WASM_VALTYPE_NULLREF,
    WASM_VALTYPE_NULLFUNCREF,
    WASM_VALTYPE_NULLEXNREF,
    WASM_VALTYPE_NULLEXTERNREF
} wasm_valtype;

#define WASM_VALTYPE_TYPE_REF_NULL_BASE 0x100
#define WASM_VALTYPE_TYPE_REF_BASE      0x200
#define WASM_VALTYPE_TYPE_REF_LIMIT     0x300
#define WASM_VALTYPE_IS_TYPE_REF(t) \
    ((unsigned)(t) >= WASM_VALTYPE_TYPE_REF_NULL_BASE && \
     (unsigned)(t) < WASM_VALTYPE_TYPE_REF_LIMIT)
#define WASM_VALTYPE_TYPE_REF_INDEX(t) ((unsigned)(t) & 0xffu)

#define NAN_MATCH_EXACT      0
#define NAN_MATCH_F32_CANON  1
#define NAN_MATCH_F32_ARITH  2
#define NAN_MATCH_F64_CANON  3
#define NAN_MATCH_F64_ARITH  4
#define REF_MATCH_NULL       255

typedef struct {
    wasm_valtype type;
    union {
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;
        wasm_v128 v128;
        uint32_t ref;
    };
    uint8_t nan_mode[16];
} wasm_value;

typedef wasm_valtype waste_value_type;
typedef wasm_v128 waste_v128;
typedef wasm_value waste_value;

#endif /* WASTE_VALUE_H */
