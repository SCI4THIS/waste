#include "process_driver.h"
#include "runtime_internal.h"
#include "instantiate.h"
#include "../config.h"
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
            if (!pass && store->modules[i].owner_pid &&
                store->modules[i].owner_pid != store->active_pid) continue;
            waste_exec_engine *engine = pass == 0 ?
                store->modules[i].engine :
                store->orphan_engines[i];
            engine = native_store_process_engine(store, engine);
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

/* Fork leaves two independently owned evaluator graphs. Restore the parent
 * immediately, returning the child PID without waiting for the child's exit. */
static exec_status process_restore_fork_parent(native_process_capsule *parent,
                                                int child_pid,
                                                exec_error *error) {
    exec_status status = process_restore_parent_graph(parent, error);
    if (status != EXEC_OK) return status;
    process_discard_parent_graph(parent);
    status = exec_continuation_resume(parent->continuation, parent->engine,
                                     parent->continuation->owner_pid,
                                     EXEC_YIELD_FORK, error);
    if (status != EXEC_OK) return status;
    process_consume_parent_continuation(parent);
    parent->pending_result = child_pid;
    parent->pending_result_valid = 1;
    parent->state = NATIVE_PROCESS_RUNNABLE;
    parent->wait_reason = EXEC_YIELD_NONE;
    return EXEC_OK;
}

static int process_wait_ready(native_store *store, native_process *process) {
    native_process_capsule *capsule = &process->capsule;
    if (capsule->wait_reason == EXEC_YIELD_NONE) return 1;
    if (capsule->wait_reason == EXEC_YIELD_WAITPID) {
        int children = 0;
        for (int i = 0; i < NATIVE_PROCESS_MAX; i++) {
            native_process *child = &store->processes[i];
            if (!child->used || child->ppid != process->pid ||
                (capsule->wait_pid > 0 && child->pid != capsule->wait_pid))
                continue;
            children++;
            if (child->zombie) return 1;
        }
        if (!children || posix_kernel_signal_default_pending(process->kernel)) return 1;
        for (unsigned i = 0; i < POSIX_SIGSET_BYTES / sizeof(uint32_t); i++) {
            if (process->kernel->pending_signals.words[i] &
                ~process->kernel->signal_mask.words[i]) return 1;
        }
        return 0;
    }
    if (capsule->wait_reason == EXEC_YIELD_READ ||
        capsule->wait_reason == EXEC_YIELD_SELECT)
        return process->kernel->wait.active &&
               posix_kernel_wait_poll(process->kernel) != POSIX_WAIT_BLOCKED;
    return 0;
}

static void process_driver_count_exits(native_process_driver *driver, native_store *store) {
    unsigned live = 0;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++)
        if (store->processes[i].used && !store->processes[i].zombie &&
            store->processes[i].pid != driver->invocation_pid) live++;
    /* A signal can zombie and reap a child inside one parent invocation, so
     * counting only the selected child's return misses asynchronous exits. */
    driver->child_exits = driver->forks >= live ? driver->forks - live : 0;
}

int native_process_driver_wait_timeout(native_store *store, uint64_t *nanoseconds) {
    int found = 0;
    uint64_t earliest = UINT64_MAX;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++) {
        native_process *process = &store->processes[i];
        if (!process->used || process->zombie || !process->kernel) continue;
        posix_kernel *kernel = process->kernel;
        posix_wait_record *wait = &kernel->wait;
        if (!wait->active || !wait->has_deadline || !kernel->clock_now) continue;
        uint64_t now = kernel->clock_now(kernel->clock_data);
        uint64_t remaining = wait->deadline_ns > now ? wait->deadline_ns - now : 0;
        if (remaining < earliest) earliest = remaining;
        found = 1;
    }
    if (found && nanoseconds) *nanoseconds = earliest;
    return found;
}

/* Pick runnable work after the current slot. If all processes are blocked,
 * publish the earliest deadline (or an input wait) to the platform adapter. */
static int process_schedule(native_process_driver *driver, native_store *store,
                            int prefer_current, exec_error *error) {
    int current = 0, blocked = -1, input = -1;
    uint64_t deadline = UINT64_MAX;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++)
        if (store->processes[i].used && store->processes[i].pid == store->active_pid)
            current = i;
    for (int n = 0; n < NATIVE_PROCESS_MAX; n++) {
        int i = (current + n + (prefer_current ? 0 : 1)) % NATIVE_PROCESS_MAX;
        native_process *process = &store->processes[i];
        if (!process->used || process->zombie || (!process->capsule.engine &&
            process->capsule.handler.kind == NATIVE_PROCESS_HANDLER_NONE)) continue;
        native_process_capsule *capsule = &process->capsule;
        if (process_wait_ready(store, process)) {
            if (native_store_set_active_process(store, process->pid) != 0) return -1;
            capsule->wait_reason = EXEC_YIELD_NONE;
            if (capsule->handler.kind != NATIVE_PROCESS_HANDLER_NONE &&
                capsule->handler.wait_reason != EXEC_YIELD_NONE &&
                native_store_resume_process_handler(store) != 0) return -1;
            process_driver_select(driver, store, capsule->engine,
                capsule->root_func_idx, capsule->root_args, capsule->root_arg_count);
            return 1;
        }
        if (capsule->wait_reason != EXEC_YIELD_READ &&
            capsule->wait_reason != EXEC_YIELD_SELECT) continue;
        posix_wait_record *wait = &process->kernel->wait;
        if (wait->active) {
            for (int fd = 0; fd < POSIX_KERNEL_FD_MAX; fd++) {
                const posix_ofd *ofd = process->kernel->fds[fd].ofd;
                if (posix_fd_isset(fd, &wait->readfds) && ofd &&
                    (ofd->kind == POSIX_OFD_TERMINAL || ofd->kind == POSIX_OFD_STREAM)) {
                    input = i;
                    break;
                }
            }
        }
        if (blocked < 0 || (wait->active && wait->has_deadline &&
                            wait->deadline_ns < deadline)) {
            blocked = i;
            deadline = wait->active && wait->has_deadline ? wait->deadline_ns : UINT64_MAX;
        }
    }
    if (input >= 0) blocked = input;
    if (blocked < 0) {
        exec_fail(error, EXEC_ERROR_TRAP, "process scheduler has no runnable or external wait");
        return -1;
    }
    native_process *process = &store->processes[blocked];
    if (native_store_set_active_process(store, process->pid) != 0) return -1;
    process->capsule.state = NATIVE_PROCESS_BROWSER_BLOCKED;
    process_driver_select(driver, store, process->capsule.engine,
        process->capsule.root_func_idx, process->capsule.root_args,
        process->capsule.root_arg_count);
    driver->selection.wait_reason = process->capsule.wait_reason;
    error->status = EXEC_YIELD;
    error->yield_reason = driver->selection.wait_reason;
    return 0;
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

/* Shared by initial startup and exec replacement. Runtime initializers are
 * ordinary calls and must complete before entering the program CRT. A failed
 * or yielding initializer never becomes a successful startup. */
static exec_status process_initialize_image(native_process_driver *driver,
    native_store *store, native_process_image *image, exec_error *error) {
    if (image->runtime_initialized) return EXEC_OK;
    uint32_t heap_base = EXEC_PAGE_SIZE;
    exec_global *heap_global = NULL;
    exec_error lookup_error = {0};
    if (exec_find_export_global(image->engine, "__heap_base", &heap_global,
            &lookup_error) == EXEC_OK && heap_global &&
        heap_global->value.type == WASM_VALTYPE_I32 &&
        heap_global->value.i32 > (int32_t)heap_base)
        heap_base = (uint32_t)heap_global->value.i32;
    /* Keep argv/environment above the module's heap, including after growth. */
    uint64_t end = ((uint64_t)image->startup_ptr + image->startup_size + 15u) & ~15ull;
    if (end > UINT32_MAX)
        return exec_fail(error, EXEC_ERROR_TRAP, "process startup heap overflow");
    if (end > heap_base) heap_base = (uint32_t)end;
    char event[128];
    snprintf(event, sizeof(event), "image-heap-base-%u", heap_base);
    process_driver_trace(driver, event);
    const char *names[] = {"waste_allocator_init", "waste_environ_set", "waste_stdio_init"};
    const char *events[] = {"image-alloc-ok", "image-env-ok", "image-stdio-ok"};
    uint32_t values[] = {heap_base,
        image->startup_ptr + 44u + (image->argc + 1u) * sizeof(uint32_t),
        PROCESS_STDIO_BUFFER_BYTES};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        uint32_t function;
        memset(&lookup_error, 0, sizeof(lookup_error));
        waste_exec_engine *provider = process_image_runtime_export(
            store, image->engine, names[i], &function, &lookup_error);
        if (!provider) continue; /* Freestanding executables need no libc. */
        uint32_t type_index;
        if (exec_get_func_type_index(provider, function, &type_index, error) != EXEC_OK ||
            type_index >= provider->type_count)
            return exec_fail(error, EXEC_ERROR_FORMAT, "invalid process runtime initializer");
        const exec_func_type *type = &provider->types[type_index];
        if (type->param_count != 1 || type->params[0] != WASM_VALTYPE_I32 ||
            type->result_count != (i == 1 ? 0 : 1) ||
            (i != 1 && type->results[0] != WASM_VALTYPE_I32))
            return exec_fail(error, EXEC_ERROR_FORMAT, "incompatible process runtime initializer");
        wasm_value argument = {.type = WASM_VALTYPE_I32, .i32 = (int32_t)values[i]};
        wasm_value result = {0};
        int count = 0;
        /* Initialization is a synchronous transaction, like DSO loading.
         * Keep deadlines active but defer event-loop pump checkpoints. */
        uint64_t quantum = store->execution_control.pump_quantum_ns;
        store->execution_control.pump_quantum_ns = 0;
        exec_status status = exec_invoke(provider, function, &argument, 1,
                                         &result, &count, error);
        store->execution_control.pump_quantum_ns = quantum;
        if (status != EXEC_OK) {
            snprintf(event, sizeof(event), "image-init-failed-%.70s", names[i]);
            process_driver_trace(driver, event);
            if (status == EXEC_YIELD)
                return exec_fail(error, EXEC_ERROR_UNSUPPORTED,
                                 "process runtime initializer cannot yield");
            return status;
        }
        if (i != 1 && (count != 1 || result.type != WASM_VALTYPE_I32 || !result.i32))
            return exec_fail(error, EXEC_ERROR_TRAP, "process runtime initialization failed");
        process_driver_trace(driver, events[i]);
    }
    image->runtime_initialized = 1;
    return EXEC_OK;
}

/* A store-owned provider holds the initial process resources. Executable and
 * DSO imports borrow them, and fork clones them through the ordinary provider
 * graph. No WAT/WAST bootstrap, guest code or platform allocation is involved. */
static waste_exec_engine *process_create_resources(uint32_t pages, uint32_t table_entries, exec_error *error) {
    waste_exec_engine *engine = calloc(1, sizeof(*engine));
    if (!engine) {
        exec_fail(error, EXEC_ERROR_TRAP, "cannot allocate initial process resources");
        return NULL;
    }
    engine->static_ref_count = malloc(sizeof(*engine->static_ref_count));
    if (engine->static_ref_count) *engine->static_ref_count = 1;
    engine->exports = calloc(4, sizeof(*engine->exports));
    if (!engine->static_ref_count || !engine->exports) goto failed;
    engine->memory_count = 1;
    engine->memory = engine->memories[0] = &engine->owned_memories[0];
    engine->owns_memories[0] = 1;
    if (wasm_instance_allocate_memory(engine->memory,
            pages, EXEC_MEM32_MAX_PAGES, 0, error) != EXEC_OK)
        goto failed;
    engine->table_count = 1;
    engine->tables[0] = &engine->owned_tables[0];
    if (wasm_instance_allocate_table(engine->tables[0], engine, WASM_VALTYPE_FUNCREF,
            table_entries, UINT32_MAX, 0, error) != EXEC_OK)
        goto failed;
    engine->global_count = 1;
    engine->globals[0] = &engine->owned_globals[0];
    wasm_value stack = {.type = WASM_VALTYPE_I32,
        .i32 = (int32_t)(pages * EXEC_PAGE_SIZE)};
    wasm_instance_initialize_global(engine->globals[0], engine,
                                    WASM_VALTYPE_I32, 1, &stack);
    const char *names[] = {"memory", "__indirect_function_table", "table", "__stack_pointer"};
    const uint8_t kinds[] = {2, 1, 1, 3};
    engine->export_count = 4;
    for (unsigned i = 0; i < 4; i++) {
        snprintf(engine->exports[i].name, sizeof(engine->exports[i].name), "%s", names[i]);
        engine->exports[i].kind = kinds[i];
    }
    return engine;
failed:
    exec_free(engine);
    if (!error->message[0]) exec_fail(error, EXEC_ERROR_TRAP, "cannot allocate process resources");
    return NULL;
}

static int process_copy_vector(char **destination, const char *const *source,
                              uint32_t count, size_t *total) {
    for (uint32_t i = 0; i < count; i++) {
        if (!source[i]) return 0;
        size_t length = 0;
        while (length < NATIVE_EXEC_BYTES_MAX - *total && source[i][length]) length++;
        if (length == NATIVE_EXEC_BYTES_MAX - *total) return 0;
        destination[i] = malloc(length + 1);
        if (!destination[i]) return 0;
        memcpy(destination[i], source[i], length + 1);
        *total += length + 1;
    }
    return 1;
}

static void process_reset_initial_store(native_store *store, posix_kernel *original) {
    /* The fresh-store precondition makes every instantiated provider and call
     * binding below transaction-owned. Release them before restoring the root. */
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++) {
        if (!store->processes[i].used) continue;
        native_process_capsule_destroy(&store->processes[i].capsule);
        posix_kernel_destroy(store->processes[i].kernel);
        if (i) memset(&store->processes[i], 0, sizeof(store->processes[i]));
    }
    for (int i = store->module_count; i > 0; i--) exec_free(store->modules[i - 1].engine);
    for (int i = store->orphan_count; i > 0; i--) exec_free(store->orphan_engines[i - 1]);
    free(store->modules);
    free(store->orphan_engines);
    store->modules = NULL;
    store->orphan_engines = NULL;
    store->module_count = store->module_capacity = store->orphan_count = store->orphan_capacity = 0;
    while (store->call_blocks) {
        native_call_block *block = store->call_blocks;
        store->call_blocks = block->next;
        free(block->calls);
        free(block->got_globals);
        free(block);
    }
    memset(&store->library_load_ctx, 0, sizeof(store->library_load_ctx));
    native_exec_request_destroy(&store->exec_request);
    for (uint32_t i = 0; i < store->shared_file_page_count; i++) {
        exec_memory_page_release(store->shared_file_pages[i].page);
        posix_kernel_file_release(store->shared_file_pages[i].file_object);
    }
    free(store->shared_file_pages);
    store->shared_file_pages = NULL;
    store->shared_file_page_count = store->shared_file_page_capacity = 0;
    free(store->host_io.data);
    memset(&store->host_io, 0, sizeof(store->host_io));
    store->kernel = store->processes[0].kernel = original;
    store->process_count = 1;
    store->active_pid = 1;
    store->next_pid = 2;
    store->fork_child_resume = store->fork_parent_resume = 0;
    store->fork_child_pid = store->fork_parent_pid = 0;
    store->last_wait_pid = store->last_wait_status = 0;
    store->processes[0].zombie = store->processes[0].exit_status = 0;
    native_process_capsule_init(&store->processes[0].capsule);
}

/* Standalone WAST production context: no executable or authored launcher.
 * The resource provider and DSO belong to the fresh store, just as at exec. */
exec_status native_process_driver_prepare_runtime(native_process_driver *driver,
    native_store *store, uint32_t pages, uint32_t table_entries, exec_error *error) {
    native_process_capsule *capsule = store ? native_store_active_capsule(store) : NULL;
    if (!driver || !driver->initialized || !capsule || !store->kernel ||
        store->module_count || store->orphan_count || store->process_count != 1 ||
        capsule->engine || capsule->image || capsule->loaded_library_count ||
        pages > GUEST_TEST_MAX_MEMORY_BYTES / EXEC_PAGE_SIZE ||
        table_entries > GUEST_TEST_MAX_MEMORY_BYTES / sizeof(exec_table_element))
        return exec_fail(error, EXEC_ERROR_FORMAT, "runtime context requires a fresh bounded store");
    int original_terminal = store->kernel_terminal;
    posix_kernel *original = store->kernel;
    posix_kernel *candidate = posix_kernel_clone(original);
    if (!candidate) return exec_fail(error, EXEC_ERROR_TRAP, "cannot clone runtime context kernel");
    store->kernel = store->processes[0].kernel = candidate;
    if (!store->kernel_terminal) {
        if (posix_kernel_attach_terminal(candidate)) {
            process_reset_initial_store(store, original);
            return exec_fail(error, EXEC_ERROR_TRAP, "cannot attach runtime terminal descriptors");
        }
        store->kernel_terminal = 1;
    }
    waste_exec_engine *resources = process_create_resources(
        pages > PROCESS_INITIAL_MEMORY_PAGES ? pages : PROCESS_INITIAL_MEMORY_PAGES,
        table_entries > PROCESS_INITIAL_TABLE_ENTRIES ? table_entries : PROCESS_INITIAL_TABLE_ENTRIES, error);
    exec_status status = resources ? EXEC_OK : EXEC_ERROR_TRAP;
    if (resources && !native_store_add(store, resources, NULL, NULL)) {
        exec_free(resources);
        resources = NULL;
        status = exec_fail(error, EXEC_ERROR_TRAP, "cannot retain runtime resources");
    }
    if (status == EXEC_OK) {
        snprintf(store->modules[0].registered, sizeof(store->modules[0].registered), "waste-runtime");
        capsule->engine = resources;
    }
    if (status == EXEC_OK && native_process_capsule_bind_memory(capsule))
        status = exec_fail(error, EXEC_ERROR_TRAP, "cannot bind runtime context memory");
    if (status == EXEC_OK && native_store_load_library(store, "/usr/lib/libc.so.wasm", error))
        status = error->status == EXEC_OK ? EXEC_ERROR_FORMAT : error->status;
    native_process_image image = {0};
    native_exec_request request = {0};
    image.engine = resources;
    image.pid = 1;
    snprintf(image.cwd, sizeof(image.cwd), "/root");
    if (status == EXEC_OK)
        status = native_process_image_startup_block(&image, &request, store, error);
    if (status == EXEC_OK) status = process_initialize_image(driver, store, &image, error);
    if (status == EXEC_OK) {
        posix_kernel_destroy(original);
        memset(error, 0, sizeof(*error));
        return EXEC_OK;
    }
    process_reset_initial_store(store, original);
    store->kernel_terminal = original_terminal;
    return status;
}

exec_status native_process_driver_start(native_process_driver *driver,
    native_store *store, const char *path, const char *const *argv,
    uint32_t argc, const char *const *envp, uint32_t envc, exec_error *error) {
    return native_process_driver_start_with_options(driver, store, path, argv,
        argc, envp, envc, NULL, error);
}

exec_status native_process_driver_start_with_options(native_process_driver *driver,
    native_store *store, const char *path, const char *const *argv,
    uint32_t argc, const char *const *envp, uint32_t envc,
    const native_process_start_options *options, exec_error *error) {
    if (!driver || !driver->initialized || !store || !error || !path ||
        argc >= NATIVE_EXEC_ARG_MAX || envc >= NATIVE_EXEC_ENV_MAX ||
        (argc && !argv) || (envc && !envp))
        return exec_fail(error, EXEC_ERROR_TRAP, "invalid initial process request");
    native_process_capsule *capsule = native_store_active_capsule(store);
    if (!store->kernel || store->active_pid != 1 || store->process_count != 1 ||
        !capsule || capsule->engine || capsule->image || store->module_count ||
        store->orphan_count || store->call_blocks || store->exec_request.active ||
        store->shared_file_page_count || store->host_io.kind != NATIVE_HOST_IO_NONE ||
        capsule->file_mapping_count || capsule->handler.kind != NATIVE_PROCESS_HANDLER_NONE ||
        capsule->state != NATIVE_PROCESS_RUNNABLE || store->processes[0].pid != 1 ||
        capsule->pending_transition != NATIVE_PROCESS_TRANSITION_NONE ||
        driver->active_engine || driver->invocation_pid)
        return exec_fail(error, EXEC_ERROR_TRAP, "initial startup requires a fresh process store");
    size_t path_length = 0;
    while (path_length < NATIVE_EXEC_PATH_MAX && path[path_length]) path_length++;
    if (!path_length || path_length == NATIVE_EXEC_PATH_MAX)
        return exec_fail(error, EXEC_ERROR_FORMAT, "invalid initial executable path");
    native_exec_request request;
    native_exec_request_init(&request);
    memcpy(request.path, path, path_length + 1);
    request.argc = argc;
    request.envc = envc;
    request.pid = 1;
    request.active = 1;
    if (options) {
        if ((unsigned)options->format > NATIVE_EXEC_FORMAT_WAT ||
            (options->entry && (!options->entry[0] ||
                strlen(options->entry) >= sizeof(request.entry))))
            return exec_fail(error, EXEC_ERROR_FORMAT, "invalid initial process options");
        request.format = (uint8_t)options->format;
        request.readable_input = options->readable_input != 0;
        if (options->entry)
            snprintf(request.entry, sizeof(request.entry), "%s", options->entry);
    }
    size_t total = 0;
    if (!process_copy_vector(request.argv, argv, argc, &total) ||
        !process_copy_vector(request.envp, envp, envc, &total)) {
        native_exec_request_destroy(&request);
        return exec_fail(error, EXEC_ERROR_TRAP, "cannot copy initial process arguments");
    }
    /* Constructors and close-on-exec operate on a candidate kernel. Retain the
     * caller's mounted kernel and descriptors until startup has fully passed. */
    posix_kernel *original = store->kernel;
    posix_kernel *candidate = posix_kernel_clone(original);
    if (!candidate) {
        native_exec_request_destroy(&request);
        return exec_fail(error, EXEC_ERROR_TRAP, "cannot clone initial process kernel");
    }
    store->kernel = store->processes[0].kernel = candidate;
    native_process_image *image = NULL;
    waste_exec_engine *resources = process_create_resources(PROCESS_INITIAL_MEMORY_PAGES, PROCESS_INITIAL_TABLE_ENTRIES, error);
    exec_status status = resources ? EXEC_OK : EXEC_ERROR_TRAP;
    if (resources && !native_store_add(store, resources, NULL, NULL)) {
        exec_free(resources);
        resources = NULL;
        status = exec_fail(error, EXEC_ERROR_TRAP, "cannot retain process resources");
    }
    if (status == EXEC_OK) {
        snprintf(store->modules[0].registered, sizeof(store->modules[0].registered),
                 "%s", "waste-runtime");
        capsule->engine = resources;
        if (native_process_capsule_bind_memory(capsule) ||
            native_store_prepare_process_exec(store, &request))
            status = exec_fail(error, EXEC_ERROR_TRAP, "cannot prepare initial process");
    }
    if (status == EXEC_OK)
        status = native_store_instantiate_executable(store, &request, &image, error);
    if (status == EXEC_OK && native_store_commit_process_image(store, image))
        status = exec_fail(error, EXEC_ERROR_TRAP, "cannot commit initial process image");
    if (status == EXEC_OK) status = process_initialize_image(driver, store, image, error);
    native_exec_request_destroy(&request);
    if (status == EXEC_YIELD)
        status = exec_fail(error, EXEC_ERROR_UNSUPPORTED, "initial process startup cannot yield");
    if (status == EXEC_OK) {
        driver->image_active = 1;
        driver->active_engine = image->engine;
        driver->active_func_idx = image->entry_func;
        process_driver_select(driver, store, image->engine, image->entry_func, NULL, 0);
        posix_kernel_destroy(original);
        return EXEC_OK;
    }
    if (image && capsule->image != image) native_process_image_release(image);
    process_reset_initial_store(store, original);
    return status;
}

/* Drive an invocation through fork/exec and scheduling transitions. Fork makes
 * two independent continuations runnable; blocked imports and opcode time
 * slices select peers. Publish READ/SELECT only when all processes are blocked.
 * The existing single host-request channel remains serialized. */
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
    native_process_capsule *capsule =
        native_store_active_capsule(store);
    if (!driver->invocation_pid) driver->invocation_pid = native_store_getpid(store);
    driver->active_engine = active_engine;
    driver->active_func_idx = active_func_idx;
    driver->active_arg_count = arg_count;
    for (int i = 0; i < arg_count && i < WAST_MAX_ARGS; i++)
        driver->active_args[i] = args[i];
    if (capsule && capsule->handler.kind == NATIVE_PROCESS_HANDLER_NONE &&
        capsule->wait_reason == EXEC_YIELD_NONE) {
        capsule->engine = active_engine;
        capsule->root_func_idx = active_func_idx;
        capsule->root_arg_count = arg_count;
        for (int i = 0; i < arg_count && i < WAST_MAX_ARGS; i++)
            capsule->root_args[i] = args[i];
        capsule->state = NATIVE_PROCESS_RUNNABLE;
        (void)native_process_capsule_bind_memory(capsule);
    }
    if (capsule && capsule->wait_reason != EXEC_YIELD_NONE) {
        /* A legacy host READ has no kernel wait record. Retry it after the
         * adapter's explicit resume; kernel-owned waits are readiness driven. */
        if (capsule->wait_reason == EXEC_YIELD_READ && !store->kernel->wait.active)
            capsule->wait_reason = EXEC_YIELD_NONE;
        int scheduled = process_schedule(driver, store, 1, error);
        if (scheduled < 0) return error->status;
        if (!scheduled) return EXEC_YIELD;
        capsule = native_store_active_capsule(store);
        active_engine = capsule->engine;
        active_func_idx = capsule->root_func_idx;
        args = capsule->root_args;
        arg_count = capsule->root_arg_count;
        image_active = capsule->is_application;
    }
    exec_status status = EXEC_OK;
    for (;;) {
        exec_status stop = exec_execution_check(&store->execution_control, error);
        if (stop != EXEC_OK && stop != EXEC_ERROR_EXIT) return stop;
        status = stop;
        capsule = native_store_active_capsule(store);
        if (status != EXEC_ERROR_EXIT && capsule && capsule->handler.kind != NATIVE_PROCESS_HANDLER_NONE) {
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
                goto process_yield;
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
                if ((error->signal ?
                    native_store_complete_process_handler_signal(
                        store, handler_status, error->signal) :
                    native_store_complete_process_handler(
                        store, handler_status, handler_exit_code)) != 0)
                    return exec_fail(error, EXEC_ERROR_TRAP,
                                     "failed to complete WAST handler");
                status = EXEC_ERROR_EXIT;
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
            if (active_engine == driver->parent_engine)
                process_driver_trace(driver, "parent-invoke");
            status = exec_invoke(active_engine, active_func_idx,
                                 args, arg_count, results, result_count, error);
            if (active_engine == driver->parent_engine)
                process_driver_trace(driver, status == EXEC_YIELD ?
                                          "parent-invoke-yield" :
                                          "parent-invoke-return");
            if (active_engine == driver->parent_engine &&
                status != EXEC_OK && status != EXEC_YIELD &&
                status != EXEC_ERROR_EXIT) {
                char invoke_event[256];
                snprintf(invoke_event, sizeof(invoke_event),
                         "parent-invoke-error-s%d-f%llu-%.180s", status,
                         error ? (unsigned long long)error->memory_fault_address : 0,
                         error && error->message[0] ? error->message : "unknown");
                process_driver_trace(driver, invoke_event);
            }
            if (active_engine == driver->parent_engine &&
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
                void *handler_context = driver->handler_create ?
                    driver->handler_create(driver->handler_context) : driver->handler_context;
                if (!handler_context || native_store_commit_process_handler_with_context(
                        store, &store->exec_request, NATIVE_PROCESS_HANDLER_WAST,
                        handler_context, driver->handler_create ? driver->handler_destroy : NULL) != 0) {
                    if (driver->handler_create && handler_context && driver->handler_destroy)
                        driver->handler_destroy(handler_context);
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
                if (driver->handler_reset) driver->handler_reset(handler_context);
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
                /* These mounted interpreter commands are valid entrypoints;
                 * their arguments failed preflight. ENOEXEC would make Bash
                 * reinterpret their empty synthetic nodes as shell scripts. */
                store->exec_request.failure_errno =
                    strcmp(store->exec_request.path, "/bin/wat") == 0 ||
                    strcmp(store->exec_request.path, "/bin/wast") == 0 ?
                        POSIX_EINVAL : POSIX_ENOEXEC;
                native_store_abort_process_exec(store);
                continue;
            }
            capsule = native_store_active_capsule(store);
            int replaces_parent_image = capsule &&
                capsule->image && capsule->image->engine == driver->parent_engine;
            if (native_store_commit_process_image(store, image) != 0) {
                exec_status restored = process_finish_exec_provider_continuations(
                    saved_providers, saved_provider_count, 1, error);
                native_process_image_release(image);
                if (restored != EXEC_OK) return restored;
                native_store_abort_process_exec(store);
                store->exec_request.failure_errno = POSIX_ENOMEM;
                continue;
            }
            if (replaces_parent_image) driver->parent_engine = NULL;
            (void)process_finish_exec_provider_continuations(
                saved_providers, saved_provider_count, 0, error);
            native_exec_request_destroy(&store->exec_request);
            process_discard_exec_provider_continuations(store, image->engine);
            driver->execs++;
            status = process_initialize_image(driver, store, image, error);
            if (status != EXEC_OK) return status;
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
                (void)native_store_terminate_process(store, error->signal);
            else
                (void)native_store_exit_process(store,
                                                error->exit_code);
            child_exit_recorded = 1;
            status = EXEC_ERROR_EXIT;
            image_active = 0;
            driver->image_active = 0;
        }
process_yield:
        process_driver_count_exits(driver, store);
        if (status == EXEC_ERROR_EXIT) {
            if (native_store_getpid(store) == driver->invocation_pid) return status;
            if (!child_exit_recorded)
                (void)native_store_exit_process(store, error->exit_code);
            process_driver_count_exits(driver, store);
        } else if (status == EXEC_YIELD && error->yield_reason != EXEC_YIELD_FORK) {
            capsule = native_store_active_capsule(store);
            if (!capsule) return exec_fail(error, EXEC_ERROR_TRAP, "missing scheduled process");
            if (error->yield_reason == EXEC_YIELD_HOST_IO) {
                process_driver_select(driver, store, active_engine, active_func_idx, args, arg_count);
                driver->selection.wait_reason = error->yield_reason;
                return status;
            }
            capsule->wait_reason = error->yield_reason == EXEC_YIELD_PUMP ?
                EXEC_YIELD_NONE : error->yield_reason;
            capsule->state = error->yield_reason == EXEC_YIELD_PUMP ?
                NATIVE_PROCESS_RUNNABLE : NATIVE_PROCESS_BROWSER_BLOCKED;
        } else if (status != EXEC_YIELD) {
            return status;
        }
        if (status == EXEC_ERROR_EXIT || error->yield_reason != EXEC_YIELD_FORK) {
            int pump = status == EXEC_YIELD && error->yield_reason == EXEC_YIELD_PUMP;
            int scheduled = process_schedule(driver, store, 0, error);
            if (scheduled < 0) return error->status;
            if (!scheduled) return EXEC_YIELD;
            capsule = native_store_active_capsule(store);
            active_engine = capsule->engine;
            active_func_idx = capsule->root_func_idx;
            args = capsule->root_args;
            arg_count = capsule->root_arg_count;
            image_active = capsule->is_application;
            driver->image_active = image_active;
            driver->active_engine = active_engine;
            driver->active_func_idx = active_func_idx;
            driver->active_arg_count = arg_count;
            child_exit_recorded = 0;
            if (pump) {
                error->status = EXEC_YIELD;
                error->yield_reason = EXEC_YIELD_PUMP;
                driver->selection.wait_reason = EXEC_YIELD_PUMP;
                return EXEC_YIELD;
            }
            memset(error, 0, sizeof(*error));
            continue;
        }
        native_process_capsule *parent_capsule = native_store_active_capsule(store);
        if (!parent_capsule)
            return exec_fail(error, EXEC_ERROR_TRAP, "missing parent process capsule");
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
        if (parent_pid == driver->invocation_pid) driver->parent_engine = active_engine;
        process_driver_engine_trace(driver, "parent-at-fork", active_engine);
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
        if (native_store_clone_process_graph(store, parent_pid,
                                             child_pid) != 0)
            return exec_fail(error, EXEC_ERROR_TRAP,
                             "failed to clone linked process graph");
        if (process_restore_fork_parent(parent_capsule, child_pid, error) != EXEC_OK)
            return error->status;
        capsule = native_store_active_capsule(store);
        if (capsule) {
            capsule->pending_result = 0;
            capsule->pending_result_valid = 1;
        }
        driver->forks++;
        capsule = native_store_active_capsule(store);
        if (capsule) {
            /* Fork cloned the evaluator state before selecting the child.
             * Continue with the child-owned engine and descriptor. */
            active_engine = capsule->engine;
            active_func_idx = capsule->root_func_idx;
            args = capsule->root_args;
            arg_count = capsule->root_arg_count;
            capsule->state = NATIVE_PROCESS_RUNNABLE;
            image_active = capsule->is_application;
            driver->image_active = image_active;
            driver->active_engine = active_engine;
            driver->active_func_idx = active_func_idx;
            driver->active_arg_count = arg_count;
        }
    }
}
