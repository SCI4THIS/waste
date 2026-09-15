#include "../runtime_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

exec_status exec_table_get_set(waste_exec_context *context,
                               const exec_instr *instr,
                               exec_error *err) {
    waste_exec_engine *eng = context->engine;
    exec_stack *stack = context->operand_stack;
    exec_table *table = eng->tables[instr->u32_imm];
    wasm_value reference;
    wasm_value index;

    if (instr->opcode == 0x26 && !stack_pop(stack, &reference))
        return exec_fail(err, EXEC_ERROR_TRAP, "table.set value missing");
    if (!stack_pop(stack, &index))
        return exec_fail(err, EXEC_ERROR_TRAP,
                         "table index operand missing");

    uint64_t element_index = table->is_64 ? (uint64_t)index.i64 :
                                             (uint64_t)(uint32_t)index.i32;
    if (element_index >= table->size)
        return exec_fail(err, EXEC_ERROR_TRAP,
                         "out of bounds table access");

    if (instr->opcode == 0x25) {
        exec_table_element element = table->elements[(size_t)element_index];
        memset(&reference, 0, sizeof(reference));
        reference.type = element.owner ? element.type : table->element_type;
        reference.ref = element.owner ? element.func_idx : UINT32_MAX;
        set_reference_dynamic_type(
            &reference,
            element.owner ? element.dynamic_type : table->element_type);
        if (!stack_push(stack, reference))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    } else {
        exec_table_element element = {NULL, 0, table->element_type,
                                      table->element_type};
        if (reference.ref != UINT32_MAX) {
            element.owner = eng;
            element.func_idx = reference.ref;
            element.type = reference.type;
            element.dynamic_type = reference_dynamic_type(&reference);
        }
        table->elements[(size_t)element_index] = element;
    }
    return EXEC_OK;
}

exec_status exec_table_bulk(waste_exec_context *context,
                            const exec_instr *instr, uint32_t subopcode,
                            exec_error *err) {
    waste_exec_engine *eng = context->engine;
    exec_stack *stack = context->operand_stack;

    if (subopcode == 12) { /* table.init */
        wasm_value n_v, src_v, dst_v;
        if (!stack_pop(stack, &n_v) || !stack_pop(stack, &src_v) ||
            !stack_pop(stack, &dst_v))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "table.init operands missing");
        uint32_t elem = instr->u32_imm;
        uint32_t table_index = instr->v128_imm.bytes[0];
        if (elem >= eng->elem_count || table_index >= eng->table_count)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "table.init index out of range");
        exec_table *table = eng->tables[table_index];
        uint32_t n = (uint32_t)n_v.i32;
        uint32_t src = (uint32_t)src_v.i32;
        uint64_t dst;
        if (!address_value(&dst_v, table->is_64, &dst) ||
            src_v.type != WASM_VALTYPE_I32 || n_v.type != WASM_VALTYPE_I32)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "table.init operand type mismatch");
        uint32_t length = eng->elem_dropped[elem] ? 0 :
                          eng->elem_lengths[elem];
        if (src > length || n > length - src ||
            dst > table->size || n > table->size - dst)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "out of bounds table access");
        if (n)
            memcpy(table->elements + (size_t)dst,
                   eng->elem_values[elem] + src,
                   (size_t)n * sizeof(exec_table_element));
        return EXEC_OK;
    }

    if (subopcode == 13) { /* elem.drop */
        if (instr->u32_imm >= eng->elem_count)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "element index out of range");
        eng->elem_dropped[instr->u32_imm] = 1;
        return EXEC_OK;
    }

    if (subopcode == 15) { /* table.grow */
        wasm_value delta_v, init_v;
        if (!stack_pop(stack, &delta_v) || !stack_pop(stack, &init_v))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "table.grow operands missing");
        uint32_t table_index = instr->u32_imm;
        if (table_index >= eng->table_count || !eng->tables[table_index])
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "table index out of range");
        exec_table *table = eng->tables[table_index];
        uint64_t old_size = table->size;
        uint64_t delta;
        if (!address_value(&delta_v, table->is_64, &delta))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "table.grow operand type mismatch");
        int failed = old_size > UINT64_MAX - delta;
        uint64_t new_size = failed ? 0 : old_size + delta;
        if (failed || (!table->is_64 && new_size > UINT32_MAX) ||
            (table->has_max && new_size > table->max_size) ||
            new_size > SIZE_MAX / sizeof(exec_table_element)) {
            wasm_value failure = table->is_64 ? i64_value(UINT64_MAX) :
                                                i32_value(UINT32_MAX);
            if (!stack_push(stack, failure))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            return EXEC_OK;
        }

        exec_table_element *elements = (exec_table_element *)realloc(
            table->elements,
            new_size ? (size_t)new_size * sizeof(*elements) : 1);
        if (!elements) {
            wasm_value failure = table->is_64 ? i64_value(UINT64_MAX) :
                                                i32_value(UINT32_MAX);
            if (!stack_push(stack, failure))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            return EXEC_OK;
        }
        for (uint64_t i = old_size; i < new_size; i++) {
            elements[i].owner = init_v.ref == UINT32_MAX ? NULL : eng;
            elements[i].func_idx = init_v.ref == UINT32_MAX ? 0 : init_v.ref;
            elements[i].type = init_v.ref == UINT32_MAX ?
                               table->element_type : init_v.type;
            elements[i].dynamic_type = init_v.ref == UINT32_MAX ?
                table->element_type : reference_dynamic_type(&init_v);
        }
        table->elements = elements;
        table->size = new_size;
        wasm_value old_value = table->is_64 ? i64_value(old_size) :
                                              i32_value((uint32_t)old_size);
        if (!stack_push(stack, old_value))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }

    if (subopcode == 16) { /* table.size */
        uint32_t table_index = instr->u32_imm;
        if (table_index >= eng->table_count || !eng->tables[table_index])
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "table index out of range");
        exec_table *table = eng->tables[table_index];
        wasm_value size = table->is_64 ? i64_value(table->size) :
                                         i32_value((uint32_t)table->size);
        if (!stack_push(stack, size))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }

    if (subopcode == 14) { /* table.copy */
        wasm_value n_v, src_v, dst_v;
        if (!stack_pop(stack, &n_v) || !stack_pop(stack, &src_v) ||
            !stack_pop(stack, &dst_v))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "table.copy operands missing");
        uint32_t dst_index = instr->u32_imm;
        uint32_t src_index = instr->v128_imm.bytes[0];
        if (dst_index >= eng->table_count || src_index >= eng->table_count)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "table index out of range");
        exec_table *dst_table = eng->tables[dst_index];
        exec_table *src_table = eng->tables[src_index];
        uint64_t dst, src, n;
        int length_is_64 = dst_table->is_64 && src_table->is_64;
        if (!address_value(&dst_v, dst_table->is_64, &dst) ||
            !address_value(&src_v, src_table->is_64, &src) ||
            !address_value(&n_v, length_is_64, &n))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "table.copy operand type mismatch");
        if (dst > dst_table->size || n > dst_table->size - dst ||
            src > src_table->size || n > src_table->size - src ||
            n > SIZE_MAX / sizeof(exec_table_element))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "out of bounds table access");
        memmove(dst_table->elements + (size_t)dst,
                src_table->elements + (size_t)src,
                (size_t)n * sizeof(exec_table_element));
        return EXEC_OK;
    }

    if (subopcode == 17) { /* table.fill */
        wasm_value n_v, value, dst_v;
        if (!stack_pop(stack, &n_v) || !stack_pop(stack, &value) ||
            !stack_pop(stack, &dst_v))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "table.fill operands missing");
        uint32_t table_index = instr->u32_imm;
        if (table_index >= eng->table_count || !eng->tables[table_index])
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "table index out of range");
        exec_table *table = eng->tables[table_index];
        uint64_t dst, n;
        if (!address_value(&dst_v, table->is_64, &dst) ||
            !address_value(&n_v, table->is_64, &n))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "table.fill operand type mismatch");
        if (dst > table->size || n > table->size - dst)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "out of bounds table access");
        exec_table_element fill;
        if (value.ref == UINT32_MAX) {
            fill.owner = NULL;
            fill.func_idx = 0;
            fill.type = table->element_type;
            fill.dynamic_type = table->element_type;
        } else {
            fill.owner = eng;
            fill.func_idx = value.ref;
            fill.type = value.type;
            fill.dynamic_type = reference_dynamic_type(&value);
        }
        for (uint64_t i = 0; i < n; i++)
            table->elements[(size_t)(dst + i)] = fill;
        return EXEC_OK;
    }

    return exec_fail(err, EXEC_ERROR_TRAP,
                     "unsupported table bulk instruction");
}
