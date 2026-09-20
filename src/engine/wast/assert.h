#ifndef WASTE_WAST_ASSERT_H
#define WASTE_WAST_ASSERT_H

#include "wat/types.h"
#include "engine_internal.h"

typedef exec_status (*wast_invoke_callback)(void *data,
                                             waste_exec_engine *engine,
                                             uint32_t func_idx,
                                             const wasm_value *args,
                                             int arg_count,
                                             wasm_value *results,
                                             int *result_count,
                                             exec_error *error);

exec_status wast_run_assertion_with_invoke(
    waste_exec_engine *engine, const wast_assertion *assertion,
    exec_error *error, wast_invoke_callback callback, void *callback_data);

/* Run one parsed WAST assertion against a selected module instance. */
exec_status wast_run_assertion(waste_exec_engine *engine,
                               const wast_assertion *assertion,
                               exec_error *error);

/* Match a result vector against any parsed expected-value alternative. */
int wast_v128_matches_any(const wasm_value *actual,
                          const wasm_value alternatives[][WAST_MAX_RESULTS],
                          int alternative_count, int result_count);

#endif
