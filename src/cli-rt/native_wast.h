#ifndef WASTE_NATIVE_WAST_H
#define WASTE_NATIVE_WAST_H

#include "process_driver.h"
#include "wast/assert.h"
#include <stdio.h>

typedef struct {
    unsigned passed, total;
    int completed; /* Complete receipt/report; JSON completed tracks script EOF. */
} native_wast_counts;

void native_wast_bind(native_store *store);
typedef enum { NATIVE_WAST_AUTO, NATIVE_WAST_LANGUAGE, NATIVE_WAST_RUNTIME } native_wast_mode;
typedef struct {
    int json, verbose;
    FILE *output;
    native_wast_mode mode;
    wast_invoke_callback invoke;
    void *invoke_context;
    void (*context_ready)(void *);
    native_process_driver *driver;
} native_wast_options;
int native_wast_run_with_options(native_store *, const char *, const char *,
    size_t, native_wast_counts *, const native_wast_options *);
/* Caller owns an isolated store, source bytes and any mounted files. Finite
 * SELECT waits resume against the native monotonic clock; external input still
 * needs the session driver. Emits assertion JSON; counts do not replace it. */
int native_wast_run(native_store *store, const char *filename,
                    const char *source, size_t length, native_wast_counts *counts);
void native_wast_json_string(FILE *output, const char *value);

#endif
