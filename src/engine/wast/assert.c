#include "wast/assert.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Compare two v128 values with NaN mode awareness.
 * nan_mode[i] in expected is:
 *   NAN_MATCH_EXACT      (0): compare byte i exactly
 *   NAN_MATCH_F32_CANON  (1): bytes i..i+3 are an f32 lane; match any canonical NaN
 *   NAN_MATCH_F32_ARITH  (2): bytes i..i+3 are an f32 lane; match any NaN
 *   NAN_MATCH_F64_CANON  (3): bytes i..i+7 are an f64 lane; match any canonical NaN
 *   NAN_MATCH_F64_ARITH  (4): bytes i..i+7 are an f64 lane; match any NaN
 * Only the first byte of each lane carries the mode; remaining bytes are EXACT.
 */
static int v128_matches(const wasm_value *actual, const wasm_value *expected) {
    int i = 0;
    while (i < 16) {
        uint8_t mode = expected->nan_mode[i];
        if (mode == NAN_MATCH_EXACT) {
            if (actual->v128.bytes[i] != expected->v128.bytes[i]) return 0;
            i++;
        } else if (mode == NAN_MATCH_F32_CANON || mode == NAN_MATCH_F32_ARITH) {
            uint32_t ab;
            memcpy(&ab, &actual->v128.bytes[i], 4);
            int is_nan = ((ab & 0x7F800000u) == 0x7F800000u) && (ab & 0x007FFFFFu);
            if (!is_nan) return 0;
            if (mode == NAN_MATCH_F32_CANON) {
                if ((ab & 0x007FFFFFu) != 0x00400000u) return 0;
            }
            i += 4;
        } else if (mode == NAN_MATCH_F64_CANON || mode == NAN_MATCH_F64_ARITH) {
            uint64_t ab;
            memcpy(&ab, &actual->v128.bytes[i], 8);
            int is_nan = ((ab & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) &&
                         (ab & 0x000FFFFFFFFFFFFFULL);
            if (!is_nan) return 0;
            if (mode == NAN_MATCH_F64_CANON) {
                if ((ab & 0x000FFFFFFFFFFFFFULL) != 0x0008000000000000ULL) return 0;
            }
            i += 8;
        } else {
            /* Unknown mode: exact */
            if (actual->v128.bytes[i] != expected->v128.bytes[i]) return 0;
            i++;
        }
    }
    return 1;
}

static int value_matches(const wasm_value *actual, const wasm_value *expected) {
    if (expected->nan_mode[0] == REF_MATCH_NULL)
        return actual->ref == UINT32_MAX &&
               ((unsigned)actual->type >= (unsigned)WASM_VALTYPE_FUNCREF ||
                WASM_VALTYPE_IS_TYPE_REF(actual->type));
    if (actual->ref == UINT32_MAX && expected->ref == UINT32_MAX) {
        if (actual->type == WASM_VALTYPE_NULLREF &&
            (expected->type == WASM_VALTYPE_ANYREF ||
             expected->type == WASM_VALTYPE_EQREF ||
             expected->type == WASM_VALTYPE_I31REF ||
             expected->type == WASM_VALTYPE_STRUCTREF ||
             expected->type == WASM_VALTYPE_ARRAYREF)) return 1;
        if (actual->type == WASM_VALTYPE_NULLFUNCREF &&
            (expected->type == WASM_VALTYPE_FUNCREF ||
             WASM_VALTYPE_IS_TYPE_REF(expected->type))) return 1;
        if (actual->type == WASM_VALTYPE_NULLEXNREF &&
            expected->type == WASM_VALTYPE_EXNREF) return 1;
        if (actual->type == WASM_VALTYPE_NULLEXTERNREF &&
            expected->type == WASM_VALTYPE_EXTERNREF) return 1;
    }
    /* (ref.func) pattern: any non-null funcref */
    if (expected->type == WASM_VALTYPE_FUNCREF_NONNULL && expected->ref == UINT32_MAX)
        return (actual->type == WASM_VALTYPE_FUNCREF ||
                actual->type == WASM_VALTYPE_FUNCREF_NONNULL ||
                WASM_VALTYPE_IS_TYPE_REF(actual->type))
               && actual->ref != UINT32_MAX;
    /* (ref.extern) pattern: any non-null externref */
    if (expected->type == WASM_VALTYPE_EXTERNREF_NONNULL && expected->ref == UINT32_MAX)
        return (actual->type == WASM_VALTYPE_EXTERNREF || actual->type == WASM_VALTYPE_EXTERNREF_NONNULL)
               && actual->ref != UINT32_MAX;
    if (expected->type == WASM_VALTYPE_I31REF_NONNULL && expected->ref == UINT32_MAX)
        return (actual->type == WASM_VALTYPE_I31REF ||
                actual->type == WASM_VALTYPE_I31REF_NONNULL) &&
               actual->ref != UINT32_MAX;
    if ((expected->type == WASM_VALTYPE_STRUCTREF_NONNULL ||
         expected->type == WASM_VALTYPE_ARRAYREF_NONNULL ||
         expected->type == WASM_VALTYPE_EQREF_NONNULL ||
         expected->type == WASM_VALTYPE_ANYREF_NONNULL) &&
        expected->ref == UINT32_MAX)
        return actual->ref != UINT32_MAX &&
               (WASM_VALTYPE_IS_TYPE_REF(actual->type) ||
                actual->type == expected->type ||
                (expected->type == WASM_VALTYPE_EQREF_NONNULL &&
                 (actual->type == WASM_VALTYPE_I31REF_NONNULL ||
                  actual->type == WASM_VALTYPE_STRUCTREF_NONNULL ||
                  actual->type == WASM_VALTYPE_ARRAYREF_NONNULL)));
    /* A typed function reference is a subtype of funcref.  Assertions use
     * the source-level expected type, while execution retains the more precise
     * indexed type needed by call_ref. */
    if (expected->type == WASM_VALTYPE_FUNCREF &&
        WASM_VALTYPE_IS_TYPE_REF(actual->type))
        return actual->ref == expected->ref;
    if ((actual->type == WASM_VALTYPE_FUNCREF_NONNULL &&
         expected->type == WASM_VALTYPE_FUNCREF) ||
        (actual->type == WASM_VALTYPE_EXTERNREF_NONNULL &&
         expected->type == WASM_VALTYPE_EXTERNREF) ||
        (actual->type == WASM_VALTYPE_ANYREF_NONNULL &&
         expected->type == WASM_VALTYPE_ANYREF) ||
        (actual->type == WASM_VALTYPE_EQREF_NONNULL &&
         expected->type == WASM_VALTYPE_EQREF) ||
        (actual->type == WASM_VALTYPE_I31REF_NONNULL &&
         expected->type == WASM_VALTYPE_I31REF) ||
        (actual->type == WASM_VALTYPE_STRUCTREF_NONNULL &&
         expected->type == WASM_VALTYPE_STRUCTREF) ||
        (actual->type == WASM_VALTYPE_ARRAYREF_NONNULL &&
         expected->type == WASM_VALTYPE_ARRAYREF))
        return actual->ref == expected->ref;
    if ((actual->type == WASM_VALTYPE_ANYREF ||
         actual->type == WASM_VALTYPE_ANYREF_NONNULL) &&
        expected->type == WASM_VALTYPE_EXTERNREF)
        return actual->ref == expected->ref;
    if (actual->type != expected->type) {
        if (actual->type == WASM_VALTYPE_V128 && expected->type == WASM_VALTYPE_V128)
            return v128_matches(actual, expected);
        return 0;
    }
    switch (actual->type) {
        case WASM_VALTYPE_V128:
            return v128_matches(actual, expected);
        case WASM_VALTYPE_I32:
            return actual->i32 == expected->i32;
        case WASM_VALTYPE_I64:
            return actual->i64 == expected->i64;
        case WASM_VALTYPE_F32: {
            uint32_t ab, eb;
            memcpy(&ab, &actual->f32, 4);
            memcpy(&eb, &expected->f32, 4);
            uint8_t mode = expected->nan_mode[0];
            if (mode == NAN_MATCH_F32_CANON || mode == NAN_MATCH_F32_ARITH) {
                int is_nan = ((ab & 0x7F800000u) == 0x7F800000u) && (ab & 0x007FFFFFu);
                if (!is_nan) return 0;
                if (mode == NAN_MATCH_F32_CANON)
                    return (ab & 0x007FFFFFu) == 0x00400000u;
                return 1; /* arithmetic: any NaN (quiet bit presence not required by Wasm) */
            }
            return ab == eb;
        }
        case WASM_VALTYPE_F64: {
            uint64_t ab, eb;
            memcpy(&ab, &actual->f64, 8);
            memcpy(&eb, &expected->f64, 8);
            uint8_t mode = expected->nan_mode[0];
            if (mode == NAN_MATCH_F64_CANON || mode == NAN_MATCH_F64_ARITH) {
                int is_nan = ((ab & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) &&
                             (ab & 0x000FFFFFFFFFFFFFULL);
                if (!is_nan) return 0;
                if (mode == NAN_MATCH_F64_CANON)
                    return (ab & 0x000FFFFFFFFFFFFFULL) == 0x0008000000000000ULL;
                return 1;
            }
            return ab == eb;
        }
        case WASM_VALTYPE_FUNCREF:
        case WASM_VALTYPE_EXTERNREF:
        case WASM_VALTYPE_FUNCREF_NONNULL:
        case WASM_VALTYPE_EXTERNREF_NONNULL:
        case WASM_VALTYPE_ANYREF:
        case WASM_VALTYPE_EQREF:
        case WASM_VALTYPE_I31REF:
        case WASM_VALTYPE_STRUCTREF:
        case WASM_VALTYPE_ARRAYREF:
        case WASM_VALTYPE_ANYREF_NONNULL:
        case WASM_VALTYPE_EQREF_NONNULL:
        case WASM_VALTYPE_I31REF_NONNULL:
        case WASM_VALTYPE_STRUCTREF_NONNULL:
        case WASM_VALTYPE_ARRAYREF_NONNULL:
        case WASM_VALTYPE_EXNREF:
        case WASM_VALTYPE_EXNREF_NONNULL:
        case WASM_VALTYPE_NULLREF:
        case WASM_VALTYPE_NULLFUNCREF:
        case WASM_VALTYPE_NULLEXNREF:
        case WASM_VALTYPE_NULLEXTERNREF:
            return actual->ref == expected->ref;
    }
    return 0;
}

int wast_v128_matches_any(const wasm_value *actual,
                           const wasm_value alternatives[][WAST_MAX_RESULTS],
                           int alt_count, int result_count) {
    for (int i = 0; i < alt_count; i++) {
        int matches = 1;
        for (int result = 0; result < result_count; result++) {
            if (!value_matches(&actual[result], &alternatives[i][result])) {
                matches = 0;
                break;
            }
        }
        if (matches) return 1;
    }
    return 0;
}

exec_status wast_run_assertion(waste_exec_engine *engine,
                               const wast_assertion *assertion,
                               exec_error *error) {
    if (assertion->action_kind == WAST_ACTION_GET) {
        exec_global *global = NULL;
        exec_status status = exec_find_export_global(
            engine, assertion->func_name, &global, error);
        if (status != EXEC_OK) return status;
        if (assertion->alt_count == 0 ||
            wast_v128_matches_any(&global->value, assertion->alternatives,
                                  assertion->alt_count, 1))
            return EXEC_OK;
        if (error) {
            error->status = EXEC_ERROR_TRAP;
            snprintf(error->message, sizeof(error->message),
                     "global result mismatch for %s", assertion->func_name);
        }
        return EXEC_ERROR_TRAP;
    }
    uint32_t func_idx;
    exec_status st = exec_find_export(engine, assertion->func_name, &func_idx, error);
    if (st != EXEC_OK) return st;

    wasm_value results[WAST_MAX_RESULTS];
    int result_count = 0;
    st = exec_invoke(engine, func_idx,
                     assertion->args, assertion->arg_count,
                     results, &result_count, error);
    if (st == EXEC_ERROR_EXIT && assertion->kind == WAST_ASSERT_RETURN &&
        assertion->alt_count == 0) {
        if (error) memset(error, 0, sizeof(*error));
        return EXEC_OK;
    }
    if (assertion->kind == WAST_ASSERT_EXCEPTION) {
        if (st == EXEC_ERROR_EXCEPTION) {
            if (error) memset(error, 0, sizeof(*error));
            return EXEC_OK;
        }
        if (st == EXEC_OK && error) {
            error->status = EXEC_ERROR_TRAP;
            snprintf(error->message, sizeof(error->message),
                     "expected exception from %.215s", assertion->func_name);
        }
        return st == EXEC_OK ? EXEC_ERROR_TRAP : st;
    }
    if (assertion->kind == WAST_ASSERT_TRAP ||
        assertion->kind == WAST_ASSERT_EXHAUSTION) {
        if (st == EXEC_ERROR_TRAP) {
            if (error) memset(error, 0, sizeof(*error));
            return EXEC_OK;
        }
        if (st == EXEC_OK && error) {
            error->status = EXEC_ERROR_TRAP;
            snprintf(error->message, sizeof(error->message),
                     "expected trap from %s", assertion->func_name);
        }
        return st == EXEC_OK ? EXEC_ERROR_TRAP : st;
    }
    if (st != EXEC_OK) return st;

    if (assertion->alt_count == 0) {
        return EXEC_OK;
    }

    if (result_count != assertion->result_count) {
        if (error) snprintf(error->message, sizeof(error->message), "result count mismatch");
        return EXEC_ERROR_TRAP;
    }

    if (wast_v128_matches_any(results, assertion->alternatives,
                               assertion->alt_count, assertion->result_count)) {
        return EXEC_OK;
    }

    if (error) {
        if (assertion->result_count == 1 &&
            results[0].type == WASM_VALTYPE_I32 &&
            assertion->alternatives[0][0].type == WASM_VALTYPE_I32)
            snprintf(error->message, sizeof(error->message),
                     "result mismatch for %.150s (actual %d, expected %d)",
                     assertion->func_name, results[0].i32,
                     assertion->alternatives[0][0].i32);
        else if (assertion->result_count == 1 &&
                 results[0].type == WASM_VALTYPE_F64 &&
                 assertion->alternatives[0][0].type == WASM_VALTYPE_F64) {
            uint64_t actual_bits, expected_bits;
            memcpy(&actual_bits, &results[0].f64, sizeof(actual_bits));
            memcpy(&expected_bits, &assertion->alternatives[0][0].f64,
                   sizeof(expected_bits));
            snprintf(error->message, sizeof(error->message),
                     "result mismatch for %.150s (actual 0x%016llx, expected 0x%016llx)",
                     assertion->func_name,
                     (unsigned long long)actual_bits,
                     (unsigned long long)expected_bits);
        }
        else
            snprintf(error->message, sizeof(error->message),
                     "result mismatch for %s", assertion->func_name);
    }
    return EXEC_ERROR_TRAP;
}
