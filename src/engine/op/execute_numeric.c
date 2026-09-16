#include "../runtime_internal.h"

#include <stdint.h>
#include <string.h>

/* ---- Saturating truncation helpers ----
 * Used by exec_sat_trunc (0xFC sub-opcodes 0-7) and SIMD trunc lanes. */

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

/* Saturating truncation (0xFC prefix, sub-opcodes 0-7) */
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
