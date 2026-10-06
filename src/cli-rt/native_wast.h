#ifndef WASTE_NATIVE_WAST_H
#define WASTE_NATIVE_WAST_H

#include "store.h"
#include <stdio.h>

typedef struct {
    unsigned passed, total;
    int completed;
} native_wast_counts;

void native_wast_bind(native_store *store);
/* Caller owns an isolated store, source bytes and any mounted files. Finite
 * SELECT waits resume against the native monotonic clock; external input still
 * needs the session driver. Emits assertion JSON; counts do not replace it. */
int native_wast_run(native_store *store, const char *filename,
                    const char *source, size_t length, native_wast_counts *counts);
void native_wast_json_string(FILE *output, const char *value);

#endif
