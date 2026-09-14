#include "runtime_internal.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/* ---- v128 helpers as byte arrays ---- */

static wasm_value v128_from_bytes(const uint8_t *b) {
    wasm_value v;
    v.type = WASM_VALTYPE_V128;
    memcpy(v.v128.bytes, b, 16);
    return v;
}

static uint64_t simd_lane_u(const wasm_value *v, uint32_t lane,
                            uint32_t width) {
    uint64_t value = 0;
    memcpy(&value, v->v128.bytes + lane * width, width);
    return value;
}

static int64_t simd_lane_s(const wasm_value *v, uint32_t lane,
                           uint32_t width) {
    uint64_t value = simd_lane_u(v, lane, width);
    if (width < 8 && (value & (UINT64_C(1) << (width * 8 - 1))))
        value |= UINT64_MAX << (width * 8);
    return (int64_t)value;
}

void simd_set_lane(wasm_value *v, uint32_t lane, uint32_t width,
                   uint64_t value) {
    memcpy(v->v128.bytes + lane * width, &value, width);
}

wasm_value simd_zero(void) {
    uint8_t bytes[16] = {0};
    return v128_from_bytes(bytes);
}

static int64_t simd_sat_s(int64_t value, uint32_t bits) {
    int64_t min = -(INT64_C(1) << (bits - 1));
    int64_t max = (INT64_C(1) << (bits - 1)) - 1;
    return value < min ? min : value > max ? max : value;
}

int simd_pop(exec_stack *stack, wasm_value *value) {
    return stack_pop(stack, value) && value->type == WASM_VALTYPE_V128;
}

/* ---- SIMD float min/max helpers ---- */

static float simd_f32_minmax(float a, float b, int maximum) {
    uint32_t ai, bi, ri;
    memcpy(&ai, &a, 4); memcpy(&bi, &b, 4);
    if (isnan(a) || isnan(b)) {
        ri = UINT32_C(0x7fc00000); memcpy(&a, &ri, 4); return a;
    }
    if (a == 0.0f && b == 0.0f) {
        ri = maximum ? ai & bi : ai | bi; memcpy(&a, &ri, 4); return a;
    }
    return maximum ? (a > b ? a : b) : (a < b ? a : b);
}

static double simd_f64_minmax(double a, double b, int maximum) {
    uint64_t ai, bi, ri;
    memcpy(&ai, &a, 8); memcpy(&bi, &b, 8);
    if (isnan(a) || isnan(b)) {
        ri = UINT64_C(0x7ff8000000000000); memcpy(&a, &ri, 8); return a;
    }
    if (a == 0.0 && b == 0.0) {
        ri = maximum ? ai & bi : ai | bi; memcpy(&a, &ri, 8); return a;
    }
    return maximum ? (a > b ? a : b) : (a < b ? a : b);
}

/* ---- Standard SIMD integer handler ---- */

/* Execute the fixed-width integer, lane, comparison, and bitwise portion of
 * the standard SIMD instruction set.  Returns 1 when the opcode is handled,
 * 0 when a later SIMD family must handle it, and -1 on an execution error. */
int exec_standard_simd_integer(uint32_t op, uint32_t lane_index,
                                      const wasm_v128 *immediate,
                                      exec_stack *stack, exec_error *err) {
    wasm_value a, b, c, out;
    uint32_t width = 0, lanes, kind = 0;
    uint64_t mask;

    /* Splat and lane access. */
    if (op >= 0x0f && op <= 0x14) {
        wasm_value scalar;
        if (!stack_pop(stack, &scalar)) {
            exec_fail(err, EXEC_ERROR_TRAP, "SIMD splat operand missing");
            return -1;
        }
        out = simd_zero();
        width = op <= 0x0f ? 1 : op == 0x10 ? 2 :
                (op == 0x11 || op == 0x13) ? 4 : 8;
        uint64_t bits = 0;
        if (op <= 0x11) bits = (uint32_t)scalar.i32;
        else if (op == 0x12) bits = (uint64_t)scalar.i64;
        else if (op == 0x13) memcpy(&bits, &scalar.f32, 4);
        else memcpy(&bits, &scalar.f64, 8);
        for (uint32_t i = 0; i < 16 / width; i++)
            simd_set_lane(&out, i, width, bits);
        if (!stack_push(stack, out)) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            return -1;
        }
        return 1;
    }
    if (op >= 0x15 && op <= 0x22) {
        int replace = op == 0x17 || op == 0x1a || op == 0x1c ||
                      op == 0x1e || op == 0x20 || op == 0x22;
        width = op <= 0x17 ? 1 : op <= 0x1a ? 2 :
                op <= 0x1c ? 4 : op <= 0x1e ? 8 :
                op <= 0x20 ? 4 : 8;
        if (replace) {
            wasm_value scalar;
            if (!stack_pop(stack, &scalar) || !simd_pop(stack, &a)) {
                exec_fail(err, EXEC_ERROR_TRAP, "SIMD replace_lane operands missing");
                return -1;
            }
            uint64_t bits = 0;
            if (op <= 0x1c) bits = (uint32_t)scalar.i32;
            else if (op == 0x1e) bits = (uint64_t)scalar.i64;
            else if (op == 0x20) memcpy(&bits, &scalar.f32, 4);
            else memcpy(&bits, &scalar.f64, 8);
            simd_set_lane(&a, lane_index, width, bits);
            if (!stack_push(stack, a)) {
                exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                return -1;
            }
        } else {
            if (!simd_pop(stack, &a)) {
                exec_fail(err, EXEC_ERROR_TRAP, "SIMD extract_lane operand missing");
                return -1;
            }
            uint64_t bits = simd_lane_u(&a, lane_index, width);
            memset(&out, 0, sizeof(out));
            if (op == 0x15 || op == 0x18) {
                out.type = WASM_VALTYPE_I32;
                out.i32 = (int32_t)simd_lane_s(&a, lane_index, width);
            } else if (op == 0x16 || op == 0x19 || op == 0x1b) {
                out.type = WASM_VALTYPE_I32; out.i32 = (int32_t)bits;
            } else if (op == 0x1d) {
                out.type = WASM_VALTYPE_I64; out.i64 = (int64_t)bits;
            } else if (op == 0x1f) {
                out.type = WASM_VALTYPE_F32; memcpy(&out.f32, &bits, 4);
            } else {
                out.type = WASM_VALTYPE_F64; memcpy(&out.f64, &bits, 8);
            }
            if (!stack_push(stack, out)) {
                exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                return -1;
            }
        }
        return 1;
    }

    if (op == 0x0d) { /* i8x16.shuffle */
        if (!simd_pop(stack, &b) || !simd_pop(stack, &a)) {
            exec_fail(err, EXEC_ERROR_TRAP, "SIMD shuffle operands missing");
            return -1;
        }
        out = simd_zero();
        for (uint32_t i = 0; i < 16; i++) {
            uint8_t index = immediate->bytes[i];
            out.v128.bytes[i] = index < 16 ? a.v128.bytes[index] :
                                  b.v128.bytes[index - 16];
        }
        if (!stack_push(stack, out)) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); return -1;
        }
        return 1;
    }
    if (op == 0x0e) { /* i8x16.swizzle */
        if (!simd_pop(stack, &b) || !simd_pop(stack, &a)) {
            exec_fail(err, EXEC_ERROR_TRAP, "SIMD swizzle operands missing");
            return -1;
        }
        out = simd_zero();
        for (uint32_t i = 0; i < 16; i++)
            out.v128.bytes[i] = b.v128.bytes[i] < 16 ?
                                a.v128.bytes[b.v128.bytes[i]] : 0;
        if (!stack_push(stack, out)) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); return -1;
        }
        return 1;
    }

    if (op >= 0x4d && op <= 0x53) {
        if (!simd_pop(stack, &a)) {
            exec_fail(err, EXEC_ERROR_TRAP, "SIMD bitwise operand missing");
            return -1;
        }
        if (op == 0x53) {
            uint32_t any = 0;
            for (uint32_t i = 0; i < 16; i++) any |= a.v128.bytes[i];
            if (!stack_push(stack, i32_value(any != 0))) {
                exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); return -1;
            }
            return 1;
        }
        if (op != 0x4d && (!simd_pop(stack, &b) ||
            (op == 0x52 && !simd_pop(stack, &c)))) {
            exec_fail(err, EXEC_ERROR_TRAP, "SIMD bitwise operands missing");
            return -1;
        }
        out = simd_zero();
        for (uint32_t i = 0; i < 16; i++) {
            if (op == 0x4d) out.v128.bytes[i] = (uint8_t)~a.v128.bytes[i];
            else if (op == 0x4e) out.v128.bytes[i] = b.v128.bytes[i] & a.v128.bytes[i];
            else if (op == 0x4f) out.v128.bytes[i] = b.v128.bytes[i] & (uint8_t)~a.v128.bytes[i];
            else if (op == 0x50) out.v128.bytes[i] = b.v128.bytes[i] | a.v128.bytes[i];
            else if (op == 0x51) out.v128.bytes[i] = b.v128.bytes[i] ^ a.v128.bytes[i];
            else out.v128.bytes[i] = (c.v128.bytes[i] & a.v128.bytes[i]) |
                                     (b.v128.bytes[i] & (uint8_t)~a.v128.bytes[i]);
        }
        if (!stack_push(stack, out)) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); return -1;
        }
        return 1;
    }

    /* Integer comparisons. */
    if (op >= 0x23 && op <= 0x40) {
        if (op <= 0x2c) { width = 1; kind = op - 0x23; }
        else if (op <= 0x36) { width = 2; kind = op - 0x2d; }
        else { width = 4; kind = op - 0x37; }
    } else if (op >= 0xd6 && op <= 0xdb) {
        width = 8;
        static const uint8_t kinds[] = {0,1,2,4,6,8};
        kind = kinds[op - 0xd6];
    }
    if (width) {
        if (!simd_pop(stack, &b) || !simd_pop(stack, &a)) {
            exec_fail(err, EXEC_ERROR_TRAP, "SIMD comparison operands missing");
            return -1;
        }
        out = simd_zero(); lanes = 16 / width;
        mask = width == 8 ? UINT64_MAX : (UINT64_C(1) << (width * 8)) - 1;
        for (uint32_t i = 0; i < lanes; i++) {
            uint64_t au = simd_lane_u(&a, i, width), bu = simd_lane_u(&b, i, width);
            int64_t as = simd_lane_s(&a, i, width), bs = simd_lane_s(&b, i, width);
            int yes = kind == 0 ? au == bu : kind == 1 ? au != bu :
                      kind == 2 ? as < bs : kind == 3 ? au < bu :
                      kind == 4 ? as > bs : kind == 5 ? au > bu :
                      kind == 6 ? as <= bs : kind == 7 ? au <= bu :
                      kind == 8 ? as >= bs : au >= bu;
            simd_set_lane(&out, i, width, yes ? mask : 0);
        }
        if (!stack_push(stack, out)) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); return -1;
        }
        return 1;
    }

    /* Integer lane-width conversions and pairwise products. */
    if (op == 0x65 || op == 0x66 || op == 0x85 || op == 0x86) {
        uint32_t source_width = op <= 0x66 ? 2 : 4;
        uint32_t dest_width = source_width / 2;
        int signed_ = op == 0x65 || op == 0x85;
        if (!simd_pop(stack,&b) || !simd_pop(stack,&a)) {
            exec_fail(err,EXEC_ERROR_TRAP,"SIMD narrow operands missing");return -1;
        }
        out=simd_zero(); lanes=16/source_width;
        for(uint32_t half=0;half<2;half++) for(uint32_t i=0;i<lanes;i++) {
            wasm_value *source=half?&b:&a;
            uint64_t r;
            if(signed_) r=(uint64_t)simd_sat_s(simd_lane_s(source,i,source_width),dest_width*8);
            else {
                int64_t x=simd_lane_s(source,i,source_width);
                uint64_t max=(UINT64_C(1)<<(dest_width*8))-1;
                r=x<0?0:(uint64_t)x>max?max:(uint64_t)x;
            }
            simd_set_lane(&out,half*lanes+i,dest_width,r);
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}
        return 1;
    }
    if ((op>=0x87&&op<=0x8a)||(op>=0xa7&&op<=0xaa)||(op>=0xc7&&op<=0xca)) {
        uint32_t source_width=op<=0x8a?1:op<=0xaa?2:4;
        uint32_t dest_width=source_width*2;
        uint32_t variant=op-(op<=0x8a?0x87:op<=0xaa?0xa7:0xc7);
        int high=(variant&1)!=0, signed_=(variant&2)==0;
        if(!simd_pop(stack,&a)){exec_fail(err,EXEC_ERROR_TRAP,"SIMD extend operand missing");return -1;}
        out=simd_zero();lanes=16/dest_width;
        for(uint32_t i=0;i<lanes;i++) {
            uint32_t source_lane=i+(high?lanes:0);
            uint64_t r=signed_?(uint64_t)simd_lane_s(&a,source_lane,source_width):simd_lane_u(&a,source_lane,source_width);
            simd_set_lane(&out,i,dest_width,r);
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}
        return 1;
    }
    if ((op>=0x9c&&op<=0x9f)||(op>=0xbc&&op<=0xbf)||(op>=0xdc&&op<=0xdf)) {
        uint32_t source_width=op<=0x9f?1:op<=0xbf?2:4;
        uint32_t dest_width=source_width*2;
        uint32_t variant=op-(op<=0x9f?0x9c:op<=0xbf?0xbc:0xdc);
        int high=(variant&1)!=0, signed_=(variant&2)==0;
        if(!simd_pop(stack,&b)||!simd_pop(stack,&a)){exec_fail(err,EXEC_ERROR_TRAP,"SIMD extmul operands missing");return -1;}
        out=simd_zero();lanes=16/dest_width;
        for(uint32_t i=0;i<lanes;i++) {
            uint32_t source_lane=i+(high?lanes:0);uint64_t r;
            if(signed_) r=(uint64_t)(simd_lane_s(&a,source_lane,source_width)*simd_lane_s(&b,source_lane,source_width));
            else r=simd_lane_u(&a,source_lane,source_width)*simd_lane_u(&b,source_lane,source_width);
            simd_set_lane(&out,i,dest_width,r);
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}
        return 1;
    }
    if(op>=0x7c&&op<=0x7f) {
        uint32_t source_width=op<=0x7d?1:2,dest_width=source_width*2;
        int signed_=(op&1)==0;
        if(!simd_pop(stack,&a)){exec_fail(err,EXEC_ERROR_TRAP,"SIMD pairwise operand missing");return -1;}
        out=simd_zero();lanes=16/dest_width;
        for(uint32_t i=0;i<lanes;i++) {
            uint64_t r=signed_?(uint64_t)(simd_lane_s(&a,i*2,source_width)+simd_lane_s(&a,i*2+1,source_width)):
                      simd_lane_u(&a,i*2,source_width)+simd_lane_u(&a,i*2+1,source_width);
            simd_set_lane(&out,i,dest_width,r);
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}
        return 1;
    }
    if(op==0x82) {
        if(!simd_pop(stack,&b)||!simd_pop(stack,&a)){exec_fail(err,EXEC_ERROR_TRAP,"SIMD q15mulr operands missing");return -1;}
        out=simd_zero();
        for(uint32_t i=0;i<8;i++) {
            int64_t r=(simd_lane_s(&a,i,2)*simd_lane_s(&b,i,2)+0x4000)>>15;
            simd_set_lane(&out,i,2,(uint64_t)simd_sat_s(r,16));
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}return 1;
    }
    if(op==0xba) {
        if(!simd_pop(stack,&b)||!simd_pop(stack,&a)){exec_fail(err,EXEC_ERROR_TRAP,"SIMD dot operands missing");return -1;}
        out=simd_zero();
        for(uint32_t i=0;i<4;i++) {
            int64_t r=simd_lane_s(&a,i*2,2)*simd_lane_s(&b,i*2,2)+
                      simd_lane_s(&a,i*2+1,2)*simd_lane_s(&b,i*2+1,2);
            simd_set_lane(&out,i,4,(uint64_t)r);
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}return 1;
    }

    /* Regular integer lane operations. kind: abs, neg, popcnt, all_true,
     * bitmask, shl, shr_s, shr_u, add, add_sat_s/u, sub, sub_sat_s/u,
     * mul, min_s/u, max_s/u, avgr_u. */
    switch (op) {
        case 0x60: width=1;kind=1;break; case 0x61:width=1;kind=2;break;
        case 0x62:width=1;kind=3;break; case 0x63:width=1;kind=4;break;
        case 0x64:width=1;kind=5;break; case 0x6b:width=1;kind=6;break;
        case 0x6c:width=1;kind=7;break; case 0x6d:width=1;kind=8;break;
        case 0x6e:width=1;kind=9;break; case 0x6f:width=1;kind=10;break;
        case 0x70:width=1;kind=11;break; case 0x71:width=1;kind=12;break;
        case 0x72:width=1;kind=13;break; case 0x73:width=1;kind=14;break;
        case 0x76:width=1;kind=16;break; case 0x77:width=1;kind=17;break;
        case 0x78:width=1;kind=18;break; case 0x79:width=1;kind=19;break;
        case 0x7b:width=1;kind=20;break;
        case 0x80:width=2;kind=1;break; case 0x81:width=2;kind=2;break;
        case 0x83:width=2;kind=4;break; case 0x84:width=2;kind=5;break;
        case 0x8b:width=2;kind=6;break; case 0x8c:width=2;kind=7;break;
        case 0x8d:width=2;kind=8;break; case 0x8e:width=2;kind=9;break;
        case 0x8f:width=2;kind=10;break; case 0x90:width=2;kind=11;break;
        case 0x91:width=2;kind=12;break; case 0x92:width=2;kind=13;break;
        case 0x93:width=2;kind=14;break; case 0x95:width=2;kind=15;break;
        case 0x96:width=2;kind=16;break; case 0x97:width=2;kind=17;break;
        case 0x98:width=2;kind=18;break; case 0x99:width=2;kind=19;break;
        case 0x9b:width=2;kind=20;break;
        case 0xa0:width=4;kind=1;break; case 0xa1:width=4;kind=2;break;
        case 0xa3:width=4;kind=4;break; case 0xa4:width=4;kind=5;break;
        case 0xab:width=4;kind=6;break; case 0xac:width=4;kind=7;break;
        case 0xad:width=4;kind=8;break; case 0xae:width=4;kind=9;break;
        case 0xb1:width=4;kind=12;break; case 0xb5:width=4;kind=15;break;
        case 0xb6:width=4;kind=16;break; case 0xb7:width=4;kind=17;break;
        case 0xb8:width=4;kind=18;break; case 0xb9:width=4;kind=19;break;
        case 0xc0:width=8;kind=1;break; case 0xc1:width=8;kind=2;break;
        case 0xc3:width=8;kind=4;break; case 0xc4:width=8;kind=5;break;
        case 0xcb:width=8;kind=6;break; case 0xcc:width=8;kind=7;break;
        case 0xcd:width=8;kind=8;break; case 0xce:width=8;kind=9;break;
        case 0xd1:width=8;kind=12;break; case 0xd5:width=8;kind=15;break;
        default: break;
    }
    if (!width) return 0;
    int unary = kind <= 5;
    if ((kind >= 6 && kind <= 8)) {
        if (!stack_pop(stack, &b) || b.type != WASM_VALTYPE_I32 || !simd_pop(stack, &a)) {
            exec_fail(err, EXEC_ERROR_TRAP, "SIMD shift operands missing"); return -1;
        }
    } else if (!simd_pop(stack, &b) || (!unary && !simd_pop(stack, &a))) {
        exec_fail(err, EXEC_ERROR_TRAP, "SIMD integer operands missing"); return -1;
    }
    if (unary) a = b;
    lanes = 16 / width;
    if (kind == 4 || kind == 5) {
        uint32_t result = kind == 4 ? 1u : 0u;
        for (uint32_t i = 0; i < lanes; i++) {
            uint64_t x = simd_lane_u(&a, i, width);
            if (kind == 4) result &= x != 0;
            else result |= (uint32_t)(x >> (width * 8 - 1)) << i;
        }
        if (!stack_push(stack, i32_value(result))) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); return -1;
        }
        return 1;
    }
    out = simd_zero();
    mask = width == 8 ? UINT64_MAX : (UINT64_C(1) << (width * 8)) - 1;
    for (uint32_t i = 0; i < lanes; i++) {
        uint64_t au=simd_lane_u(&a,i,width), bu=simd_lane_u(&b,i,width), r=0;
        int64_t as=simd_lane_s(&a,i,width), bs=simd_lane_s(&b,i,width);
        uint32_t shift = (uint32_t)b.i32 & (width * 8 - 1);
        switch (kind) {
            case 1: r = as < 0 ? (uint64_t)(0 - au) : au; break;
            case 2: r = 0 - au; break;
            case 3: r = (uint64_t)__builtin_popcount((unsigned)au); break;
            case 6: r = au << shift; break;
            case 7: r = (uint64_t)(as >> shift); break;
            case 8: r = au >> shift; break;
            case 9: r = au + bu; break;
            case 10: r = (uint64_t)simd_sat_s(as + bs, width*8); break;
            case 11: { uint64_t sum=au+bu; r=sum>mask?mask:sum; break; }
            case 12: r = au - bu; break;
            case 13: r = (uint64_t)simd_sat_s(as - bs, width*8); break;
            case 14: r = au < bu ? 0 : au - bu; break;
            case 15: r = au * bu; break;
            case 16: r = (uint64_t)(as < bs ? as : bs); break;
            case 17: r = au < bu ? au : bu; break;
            case 18: r = (uint64_t)(as > bs ? as : bs); break;
            case 19: r = au > bu ? au : bu; break;
            case 20: r = (au + bu + 1) >> 1; break;
            default: break;
        }
        simd_set_lane(&out, i, width, r & mask);
    }
    if (!stack_push(stack, out)) {
        exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); return -1;
    }
    return 1;
}

/* ---- Standard SIMD float handler ---- */

int exec_standard_simd_float(uint32_t op, exec_stack *stack,
                             exec_error *err) {
    wasm_value a, b, out;
    uint32_t width = 0, lanes = 0, kind = 0;

    if (op >= 0x41 && op <= 0x46) { width=4;lanes=4;kind=op-0x41; }
    else if (op >= 0x47 && op <= 0x4c) { width=8;lanes=2;kind=op-0x47; }
    if (width) {
        if (!simd_pop(stack,&b) || !simd_pop(stack,&a)) {
            exec_fail(err,EXEC_ERROR_TRAP,"SIMD float comparison operands missing");
            return -1;
        }
        out=simd_zero();
        for (uint32_t i=0;i<lanes;i++) {
            int yes;
            if (width==4) {
                float x,y; memcpy(&x,a.v128.bytes+i*4,4); memcpy(&y,b.v128.bytes+i*4,4);
                yes=kind==0?x==y:kind==1?x!=y:kind==2?x<y:kind==3?x>y:kind==4?x<=y:x>=y;
                simd_set_lane(&out,i,4,yes?UINT32_MAX:0);
            } else {
                double x,y; memcpy(&x,a.v128.bytes+i*8,8); memcpy(&y,b.v128.bytes+i*8,8);
                yes=kind==0?x==y:kind==1?x!=y:kind==2?x<y:kind==3?x>y:kind==4?x<=y:x>=y;
                simd_set_lane(&out,i,8,yes?UINT64_MAX:0);
            }
        }
        if (!stack_push(stack,out)) { exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1; }
        return 1;
    }

    /* Unary rounding/arithmetic operations. */
    switch(op) {
        case 0x67:width=4;kind=1;break; case 0x68:width=4;kind=2;break;
        case 0x69:width=4;kind=3;break; case 0x6a:width=4;kind=4;break;
        case 0x74:width=8;kind=1;break; case 0x75:width=8;kind=2;break;
        case 0x7a:width=8;kind=3;break; case 0x94:width=8;kind=4;break;
        case 0xe0:width=4;kind=5;break; case 0xe1:width=4;kind=6;break;
        case 0xe3:width=4;kind=7;break; case 0xec:width=8;kind=5;break;
        case 0xed:width=8;kind=6;break; case 0xef:width=8;kind=7;break;
        default:break;
    }
    if (width) {
        if (!simd_pop(stack,&a)) { exec_fail(err,EXEC_ERROR_TRAP,"SIMD float operand missing");return -1; }
        out=simd_zero(); lanes=16/width;
        for(uint32_t i=0;i<lanes;i++) {
            if(width==4) {
                float x,r; uint32_t bits;
                memcpy(&x,a.v128.bytes+i*4,4);
                if(kind==5) { memcpy(&bits,&x,4);bits&=UINT32_C(0x7fffffff);memcpy(&r,&bits,4); }
                else if(kind==6) { memcpy(&bits,&x,4);bits^=UINT32_C(0x80000000);memcpy(&r,&bits,4); }
                else r=kind==1?ceilf(x):kind==2?floorf(x):kind==3?truncf(x):kind==4?nearbyintf(x):sqrtf(x);
                memcpy(out.v128.bytes+i*4,&r,4);
            } else {
                double x,r; uint64_t bits;
                memcpy(&x,a.v128.bytes+i*8,8);
                if(kind==5) { memcpy(&bits,&x,8);bits&=UINT64_C(0x7fffffffffffffff);memcpy(&r,&bits,8); }
                else if(kind==6) { memcpy(&bits,&x,8);bits^=UINT64_C(0x8000000000000000);memcpy(&r,&bits,8); }
                else r=kind==1?ceil(x):kind==2?floor(x):kind==3?trunc(x):kind==4?nearbyint(x):sqrt(x);
                memcpy(out.v128.bytes+i*8,&r,8);
            }
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}
        return 1;
    }

    /* Binary float arithmetic. */
    if(op>=0xe4&&op<=0xeb){width=4;kind=op-0xe4;}
    else if(op>=0xf0&&op<=0xf7){width=8;kind=op-0xf0;}
    if(width) {
        if(!simd_pop(stack,&b)||!simd_pop(stack,&a)){exec_fail(err,EXEC_ERROR_TRAP,"SIMD float operands missing");return -1;}
        out=simd_zero();lanes=16/width;
        for(uint32_t i=0;i<lanes;i++) {
            if(width==4) {
                float x,y,r;memcpy(&x,a.v128.bytes+i*4,4);memcpy(&y,b.v128.bytes+i*4,4);
                if(kind==0)r=x+y;else if(kind==1)r=x-y;else if(kind==2)r=x*y;else if(kind==3)r=x/y;
                else if(kind==4)r=simd_f32_minmax(x,y,0);else if(kind==5)r=simd_f32_minmax(x,y,1);
                else if(kind==6)r=y<x?y:x;else r=x<y?y:x;
                memcpy(out.v128.bytes+i*4,&r,4);
            } else {
                double x,y,r;memcpy(&x,a.v128.bytes+i*8,8);memcpy(&y,b.v128.bytes+i*8,8);
                if(kind==0)r=x+y;else if(kind==1)r=x-y;else if(kind==2)r=x*y;else if(kind==3)r=x/y;
                else if(kind==4)r=simd_f64_minmax(x,y,0);else if(kind==5)r=simd_f64_minmax(x,y,1);
                else if(kind==6)r=y<x?y:x;else r=x<y?y:x;
                memcpy(out.v128.bytes+i*8,&r,8);
            }
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}
        return 1;
    }

    /* Numeric vector conversions. */
    if(op==0x5e||op==0x5f||(op>=0xf8&&op<=0xff)) {
        if(!simd_pop(stack,&a)){exec_fail(err,EXEC_ERROR_TRAP,"SIMD conversion operand missing");return -1;}
        out=simd_zero();
        if(op==0x5e) {
            for(uint32_t i=0;i<2;i++){double x;float r;memcpy(&x,a.v128.bytes+i*8,8);r=(float)x;memcpy(out.v128.bytes+i*4,&r,4);}
        } else if(op==0x5f) {
            for(uint32_t i=0;i<2;i++){float x;double r;memcpy(&x,a.v128.bytes+i*4,4);r=(double)x;memcpy(out.v128.bytes+i*8,&r,8);}
        } else if(op==0xf8||op==0xf9) {
            for(uint32_t i=0;i<4;i++){float x;uint32_t r;memcpy(&x,a.v128.bytes+i*4,4);r=op==0xf8?trunc_sat_i32_s_f32(x):trunc_sat_i32_u_f32(x);simd_set_lane(&out,i,4,r);}
        } else if(op==0xfa||op==0xfb) {
            for(uint32_t i=0;i<4;i++){uint32_t x=(uint32_t)simd_lane_u(&a,i,4);float r=op==0xfa?(float)(int32_t)x:(float)x;memcpy(out.v128.bytes+i*4,&r,4);}
        } else if(op==0xfc||op==0xfd) {
            for(uint32_t i=0;i<2;i++){double x;uint32_t r;memcpy(&x,a.v128.bytes+i*8,8);r=op==0xfc?trunc_sat_i32_s_f64(x):trunc_sat_i32_u_f64(x);simd_set_lane(&out,i,4,r);}
        } else {
            for(uint32_t i=0;i<2;i++){uint32_t x=(uint32_t)simd_lane_u(&a,i,4);double r=op==0xfe?(double)(int32_t)x:(double)x;memcpy(out.v128.bytes+i*8,&r,8);}
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}
        return 1;
    }
    return 0;
}

/* ---- Relaxed SIMD pure functions ---- */

/* i8x16.relaxed_laneselect: bitselect */
wasm_value exec_i8x16_laneselect(wasm_value a, wasm_value b, wasm_value c) {
    uint8_t res[16];
    for (int i = 0; i < 16; i++)
        res[i] = (a.v128.bytes[i] & c.v128.bytes[i]) | (b.v128.bytes[i] & ~c.v128.bytes[i]);
    return v128_from_bytes(res);
}

/* Generic bitselect for other lane widths (same byte-level operation) */
wasm_value exec_bitselect(wasm_value a, wasm_value b, wasm_value c) {
    uint8_t res[16];
    for (int i = 0; i < 16; i++)
        res[i] = (a.v128.bytes[i] & c.v128.bytes[i]) | (b.v128.bytes[i] & ~c.v128.bytes[i]);
    return v128_from_bytes(res);
}

/* i8x16.relaxed_swizzle: pshufb-style */
wasm_value exec_i8x16_relaxed_swizzle(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 16; i++) {
        uint8_t idx = b.v128.bytes[i];
        res[i] = (idx < 16) ? a.v128.bytes[idx] : 0;
    }
    return v128_from_bytes(res);
}

/* i8x16.eq: byte-wise equality */
wasm_value exec_i8x16_eq(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 16; i++)
        res[i] = (a.v128.bytes[i] == b.v128.bytes[i]) ? 0xFF : 0x00;
    return v128_from_bytes(res);
}

/* i16x8.eq: 16-bit lane equality */
wasm_value exec_i16x8_eq(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 8; i++) {
        uint16_t av, bv;
        memcpy(&av, &a.v128.bytes[i*2], 2);
        memcpy(&bv, &b.v128.bytes[i*2], 2);
        uint16_t r = (av == bv) ? 0xFFFF : 0x0000;
        memcpy(&res[i*2], &r, 2);
    }
    return v128_from_bytes(res);
}

/* i32x4.eq: 32-bit lane equality */
wasm_value exec_i32x4_eq(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        uint32_t av, bv;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        memcpy(&bv, &b.v128.bytes[i*4], 4);
        uint32_t r = (av == bv) ? 0xFFFFFFFFu : 0u;
        memcpy(&res[i*4], &r, 4);
    }
    return v128_from_bytes(res);
}

/* i64x2.eq */
wasm_value exec_i64x2_eq(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        uint64_t av, bv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        memcpy(&bv, &b.v128.bytes[i*8], 8);
        uint64_t r = (av == bv) ? UINT64_MAX : 0u;
        memcpy(&res[i*8], &r, 8);
    }
    return v128_from_bytes(res);
}

/* f32x4.eq */
wasm_value exec_f32x4_eq(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        float av, bv;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        memcpy(&bv, &b.v128.bytes[i*4], 4);
        uint32_t r = (av == bv) ? 0xFFFFFFFFu : 0u;
        memcpy(&res[i*4], &r, 4);
    }
    return v128_from_bytes(res);
}

/* f64x2.eq */
wasm_value exec_f64x2_eq(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        double av, bv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        memcpy(&bv, &b.v128.bytes[i*8], 8);
        uint64_t r = (av == bv) ? UINT64_MAX : 0u;
        memcpy(&res[i*8], &r, 8);
    }
    return v128_from_bytes(res);
}

/* f32x4.relaxed_min: if either is NaN, result is NaN; else fmin */
wasm_value exec_f32x4_relaxed_min(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        float av, bv, rv;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        memcpy(&bv, &b.v128.bytes[i*4], 4);
        if (isnan(av) || isnan(bv)) {
            rv = av; /* return first arg on NaN */
        } else {
            rv = av < bv ? av : bv;
            /* handle -0.0 vs +0.0: min should prefer -0.0 */
            if (av == 0.0f && bv == 0.0f) {
                uint32_t ai, bi;
                memcpy(&ai, &av, 4); memcpy(&bi, &bv, 4);
                if ((ai | bi) & 0x80000000u) {
                    uint32_t neg = 0x80000000u;
                    memcpy(&rv, &neg, 4);
                }
            }
        }
        memcpy(&res[i*4], &rv, 4);
    }
    return v128_from_bytes(res);
}

/* f32x4.relaxed_max */
wasm_value exec_f32x4_relaxed_max(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        float av, bv, rv;
        uint32_t ai, bi;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        memcpy(&bv, &b.v128.bytes[i*4], 4);
        memcpy(&ai, &a.v128.bytes[i*4], 4);
        memcpy(&bi, &b.v128.bytes[i*4], 4);
        if (isnan(av) || isnan(bv)) {
            rv = av;
        } else if (av == 0.0f && bv == 0.0f) {
            /* Both zeros: return first arg (deterministic choice) */
            rv = av;
        } else {
            rv = av > bv ? av : bv;
        }
        memcpy(&res[i*4], &rv, 4);
    }
    return v128_from_bytes(res);
}

/* f64x2.relaxed_min */
wasm_value exec_f64x2_relaxed_min(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        double av, bv, rv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        memcpy(&bv, &b.v128.bytes[i*8], 8);
        if (isnan(av) || isnan(bv)) {
            rv = av;
        } else {
            rv = av < bv ? av : bv;
            if (av == 0.0 && bv == 0.0) {
                uint64_t ai, bi;
                memcpy(&ai, &av, 8); memcpy(&bi, &bv, 8);
                if ((ai | bi) & 0x8000000000000000ULL) {
                    uint64_t neg = 0x8000000000000000ULL;
                    memcpy(&rv, &neg, 8);
                }
            }
        }
        memcpy(&res[i*8], &rv, 8);
    }
    return v128_from_bytes(res);
}

/* f64x2.relaxed_max */
wasm_value exec_f64x2_relaxed_max(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        double av, bv, rv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        memcpy(&bv, &b.v128.bytes[i*8], 8);
        if (isnan(av) || isnan(bv)) {
            rv = av;
        } else {
            rv = av > bv ? av : bv;
            if (av == 0.0 && bv == 0.0) {
                /* Both zeros: return first arg (deterministic choice) */
                rv = av;
            }
        }
        memcpy(&res[i*8], &rv, 8);
    }
    return v128_from_bytes(res);
}

/* f32x4.relaxed_madd: fmaf(a, b, c) */
wasm_value exec_f32x4_relaxed_madd(wasm_value a, wasm_value b, wasm_value c) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        float av, bv, cv, rv;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        memcpy(&bv, &b.v128.bytes[i*4], 4);
        memcpy(&cv, &c.v128.bytes[i*4], 4);
        rv = fmaf(av, bv, cv);
        memcpy(&res[i*4], &rv, 4);
    }
    return v128_from_bytes(res);
}

/* f32x4.relaxed_nmadd: fmaf(-a, b, c) */
wasm_value exec_f32x4_relaxed_nmadd(wasm_value a, wasm_value b, wasm_value c) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        float av, bv, cv, rv;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        memcpy(&bv, &b.v128.bytes[i*4], 4);
        memcpy(&cv, &c.v128.bytes[i*4], 4);
        rv = fmaf(-av, bv, cv);
        memcpy(&res[i*4], &rv, 4);
    }
    return v128_from_bytes(res);
}

/* f64x2.relaxed_madd: fma(a, b, c) */
wasm_value exec_f64x2_relaxed_madd(wasm_value a, wasm_value b, wasm_value c) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        double av, bv, cv, rv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        memcpy(&bv, &b.v128.bytes[i*8], 8);
        memcpy(&cv, &c.v128.bytes[i*8], 8);
        rv = fma(av, bv, cv);
        memcpy(&res[i*8], &rv, 8);
    }
    return v128_from_bytes(res);
}

/* f64x2.relaxed_nmadd: fma(-a, b, c) */
wasm_value exec_f64x2_relaxed_nmadd(wasm_value a, wasm_value b, wasm_value c) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        double av, bv, cv, rv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        memcpy(&bv, &b.v128.bytes[i*8], 8);
        memcpy(&cv, &c.v128.bytes[i*8], 8);
        rv = fma(-av, bv, cv);
        memcpy(&res[i*8], &rv, 8);
    }
    return v128_from_bytes(res);
}

/* i32x4.relaxed_trunc_f32x4_s: saturating signed trunc */
wasm_value exec_i32x4_relaxed_trunc_f32x4_s(wasm_value a) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        float av;
        int32_t rv;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        if (isnan(av) || av < -2147483648.0f) rv = INT32_MIN;
        else if (av >= 2147483648.0f) rv = INT32_MAX;
        else rv = (int32_t)av;
        memcpy(&res[i*4], &rv, 4);
    }
    return v128_from_bytes(res);
}

/* i32x4.relaxed_trunc_f32x4_u */
wasm_value exec_i32x4_relaxed_trunc_f32x4_u(wasm_value a) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        float av;
        uint32_t rv;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        if (isnan(av) || av < 0.0f) rv = 0;
        else if (av >= 4294967296.0f) rv = UINT32_MAX;
        else rv = (uint32_t)av;
        memcpy(&res[i*4], &rv, 4);
    }
    return v128_from_bytes(res);
}

/* i32x4.relaxed_trunc_f64x2_s_zero */
wasm_value exec_i32x4_relaxed_trunc_f64x2_s_zero(wasm_value a) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        double av;
        int32_t rv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        if (isnan(av) || av < -2147483648.0) rv = INT32_MIN;
        else if (av >= 2147483648.0) rv = INT32_MAX;
        else rv = (int32_t)av;
        memcpy(&res[i*4], &rv, 4);
    }
    /* upper two lanes are zero */
    memset(&res[8], 0, 8);
    return v128_from_bytes(res);
}

/* i32x4.relaxed_trunc_f64x2_u_zero */
wasm_value exec_i32x4_relaxed_trunc_f64x2_u_zero(wasm_value a) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        double av;
        uint32_t rv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        if (isnan(av) || av < 0.0) rv = 0;
        else if (av >= 4294967296.0) rv = UINT32_MAX;
        else rv = (uint32_t)av;
        memcpy(&res[i*4], &rv, 4);
    }
    memset(&res[8], 0, 8);
    return v128_from_bytes(res);
}

/* i16x8.relaxed_q15mulr_s: saturating q15 multiply */
wasm_value exec_i16x8_relaxed_q15mulr_s(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 8; i++) {
        int16_t av, bv;
        int32_t prod;
        int16_t rv;
        memcpy(&av, &a.v128.bytes[i*2], 2);
        memcpy(&bv, &b.v128.bytes[i*2], 2);
        prod = ((int32_t)av * (int32_t)bv + 0x4000) >> 15;
        if (prod > 32767) rv = 32767;
        else if (prod < -32768) rv = -32768;
        else rv = (int16_t)prod;
        memcpy(&res[i*2], &rv, 2);
    }
    return v128_from_bytes(res);
}

/* i16x8.relaxed_dot_i8x16_i7x16_s: signed dot product */
wasm_value exec_i16x8_relaxed_dot_i8x16_i7x16_s(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 8; i++) {
        int8_t  a0 = (int8_t)a.v128.bytes[i*2];
        int8_t  a1 = (int8_t)a.v128.bytes[i*2+1];
        int8_t  b0 = (int8_t)b.v128.bytes[i*2];
        int8_t  b1 = (int8_t)b.v128.bytes[i*2+1];
        int32_t dot = (int32_t)a0 * (int32_t)b0 + (int32_t)a1 * (int32_t)b1;
        int16_t rv = (dot > 32767) ? 32767 : (dot < -32768) ? -32768 : (int16_t)dot;
        memcpy(&res[i*2], &rv, 2);
    }
    return v128_from_bytes(res);
}

/* i32x4.relaxed_dot_i8x16_i7x16_add_s */
wasm_value exec_i32x4_relaxed_dot_i8x16_i7x16_add_s(wasm_value a, wasm_value b, wasm_value c) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        int32_t acc = 0;
        int32_t cv;
        memcpy(&cv, &c.v128.bytes[i*4], 4);
        for (int j = 0; j < 4; j++) {
            int8_t av = (int8_t)a.v128.bytes[i*4+j];
            int8_t bv = (int8_t)b.v128.bytes[i*4+j];
            acc += (int32_t)av * (int32_t)bv;
        }
        acc += cv;
        memcpy(&res[i*4], &acc, 4);
    }
    return v128_from_bytes(res);
}
