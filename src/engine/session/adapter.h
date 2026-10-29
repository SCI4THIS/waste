#ifndef WASTE_SESSION_ADAPTER_H
#define WASTE_SESSION_ADAPTER_H

#include "../include/waste.h"
#include "../process_driver.h"

/* Private compatibility bridge for the native TTY and browser WAST consumers.
 * The opaque session owns the store/driver and their teardown. Adapters install
 * their own explicit platform capabilities, terminal and assertion callbacks;
 * they never publish these borrowed implementation pointers to an embedder.
 * Batch embedders use waste_session_create/start/step instead. */
exec_status waste_session_adapter_create(waste_session **, exec_error *);
native_store *waste_session_adapter_store(waste_session *);
native_process_driver *waste_session_adapter_driver(waste_session *);
exec_status waste_session_adapter_start(waste_session *, const char *,
    const char *const *, uint32_t, const char *const *, uint32_t,
    const native_process_start_options *, exec_error *);
exec_status waste_session_adapter_invoke(waste_session *, waste_exec_engine *,
    uint32_t, const wasm_value *, int, wasm_value *, int *, exec_error *);

#endif
