#ifndef WASTE_WAST_HANDLER_H
#define WASTE_WAST_HANDLER_H

#include "store.h"
#include "wast/stream.h"

/* One process-owned command handler. The store borrows parsed module
 * metadata until handler completion (or store teardown after cancellation).
 * Reset closes only the current command stream.
 * Destroy AFTER native_store_free, reset BEFORE releasing a handler source. */
typedef void (*wast_handler_result)(void *, int, const char *, const char *);
typedef struct {
    native_store *store;
    int pid;
    wast_stream stream;
    int stream_active, pending, failed, stopped, exit_code;
    exec_yield_reason wait_reason;
    wast_assertion assertion;
    waste_exec_engine *assertion_engine;
    wast_script **retained;
    unsigned retained_count;
    waste_exec_engine **owned_orphans;
    unsigned owned_orphan_count;
    int module_start, orphan_start;
    unsigned retained_start;
    wast_handler_result result;
    void *result_data;
    unsigned total, passed;
    int verbose;
    char first_failure[256];
} wast_process_handler;

void wast_process_handler_init(wast_process_handler *, native_store *,
                               wast_handler_result, void *);
void wast_process_handler_reset(void *);
void wast_process_handler_destroy(wast_process_handler *);
void *wast_process_handler_create(void *);
void wast_process_handler_destroy_owned(void *);
exec_status wast_process_handler_step(const uint8_t *, size_t, size_t,
                                      unsigned, size_t *, unsigned *, void *);

#endif
