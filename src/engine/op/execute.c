#include "dispatch_gen.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void runtime_free_jump_snapshots(waste_exec_engine *eng) {
    for (uint32_t i = 0; i < eng->jump_snapshot_count; i++)
        free(eng->jump_snapshots[i].locals);
    free(eng->jump_snapshots);
}

static void invalidate_jump_depth(waste_exec_engine *eng, uint32_t depth) {
    for (uint32_t i = 0; i < eng->jump_snapshot_count; i++)
        if (eng->jump_snapshots[i].valid &&
            eng->jump_snapshots[i].depth == depth)
            eng->jump_snapshots[i].valid = 0;
}

static void invalidate_jump_frame(waste_exec_engine *eng, uint32_t depth,
                                  uint64_t frame_generation) {
    for (uint32_t i = 0; i < eng->jump_snapshot_count; i++) {
        exec_jump_snapshot *snapshot = &eng->jump_snapshots[i];
        if (snapshot->valid && snapshot->depth == depth &&
            snapshot->frame_generation == frame_generation)
            snapshot->valid = 0;
    }
}

static exec_status save_jump_frame(waste_exec_context *ctx,
                                   uint32_t environment) {
    waste_exec_engine *eng = ctx->engine;
    exec_jump_snapshot *snapshot = (void *)0;
    for (uint32_t i = 0; i < eng->jump_snapshot_count; i++)
        if (eng->jump_snapshots[i].environment == environment) {
            snapshot = &eng->jump_snapshots[i];
            break;
        }
    if (!snapshot) {
        if (eng->jump_snapshot_count == eng->jump_snapshot_capacity) {
            uint32_t capacity = eng->jump_snapshot_capacity ?
                                eng->jump_snapshot_capacity * 2u : 8u;
            exec_jump_snapshot *next = (exec_jump_snapshot *)realloc(
                eng->jump_snapshots, (size_t)capacity * sizeof(*next));
            if (!next)
                return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                                 "setjmp snapshot allocation failed");
            memset(next + eng->jump_snapshot_capacity, 0,
                   (size_t)(capacity - eng->jump_snapshot_capacity) *
                   sizeof(*next));
            eng->jump_snapshots = next;
            eng->jump_snapshot_capacity = capacity;
        }
        snapshot = &eng->jump_snapshots[eng->jump_snapshot_count++];
    }
    if (ctx->local_count) {
        wasm_value *next = (wasm_value *)realloc(
            snapshot->locals, (size_t)ctx->local_count * sizeof(*next));
        if (!next)
            return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                             "setjmp locals allocation failed");
        snapshot->locals = next;
        memcpy(snapshot->locals, ctx->locals,
               (size_t)ctx->local_count * sizeof(*ctx->locals));
    }
    snapshot->valid = 1;
    snapshot->environment = environment;
    snapshot->depth = ctx->depth;
    snapshot->frame_generation = ctx->frame_generation;
    snapshot->func_idx = ctx->function_index;
    snapshot->pc = *ctx->pc;
    snapshot->stack = *ctx->operand_stack;
    snapshot->control_top = *ctx->control_top;
    if (*ctx->control_top)
        memcpy(snapshot->controls, ctx->controls,
               (size_t)*ctx->control_top * sizeof(*ctx->controls));
    snapshot->local_count = ctx->local_count;
    return EXEC_OK;
}

static int restore_jump_frame(waste_exec_context *ctx) {
    waste_exec_engine *eng = ctx->engine;
    exec_error *err = ctx->error;
    if (!err || err->status != EXEC_ERROR_LONGJMP ||
        err->jump_owner != eng)
        return 0;
    for (uint32_t i = 0; i < eng->jump_snapshot_count; i++) {
        exec_jump_snapshot *snapshot = &eng->jump_snapshots[i];
        if (!snapshot->valid ||
            snapshot->environment != err->jump_environment ||
            snapshot->depth != ctx->depth ||
            snapshot->frame_generation != ctx->frame_generation ||
            snapshot->func_idx != ctx->function_index)
            continue;
        if (snapshot->local_count != ctx->local_count) {
            exec_fail(err, EXEC_ERROR_TRAP, "setjmp frame shape changed");
            return -1;
        }
        *ctx->pc = snapshot->pc;
        *ctx->operand_stack = snapshot->stack;
        *ctx->control_top = snapshot->control_top;
        if (*ctx->control_top)
            memcpy(ctx->controls, snapshot->controls,
                   (size_t)*ctx->control_top * sizeof(*ctx->controls));
        if (ctx->local_count)
            memcpy(ctx->locals, snapshot->locals,
                   (size_t)ctx->local_count * sizeof(*ctx->locals));
        int32_t value = err->jump_value ? err->jump_value : 1;
        memset(err, 0, sizeof(*err));
        if (!stack_push(ctx->operand_stack, i32_value((uint32_t)value))) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow after longjmp");
            return -1;
        }
        return 1;
    }
    return 0;
}

/* Handle post-call status: exceptions, longjmp, normal errors, and success.
 * Callers must check EXEC_YIELD and restore arguments before calling this. */
static int handle_call_result(waste_exec_context *ctx,
                              exec_status status,
                              const wasm_value *call_results,
                              int call_result_count) {
    if (status == EXEC_ERROR_EXCEPTION) {
        int handled = handle_exception(
            ctx, ctx->error->exception_tag, ctx->error->exception_payload,
            ctx->error->exception_payload_count, ctx->error->exception_owner,
            ctx->error->exception_ref);
        if (handled < 0) return ctx->error->status;
        if (handled == 0) return EXEC_ERROR_EXCEPTION;
        if (handled == 2) return WASM_DISPATCH_RETURN;
        return EXEC_OK;
    }
    if (status == EXEC_ERROR_LONGJMP) {
        int restored = restore_jump_frame(ctx);
        if (restored < 0) return ctx->error->status;
        if (restored > 0) return EXEC_OK;
        invalidate_jump_frame(ctx->engine, ctx->depth, ctx->frame_generation);
        return EXEC_ERROR_LONGJMP;
    }
    if (status != EXEC_OK) return status;
    for (int i = 0; i < call_result_count; i++)
        if (!stack_push(ctx->operand_stack, call_results[i]))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

static void save_yield_frame(waste_exec_context *ctx) {
    waste_exec_engine *eng = ctx->engine;
    eng->yield_frames[ctx->depth].valid = 1;
    eng->yield_frames[ctx->depth].func_idx = ctx->function_index;
    eng->yield_frames[ctx->depth].pc = *ctx->pc;
    eng->yield_frames[ctx->depth].control_top = *ctx->control_top;
}

/* ---- Invoke ---- */

wasm_value i32_value(uint32_t bits) {
    wasm_value value;
    memset(&value, 0, sizeof(value));
    value.type = WASM_VALTYPE_I32;
    value.i32 = (int32_t)bits;
    return value;
}

wasm_value i64_value(uint64_t bits) {
    wasm_value value;
    memset(&value, 0, sizeof(value));
    value.type = WASM_VALTYPE_I64;
    value.i64 = (int64_t)bits;
    return value;
}

static exec_status exec_invoke_frame(waste_exec_engine *eng,
                                     uint32_t func_idx,
                                     const wasm_value *args, int arg_count,
                                     wasm_value *results, int *result_count,
                                     exec_error *err, uint32_t depth);

static exec_status exec_invoke_managed(waste_exec_engine *eng,
                                       uint32_t func_idx,
                                       const wasm_value *args, int arg_count,
                                       wasm_value *results,
                                       int *result_count, exec_error *err) {
    uint32_t depth;
    exec_status status;
    if (!eng) return exec_fail(err, EXEC_ERROR_NOT_FOUND, "null engine");
    depth = eng->active_call_depth;
    if (depth >= EXEC_MAX_CALL_DEPTH)
        return exec_fail(err, EXEC_ERROR_TRAP, "call stack exhausted");
    eng->active_call_depth = depth + 1;
    status = exec_invoke_frame(eng, func_idx, args, arg_count, results,
                               result_count, err, depth);
    eng->active_call_depth = depth;
    return status;
}

/* ---- Dispatch handler functions ---- */

int execute_op_end(waste_exec_context *ctx, const exec_instr *instr) {
    (void)instr;
    if (*ctx->control_top > 0) {
        exec_control target = ctx->controls[--(*ctx->control_top)];
        wasm_value values[WAST_MAX_RESULTS];
        if (target.end_arity > ctx->operand_stack->top - target.stack_height)
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "block results missing");
        for (int i = target.end_arity; i-- > 0;)
            stack_pop(ctx->operand_stack, &values[i]);
        ctx->operand_stack->top = target.stack_height;
        for (int i = 0; i < target.end_arity; i++)
            if (!stack_push(ctx->operand_stack, values[i]))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
    }
    else return WASM_DISPATCH_RETURN;
    return EXEC_OK;
}

int execute_op_throw(waste_exec_context *ctx, const exec_instr *instr) {
    waste_exec_engine *eng = ctx->engine;
    if (instr->u32_imm >= eng->tag_count)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                         "throw tag index out of range");
    uint32_t tag_type_index = eng->tag_types[instr->u32_imm];
    if (tag_type_index >= eng->type_count)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "invalid tag type");
    exec_func_type *tag_type = &eng->types[tag_type_index];
    wasm_value payload[WAST_MAX_PARAMS];
    if (tag_type->param_count > ctx->operand_stack->top)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                         "throw payload missing");
    for (int i = tag_type->param_count; i-- > 0;)
        stack_pop(ctx->operand_stack, &payload[i]);
    exec_raise(ctx->error, eng->tags[instr->u32_imm], payload,
               tag_type->param_count, NULL, UINT32_MAX);
    int handled = handle_exception(
        ctx, eng->tags[instr->u32_imm], payload, tag_type->param_count,
        NULL, UINT32_MAX);
    if (handled < 0) return ctx->error ? ctx->error->status : EXEC_ERROR_TRAP;
    if (handled == 0) return EXEC_ERROR_EXCEPTION;
    if (handled == 2) return WASM_DISPATCH_RETURN;
    return EXEC_OK;
}

int execute_op_throw_ref(waste_exec_context *ctx, const exec_instr *instr) {
    (void)instr;
    waste_exec_engine *eng = ctx->engine;
    wasm_value reference;
    if (!stack_pop(ctx->operand_stack, &reference) ||
        !global_type_is_compat(eng, reference.type, eng,
                               WASM_VALTYPE_EXNREF, 0))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                         "throw_ref operand missing");
    if (reference.ref == UINT32_MAX)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                         "null exception reference");
    exec_exception_object *object = exception_object(eng, &reference);
    if (!object)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                         "invalid exception reference");
    exec_raise(ctx->error, object->tag, object->payload,
               object->payload_count, eng, reference.ref);
    int handled = handle_exception(
        ctx, object->tag, object->payload, object->payload_count,
        eng, reference.ref);
    if (handled < 0) return ctx->error ? ctx->error->status : EXEC_ERROR_TRAP;
    if (handled == 0) return EXEC_ERROR_EXCEPTION;
    if (handled == 2) return WASM_DISPATCH_RETURN;
    return EXEC_OK;
}

int execute_op_unreachable(waste_exec_context *ctx, const exec_instr *instr) {
    (void)instr;
    if (ctx->error) {
        ctx->error->status = EXEC_ERROR_TRAP;
        snprintf(ctx->error->message, sizeof(ctx->error->message),
                 "unreachable in function %u at pc %u",
                 ctx->function_index, *ctx->pc);
    }
    return EXEC_ERROR_TRAP;
}

int execute_op_nop(waste_exec_context *ctx, const exec_instr *instr) {
    (void)ctx; (void)instr;
    return EXEC_OK;
}

int execute_op_block(waste_exec_context *ctx, const exec_instr *instr) {
    int condition = 1;
    if (instr->opcode == 0x04) {
        wasm_value value;
        if (!stack_pop(ctx->operand_stack, &value))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "if condition missing");
        condition = value.i32 != 0;
    }
    if (*ctx->control_top >= EXEC_MAX_CONTROL)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "control stack overflow");
    int parameter_count = instr->v128_imm.bytes[0];
    int result_count_for_block = instr->v128_imm.bytes[1];
    if (parameter_count > ctx->operand_stack->top)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "block parameters missing");
    ctx->controls[(*ctx->control_top)++] = (exec_control){
        instr->opcode, *ctx->pc + 1, instr->resolved_target,
        ctx->operand_stack->top - parameter_count,
        instr->opcode == 0x03 ? parameter_count : result_count_for_block,
        result_count_for_block
    };
    if (!condition) *ctx->pc = instr->u32_imm - 1;
    return EXEC_OK;
}

int execute_op_else(waste_exec_context *ctx, const exec_instr *instr) {
    *ctx->pc = instr->u32_imm - 1;
    return EXEC_OK;
}

int execute_op_branch(waste_exec_context *ctx, const exec_instr *instr) {
    uint32_t depth = instr->resolved_target;
    if (instr->opcode == 0x0d) {
        wasm_value condition;
        if (!stack_pop(ctx->operand_stack, &condition))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "br_if condition missing");
        if (!condition.i32) return EXEC_OK;
    } else if (instr->opcode == 0x0e) {
        wasm_value index;
        if (!stack_pop(ctx->operand_stack, &index))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "br_table index missing");
        uint32_t selected = (uint32_t)index.i32;
        if (selected > instr->u32_imm) selected = instr->u32_imm;
        uint32_t *depths; memcpy(&depths, instr->v128_imm.bytes, sizeof(depths));
        depth = depths[selected];
    } else if (instr->opcode == 0xd5 || instr->opcode == 0xd6) {
        wasm_value reference;
        if (!stack_pop(ctx->operand_stack, &reference) ||
            !is_reference_type(reference.type))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                             "reference branch operand missing");
        int nonnull = reference.ref != UINT32_MAX;
        reference.type = nonnullable_reference_type(reference.type);
        if (instr->opcode == 0xd5 && !nonnull) {
            /* null: take the branch without carrying the reference */
        } else if (instr->opcode == 0xd5) {
            if (!stack_push(ctx->operand_stack, reference))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                                 "stack overflow");
            return EXEC_OK;
        } else if (!nonnull) {
            /* br_on_non_null consumes the null reference on its
             * fall-through path. */
            return EXEC_OK;
        } else if (!stack_push(ctx->operand_stack, reference)) {
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
        }
    } else if (instr->opcode == 0xfb) {
        wasm_value reference;
        int32_t target_heap = (int32_t)instr->lane_index;
        if (!stack_pop(ctx->operand_stack, &reference) ||
            !is_reference_type(reference.type))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                             "br_on_cast reference missing");
        int matches = reference_matches_heap(
            ctx->engine, &reference, target_heap,
            (instr->alignment & 2u) != 0);
        int take = instr->simd_op == 0x18 ? matches : !matches;
        if (!stack_push(ctx->operand_stack, reference))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
        if (!take) return EXEC_OK;
    }
    if (depth > (uint32_t)*ctx->control_top)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "branch depth out of range");
    if (depth == (uint32_t)*ctx->control_top) {
        /* Branch to implicit function body block — act as return */
        if (!stack_carry(ctx->operand_stack, ctx->type->result_count, 0))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "branch values missing");
        return WASM_DISPATCH_RETURN;
    }
    {
    int target_index = *ctx->control_top - 1 - (int)depth;
    exec_control target = ctx->controls[target_index];
    if (!stack_carry(ctx->operand_stack, target.branch_arity, target.stack_height))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "branch values missing");
    if (target.kind == 0x03) {
        *ctx->control_top = target_index + 1;
        *ctx->pc = target.start_pc - 1;
    } else {
        *ctx->control_top = target_index;
        *ctx->pc = target.end_pc;
    }
    }
    return EXEC_OK;
}

int execute_op_local_get(waste_exec_context *ctx, const exec_instr *instr) {
    if (!stack_push(ctx->operand_stack, ctx->locals[instr->u32_imm]))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

int execute_op_local_set(waste_exec_context *ctx, const exec_instr *instr) {
    wasm_value value;
    if (!stack_pop(ctx->operand_stack, &value))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "local value missing");
    ctx->locals[instr->u32_imm] = value;
    if (instr->opcode == 0x22 && !stack_push(ctx->operand_stack, value))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

int execute_op_global(waste_exec_context *ctx, const exec_instr *instr) {
    waste_exec_engine *eng = ctx->engine;
    if (instr->opcode == 0x23) {
        if (!stack_push(ctx->operand_stack, eng->globals[instr->u32_imm]->value))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
    } else {
        wasm_value value;
        exec_global *global = eng->globals[instr->u32_imm];
        if (!stack_pop(ctx->operand_stack, &value))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "global value mismatch");
        value.type = global->value.type;
        global->value = value;
    }
    return EXEC_OK;
}

int execute_op_table(waste_exec_context *ctx, const exec_instr *instr) {
    exec_status status = exec_table_get_set(ctx, instr, ctx->error);
    if (status != EXEC_OK) return status;
    return EXEC_OK;
}

int execute_op_memory(waste_exec_context *ctx, const exec_instr *instr) {
    exec_status status = exec_memory_instruction(ctx, instr, ctx->error);
    if (status != EXEC_OK) return status;
    return EXEC_OK;
}

int execute_op_gc(waste_exec_context *ctx, const exec_instr *instr) {
    /* br_on_cast (0x18/0x19) is handled by the branch logic */
    if (instr->simd_op == 0x18 || instr->simd_op == 0x19)
        return execute_op_branch(ctx, instr);
    int handled = exec_gc_instruction(ctx->engine, instr,
                                      ctx->operand_stack, ctx->error);
    if (handled < 0) return ctx->error ? ctx->error->status : EXEC_ERROR_TRAP;
    if (handled > 0) return EXEC_OK;
    return exec_fail(ctx->error, EXEC_ERROR_UNSUPPORTED, "unsupported opcode");
}

int execute_op_ref_null(waste_exec_context *ctx, const exec_instr *instr) {
    wasm_value value; memset(&value, 0, sizeof(value));
    if (!nullable_reference_for_heap(
            ctx->engine, (int32_t)instr->u32_imm, &value.type))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                         "invalid ref.null heap type");
    value.ref = UINT32_MAX;
    set_reference_dynamic_type(&value, value.type);
    if (!stack_push(ctx->operand_stack, value))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

int execute_op_ref_func(waste_exec_context *ctx, const exec_instr *instr) {
    waste_exec_engine *eng = ctx->engine;
    wasm_value value; memset(&value, 0, sizeof(value));
    uint32_t function_type =
        instr->u32_imm < eng->import_func_count ?
        eng->import_func_types[instr->u32_imm] :
        eng->funcs[instr->u32_imm -
                   eng->import_func_count].type_index;
    value.type = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE +
                                function_type);
    value.ref = instr->u32_imm;
    set_reference_dynamic_type(&value, value.type);
    if (instr->u32_imm >= eng->import_func_count + eng->func_count)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "ref.func index out of range");
    if (!stack_push(ctx->operand_stack, value))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

int execute_op_ref_is_null(waste_exec_context *ctx, const exec_instr *instr) {
    (void)instr;
    wasm_value value;
    if (!stack_pop(ctx->operand_stack, &value) ||
        !is_reference_type(value.type))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "ref.is_null operand missing");
    if (!stack_push(ctx->operand_stack, i32_value(value.ref == UINT32_MAX)))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

int execute_op_ref_eq(waste_exec_context *ctx, const exec_instr *instr) {
    (void)instr;
    wasm_value right, left;
    if (!stack_pop(ctx->operand_stack, &right) || !is_reference_type(right.type) ||
        !stack_pop(ctx->operand_stack, &left) || !is_reference_type(left.type))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                         "ref.eq operands missing");
    if (!stack_push(ctx->operand_stack, i32_value(left.ref == right.ref)))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

int execute_op_ref_as_non_null(waste_exec_context *ctx, const exec_instr *instr) {
    (void)instr;
    wasm_value value;
    if (!stack_pop(ctx->operand_stack, &value) || !is_reference_type(value.type))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                         "ref.as_non_null operand missing");
    if (value.ref == UINT32_MAX)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "null reference");
    value.type = nonnullable_reference_type(value.type);
    if (!stack_push(ctx->operand_stack, value))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

int execute_op_drop(waste_exec_context *ctx, const exec_instr *instr) {
    (void)instr;
    wasm_value ignored;
    if (!stack_pop(ctx->operand_stack, &ignored))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "drop operand missing");
    return EXEC_OK;
}

int execute_op_select(waste_exec_context *ctx, const exec_instr *instr) {
    (void)instr;
    wasm_value condition, second, first;
    if (!stack_pop(ctx->operand_stack, &condition) ||
        !stack_pop(ctx->operand_stack, &second) ||
        !stack_pop(ctx->operand_stack, &first))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "select operands missing");
    if (!stack_push(ctx->operand_stack, condition.i32 ? first : second))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

int execute_op_call(waste_exec_context *ctx, const exec_instr *instr) {
    waste_exec_engine *eng = ctx->engine;
    exec_func_type *callee_type =
        &eng->types[instr->resolved_type_index];
    if (instr->opcode == 0x12) {
        /* return_call: tail call — restart function without recursion */
        for (int i = callee_type->param_count; i-- > 0;)
            if (!stack_pop(ctx->operand_stack, &ctx->tail_args[i]))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "call arguments missing");
        *ctx->tail_arg_count = callee_type->param_count;
        ctx->function_index = instr->u32_imm;
        return WASM_DISPATCH_TAIL_CALL;
    }
    wasm_value call_args[EXEC_MAX_CALL_ARGS], call_results[WAST_MAX_RESULTS]; int call_result_count = 0;
    for (int i = callee_type->param_count; i-- > 0;) {
        if (!stack_pop(ctx->operand_stack, &call_args[i]))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "call arguments missing");
    }
    if (instr->u32_imm < eng->import_func_count &&
        eng->import_controls[instr->u32_imm] ==
            EXEC_HOST_CONTROL_SIGSETJMP) {
        if (callee_type->param_count < 1 ||
            call_args[0].type != WASM_VALTYPE_I32 ||
            callee_type->result_count != 1 ||
            callee_type->results[0] != WASM_VALTYPE_I32)
            return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                             "unsupported sigsetjmp signature");
        exec_status saved = save_jump_frame(
            ctx, (uint32_t)call_args[0].i32);
        if (saved != EXEC_OK) return saved;
        if (!stack_push(ctx->operand_stack, i32_value(0)))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                             "stack overflow after setjmp");
        return EXEC_OK;
    }
    exec_status status = exec_invoke_managed(
        eng, instr->u32_imm, call_args, callee_type->param_count,
        call_results, &call_result_count, ctx->error);
    if (status == EXEC_YIELD) {
        for (int i = 0; i < callee_type->param_count; i++)
            stack_push(ctx->operand_stack, call_args[i]);
        save_yield_frame(ctx);
        return EXEC_YIELD;
    }
    return handle_call_result(ctx, status, call_results, call_result_count);
}

int execute_op_call_indirect(waste_exec_context *ctx, const exec_instr *instr) {
    waste_exec_engine *eng = ctx->engine;
    wasm_value table_operand;
    exec_table *table = eng->tables[instr->simd_op];
    uint64_t element;
    if (!stack_pop(ctx->operand_stack, &table_operand) ||
        !address_value(&table_operand, table->is_64, &element))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "call_indirect table operand missing");
    if (element >= table->size)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "undefined element");
    exec_table_element slot = table->elements[(size_t)element];
    if (!slot.owner)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "uninitialized element");
    waste_exec_engine *teng = slot.owner;
    uint32_t target = slot.func_idx;
    if (target >= teng->import_func_count + teng->func_count)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP, "call_indirect target out of range");
    uint32_t actual_type_index = target < teng->import_func_count ?
        teng->import_func_types[target] :
        teng->funcs[target - teng->import_func_count].type_index;
    if (!func_type_is_subtype(teng, actual_type_index, eng, instr->u32_imm)) {
        if (ctx->error) {
            ctx->error->status = EXEC_ERROR_TRAP;
            snprintf(ctx->error->message, sizeof(ctx->error->message),
                     "indirect call type mismatch: function %u pc %u, "
                     "table element %u targets function %u type %u, "
                     "expected type %u",
                     ctx->function_index, *ctx->pc, (unsigned)element, target,
                     actual_type_index, instr->u32_imm);
        }
        return EXEC_ERROR_TRAP;
    }
    exec_func_type *expected = &eng->types[instr->u32_imm];
    if (instr->opcode == 0x13) {
        /* return_call_indirect: tail call — restart without recursion */
        for (int i = expected->param_count; i-- > 0;)
            if (!stack_pop(ctx->operand_stack, &ctx->tail_args[i]))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "call_indirect arguments missing");
        if (teng != eng)
            return exec_invoke_managed(
                teng, target, ctx->tail_args, expected->param_count,
                ctx->results, ctx->result_count, ctx->error);
        ctx->engine = teng;
        ctx->function_index = target;
        *ctx->tail_arg_count = expected->param_count;
        return WASM_DISPATCH_TAIL_CALL;
    }
    wasm_value call_args[EXEC_MAX_CALL_ARGS], call_results[WAST_MAX_RESULTS]; int call_result_count = 0;
    for (int i = expected->param_count; i-- > 0;)
        if (!stack_pop(ctx->operand_stack, &call_args[i]))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "call_indirect arguments missing");
    exec_status status = exec_invoke_managed(
        teng, target, call_args, expected->param_count,
        call_results, &call_result_count, ctx->error);
    if (status == EXEC_YIELD) {
        for (int i = 0; i < expected->param_count; i++)
            stack_push(ctx->operand_stack, call_args[i]);
        stack_push(ctx->operand_stack, table_operand);
        save_yield_frame(ctx);
        return EXEC_YIELD;
    }
    return handle_call_result(ctx, status, call_results, call_result_count);
}

int execute_op_call_ref(waste_exec_context *ctx, const exec_instr *instr) {
    waste_exec_engine *eng = ctx->engine;
    if (instr->u32_imm >= eng->type_count)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                         "call_ref type out of range");
    exec_func_type *callee_type = &eng->types[instr->u32_imm];
    wasm_value reference;
    if (!stack_pop(ctx->operand_stack, &reference))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                         "call_ref target missing");
    if (reference.ref == UINT32_MAX)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                         "null function reference");
    uint32_t target = reference.ref;
    if (target >= eng->import_func_count + eng->func_count)
        return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                         "call_ref target out of range");
    uint32_t actual_type_index = target < eng->import_func_count ?
        eng->import_func_types[target] :
        eng->funcs[target - eng->import_func_count].type_index;
    if (!func_type_is_subtype(eng, actual_type_index,
                              eng, instr->u32_imm))
        return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                         "call_ref type mismatch");
    if (instr->opcode == 0x15) {
        /* return_call_ref: tail call — restart without recursion */
        for (int i = callee_type->param_count; i-- > 0;)
            if (!stack_pop(ctx->operand_stack, &ctx->tail_args[i]))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                                 "call_ref arguments missing");
        ctx->function_index = target;
        *ctx->tail_arg_count = callee_type->param_count;
        return WASM_DISPATCH_TAIL_CALL;
    }
    wasm_value call_args[EXEC_MAX_CALL_ARGS];
    wasm_value call_results[WAST_MAX_RESULTS];
    int call_result_count = 0;
    for (int i = callee_type->param_count; i-- > 0;)
        if (!stack_pop(ctx->operand_stack, &call_args[i]))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP,
                             "call_ref arguments missing");
    exec_status status = exec_invoke_managed(
        eng, target, call_args, callee_type->param_count,
        call_results, &call_result_count, ctx->error);
    if (status == EXEC_YIELD) {
        for (int i = 0; i < callee_type->param_count; i++)
            stack_push(ctx->operand_stack, call_args[i]);
        stack_push(ctx->operand_stack, reference);
        save_yield_frame(ctx);
        return EXEC_YIELD;
    }
    return handle_call_result(ctx, status, call_results, call_result_count);
}

int execute_op_return(waste_exec_context *ctx, const exec_instr *instr) {
    (void)ctx; (void)instr;
    return WASM_DISPATCH_RETURN;
}

int execute_op_fc(waste_exec_context *ctx, const exec_instr *instr) {
    uint32_t sub = instr->simd_op;
    if (sub <= 7) {
        exec_status st = exec_sat_trunc(sub, ctx->operand_stack, ctx->error);
        if (st != EXEC_OK) return st;
    } else if (sub >= 8 && sub <= 11) {
        exec_status status = exec_memory_bulk(ctx, instr, sub, ctx->error);
        if (status != EXEC_OK) return status;
    } else if (sub >= 12 && sub <= 17) {
        exec_status status = exec_table_bulk(ctx, instr, sub, ctx->error);
        if (status != EXEC_OK) return status;
    } else {
        /* Unsupported 0xFC operation. */
    }
    return EXEC_OK;
}

int execute_op_simd(waste_exec_context *ctx, const exec_instr *instr) {
    waste_exec_engine *eng = ctx->engine;
    uint32_t op = instr->simd_op;

    if (op == 12) {
        /* v128.const */
        wasm_value v;
        v.type = WASM_VALTYPE_V128;
        memcpy(v.v128.bytes, instr->v128_imm.bytes, 16);
        if (!stack_push(ctx->operand_stack, v))
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }

    if (op <= 0x0b || (op >= 0x54 && op <= 0x5d)) {
        wasm_value base, vector, out;
        size_t address; uint32_t width = 16;
        int lane_memory = op >= 0x54 && op <= 0x5b;
        int store = op == 0x0b || (op >= 0x58 && op <= 0x5b);
        if (instr->memory_index >= eng->memory_count)
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "memory index out of range");
        exec_memory *memory = eng->memories[instr->memory_index];
        if (lane_memory || store) {
            if (!simd_pop(ctx->operand_stack, &vector) ||
                !stack_pop(ctx->operand_stack, &base) ||
                base.type != (memory->is_64 ? WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "SIMD memory operands missing");
        } else if (!stack_pop(ctx->operand_stack, &base) ||
                   base.type != (memory->is_64 ? WASM_VALTYPE_I64 : WASM_VALTYPE_I32)) {
            return exec_fail(ctx->error, EXEC_ERROR_TRAP, "SIMD memory address missing");
        }
        if (lane_memory) width = UINT32_C(1) << ((op - 0x54) & 3u);
        else if (op == 0x01 || op == 0x02) width=8;
        else if (op == 0x03 || op == 0x04) width=8;
        else if (op == 0x05 || op == 0x06) width=8;
        else if (op >= 0x07 && op <= 0x0a) width=UINT32_C(1)<<(op-0x07);
        else if (op == 0x5c) width=4;
        else if (op == 0x5d) width=8;
        uint64_t base_address = memory->is_64 ? (uint64_t)base.i64 :
                                               (uint64_t)(uint32_t)base.i32;
        exec_status mem_status=memory_address(memory,base_address,
            instr->u64_imm,width,&address,ctx->error);
        if(mem_status!=EXEC_OK)return mem_status;
        if(store) {
            if(lane_memory) {
                uint32_t lane_width=UINT32_C(1)<<((op-0x58)&3u);
                memcpy(memory->data+address,
                       vector.v128.bytes+instr->lane_index*lane_width,
                       lane_width);
            } else memcpy(memory->data+address,vector.v128.bytes,16);
            return EXEC_OK;
        }
        if(lane_memory) {
            memcpy(vector.v128.bytes+instr->lane_index*width,
                   memory->data+address,width);
            if(!stack_push(ctx->operand_stack,vector))
                return exec_fail(ctx->error,EXEC_ERROR_TRAP,"stack overflow");
            return EXEC_OK;
        }
        out=simd_zero();
        if(op==0x00) memcpy(out.v128.bytes,memory->data+address,16);
        else if(op>=0x01&&op<=0x06) {
            uint32_t source_width=op<=0x02?1:op<=0x04?2:4;
            uint32_t dest_width=source_width*2;
            int signed_=(op&1)!=0;
            for(uint32_t i=0;i<16/dest_width;i++) {
                uint64_t raw=0;memcpy(&raw,memory->data+address+i*source_width,source_width);
                if(signed_&&source_width<8&&(raw&(UINT64_C(1)<<(source_width*8-1))))
                    raw|=UINT64_MAX<<(source_width*8);
                simd_set_lane(&out,i,dest_width,raw);
            }
        } else if(op>=0x07&&op<=0x0a) {
            uint64_t raw=0;memcpy(&raw,memory->data+address,width);
            for(uint32_t i=0;i<16/width;i++)simd_set_lane(&out,i,width,raw);
        } else memcpy(out.v128.bytes,memory->data+address,width);
        if(!stack_push(ctx->operand_stack,out))
            return exec_fail(ctx->error,EXEC_ERROR_TRAP,"stack overflow");
        return EXEC_OK;
    }

    int standard_simd = exec_standard_simd_integer(
        op, instr->lane_index, &instr->v128_imm, ctx->operand_stack, ctx->error);
    if (standard_simd < 0) return ctx->error->status;
    if (standard_simd > 0) return EXEC_OK;
    standard_simd = exec_standard_simd_float(op, ctx->operand_stack, ctx->error);
    if (standard_simd < 0) return ctx->error->status;
    if (standard_simd > 0) return EXEC_OK;

    /* SIMD ops that take operands from the stack */
    wasm_value a, b, c;
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b)); memset(&c, 0, sizeof(c));

    switch (op) {
        /* Unary ops */
        case 257: /* i32x4.relaxed_trunc_f32x4_s */
            if (!stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_i32x4_relaxed_trunc_f32x4_s(a)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 258: /* i32x4.relaxed_trunc_f32x4_u */
            if (!stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_i32x4_relaxed_trunc_f32x4_u(a)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 259: /* i32x4.relaxed_trunc_f64x2_s_zero */
            if (!stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_i32x4_relaxed_trunc_f64x2_s_zero(a)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 260: /* i32x4.relaxed_trunc_f64x2_u_zero */
            if (!stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_i32x4_relaxed_trunc_f64x2_u_zero(a)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;

        /* Binary ops */
        case 256: /* i8x16.relaxed_swizzle */
            if (!stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_i8x16_relaxed_swizzle(a, b)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 269: /* f32x4.relaxed_min */
            if (!stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_f32x4_relaxed_min(a, b)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 270: /* f32x4.relaxed_max */
            if (!stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_f32x4_relaxed_max(a, b)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 271: /* f64x2.relaxed_min */
            if (!stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_f64x2_relaxed_min(a, b)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 272: /* f64x2.relaxed_max */
            if (!stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_f64x2_relaxed_max(a, b)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 273: /* i16x8.relaxed_q15mulr_s */
            if (!stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_i16x8_relaxed_q15mulr_s(a, b)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 274: /* i16x8.relaxed_dot_i8x16_i7x16_s */
            if (!stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_i16x8_relaxed_dot_i8x16_i7x16_s(a, b)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;

        /* Ternary ops */
        case 261: /* f32x4.relaxed_madd */
            if (!stack_pop(ctx->operand_stack, &c) || !stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_f32x4_relaxed_madd(a, b, c)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 262: /* f32x4.relaxed_nmadd */
            if (!stack_pop(ctx->operand_stack, &c) || !stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_f32x4_relaxed_nmadd(a, b, c)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 263: /* f64x2.relaxed_madd */
            if (!stack_pop(ctx->operand_stack, &c) || !stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_f64x2_relaxed_madd(a, b, c)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 264: /* f64x2.relaxed_nmadd */
            if (!stack_pop(ctx->operand_stack, &c) || !stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_f64x2_relaxed_nmadd(a, b, c)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 265: /* i8x16.relaxed_laneselect */
            if (!stack_pop(ctx->operand_stack, &c) || !stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_i8x16_laneselect(a, b, c)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 266: /* i16x8.relaxed_laneselect */
        case 267: /* i32x4.relaxed_laneselect */
        case 268: /* i64x2.relaxed_laneselect */
            if (!stack_pop(ctx->operand_stack, &c) || !stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_bitselect(a, b, c)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 275: /* i32x4.relaxed_dot_i8x16_i7x16_add_s */
            if (!stack_pop(ctx->operand_stack, &c) || !stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_i32x4_relaxed_dot_i8x16_i7x16_add_s(a, b, c)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;

        /* Eq ops */
        case 35: /* i8x16.eq */
            if (!stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_i8x16_eq(a, b)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 37: /* i16x8.eq */
            if (!stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_i16x8_eq(a, b)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 39: /* i32x4.eq */
            if (!stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_i32x4_eq(a, b)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 214: /* i64x2.eq */
            if (!stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_i64x2_eq(a, b)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 65: /* f32x4.eq */
            if (!stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_f32x4_eq(a, b)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;
        case 71: /* f64x2.eq */
            if (!stack_pop(ctx->operand_stack, &b) || !stack_pop(ctx->operand_stack, &a))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack underflow");
            if (!stack_push(ctx->operand_stack, exec_f64x2_eq(a, b)))
                return exec_fail(ctx->error, EXEC_ERROR_TRAP, "stack overflow");
            break;

        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "unsupported SIMD op %u", op);
            return exec_fail(ctx->error, EXEC_ERROR_UNSUPPORTED, msg);
        }
    }
    return EXEC_OK;
}

/* ---- Main execution loop ---- */

static exec_status exec_invoke_frame(waste_exec_engine *eng,
                                     uint32_t func_idx,
                                     const wasm_value *args, int arg_count,
                                     wasm_value *results, int *result_count,
                                     exec_error *err, uint32_t depth) {
    waste_exec_context context;
    /* Tail-call arguments: these are updated in-place by return_call */
    wasm_value tail_args[EXEC_MAX_CALL_ARGS];
    int tail_arg_count;

tail_entry:
    if (!eng || func_idx >= eng->import_func_count + eng->func_count)
        return exec_fail(err, EXEC_ERROR_NOT_FOUND, "function index out of range");
    if (depth >= EXEC_MAX_CALL_DEPTH)
        return exec_fail(err, EXEC_ERROR_TRAP, "call stack exhausted");

    if (func_idx < eng->import_func_count) {
        exec_func_type *type=&eng->types[eng->import_func_types[func_idx]];
        if (arg_count != type->param_count) return exec_fail(err, EXEC_ERROR_TRAP, "import argument count mismatch");
        if (eng->import_controls[func_idx] == EXEC_HOST_CONTROL_EXIT) {
            if (arg_count < 1 || args[0].type != WASM_VALTYPE_I32)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "exit argument missing");
            if (err) {
                memset(err, 0, sizeof(*err));
                err->status = EXEC_ERROR_EXIT;
                err->exit_code = args[0].i32;
            }
            return EXEC_ERROR_EXIT;
        }
        if (eng->import_controls[func_idx] ==
            EXEC_HOST_CONTROL_SIGLONGJMP) {
            if (arg_count < 2 || args[0].type != WASM_VALTYPE_I32 ||
                args[1].type != WASM_VALTYPE_I32)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "siglongjmp arguments missing");
            if (err) {
                memset(err, 0, sizeof(*err));
                err->status = EXEC_ERROR_LONGJMP;
                err->jump_owner = eng;
                err->jump_environment = (uint32_t)args[0].i32;
                err->jump_value = args[1].i32 ? args[1].i32 : 1;
            }
            return EXEC_ERROR_LONGJMP;
        }
        int count=0;
        exec_status status=eng->import_funcs[func_idx](eng->import_host_data[func_idx],args,arg_count,results,&count,err);
        if (status != EXEC_OK) return status;
        if (count != type->result_count) return exec_fail(err, EXEC_ERROR_TRAP, "import result count mismatch");
        if (result_count) *result_count=count;
        return EXEC_OK;
    }

    if (!eng->operand_frames[depth]) {
        eng->operand_frames[depth] =
            (exec_stack *)calloc(1, sizeof(*eng->operand_frames[depth]));
        eng->control_frames[depth] = (exec_control *)calloc(
            EXEC_MAX_CONTROL, sizeof(*eng->control_frames[depth]));
        if (!eng->operand_frames[depth] || !eng->control_frames[depth])
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "runtime frame allocation failed");
    }
    exec_stack *runtime_stack = eng->operand_frames[depth];
    exec_control *controls = eng->control_frames[depth];
    context.engine = eng;
    context.operand_stack = runtime_stack;
    context.controls = controls;
    context.depth = depth;

    int resuming = eng->yield_frames[depth].valid;
    uint32_t start_pc = 0;
    int control_top = 0;

    if (resuming) {
        func_idx = eng->yield_frames[depth].func_idx;
        control_top = eng->yield_frames[depth].control_top;
        start_pc = eng->yield_frames[depth].pc;
        eng->yield_frames[depth].valid = 0;
    }

    uint32_t defined_index=func_idx-eng->import_func_count;

    if (!resuming) {
        /* Reusing an interpreter depth means the previous C/Wasm activation is
         * gone.  This also runs for return_call, where the current activation is
         * deliberately replaced. */
        invalidate_jump_depth(eng, depth);
    }
    uint64_t frame_generation = resuming ? eng->frame_generations[depth]
                                         : ++eng->frame_generations[depth];

    exec_func      *func = &eng->funcs[defined_index];
    exec_func_type *type = &eng->types[func->type_index];
    if (!func->validated)
        return exec_fail(err, EXEC_ERROR_TRAP,
                         "attempted to execute an unvalidated function");

    if (!resuming) {
        /* Validate arg count */
        if (arg_count != type->param_count)
            return exec_fail(err, EXEC_ERROR_TRAP, "argument count mismatch");

        /* Locals are engine-owned per invocation depth.  Their storage therefore
         * scales with the module declaration without consuming the browser/C
         * control stack (the guard-page conformance case has 1,056 i64 locals). */
        uint32_t local_count = (uint32_t)arg_count + func->local_count;
        if (local_count > EXEC_MAX_LOCALS)
            return exec_fail(err, EXEC_ERROR_TRAP, "too many runtime locals");
        uint32_t storage_count = local_count ? local_count : 1;
        if (eng->local_frame_capacities[depth] < storage_count) {
            wasm_value *next = (wasm_value *)realloc(
                eng->local_frames[depth],
                (size_t)storage_count * sizeof(*next));
            if (!next)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "runtime locals allocation failed");
            eng->local_frames[depth] = next;
            eng->local_frame_capacities[depth] = storage_count;
        }
        memset(eng->local_frames[depth], 0,
               (size_t)storage_count * sizeof(*eng->local_frames[depth]));
        for (int i = 0; i < arg_count; i++)
            eng->local_frames[depth][i] = args[i];
        for (uint32_t i = 0; i < func->local_count; i++)
            eng->local_frames[depth][arg_count + i].type = func->locals[i];

        runtime_stack->top = 0;
    }

    uint32_t local_count = (uint32_t)type->param_count + func->local_count;
    wasm_value *locals = eng->local_frames[depth];
    context.function = func;
    context.type = type;
    context.locals = locals;
    context.local_count = local_count;
    context.function_index = func_idx;
    context.frame_generation = frame_generation;
    context.control_top = &control_top;
    context.error = err;
    context.results = results;
    context.result_count = result_count;
    context.tail_args = tail_args;
    context.tail_arg_count = &tail_arg_count;

    for (uint32_t pc = start_pc; pc < func->code_size; pc++) {
        const exec_instr *instr = &func->code[pc];
        context.pc = &pc;

        const wasm_opcode_dispatch *dispatch = wasm_opcode_get_dispatch(instr);
        if (!dispatch || !dispatch->execute)
            return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported opcode");

        int action = dispatch->execute(&context, instr);
        if (action == EXEC_OK) continue;
        if (action == WASM_DISPATCH_RETURN) goto func_return;
        if (action == WASM_DISPATCH_TAIL_CALL) {
            eng = context.engine;
            func_idx = context.function_index;
            args = tail_args;
            arg_count = tail_arg_count;
            goto tail_entry;
        }
        return (exec_status)action;
    }
func_return:
    invalidate_jump_frame(eng, depth, frame_generation);
    /* Collect results */
    if (runtime_stack->top < type->result_count)
        return exec_fail(err, EXEC_ERROR_TRAP, "missing result");
    for (int i = type->result_count; i-- > 0;)
        if (results) stack_pop(runtime_stack, &results[i]); else { wasm_value ignored; stack_pop(runtime_stack, &ignored); }
    if (result_count) *result_count = type->result_count;

    return EXEC_OK;
}

exec_status exec_invoke(waste_exec_engine *eng,
                        uint32_t func_idx,
                        const wasm_value *args, int arg_count,
                        wasm_value *results, int *result_count,
                        exec_error *err) {
    exec_error local_error;
    if (!err) {
        memset(&local_error, 0, sizeof(local_error));
        local_error.exception_ref = UINT32_MAX;
        err = &local_error;
    }
    return exec_invoke_managed(eng, func_idx, args, arg_count, results,
                               result_count, err);
}
