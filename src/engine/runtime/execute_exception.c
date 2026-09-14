#include "runtime_internal.h"
#include "instantiate.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define EXEC_EXN_REF_BASE UINT32_C(0x40000000)

exec_status exec_raise(exec_error *error, const exec_tag *tag,
                              const wasm_value *payload, int payload_count,
                              waste_exec_engine *owner, uint32_t reference) {
    if (error) {
        error->status = EXEC_ERROR_EXCEPTION;
        snprintf(error->message, sizeof(error->message), "%s",
                 "uncaught exception");
        error->exception_tag = tag;
        error->exception_payload_count = payload_count;
        if (payload_count > 0)
            memcpy(error->exception_payload, payload,
                   (size_t)payload_count * sizeof(payload[0]));
        error->exception_owner = owner;
        error->exception_ref = reference;
    }
    return EXEC_ERROR_EXCEPTION;
}



exec_exception_object *exception_object(waste_exec_engine *eng,
                                                const wasm_value *value) {
    if (!eng || value->ref == UINT32_MAX ||
        (value->ref & UINT32_C(0xc0000000)) != EXEC_EXN_REF_BASE)
        return NULL;
    uint32_t index = value->ref & ~EXEC_EXN_REF_BASE;
    return index < eng->exception_object_count ?
        &eng->exception_objects[index] : NULL;
}

static int exception_reference(waste_exec_engine *eng, const exec_tag *tag,
                               const wasm_value *payload, int payload_count,
                               waste_exec_engine *source_owner,
                               uint32_t source_reference, wasm_value *out,
                               exec_error *err) {
    if (source_owner == eng && source_reference != UINT32_MAX) {
        wasm_value existing = default_value(WASM_VALTYPE_EXNREF_NONNULL);
        existing.ref = source_reference;
        if (exception_object(eng, &existing)) {
            *out = existing;
            return 1;
        }
    }
    if (payload_count < 0 || payload_count > WAST_MAX_PARAMS) {
        exec_fail(err, EXEC_ERROR_TRAP, "invalid exception payload");
        return 0;
    }
    if (eng->exception_object_count >= UINT32_C(0x3ffffffe)) {
        exec_fail(err, EXEC_ERROR_TRAP, "exception allocation failed");
        return 0;
    }
    if (eng->exception_object_count == eng->exception_object_capacity) {
        uint32_t capacity = eng->exception_object_capacity ?
            eng->exception_object_capacity * 2u : 8u;
        exec_exception_object *objects =
            (exec_exception_object *)wasm_instance_resize_objects(
                eng->exception_objects, eng->exception_object_capacity,
                capacity, sizeof(*objects), EXEC_ERROR_TRAP,
                "exception allocation failed", err);
        if (!objects) {
            return 0;
        }
        eng->exception_objects = objects;
        eng->exception_object_capacity = capacity;
    }
    uint32_t index = eng->exception_object_count++;
    exec_exception_object *object = &eng->exception_objects[index];
    object->tag = tag;
    object->payload_count = payload_count;
    if (payload_count)
        memcpy(object->payload, payload,
               (size_t)payload_count * sizeof(payload[0]));
    *out = default_value(WASM_VALTYPE_EXNREF_NONNULL);
    out->ref = EXEC_EXN_REF_BASE | index;
    return 1;
}

/* Route an exception to the innermost matching try_table in this frame.
 * Returns 0 when it must propagate, 1 after branching to a local handler,
 * 2 when the handler branches to the function label, and -1 on a runtime
 * failure while materializing an exception reference. */
int handle_exception(waste_exec_engine *eng, exec_func *func,
                            exec_stack *stack, exec_control *controls,
                            int *control_top, uint32_t *pc,
                            const exec_tag *tag, const wasm_value *payload,
                            int payload_count, waste_exec_engine *source_owner,
                            uint32_t source_reference, exec_error *err) {
    int try_index = -1;
    const exec_catch *selected = NULL;
    for (int i = *control_top - 1; i >= 0 && !selected; i--) {
        if (controls[i].kind != 0x1f) continue;
        const exec_instr *try_instr = &func->code[controls[i].start_pc - 1];
        for (uint32_t j = 0; j < try_instr->catch_count; j++) {
            const exec_catch *catch_ = &try_instr->catches[j];
            if (catch_->kind >= 2 ||
                (catch_->tag_index < eng->tag_count &&
                 eng->tags[catch_->tag_index] == tag)) {
                try_index = i;
                selected = catch_;
                break;
            }
        }
    }
    if (!selected) return 0;
    if (selected->depth > (uint32_t)try_index) {
        exec_fail(err, EXEC_ERROR_TRAP, "catch branch depth out of range");
        return -1;
    }

    stack->top = controls[try_index].stack_height;
    if (selected->kind < 2)
        for (int i = 0; i < payload_count; i++)
            if (!stack_push(stack, payload[i])) {
                exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                return -1;
            }
    if (selected->kind & 1u) {
        wasm_value reference;
        if (!exception_reference(eng, tag, payload, payload_count,
                                 source_owner, source_reference, &reference,
                                 err))
            return -1;
        if (!stack_push(stack, reference)) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            return -1;
        }
    }

    if (err) {
        err->status = EXEC_OK;
        err->message[0] = '\0';
        err->exception_tag = NULL;
        err->exception_payload_count = 0;
        err->exception_owner = NULL;
        err->exception_ref = UINT32_MAX;
    }
    if (selected->depth == (uint32_t)try_index) return 2;

    int target_index = try_index - 1 - (int)selected->depth;
    exec_control target = controls[target_index];
    wasm_value carried[WAST_MAX_RESULTS];
    if (target.branch_arity > stack->top - target.stack_height) {
        exec_fail(err, EXEC_ERROR_TRAP, "catch branch values missing");
        return -1;
    }
    for (int i = target.branch_arity; i-- > 0;)
        stack_pop(stack, &carried[i]);
    stack->top = target.stack_height;
    for (int i = 0; i < target.branch_arity; i++)
        if (!stack_push(stack, carried[i])) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            return -1;
        }
    if (target.kind == 0x03) {
        *control_top = target_index + 1;
        *pc = target.start_pc - 1;
    } else {
        *control_top = target_index;
        *pc = target.end_pc;
    }
    return 1;
}
