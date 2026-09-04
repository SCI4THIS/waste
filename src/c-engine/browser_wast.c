#include "waste_exec.h"
#include "wast_types.h"

#include <stddef.h>
#include <stdint.h>

/* Freestanding stubs */
void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = a, *q = b;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != q[i]) return p[i] < q[i] ? -1 : 1;
    }
    return 0;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    (void)buf; (void)n; (void)fmt;
    return 0;
}

/* Bump allocator */
extern unsigned char __heap_base;
static uintptr_t heap_cursor;

void *malloc(size_t size) {
    uintptr_t start;
    uintptr_t limit;
    uintptr_t memory_size;
    uint32_t pages;
    if (heap_cursor == 0) heap_cursor = (uintptr_t)&__heap_base;
    start = (heap_cursor + 15u) & ~(uintptr_t)15u;
    if (size > (uintptr_t)-1 - start) return (void *)0;
    limit = start + size;
    memory_size = (uintptr_t)__builtin_wasm_memory_size(0) * 65536u;
    if (limit > memory_size) {
        uintptr_t missing = limit - memory_size;
        pages = (uint32_t)((missing + 65535u) / 65536u);
        if (__builtin_wasm_memory_grow(0, pages) == (size_t)-1) return (void *)0;
    }
    heap_cursor = limit;
    return (void *)start;
}

void *calloc(size_t count, size_t size) {
    unsigned char *result;
    size_t total;
    if (count != 0 && size > (size_t)-1 / count) return (void *)0;
    total = count * size;
    result = (unsigned char *)malloc(total);
    if (!result) return (void *)0;
    for (size_t i = 0; i < total; i++) result[i] = 0;
    return result;
}

void free(void *p) { (void)p; }

/* ---- Math stubs (relaxed semantics are sufficient) ---- */

float fmaf(float a, float b, float c) {
    /* Relaxed FMA: a*b+c without strict rounding guarantee */
    return (float)((double)a * (double)b + (double)c);
}

double fma(double a, double b, double c) {
    return a * b + c;
}

/* ---- Engine state ---- */

static waste_exec_engine *g_engine = (void *)0;
static exec_error         g_error;

/* ---- Exported API ---- */

__attribute__((export_name("waste_wast_alloc")))
uint32_t waste_wast_alloc(uint32_t size) {
    return (uint32_t)(uintptr_t)malloc((size_t)size);
}

__attribute__((export_name("waste_wast_load_module")))
uint32_t waste_wast_load_module(uint32_t ptr, uint32_t size) {
    if (g_engine) { exec_free(g_engine); g_engine = (void *)0; }
    memset(&g_error, 0, sizeof(g_error));
    exec_status st = exec_load((const uint8_t *)(uintptr_t)ptr, (size_t)size,
                               &g_engine, &g_error);
    return (uint32_t)st;
}

__attribute__((export_name("waste_wast_find_export")))
int32_t waste_wast_find_export(uint32_t name_ptr, uint32_t name_len) {
    if (!g_engine) return -1;
    /* Build a null-terminated name (name_ptr must be in engine memory) */
    char name[WAST_MAX_EXPORT_NAME];
    if (name_len >= WAST_MAX_EXPORT_NAME) name_len = WAST_MAX_EXPORT_NAME - 1;
    memcpy(name, (const void *)(uintptr_t)name_ptr, name_len);
    name[name_len] = '\0';
    uint32_t func_idx = 0;
    memset(&g_error, 0, sizeof(g_error));
    exec_status st = exec_find_export(g_engine, name, &func_idx, &g_error);
    if (st != EXEC_OK) return -1;
    return (int32_t)func_idx;
}

/*
 * waste_wast_run: run a function.
 * args_ptr points to packed wasm_value structs in engine memory.
 * results_ptr points to output buffer.
 * Returns status (0 = ok).
 */
__attribute__((export_name("waste_wast_run")))
uint32_t waste_wast_run(uint32_t func_idx,
                         uint32_t args_ptr, uint32_t arg_count,
                         uint32_t results_ptr) {
    if (!g_engine) return (uint32_t)EXEC_ERROR_FORMAT;
    memset(&g_error, 0, sizeof(g_error));

    wasm_value *args    = (wasm_value *)(uintptr_t)args_ptr;
    wasm_value *results = (wasm_value *)(uintptr_t)results_ptr;
    int result_count = 0;

    exec_status st = exec_invoke(g_engine, func_idx,
                                 args, (int)arg_count,
                                 results, &result_count,
                                 &g_error);
    return (uint32_t)st;
}

/* ---- error access ---- */

__attribute__((export_name("waste_wast_error_ptr")))
uint32_t waste_wast_error_ptr(void) {
    return (uint32_t)(uintptr_t)g_error.message;
}

/* ---- flat value comparison helpers ---- */

/*
 * Flat value layout (33 bytes):
 *   [0]     : type byte (0=i32,1=i64,2=f32,3=f64,4=v128)
 *   [1..16] : 16 data bytes (little-endian for scalars, raw for v128)
 *   [17..32]: 16 nan_mode bytes
 */
#define FLAT_VALUE_SIZE 33

static int v128_matches_flat(const uint8_t *actual_bytes,
                              const uint8_t *exp_data,
                              const uint8_t *exp_nan_mode) {
    int i = 0;
    while (i < 16) {
        uint8_t mode = exp_nan_mode[i];
        if (mode == NAN_MATCH_EXACT) {
            if (actual_bytes[i] != exp_data[i]) return 0;
            i++;
        } else if (mode == NAN_MATCH_F32_CANON || mode == NAN_MATCH_F32_ARITH) {
            uint32_t ab;
            memcpy(&ab, &actual_bytes[i], 4);
            int is_nan = ((ab & 0x7F800000u) == 0x7F800000u) && (ab & 0x007FFFFFu);
            if (!is_nan) return 0;
            if (mode == NAN_MATCH_F32_CANON && (ab & 0x007FFFFFu) != 0x00400000u) return 0;
            i += 4;
        } else if (mode == NAN_MATCH_F64_CANON || mode == NAN_MATCH_F64_ARITH) {
            uint64_t ab;
            memcpy(&ab, &actual_bytes[i], 8);
            int is_nan = ((ab & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) &&
                         (ab & 0x000FFFFFFFFFFFFFULL);
            if (!is_nan) return 0;
            if (mode == NAN_MATCH_F64_CANON &&
                (ab & 0x000FFFFFFFFFFFFFULL) != 0x0008000000000000ULL) return 0;
            i += 8;
        } else {
            if (actual_bytes[i] != exp_data[i]) return 0;
            i++;
        }
    }
    return 1;
}

static void unpack_flat_value(const uint8_t *flat, wasm_value *v) {
    memset(v, 0, sizeof(*v));
    v->type = (wasm_valtype)flat[0];
    memcpy(v->nan_mode, flat + 17, 16);
    switch (v->type) {
        case WASM_VALTYPE_I32: memcpy(&v->i32,       flat + 1, 4);  break;
        case WASM_VALTYPE_I64: memcpy(&v->i64,       flat + 1, 8);  break;
        case WASM_VALTYPE_F32: memcpy(&v->f32,       flat + 1, 4);  break;
        case WASM_VALTYPE_F64: memcpy(&v->f64,       flat + 1, 8);  break;
        case WASM_VALTYPE_V128: memcpy(v->v128.bytes, flat + 1, 16); break;
    }
}

static int flat_value_matches(const wasm_value *actual, const uint8_t *flat_exp) {
    uint8_t exp_type = flat_exp[0];
    const uint8_t *exp_data     = flat_exp + 1;
    const uint8_t *exp_nan_mode = flat_exp + 17;
    if ((uint32_t)actual->type != (uint32_t)exp_type) return 0;
    switch (actual->type) {
        case WASM_VALTYPE_V128:
            return v128_matches_flat(actual->v128.bytes, exp_data, exp_nan_mode);
        case WASM_VALTYPE_I32: {
            int32_t ev; memcpy(&ev, exp_data, 4);
            return actual->i32 == ev;
        }
        case WASM_VALTYPE_I64: {
            int64_t ev; memcpy(&ev, exp_data, 8);
            return actual->i64 == ev;
        }
        case WASM_VALTYPE_F32: {
            uint32_t ab, eb;
            memcpy(&ab, &actual->f32, 4); memcpy(&eb, exp_data, 4);
            return ab == eb;
        }
        case WASM_VALTYPE_F64: {
            uint64_t ab, eb;
            memcpy(&ab, &actual->f64, 8); memcpy(&eb, exp_data, 8);
            return ab == eb;
        }
    }
    return 0;
}

/*
 * waste_wast_assert_return: run one assert_return.
 *
 * name_ptr/name_len : exported function name (not null-terminated required)
 * args_ptr          : arg_count flat values (FLAT_VALUE_SIZE bytes each)
 * alts_ptr          : alt_count * result_count flat values
 * Returns 1 on pass, 0 on fail (error in g_error.message).
 */
__attribute__((export_name("waste_wast_assert_return")))
uint32_t waste_wast_assert_return(
        uint32_t name_ptr,  uint32_t name_len,
        uint32_t args_ptr,  uint32_t arg_count,
        uint32_t alts_ptr,  uint32_t alt_count,
        uint32_t result_count) {

    if (!g_engine) {
        memset(&g_error, 0, sizeof(g_error));
        const char *msg = "no module loaded";
        int i = 0;
        while (msg[i] && i < 255) { g_error.message[i] = msg[i]; i++; }
        g_error.message[i] = '\0';
        return 0;
    }

    /* Build null-terminated export name */
    char name[WAST_MAX_EXPORT_NAME];
    if (name_len >= WAST_MAX_EXPORT_NAME) name_len = WAST_MAX_EXPORT_NAME - 1;
    memcpy(name, (const void *)(uintptr_t)name_ptr, name_len);
    name[name_len] = '\0';

    /* Find export */
    uint32_t func_idx = 0;
    memset(&g_error, 0, sizeof(g_error));
    exec_status st = exec_find_export(g_engine, name, &func_idx, &g_error);
    if (st != EXEC_OK) return 0;

    /* Unpack args */
    wasm_value args[WAST_MAX_ARGS];
    uint32_t nargs = arg_count < WAST_MAX_ARGS ? arg_count : WAST_MAX_ARGS;
    const uint8_t *flat_args = (const uint8_t *)(uintptr_t)args_ptr;
    for (uint32_t i = 0; i < nargs; i++)
        unpack_flat_value(flat_args + i * FLAT_VALUE_SIZE, &args[i]);

    /* Invoke */
    wasm_value results[WAST_MAX_RESULTS];
    int nresults = 0;
    memset(&g_error, 0, sizeof(g_error));
    st = exec_invoke(g_engine, func_idx, args, (int)nargs,
                     results, &nresults, &g_error);
    if (st != EXEC_OK) return 0;

    /* No expected results → pass */
    if (alt_count == 0 || result_count == 0) return 1;
    if (nresults < 1) {
        const char *msg = "no result returned";
        int i = 0;
        while (msg[i] && i < 255) { g_error.message[i] = msg[i]; i++; }
        g_error.message[i] = '\0';
        return 0;
    }

    /* Check each alternative */
    const uint8_t *flat_alts = (const uint8_t *)(uintptr_t)alts_ptr;
    uint32_t stride = result_count * FLAT_VALUE_SIZE;
    for (uint32_t a = 0; a < alt_count; a++) {
        const uint8_t *alt = flat_alts + a * stride;
        int all_ok = 1;
        for (uint32_t r = 0; r < result_count && r < (uint32_t)nresults; r++) {
            if (!flat_value_matches(&results[r], alt + r * FLAT_VALUE_SIZE)) {
                all_ok = 0;
                break;
            }
        }
        if (all_ok) return 1;
    }

    /* Mismatch — compose error */
    {
        const char *prefix = "result mismatch: ";
        int pos = 0;
        for (; *prefix && pos < 254; pos++, prefix++) g_error.message[pos] = *prefix;
        for (int k = 0; name[k] && pos < 255; k++, pos++) g_error.message[pos] = name[k];
        g_error.message[pos] = '\0';
    }
    return 0;
}
