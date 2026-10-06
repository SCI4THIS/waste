#ifndef WASTE_WAST_SETUP_H
#define WASTE_WAST_SETUP_H

#include "engine_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Ordinary module commands are setup, not assertions. Keep their failures
 * independently observable, including scripts that execute no assertions.
 * Reports own their diagnostics until reset; no engine pointers are retained. */
enum { WAST_SETUP_ENCODE = 1, WAST_SETUP_LOAD, WAST_SETUP_DEFINITION,
       WAST_SETUP_RETAIN };
typedef struct {
    uint32_t line;
    int32_t status;
    uint32_t phase;
    char error[WAST_SETUP_ERROR_BYTES];
} wast_setup_failure;
typedef struct {
    uint32_t total, passed, count, capacity;
    int incomplete;
    wast_setup_failure *failures;
} wast_setup_report;

static inline void wast_setup_reset(wast_setup_report *report) {
    free(report->failures);
    memset(report, 0, sizeof(*report));
}

static inline void wast_setup_record(wast_setup_report *report, unsigned line,
                                     exec_status status, unsigned phase,
                                     const char *error) {
    report->total++;
    if (status == EXEC_OK) { report->passed++; return; }
    if (report->count == report->capacity) {
        unsigned capacity = report->capacity ? report->capacity * 2u : 8u;
        if (capacity > 16384u) { report->incomplete = 1; return; }
        wast_setup_failure *failures = realloc(report->failures,
            (size_t)capacity * sizeof(*failures));
        if (!failures) { report->incomplete = 1; return; }
        report->failures = failures;
        report->capacity = capacity;
    }
    wast_setup_failure *failure = &report->failures[report->count++];
    failure->line = line;
    failure->status = status;
    failure->phase = phase;
    snprintf(failure->error, sizeof(failure->error), "%s",
             error && error[0] ? error : "module setup failed");
}

#endif
