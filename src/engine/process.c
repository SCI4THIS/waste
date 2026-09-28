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

int native_store_shared_file_page(native_store *store,
                                  posix_file_object *file_object,
                                  uint64_t file_offset,
                                  exec_memory_page **page_out) {
    native_shared_file_page *entry;
    uint32_t capacity;
    if (!store || !file_object || !page_out ||
        file_offset % EXEC_PAGE_SIZE != 0)
        return -POSIX_EINVAL;
    for (uint32_t i = 0; i < store->shared_file_page_count; i++) {
        entry = &store->shared_file_pages[i];
        if (entry->file_object == file_object &&
            entry->file_offset == file_offset) {
            *page_out = entry->page;
            return 0;
        }
    }
    if (store->shared_file_page_count == store->shared_file_page_capacity) {
        capacity = store->shared_file_page_capacity ?
            store->shared_file_page_capacity * 2 : 8;
        if (capacity < store->shared_file_page_count)
            return -POSIX_ENOMEM;
        entry = realloc(store->shared_file_pages,
                        (size_t)capacity * sizeof(*entry));
        if (!entry) return -POSIX_ENOMEM;
        store->shared_file_pages = entry;
        store->shared_file_page_capacity = capacity;
    }
    entry = &store->shared_file_pages[store->shared_file_page_count];
    entry->page = calloc(1, sizeof(*entry->page));
    if (!entry->page) return -POSIX_ENOMEM;
    entry->page->bytes = calloc(EXEC_PAGE_SIZE, 1);
    if (!entry->page->bytes) {
        free(entry->page);
        entry->page = NULL;
        return -POSIX_ENOMEM;
    }
    entry->page->refs = 1;
    if (file_offset < file_object->data_capacity) {
        size_t length = file_object->data_capacity - (size_t)file_offset;
        if (length > EXEC_PAGE_SIZE) length = EXEC_PAGE_SIZE;
        memcpy(entry->page->bytes, file_object->data + file_offset, length);
    }
    entry->file_object = file_object;
    entry->file_offset = file_offset;
    posix_file_object_retain(file_object);
    store->shared_file_page_count++;
    *page_out = entry->page;
    return 0;
}

static int native_process_validate_memory_access(
        const exec_memory *memory, uint64_t offset, size_t length,
        uint8_t access, void *context, void *error_pointer) {
    native_process_capsule *capsule = (native_process_capsule *)context;
    exec_error *error = (exec_error *)error_pointer;
    (void)memory;
    if (!capsule || !length) return 0;
    for (uint32_t i = 0; i < capsule->file_mapping_count; i++) {
        native_process_file_mapping *mapping = &capsule->file_mappings[i];
        uint64_t mapping_end = mapping->address + mapping->length;
        uint64_t access_end = offset > UINT64_MAX - length ?
            UINT64_MAX : offset + length;
        if (offset >= mapping_end || access_end <= mapping->address)
            continue;
        if (mapping->file_offset > mapping->file_object->data_capacity ||
            mapping->length > mapping->file_object->data_capacity -
                               mapping->file_offset) {
            (void)exec_fail(error, EXEC_ERROR_TRAP,
                            "access past truncated file mapping");
            if (error) {
                error->signal = POSIX_SIGBUS;
                error->memory_fault = EXEC_MEMORY_FAULT_FILE_TRUNCATED;
                error->memory_fault_address = offset;
                error->memory_fault_length = length;
                error->memory_fault_access = access;
            }
            return EXEC_ERROR_TRAP;
        }
    }
    return 0;
}

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

int native_store_complete_process_handler_signal(native_store *store,
                                                 exec_status status,
                                                 int signal) {
    native_process *process = active_process(store);
    native_process_capsule *capsule;
    if (!process || process->pid == 1 || process->zombie ||
        signal <= 0 || signal > POSIX_SIGNAL_MAX ||
        process->capsule.pending_result_valid)
        return -POSIX_EINVAL;
    capsule = &process->capsule;
    if (capsule->handler.kind == NATIVE_PROCESS_HANDLER_NONE ||
        capsule->state != NATIVE_PROCESS_RUNNABLE)
        return -POSIX_EBUSY;
    capsule->handler.status = status;
    capsule->handler.exit_code = 128 + signal;
    capsule->pending_error = (int)status;
    capsule->pending_result = 128 + signal;
    capsule->pending_result_valid = 1;
    native_process_capsule_clear_handler(capsule);
    process->exit_status = ((128 + signal) & 0xff) << 8;
    process->zombie = 1;
    capsule->state = NATIVE_PROCESS_EXITED;
    capsule->pending_transition = NATIVE_PROCESS_TRANSITION_EXIT;
    return 0;
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

void native_process_capsule_clear_regions(native_process_capsule *capsule) {
    if (!capsule) return;
    free(capsule->regions);
    capsule->regions = NULL;
    capsule->region_count = 0;
    capsule->region_capacity = 0;
}

int native_process_capsule_region_is_reserved(
        const native_process_capsule *capsule, uint64_t first_page,
        uint64_t page_count, native_process_region_kind *kind_out) {
    uint64_t end;
    if (kind_out) *kind_out = 0;
    if (!capsule || !page_count || first_page > UINT64_MAX - page_count)
        return 0;
    end = first_page + page_count;
    for (uint32_t i = 0; i < capsule->region_count; i++) {
        const native_process_region *region = &capsule->regions[i];
        uint64_t region_end = region->first_page + region->page_count;
        if (first_page < region_end && region->first_page < end) {
            if (kind_out) *kind_out = region->kind;
            return 1;
        }
    }
    return 0;
}

int native_process_capsule_reserve_region(
        native_process_capsule *capsule, uint64_t first_page,
        uint64_t page_count, native_process_region_kind kind) {
    native_process_region *regions;
    uint32_t capacity;
    if (!capsule || !page_count || !kind ||
        first_page > UINT64_MAX - page_count ||
        native_process_capsule_region_is_reserved(
            capsule, first_page, page_count, NULL))
        return -POSIX_EINVAL;
    if (capsule->region_count == capsule->region_capacity) {
        capacity = capsule->region_capacity ? capsule->region_capacity * 2 : 8;
        if (capacity < capsule->region_count) return -POSIX_ENOMEM;
        regions = realloc(capsule->regions,
                          (size_t)capacity * sizeof(*regions));
        if (!regions) return -POSIX_ENOMEM;
        capsule->regions = regions;
        capsule->region_capacity = capacity;
    }
    capsule->regions[capsule->region_count++] = (native_process_region){
        first_page, page_count, kind};
    return 0;
}

int native_process_capsule_allocate_region(
        native_process_capsule *capsule, uint64_t page_count,
        native_process_region_kind kind, int top_down, uint64_t *first_page_out) {
    uint64_t limit;
    if (!capsule || !page_count || !kind || !first_page_out)
        return -POSIX_EINVAL;
    limit = capsule->virtual_page_limit;
    if (!limit) limit = EXEC_MEM32_MAX_PAGES;
    if (page_count > limit) return -POSIX_ENOMEM;
    if (top_down) {
        uint64_t candidate = limit - page_count;
        for (;;) {
            if (!native_process_capsule_region_is_reserved(
                    capsule, candidate, page_count, NULL)) {
                int result = native_process_capsule_reserve_region(
                    capsule, candidate, page_count, kind);
                if (result == 0) *first_page_out = candidate;
                return result;
            }
            if (candidate == 0) break;
            candidate--;
        }
    } else {
        for (uint64_t candidate = 0;
             candidate <= limit - page_count; candidate++) {
            if (!native_process_capsule_region_is_reserved(
                    capsule, candidate, page_count, NULL)) {
                int result = native_process_capsule_reserve_region(
                    capsule, candidate, page_count, kind);
                if (result == 0) *first_page_out = candidate;
                return result;
            }
        }
    }
    return -POSIX_ENOMEM;
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
    (void)native_process_capsule_bind_memory(capsule);
    capsule->root_func_idx = func_idx;
    capsule->root_arg_count = arg_count;
    memset(capsule->root_args, 0, sizeof(capsule->root_args));
    if (arg_count)
        memcpy(capsule->root_args, args,
               (size_t)arg_count * sizeof(capsule->root_args[0]));
    capsule->state = NATIVE_PROCESS_RUNNABLE;
    return 0;
}

static int native_process_bind_engine_memory(
        native_process_capsule *capsule, waste_exec_engine *engine) {
    if (!capsule || !engine || !engine->memory)
        return -POSIX_EINVAL;
    engine->memory->access_check =
        native_process_validate_memory_access;
    engine->memory->access_check_context = capsule;
    engine->memory->virtual_max_pages = engine->memory->is_64 ?
        EXEC_MEM64_MAX_PAGES : EXEC_MEM32_MAX_PAGES;
    engine->memory->process_virtual_memory = 1;
    return 0;
}

int native_process_capsule_bind_memory(native_process_capsule *capsule) {
    if (!capsule) return -POSIX_EINVAL;
    return native_process_bind_engine_memory(capsule, capsule->engine);
}

int native_process_capsule_map_pages(native_process_capsule *capsule,
                                     uint64_t first_page, uint64_t page_count,
                                     uint8_t protection, uint8_t flags) {
    exec_error error;
    if (!capsule || !capsule->engine || !capsule->engine->memory)
        return -POSIX_EINVAL;
    memset(&error, 0, sizeof(error));
    return exec_memory_map_pages(capsule->engine->memory, first_page,
                                 page_count, protection, flags, &error) ==
                   EXEC_OK ? 0 : -POSIX_EINVAL;
}

int native_process_capsule_unmap_pages(native_process_capsule *capsule,
                                       uint64_t first_page,
                                       uint64_t page_count) {
    exec_error error;
    if (!capsule || !capsule->engine || !capsule->engine->memory)
        return -POSIX_EINVAL;
    memset(&error, 0, sizeof(error));
    return exec_memory_unmap_pages(capsule->engine->memory, first_page,
                                   page_count, &error) == EXEC_OK ?
               0 : -POSIX_EINVAL;
}

static int native_process_capsule_page_range(uint64_t length,
                                             uint64_t *page_count) {
    if (!length || length > UINT64_MAX - (EXEC_PAGE_SIZE - 1)) return 0;
    *page_count = (length + EXEC_PAGE_SIZE - 1) / EXEC_PAGE_SIZE;
    return 1;
}

static int native_process_capsule_build_mapping_release(
        const native_process_capsule *capsule, uint64_t first_page,
        uint64_t page_count, native_process_region *replacement,
        uint32_t *replacement_count_out) {
    uint32_t replacement_count = 0;
    uint64_t end = first_page + page_count;
    if (!capsule || !page_count || end < first_page || !replacement ||
        !replacement_count_out) return -POSIX_EINVAL;
    for (uint32_t i = 0; i < capsule->region_count; i++) {
        native_process_region old = capsule->regions[i];
        uint64_t old_end = old.first_page + old.page_count;
        uint64_t overlap_start = old.first_page > first_page ?
            old.first_page : first_page;
        uint64_t overlap_end = old_end < end ? old_end : end;
        if (old.kind != NATIVE_PROCESS_REGION_MAPPING ||
            overlap_start >= overlap_end) {
            replacement[replacement_count++] = old;
            continue;
        }
        if (old.first_page < overlap_start)
            replacement[replacement_count++] = (native_process_region){
                old.first_page, overlap_start - old.first_page, old.kind};
        if (overlap_end < old_end)
            replacement[replacement_count++] = (native_process_region){
                overlap_end, old_end - overlap_end, old.kind};
    }
    *replacement_count_out = replacement_count;
    return 0;
}

static void native_process_capsule_remove_last_mapping_region(
        native_process_capsule *capsule, uint64_t first_page,
        uint64_t page_count) {
    native_process_region *region;
    if (!capsule || !capsule->region_count) return;
    region = &capsule->regions[capsule->region_count - 1];
    if (region->kind == NATIVE_PROCESS_REGION_MAPPING &&
        region->first_page == first_page && region->page_count == page_count)
        capsule->region_count--;
}

int native_process_capsule_mmap_range(native_process_capsule *capsule,
                                      uint64_t address, uint64_t length,
                                      uint8_t protection, uint8_t flags,
                                      uint64_t *address_out) {
    exec_memory *memory;
    uint64_t page_count;
    uint64_t first_page = 0;
    uint64_t memory_limit;
    uint64_t linear_limit;
    if (!capsule || !capsule->engine || !capsule->engine->memory ||
        !address_out || !native_process_capsule_page_range(length,
                                                           &page_count))
        return -POSIX_EINVAL;
    memory = capsule->engine->memory;
    memory_limit = memory->virtual_max_pages ? memory->virtual_max_pages :
        (memory->is_64 ? EXEC_MEM64_MAX_PAGES : EXEC_MEM32_MAX_PAGES);
    linear_limit = memory->has_max ? memory->max_pages :
        (memory->is_64 ? EXEC_MEM64_MAX_PAGES : EXEC_MEM32_MAX_PAGES);
    if (address) {
        if (address % EXEC_PAGE_SIZE ||
            address / EXEC_PAGE_SIZE > memory_limit)
            return -POSIX_EINVAL;
        first_page = address / EXEC_PAGE_SIZE;
        if (page_count > memory_limit - first_page)
            return -POSIX_ENOMEM;
        if (native_process_capsule_region_is_reserved(
                capsule, first_page, page_count, NULL))
            return -POSIX_EEXIST;
        if (first_page > memory->pages ||
            page_count > memory->pages - first_page) {
            if (exec_memory_reserve_virtual_pages(
                    memory, first_page + page_count, NULL) != EXEC_OK)
                return -POSIX_ENOMEM;
        }
    } else {
        int found = 0;
        for (uint64_t candidate = 0;
             candidate <= memory_limit - page_count; candidate++) {
            if (native_process_capsule_region_is_reserved(
                    capsule, candidate, page_count, NULL))
                continue;
            int free_range = 1;
            uint64_t checked_end = candidate + page_count;
            uint64_t checked_limit = checked_end < memory->pages ?
                checked_end : memory->pages;
            for (uint64_t page = candidate; page < checked_limit; page++) {
                if (exec_memory_page_is_mapped(memory, page)) {
                    free_range = 0;
                    break;
                }
            }
            if (free_range) {
                first_page = candidate;
                found = 1;
                break;
            }
        }
        if (!found) return -POSIX_ENOMEM;
        if (first_page > memory->pages ||
            page_count > memory->pages - first_page) {
            if (exec_memory_reserve_virtual_pages(
                    memory, first_page + page_count, NULL) != EXEC_OK)
                return -POSIX_ENOMEM;
        }
    }
    if (native_process_capsule_reserve_region(
            capsule, first_page, page_count,
            NATIVE_PROCESS_REGION_MAPPING) != 0)
        return -POSIX_ENOMEM;
    if ((flags & EXEC_MEMORY_MAPPING_FIXED_NOREPLACE) && address) {
        for (uint64_t page = first_page; page < first_page + page_count; page++)
            if (exec_memory_page_is_mapped(memory, page)) {
                native_process_capsule_remove_last_mapping_region(
                    capsule, first_page, page_count);
                return -POSIX_EEXIST;
            }
    }
    if (exec_memory_map_pages(memory, first_page, page_count, protection,
                              flags, NULL) != EXEC_OK) {
        native_process_capsule_remove_last_mapping_region(
            capsule, first_page, page_count);
        return -POSIX_EINVAL;
    }
    if (first_page + page_count > memory->linear_pages &&
        first_page + page_count <= linear_limit &&
        exec_memory_promote_linear_pages(memory, first_page + page_count,
                                         NULL) != EXEC_OK) {
        (void)exec_memory_unmap_pages(memory, first_page, page_count, NULL);
        native_process_capsule_remove_last_mapping_region(
            capsule, first_page, page_count);
        return -POSIX_EINVAL;
    }
    *address_out = first_page * EXEC_PAGE_SIZE;
    return 0;
}

static int native_process_capsule_build_file_mapping_release(
        const native_process_capsule *capsule, uint64_t address,
        uint64_t length, native_process_file_mapping *replacement,
        uint32_t *replacement_count_out) {
    uint32_t count = 0;
    uint64_t end;
    if (!capsule || !length || address % EXEC_PAGE_SIZE ||
        length % EXEC_PAGE_SIZE || address > UINT64_MAX - length ||
        (!replacement && capsule->file_mapping_count) ||
        !replacement_count_out)
        return -POSIX_EINVAL;
    end = address + length;
    for (uint32_t i = 0; i < capsule->file_mapping_count; i++) {
        native_process_file_mapping old = capsule->file_mappings[i];
        uint64_t old_end = old.address + old.length;
        uint64_t overlap_start = old.address > address ? old.address : address;
        uint64_t overlap_end = old_end < end ? old_end : end;
        if (overlap_start >= overlap_end) {
            replacement[count] = old;
            posix_file_object_retain(replacement[count].file_object);
            count++;
            continue;
        }
        if (old.address < overlap_start) {
            replacement[count] = old;
            replacement[count].length = overlap_start - old.address;
            posix_file_object_retain(replacement[count].file_object);
            count++;
        }
        if (overlap_end < old_end) {
            native_process_file_mapping right = old;
            right.address = overlap_end;
            right.length = old_end - overlap_end;
            right.file_offset += overlap_end - old.address;
            replacement[count++] = right;
            posix_file_object_retain(right.file_object);
        }
    }
    *replacement_count_out = count;
    return 0;
}

static void native_process_capsule_discard_file_mapping_release(
        native_process_file_mapping *replacement, uint32_t count) {
    if (!replacement) return;
    for (uint32_t i = 0; i < count; i++)
        posix_kernel_file_release(replacement[i].file_object);
    free(replacement);
}

static void native_process_capsule_commit_file_mapping_release(
        native_process_capsule *capsule,
        native_process_file_mapping *replacement, uint32_t count) {
    for (uint32_t i = 0; i < capsule->file_mapping_count; i++)
        posix_kernel_file_release(capsule->file_mappings[i].file_object);
    free(capsule->file_mappings);
    capsule->file_mappings = replacement;
    capsule->file_mapping_count = count;
    capsule->file_mapping_capacity = count;
}

int native_process_capsule_munmap_range(native_process_capsule *capsule,
                                        uint64_t address, uint64_t length) {
    uint64_t page_count;
    uint64_t first_page;
    uint64_t unmap_length;
    native_process_region_kind region_kind;
    if (!capsule || address % EXEC_PAGE_SIZE ||
        !native_process_capsule_page_range(length, &page_count))
        return -POSIX_EINVAL;
    first_page = address / EXEC_PAGE_SIZE;
    unmap_length = page_count * EXEC_PAGE_SIZE;
    if (native_process_capsule_region_is_reserved(
            capsule, first_page, page_count, &region_kind) &&
        region_kind == NATIVE_PROCESS_REGION_STARTUP)
        return -POSIX_EINVAL;
    if (capsule->image && capsule->image->startup_size) {
        uint64_t end = address + length;
        uint64_t startup_end = (uint64_t)capsule->image->startup_ptr +
                               capsule->image->startup_size;
        if (end < address || startup_end < capsule->image->startup_ptr)
            return -POSIX_EINVAL;
        if (address < startup_end && capsule->image->startup_ptr < end)
            return -POSIX_EINVAL;
    }
    native_process_region *replacement = calloc(
        (size_t)capsule->region_count * 2 + 1, sizeof(*replacement));
    uint32_t replacement_count = 0;
    native_process_file_mapping *file_replacement =
        capsule->file_mapping_count ? calloc(
            (size_t)capsule->file_mapping_count * 2,
            sizeof(*file_replacement)) : NULL;
    uint32_t file_replacement_count = 0;
    if (!replacement) return -POSIX_ENOMEM;
    if (native_process_capsule_build_mapping_release(
            capsule, first_page, page_count, replacement,
            &replacement_count) != 0) {
        free(replacement);
        return -POSIX_EINVAL;
    }
    if (capsule->file_mapping_count && !file_replacement) {
        free(replacement);
        return -POSIX_ENOMEM;
    }
    if (native_process_capsule_build_file_mapping_release(
            capsule, address, unmap_length, file_replacement,
            &file_replacement_count) != 0) {
        free(replacement);
        free(file_replacement);
        return -POSIX_EINVAL;
    }
    if (native_process_capsule_unmap_pages(capsule, first_page, page_count) != 0)
    {
        free(replacement);
        native_process_capsule_discard_file_mapping_release(
            file_replacement, file_replacement_count);
        return -POSIX_EINVAL;
    }
    free(capsule->regions);
    capsule->regions = replacement;
    capsule->region_count = replacement_count;
    capsule->region_capacity = replacement_count;
    native_process_capsule_commit_file_mapping_release(
        capsule, file_replacement, file_replacement_count);
    return 0;
}

int native_process_capsule_mprotect_range(native_process_capsule *capsule,
                                          uint64_t address, uint64_t length,
                                          uint8_t protection) {
    exec_memory *memory;
    uint64_t page_count;
    uint64_t first_page;
    if (!capsule || !capsule->engine || !capsule->engine->memory ||
        address % EXEC_PAGE_SIZE ||
        !native_process_capsule_page_range(length, &page_count))
        return -POSIX_EINVAL;
    memory = capsule->engine->memory;
    first_page = address / EXEC_PAGE_SIZE;
    if (first_page > memory->pages || page_count > memory->pages - first_page)
        return -POSIX_EINVAL;
    for (uint64_t page = first_page; page < first_page + page_count; page++)
        if (!exec_memory_page_is_mapped(memory, page))
            return -POSIX_EINVAL;
    return exec_memory_set_protection(memory, first_page, page_count,
                                      protection, NULL) == EXEC_OK ?
               0 : -POSIX_EINVAL;
}

int native_process_capsule_record_file_mapping(
    native_process_capsule *capsule, uint64_t address, uint64_t length,
    posix_file_object *file_object, uint64_t file_offset, int shared,
    int writable) {
    native_process_file_mapping *grown;
    if (!capsule || !length || address % EXEC_PAGE_SIZE ||
        !file_object ||
        address > UINT64_MAX - length || file_offset > UINT64_MAX - length)
        return -POSIX_EINVAL;
    if (capsule->file_mapping_count == capsule->file_mapping_capacity) {
        uint32_t capacity = capsule->file_mapping_capacity ?
            capsule->file_mapping_capacity * 2 : 4;
        if (capacity < capsule->file_mapping_count) return -POSIX_ENOMEM;
        grown = realloc(capsule->file_mappings,
                        (size_t)capacity * sizeof(*grown));
        if (!grown) return -POSIX_ENOMEM;
        capsule->file_mappings = grown;
        capsule->file_mapping_capacity = capacity;
    }
    capsule->file_mappings[capsule->file_mapping_count++] =
        (native_process_file_mapping){address, length, file_offset,
                                      file_object, (uint8_t)(shared != 0),
                                      (uint8_t)(writable != 0)};
    /* The caller transfers its retained object reference to this record. */
    return 0;
}

int native_process_capsule_forget_file_mapping(
        native_process_capsule *capsule, uint64_t address, uint64_t length) {
    native_process_file_mapping *replacement;
    uint32_t count = 0;
    if (!capsule || !length || address % EXEC_PAGE_SIZE ||
        length % EXEC_PAGE_SIZE || address > UINT64_MAX - length)
        return -POSIX_EINVAL;
    replacement = capsule->file_mapping_count ?
        calloc((size_t)capsule->file_mapping_count * 2,
               sizeof(*replacement)) : NULL;
    if (capsule->file_mapping_count && !replacement) return -POSIX_ENOMEM;
    if (native_process_capsule_build_file_mapping_release(
            capsule, address, length, replacement, &count) != 0) {
        free(replacement);
        return -POSIX_EINVAL;
    }
    native_process_capsule_commit_file_mapping_release(
        capsule, replacement, count);
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
        if (destination->engine->memory) {
            destination->engine->memory->access_check =
                native_process_validate_memory_access;
            destination->engine->memory->access_check_context = destination;
        }
    }
    destination->root_func_idx = source->root_func_idx;
    destination->root_arg_count = source->root_arg_count;
    for (int i = 0; i < source->root_arg_count && i < WAST_MAX_ARGS; i++)
        destination->root_args[i] = source->root_args[i];
    destination->generation = source->generation;
    destination->virtual_page_limit = source->virtual_page_limit;
    destination->state = NATIVE_PROCESS_RUNNABLE;
    destination->pending_result = source->pending_result;
    destination->pending_error = source->pending_error;
    destination->pending_result_valid = source->pending_result_valid;
    if (source->file_mapping_count) {
        destination->file_mappings = malloc(
            (size_t)source->file_mapping_count * sizeof(*destination->file_mappings));
        if (!destination->file_mappings) {
            native_process_capsule_destroy(destination);
            return 0;
        }
        memcpy(destination->file_mappings, source->file_mappings,
               (size_t)source->file_mapping_count *
                   sizeof(*destination->file_mappings));
        for (uint32_t i = 0; i < source->file_mapping_count; i++)
            posix_file_object_retain(destination->file_mappings[i].file_object);
        destination->file_mapping_count = source->file_mapping_count;
        destination->file_mapping_capacity = source->file_mapping_count;
    }
    if (source->region_count) {
        destination->regions = malloc((size_t)source->region_count *
                                      sizeof(*destination->regions));
        if (!destination->regions) {
            native_process_capsule_destroy(destination);
            return 0;
        }
        memcpy(destination->regions, source->regions,
               (size_t)source->region_count * sizeof(*destination->regions));
        destination->region_count = source->region_count;
        destination->region_capacity = source->region_count;
    }
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
    if (capsule->file_mappings)
        for (uint32_t i = 0; i < capsule->file_mapping_count; i++)
            posix_kernel_file_release(capsule->file_mappings[i].file_object);
    free(capsule->file_mappings);
    native_process_capsule_clear_regions(capsule);
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
    if (native_process_bind_engine_memory(&process->capsule, image->engine) != 0)
        return -POSIX_EINVAL;
    native_process_capsule_clear_regions(&process->capsule);
    process->capsule.virtual_page_limit = image->engine->memory &&
        image->engine->memory->is_64 ? EXEC_MEM64_MAX_PAGES :
        EXEC_MEM32_MAX_PAGES;
    if (image->engine->memory && image->engine->memory->linear_pages) {
        uint64_t module_pages = image->engine->memory->linear_pages;
        uint64_t startup_first = module_pages;
        uint64_t startup_last = module_pages;
        uint64_t stack_first = process->capsule.virtual_page_limit - 17;
        if (image->startup_size) {
            uint64_t startup_end = (uint64_t)image->startup_ptr +
                                   image->startup_size;
            startup_first = image->startup_ptr / EXEC_PAGE_SIZE;
            startup_last = (startup_end + EXEC_PAGE_SIZE - 1) /
                           EXEC_PAGE_SIZE;
        }
        if (startup_first > module_pages || startup_last < startup_first ||
            startup_last > module_pages ||
            startup_last >= stack_first ||
            (startup_first && native_process_capsule_reserve_region(
                &process->capsule, 0, startup_first,
                NATIVE_PROCESS_REGION_MODULE) != 0) ||
            (startup_last > startup_first &&
             native_process_capsule_reserve_region(
                 &process->capsule, startup_first,
                 startup_last - startup_first,
                 NATIVE_PROCESS_REGION_STARTUP) != 0) ||
            native_process_capsule_reserve_region(
                &process->capsule, startup_last, 1,
                NATIVE_PROCESS_REGION_BRK) != 0 ||
            native_process_capsule_reserve_region(
                &process->capsule, stack_first, 1,
                NATIVE_PROCESS_REGION_GUARD) != 0 ||
            native_process_capsule_reserve_region(
                &process->capsule, stack_first + 1, 16,
                NATIVE_PROCESS_REGION_STACK) != 0)
            return -POSIX_ENOMEM;
    }
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
        if (bindings[i].clone->memory &&
            native_process_bind_engine_memory(&child->capsule,
                                              bindings[i].clone) != 0) {
            for (uint32_t j = 0; j < owned_count; j++)
                if (exec_free) exec_free(owned[j]);
            free(owned);
            free(bindings);
            child->capsule.linked_engines = NULL;
            child->capsule.linked_engine_count = 0;
            return -POSIX_EINVAL;
        }
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
    /* Release file descriptors immediately so that pipe endpoints are freed
     * and readers can observe EOF.  The kernel stays alive for path-node
     * merging at waitpid time; only FDs are closed. */
    posix_kernel_close_all_fds(process->kernel);
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
    {
        int merge_status = posix_kernel_merge_paths(parent->kernel,
                                                     candidate->kernel);
        if (merge_status != 0) return merge_status;
    }
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
