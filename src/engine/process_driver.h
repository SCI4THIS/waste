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
exec_status native_process_driver_invoke(native_process_driver *driver,
    native_store *store, waste_exec_engine *engine, uint32_t function,
    const wasm_value *args, int arg_count, wasm_value *results,
    int *result_count, exec_error *error);

#endif
