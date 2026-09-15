#ifndef WASTE_WAST_ASSERT_H
#define WASTE_WAST_ASSERT_H

#include "wat/types.h"
#include "engine_internal.h"

/* Run one parsed WAST assertion against a selected module instance. */
exec_status wast_run_assertion(waste_exec_engine *engine,
                               const wast_assertion *assertion,
                               exec_error *error);

/* Match a result vector against any parsed expected-value alternative. */
int wast_v128_matches_any(const wasm_value *actual,
                          const wasm_value alternatives[][WAST_MAX_RESULTS],
                          int alternative_count, int result_count);

#endif
