#ifndef WASTE_PROCESS_DRIVER_H
#define WASTE_PROCESS_DRIVER_H

#include "store.h"

/* A bounded child-first selector, not a concurrent process/thread scheduler.
 * All mutable state belongs to one runtime session; kernels/capsules own the
 * actual process graph. External waits are returned to the runtime unchanged. */
typedef struct {
    int pid;
    waste_exec_engine *engine;
    uint32_t func_idx;
    wasm_value args[WAST_MAX_ARGS];
    int arg_count;
    exec_yield_reason wait_reason;
} native_driver_selection;

typedef struct {
    exec_continuation continuation;
    int initialized, fork_active, parent_restored, image_active;
    waste_exec_engine *parent_engine, *active_engine;
    uint32_t parent_func_idx, active_func_idx;
    wasm_value parent_args[WAST_MAX_ARGS], active_args[WAST_MAX_ARGS];
    int parent_arg_count, active_arg_count;
    native_driver_selection selection;
    /* Borrowed runtime command-stream context and callbacks. No guest memory
     * pointer, browser import, host syscall or mutable global lives here. */
    native_process_handler_step handler_step;
    void (*handler_reset)(void *);
    void *handler_context;
    exec_yield_reason handler_wait_reason;
    void (*trace)(void *, const char *);
    void *trace_context;
    unsigned forks, execs, child_exits;
} native_process_driver;

void native_process_driver_init(native_process_driver *driver);
void native_process_driver_destroy(native_process_driver *driver);
/* Prepare the first image in a fresh store. The store must have exactly its
 * initial process and no instantiated modules. On success selection names
 * the entry to invoke; argv/envp are copied and may be released by the caller.
 * On failure the original kernel and an empty process capsule are restored. */
exec_status native_process_driver_start(native_process_driver *driver,
    native_store *store, const char *path, const char *const *argv,
    uint32_t argc, const char *const *envp, uint32_t envc, exec_error *error);
typedef struct {
    const char *entry; /* Default _start. Process entries have type () -> (). */
    native_exec_format format;
    int readable_input; /* Explicit interpreter reads; later exec checks X_OK. */
} native_process_start_options;
exec_status native_process_driver_start_with_options(native_process_driver *driver,
    native_store *store, const char *path, const char *const *argv,
    uint32_t argc, const char *const *envp, uint32_t envc,
    const native_process_start_options *options, exec_error *error);
/* Fresh-store production resource/libc context without starting an application.
 * Failures restore the original mounted kernel and leave no providers. */
exec_status native_process_driver_prepare_runtime(native_process_driver *,
    native_store *, uint32_t memory_pages, uint32_t table_entries, exec_error *);
exec_status native_process_driver_invoke(native_process_driver *driver,
    native_store *store, waste_exec_engine *engine, uint32_t function,
    const wasm_value *args, int arg_count, wasm_value *results,
    int *result_count, exec_error *error);

#endif
