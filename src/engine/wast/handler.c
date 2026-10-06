#include "wast/handler.h"
#include "wast/runner.h"
#include "runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HANDLER_COMMAND_MAX 1024u

void wast_process_handler_init(wast_process_handler *handler, native_store *store,
                               wast_handler_result result, void *data) {
    memset(handler, 0, sizeof(*handler));
    handler->store = store;
    handler->result = result;
    handler->result_data = data;
}

void wast_process_handler_reset(void *opaque) {
    wast_process_handler *handler = opaque;
    if (handler->stream_active) wast_stream_destroy(&handler->stream);
    memset(&handler->stream, 0, sizeof(handler->stream));
    handler->stream_active = handler->pending = handler->failed = 0;
    handler->stopped = handler->exit_code = 0;
    handler->assertion_engine = NULL;
    handler->wait_reason = EXEC_YIELD_NONE;
    handler->module_start = handler->store ? handler->store->module_count : 0;
    handler->orphan_start = handler->store ? handler->store->orphan_count : 0;
    handler->retained_start = handler->retained_count;
}

void wast_process_handler_destroy(wast_process_handler *handler) {
    wast_process_handler_reset(handler);
    for (unsigned i = 0; i < handler->retained_count; i++) {
        wast_script_free(handler->retained[i]);
        free(handler->retained[i]);
    }
    free(handler->retained);
    memset(handler, 0, sizeof(*handler));
}

static void handler_result(wast_process_handler *handler, int passed,
                            const char *name, const char *message) {
    if (!passed) handler->failed = 1;
    if (handler->result)
        handler->result(handler->result_data, passed, name, message);
}

static wast_script *handler_retain(wast_process_handler *handler, wast_script *parsed) {
    if (handler->retained_count >= HANDLER_COMMAND_MAX) return NULL;
    /* Streaming parses reserve large group arrays for ordinary full scripts. */
    if (parsed->group_count && parsed->group_capacity != parsed->group_count) {
        wast_group *groups = realloc(parsed->groups,
                                    (size_t)parsed->group_count * sizeof(*groups));
        if (!groups) return NULL;
        parsed->groups = groups;
        parsed->group_capacity = parsed->group_count;
    }
    wast_script **retained = realloc(handler->retained,
        (handler->retained_count + 1u) * sizeof(*retained));
    if (!retained) return NULL;
    handler->retained = retained;
    wast_script *owned = malloc(sizeof(*owned));
    if (!owned) return NULL;
    *owned = *parsed;
    memset(parsed, 0, sizeof(*parsed));
    handler->retained[handler->retained_count++] = owned;
    return owned;
}

static const wast_module *handler_definition(wast_process_handler *handler,
                                             const char *id) {
    for (unsigned i = handler->retained_count; i > 0; i--) {
        const wast_script *script = handler->retained[i - 1];
        for (int j = script->group_count; j > 0; j--) {
            const wast_module *module = &script->groups[j - 1].module;
            if (module->is_definition && !strcmp(module->id, id)) return module;
        }
    }
    return NULL;
}

static exec_status handler_invoke(void *opaque, waste_exec_engine *engine,
    uint32_t function, const wasm_value *args, int count, wasm_value *results,
    int *result_count, exec_error *error) {
    wast_process_handler *handler = opaque;
    /* Do not recursively enter the enclosing Bash process driver. This engine
     * retains its evaluator state, while the enclosing driver retains Bash. */
    exec_status status = exec_invoke(engine, function, args, count,
                                     results, result_count, error);
    if (status == EXEC_ERROR_EXIT) {
        handler->stopped = 1;
        handler->exit_code = error->exit_code;
    }
    if (status == EXEC_YIELD && error->yield_reason != EXEC_YIELD_READ &&
        error->yield_reason != EXEC_YIELD_SELECT) {
        handler->stopped = 1;
        handler->exit_code = 126;
        return exec_fail(error, EXEC_ERROR_UNSUPPORTED,
                         "WAST handler supports READ/SELECT, not nested process transitions");
    }
    return status;
}

static void handler_assertion(wast_process_handler *handler) {
    exec_error error = {0};
    exec_status status = handler->assertion_engine ? wast_run_assertion_with_invoke(
        handler->assertion_engine, &handler->assertion, &error,
        handler_invoke, handler) : exec_fail(&error, EXEC_ERROR_NOT_FOUND, "unknown module id");
    handler->pending = status == EXEC_YIELD;
    if (status == EXEC_ERROR_INTERRUPTED) handler->stopped = 1;
    handler->wait_reason = handler->pending ? error.yield_reason : EXEC_YIELD_NONE;
    if (!handler->pending)
        handler_result(handler, status == EXEC_OK, handler->assertion.func_name,
                        status == EXEC_OK ? NULL : error.message);
}

static void handler_module(wast_process_handler *handler, wast_group *group) {
    const wast_module *module = &group->module;
    if (module->is_definition) return;
    if (module->instance_of[0]) {
        module = handler_definition(handler, module->instance_of);
        if (!module) {
            handler_result(handler, 0, "(module)", "unknown module definition");
            return;
        }
    }
    if (group->has_module_assertion && group->has_validation_error) {
        int passed = group->module_assert_kind == WAST_ASSERT_INVALID ||
                     group->module_assert_kind == WAST_ASSERT_MALFORMED;
        handler_result(handler, passed, "(module)", passed ? NULL : group->validation_error);
        return;
    }
    wast_group encoded = *group;
    encoded.module = *module;
    size_t size = 0;
    char message[256] = {0};
    uint8_t *binary = encode_group_module(&encoded, &size, message);
    if (!binary) {
        int passed = group->has_module_assertion && group->module_assert_kind != WAST_ASSERT_TRAP;
        handler_result(handler, passed, "(module)", passed ? NULL : message);
        return;
    }
    waste_exec_engine *engine = NULL;
    exec_error error = {0};
    exec_status status = native_load_module(handler->store, module, binary, size, &engine, &error);
    free(binary);
    if (status == EXEC_ERROR_INTERRUPTED) {
        if (engine && !native_store_keep_orphan(handler->store, engine)) exec_free(engine);
        handler_result(handler, 0, "(module)", error.message);
        handler->stopped = 1;
        return;
    }
    if (status == EXEC_YIELD) {
        /* Instantiation is not a resumable command transaction yet. In
         * particular, a paused start is not a satisfied assert_invalid. */
        if (engine && !native_store_keep_orphan(handler->store, engine)) exec_free(engine);
        handler_result(handler, 0, "(module)", "yielding module starts are unsupported in WAST handlers");
        handler->stopped = 1;
        handler->exit_code = 126;
        return;
    }
    if (group->has_module_assertion) {
        int passed = group->module_assert_kind == WAST_ASSERT_TRAP ?
                     status == EXEC_ERROR_TRAP : status != EXEC_OK;
        handler_result(handler, passed, "(module)", passed ? NULL :
                        status == EXEC_OK ? "module unexpectedly instantiated" : error.message);
        /* Failed starts may already have linked callers; retain their engine. */
        if (engine && !native_store_keep_orphan(handler->store, engine)) {
            exec_free(engine);
            handler->stopped = 1;
            handler_result(handler, 0, "(module)", "cannot retain module assertion engine");
        }
        return;
    }
    if (status != EXEC_OK) {
        if (engine && !native_store_keep_orphan(handler->store, engine)) exec_free(engine);
        handler_result(handler, 0, "(module)", error.message);
        handler->stopped = 1;
    } else if (!native_store_add(handler->store, engine, &group->module, module)) {
        exec_free(engine);
        handler_result(handler, 0, "(module)", "cannot retain module instance");
        handler->stopped = 1;
    }
}

static int handler_command(wast_stream_command_kind kind, const char *bytes,
    size_t length, size_t offset, unsigned line, wast_script *parsed, void *opaque) {
    wast_process_handler *handler = opaque;
    (void)bytes; (void)length; (void)offset; (void)line;
    if (parsed->error[0]) {
        handler_result(handler, 0, "(parse)", parsed->error);
        handler->stopped = 1;
        return 0;
    }
    if (kind == WAST_STREAM_REGISTER && parsed->group_count == 1) {
        wast_module *registration = &parsed->groups[0].module;
        native_linked_module *target = native_selected_module(handler->store,
                                                               registration->register_target);
        if (!target) handler_result(handler, 0, "(register)", "unknown module id");
        else snprintf(target->registered, sizeof(target->registered), "%s", registration->register_name);
        return 0;
    }
    if ((kind == WAST_STREAM_ASSERTION || kind == WAST_STREAM_INVOKE) &&
        parsed->group_count == 1 && !parsed->groups[0].has_module_assertion &&
        parsed->assertion_count == 1) {
        /* Copy values/expectations before the scanner frees the command. */
        handler->assertion = parsed->assertions[0];
        handler->assertion_engine = native_selected_engine(handler->store, handler->assertion.module_id);
        handler_assertion(handler);
        return 0;
    }
    if (!parsed->group_count) return 0;
    wast_script *owned = handler_retain(handler, parsed);
    if (!owned) {
        handler_result(handler, 0, "(module)", "WAST handler retained-command bound/allocation failed");
        handler->stopped = 1;
        return 0;
    }
    for (int i = 0; i < owned->group_count && !handler->stopped; i++)
        handler_module(handler, &owned->groups[i]);
    return 0;
}

exec_status wast_process_handler_step(const uint8_t *source, size_t size,
    size_t offset, unsigned line, size_t *next_offset, unsigned *next_line, void *opaque) {
    wast_process_handler *handler = opaque;
    (void)offset; (void)line;
    if (!handler || !handler->store || !source || !size || !next_offset || !next_line)
        return EXEC_ERROR_FORMAT;
    if (!handler->stream_active) {
        wast_stream_init(&handler->stream, (const char *)source, size);
        handler->stream_active = 1;
    }
    if (handler->pending) handler_assertion(handler);
    while (!handler->pending && !handler->stopped) {
        int status = wast_stream_next(&handler->stream, handler_command, handler);
        if (status <= 0) {
            if (status < 0) handler_result(handler, 0, "(parse)", handler->stream.error);
            break;
        }
    }
    if (wast_stream_position(&handler->stream, next_offset, next_line)) return EXEC_ERROR_FORMAT;
    if (handler->pending) return EXEC_YIELD;
    wast_stream_destroy(&handler->stream);
    handler->stream_active = 0;
    /* Commands belong to this child, not the parent's module namespace.
     * Destroy them before the child provider clones are reaped, and before a
     * later fork tries to clone their no-longer-live import bindings. */
    while (handler->store->module_count > handler->module_start) {
        native_linked_module *module = &handler->store->modules[--handler->store->module_count];
        exec_free(module->engine);
        memset(module, 0, sizeof(*module));
    }
    while (handler->store->orphan_count > handler->orphan_start)
        exec_free(handler->store->orphan_engines[--handler->store->orphan_count]);
    while (handler->retained_count > handler->retained_start) {
        wast_script *script = handler->retained[--handler->retained_count];
        wast_script_free(script);
        free(script);
    }
    native_process_capsule *capsule = native_store_active_capsule(handler->store);
    if (!capsule) return EXEC_ERROR_FORMAT;
    capsule->handler.exit_code = handler->exit_code ? handler->exit_code : handler->failed ? 1 : 0;
    return EXEC_ERROR_EXIT;
}
