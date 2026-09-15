#include "../runtime_internal.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static uint32_t rotl32(uint32_t value, uint32_t count) {
    count &= 31u;
    return count ? (value << count) | (value >> (32u - count)) : value;
}

static uint32_t rotr32(uint32_t value, uint32_t count) {
    count &= 31u;
    return count ? (value >> count) | (value << (32u - count)) : value;
}

/* ---- helper: raise-to-i64 for trunc operations ---- */
uint32_t trunc_sat_i32_s_f32(float v) {
    if (v != v) return 0;
    if (v <= -2147483648.0f) return UINT32_C(0x80000000);
    if (v >= 2147483648.0f) return UINT32_C(0x7fffffff);
    return (uint32_t)(int32_t)v;
}
uint32_t trunc_sat_i32_u_f32(float v) {
    if (v != v || v <= 0.0f) return 0;
    if (v >= 4294967296.0f) return UINT32_MAX;
    return (uint32_t)v;
}
uint32_t trunc_sat_i32_s_f64(double v) {
    if (v != v) return 0;
    if (v <= -2147483648.0) return UINT32_C(0x80000000);
    if (v >= 2147483648.0) return UINT32_C(0x7fffffff);
    return (uint32_t)(int32_t)v;
}
uint32_t trunc_sat_i32_u_f64(double v) {
    if (v != v || v <= 0.0) return 0;
    if (v >= 4294967296.0) return UINT32_MAX;
    return (uint32_t)v;
}
uint64_t trunc_sat_i64_s_f32(float v) {
    if (v != v) return 0;
    if (v <= -9223372036854775808.0f)
        return UINT64_C(0x8000000000000000);
    if (v >= 9223372036854775808.0f)
        return UINT64_C(0x7fffffffffffffff);
    return (uint64_t)(int64_t)v;
}
uint64_t trunc_sat_i64_u_f32(float v) {
    if (v != v || v <= 0.0f) return 0;
    if (v >= 18446744073709551616.0f) return UINT64_MAX;
    return (uint64_t)v;
}
uint64_t trunc_sat_i64_s_f64(double v) {
    if (v != v) return 0;
    if (v <= -9223372036854775808.0)
        return UINT64_C(0x8000000000000000);
    if (v >= 9223372036854775808.0)
        return UINT64_C(0x7fffffffffffffff);
    return (uint64_t)(int64_t)v;
}
uint64_t trunc_sat_i64_u_f64(double v) {
    if (v != v || v <= 0.0) return 0;
    if (v >= 18446744073709551616.0) return UINT64_MAX;
    return (uint64_t)v;
}


/* ---- Numeric opcode handlers ---- */

exec_status exec_i64_numeric(uint32_t opcode, exec_stack *stack,
                                    exec_error *err) {
    wasm_value left, right;
    uint64_t a = 0, b;
    int unary = opcode == 0x50 || opcode == 0x79 || opcode == 0x7a ||
                opcode == 0x7b || opcode == 0xc2 || opcode == 0xc3 || opcode == 0xc4;
    if (!stack_pop(stack, &right) || right.type != WASM_VALTYPE_I64)
        return exec_fail(err, EXEC_ERROR_TRAP, "i64 operand missing");
    b = (uint64_t)right.i64;
    if (!unary) {
        if (!stack_pop(stack, &left) || left.type != WASM_VALTYPE_I64)
            return exec_fail(err, EXEC_ERROR_TRAP, "i64 operand missing");
        a = (uint64_t)left.i64;
    }
    /* i64 comparison ops → push i32 result */
    if (opcode <= 0x5a) {
        int32_t result = 0;
        switch (opcode) {
            case 0x50: result = b == 0; break;
            case 0x51: result = a == b; break;
            case 0x52: result = a != b; break;
            case 0x53: result = (int64_t)a < (int64_t)b; break;
            case 0x54: result = a < b; break;
            case 0x55: result = (int64_t)a > (int64_t)b; break;
            case 0x56: result = a > b; break;
            case 0x57: result = (int64_t)a <= (int64_t)b; break;
            case 0x58: result = a <= b; break;
            case 0x59: result = (int64_t)a >= (int64_t)b; break;
            case 0x5a: result = a >= b; break;
            default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported i64 cmp");
        }
        if (!stack_push(stack, i32_value((uint32_t)result)))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }
    /* i64 arithmetic → push i64 result */
    uint64_t result = 0;
    switch (opcode) {
        case 0x79: result = b ? (uint64_t)__builtin_clzll(b) : 64u; break;
        case 0x7a: result = b ? (uint64_t)__builtin_ctzll(b) : 64u; break;
        case 0x7b: result = (uint64_t)__builtin_popcountll(b); break;
        case 0x7c: result = a + b; break;
        case 0x7d: result = a - b; break;
        case 0x7e: result = a * b; break;
        case 0x7f:
            if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero");
            if (a == (uint64_t)INT64_MIN && b == UINT64_MAX)
                return exec_fail(err, EXEC_ERROR_TRAP, "integer overflow");
            result = (uint64_t)((int64_t)a / (int64_t)b); break;
        case 0x80: if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero"); result = a / b; break;
        case 0x81:
            if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero");
            result = (a == (uint64_t)INT64_MIN && b == UINT64_MAX) ? 0 : (uint64_t)((int64_t)a % (int64_t)b); break;
        case 0x82: if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero"); result = a % b; break;
        case 0x83: result = a & b; break;
        case 0x84: result = a | b; break;
        case 0x85: result = a ^ b; break;
        case 0x86: result = a << (b & 63u); break;
        case 0x87: result = (uint64_t)((int64_t)a >> (b & 63u)); break;
        case 0x88: result = a >> (b & 63u); break;
        case 0x89: { uint32_t n = (uint32_t)(b & 63u); result = n ? (a << n) | (a >> (64u - n)) : a; break; }
        case 0x8a: { uint32_t n = (uint32_t)(b & 63u); result = n ? (a >> n) | (a << (64u - n)) : a; break; }
        case 0xc2: result = (uint64_t)(int64_t)(int8_t)b; break;
        case 0xc3: result = (uint64_t)(int64_t)(int16_t)b; break;
        case 0xc4: result = (uint64_t)(int64_t)(int32_t)b; break;
        default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported i64 opcode");
    }
    wasm_value out; memset(&out, 0, sizeof(out));
    out.type = WASM_VALTYPE_I64; out.i64 = (int64_t)result;
    if (!stack_push(stack, out))
        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

exec_status exec_f32_numeric(uint32_t opcode, exec_stack *stack,
                                    exec_error *err) {
    wasm_value left, right;
    float a = 0.0f, b;
    int unary = opcode == 0x8b || opcode == 0x8c || opcode == 0x8d ||
                opcode == 0x8e || opcode == 0x8f || opcode == 0x90 || opcode == 0x91;
    /* comparisons are always binary */
    int is_cmp = opcode >= 0x5b && opcode <= 0x60;
    if (!stack_pop(stack, &right) || right.type != WASM_VALTYPE_F32)
        return exec_fail(err, EXEC_ERROR_TRAP, "f32 operand missing");
    memcpy(&b, &right.f32, 4);
    if (!unary || is_cmp) {
        if (!stack_pop(stack, &left) || left.type != WASM_VALTYPE_F32)
            return exec_fail(err, EXEC_ERROR_TRAP, "f32 operand missing");
        memcpy(&a, &left.f32, 4);
    }
    if (is_cmp) {
        int32_t result = 0;
        switch (opcode) {
            case 0x5b: result = a == b; break;
            case 0x5c: result = a != b; break;
            case 0x5d: result = a <  b; break;
            case 0x5e: result = a >  b; break;
            case 0x5f: result = a <= b; break;
            case 0x60: result = a >= b; break;
            default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported f32 cmp");
        }
        if (!stack_push(stack, i32_value((uint32_t)result)))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }
    float result;
    switch (opcode) {
        case 0x8b: result = fabsf(b); break;
        case 0x8c: result = -b; break;
        case 0x8d: result = ceilf(b); break;
        case 0x8e: result = floorf(b); break;
        case 0x8f: result = truncf(b); break;
        case 0x90: result = nearbyintf(b); break;
        case 0x91: result = sqrtf(b); break;
        case 0x92: result = a + b; break;
        case 0x93: result = a - b; break;
        case 0x94: result = a * b; break;
        case 0x95: result = a / b; break;
        case 0x96: { /* f32.min: Wasm semantics — NaN → canonical NaN, -0 < +0 */
            uint32_t ai, bi;
            memcpy(&ai, &a, 4); memcpy(&bi, &b, 4);
            int a_nan = ((ai & 0x7F800000u) == 0x7F800000u) && (ai & 0x007FFFFFu);
            int b_nan = ((bi & 0x7F800000u) == 0x7F800000u) && (bi & 0x007FFFFFu);
            if (a_nan || b_nan) { uint32_t r = 0x7FC00000u; memcpy(&result, &r, 4); }
            else if (a == 0.0f && b == 0.0f) { /* -0 vs +0: pick most-negative */
                uint32_t r = ai | bi; memcpy(&result, &r, 4);
            } else result = a < b ? a : b;
            break;
        }
        case 0x97: { /* f32.max: Wasm semantics — NaN → canonical NaN, +0 > -0 */
            uint32_t ai, bi;
            memcpy(&ai, &a, 4); memcpy(&bi, &b, 4);
            int a_nan = ((ai & 0x7F800000u) == 0x7F800000u) && (ai & 0x007FFFFFu);
            int b_nan = ((bi & 0x7F800000u) == 0x7F800000u) && (bi & 0x007FFFFFu);
            if (a_nan || b_nan) { uint32_t r = 0x7FC00000u; memcpy(&result, &r, 4); }
            else if (a == 0.0f && b == 0.0f) { /* -0 vs +0: pick most-positive */
                uint32_t r = ai & bi; memcpy(&result, &r, 4);
            } else result = a > b ? a : b;
            break;
        }
        case 0x98: { /* f32.copysign */
            uint32_t ai, bi; memcpy(&ai, &a, 4); memcpy(&bi, &b, 4);
            uint32_t ri = (ai & 0x7fffffffu) | (bi & 0x80000000u);
            memcpy(&result, &ri, 4); break;
        }
        default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported f32 opcode");
    }
    wasm_value out; memset(&out, 0, sizeof(out));
    out.type = WASM_VALTYPE_F32; memcpy(&out.f32, &result, 4);
    if (!stack_push(stack, out))
        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

exec_status exec_f64_numeric(uint32_t opcode, exec_stack *stack,
                                    exec_error *err) {
    wasm_value left, right;
    double a = 0.0, b;
    int unary = opcode == 0x99 || opcode == 0x9a || opcode == 0x9b ||
                opcode == 0x9c || opcode == 0x9d || opcode == 0x9e || opcode == 0x9f;
    int is_cmp = opcode >= 0x61 && opcode <= 0x66;
    if (!stack_pop(stack, &right) || right.type != WASM_VALTYPE_F64)
        return exec_fail(err, EXEC_ERROR_TRAP, "f64 operand missing");
    memcpy(&b, &right.f64, 8);
    if (!unary || is_cmp) {
        if (!stack_pop(stack, &left) || left.type != WASM_VALTYPE_F64)
            return exec_fail(err, EXEC_ERROR_TRAP, "f64 operand missing");
        memcpy(&a, &left.f64, 8);
    }
    if (is_cmp) {
        int32_t result = 0;
        switch (opcode) {
            case 0x61: result = a == b; break;
            case 0x62: result = a != b; break;
            case 0x63: result = a <  b; break;
            case 0x64: result = a >  b; break;
            case 0x65: result = a <= b; break;
            case 0x66: result = a >= b; break;
            default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported f64 cmp");
        }
        if (!stack_push(stack, i32_value((uint32_t)result)))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }
    double result;
    switch (opcode) {
        case 0x99: result = fabs(b); break;
        case 0x9a: result = -b; break;
        case 0x9b: result = ceil(b); break;
        case 0x9c: result = floor(b); break;
        case 0x9d: result = trunc(b); break;
        case 0x9e: result = nearbyint(b); break;
        case 0x9f: result = sqrt(b); break;
        case 0xa0: result = a + b; break;
        case 0xa1: result = a - b; break;
        case 0xa2: result = a * b; break;
        case 0xa3: result = a / b; break;
        case 0xa4: { /* f64.min: Wasm semantics */
            uint64_t ai, bi;
            memcpy(&ai, &a, 8); memcpy(&bi, &b, 8);
            int a_nan = ((ai & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (ai & 0x000FFFFFFFFFFFFFULL);
            int b_nan = ((bi & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (bi & 0x000FFFFFFFFFFFFFULL);
            if (a_nan || b_nan) { uint64_t r = 0x7FF8000000000000ULL; memcpy(&result, &r, 8); }
            else if (a == 0.0 && b == 0.0) { uint64_t r = ai | bi; memcpy(&result, &r, 8); }
            else result = a < b ? a : b;
            break;
        }
        case 0xa5: { /* f64.max: Wasm semantics */
            uint64_t ai, bi;
            memcpy(&ai, &a, 8); memcpy(&bi, &b, 8);
            int a_nan = ((ai & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (ai & 0x000FFFFFFFFFFFFFULL);
            int b_nan = ((bi & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (bi & 0x000FFFFFFFFFFFFFULL);
            if (a_nan || b_nan) { uint64_t r = 0x7FF8000000000000ULL; memcpy(&result, &r, 8); }
            else if (a == 0.0 && b == 0.0) { uint64_t r = ai & bi; memcpy(&result, &r, 8); }
            else result = a > b ? a : b;
            break;
        }
        case 0xa6: { /* f64.copysign */
            uint64_t ai, bi; memcpy(&ai, &a, 8); memcpy(&bi, &b, 8);
            uint64_t ri = (ai & UINT64_C(0x7fffffffffffffff)) | (bi & UINT64_C(0x8000000000000000));
            memcpy(&result, &ri, 8); break;
        }
        default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported f64 opcode");
    }
    wasm_value out; memset(&out, 0, sizeof(out));
    out.type = WASM_VALTYPE_F64; memcpy(&out.f64, &result, 8);
    if (!stack_push(stack, out))
        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

/* Conversion ops: 0xa7-0xbf (and saturating 0xFC 0x00-0x07) */
exec_status exec_conversion(uint32_t opcode, exec_stack *stack,
                                   exec_error *err) {
    wasm_value v;
    if (!stack_pop(stack, &v))
        return exec_fail(err, EXEC_ERROR_TRAP, "conversion operand missing");
    wasm_value out; memset(&out, 0, sizeof(out));
    float f32v; double f64v; uint32_t u32; uint64_t u64;
    switch (opcode) {
        /* i32.wrap_i64 */
        case 0xa7: out.type=WASM_VALTYPE_I32; out.i32=(int32_t)(int32_t)v.i64; break;
        /* i32.trunc_f32_s */
        case 0xa8:
            memcpy(&f32v,&v.f32,4);
            if (f32v!=f32v||f32v<=-2147483904.0f||f32v>=2147483648.0f)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I32; out.i32=(int32_t)f32v; break;
        /* i32.trunc_f32_u */
        case 0xa9:
            memcpy(&f32v,&v.f32,4);
            if (f32v!=f32v||f32v<=-1.0f||f32v>=4294967296.0f)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I32; out.i32=(int32_t)(uint32_t)f32v; break;
        /* i32.trunc_f64_s */
        case 0xaa:
            memcpy(&f64v,&v.f64,8);
            if (f64v!=f64v||f64v<=-2147483649.0||f64v>=2147483648.0)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I32; out.i32=(int32_t)f64v; break;
        /* i32.trunc_f64_u */
        case 0xab:
            memcpy(&f64v,&v.f64,8);
            if (f64v!=f64v||f64v<=-1.0||f64v>=4294967296.0)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I32; out.i32=(int32_t)(uint32_t)f64v; break;
        /* i64.extend_i32_s */
        case 0xac: out.type=WASM_VALTYPE_I64; out.i64=(int64_t)(int32_t)v.i32; break;
        /* i64.extend_i32_u */
        case 0xad: out.type=WASM_VALTYPE_I64; out.i64=(int64_t)(uint32_t)(uint32_t)v.i32; break;
        /* i64.trunc_f32_s */
        case 0xae:
            memcpy(&f32v,&v.f32,4);
            if (f32v!=f32v||f32v< -9223372036854775808.0f||f32v>=9223372036854775808.0f)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I64; out.i64=(int64_t)f32v; break;
        /* i64.trunc_f32_u */
        case 0xaf:
            memcpy(&f32v,&v.f32,4);
            if (f32v!=f32v||f32v<=-1.0f||f32v>=18446744073709551616.0f)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I64; out.i64=(int64_t)(uint64_t)f32v; break;
        /* i64.trunc_f64_s */
        case 0xb0:
            memcpy(&f64v,&v.f64,8);
            if (f64v!=f64v||f64v< -9223372036854775808.0||f64v>=9223372036854775808.0)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I64; out.i64=(int64_t)f64v; break;
        /* i64.trunc_f64_u */
        case 0xb1:
            memcpy(&f64v,&v.f64,8);
            if (f64v!=f64v||f64v<=-1.0||f64v>=18446744073709551616.0)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I64; out.i64=(int64_t)(uint64_t)f64v; break;
        /* f32.convert_i32_s */
        case 0xb2: { float r=(float)(int32_t)v.i32; out.type=WASM_VALTYPE_F32; memcpy(&out.f32,&r,4); break; }
        /* f32.convert_i32_u */
        case 0xb3: { float r=(float)(uint32_t)v.i32; out.type=WASM_VALTYPE_F32; memcpy(&out.f32,&r,4); break; }
        /* f32.convert_i64_s */
        case 0xb4: { float r=(float)(int64_t)v.i64; out.type=WASM_VALTYPE_F32; memcpy(&out.f32,&r,4); break; }
        /* f32.convert_i64_u */
        case 0xb5: { float r=(float)(uint64_t)v.i64; out.type=WASM_VALTYPE_F32; memcpy(&out.f32,&r,4); break; }
        /* f32.demote_f64 */
        case 0xb6: { memcpy(&f64v,&v.f64,8); float r=(float)f64v; out.type=WASM_VALTYPE_F32; memcpy(&out.f32,&r,4); break; }
        /* f64.convert_i32_s */
        case 0xb7: { double r=(double)(int32_t)v.i32; out.type=WASM_VALTYPE_F64; memcpy(&out.f64,&r,8); break; }
        /* f64.convert_i32_u */
        case 0xb8: { double r=(double)(uint32_t)v.i32; out.type=WASM_VALTYPE_F64; memcpy(&out.f64,&r,8); break; }
        /* f64.convert_i64_s */
        case 0xb9: { double r=(double)(int64_t)v.i64; out.type=WASM_VALTYPE_F64; memcpy(&out.f64,&r,8); break; }
        /* f64.convert_i64_u */
        case 0xba: { double r=(double)(uint64_t)v.i64; out.type=WASM_VALTYPE_F64; memcpy(&out.f64,&r,8); break; }
        /* f64.promote_f32 */
        case 0xbb: { memcpy(&f32v,&v.f32,4); double r=(double)f32v; out.type=WASM_VALTYPE_F64; memcpy(&out.f64,&r,8); break; }
        /* i32.reinterpret_f32 */
        case 0xbc: memcpy(&u32,&v.f32,4); out.type=WASM_VALTYPE_I32; out.i32=(int32_t)u32; break;
        /* i64.reinterpret_f64 */
        case 0xbd: memcpy(&u64,&v.f64,8); out.type=WASM_VALTYPE_I64; out.i64=(int64_t)u64; break;
        /* f32.reinterpret_i32 */
        case 0xbe: u32=(uint32_t)v.i32; out.type=WASM_VALTYPE_F32; memcpy(&out.f32,&u32,4); break;
        /* f64.reinterpret_i64 */
        case 0xbf: u64=(uint64_t)v.i64; out.type=WASM_VALTYPE_F64; memcpy(&out.f64,&u64,8); break;
        default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported conversion");
    }
    if (!stack_push(stack, out))
        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

/* Saturating truncation (0xFC prefix) */
exec_status exec_sat_trunc(uint32_t sub_op, exec_stack *stack,
                                  exec_error *err) {
    wasm_value v;
    if (!stack_pop(stack, &v))
        return exec_fail(err, EXEC_ERROR_TRAP, "sat_trunc operand missing");
    wasm_value out; memset(&out, 0, sizeof(out));
    float f32v; double f64v;
    switch (sub_op) {
        case 0: memcpy(&f32v,&v.f32,4); out.type=WASM_VALTYPE_I32; out.i32=(int32_t)trunc_sat_i32_s_f32(f32v); break;
        case 1: memcpy(&f32v,&v.f32,4); out.type=WASM_VALTYPE_I32; out.i32=(int32_t)trunc_sat_i32_u_f32(f32v); break;
        case 2: memcpy(&f64v,&v.f64,8); out.type=WASM_VALTYPE_I32; out.i32=(int32_t)trunc_sat_i32_s_f64(f64v); break;
        case 3: memcpy(&f64v,&v.f64,8); out.type=WASM_VALTYPE_I32; out.i32=(int32_t)trunc_sat_i32_u_f64(f64v); break;
        case 4: memcpy(&f32v,&v.f32,4); out.type=WASM_VALTYPE_I64; out.i64=(int64_t)trunc_sat_i64_s_f32(f32v); break;
        case 5: memcpy(&f32v,&v.f32,4); out.type=WASM_VALTYPE_I64; out.i64=(int64_t)trunc_sat_i64_u_f32(f32v); break;
        case 6: memcpy(&f64v,&v.f64,8); out.type=WASM_VALTYPE_I64; out.i64=(int64_t)trunc_sat_i64_s_f64(f64v); break;
        case 7: memcpy(&f64v,&v.f64,8); out.type=WASM_VALTYPE_I64; out.i64=(int64_t)trunc_sat_i64_u_f64(f64v); break;
        default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported sat_trunc");
    }
    if (!stack_push(stack, out))
        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

exec_status exec_i32_numeric(uint32_t opcode, exec_stack *stack,
                                    exec_error *err) {
    wasm_value left, right;
    uint32_t a, b, result = 0;
    int unary = opcode == 0x45 || opcode == 0x67 || opcode == 0x68 ||
                opcode == 0x69 || opcode == 0xc0 || opcode == 0xc1;
    if (!stack_pop(stack, &right) || right.type != WASM_VALTYPE_I32)
        return exec_fail(err, EXEC_ERROR_TRAP, "i32 operand missing");
    b = (uint32_t)right.i32;
    if (!unary) {
        if (!stack_pop(stack, &left) || left.type != WASM_VALTYPE_I32)
            return exec_fail(err, EXEC_ERROR_TRAP, "i32 operand missing");
        a = (uint32_t)left.i32;
    } else a = 0;
    switch (opcode) {
        case 0x45: result = b == 0; break;
        case 0x46: result = a == b; break; case 0x47: result = a != b; break;
        case 0x48: result = (int32_t)a < (int32_t)b; break;
        case 0x49: result = a < b; break;
        case 0x4a: result = (int32_t)a > (int32_t)b; break;
        case 0x4b: result = a > b; break;
        case 0x4c: result = (int32_t)a <= (int32_t)b; break;
        case 0x4d: result = a <= b; break;
        case 0x4e: result = (int32_t)a >= (int32_t)b; break;
        case 0x4f: result = a >= b; break;
        case 0x67: result = b ? (uint32_t)__builtin_clz(b) : 32u; break;
        case 0x68: result = b ? (uint32_t)__builtin_ctz(b) : 32u; break;
        case 0x69: result = (uint32_t)__builtin_popcount(b); break;
        case 0x6a: result = a + b; break; case 0x6b: result = a - b; break;
        case 0x6c: result = a * b; break;
        case 0x6d:
            if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero");
            if (a == 0x80000000u && b == UINT32_MAX)
                return exec_fail(err, EXEC_ERROR_TRAP, "integer overflow");
            result = (uint32_t)((int32_t)a / (int32_t)b); break;
        case 0x6e: if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero"); result = a / b; break;
        case 0x6f: if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero"); result = (a == 0x80000000u && b == UINT32_MAX) ? 0 : (uint32_t)((int32_t)a % (int32_t)b); break;
        case 0x70: if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero"); result = a % b; break;
        case 0x71: result = a & b; break; case 0x72: result = a | b; break;
        case 0x73: result = a ^ b; break; case 0x74: result = a << (b & 31u); break;
        case 0x75: result = (uint32_t)((int32_t)a >> (b & 31u)); break;
        case 0x76: result = a >> (b & 31u); break;
        case 0x77: result = rotl32(a, b); break; case 0x78: result = rotr32(a, b); break;
        case 0xc0: result = (uint32_t)(int32_t)(int8_t)b; break;
        case 0xc1: result = (uint32_t)(int32_t)(int16_t)b; break;
        default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported i32 opcode");
    }
    if (!stack_push(stack, i32_value(result)))
        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}
