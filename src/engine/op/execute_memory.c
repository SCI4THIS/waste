#include "../runtime_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- Stack and value helpers ---- */

int stack_push(exec_stack *s, wasm_value v) {
    if (s->top >= EXEC_MAX_STACK) return 0;
    s->vals[s->top++] = v;
    return 1;
}

int stack_pop(exec_stack *s, wasm_value *out) {
    if (s->top <= 0) return 0;
    *out = s->vals[--s->top];
    return 1;
}

/* ---- Arithmetic and memory helpers ---- */

int address_value(const wasm_value *value, int is_64, uint64_t *out) {
    if (value->type != (is_64 ? WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
        return 0;
    *out = is_64 ? (uint64_t)value->i64 : (uint64_t)(uint32_t)value->i32;
    return 1;
}

uint64_t load_le(const uint8_t *memory, size_t address, uint32_t width) {
    uint64_t value = 0;
    for (uint32_t i = 0; i < width; i++) value |= (uint64_t)memory[address + i] << (8u * i);
    return value;
}

void store_le(uint8_t *memory, size_t address, uint64_t value, uint32_t width) {
    for (uint32_t i = 0; i < width; i++) memory[address + i] = (uint8_t)(value >> (8u * i));
}

exec_status memory_address(exec_memory *memory, uint64_t base, uint64_t offset,
                           uint32_t width, size_t *address,
                           exec_error *err) {
    uint64_t size = memory ? (uint64_t)memory->pages * EXEC_PAGE_SIZE : 0;
    if (!memory || base > UINT64_MAX - offset)
        return exec_fail(err, EXEC_ERROR_TRAP, "out of bounds memory access");
    uint64_t effective = base + offset;
    if (effective > size || width > size - effective || effective > SIZE_MAX)
        return exec_fail(err, EXEC_ERROR_TRAP, "out of bounds memory access");
    *address = (size_t)effective;
    return EXEC_OK;
}

exec_status exec_memory_instruction(waste_exec_context *context,
                                    const exec_instr *instr,
                                    exec_error *err) {
    waste_exec_engine *eng = context->engine;
    exec_stack *stack = context->operand_stack;

    if (instr->opcode >= 0x28 && instr->opcode <= 0x35) {
        wasm_value base, value;
        uint32_t width;
        size_t address = 0;
        int sign = 0;
        exec_memory *memory = eng->memories[instr->memory_index];
        if (!stack_pop(stack, &base))
            return exec_fail(err, EXEC_ERROR_TRAP, "load address missing");
        switch (instr->opcode) {
            case 0x28: width = 4; value.type = WASM_VALTYPE_I32; break;
            case 0x29: width = 8; value.type = WASM_VALTYPE_I64; break;
            case 0x2a: width = 4; value.type = WASM_VALTYPE_F32; break;
            case 0x2b: width = 8; value.type = WASM_VALTYPE_F64; break;
            case 0x2c: width = 1; value.type = WASM_VALTYPE_I32; sign = 1; break;
            case 0x2d: width = 1; value.type = WASM_VALTYPE_I32; break;
            case 0x2e: width = 2; value.type = WASM_VALTYPE_I32; sign = 1; break;
            case 0x2f: width = 2; value.type = WASM_VALTYPE_I32; break;
            case 0x30: width = 1; value.type = WASM_VALTYPE_I64; sign = 1; break;
            case 0x31: width = 1; value.type = WASM_VALTYPE_I64; break;
            case 0x32: width = 2; value.type = WASM_VALTYPE_I64; sign = 1; break;
            case 0x33: width = 2; value.type = WASM_VALTYPE_I64; break;
            case 0x34: width = 4; value.type = WASM_VALTYPE_I64; sign = 1; break;
            default: width = 4; value.type = WASM_VALTYPE_I64; break;
        }
        uint64_t base_address = memory->is_64 ? (uint64_t)base.i64 :
                                               (uint64_t)(uint32_t)base.i32;
        exec_status status = memory_address(memory, base_address,
                                            instr->u64_imm, width,
                                            &address, err);
        if (status != EXEC_OK) return status;
        uint64_t bits = load_le(memory->data, address, width);
        if (sign && width < 8 &&
            (bits & ((uint64_t)1 << (width * 8u - 1u))))
            bits |= UINT64_MAX << (width * 8u);
        memset(value.nan_mode, 0, sizeof(value.nan_mode));
        if (value.type == WASM_VALTYPE_I32)
            value.i32 = (int32_t)bits;
        else if (value.type == WASM_VALTYPE_I64)
            value.i64 = (int64_t)bits;
        else if (value.type == WASM_VALTYPE_F32) {
            uint32_t word = (uint32_t)bits;
            memcpy(&value.f32, &word, sizeof(word));
        } else {
            memcpy(&value.f64, &bits, sizeof(bits));
        }
        if (!stack_push(stack, value))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }

    if (instr->opcode >= 0x36 && instr->opcode <= 0x3e) {
        wasm_value value, base;
        uint32_t width;
        size_t address = 0;
        uint64_t bits;
        exec_memory *memory = eng->memories[instr->memory_index];
        if (!stack_pop(stack, &value) || !stack_pop(stack, &base))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "store operands missing");
        switch (instr->opcode) {
            case 0x36: width = 4; bits = (uint32_t)value.i32; break;
            case 0x37: width = 8; bits = (uint64_t)value.i64; break;
            case 0x38: {
                uint32_t word;
                width = 4;
                memcpy(&word, &value.f32, sizeof(word));
                bits = word;
                break;
            }
            case 0x39:
                width = 8;
                memcpy(&bits, &value.f64, sizeof(bits));
                break;
            case 0x3a: width = 1; bits = (uint32_t)value.i32; break;
            case 0x3b: width = 2; bits = (uint32_t)value.i32; break;
            case 0x3c: width = 1; bits = (uint64_t)value.i64; break;
            case 0x3d: width = 2; bits = (uint64_t)value.i64; break;
            default: width = 4; bits = (uint64_t)value.i64; break;
        }
        uint64_t base_address = memory->is_64 ? (uint64_t)base.i64 :
                                               (uint64_t)(uint32_t)base.i32;
        exec_status status = memory_address(memory, base_address,
                                            instr->u64_imm, width,
                                            &address, err);
        if (status != EXEC_OK) return status;
        store_le(memory->data, address, bits, width);
        return EXEC_OK;
    }

    exec_memory *memory = instr->memory_index < eng->memory_count ?
                          eng->memories[instr->memory_index] : NULL;
    if (!memory)
        return exec_fail(err, EXEC_ERROR_TRAP, "memory missing");
    if (instr->opcode == 0x3f) {
        wasm_value size = memory->is_64 ? i64_value(memory->pages) :
                                         i32_value((uint32_t)memory->pages);
        if (!stack_push(stack, size))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }

    wasm_value delta;
    if (!stack_pop(stack, &delta) ||
        delta.type != (memory->is_64 ? WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
        return exec_fail(err, EXEC_ERROR_TRAP,
                         "memory.grow operand missing");
    uint64_t old_pages = memory->pages;
    uint64_t add = memory->is_64 ? (uint64_t)delta.i64 :
                                   (uint64_t)(uint32_t)delta.i32;
    uint64_t limit = memory->is_64 ? UINT64_C(0x1000000000000) :
                                     UINT64_C(65536);
    int failed = old_pages > UINT64_MAX - add;
    uint64_t pages = failed ? 0 : old_pages + add;
    if (failed || pages > limit ||
        (memory->has_max && pages > memory->max_pages) ||
        pages > SIZE_MAX / EXEC_PAGE_SIZE) {
        wasm_value failure = memory->is_64 ? i64_value(UINT64_MAX) :
                                             i32_value(UINT32_MAX);
        if (!stack_push(stack, failure))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }
    size_t new_size = (size_t)pages * EXEC_PAGE_SIZE;
    uint8_t *grown = (uint8_t *)calloc(new_size ? new_size : 1, 1);
    if (!grown) {
        wasm_value failure = memory->is_64 ? i64_value(UINT64_MAX) :
                                             i32_value(UINT32_MAX);
        if (!stack_push(stack, failure))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }
    memcpy(grown, memory->data, (size_t)old_pages * EXEC_PAGE_SIZE);
    free(memory->data);
    memory->data = grown;
    memory->pages = pages;
    wasm_value old = memory->is_64 ? i64_value(old_pages) :
                                     i32_value((uint32_t)old_pages);
    if (!stack_push(stack, old))
        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

exec_status exec_memory_bulk(waste_exec_context *context,
                             const exec_instr *instr, uint32_t subopcode,
                             exec_error *err) {
    waste_exec_engine *eng = context->engine;
    exec_stack *stack = context->operand_stack;

    if (subopcode == 10) { /* memory.copy */
        wasm_value n_v, src_v, dst_v;
        if (!stack_pop(stack, &n_v) || !stack_pop(stack, &src_v) ||
            !stack_pop(stack, &dst_v))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory.copy operands missing");
        if (instr->memory_index >= eng->memory_count ||
            instr->source_memory_index >= eng->memory_count)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory index out of range");
        exec_memory *dst_memory = eng->memories[instr->memory_index];
        exec_memory *src_memory = eng->memories[instr->source_memory_index];
        uint64_t dst, src, n;
        int length_is_64 = dst_memory->is_64 && src_memory->is_64;
        if (!address_value(&dst_v, dst_memory->is_64, &dst) ||
            !address_value(&src_v, src_memory->is_64, &src) ||
            !address_value(&n_v, length_is_64, &n))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory.copy operand type mismatch");
        uint64_t dst_size = (uint64_t)dst_memory->pages * EXEC_PAGE_SIZE;
        uint64_t src_size = (uint64_t)src_memory->pages * EXEC_PAGE_SIZE;
        if (dst > dst_size || n > dst_size - dst ||
            src > src_size || n > src_size - src || n > SIZE_MAX)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "out of bounds memory access");
        if (dst_memory == src_memory)
            memmove(dst_memory->data + (size_t)dst,
                    src_memory->data + (size_t)src, (size_t)n);
        else if (n)
            memcpy(dst_memory->data + (size_t)dst,
                   src_memory->data + (size_t)src, (size_t)n);
        return EXEC_OK;
    }

    if (subopcode == 11) { /* memory.fill */
        wasm_value n_v, value, dst_v;
        if (!stack_pop(stack, &n_v) || !stack_pop(stack, &value) ||
            !stack_pop(stack, &dst_v))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory.fill operands missing");
        if (instr->memory_index >= eng->memory_count)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory index out of range");
        exec_memory *memory = eng->memories[instr->memory_index];
        uint64_t dst, n;
        if (!address_value(&dst_v, memory->is_64, &dst) ||
            !address_value(&n_v, memory->is_64, &n) ||
            value.type != WASM_VALTYPE_I32)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory.fill operand type mismatch");
        uint64_t size = (uint64_t)memory->pages * EXEC_PAGE_SIZE;
        if (dst > size || n > size - dst || n > SIZE_MAX)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "out of bounds memory access");
        memset(memory->data + (size_t)dst, (uint8_t)value.i32, (size_t)n);
        return EXEC_OK;
    }

    if (subopcode == 8) { /* memory.init */
        wasm_value n_v, src_v, dst_v;
        if (!stack_pop(stack, &n_v) || !stack_pop(stack, &src_v) ||
            !stack_pop(stack, &dst_v))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory.init operands missing");
        if (instr->memory_index >= eng->memory_count ||
            instr->u32_imm >= eng->data_count)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory.init index out of range");
        exec_memory *memory = eng->memories[instr->memory_index];
        uint64_t dst;
        uint32_t src = (uint32_t)src_v.i32;
        uint32_t n = (uint32_t)n_v.i32;
        if (!address_value(&dst_v, memory->is_64, &dst) ||
            src_v.type != WASM_VALTYPE_I32 || n_v.type != WASM_VALTYPE_I32)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory.init operand type mismatch");
        uint32_t data_length = eng->data_dropped[instr->u32_imm] ? 0 :
            eng->data_seg_lengths[instr->u32_imm];
        uint64_t size = (uint64_t)memory->pages * EXEC_PAGE_SIZE;
        if (dst > size || n > size - dst ||
            src > data_length || n > data_length - src)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "out of bounds memory access");
        if (n)
            memcpy(memory->data + (size_t)dst,
                   eng->data_segs[instr->u32_imm] + src, n);
        return EXEC_OK;
    }

    if (instr->u32_imm >= eng->data_count)
        return exec_fail(err, EXEC_ERROR_TRAP,
                         "data segment index out of range");
    eng->data_dropped[instr->u32_imm] = 1;
    return EXEC_OK;
}
