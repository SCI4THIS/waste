#include "process_driver.h"
#include "runtime_internal.h"
#include "lib/include/kernel.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

void native_process_driver_init(native_process_driver *driver) {
    memset(driver, 0, sizeof(*driver));
    exec_continuation_init(&driver->continuation);
    driver->initialized = 1;
}
void native_process_driver_destroy(native_process_driver *driver) {
    if (!driver) return;
    if (driver->initialized) exec_continuation_destroy(&driver->continuation);
    memset(driver, 0, sizeof(*driver));
}
static void process_driver_trace(native_process_driver *driver, const char *event) {
    if (driver->trace) driver->trace(driver->trace_context, event);
}
static void process_driver_engine_trace(native_process_driver *driver,
                                        const char *label, waste_exec_engine *engine) {
    char event[64];
    snprintf(event, sizeof(event), "%.40s-f%u-i%u", label,
             engine ? engine->func_count : 0, engine ? engine->import_func_count : 0);
    process_driver_trace(driver, event);
}
static void process_driver_select(native_process_driver *driver, native_store *store,
    waste_exec_engine *engine, uint32_t function, const wasm_value *args, int count) {
    native_driver_selection *selected = &driver->selection;
    native_process_capsule *capsule = native_store_active_capsule(store);
    selected->pid = native_store_getpid(store);
    selected->engine = engine;
    selected->func_idx = function;
    selected->arg_count = count;
    if (count) memmove(selected->args, args, (size_t)count * sizeof(*args));
    if (capsule && capsule->engine) {
        selected->engine = capsule->engine;
        selected->func_idx = capsule->root_func_idx;
        selected->arg_count = capsule->root_arg_count;
        memcpy(selected->args, capsule->root_args, sizeof(selected->args));
    }
}

static void process_consume_parent_continuation(native_process_capsule *capsule) {
    if (!capsule || !capsule->continuation) return;
    exec_continuation_destroy(capsule->continuation);
    free(capsule->continuation);
    capsule->continuation = NULL;
    capsule->continuation_valid = 0;
}

static int process_engine_seen(waste_exec_engine **engines, uint32_t count,
                               waste_exec_engine *engine) {
    for (uint32_t i = 0; i < count; i++)
        if (engines[i] == engine) return 1;
    return 0;
}

static exec_status process_capture_parent_graph(native_process_driver *driver, native_store *store,
                                                native_process_capsule *capsule,
                                                waste_exec_engine *root,
                                                exec_error *error) {
    uint32_t capacity = (uint32_t)store->module_count +
                        (uint32_t)store->orphan_count;
    char graph_event[64];
    snprintf(graph_event, sizeof(graph_event), "graph-cap-m%d-o%d",
             store->module_count, store->orphan_count);
    process_driver_trace(driver, graph_event);
    waste_exec_engine **engines = NULL;
    exec_continuation *continuations = NULL;
    uint32_t count = 0;
    if (capacity) {
        engines = calloc(capacity, sizeof(*engines));
        continuations = calloc(capacity, sizeof(*continuations));
        if (!engines || !continuations) {
            free(engines); free(continuations);
            return exec_fail(error, EXEC_ERROR_TRAP,
                             "parent continuation graph allocation failed");
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        uint32_t n = pass == 0 ? (uint32_t)store->module_count :
                                (uint32_t)store->orphan_count;
        for (uint32_t i = 0; i < n; i++) {
            waste_exec_engine *engine = pass == 0 ?
                store->modules[i].engine :
                store->orphan_engines[i];
            if (!engine || engine == root ||
                process_engine_seen(engines, count, engine)) continue;
            if (count == capacity) {
                for (uint32_t j = 0; j < count; j++)
                    exec_continuation_destroy(&continuations[j]);
                free(engines); free(continuations);
                return exec_fail(error, EXEC_ERROR_TRAP,
                                 "parent continuation graph capacity exhausted");
            }
            if (exec_continuation_capture(
                    engine, &continuations[count], error) != EXEC_OK) {
                char message[256];
                snprintf(message, sizeof(message),
                         "parent continuation graph capture failed: %.190s",
                         error->message[0] ? error->message : "unknown error");
                for (uint32_t j = 0; j < count; j++)
                    exec_continuation_destroy(&continuations[j]);
                free(engines); free(continuations);
                return exec_fail(error, EXEC_ERROR_TRAP, message);
            }
            engines[count++] = engine;
        }
    }
    capsule->continuation_engines = engines;
    capsule->continuations = continuations;
    capsule->continuation_count = count;
    return EXEC_OK;
}

static exec_status process_restore_parent_graph(native_process_capsule *capsule,
                                                exec_error *error) {
    if (!capsule) return exec_fail(error, EXEC_ERROR_TRAP,
                                   "missing parent continuation capsule");
    for (uint32_t i = 0; i < capsule->continuation_count; i++) {
        exec_status status = exec_continuation_resume(
            &capsule->continuations[i], capsule->continuation_engines[i],
            0, EXEC_YIELD_NONE, error);
        if (status != EXEC_OK) return status;
    }
    return EXEC_OK;
}

static void process_discard_parent_graph(native_process_capsule *capsule) {
    if (!capsule) return;
    for (uint32_t i = 0; i < capsule->continuation_count; i++)
        exec_continuation_destroy(&capsule->continuations[i]);
    free(capsule->continuations);
    free(capsule->continuation_engines);
    capsule->continuations = NULL;
    capsule->continuation_engines = NULL;
    capsule->continuation_count = 0;
}

/* Restore the suspended Bash parent before a child replacement starts or
 * finishes.  Both ordinary Wasm image exec and future process handlers must
 * use this same ordering so the parent continuation is never skipped. */
static exec_status process_restore_fork_parent(native_process_driver *driver, native_store *store,
                                               exec_error *error) {
    native_process_capsule *parent_capsule;
    waste_exec_engine *parent_engine;
    uint32_t parent_func_idx;
    int parent_arg_count;
    wasm_value parent_args[WAST_MAX_ARGS];
    exec_status restored;
    if (!store || !driver->fork_active || driver->parent_restored)
        return EXEC_OK;
    parent_capsule = native_store_process_capsule(
        store, store->fork_parent_pid);
    if (!parent_capsule)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "missing parent process capsule");
    if (!parent_capsule->continuation || !parent_capsule->engine)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "missing parent continuation engine");
    parent_engine = parent_capsule->engine;
    parent_func_idx = parent_capsule->continuation->root_func_idx;
    parent_arg_count = parent_capsule->continuation->root_arg_count;
    memcpy(parent_args, parent_capsule->continuation->root_args,
           sizeof(parent_args));
    restored = process_restore_parent_graph(parent_capsule, error);
    if (restored != EXEC_OK) return restored;
    /* The graph snapshots are consumed by the restore.  Release them before
     * the next WAST/WAT child can capture a replacement graph; otherwise
     * repeated execs leak continuation frames and exhaust the Wasm heap. */
    process_discard_parent_graph(parent_capsule);
    restored = exec_continuation_resume(
        parent_capsule->continuation, parent_engine,
        store->fork_parent_pid, EXEC_YIELD_FORK, error);
    if (restored != EXEC_OK) return restored;
    parent_capsule->engine = parent_engine;
    parent_capsule->root_func_idx = parent_func_idx;
    parent_capsule->root_arg_count = parent_arg_count;
    memcpy(parent_capsule->root_args, parent_args, sizeof(parent_args));
    parent_capsule->state = NATIVE_PROCESS_RUNNABLE;
    driver->active_engine = parent_engine;
    driver->active_func_idx = parent_func_idx;
    driver->active_arg_count = parent_arg_count;
    for (int i = 0; i < parent_arg_count; i++)
        driver->active_args[i] = parent_args[i];
    driver->parent_restored = 1;
    process_consume_parent_continuation(parent_capsule);
    return EXEC_OK;
}

static waste_exec_engine *process_image_runtime_export(
        native_store *store, waste_exec_engine *image, const char *name,
        uint32_t *index, exec_error *error) {
    native_linked_module *env;
    waste_exec_engine *runtime;
    if (exec_find_export(image, name, index, error) == EXEC_OK)
        return image;
    /* Production images import libc explicitly. Bootstrap the process-local
     * DSO, independently of any WAST compatibility registration alias. */
    native_loaded_library *libc = native_store_find_library(store, "libc");
    runtime = libc ? libc->engine : NULL;
    if (runtime && runtime->memory == image->memory) {
        if (error) memset(error, 0, sizeof(*error));
        if (exec_find_export(runtime, name, index, error) == EXEC_OK)
            return runtime;
    }
    env = native_registered_module(store, "env");
    runtime = env ? native_store_process_engine(store, env->engine) : NULL;
    if (!runtime || runtime->memory != image->memory) return NULL;
    if (error) memset(error, 0, sizeof(*error));
    return exec_find_export(runtime, name, index, error) == EXEC_OK ?
        runtime : NULL;
}

static void process_discard_provider_frames(waste_exec_engine *engine,
                                            waste_exec_engine *image) {
    if (!engine || engine == image) return;
    /* exec replaces activations, not the resident provider's linked objects.
     * A fork clone may still hold the abandoned libc execve activation. Its
     * first bootstrap call must not resume that activation instead. */
    memset(engine->yield_frames, 0, sizeof(engine->yield_frames));
    engine->active_call_depth = 0;
    runtime_free_jump_snapshots(engine);
    engine->jump_snapshots = NULL;
    engine->jump_snapshot_count = 0;
    engine->jump_snapshot_capacity = 0;
}

static void process_discard_exec_provider_continuations(native_store *store,
                                                        waste_exec_engine *image) {
    native_process_capsule *capsule = native_store_active_capsule(store);
    if (!capsule) return;
    for (uint32_t i = 0; i < capsule->linked_engine_count; i++)
        process_discard_provider_frames(capsule->linked_engines[i], image);
    for (uint32_t i = 0; i < capsule->loaded_library_count; i++)
        process_discard_provider_frames(capsule->loaded_libraries[i].engine,
                                       image);
    /* Without a fork clone, providers are store-owned. With a clone, do not
     * touch canonical engines: they belong to the dormant parent. */
    if (!capsule->owned_fork_engine) {
        for (int i = 0; i < store->module_count; i++)
            process_discard_provider_frames(store->modules[i].engine, image);
        for (int i = 0; i < store->orphan_count; i++)
            process_discard_provider_frames(store->orphan_engines[i], image);
    }
}

static exec_status process_finish_exec_provider_continuations(
        exec_continuation *saved, uint32_t count, int restore,
        exec_error *error) {
    exec_status status = EXEC_OK;
    for (uint32_t i = 0; i < count; i++) {
        if (restore && status == EXEC_OK)
            status = exec_continuation_restore(&saved[i], error);
        exec_continuation_destroy(&saved[i]);
    }
    free(saved);
    return status;
}

static exec_status process_prepare_exec_provider_continuations(
        native_store *store, exec_continuation **saved_out,
        uint32_t *count_out, exec_error *error) {
    native_process_capsule *capsule = native_store_active_capsule(store);
    exec_continuation *saved = NULL;
    uint32_t count = 0;
    uint32_t total = 1 + capsule->linked_engine_count +
                     capsule->loaded_library_count;
    if (!capsule->owned_fork_engine)
        total += (uint32_t)store->module_count + (uint32_t)store->orphan_count;
    *saved_out = NULL;
    *count_out = 0;
    for (uint32_t i = 0; i < total; i++) {
        waste_exec_engine *engine;
        uint32_t index = i ? i - 1 : 0;
        if (!i)
            engine = capsule->engine;
        else if (index < capsule->linked_engine_count)
            engine = capsule->linked_engines[index];
        else if ((index -= capsule->linked_engine_count) <
                 capsule->loaded_library_count)
            engine = capsule->loaded_libraries[index].engine;
        else if ((index -= capsule->loaded_library_count) <
                 (uint32_t)store->module_count)
            engine = store->modules[index].engine;
        else
            engine = store->orphan_engines[index - (uint32_t)store->module_count];
        if (!engine || !engine->yield_frames[0].valid) continue;
        uint32_t j;
        for (j = 0; j < count; j++)
            if (saved[j].engine == engine) break;
        if (j != count) continue;
        exec_continuation *next = realloc(saved,
            (size_t)(count + 1) * sizeof(*saved));
        if (!next) {
            (void)process_finish_exec_provider_continuations(
                saved, count, 1, error);
            return exec_fail(error, EXEC_ERROR_TRAP,
                             "exec provider continuation allocation failed");
        }
        saved = next;
        exec_continuation_init(&saved[count]);
        if (exec_continuation_capture(engine, &saved[count], error) != EXEC_OK) {
            (void)process_finish_exec_provider_continuations(
                saved, count, 1, error);
            return error->status;
        }
        count++;
        /* Library constructors run during preflight. Temporarily detach old
         * activations now, restoring them if loading/commit fails so execve
         * can return its errno through the original call chain. */
        process_discard_provider_frames(engine, NULL);
    }
    *saved_out = saved;
    *count_out = count;
    return EXEC_OK;
}

/* Drive an invocation through internal process transitions.  A fork yield is
 * handled entirely inside this per-session engine driver: the parent evaluator
 * and store are captured, the child continuation runs with fork returning zero, and a
 * child exit restores the parent before the fork call is resumed with its PID.
 * Terminal/select yields remain visible to the platform runtime. */
exec_status native_process_driver_invoke(native_process_driver *driver, native_store *store,
                                          waste_exec_engine *engine,
                                          uint32_t func_idx,
                                          const wasm_value *args, int arg_count,
                                          wasm_value *results, int *result_count,
                                          exec_error *error) {
    if (!driver || !store || !error || arg_count < 0 || arg_count > WAST_MAX_ARGS ||
        (arg_count && !args))
        return exec_fail(error, EXEC_ERROR_TRAP, "invalid process invocation");
    process_driver_select(driver, store, engine, func_idx, args, arg_count);
    waste_exec_engine *active_engine = engine;
    uint32_t active_func_idx = func_idx;
    int image_active = driver->image_active;
    int child_exit_recorded = 0;
    int handler_wake_recorded = 0;
    native_process_capsule *capsule =
        native_store_active_capsule(store);
    driver->active_engine = active_engine;
    driver->active_func_idx = active_func_idx;
    driver->active_arg_count = arg_count;
    for (int i = 0; i < arg_count && i < WAST_MAX_ARGS; i++)
        driver->active_args[i] = args[i];
    if (capsule && capsule->handler.kind == NATIVE_PROCESS_HANDLER_NONE) {
        capsule->engine = active_engine;
        capsule->root_func_idx = active_func_idx;
        capsule->root_arg_count = arg_count;
        for (int i = 0; i < arg_count && i < WAST_MAX_ARGS; i++)
            capsule->root_args[i] = args[i];
        capsule->state = NATIVE_PROCESS_RUNNABLE;
        (void)native_process_capsule_bind_memory(capsule);
    }
    exec_status status = EXEC_OK;
    for (;;) {
        exec_status stop = exec_execution_check(&store->execution_control, error);
        if (stop != EXEC_OK) return stop;
        capsule = native_store_active_capsule(store);
        if (capsule && capsule->handler.kind != NATIVE_PROCESS_HANDLER_NONE) {
            status = native_store_run_process_handler_step(
                store, driver->handler_step);
            stop = exec_execution_check(&store->execution_control, error);
            if (stop != EXEC_OK) return stop;
            if (status == EXEC_YIELD) {
                if (driver->handler_wait_reason == EXEC_YIELD_NONE)
                    driver->handler_wait_reason = EXEC_YIELD_READ;
                if (native_store_suspend_process_handler_for_yield(
                        store, driver->handler_wait_reason) != 0)
                    return exec_fail(error, EXEC_ERROR_TRAP,
                                     "failed to suspend WAST handler");
                driver->selection.pid = native_store_getpid(store);
                driver->selection.wait_reason = error->yield_reason;
                driver->selection.engine = NULL;
                driver->selection.func_idx = 0;
                driver->selection.arg_count = 0;
                driver->selection.wait_reason = driver->handler_wait_reason;
                error->status = status;
                error->yield_reason = driver->handler_wait_reason;
                return status;
            }
            if (status != EXEC_OK && status != EXEC_ERROR_EXIT)
                driver->handler_wait_reason = EXEC_YIELD_NONE;
            {
                exec_status handler_status = EXEC_OK;
                int handler_exit_code = 0;
                if (native_store_process_handler_result(
                        store, &handler_status,
                        &handler_exit_code) != 0)
                    return exec_fail(error, EXEC_ERROR_TRAP,
                                     "missing WAST handler result");
                if (status != EXEC_OK)
                    handler_status = status;
                handler_exit_code = native_process_handler_default_exit_code(
                    handler_status, handler_exit_code);
                error->exit_code = handler_exit_code;
                if (driver->fork_active &&
                    native_store_getpid(store) ==
                        store->fork_child_pid) {
                    /* Leave the child selected here.  The common fork-exit
                     * path below must perform the parent wake after it has
                     * observed the child exit; the combined helper selects
                     * the parent too early for that path. */
                    if ((error->signal ?
                        native_store_complete_process_handler_signal(
                            store, handler_status, error->signal) :
                        native_store_complete_process_handler(
                            store, handler_status,
                            handler_exit_code)) != 0)
                        return exec_fail(error, EXEC_ERROR_TRAP,
                                         "failed to complete WAST handler");
                    process_driver_engine_trace(driver, "handler-completed",
                                                      driver->parent_engine);
                    status = EXEC_ERROR_EXIT;
                } else {
                    if ((error->signal ?
                        native_store_complete_process_handler_signal(
                            store, handler_status, error->signal) :
                        native_store_complete_process_handler(
                            store, handler_status,
                            handler_exit_code)) != 0)
                        return exec_fail(error, EXEC_ERROR_TRAP,
                                         "failed to complete WAST handler");
                    status = EXEC_ERROR_EXIT;
                }
            }
        }
        if (status != EXEC_ERROR_EXIT) {
            uint64_t function_count = active_engine ?
                (uint64_t)active_engine->import_func_count +
                (uint64_t)active_engine->func_count : 0;
            if (!active_engine || (uint64_t)active_func_idx >= function_count) {
                char entry_error[128];
                snprintf(entry_error, sizeof(entry_error),
                         "process entry %u is outside engine function range %llu",
                         active_func_idx,
                         (unsigned long long)function_count);
                return exec_fail(error, EXEC_ERROR_NOT_FOUND, entry_error);
            }
            if (!driver->fork_active &&
                active_engine == driver->parent_engine)
                process_driver_trace(driver, "parent-invoke");
            status = exec_invoke(active_engine, active_func_idx,
                                 args, arg_count, results, result_count, error);
            if (!driver->fork_active &&
                active_engine == driver->parent_engine)
                process_driver_trace(driver, status == EXEC_YIELD ?
                                          "parent-invoke-yield" :
                                          "parent-invoke-return");
            if (!driver->fork_active &&
                active_engine == driver->parent_engine &&
                status != EXEC_OK && status != EXEC_YIELD &&
                status != EXEC_ERROR_EXIT) {
                char invoke_event[256];
                snprintf(invoke_event, sizeof(invoke_event),
                         "parent-invoke-error-s%d-f%llu-%.180s", status,
                         error ? (unsigned long long)error->memory_fault_address : 0,
                         error && error->message[0] ? error->message : "unknown");
                process_driver_trace(driver, invoke_event);
            }
            if (!driver->fork_active &&
                active_engine == driver->parent_engine &&
                status == EXEC_YIELD) {
                char yield_event[32];
                snprintf(yield_event, sizeof(yield_event), "parent-yield-r%u",
                         error->yield_reason);
                process_driver_trace(driver, yield_event);
            }
        }
        if (status == EXEC_YIELD &&
            error->yield_reason == EXEC_YIELD_EXEC) {
            process_driver_trace(driver, "exec-yield");
            native_process_image *image = NULL;
            if (native_store_prepare_process_exec(
                    store, &store->exec_request) != 0)
                return exec_fail(error, EXEC_ERROR_TRAP,
                                 "invalid process exec transition");
            exec_continuation *saved_providers = NULL;
            uint32_t saved_provider_count = 0;
            exec_status prepared = process_prepare_exec_provider_continuations(
                store, &saved_providers, &saved_provider_count, error);
            if (prepared != EXEC_OK) return prepared;
            exec_status loaded = native_store_instantiate_executable(
                store, &store->exec_request, &image, error);
            if (store->execution_control.stopped) {
                (void)process_finish_exec_provider_continuations(
                    saved_providers, saved_provider_count, 0, error);
                native_process_image_release(image);
                return exec_execution_check(&store->execution_control, error);
            }
            if (loaded == EXEC_ERROR_UNSUPPORTED &&
                store->exec_request.handler_kind ==
                    NATIVE_EXEC_HANDLER_WAST && driver->handler_step) {
                process_driver_engine_trace(driver, "handler-before",
                                                  driver->parent_engine);
                if (native_store_commit_process_handler_with_context(
                        store, &store->exec_request,
                        NATIVE_PROCESS_HANDLER_WAST, driver->handler_context, NULL) != 0) {
                    (void)process_finish_exec_provider_continuations(
                        saved_providers, saved_provider_count, 1, error);
                    return exec_fail(error, EXEC_ERROR_TRAP,
                                     "failed to commit WAST process handler");
                }
                (void)process_finish_exec_provider_continuations(
                    saved_providers, saved_provider_count, 0, error);
                process_discard_exec_provider_continuations(store, NULL);
                process_driver_engine_trace(driver, "handler-after",
                                                  driver->parent_engine);
                native_exec_request_destroy(&store->exec_request);
                active_engine = NULL;
                active_func_idx = 0;
                args = NULL;
                arg_count = 0;
                image_active = 0;
                driver->image_active = 0;
                if (driver->handler_reset) driver->handler_reset(driver->handler_context);
                driver->execs++;
                continue;
            }
            if (loaded != EXEC_OK) {
                char reject_event[192];
                snprintf(reject_event, sizeof(reject_event),
                         "exec-preflight-rejected-s%d-%.140s",
                         error ? (int)error->status : -1,
                         error && error->message[0] ? error->message : "unknown");
                process_driver_trace(driver, reject_event);
                exec_status restored = process_finish_exec_provider_continuations(
                    saved_providers, saved_provider_count, 1, error);
                if (restored != EXEC_OK) return restored;
                /* Leave the old evaluator suspended. Its next import retry
                 * consumes this one-shot errno and returns from execve. */
                store->exec_request.failure_errno = POSIX_ENOEXEC;
                native_store_abort_process_exec(store);
                continue;
            }
            /* Eagerly restore the dormant parent before running replacement
             * guest code.  The child then runs against an independent image,
             * and child exit only selects the already-restored parent. */
            if (driver->fork_active && !driver->parent_restored) {
                exec_status restored = process_restore_fork_parent(driver, store,
                                                                    error);
                if (restored != EXEC_OK) {
                    (void)process_finish_exec_provider_continuations(
                        saved_providers, saved_provider_count, 1, error);
                    native_process_image_release(image);
                    return restored;
                }
            }
            if (native_store_commit_process_image(store, image) != 0) {
                exec_status restored = process_finish_exec_provider_continuations(
                    saved_providers, saved_provider_count, 1, error);
                native_process_image_release(image);
                if (restored != EXEC_OK) return restored;
                /* Once the parent has been eagerly restored there is no
                 * coherent child evaluator left to retry.  Treat a commit
                 * failure as an internal transition error rather than
                 * resuming the stale pre-exec child. */
                if (driver->parent_restored)
                    return exec_fail(error, EXEC_ERROR_TRAP,
                                     "failed to commit executable image");
                native_store_abort_process_exec(store);
                store->exec_request.failure_errno = POSIX_ENOMEM;
                continue;
            }
            (void)process_finish_exec_provider_continuations(
                saved_providers, saved_provider_count, 0, error);
            native_exec_request_destroy(&store->exec_request);
            process_discard_exec_provider_continuations(store, image->engine);
            driver->execs++;
            /* Run guest libc bootstrap on the new image when the
             * standard init exports are present.  This mirrors the
             * WAST-level init sequence the launcher performs for Bash. */
            {
                uint32_t init_func;
                waste_exec_engine *init_engine;
                wasm_value init_args[4];
                int init_result_count = 0;
                exec_error init_error;
                uint32_t heap_base = 65536;
                exec_global *heap_global = NULL;
                memset(&init_error, 0, sizeof(init_error));
                if (exec_find_export_global(image->engine, "__heap_base",
                                            &heap_global,
                                            &init_error) == EXEC_OK
                    && heap_global
                    && heap_global->value.type == WASM_VALTYPE_I32
                    && heap_global->value.i32 > (int32_t)heap_base) {
                    heap_base = (uint32_t)heap_global->value.i32;
                }
                /* The exec startup block is materialized at the top of the
                 * image's initial linear memory.  Starting malloc at the
                 * module's __heap_base would let a growing heap overwrite
                 * argv, envp, and the startup metadata before memory.grow is
                 * needed.  Place the process heap immediately after that
                 * immutable block instead. */
                if (image->startup_size) {
                    uint64_t startup_end = (uint64_t)image->startup_ptr +
                                           image->startup_size;
                    uint64_t aligned_end = (startup_end + 15u) & ~15u;
                    if (aligned_end <= UINT32_MAX &&
                        aligned_end > heap_base)
                        heap_base = (uint32_t)aligned_end;
                }
                {
                    char heap_event[128];
                    snprintf(heap_event, sizeof(heap_event),
                             "image-heap-base-%u", heap_base);
                    process_driver_trace(driver, heap_event);
                }
                memset(&init_error, 0, sizeof(init_error));
                init_engine = process_image_runtime_export(
                    store, image->engine, "waste_allocator_init",
                    &init_func, &init_error);
                if (init_engine) {
                    init_args[0].type = WASM_VALTYPE_I32;
                    init_args[0].i32 = (int32_t)heap_base;
                    exec_status init_st = exec_invoke(init_engine, init_func,
                                      init_args, 1, NULL,
                                      &init_result_count, &init_error);
                    if (init_st != EXEC_OK) {
                        char ev[128];
                        snprintf(ev, sizeof(ev), "image-alloc-FAIL-s%d-%.80s",
                                 init_st, init_error.message);
                        process_driver_trace(driver, ev);
                    }
                }
                process_driver_trace(driver, "image-alloc-ok");
                memset(&init_error, 0, sizeof(init_error));
                init_engine = process_image_runtime_export(
                    store, image->engine, "waste_environ_set",
                    &init_func, &init_error);
                if (init_engine) {
                    init_args[0].type = WASM_VALTYPE_I32;
                    init_args[0].i32 = (int32_t)(image->startup_ptr + 44u +
                        (image->argc + 1u) * sizeof(uint32_t));
                    exec_status init_st = exec_invoke(init_engine, init_func,
                                      init_args, 1, NULL,
                                      &init_result_count, &init_error);
                    if (init_st != EXEC_OK) {
                        char ev[128];
                        snprintf(ev, sizeof(ev), "image-env-FAIL-s%d-%.80s",
                                 init_st, init_error.message);
                        process_driver_trace(driver, ev);
                    } else {
                        process_driver_trace(driver, "image-env-ok");
                    }
                } else {
                    process_driver_trace(driver, "image-env-missing");
                }
                memset(&init_error, 0, sizeof(init_error));
                init_engine = process_image_runtime_export(
                    store, image->engine, "waste_stdio_init",
                    &init_func, &init_error);
                if (init_engine) {
                    init_args[0].type = WASM_VALTYPE_I32;
                    init_args[0].i32 = 4096;
                    exec_status init_st = exec_invoke(init_engine, init_func,
                                      init_args, 1, NULL,
                                      &init_result_count, &init_error);
                    if (init_st != EXEC_OK) {
                        char ev[128];
                        snprintf(ev, sizeof(ev), "image-stdio-FAIL-s%d-%.80s",
                                 init_st, init_error.message);
                        process_driver_trace(driver, ev);
                    }
                }
                process_driver_trace(driver, "image-stdio-ok");
                memset(&init_error, 0, sizeof(init_error));
                /* Only an image that owns its stdin/stdout/stderr pointer
                 * slots may request them to be populated.  A dynamically
                 * linked image uses waste_stdin/out/err from the resident
                 * libc and must not write FILE pointers to addresses 0,1,2. */
                init_engine = exec_find_export(
                    image->engine, "waste_stdio_bind", &init_func,
                    &init_error) == EXEC_OK ? image->engine : NULL;
                if (init_engine) {
                    init_args[0].type = WASM_VALTYPE_I32;
                    init_args[0].i32 = 0;
                    init_args[1].type = WASM_VALTYPE_I32;
                    init_args[1].i32 = 1;
                    init_args[2].type = WASM_VALTYPE_I32;
                    init_args[2].i32 = 2;
                    exec_status init_st = exec_invoke(init_engine, init_func,
                                      init_args, 3, NULL,
                                      &init_result_count, &init_error);
                    if (init_st != EXEC_OK) {
                        char ev[128];
                        snprintf(ev, sizeof(ev), "image-bind-FAIL-s%d-%.80s",
                                 init_st, init_error.message);
                        process_driver_trace(driver, ev);
                    } else {
                        process_driver_trace(driver, "image-bind-ok");
                    }
                } else {
                    process_driver_trace(driver, "image-bind-missing");
                }
            }
            active_engine = image->engine;
            active_func_idx = image->entry_func;
            image_active = 1;
            driver->image_active = 1;
            driver->active_engine = active_engine;
            driver->active_func_idx = active_func_idx;
            driver->active_arg_count = 0;
            args = NULL;
            arg_count = 0;
            capsule = native_store_active_capsule(store);
            if (capsule) {
                capsule->engine = active_engine;
                capsule->image = image;
                capsule->root_func_idx = active_func_idx;
                capsule->root_arg_count = 0;
                capsule->generation++;
                (void)native_process_capsule_bind_memory(capsule);
            }
            continue;
        }
        if (status == EXEC_ERROR_INTERRUPTED)
            return status;
        if (image_active && status != EXEC_YIELD) {
            char image_event[512];
            snprintf(image_event, sizeof(image_event),
                     "image-return-s%d-e%d-f%llu-%s", status,
                     error ? error->exit_code : -1,
                     error ? (unsigned long long)error->memory_fault_address : 0,
                     error && error->message[0] ? error->message : "none");
            process_driver_trace(driver, image_event);
            if (status == EXEC_ERROR_EXIT) {
                /* Preserve an explicit guest exit code. */
            } else if (status == EXEC_OK) {
                error->exit_code = 0;
            } else {
                error->exit_code = 127;
            }
            if (error->signal)
                (void)native_store_signal_process(
                    store, native_store_getpid(store),
                    error->signal);
            else
                (void)native_store_exit_process(store,
                                                error->exit_code);
            child_exit_recorded = 1;
            status = EXEC_ERROR_EXIT;
            image_active = 0;
            driver->image_active = 0;
        }
        if (status == EXEC_ERROR_EXIT && driver->fork_active &&
            native_store_getpid(store) ==
                store->fork_child_pid) {
            int parent_pid = store->fork_parent_pid;
            process_driver_engine_trace(driver, "child-before-wake",
                                              driver->parent_engine);
            if (!child_exit_recorded && !handler_wake_recorded)
                (void)native_store_exit_process(store,
                                                error->exit_code);
            if (!handler_wake_recorded &&
                native_store_wake_process(store, parent_pid,
                                          store->fork_child_pid) != 0)
                return exec_fail(error, EXEC_ERROR_TRAP,
                                 "failed to restore parent process");
            if (!driver->parent_restored) {
                exec_status restored = process_restore_fork_parent(driver, store,
                                                                    error);
                if (restored != EXEC_OK) return restored;
            }
            driver->child_exits++;
            driver->fork_active = 0;
            native_process_capsule *parent_capsule =
                native_store_process_capsule(store, parent_pid);
            exec_continuation_destroy(&driver->continuation);
            /* The child image has finished.  Resume the original parent
             * evaluator at the suspended fork import; otherwise the next
             * iteration would invoke the replacement image a second time. */
            active_engine = parent_capsule && parent_capsule->engine ?
                parent_capsule->engine : driver->parent_engine;
            process_driver_engine_trace(driver, "parent-after-wake", active_engine);
            active_func_idx = parent_capsule && parent_capsule->engine ?
                parent_capsule->root_func_idx : driver->parent_func_idx;
            args = parent_capsule && parent_capsule->engine ?
                parent_capsule->root_args : driver->parent_args;
            arg_count = parent_capsule && parent_capsule->engine ?
                parent_capsule->root_arg_count : driver->parent_arg_count;
            image_active = 0;
            driver->image_active = 0;
            driver->parent_restored = 0;
            handler_wake_recorded = 0;
            driver->active_engine = active_engine;
            driver->active_func_idx = active_func_idx;
            driver->active_arg_count = arg_count;
            capsule = native_store_active_capsule(store);
            if (capsule) {
                capsule->engine = active_engine;
                capsule->root_func_idx = active_func_idx;
                capsule->root_arg_count = arg_count;
                capsule->state = NATIVE_PROCESS_RUNNABLE;
                /* native_store_wake_process already queued the child PID;
                 * keep that result authoritative for the resumed fork. */
                capsule->pending_result_valid = 1;
            }
            status = EXEC_OK;
            continue;
        }
        if (status != EXEC_YIELD || error->yield_reason != EXEC_YIELD_FORK) {
            if (status == EXEC_YIELD) {
                driver->selection.engine = driver->active_engine ?
                    driver->active_engine : active_engine;
                driver->selection.func_idx = driver->active_engine ?
                    driver->active_func_idx : active_func_idx;
                driver->selection.arg_count = driver->active_engine ?
                    driver->active_arg_count : arg_count;
                for (int i = 0; i < driver->selection.arg_count; i++)
                    driver->selection.args[i] = driver->active_engine ?
                        driver->active_args[i] : args[i];
                driver->selection.pid = native_store_getpid(store);
            }
            return status;
        }
        if (driver->fork_active)
            return exec_fail(error, EXEC_ERROR_TRAP,
                             "nested child-first fork is unsupported");
        native_process_capsule *parent_capsule =
            native_store_process_capsule(store,
                                         native_store_getpid(store));
        if (!parent_capsule)
            return exec_fail(error, EXEC_ERROR_TRAP,
                             "missing parent process capsule");
        exec_continuation_destroy(&driver->continuation);
        exec_continuation_init(&driver->continuation);
        if (exec_continuation_capture(active_engine, &driver->continuation,
                                      error) != EXEC_OK)
            return error->status;
        /* The child may replace its image with a WAST handler while this
         * parent continuation remains suspended.  Keep the parent's image
         * alive until the continuation is restored and consumed. */
        exec_continuation_pin_image(&driver->continuation,
                                    parent_capsule->image);
        parent_capsule->engine = active_engine;
        parent_capsule->root_func_idx = active_func_idx;
        parent_capsule->root_arg_count = arg_count;
        for (int i = 0; i < arg_count && i < WAST_MAX_ARGS; i++)
            parent_capsule->root_args[i] = args[i];
        int parent_pid = native_store_getpid(store);
        driver->parent_engine = active_engine;
        process_driver_engine_trace(driver, "parent-at-fork",
                                          driver->parent_engine);
        driver->parent_func_idx = active_func_idx;
        driver->parent_arg_count = arg_count;
        for (int i = 0; i < arg_count && i < WAST_MAX_ARGS; i++)
            driver->parent_args[i] = args[i];
        exec_continuation_describe(&driver->continuation, active_engine,
                                   active_func_idx, args, arg_count,
                                   EXEC_YIELD_FORK, parent_pid, 1);
        parent_capsule->continuation = malloc(sizeof(*parent_capsule->continuation));
        if (!parent_capsule->continuation)
            return exec_fail(error, EXEC_ERROR_TRAP,
                             "parent continuation capsule allocation failed");
        *parent_capsule->continuation = driver->continuation;
        parent_capsule->continuation_valid = 1;
        exec_continuation_init(&driver->continuation);
        if (process_capture_parent_graph(driver, store, parent_capsule,
                                          active_engine, error) != EXEC_OK)
            return error->status;
        int child_pid = 0;
        if (native_store_fork_process(store, &child_pid) != 0 ||
            native_store_set_active_process(store, child_pid) != 0)
            return exec_fail(error, EXEC_ERROR_TRAP,
                             "failed to create child process");
        store->fork_parent_pid = parent_pid;
        store->fork_child_pid = child_pid;
        if (native_store_clone_process_graph(store, parent_pid,
                                             child_pid) != 0)
            return exec_fail(error, EXEC_ERROR_TRAP,
                             "failed to clone linked process graph");
        capsule = native_store_active_capsule(store);
        if (capsule) {
            capsule->pending_result = 0;
            capsule->pending_result_valid = 1;
        }
        driver->forks++;
        driver->fork_active = 1;
        if (parent_capsule)
            parent_capsule->state = NATIVE_PROCESS_BROWSER_BLOCKED;
        capsule = native_store_active_capsule(store);
        if (capsule) {
            /* Fork cloned the evaluator state before selecting the child.
             * Continue with the child-owned engine and descriptor. */
            active_engine = capsule->engine;
            active_func_idx = capsule->root_func_idx;
            args = capsule->root_args;
            arg_count = capsule->root_arg_count;
            capsule->state = NATIVE_PROCESS_RUNNABLE;
            driver->active_engine = active_engine;
            driver->active_func_idx = active_func_idx;
            driver->active_arg_count = arg_count;
        }
    }
}
