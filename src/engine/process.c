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
    if (!process || !image) return -POSIX_EINVAL;
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
