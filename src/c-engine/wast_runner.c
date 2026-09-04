#include "wast_runner.h"
#include "wast_types.h"
#include "waste_exec.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>

/* Flex/Bison generated API */
typedef void *yyscan_t;
typedef struct yy_buffer_state *YY_BUFFER_STATE;

extern int           yyparse(wast_script *script, void *scanner);
extern int           yylex_init(yyscan_t *scanner);
extern int           yylex_destroy(yyscan_t scanner);
extern YY_BUFFER_STATE yy_scan_bytes(const char *bytes, int len, yyscan_t scanner);
extern void          yy_switch_to_buffer(YY_BUFFER_STATE buf, yyscan_t scanner);
extern void          yy_delete_buffer(YY_BUFFER_STATE buf, yyscan_t scanner);

int wast_parse_file(const char *path, wast_script *script) {
    memset(script, 0, sizeof(*script));

    /* Read entire file into memory */
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(script->error, sizeof(script->error), "cannot open file: %s", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize < 0) {
        fclose(f);
        snprintf(script->error, sizeof(script->error), "cannot stat file: %s", path);
        return -1;
    }
    char *source = (char *)malloc((size_t)fsize + 2);
    if (!source) {
        fclose(f);
        snprintf(script->error, sizeof(script->error), "out of memory");
        return -1;
    }
    size_t nread = fread(source, 1, (size_t)fsize, f);
    fclose(f);
    /* Flex's yy_scan_bytes needs 2 null bytes at the end of the buffer */
    source[nread]   = '\0';
    source[nread+1] = '\0';

    yyscan_t scanner;
    yylex_init(&scanner);
    YY_BUFFER_STATE buf = yy_scan_bytes(source, (int)nread, scanner);
    yy_switch_to_buffer(buf, scanner);
    int rc = yyparse(script, scanner);
    yy_delete_buffer(buf, scanner);
    yylex_destroy(scanner);
    free(source);

    if (rc != 0 && script->error[0] == '\0') {
        snprintf(script->error, sizeof(script->error), "parse failed");
    }
    return (rc != 0 || script->error[0] != '\0') ? -1 : 0;
}

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
            return ab == eb;
        }
        case WASM_VALTYPE_F64: {
            uint64_t ab, eb;
            memcpy(&ab, &actual->f64, 8);
            memcpy(&eb, &expected->f64, 8);
            return ab == eb;
        }
    }
    return 0;
}

int wast_v128_matches_any(const wasm_value *actual,
                           const wasm_value alternatives[][WAST_MAX_RESULTS],
                           int alt_count, int result_count) {
    (void)result_count;
    for (int i = 0; i < alt_count; i++) {
        if (value_matches(actual, &alternatives[i][0])) return 1;
    }
    return 0;
}

exec_status wast_run_assertion(waste_exec_engine *engine,
                               const wast_assertion *assertion,
                               exec_error *error) {
    uint32_t func_idx;
    exec_status st = exec_find_export(engine, assertion->func_name, &func_idx, error);
    if (st != EXEC_OK) return st;

    wasm_value results[WAST_MAX_RESULTS];
    int result_count = 0;
    st = exec_invoke(engine, func_idx,
                     assertion->args, assertion->arg_count,
                     results, &result_count, error);
    if (st != EXEC_OK) return st;

    if (assertion->alt_count == 0) {
        return EXEC_OK;
    }

    if (result_count < 1) {
        if (error) snprintf(error->message, sizeof(error->message), "no result returned");
        return EXEC_ERROR_TRAP;
    }

    if (wast_v128_matches_any(&results[0], assertion->alternatives,
                               assertion->alt_count, assertion->result_count)) {
        return EXEC_OK;
    }

    if (error) {
        snprintf(error->message, sizeof(error->message),
                 "result mismatch for %s", assertion->func_name);
    }
    return EXEC_ERROR_TRAP;
}
