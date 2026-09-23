#include "store.h"

#include <stdlib.h>
#include <string.h>

extern void native_process_image_release(native_process_image *image)
    __attribute__((weak));
extern void native_process_image_retain(native_process_image *image)
    __attribute__((weak));
extern void exec_continuation_destroy(exec_continuation *continuation)
    __attribute__((weak));
extern exec_status exec_clone_engine(const waste_exec_engine *source,
                                     waste_exec_engine **clone_out,
                                     exec_error *error)
    __attribute__((weak));
extern exec_status exec_clone_engine_bind(waste_exec_engine *clone,
                                          const exec_clone_binding *bindings,
                                          uint32_t binding_count,
                                          exec_error *error)
    __attribute__((weak));
extern void exec_free(waste_exec_engine *engine) __attribute__((weak));
extern void native_store_checkpoint_destroy(native_store_checkpoint *checkpoint)
    __attribute__((weak));

static void release_image(native_process_image *image) {
    if (image && native_process_image_release)
        native_process_image_release(image);
}

void native_process_capsule_clear_handler(native_process_capsule *capsule) {
    if (!capsule) return;
    free(capsule->handler.source);
    if (capsule->handler.destroy_context && capsule->handler.context)
        capsule->handler.destroy_context(capsule->handler.context);
    memset(&capsule->handler, 0, sizeof(capsule->handler));
    capsule->handler.status = EXEC_OK;
}

static native_process *active_process(native_store *store);
static native_process *find_process(native_store *store, int pid);

int native_store_complete_process_handler(native_store *store,
                                          exec_status status, int exit_code) {
    native_process *process = active_process(store);
    native_process_capsule *capsule;
    if (!process || process->pid == 1 || process->zombie ||
        exit_code < 0 || exit_code > 255 ||
        process->capsule.pending_result_valid)
        return -POSIX_EINVAL;
    capsule = &process->capsule;
    if (capsule->handler.kind == NATIVE_PROCESS_HANDLER_NONE)
        return -POSIX_EINVAL;
    if (capsule->state != NATIVE_PROCESS_RUNNABLE)
        return -POSIX_EBUSY;
    capsule->handler.status = status;
    capsule->handler.exit_code = exit_code;
    capsule->pending_error = (int)status;
    capsule->pending_result = exit_code;
    capsule->pending_result_valid = 1;
    native_process_capsule_clear_handler(capsule);
    return native_store_exit_process(store, exit_code);
}

int native_store_complete_process_handler_default(native_store *store,
                                                  exec_status status) {
    native_process *process = active_process(store);
    if (!process || process->capsule.handler.kind == NATIVE_PROCESS_HANDLER_NONE)
        return -POSIX_EINVAL;
    return native_store_complete_process_handler(
        store, status,
        native_process_handler_default_exit_code(
            status, process->capsule.handler.exit_code));
}

int native_store_complete_process_handler_result_and_wake(
        native_store *store) {
    native_process *process = active_process(store);
    exec_status status;
    int exit_code;
    if (!process || process->capsule.handler.kind == NATIVE_PROCESS_HANDLER_NONE)
        return -POSIX_EINVAL;
    status = process->capsule.handler.status;
    exit_code = native_process_handler_default_exit_code(
        status, process->capsule.handler.exit_code);
    return native_store_complete_process_handler_and_wake(
        store, status, exit_code);
}

int native_store_complete_process_handler_and_wake(
        native_store *store, exec_status status, int exit_code) {
    native_process *child = active_process(store);
    native_process *parent;
    int child_pid;
    if (!child || child->pid == 1 || child->ppid <= 0)
        return -POSIX_EINVAL;
    parent = find_process(store, child->ppid);
    if (!parent || parent->zombie || parent->capsule.pending_result_valid ||
        parent->capsule.pending_transition != NATIVE_PROCESS_TRANSITION_NONE)
        return -POSIX_EBUSY;
    child_pid = child->pid;
    if (native_store_complete_process_handler(store, status, exit_code) != 0)
        return -POSIX_EINVAL;
    return native_store_wake_process(store, parent->pid, child_pid);
}

int native_store_complete_process_handler_and_wake_default(
        native_store *store, exec_status status) {
    native_process *process = active_process(store);
    if (!process || process->capsule.handler.kind == NATIVE_PROCESS_HANDLER_NONE)
        return -POSIX_EINVAL;
    return native_store_complete_process_handler_and_wake(
        store, status,
        native_process_handler_default_exit_code(
            status, process->capsule.handler.exit_code));
}

int native_process_handler_default_exit_code(exec_status status,
                                             int explicit_exit_code) {
    if (status == EXEC_OK) return 0;
    if (status == EXEC_ERROR_EXIT) return explicit_exit_code;
    if (status == EXEC_ERROR_FORMAT || status == EXEC_ERROR_UNSUPPORTED ||
        status == EXEC_ERROR_NOT_FOUND)
        return 126;
    return 127;
}

int native_process_capsule_install_handler(
        native_process_capsule *capsule, native_process_handler_kind kind,
        uint8_t *source, size_t source_size, void *context,
        native_process_handler_context_destroy destroy_context) {
    if (!capsule || kind != NATIVE_PROCESS_HANDLER_WAST || !source ||
        source_size == 0 || source_size > NATIVE_EXEC_BYTES_MAX ||
        capsule->handler.kind != NATIVE_PROCESS_HANDLER_NONE)
        return -POSIX_EINVAL;
    capsule->handler.kind = kind;
    capsule->handler.source = source;
    capsule->handler.source_size = source_size;
    capsule->handler.stream_offset = 0;
    capsule->handler.stream_line = 1;
    capsule->handler.context = context;
    capsule->handler.destroy_context = destroy_context;
    capsule->handler.status = EXEC_OK;
    capsule->handler.exit_code = 0;
    return 0;
}

int native_process_capsule_attach_handler_context(
        native_process_capsule *capsule, void *context,
        native_process_handler_context_destroy destroy_context) {
    if (!capsule || capsule->handler.kind == NATIVE_PROCESS_HANDLER_NONE ||
        !context || capsule->handler.context)
        return -POSIX_EINVAL;
    capsule->handler.context = context;
    capsule->handler.destroy_context = destroy_context;
    return 0;
}

int native_process_capsule_handler_cursor(
        const native_process_capsule *capsule, const uint8_t **source_out,
        size_t *size_out, size_t *offset_out, unsigned *line_out) {
    if (!capsule || capsule->handler.kind == NATIVE_PROCESS_HANDLER_NONE ||
        !source_out || !size_out || !offset_out || !line_out)
        return -POSIX_EINVAL;
    *source_out = capsule->handler.source;
    *size_out = capsule->handler.source_size;
    *offset_out = capsule->handler.stream_offset;
    *line_out = capsule->handler.stream_line;
    return 0;
}

int native_process_capsule_advance_handler(native_process_capsule *capsule,
                                           size_t offset, unsigned line) {
    if (!capsule || capsule->handler.kind == NATIVE_PROCESS_HANDLER_NONE ||
        offset < capsule->handler.stream_offset ||
        offset > capsule->handler.source_size || line == 0 ||
        line < capsule->handler.stream_line)
        return -POSIX_EINVAL;
    capsule->handler.stream_offset = offset;
    capsule->handler.stream_line = line;
    return 0;
}

int native_process_capsule_suspend_handler(native_process_capsule *capsule,
                                           native_process_run_state state) {
    if (!capsule || capsule->handler.kind == NATIVE_PROCESS_HANDLER_NONE ||
        capsule->state != NATIVE_PROCESS_RUNNABLE ||
        (state != NATIVE_PROCESS_BROWSER_BLOCKED &&
         state != NATIVE_PROCESS_WAIT_BLOCKED))
        return -POSIX_EINVAL;
    capsule->state = state;
    return 0;
}

int native_process_capsule_resume_handler(native_process_capsule *capsule) {
    if (!capsule || capsule->handler.kind == NATIVE_PROCESS_HANDLER_NONE ||
        ((capsule->state != NATIVE_PROCESS_BROWSER_BLOCKED &&
          capsule->state != NATIVE_PROCESS_WAIT_BLOCKED) &&
         capsule->handler.wait_reason == EXEC_YIELD_NONE))
        return -POSIX_EINVAL;
    capsule->state = NATIVE_PROCESS_RUNNABLE;
    return 0;
}

int native_process_capsule_set_handler_exit_code(native_process_capsule *capsule,
                                                 int exit_code) {
    if (!capsule || capsule->handler.kind == NATIVE_PROCESS_HANDLER_NONE ||
        exit_code < 0 || exit_code > 255)
        return -POSIX_EINVAL;
    capsule->handler.exit_code = exit_code;
    return 0;
}

exec_status native_process_capsule_run_handler_step(
        native_process_capsule *capsule, native_process_handler_step callback,
        void *context) {
    size_t next_offset;
    unsigned next_line;
    exec_status status;
    if (!capsule || capsule->handler.kind == NATIVE_PROCESS_HANDLER_NONE ||
        !callback || capsule->state != NATIVE_PROCESS_RUNNABLE)
        return EXEC_ERROR_FORMAT;
    next_offset = capsule->handler.stream_offset;
    next_line = capsule->handler.stream_line;
    status = callback(capsule->handler.source, capsule->handler.source_size,
                      capsule->handler.stream_offset,
                      capsule->handler.stream_line, &next_offset, &next_line,
                      context);
    if (status != EXEC_OK && status != EXEC_YIELD) {
        capsule->handler.status = status;
        return status;
    }
    if (native_process_capsule_advance_handler(
            capsule, next_offset, next_line) != 0) {
        capsule->handler.status = EXEC_ERROR_FORMAT;
        return EXEC_ERROR_FORMAT;
    }
    capsule->handler.status = status;
    return status;
}

exec_status native_process_capsule_run_attached_handler_step(
        native_process_capsule *capsule, native_process_handler_step callback) {
    if (!capsule || !capsule->handler.context)
        return EXEC_ERROR_FORMAT;
    return native_process_capsule_run_handler_step(
        capsule, callback, capsule->handler.context);
}

exec_status native_store_run_process_handler_step(
        native_store *store, native_process_handler_step callback) {
    native_process_capsule *capsule;
    if (!store) return EXEC_ERROR_FORMAT;
    capsule = native_store_active_capsule(store);
    if (!capsule) return EXEC_ERROR_FORMAT;
    return native_process_capsule_run_attached_handler_step(capsule, callback);
}

int native_store_process_handler_cursor(
        const native_store *store, const uint8_t **source_out,
        size_t *size_out, size_t *offset_out, unsigned *line_out) {
    const native_process_capsule *capsule;
    if (!store) return -POSIX_EINVAL;
    capsule = native_store_active_capsule((native_store *)store);
    if (!capsule) return -POSIX_EINVAL;
    return native_process_capsule_handler_cursor(
        capsule, source_out, size_out, offset_out, line_out);
}

int native_store_process_handler_context(const native_store *store,
                                         void **context_out) {
    const native_process_capsule *capsule;
    if (!store || !context_out) return -POSIX_EINVAL;
    capsule = native_store_active_capsule((native_store *)store);
    if (!capsule || capsule->handler.kind == NATIVE_PROCESS_HANDLER_NONE)
        return -POSIX_EINVAL;
    *context_out = capsule->handler.context;
    return 0;
}

int native_store_process_handler_result(const native_store *store,
                                        exec_status *status_out,
                                        int *exit_code_out) {
    const native_process_capsule *capsule;
    if (!store || !status_out || !exit_code_out) return -POSIX_EINVAL;
    capsule = native_store_active_capsule((native_store *)store);
    if (!capsule || capsule->handler.kind == NATIVE_PROCESS_HANDLER_NONE)
        return -POSIX_EINVAL;
    *status_out = capsule->handler.status;
    *exit_code_out = capsule->handler.exit_code;
    return 0;
}

int native_store_advance_process_handler(native_store *store,
                                          size_t offset, unsigned line) {
    native_process_capsule *capsule;
    if (!store) return -POSIX_EINVAL;
    capsule = native_store_active_capsule(store);
    if (!capsule) return -POSIX_EINVAL;
    return native_process_capsule_advance_handler(capsule, offset, line);
}

int native_store_suspend_process_handler(native_store *store,
                                         native_process_run_state state) {
    native_process_capsule *capsule;
    int result;
    if (!store) return -POSIX_EINVAL;
    capsule = native_store_active_capsule(store);
    if (!capsule) return -POSIX_EINVAL;
    result = native_process_capsule_suspend_handler(capsule, state);
    if (result == 0) capsule->handler.wait_reason = EXEC_YIELD_NONE;
    return result;
}

int native_store_suspend_process_handler_for_yield(
        native_store *store, exec_yield_reason reason) {
    native_process_capsule *capsule;
    if (reason != EXEC_YIELD_READ && reason != EXEC_YIELD_SELECT)
        return -POSIX_EINVAL;
    if (native_store_suspend_process_handler(
            store, NATIVE_PROCESS_BROWSER_BLOCKED) != 0)
        return -POSIX_EINVAL;
    capsule = native_store_active_capsule(store);
    capsule->handler.wait_reason = reason;
    return 0;
}

int native_store_process_handler_wait_reason(
        const native_store *store, exec_yield_reason *reason_out) {
    const native_process_capsule *capsule;
    if (!store || !reason_out) return -POSIX_EINVAL;
    capsule = native_store_active_capsule((native_store *)store);
    if (!capsule || capsule->handler.kind == NATIVE_PROCESS_HANDLER_NONE)
        return -POSIX_EINVAL;
    *reason_out = capsule->handler.wait_reason;
    return 0;
}

int native_store_resume_process_handler(native_store *store) {
    native_process_capsule *capsule;
    if (!store) return -POSIX_EINVAL;
    capsule = native_store_active_capsule(store);
    if (!capsule) return -POSIX_EINVAL;
    if (native_process_capsule_resume_handler(capsule) != 0)
        return -POSIX_EINVAL;
    capsule->handler.wait_reason = EXEC_YIELD_NONE;
    return 0;
}

static native_process *find_process(native_store *store, int pid) {
    if (!store || pid <= 0) return NULL;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++)
        if (store->processes[i].used && store->processes[i].pid == pid)
            return &store->processes[i];
    return NULL;
}

static const native_process *find_process_const(const native_store *store, int pid) {
    return find_process((native_store *)store, pid);
}

static native_process *active_process(native_store *store) {
    return find_process(store, store ? store->active_pid : 0);
}

void native_process_capsule_init(native_process_capsule *capsule) {
    if (!capsule) return;
    memset(capsule, 0, sizeof(*capsule));
    capsule->state = NATIVE_PROCESS_RUNNABLE;
}

int native_process_capsule_select_entry(native_process_capsule *capsule,
                                         waste_exec_engine *engine,
                                         uint32_t func_idx,
                                         const wasm_value *args,
                                         int arg_count) {
    if (!capsule || !engine || arg_count < 0 || arg_count > WAST_MAX_ARGS ||
        (arg_count > 0 && !args) ||
        capsule->handler.kind != NATIVE_PROCESS_HANDLER_NONE ||
        (uint64_t)func_idx >= (uint64_t)engine->import_func_count +
            (uint64_t)engine->func_count)
        return -POSIX_EINVAL;
    capsule->engine = engine;
    capsule->root_func_idx = func_idx;
    capsule->root_arg_count = arg_count;
    memset(capsule->root_args, 0, sizeof(capsule->root_args));
    if (arg_count)
        memcpy(capsule->root_args, args,
               (size_t)arg_count * sizeof(capsule->root_args[0]));
    capsule->state = NATIVE_PROCESS_RUNNABLE;
    return 0;
}

int native_process_capsule_clone(native_process_capsule *destination,
                                 const native_process_capsule *source) {
    exec_error error;
    if (!destination || !source) return 0;
    native_process_capsule_init(destination);
    destination->engine = source->engine;
    if (source->engine) {
        if (!exec_clone_engine) return 0;
        memset(&error, 0, sizeof(error));
        if (exec_clone_engine(source->engine, &destination->engine, &error) !=
            EXEC_OK) {
            if (destination->engine != source->engine && exec_free)
                exec_free(destination->engine);
            destination->engine = NULL;
            return 0;
        }
    }
    destination->root_func_idx = source->root_func_idx;
    destination->root_arg_count = source->root_arg_count;
    for (int i = 0; i < source->root_arg_count && i < WAST_MAX_ARGS; i++)
        destination->root_args[i] = source->root_args[i];
    destination->generation = source->generation;
    destination->state = NATIVE_PROCESS_RUNNABLE;
    destination->pending_result = source->pending_result;
    destination->pending_error = source->pending_error;
    destination->pending_result_valid = source->pending_result_valid;
    if (source->handler.kind != NATIVE_PROCESS_HANDLER_NONE) {
        if (source->handler.context || !source->handler.source ||
            source->handler.source_size == 0) {
            native_process_capsule_destroy(destination);
            return 0;
        }
        destination->handler.source = malloc(source->handler.source_size);
        if (!destination->handler.source) {
            native_process_capsule_destroy(destination);
            return 0;
        }
        memcpy(destination->handler.source, source->handler.source,
               source->handler.source_size);
        destination->handler.kind = source->handler.kind;
        destination->handler.source_size = source->handler.source_size;
        destination->handler.stream_offset = source->handler.stream_offset;
        destination->handler.stream_line = source->handler.stream_line;
        destination->handler.status = source->handler.status;
        destination->handler.exit_code = source->handler.exit_code;
        /* A forked child starts runnable; never carry the parent's external
         * wait marker into that fresh scheduling state. */
        destination->handler.wait_reason = EXEC_YIELD_NONE;
    }
    /* An image is immutable and may be retained by both capsules until the
     * later execution-capsule stage gives each process a private image. */
    /* A cloned engine is the child image for this bounded fork model; do not
     * alias the parent's image descriptor. External executable-image cloning
     * is completed by the exec transition stage. */
    destination->image = NULL;
    return 1;
}

void native_process_capsule_destroy(native_process_capsule *capsule) {
    if (!capsule) return;
    native_process_capsule_clear_handler(capsule);
    if (capsule->continuation) {
        if (exec_continuation_destroy)
            exec_continuation_destroy(capsule->continuation);
        free(capsule->continuation);
    }
    if (capsule->checkpoint) {
        if (native_store_checkpoint_destroy)
            native_store_checkpoint_destroy(capsule->checkpoint);
        free(capsule->checkpoint);
    }
    release_image(capsule->image);
    for (uint32_t i = 0; i < capsule->linked_engine_count; i++)
        if (capsule->linked_engines[i] && exec_free)
            exec_free(capsule->linked_engines[i]);
    free(capsule->linked_engines);
    if (capsule->continuations) {
        for (uint32_t i = 0; i < capsule->continuation_count; i++)
            if (exec_continuation_destroy)
                exec_continuation_destroy(&capsule->continuations[i]);
    }
    free(capsule->continuations);
    free(capsule->continuation_engines);
    memset(capsule, 0, sizeof(*capsule));
}

native_process_capsule *native_store_active_capsule(native_store *store) {
    native_process *process = active_process(store);
    return process ? &process->capsule : NULL;
}

native_process_capsule *native_store_process_capsule(native_store *store,
                                                     int pid) {
    native_process *process = find_process(store, pid);
    return process ? &process->capsule : NULL;
}

int native_store_getpid(const native_store *store) {
    const native_process *process = store ? find_process_const(store, store->active_pid) : NULL;
    return process ? process->pid : -POSIX_EINVAL;
}

int native_store_getppid(const native_store *store) {
    const native_process *process = store ? find_process_const(store, store->active_pid) : NULL;
    return process ? process->ppid : -POSIX_EINVAL;
}

int native_store_set_active_process(native_store *store, int pid) {
    native_process *process = find_process(store, pid);
    if (!process || process->zombie) return -POSIX_ECHILD;
    store->active_pid = pid;
    store->kernel = process->kernel;
    store->kernel_terminal = process->kernel && process->kernel->fds[0].ofd != NULL;
    process->capsule.state = NATIVE_PROCESS_RUNNABLE;
    return 0;
}

int native_store_commit_process_image(native_store *store,
                                      native_process_image *image) {
    native_process *process = active_process(store);
    if (!process || !image || !image->engine ||
        (uint64_t)image->entry_func >=
            (uint64_t)image->engine->import_func_count +
            (uint64_t)image->engine->func_count ||
        process->capsule.handler.kind != NATIVE_PROCESS_HANDLER_NONE ||
        process->capsule.pending_result_valid ||
        process->capsule.pending_transition != NATIVE_PROCESS_TRANSITION_EXEC)
        return -POSIX_EINVAL;
    posix_kernel_close_on_exec(process->kernel);
    release_image(process->capsule.image);
    process->capsule.image = image;
    process->capsule.engine = image->engine;
    process->capsule.root_func_idx = image->entry_func;
    process->capsule.generation++;
    process->capsule.state = NATIVE_PROCESS_RUNNABLE;
    process->capsule.pending_transition = NATIVE_PROCESS_TRANSITION_NONE;
    return 0;
}

int native_store_prepare_process_exec(native_store *store,
                                      const native_exec_request *request) {
    native_process *process = active_process(store);
    if (!process || !request || !request->active || process->zombie)
        return -POSIX_EINVAL;
    if (request->pid > 0 && request->pid != process->pid)
        return -POSIX_EINVAL;
    if (process->capsule.pending_result_valid ||
        process->capsule.pending_transition != NATIVE_PROCESS_TRANSITION_NONE)
        return -POSIX_EBUSY;
    process->capsule.pending_transition = NATIVE_PROCESS_TRANSITION_EXEC;
    return 0;
}

void native_store_abort_process_exec(native_store *store) {
    native_process *process = active_process(store);
    if (!process) return;
    if (process->capsule.pending_transition == NATIVE_PROCESS_TRANSITION_EXEC)
        process->capsule.pending_transition = NATIVE_PROCESS_TRANSITION_NONE;
}

int native_store_wake_process(native_store *store, int pid, int result) {
    native_process *process = find_process(store, pid);
    if (!process || process->zombie) return -POSIX_ECHILD;
    if (process->capsule.pending_transition == NATIVE_PROCESS_TRANSITION_WAKE)
        return -POSIX_EBUSY;
    if (native_store_set_active_process(store, pid) != 0)
        return -POSIX_ECHILD;
    process->capsule.pending_result = result;
    process->capsule.pending_result_valid = 1;
    process->capsule.pending_transition = NATIVE_PROCESS_TRANSITION_WAKE;
    process->capsule.state = NATIVE_PROCESS_RUNNABLE;
    return 0;
}

void native_store_complete_process_wake(native_store *store) {
    native_process *process = active_process(store);
    if (!process) return;
    if (process->capsule.pending_transition == NATIVE_PROCESS_TRANSITION_WAKE)
        process->capsule.pending_transition = NATIVE_PROCESS_TRANSITION_NONE;
}

int native_store_take_process_wake(native_store *store, int *result_out) {
    native_process *process = active_process(store);
    if (!process || !result_out || !process->capsule.pending_result_valid)
        return -POSIX_EINVAL;
    *result_out = process->capsule.pending_result;
    process->capsule.pending_result_valid = 0;
    if (process->capsule.pending_transition == NATIVE_PROCESS_TRANSITION_WAKE)
        process->capsule.pending_transition = NATIVE_PROCESS_TRANSITION_NONE;
    return 0;
}

int native_store_fork_process(native_store *store, int *pid_out) {
    native_process *parent = active_process(store);
    if (!parent || !pid_out) return -POSIX_EFAULT;
    int slot = -1;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++)
        if (!store->processes[i].used) { slot = i; break; }
    if (slot < 0) return -POSIX_ENOMEM;
    posix_kernel *kernel = posix_kernel_clone(parent->kernel);
    if (!kernel) return -POSIX_ENOMEM;
    if (store->next_pid <= 0) store->next_pid = 2;
    int pid = store->next_pid;
    if (store->next_pid == INT32_MAX) store->next_pid = -1;
    else store->next_pid++;
    native_process *child = &store->processes[slot];
    memset(child, 0, sizeof(*child));
    child->used = 1;
    child->pid = pid;
    child->ppid = parent->pid;
    child->kernel = kernel;
    if (!native_process_capsule_clone(&child->capsule, &parent->capsule)) {
        posix_kernel_destroy(kernel);
        memset(child, 0, sizeof(*child));
        return -POSIX_ENOMEM;
    }
    store->process_count++;
    *pid_out = pid;
    return 0;
}

static waste_exec_engine *mapped_engine(const exec_clone_binding *bindings,
                                        uint32_t count,
                                        const waste_exec_engine *source) {
    for (uint32_t i = 0; i < count; i++)
        if (bindings[i].source == source) return bindings[i].clone;
    return NULL;
}

static void rebind_imports(waste_exec_engine *clone,
                           const waste_exec_engine *source,
                           const exec_clone_binding *bindings,
                           uint32_t count) {
    for (uint32_t i = 0; i < clone->import_memory_count; i++) {
        for (uint32_t j = 0; j < count; j++) {
            const waste_exec_engine *provider = bindings[j].source;
            for (uint32_t k = 0; k < provider->memory_count; k++)
                if (source->memories[i] == provider->memories[k]) {
                    clone->memories[i] = bindings[j].clone->memories[k];
                    j = count;
                    break;
                }
        }
    }
    for (uint32_t i = 0; i < clone->import_table_count; i++) {
        for (uint32_t j = 0; j < count; j++) {
            const waste_exec_engine *provider = bindings[j].source;
            for (uint32_t k = 0; k < provider->table_count; k++)
                if (source->tables[i] == provider->tables[k]) {
                    clone->tables[i] = bindings[j].clone->tables[k];
                    j = count;
                    break;
                }
        }
    }
    for (uint32_t i = 0; i < clone->import_global_count; i++) {
        for (uint32_t j = 0; j < count; j++) {
            const waste_exec_engine *provider = bindings[j].source;
            for (uint32_t k = 0; k < provider->global_count; k++)
                if (source->globals[i] == provider->globals[k]) {
                    clone->globals[i] = bindings[j].clone->globals[k];
                    j = count;
                    break;
                }
        }
    }
}

int native_store_clone_process_graph(native_store *store, int parent_pid,
                                     int child_pid) {
    native_process *parent = find_process(store, parent_pid);
    native_process *child = find_process(store, child_pid);
    if (!parent || !child || !parent->capsule.engine || !child->capsule.engine)
        return -POSIX_EINVAL;
    uint32_t count = (uint32_t)store->module_count + 1;
    exec_clone_binding *bindings = calloc(count, sizeof(*bindings));
    waste_exec_engine **owned = NULL;
    uint32_t owned_count = 0;
    if (!bindings) return -POSIX_ENOMEM;
    bindings[0].source = parent->capsule.engine;
    bindings[0].clone = child->capsule.engine;
    uint32_t used = 1;
    for (int i = 0; i < store->module_count; i++) {
        waste_exec_engine *source = store->modules[i].engine;
        if (!source || mapped_engine(bindings, used, source)) continue;
        waste_exec_engine *clone = NULL;
        exec_error error;
        memset(&error, 0, sizeof(error));
        if (!exec_clone_engine || exec_clone_engine(source, &clone, &error) != EXEC_OK) {
            for (uint32_t j = 0; j < owned_count; j++)
                if (exec_free) exec_free(owned[j]);
            free(owned);
            free(bindings);
            return -POSIX_ENOMEM;
        }
        bindings[used++] = (exec_clone_binding){source, clone};
        waste_exec_engine **grown = realloc(owned,
            (size_t)(owned_count + 1) * sizeof(*owned));
        if (!grown) {
            if (exec_free) exec_free(clone);
            for (uint32_t j = 0; j < owned_count; j++)
                if (exec_free) exec_free(owned[j]);
            free(owned);
            free(bindings);
            return -POSIX_ENOMEM;
        }
        owned = grown;
        owned[owned_count++] = clone;
    }
    for (uint32_t i = 0; i < used; i++) {
        exec_error error;
        memset(&error, 0, sizeof(error));
        if (!exec_clone_engine_bind ||
            exec_clone_engine_bind(bindings[i].clone, bindings, used, &error) != EXEC_OK) {
            for (uint32_t j = 0; j < owned_count; j++)
                if (exec_free) exec_free(owned[j]);
            free(owned);
            free(bindings);
            return -POSIX_ENOMEM;
        }
        rebind_imports(bindings[i].clone, bindings[i].source, bindings, used);
    }
    child->capsule.linked_engines = owned;
    child->capsule.linked_engine_count = owned_count;
    free(bindings);
    return 0;
}

int native_store_exit_process(native_store *store, int status) {
    native_process *process = active_process(store);
    if (!process) return -POSIX_EINVAL;
    if (process->pid == 1) return -POSIX_EINVAL;
    if (process->zombie) return 0;
    process->exit_status = (status & 0xff) << 8;
    process->zombie = 1;
    process->capsule.state = NATIVE_PROCESS_EXITED;
    process->capsule.pending_transition = NATIVE_PROCESS_TRANSITION_EXIT;
    return 0;
}

int native_store_signal_process(native_store *store, int pid, int signal) {
    native_process *process = find_process(store, pid);
    posix_signal_disposition disposition;
    if (!process || process->zombie || signal <= 0) return -POSIX_EINVAL;
    if (posix_kernel_signal_raise(process->kernel, signal) != 0) return -POSIX_EINVAL;
    if (posix_kernel_signal_get_disposition(process->kernel, signal,
                                             &disposition) != 0 ||
        disposition != POSIX_SIGNAL_DEFAULT || signal == POSIX_SIGSTOP)
        return 0;
    process->exit_status = ((128 + signal) & 0xff) << 8;
    process->zombie = 1;
    process->capsule.state = NATIVE_PROCESS_EXITED;
    process->capsule.pending_transition = NATIVE_PROCESS_TRANSITION_EXIT;
    return 0;
}

int native_store_wait_process(native_store *store, int pid, int options,
                              int *status_out) {
    native_process *parent = active_process(store);
    if (!parent || !status_out) return -POSIX_EFAULT;
    /* The browser Bash build passes implementation-specific wait flags. The
     * process model only has exited/stopped state, so recognized lifecycle
     * bits are ignored and unknown bits are treated as a non-blocking query. */
    if (pid < -1) return -POSIX_EINVAL;
    if (pid == 0) pid = -1; /* single process group in the browser sandbox */
    native_process *candidate = NULL;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++) {
        native_process *child = &store->processes[i];
        if (!child->used || child->ppid != parent->pid) continue;
        if (pid > 0 && child->pid != pid) continue;
        candidate = child;
        if (child->zombie) break;
    }
    if (!candidate) return -POSIX_ECHILD;
    if (!candidate->zombie) return (options & POSIX_WNOHANG) ? 0 : -POSIX_EAGAIN;
    int child_pid = candidate->pid;
    *status_out = candidate->exit_status;
    store->last_wait_pid = child_pid;
    store->last_wait_status = candidate->exit_status;
    native_process_capsule_destroy(&candidate->capsule);
    posix_kernel_destroy(candidate->kernel);
    memset(candidate, 0, sizeof(*candidate));
    if (store->process_count > 0) store->process_count--;
    return child_pid;
}
