#include "../runtime_internal.h"
#include "validate.h"
#include "../instantiate.h"

#include <stdint.h>
#include <string.h>

#define EXEC_GC_REF_BASE UINT32_C(0x80000000)

/* References use nan_mode as engine-private metadata.  The field is otherwise
 * irrelevant for reference values and travels with values through locals,
 * globals, and aggregate fields.  Encoding type+1 leaves an all-zero value as
 * the natural "use the public value type" default for host-provided refs. */
wasm_valtype reference_dynamic_type(const wasm_value *value) {
    uint32_t encoded = 0;
    memcpy(&encoded, value->nan_mode, sizeof(encoded));
    return encoded ? (wasm_valtype)(encoded - 1u) : value->type;
}

void set_reference_dynamic_type(wasm_value *value,
                                wasm_valtype dynamic_type) {
    uint32_t encoded = (uint32_t)dynamic_type + 1u;
    memcpy(value->nan_mode, &encoded, sizeof(encoded));
}

exec_gc_object *gc_object(waste_exec_engine *eng,
                          const wasm_value *value) {
    if (!eng || value->ref == UINT32_MAX ||
        (value->ref & EXEC_GC_REF_BASE) == 0)
        return NULL;
    uint32_t index = value->ref & ~EXEC_GC_REF_BASE;
    return index < eng->gc_object_count ? &eng->gc_objects[index] : NULL;
}

wasm_value default_value(wasm_valtype type) {
    wasm_value value;
    memset(&value, 0, sizeof(value));
    value.type = type;
    if (is_reference_type(type)) {
        value.ref = UINT32_MAX;
        set_reference_dynamic_type(&value, type);
    }
    return value;
}

int gc_allocate(waste_exec_engine *eng, wast_type_kind kind,
                uint32_t type_index, uint32_t length,
                wasm_value *out, exec_error *err) {
    if (length > (uint32_t)(EXEC_MAX_GC_OBJECT_BYTES /
                            sizeof(*eng->gc_objects[0].values))) {
        exec_fail(err, EXEC_ERROR_TRAP, "GC allocation failed");
        return 0;
    }
    if (eng->gc_object_count >= UINT32_C(0x7ffffffe)) {
        exec_fail(err, EXEC_ERROR_TRAP, "GC object limit exceeded");
        return 0;
    }
    if (eng->gc_object_count == eng->gc_object_capacity) {
        uint32_t capacity;
        if (!eng->gc_object_capacity)
            capacity = 16u;
        else if (eng->gc_object_capacity > UINT32_C(0x3fffffff))
            capacity = UINT32_C(0x7ffffffe);
        else
            capacity = eng->gc_object_capacity * 2u;
#if SIZE_MAX <= UINT32_MAX
        if (capacity > SIZE_MAX / sizeof(*eng->gc_objects)) {
            exec_fail(err, EXEC_ERROR_TRAP, "GC allocation failed");
            return 0;
        }
#endif
        exec_gc_object *objects =
            (exec_gc_object *)wasm_instance_resize_objects(
                eng->gc_objects, eng->gc_object_capacity, capacity,
                sizeof(*objects), EXEC_ERROR_TRAP, "GC allocation failed",
                err);
        if (!objects) return 0;
        eng->gc_objects = objects;
        eng->gc_object_capacity = capacity;
    }
    uint32_t index = eng->gc_object_count++;
    exec_gc_object *object = &eng->gc_objects[index];
    memset(object, 0, sizeof(*object));
    object->kind = kind;
    object->type_index = type_index;
    object->length = length;
    if (length) {
#if SIZE_MAX <= UINT32_MAX
        if (length > SIZE_MAX / sizeof(*object->values)) {
            exec_fail(err, EXEC_ERROR_TRAP, "GC allocation failed");
            eng->gc_object_count--;
            return 0;
        }
#endif
        object->values = (wasm_value *)wasm_instance_allocate_objects(
            length, sizeof(*object->values), EXEC_ERROR_TRAP,
            "GC allocation failed", err);
        if (!object->values) {
            eng->gc_object_count--;
            return 0;
        }
    }
    memset(out, 0, sizeof(*out));
    out->type = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE + type_index);
    out->ref = EXEC_GC_REF_BASE | index;
    set_reference_dynamic_type(out, out->type);
    return 1;
}

int reference_matches_heap(waste_exec_engine *eng,
                           const wasm_value *value,
                           int32_t heap_type, int nullable) {
    if (!is_reference_type(value->type)) return 0;
    if (value->ref == UINT32_MAX) return nullable;
    wasm_valtype dynamic = reference_dynamic_type(value);
    if (heap_type >= 0) {
        if ((uint32_t)heap_type >= eng->type_count ||
            !WASM_VALTYPE_IS_TYPE_REF(dynamic))
            return 0;
        wasm_valtype required = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE +
                                              (uint32_t)heap_type);
        return global_type_is_compat(eng, dynamic, eng, required, 0);
    }
    switch (heap_type) {
        case -16: /* func */
            return is_function_reference_type(dynamic) ||
                   (WASM_VALTYPE_IS_TYPE_REF(dynamic) &&
                    eng->types[WASM_VALTYPE_TYPE_REF_INDEX(dynamic)].kind ==
                        WAST_TYPE_FUNC);
        case -17: /* extern */
            return value->type == WASM_VALTYPE_EXTERNREF ||
                   value->type == WASM_VALTYPE_EXTERNREF_NONNULL;
        case -18: /* any */
            return value->type != WASM_VALTYPE_FUNCREF &&
                   value->type != WASM_VALTYPE_FUNCREF_NONNULL &&
                   value->type != WASM_VALTYPE_EXNREF &&
                   value->type != WASM_VALTYPE_EXNREF_NONNULL;
        case -19: /* eq */
            if (dynamic == WASM_VALTYPE_I31REF ||
                dynamic == WASM_VALTYPE_I31REF_NONNULL ||
                dynamic == WASM_VALTYPE_STRUCTREF ||
                dynamic == WASM_VALTYPE_STRUCTREF_NONNULL ||
                dynamic == WASM_VALTYPE_ARRAYREF ||
                dynamic == WASM_VALTYPE_ARRAYREF_NONNULL)
                return 1;
            return WASM_VALTYPE_IS_TYPE_REF(dynamic) &&
                eng->types[WASM_VALTYPE_TYPE_REF_INDEX(dynamic)].kind !=
                    WAST_TYPE_FUNC;
        case -20: /* i31 */
            return dynamic == WASM_VALTYPE_I31REF ||
                   dynamic == WASM_VALTYPE_I31REF_NONNULL;
        case -21: /* struct */
            return dynamic == WASM_VALTYPE_STRUCTREF ||
                   dynamic == WASM_VALTYPE_STRUCTREF_NONNULL ||
                   (WASM_VALTYPE_IS_TYPE_REF(dynamic) &&
                    eng->types[WASM_VALTYPE_TYPE_REF_INDEX(dynamic)].kind ==
                        WAST_TYPE_STRUCT);
        case -22: /* array */
            return dynamic == WASM_VALTYPE_ARRAYREF ||
                   dynamic == WASM_VALTYPE_ARRAYREF_NONNULL ||
                   (WASM_VALTYPE_IS_TYPE_REF(dynamic) &&
                    eng->types[WASM_VALTYPE_TYPE_REF_INDEX(dynamic)].kind ==
                        WAST_TYPE_ARRAY);
        default: /* none/nofunc/noextern/noexn have no non-null values */
            return 0;
    }
}

wasm_value packed_field_value(wasm_value value, uint8_t packed) {
    if (packed == 1)
        value.i32 = (int32_t)((uint32_t)value.i32 & 0xffu);
    else if (packed == 2)
        value.i32 = (int32_t)((uint32_t)value.i32 & 0xffffu);
    return value;
}

int gc_push(exec_stack *stack, wasm_value value, exec_error *err) {
    if (stack_push(stack, value)) return 1;
    exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return 0;
}

/* ---- GC instruction handler ---- */

/* Execute non-branching 0xfb instructions.  Returns 1 when handled and -1 on
 * a trap.  br_on_cast(_fail) is integrated with the control-flow loop. */
int exec_gc_instruction(waste_exec_engine *eng,
                               const exec_instr *instr,
                               exec_stack *stack, exec_error *err) {
    uint32_t op = instr->simd_op;
    wasm_value a, b, c, d, e, out;
    exec_func_type *type;
    exec_gc_object *object;
    uint32_t type_index = instr->u32_imm;

    if (op == 0x1a || op == 0x1b) {
        if (!stack_pop(stack, &out) || !is_reference_type(out.type)) {
            exec_fail(err, EXEC_ERROR_TRAP, "reference conversion operand missing");
            return -1;
        }
        wasm_valtype dynamic = reference_dynamic_type(&out);
        out.type = op == 0x1a ?
            (out.ref == UINT32_MAX ? WASM_VALTYPE_ANYREF :
             (WASM_VALTYPE_IS_TYPE_REF(dynamic) ||
              dynamic == WASM_VALTYPE_I31REF_NONNULL ||
              dynamic == WASM_VALTYPE_STRUCTREF_NONNULL ||
              dynamic == WASM_VALTYPE_ARRAYREF_NONNULL ?
                dynamic : WASM_VALTYPE_ANYREF_NONNULL)) :
            (out.ref == UINT32_MAX ? WASM_VALTYPE_EXTERNREF : WASM_VALTYPE_EXTERNREF_NONNULL);
        set_reference_dynamic_type(&out, dynamic);
        return gc_push(stack, out, err) ? 1 : -1;
    }
    if (op == 0x1c) {
        if (!stack_pop(stack, &a) || a.type != WASM_VALTYPE_I32) {
            exec_fail(err, EXEC_ERROR_TRAP, "ref.i31 operand missing");
            return -1;
        }
        memset(&out, 0, sizeof(out));
        out.type = WASM_VALTYPE_I31REF_NONNULL;
        out.ref = (uint32_t)a.i32 & UINT32_C(0x7fffffff);
        set_reference_dynamic_type(&out, out.type);
        return gc_push(stack, out, err) ? 1 : -1;
    }
    if (op == 0x1d || op == 0x1e) {
        if (!stack_pop(stack, &a) ||
            !reference_matches_heap(eng, &a, -20, 0)) {
            exec_fail(err, EXEC_ERROR_TRAP, "i31 reference expected");
            return -1;
        }
        uint32_t bits = a.ref & UINT32_C(0x7fffffff);
        if (op == 0x1d && (bits & UINT32_C(0x40000000)))
            bits |= UINT32_C(0x80000000);
        return gc_push(stack, i32_value(bits), err) ? 1 : -1;
    }
    if (op >= 0x14 && op <= 0x17) {
        if (!stack_pop(stack, &a) || !is_reference_type(a.type)) {
            exec_fail(err, EXEC_ERROR_TRAP, "ref.test/ref.cast operand missing");
            return -1;
        }
        int nullable = op == 0x15 || op == 0x17;
        int matches = reference_matches_heap(eng, &a,
                                             instr->block_type_index,
                                             nullable);
        if (op <= 0x15)
            return gc_push(stack, i32_value((uint32_t)matches), err) ? 1 : -1;
        if (!matches) {
            exec_fail(err, EXEC_ERROR_TRAP, "cast failure");
            return -1;
        }
        if (a.ref != UINT32_MAX) {
            if (instr->block_type_index >= 0)
                a.type = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE +
                                        (uint32_t)instr->block_type_index);
            else
                a.type = nonnullable_reference_type(a.type);
        }
        return gc_push(stack, a, err) ? 1 : -1;
    }

    if ((op <= 0x10 && op != 0x0f) || (op >= 0x11 && op <= 0x13)) {
        if (type_index >= eng->type_count) {
            exec_fail(err, EXEC_ERROR_TRAP, "GC type index out of range");
            return -1;
        }
        type = &eng->types[type_index];
    } else type = NULL;

    if (op == 0x00 || op == 0x01) {
        if (type->kind != WAST_TYPE_STRUCT ||
            !gc_allocate(eng, WAST_TYPE_STRUCT, type_index,
                         (uint32_t)type->field_count, &out, err)) return -1;
        object = gc_object(eng, &out);
        for (int i = type->field_count; i-- > 0;) {
            if (op == 0x00) {
                if (!stack_pop(stack, &a)) {
                    exec_fail(err, EXEC_ERROR_TRAP, "struct.new operand missing");
                    return -1;
                }
                object->values[i] = packed_field_value(a, type->field_packed[i]);
            } else object->values[i] = default_value(type->fields[i]);
        }
        return gc_push(stack, out, err) ? 1 : -1;
    }
    if (op >= 0x02 && op <= 0x05) {
        uint32_t field = instr->memory_index;
        if (type->kind != WAST_TYPE_STRUCT || field >= (uint32_t)type->field_count) {
            exec_fail(err, EXEC_ERROR_TRAP, "struct field index out of range");
            return -1;
        }
        if (op == 0x05 && !stack_pop(stack, &b)) {
            exec_fail(err, EXEC_ERROR_TRAP, "struct.set value missing"); return -1;
        }
        if (!stack_pop(stack, &a) || !(object = gc_object(eng, &a)) ||
            object->kind != WAST_TYPE_STRUCT ||
            !reference_matches_heap(eng, &a, (int32_t)type_index, 0)) {
            exec_fail(err, EXEC_ERROR_TRAP, a.ref == UINT32_MAX ?
                      "null struct reference" : "struct reference type mismatch");
            return -1;
        }
        if (field >= object->length) {
            exec_fail(err, EXEC_ERROR_TRAP, "struct field index out of range"); return -1;
        }
        if (op == 0x05) object->values[field] =
            packed_field_value(b, type->field_packed[field]);
        else {
            out = object->values[field];
            uint8_t packed = type->field_packed[field];
            uint32_t bits = (uint32_t)out.i32;
            if (packed == 1) bits &= 0xffu;
            else if (packed == 2) bits &= 0xffffu;
            if (op == 0x03 && packed == 1 && (bits & 0x80u)) bits |= 0xffffff00u;
            if (op == 0x03 && packed == 2 && (bits & 0x8000u)) bits |= 0xffff0000u;
            if (packed) out = i32_value(bits);
            if (!gc_push(stack, out, err)) return -1;
        }
        return 1;
    }
    if (op == 0x06 || op == 0x07 || op == 0x08) {
        uint32_t length;
        if (type->kind != WAST_TYPE_ARRAY) {
            exec_fail(err, EXEC_ERROR_TRAP, "array type expected"); return -1;
        }
        if (op == 0x08) length = instr->lane_index;
        else {
            if (!stack_pop(stack, &a) || a.type != WASM_VALTYPE_I32) {
                exec_fail(err, EXEC_ERROR_TRAP, "array length missing"); return -1;
            }
            length = (uint32_t)a.i32;
        }
        if (!gc_allocate(eng, WAST_TYPE_ARRAY, type_index, length, &out, err))
            return -1;
        object = gc_object(eng, &out);
        if (op == 0x07) {
            a = default_value(type->fields[0]);
            for (uint32_t i = 0; i < length; i++) object->values[i] = a;
        } else if (op == 0x06) {
            if (!stack_pop(stack, &a)) {
                exec_fail(err, EXEC_ERROR_TRAP, "array initializer missing"); return -1;
            }
            a = packed_field_value(a, type->field_packed[0]);
            for (uint32_t i = 0; i < length; i++) object->values[i] = a;
        } else {
            for (uint32_t i = length; i-- > 0;) {
                if (!stack_pop(stack, &a)) {
                    exec_fail(err, EXEC_ERROR_TRAP, "array initializer missing"); return -1;
                }
                object->values[i] = packed_field_value(a, type->field_packed[0]);
            }
        }
        return gc_push(stack, out, err) ? 1 : -1;
    }
    if (op == 0x09 || op == 0x0a) {
        uint32_t segment = instr->memory_index;
        if (type->kind != WAST_TYPE_ARRAY ||
            !stack_pop(stack, &b) || b.type != WASM_VALTYPE_I32 ||
            !stack_pop(stack, &a) || a.type != WASM_VALTYPE_I32) {
            exec_fail(err, EXEC_ERROR_TRAP, "array segment constructor operands missing");
            return -1;
        }
        uint32_t source = (uint32_t)a.i32, length = (uint32_t)b.i32;
        if (!gc_allocate(eng, WAST_TYPE_ARRAY, type_index, length, &out, err))
            return -1;
        object = gc_object(eng, &out);
        if (op == 0x09) {
            uint32_t width = type->field_packed[0] == 1 ? 1u :
                             type->field_packed[0] == 2 ? 2u :
                             type->fields[0] == WASM_VALTYPE_I64 ||
                             type->fields[0] == WASM_VALTYPE_F64 ? 8u : 4u;
            uint32_t segment_bytes = segment < eng->data_count &&
                !eng->data_dropped[segment] ? eng->data_seg_lengths[segment] : 0;
            if (segment >= eng->data_count || source > segment_bytes ||
                length > (segment_bytes - source) / width) {
                exec_fail(err, EXEC_ERROR_TRAP, "out of bounds array data access"); return -1;
            }
            for (uint32_t i = 0; i < length; i++) {
                uint64_t bits = load_le(eng->data_segs[segment],
                                        source + (size_t)i * width, width);
                object->values[i] = default_value(type->fields[0]);
                if (type->fields[0] == WASM_VALTYPE_I64) object->values[i].i64 = (int64_t)bits;
                else if (type->fields[0] == WASM_VALTYPE_F32) { uint32_t x=(uint32_t)bits; memcpy(&object->values[i].f32,&x,4); }
                else if (type->fields[0] == WASM_VALTYPE_F64) memcpy(&object->values[i].f64,&bits,8);
                else object->values[i].i32 = (int32_t)bits;
            }
        } else {
            uint32_t segment_length = segment < eng->elem_count &&
                !eng->elem_dropped[segment] ? eng->elem_lengths[segment] : 0;
            if (segment >= eng->elem_count || source > segment_length ||
                length > segment_length - source) {
                exec_fail(err, EXEC_ERROR_TRAP, "out of bounds array element access"); return -1;
            }
            for (uint32_t i = 0; i < length; i++) {
                exec_table_element e = eng->elem_values[segment][source + i];
                object->values[i] = default_value(type->fields[0]);
                if (e.owner) {
                    object->values[i].type = e.type;
                    object->values[i].ref = e.func_idx;
                    set_reference_dynamic_type(&object->values[i], e.dynamic_type);
                }
            }
        }
        return gc_push(stack, out, err) ? 1 : -1;
    }
    if (op >= 0x0b && op <= 0x0e) {
        if (op == 0x0e && !stack_pop(stack, &b)) {
            exec_fail(err, EXEC_ERROR_TRAP, "array.set value missing"); return -1;
        }
        if (!stack_pop(stack, &a) || a.type != WASM_VALTYPE_I32 ||
            !stack_pop(stack, &c) || !(object = gc_object(eng, &c)) ||
            object->kind != WAST_TYPE_ARRAY || (uint32_t)a.i32 >= object->length) {
            exec_fail(err, EXEC_ERROR_TRAP, c.ref == UINT32_MAX ?
                      "null array reference" : "out of bounds array access"); return -1;
        }
        if (op == 0x0e) object->values[(uint32_t)a.i32] =
            packed_field_value(b, type->field_packed[0]);
        else {
            out = object->values[(uint32_t)a.i32];
            uint8_t packed = type->field_packed[0];
            uint32_t bits = (uint32_t)out.i32;
            if (packed == 1) bits &= 0xffu; else if (packed == 2) bits &= 0xffffu;
            if (op == 0x0c && packed == 1 && (bits & 0x80u)) bits |= 0xffffff00u;
            if (op == 0x0c && packed == 2 && (bits & 0x8000u)) bits |= 0xffff0000u;
            if (packed) out = i32_value(bits);
            if (!gc_push(stack, out, err)) return -1;
        }
        return 1;
    }
    if (op == 0x0f) {
        if (!stack_pop(stack, &a) || !(object = gc_object(eng, &a)) ||
            object->kind != WAST_TYPE_ARRAY) {
            exec_fail(err, EXEC_ERROR_TRAP, a.ref == UINT32_MAX ?
                      "null array reference" : "array reference expected"); return -1;
        }
        return gc_push(stack, i32_value(object->length), err) ? 1 : -1;
    }
    if (op == 0x10) {
        if (!stack_pop(stack, &d) || d.type != WASM_VALTYPE_I32 ||
            !stack_pop(stack, &c) || !stack_pop(stack, &b) || b.type != WASM_VALTYPE_I32 ||
            !stack_pop(stack, &a) || !(object = gc_object(eng, &a)) ||
            object->kind != WAST_TYPE_ARRAY) {
            exec_fail(err, EXEC_ERROR_TRAP, "array.fill operands missing"); return -1;
        }
        uint32_t offset=(uint32_t)b.i32, length=(uint32_t)d.i32;
        if (offset > object->length || length > object->length-offset) {
            exec_fail(err, EXEC_ERROR_TRAP, "out of bounds array access"); return -1;
        }
        c = packed_field_value(c, type->field_packed[0]);
        for (uint32_t i=0;i<length;i++) object->values[offset+i]=c;
        return 1;
    }
    if (op == 0x11 || op == 0x12) {
        uint32_t segment = instr->memory_index;
        if (type->kind != WAST_TYPE_ARRAY ||
            !stack_pop(stack,&d) || d.type!=WASM_VALTYPE_I32 ||
            !stack_pop(stack,&c) || c.type!=WASM_VALTYPE_I32 ||
            !stack_pop(stack,&b) || b.type!=WASM_VALTYPE_I32 ||
            !stack_pop(stack,&a) || !(object=gc_object(eng,&a)) ||
            object->kind!=WAST_TYPE_ARRAY) {
            exec_fail(err,EXEC_ERROR_TRAP,"array.init operands missing"); return -1;
        }
        uint32_t dst=(uint32_t)b.i32, src=(uint32_t)c.i32,
                 len=(uint32_t)d.i32;
        if (dst>object->length || len>object->length-dst) {
            exec_fail(err,EXEC_ERROR_TRAP,"out of bounds array access"); return -1;
        }
        if (op==0x11) {
            uint32_t width=type->field_packed[0]==1?1u:
                           type->field_packed[0]==2?2u:
                           type->fields[0]==WASM_VALTYPE_I64 ||
                           type->fields[0]==WASM_VALTYPE_F64?8u:4u;
            uint32_t segment_bytes=segment<eng->data_count &&
                !eng->data_dropped[segment]?eng->data_seg_lengths[segment]:0;
            if (segment>=eng->data_count || src>segment_bytes ||
                len>(segment_bytes-src)/width) {
                exec_fail(err,EXEC_ERROR_TRAP,"out of bounds array data access"); return -1;
            }
            for(uint32_t i=0;i<len;i++) {
                uint64_t bits=load_le(eng->data_segs[segment],
                    src+(size_t)i*width,width);
                wasm_value v=default_value(type->fields[0]);
                if(type->fields[0]==WASM_VALTYPE_I64)v.i64=(int64_t)bits;
                else if(type->fields[0]==WASM_VALTYPE_F32){uint32_t x=(uint32_t)bits;memcpy(&v.f32,&x,4);}
                else if(type->fields[0]==WASM_VALTYPE_F64)memcpy(&v.f64,&bits,8);
                else v.i32=(int32_t)bits;
                object->values[dst+i]=v;
            }
        } else {
            uint32_t segment_length=segment<eng->elem_count &&
                !eng->elem_dropped[segment]?eng->elem_lengths[segment]:0;
            if(segment>=eng->elem_count || src>segment_length ||
               len>segment_length-src){
                exec_fail(err,EXEC_ERROR_TRAP,"out of bounds array element access");return -1;
            }
            for(uint32_t i=0;i<len;i++){
                exec_table_element el=eng->elem_values[segment][src+i];
                wasm_value v=default_value(type->fields[0]);
                if(el.owner){v.type=el.type;v.ref=el.func_idx;set_reference_dynamic_type(&v,el.dynamic_type);}
                object->values[dst+i]=v;
            }
        }
        return 1;
    }
    if (op == 0x13) {
        if (instr->memory_index >= eng->type_count ||
            !stack_pop(stack,&d) || d.type!=WASM_VALTYPE_I32 ||
            !stack_pop(stack,&c) || c.type!=WASM_VALTYPE_I32 ||
            !stack_pop(stack,&b) || !(object=gc_object(eng,&b)) ||
            !stack_pop(stack,&e) || e.type!=WASM_VALTYPE_I32 ||
            !stack_pop(stack,&a)) {
            exec_fail(err,EXEC_ERROR_TRAP,"array.copy operands missing"); return -1;
        }
        exec_gc_object *destination=gc_object(eng,&a);
        uint32_t dst=(uint32_t)e.i32, src=(uint32_t)c.i32, len=(uint32_t)d.i32;
        if (!destination || destination->kind!=WAST_TYPE_ARRAY ||
            object->kind!=WAST_TYPE_ARRAY || dst>destination->length ||
            len>destination->length-dst || src>object->length ||
            len>object->length-src) {
            exec_fail(err,EXEC_ERROR_TRAP,"out of bounds array access"); return -1;
        }
        if(destination==object)
            memmove(destination->values+dst,object->values+src,
                    (size_t)len*sizeof(*object->values));
        else
            memcpy(destination->values+dst,object->values+src,
                   (size_t)len*sizeof(*object->values));
        return 1;
    }
    return 0;
}
