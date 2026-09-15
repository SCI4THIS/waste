#ifndef WAST_RUNNER_H
#define WAST_RUNNER_H

#include "wat/types.h"
#include "engine_internal.h"
#include "wast/assert.h"

/*
 * Parse a WAST file into a wast_script with per-module groups.
 * Returns 0 on success, -1 on parse error (script->error is set).
 */
int wast_parse_file(const char *path, wast_script *script);
int wast_parse_bytes(const char *bytes, size_t length, wast_script *script);
void wast_script_free(wast_script *script);
int wast_source_is_inline_module(const char *source, size_t length);

/* Strict WAT mode: compile exactly one complete module transactionally. */
int waste_wat_compile(const char *bytes, size_t length,
                      uint8_t **wasm_out, size_t *wasm_length,
                      char *error, size_t error_length);

#endif /* WAST_RUNNER_H */
