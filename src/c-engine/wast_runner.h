#ifndef WAST_RUNNER_H
#define WAST_RUNNER_H

#include "wast_types.h"
#include "waste_exec.h"

/*
 * Parse a WAST file into a wast_script with per-module groups.
 * Returns 0 on success, -1 on parse error (script->error is set).
 */
int wast_parse_file(const char *path, wast_script *script);

/*
 * Run a single assertion against a loaded engine.
 * Returns EXEC_OK if the assertion passes, EXEC_ERROR_TRAP if it fails.
 */
exec_status wast_run_assertion(waste_exec_engine *engine,
                               const wast_assertion *assertion,
                               exec_error *error);

/*
 * Check if a result value matches any of the alternatives.
 * Returns 1 if it matches, 0 if not.
 */
int wast_v128_matches_any(const wasm_value *actual,
                           const wasm_value alternatives[][WAST_MAX_RESULTS],
                           int alt_count, int result_count);

#endif /* WAST_RUNNER_H */
