#include "../runtime_internal.h"
#include "validate.h"

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

static exec_status save_jump_frame(waste_exec_engine *eng,
                                   uint32_t environment, uint32_t depth,
                                   uint64_t frame_generation,
                                   uint32_t func_idx, uint32_t pc,
                                   const exec_stack *stack,
                                   const exec_control *controls,
                                   int control_top,
                                   const wasm_value *locals,
                                   uint32_t local_count,
                                   exec_error *err) {
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
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "setjmp snapshot allocation failed");
            memset(next + eng->jump_snapshot_capacity, 0,
                   (size_t)(capacity - eng->jump_snapshot_capacity) *
                   sizeof(*next));
            eng->jump_snapshots = next;
            eng->jump_snapshot_capacity = capacity;
        }
        snapshot = &eng->jump_snapshots[eng->jump_snapshot_count++];
    }
    if (local_count) {
        wasm_value *next = (wasm_value *)realloc(
            snapshot->locals, (size_t)local_count * sizeof(*next));
        if (!next)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "setjmp locals allocation failed");
        snapshot->locals = next;
        memcpy(snapshot->locals, locals,
               (size_t)local_count * sizeof(*locals));
    }
    snapshot->valid = 1;
    snapshot->environment = environment;
    snapshot->depth = depth;
    snapshot->frame_generation = frame_generation;
    snapshot->func_idx = func_idx;
    snapshot->pc = pc;
    snapshot->stack = *stack;
    snapshot->control_top = control_top;
    if (control_top)
        memcpy(snapshot->controls, controls,
               (size_t)control_top * sizeof(*controls));
    snapshot->local_count = local_count;
    return EXEC_OK;
}

static int restore_jump_frame(waste_exec_engine *eng, exec_error *err,
                              uint32_t depth, uint64_t frame_generation,
                              uint32_t func_idx, uint32_t *pc,
                              exec_stack *stack, exec_control *controls,
                              int *control_top, wasm_value *locals,
                              uint32_t local_count) {
    if (!err || err->status != EXEC_ERROR_LONGJMP ||
        err->jump_owner != eng)
        return 0;
    for (uint32_t i = 0; i < eng->jump_snapshot_count; i++) {
        exec_jump_snapshot *snapshot = &eng->jump_snapshots[i];
        if (!snapshot->valid ||
            snapshot->environment != err->jump_environment ||
            snapshot->depth != depth ||
            snapshot->frame_generation != frame_generation ||
            snapshot->func_idx != func_idx)
            continue;
        if (snapshot->local_count != local_count) {
            exec_fail(err, EXEC_ERROR_TRAP, "setjmp frame shape changed");
            return -1;
        }
        *pc = snapshot->pc;
        *stack = snapshot->stack;
        *control_top = snapshot->control_top;
        if (*control_top)
            memcpy(controls, snapshot->controls,
                   (size_t)*control_top * sizeof(*controls));
        if (local_count)
            memcpy(locals, snapshot->locals,
                   (size_t)local_count * sizeof(*locals));
        int32_t value = err->jump_value ? err->jump_value : 1;
        memset(err, 0, sizeof(*err));
        if (!stack_push(stack, i32_value((uint32_t)value))) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow after longjmp");
            return -1;
        }
        return 1;
    }
    return 0;
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

static exec_status exec_invoke_frame(waste_exec_engine *eng,
                                     uint32_t func_idx,
                                     const wasm_value *args, int arg_count,
                                     wasm_value *results, int *result_count,
                                     exec_error *err, uint32_t depth) {
    waste_exec_context context;
    /* Tail-call arguments: these are updated in-place by return_call */
    wasm_value tail_args[WAST_MAX_ARGS];
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
#define stack (*context.operand_stack)

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

        stack.top = 0;
    }

    uint32_t local_count = (uint32_t)type->param_count + func->local_count;
    wasm_value *locals = eng->local_frames[depth];
    context.function = func;
    context.type = type;
    context.locals = locals;
    context.local_count = local_count;
    context.function_index = func_idx;
    context.frame_generation = frame_generation;

    for (uint32_t pc = start_pc; pc < func->code_size; pc++) {
        const exec_instr *instr = &func->code[pc];

        if (instr->opcode == 0x0B) {
            if (control_top > 0) {
                exec_control target = controls[--control_top];
                wasm_value values[WAST_MAX_RESULTS];
                if (target.end_arity > stack.top - target.stack_height)
                    return exec_fail(err, EXEC_ERROR_TRAP, "block results missing");
                for (int i = target.end_arity; i-- > 0;)
                    stack_pop(&stack, &values[i]);
                stack.top = target.stack_height;
                for (int i = 0; i < target.end_arity; i++)
                    if (!stack_push(&stack, values[i])) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            }
            else break;
            continue;
        }

        if (instr->opcode == 0x08) {
            if (instr->u32_imm >= eng->tag_count)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "throw tag index out of range");
            uint32_t tag_type_index = eng->tag_types[instr->u32_imm];
            if (tag_type_index >= eng->type_count)
                return exec_fail(err, EXEC_ERROR_TRAP, "invalid tag type");
            exec_func_type *tag_type = &eng->types[tag_type_index];
            wasm_value payload[WAST_MAX_PARAMS];
            if (tag_type->param_count > stack.top)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "throw payload missing");
            for (int i = tag_type->param_count; i-- > 0;)
                stack_pop(&stack, &payload[i]);
            exec_raise(err, eng->tags[instr->u32_imm], payload,
                       tag_type->param_count, NULL, UINT32_MAX);
            int handled = handle_exception(
                eng, func, &stack, controls, &control_top, &pc,
                eng->tags[instr->u32_imm], payload, tag_type->param_count,
                NULL, UINT32_MAX, err);
            if (handled < 0) return err ? err->status : EXEC_ERROR_TRAP;
            if (handled == 0) return EXEC_ERROR_EXCEPTION;
            if (handled == 2) goto func_return;
            continue;
        }

        if (instr->opcode == 0x0a) {
            wasm_value reference;
            if (!stack_pop(&stack, &reference) ||
                !global_type_is_compat(eng, reference.type, eng,
                                       WASM_VALTYPE_EXNREF, 0))
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "throw_ref operand missing");
            if (reference.ref == UINT32_MAX)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "null exception reference");
            exec_exception_object *object = exception_object(eng, &reference);
            if (!object)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "invalid exception reference");
            exec_raise(err, object->tag, object->payload,
                       object->payload_count, eng, reference.ref);
            int handled = handle_exception(
                eng, func, &stack, controls, &control_top, &pc,
                object->tag, object->payload, object->payload_count,
                eng, reference.ref, err);
            if (handled < 0) return err ? err->status : EXEC_ERROR_TRAP;
            if (handled == 0) return EXEC_ERROR_EXCEPTION;
            if (handled == 2) goto func_return;
            continue;
        }

        if (instr->opcode == 0x00)
        {
            if (err) {
                err->status = EXEC_ERROR_TRAP;
                snprintf(err->message, sizeof(err->message),
                         "unreachable in function %u at pc %u", func_idx, pc);
            }
            return EXEC_ERROR_TRAP;
        }
        if (instr->opcode == 0x01) continue;

        if (instr->opcode == 0x02 || instr->opcode == 0x03 ||
            instr->opcode == 0x04 || instr->opcode == 0x1f) {
            int condition = 1;
            if (instr->opcode == 0x04) {
                wasm_value value;
                if (!stack_pop(&stack, &value))
                    return exec_fail(err, EXEC_ERROR_TRAP, "if condition missing");
                condition = value.i32 != 0;
            }
            if (control_top >= EXEC_MAX_CONTROL)
                return exec_fail(err, EXEC_ERROR_TRAP, "control stack overflow");
            int parameter_count = instr->v128_imm.bytes[0];
            int result_count_for_block = instr->v128_imm.bytes[1];
            if (parameter_count > stack.top)
                return exec_fail(err, EXEC_ERROR_TRAP, "block parameters missing");
            controls[control_top++] = (exec_control){
                instr->opcode, pc + 1, instr->resolved_target,
                stack.top - parameter_count,
                instr->opcode == 0x03 ? parameter_count : result_count_for_block,
                result_count_for_block
            };
            if (!condition) pc = instr->u32_imm - 1;
            continue;
        }

        if (instr->opcode == 0x05) {
            pc = instr->u32_imm - 1;
            continue;
        }

        if (instr->opcode == 0x0c || instr->opcode == 0x0d ||
            instr->opcode == 0x0e || instr->opcode == 0xd5 ||
            instr->opcode == 0xd6 ||
            (instr->opcode == 0xfb &&
             (instr->simd_op == 0x18 || instr->simd_op == 0x19))) {
            uint32_t depth = instr->resolved_target;
            if (instr->opcode == 0x0d) {
                wasm_value condition;
                if (!stack_pop(&stack, &condition))
                    return exec_fail(err, EXEC_ERROR_TRAP, "br_if condition missing");
                if (!condition.i32) continue;
            } else if (instr->opcode == 0x0e) {
                wasm_value index;
                if (!stack_pop(&stack, &index))
                    return exec_fail(err, EXEC_ERROR_TRAP, "br_table index missing");
                uint32_t selected = (uint32_t)index.i32;
                if (selected > instr->u32_imm) selected = instr->u32_imm;
                uint32_t *depths; memcpy(&depths, instr->v128_imm.bytes, sizeof(depths));
                depth = depths[selected];
            } else if (instr->opcode == 0xd5 || instr->opcode == 0xd6) {
                wasm_value reference;
                if (!stack_pop(&stack, &reference) ||
                    !is_reference_type(reference.type))
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "reference branch operand missing");
                int nonnull = reference.ref != UINT32_MAX;
                reference.type = nonnullable_reference_type(reference.type);
                if (instr->opcode == 0xd5 && !nonnull) {
                    /* null: take the branch without carrying the reference */
                } else if (instr->opcode == 0xd5) {
                    if (!stack_push(&stack, reference))
                        return exec_fail(err, EXEC_ERROR_TRAP,
                                         "stack overflow");
                    continue;
                } else if (!nonnull) {
                    /* br_on_non_null consumes the null reference on its
                     * fall-through path. */
                    continue;
                } else if (!stack_push(&stack, reference)) {
                    return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                }
            } else if (instr->opcode == 0xfb) {
                wasm_value reference;
                int32_t target_heap = (int32_t)instr->lane_index;
                if (!stack_pop(&stack, &reference) ||
                    !is_reference_type(reference.type))
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "br_on_cast reference missing");
                int matches = reference_matches_heap(
                    eng, &reference, target_heap,
                    (instr->alignment & 2u) != 0);
                int take = instr->simd_op == 0x18 ? matches : !matches;
                if (!stack_push(&stack, reference))
                    return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                if (!take) continue;
            }
            if (depth > (uint32_t)control_top)
                return exec_fail(err, EXEC_ERROR_TRAP, "branch depth out of range");
            if (depth == (uint32_t)control_top) {
                /* Branch to implicit function body block — act as return */
                wasm_value carried[WAST_MAX_RESULTS];
                int arity = type->result_count;
                if (arity > stack.top)
                    return exec_fail(err, EXEC_ERROR_TRAP, "branch values missing");
                for (int i = arity; i-- > 0;) stack_pop(&stack, &carried[i]);
                stack.top = 0;
                for (int i = 0; i < arity; i++)
                    if (!stack_push(&stack, carried[i])) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                goto func_return;
            }
            {
            int target_index = control_top - 1 - (int)depth;
            exec_control target = controls[target_index];
            wasm_value carried[WAST_MAX_RESULTS];
            if (target.branch_arity > stack.top - target.stack_height)
                return exec_fail(err, EXEC_ERROR_TRAP, "branch values missing");
            for (int i = target.branch_arity; i-- > 0;) stack_pop(&stack, &carried[i]);
            stack.top = target.stack_height;
            for (int i = 0; i < target.branch_arity; i++)
                if (!stack_push(&stack, carried[i])) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            if (target.kind == 0x03) {
                control_top = target_index + 1;
                pc = target.start_pc - 1;
            } else {
                control_top = target_index;
                pc = target.end_pc;
            }
            }
            continue;
        }

        if (instr->opcode == 0x20) {
            /* local.get */
            if (!stack_push(&stack, locals[instr->u32_imm]))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0x21 || instr->opcode == 0x22) {
            wasm_value value;
            if (!stack_pop(&stack, &value))
                return exec_fail(err, EXEC_ERROR_TRAP, "local value missing");
            locals[instr->u32_imm] = value;
            if (instr->opcode == 0x22 && !stack_push(&stack, value))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0x23 || instr->opcode == 0x24) {
            if (instr->opcode == 0x23) {
                if (!stack_push(&stack, eng->globals[instr->u32_imm]->value)) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            } else {
                wasm_value value;
                exec_global *global = eng->globals[instr->u32_imm];
                if (!stack_pop(&stack, &value))
                    return exec_fail(err, EXEC_ERROR_TRAP, "global value mismatch");
                value.type = global->value.type;
                global->value = value;
            }
            continue;
        }

        if (instr->opcode == 0x25 || instr->opcode == 0x26) {
            exec_status status = exec_table_get_set(&context, instr, err);
            if (status != EXEC_OK) return status;
            continue;
        }

        if (instr->opcode >= 0x28 && instr->opcode <= 0x40) {
            exec_status status = exec_memory_instruction(&context, instr, err);
            if (status != EXEC_OK) return status;
            continue;
        }
        if (instr->opcode == 0xfb) {
            int handled = exec_gc_instruction(eng, instr, &stack, err);
            if (handled < 0) return err ? err->status : EXEC_ERROR_TRAP;
            if (handled > 0) continue;
        }
        if (instr->opcode == 0xd0 || instr->opcode == 0xd2) {
            wasm_value value; memset(&value,0,sizeof(value));
            if (instr->opcode == 0xd0) {
                if (!nullable_reference_for_heap(
                        eng, (int32_t)instr->u32_imm, &value.type))
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "invalid ref.null heap type");
            } else if (instr->opcode == 0xd2) {
                uint32_t function_type =
                    instr->u32_imm < eng->import_func_count ?
                    eng->import_func_types[instr->u32_imm] :
                    eng->funcs[instr->u32_imm -
                               eng->import_func_count].type_index;
                value.type = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE +
                                            function_type);
            }
            value.ref = instr->opcode == 0xd0 ? UINT32_MAX : instr->u32_imm;
            set_reference_dynamic_type(&value, value.type);
            if (instr->opcode == 0xd2 && instr->u32_imm >= eng->import_func_count + eng->func_count)
                return exec_fail(err, EXEC_ERROR_TRAP, "ref.func index out of range");
            if (!stack_push(&stack,value)) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }
        if (instr->opcode == 0xd1) {
            wasm_value value;
            if (!stack_pop(&stack,&value) ||
                !is_reference_type(value.type))
                return exec_fail(err, EXEC_ERROR_TRAP, "ref.is_null operand missing");
            if (!stack_push(&stack,i32_value(value.ref == UINT32_MAX))) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0xd3) {
            wasm_value right, left;
            if (!stack_pop(&stack, &right) || !is_reference_type(right.type) ||
                !stack_pop(&stack, &left) || !is_reference_type(left.type))
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "ref.eq operands missing");
            if (!stack_push(&stack, i32_value(left.ref == right.ref)))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0xd4) {
            wasm_value value;
            if (!stack_pop(&stack, &value) || !is_reference_type(value.type))
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "ref.as_non_null operand missing");
            if (value.ref == UINT32_MAX)
                return exec_fail(err, EXEC_ERROR_TRAP, "null reference");
            value.type = nonnullable_reference_type(value.type);
            if (!stack_push(&stack, value))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0x1a) {
            wasm_value ignored;
            if (!stack_pop(&stack, &ignored))
                return exec_fail(err, EXEC_ERROR_TRAP, "drop operand missing");
            continue;
        }

        if (instr->opcode == 0x1b) {
            wasm_value condition, second, first;
            if (!stack_pop(&stack, &condition) || !stack_pop(&stack, &second) ||
                !stack_pop(&stack, &first))
                return exec_fail(err, EXEC_ERROR_TRAP, "select operands missing");
            if (!stack_push(&stack, condition.i32 ? first : second))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0x10 || instr->opcode == 0x12) {
            exec_func_type *callee_type =
                &eng->types[instr->resolved_type_index];
            if (instr->opcode == 0x12) {
                /* return_call: tail call — restart function without recursion */
                for (int i = callee_type->param_count; i-- > 0;)
                    if (!stack_pop(&stack, &tail_args[i]))
                        return exec_fail(err, EXEC_ERROR_TRAP, "call arguments missing");
                tail_arg_count = callee_type->param_count;
                func_idx = instr->u32_imm;
                args = tail_args;
                arg_count = tail_arg_count;
                goto tail_entry;
            }
            wasm_value call_args[WAST_MAX_ARGS], call_results[WAST_MAX_RESULTS]; int call_result_count = 0;
            for (int i = callee_type->param_count; i-- > 0;) {
                if (!stack_pop(&stack, &call_args[i]))
                    return exec_fail(err, EXEC_ERROR_TRAP, "call arguments missing");
            }
            if (instr->u32_imm < eng->import_func_count &&
                eng->import_controls[instr->u32_imm] ==
                    EXEC_HOST_CONTROL_SIGSETJMP) {
                if (callee_type->param_count < 1 ||
                    call_args[0].type != WASM_VALTYPE_I32 ||
                    callee_type->result_count != 1 ||
                    callee_type->results[0] != WASM_VALTYPE_I32)
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "unsupported sigsetjmp signature");
                exec_status saved = save_jump_frame(
                    eng, (uint32_t)call_args[0].i32, depth,
                    frame_generation, func_idx, pc, &stack, controls,
                    control_top, locals, local_count, err);
                if (saved != EXEC_OK) return saved;
                if (!stack_push(&stack, i32_value(0)))
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "stack overflow after setjmp");
                continue;
            }
            exec_status status = exec_invoke_managed(
                eng, instr->u32_imm, call_args, callee_type->param_count,
                call_results, &call_result_count, err);
            if (status == EXEC_ERROR_EXCEPTION) {
                int handled = handle_exception(
                    eng, func, &stack, controls, &control_top, &pc,
                    err->exception_tag, err->exception_payload,
                    err->exception_payload_count, err->exception_owner,
                    err->exception_ref, err);
                if (handled < 0) return err->status;
                if (handled == 0) return status;
                if (handled == 2) goto func_return;
                continue;
            }
            if (status == EXEC_ERROR_LONGJMP) {
                int restored = restore_jump_frame(
                    eng, err, depth, frame_generation, func_idx, &pc,
                    &stack, controls, &control_top, locals, local_count);
                if (restored < 0) return err->status;
                if (restored > 0) continue;
                invalidate_jump_frame(eng, depth, frame_generation);
                return status;
            }
            if (status == EXEC_YIELD) {
                for (int i = 0; i < callee_type->param_count; i++)
                    stack_push(&stack, call_args[i]);
                eng->yield_frames[depth].valid = 1;
                eng->yield_frames[depth].func_idx = func_idx;
                eng->yield_frames[depth].pc = pc;
                eng->yield_frames[depth].control_top = control_top;
                return EXEC_YIELD;
            }
            if (status != EXEC_OK) return status;
            for (int i = 0; i < call_result_count; i++)
                if (!stack_push(&stack, call_results[i]))
                    return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0x11 || instr->opcode == 0x13) {
            wasm_value table_operand;
            exec_table *table=eng->tables[instr->simd_op];
            uint64_t element;
            if (!stack_pop(&stack,&table_operand) ||
                !address_value(&table_operand,table->is_64,&element))
                return exec_fail(err,EXEC_ERROR_TRAP,"call_indirect table operand missing");
            if(element>=table->size)return exec_fail(err,EXEC_ERROR_TRAP,"undefined element");
            exec_table_element slot=table->elements[(size_t)element];
            if(!slot.owner)return exec_fail(err,EXEC_ERROR_TRAP,"uninitialized element");
            waste_exec_engine *teng=slot.owner;
            uint32_t target=slot.func_idx;
            if(target>=teng->import_func_count+teng->func_count)
                return exec_fail(err,EXEC_ERROR_TRAP,"call_indirect target out of range");
            uint32_t actual_type_index=target<teng->import_func_count?
                teng->import_func_types[target]:
                teng->funcs[target-teng->import_func_count].type_index;
            if(!func_type_is_subtype(teng,actual_type_index,eng,instr->u32_imm)) {
                if (err) {
                    err->status = EXEC_ERROR_TRAP;
                    snprintf(err->message, sizeof(err->message),
                             "indirect call type mismatch: function %u pc %u, "
                             "table element %u targets function %u type %u, "
                             "expected type %u",
                             func_idx, pc, (unsigned)element, target,
                             actual_type_index, instr->u32_imm);
                }
                return EXEC_ERROR_TRAP;
            }
            exec_func_type *expected=&eng->types[instr->u32_imm];
            if (instr->opcode == 0x13) {
                /* return_call_indirect: tail call — restart without recursion */
                for(int i=expected->param_count;i-->0;)
                    if(!stack_pop(&stack,&tail_args[i]))
                        return exec_fail(err,EXEC_ERROR_TRAP,"call_indirect arguments missing");
                if (teng != eng)
                    return exec_invoke_managed(
                        teng, target, tail_args, expected->param_count,
                        results, result_count, err);
                eng = teng;
                func_idx = target;
                args = tail_args;
                arg_count = expected->param_count;
                goto tail_entry;
            }
            wasm_value call_args[WAST_MAX_ARGS],call_results[WAST_MAX_RESULTS];int call_result_count=0;
            for(int i=expected->param_count;i-->0;)
                if(!stack_pop(&stack,&call_args[i]))
                    return exec_fail(err,EXEC_ERROR_TRAP,"call_indirect arguments missing");
            exec_status status=exec_invoke_managed(
                teng, target, call_args, expected->param_count,
                call_results, &call_result_count, err);
            if(status==EXEC_ERROR_EXCEPTION){
                int handled=handle_exception(eng,func,&stack,controls,
                    &control_top,&pc,err->exception_tag,
                    err->exception_payload,err->exception_payload_count,
                    err->exception_owner,err->exception_ref,err);
                if(handled<0)return err->status;
                if(handled==0)return status;
                if(handled==2)goto func_return;
                continue;
            }
            if(status==EXEC_ERROR_LONGJMP){
                int restored=restore_jump_frame(
                    eng,err,depth,frame_generation,func_idx,&pc,&stack,
                    controls,&control_top,locals,local_count);
                if(restored<0)return err->status;
                if(restored>0)continue;
                invalidate_jump_frame(eng,depth,frame_generation);
                return status;
            }
            if (status == EXEC_YIELD) {
                for (int i = 0; i < expected->param_count; i++)
                    stack_push(&stack, call_args[i]);
                stack_push(&stack, table_operand);
                eng->yield_frames[depth].valid = 1;
                eng->yield_frames[depth].func_idx = func_idx;
                eng->yield_frames[depth].pc = pc;
                eng->yield_frames[depth].control_top = control_top;
                return EXEC_YIELD;
            }
            if(status!=EXEC_OK)return status;
            for(int i=0;i<call_result_count;i++)if(!stack_push(&stack,call_results[i]))
                return exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");
            continue;
        }

        if (instr->opcode == 0x14 || instr->opcode == 0x15) {
            if (instr->u32_imm >= eng->type_count)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "call_ref type out of range");
            exec_func_type *callee_type = &eng->types[instr->u32_imm];
            wasm_value reference;
            if (!stack_pop(&stack, &reference))
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "call_ref target missing");
            if (reference.ref == UINT32_MAX)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "null function reference");
            uint32_t target = reference.ref;
            if (target >= eng->import_func_count + eng->func_count)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "call_ref target out of range");
            uint32_t actual_type_index = target < eng->import_func_count ?
                eng->import_func_types[target] :
                eng->funcs[target - eng->import_func_count].type_index;
            if (!func_type_is_subtype(eng, actual_type_index,
                                      eng, instr->u32_imm))
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "call_ref type mismatch");
            if (instr->opcode == 0x15) {
                /* return_call_ref: tail call — restart without recursion */
                for (int i = callee_type->param_count; i-- > 0;)
                    if (!stack_pop(&stack, &tail_args[i]))
                        return exec_fail(err, EXEC_ERROR_TRAP,
                                         "call_ref arguments missing");
                func_idx = target;
                args = tail_args;
                arg_count = callee_type->param_count;
                goto tail_entry;
            }
            wasm_value call_args[WAST_MAX_ARGS];
            wasm_value call_results[WAST_MAX_RESULTS];
            int call_result_count = 0;
            for (int i = callee_type->param_count; i-- > 0;)
                if (!stack_pop(&stack, &call_args[i]))
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "call_ref arguments missing");
            exec_status status = exec_invoke_managed(
                eng, target, call_args, callee_type->param_count,
                call_results, &call_result_count, err);
            if (status == EXEC_ERROR_EXCEPTION) {
                int handled = handle_exception(
                    eng, func, &stack, controls, &control_top, &pc,
                    err->exception_tag, err->exception_payload,
                    err->exception_payload_count, err->exception_owner,
                    err->exception_ref, err);
                if (handled < 0) return err->status;
                if (handled == 0) return status;
                if (handled == 2) goto func_return;
                continue;
            }
            if (status == EXEC_ERROR_LONGJMP) {
                int restored = restore_jump_frame(
                    eng, err, depth, frame_generation, func_idx, &pc,
                    &stack, controls, &control_top, locals, local_count);
                if (restored < 0) return err->status;
                if (restored > 0) continue;
                invalidate_jump_frame(eng, depth, frame_generation);
                return status;
            }
            if (status == EXEC_YIELD) {
                for (int i = 0; i < callee_type->param_count; i++)
                    stack_push(&stack, call_args[i]);
                stack_push(&stack, reference);
                eng->yield_frames[depth].valid = 1;
                eng->yield_frames[depth].func_idx = func_idx;
                eng->yield_frames[depth].pc = pc;
                eng->yield_frames[depth].control_top = control_top;
                return EXEC_YIELD;
            }
            if (status != EXEC_OK) return status;
            for (int i = 0; i < call_result_count; i++)
                if (!stack_push(&stack, call_results[i]))
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "stack overflow");
            continue;
        }

        if (instr->opcode == 0x0f) break;

        if (instr->opcode == 0x41) {
            if (!stack_push(&stack, i32_value(instr->u32_imm)))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }
        if (instr->opcode == 0x42 || instr->opcode == 0x43 || instr->opcode == 0x44) {
            wasm_value value; memset(&value, 0, sizeof(value));
            if (instr->opcode == 0x42) { value.type=WASM_VALTYPE_I64; memcpy(&value.i64,instr->v128_imm.bytes,8); }
            else if (instr->opcode == 0x43) { value.type=WASM_VALTYPE_F32; memcpy(&value.f32,instr->v128_imm.bytes,4); }
            else { value.type=WASM_VALTYPE_F64; memcpy(&value.f64,instr->v128_imm.bytes,8); }
            if (!stack_push(&stack, value)) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        /* i32 ops: comparisons 0x45-0x4f, unary/binary 0x67-0x78, extend 0xc0-0xc1 */
        if ((instr->opcode >= 0x45 && instr->opcode <= 0x4f) ||
            (instr->opcode >= 0x67 && instr->opcode <= 0x78) ||
            instr->opcode == 0xc0 || instr->opcode == 0xc1) {
            exec_status numeric = exec_i32_numeric(instr->opcode, &stack, err);
            if (numeric != EXEC_OK) return numeric;
            continue;
        }

        /* i64 ops: comparisons 0x50-0x5a, unary/binary 0x79-0x8a, extend 0xc2-0xc4 */
        if ((instr->opcode >= 0x50 && instr->opcode <= 0x5a) ||
            (instr->opcode >= 0x79 && instr->opcode <= 0x8a) ||
            (instr->opcode >= 0xc2 && instr->opcode <= 0xc4)) {
            exec_status numeric = exec_i64_numeric(instr->opcode, &stack, err);
            if (numeric != EXEC_OK) return numeric;
            continue;
        }

        /* f32 ops: comparisons 0x5b-0x60, unary/binary 0x8b-0x98 */
        if ((instr->opcode >= 0x5b && instr->opcode <= 0x60) ||
            (instr->opcode >= 0x8b && instr->opcode <= 0x98)) {
            exec_status numeric = exec_f32_numeric(instr->opcode, &stack, err);
            if (numeric != EXEC_OK) return numeric;
            continue;
        }

        /* f64 ops: comparisons 0x61-0x66, unary/binary 0x99-0xa6 */
        if ((instr->opcode >= 0x61 && instr->opcode <= 0x66) ||
            (instr->opcode >= 0x99 && instr->opcode <= 0xa6)) {
            exec_status numeric = exec_f64_numeric(instr->opcode, &stack, err);
            if (numeric != EXEC_OK) return numeric;
            continue;
        }

        /* conversion ops 0xa7-0xbf */
        if (instr->opcode >= 0xa7 && instr->opcode <= 0xbf) {
            exec_status st = exec_conversion(instr->opcode, &stack, err);
            if (st != EXEC_OK) return st;
            continue;
        }

        /* 0xFC: saturating trunc (0x00-0x07) and bulk memory/table ops */
        if (instr->opcode == 0xFC) {
            uint32_t sub = instr->simd_op;
            if (sub <= 7) {
                exec_status st = exec_sat_trunc(sub, &stack, err);
                if (st != EXEC_OK) return st;
            } else if (sub >= 8 && sub <= 11) {
                exec_status status = exec_memory_bulk(&context, instr, sub, err);
                if (status != EXEC_OK) return status;
            } else if (sub >= 12 && sub <= 17) {
                exec_status status = exec_table_bulk(&context, instr, sub, err);
                if (status != EXEC_OK) return status;
            } else {
                /* Unsupported 0xFC operation. */
            }
            continue;
        }

        if (instr->opcode == 0xFD) {
            uint32_t op = instr->simd_op;

            if (op == 12) {
                /* v128.const */
                wasm_value v;
                v.type = WASM_VALTYPE_V128;
                memcpy(v.v128.bytes, instr->v128_imm.bytes, 16);
                if (!stack_push(&stack, v))
                    return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                continue;
            }

            if (op <= 0x0b || (op >= 0x54 && op <= 0x5d)) {
                wasm_value base, vector, out;
                size_t address; uint32_t width = 16;
                int lane_memory = op >= 0x54 && op <= 0x5b;
                int store = op == 0x0b || (op >= 0x58 && op <= 0x5b);
                if (instr->memory_index >= eng->memory_count)
                    return exec_fail(err,EXEC_ERROR_TRAP,"memory index out of range");
                exec_memory *memory = eng->memories[instr->memory_index];
                if (lane_memory || store) {
                    if (!simd_pop(&stack,&vector) || !stack_pop(&stack,&base) ||
                        base.type != (memory->is_64 ? WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
                        return exec_fail(err,EXEC_ERROR_TRAP,"SIMD memory operands missing");
                } else if (!stack_pop(&stack,&base) ||
                           base.type != (memory->is_64 ? WASM_VALTYPE_I64 : WASM_VALTYPE_I32)) {
                    return exec_fail(err,EXEC_ERROR_TRAP,"SIMD memory address missing");
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
                    instr->u64_imm,width,&address,err);
                if(mem_status!=EXEC_OK)return mem_status;
                if(store) {
                    if(lane_memory) {
                        uint32_t lane_width=UINT32_C(1)<<((op-0x58)&3u);
                        memcpy(memory->data+address,
                               vector.v128.bytes+instr->lane_index*lane_width,
                               lane_width);
                    } else memcpy(memory->data+address,vector.v128.bytes,16);
                    continue;
                }
                if(lane_memory) {
                    memcpy(vector.v128.bytes+instr->lane_index*width,
                           memory->data+address,width);
                    if(!stack_push(&stack,vector))return exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");
                    continue;
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
                if(!stack_push(&stack,out))return exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");
                continue;
            }

            int standard_simd = exec_standard_simd_integer(
                op, instr->lane_index, &instr->v128_imm, &stack, err);
            if (standard_simd < 0) return err->status;
            if (standard_simd > 0) continue;
            standard_simd = exec_standard_simd_float(op, &stack, err);
            if (standard_simd < 0) return err->status;
            if (standard_simd > 0) continue;

            /* SIMD ops that take operands from the stack */
            wasm_value a, b, c;
            memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b)); memset(&c, 0, sizeof(c));

            switch (op) {
                /* Unary ops */
                case 257: /* i32x4.relaxed_trunc_f32x4_s */
                    if (!stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i32x4_relaxed_trunc_f32x4_s(a)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 258: /* i32x4.relaxed_trunc_f32x4_u */
                    if (!stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i32x4_relaxed_trunc_f32x4_u(a)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 259: /* i32x4.relaxed_trunc_f64x2_s_zero */
                    if (!stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i32x4_relaxed_trunc_f64x2_s_zero(a)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 260: /* i32x4.relaxed_trunc_f64x2_u_zero */
                    if (!stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i32x4_relaxed_trunc_f64x2_u_zero(a)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;

                /* Binary ops */
                case 256: /* i8x16.relaxed_swizzle */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i8x16_relaxed_swizzle(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 269: /* f32x4.relaxed_min */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f32x4_relaxed_min(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 270: /* f32x4.relaxed_max */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f32x4_relaxed_max(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 271: /* f64x2.relaxed_min */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f64x2_relaxed_min(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 272: /* f64x2.relaxed_max */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f64x2_relaxed_max(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 273: /* i16x8.relaxed_q15mulr_s */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i16x8_relaxed_q15mulr_s(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 274: /* i16x8.relaxed_dot_i8x16_i7x16_s */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i16x8_relaxed_dot_i8x16_i7x16_s(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;

                /* Ternary ops */
                case 261: /* f32x4.relaxed_madd */
                    if (!stack_pop(&stack, &c) || !stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f32x4_relaxed_madd(a, b, c)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 262: /* f32x4.relaxed_nmadd */
                    if (!stack_pop(&stack, &c) || !stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f32x4_relaxed_nmadd(a, b, c)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 263: /* f64x2.relaxed_madd */
                    if (!stack_pop(&stack, &c) || !stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f64x2_relaxed_madd(a, b, c)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 264: /* f64x2.relaxed_nmadd */
                    if (!stack_pop(&stack, &c) || !stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f64x2_relaxed_nmadd(a, b, c)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 265: /* i8x16.relaxed_laneselect */
                    if (!stack_pop(&stack, &c) || !stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i8x16_laneselect(a, b, c)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 266: /* i16x8.relaxed_laneselect */
                case 267: /* i32x4.relaxed_laneselect */
                case 268: /* i64x2.relaxed_laneselect */
                    if (!stack_pop(&stack, &c) || !stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_bitselect(a, b, c)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 275: /* i32x4.relaxed_dot_i8x16_i7x16_add_s */
                    if (!stack_pop(&stack, &c) || !stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i32x4_relaxed_dot_i8x16_i7x16_add_s(a, b, c)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;

                /* Eq ops */
                case 35: /* i8x16.eq */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i8x16_eq(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 37: /* i16x8.eq */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i16x8_eq(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 39: /* i32x4.eq */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i32x4_eq(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 214: /* i64x2.eq */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i64x2_eq(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 65: /* f32x4.eq */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f32x4_eq(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 71: /* f64x2.eq */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f64x2_eq(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;

                default: {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "unsupported SIMD op %u", op);
                    return exec_fail(err, EXEC_ERROR_UNSUPPORTED, msg);
                }
            }
            continue;
        }

        return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported opcode");
    }
func_return:
    invalidate_jump_frame(eng, depth, frame_generation);
    /* Collect results */
    if (stack.top < type->result_count)
        return exec_fail(err, EXEC_ERROR_TRAP, "missing result");
    for (int i = type->result_count; i-- > 0;)
        if (results) stack_pop(&stack, &results[i]); else { wasm_value ignored; stack_pop(&stack, &ignored); }
    if (result_count) *result_count = type->result_count;

    return EXEC_OK;
}
#undef stack

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
