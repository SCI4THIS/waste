#include "waste_exec.h"

#ifdef WASTE_FREESTANDING
/* Freestanding build: use only clang built-in headers; implementations
 * are provided by browser_wast.c (bump allocator + stubs). */
#include <stddef.h>
#include <stdint.h>
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void  free(void *p);
int   snprintf(char *buf, size_t n, const char *fmt, ...);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
int   strcmp(const char *a, const char *b);
/* isnan: use compiler builtin; fma/fmaf provided by browser_wast.c */
#define isnan(x)   __builtin_isnan(x)
float  fmaf(float a, float b, float c);
double fma(double a, double b, double c);
#else
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#endif

/* ---- Engine internal constants ---- */

enum {
    EXEC_MAX_TYPES    = 128,
    EXEC_MAX_FUNCS    = 128,
    EXEC_MAX_EXPORTS  = 128,
    EXEC_MAX_NAME     = 128,
    EXEC_MAX_INSTRS   = 4096,
    EXEC_MAX_STACK    = 64,
    EXEC_MAX_LOCALS   = 8,
};

/* ---- Internal instruction representation ---- */

typedef struct {
    uint32_t  opcode;   /* 0x20=local.get, 0xFD=SIMD, 0x0B=end */
    uint32_t  simd_op;  /* for SIMD ops */
    uint32_t  u32_imm;  /* for local.get */
    wasm_v128 v128_imm; /* for v128.const (simd_op==12) */
} exec_instr;

/* ---- Function type ---- */

typedef struct {
    wasm_valtype params[8];
    int          param_count;
    wasm_valtype result;
    int          has_result;
} exec_func_type;

/* ---- Decoded function ---- */

typedef struct {
    uint32_t    type_index;
    exec_instr *code;
    uint32_t    code_size;
} exec_func;

/* ---- Export ---- */

typedef struct {
    char     name[EXEC_MAX_NAME];
    uint32_t func_index;
} exec_export;

/* ---- Engine struct ---- */

struct waste_exec_engine {
    exec_func_type *types;
    uint32_t        type_count;
    exec_func      *funcs;
    uint32_t        func_count;
    exec_export    *exports;
    uint32_t        export_count;
};

/* ---- Error helper ---- */

static exec_status exec_fail(exec_error *error, exec_status status, const char *msg) {
    if (error) {
        error->status = status;
        snprintf(error->message, sizeof(error->message), "%s", msg);
    }
    return status;
}

/* ---- Bounded binary reader ---- */

typedef struct {
    const uint8_t *start;
    const uint8_t *cursor;
    const uint8_t *end;
} exec_reader;

static int er_u8(exec_reader *r, uint8_t *out) {
    if (r->cursor >= r->end) return 0;
    *out = *r->cursor++;
    return 1;
}

static int er_bytes(exec_reader *r, size_t n, const uint8_t **out) {
    if ((size_t)(r->end - r->cursor) < n) return 0;
    *out = r->cursor;
    r->cursor += n;
    return 1;
}

static int er_u32(exec_reader *r, uint32_t *out) {
    uint32_t result = 0;
    unsigned shift = 0;
    for (unsigned i = 0; i < 5; i++) {
        uint8_t b;
        if (!er_u8(r, &b)) return 0;
        if (i == 4 && (b & 0xF0u)) return 0;
        result |= (uint32_t)(b & 0x7Fu) << shift;
        if (!(b & 0x80u)) { *out = result; return 1; }
        shift += 7;
    }
    return 0;
}

/* ---- Type section ---- */

static exec_status parse_types(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t count;
    if (!er_u32(sec, &count))
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid type count");
    if (count > EXEC_MAX_TYPES)
        return exec_fail(err, EXEC_ERROR_FORMAT, "too many types");
    eng->types = (exec_func_type *)calloc(count, sizeof(*eng->types));
    if (count && !eng->types)
        return exec_fail(err, EXEC_ERROR_FORMAT, "type alloc failed");
    eng->type_count = count;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t form;
        uint32_t params, results;
        if (!er_u8(sec, &form) || form != 0x60)
            return exec_fail(err, EXEC_ERROR_FORMAT, "expected func type 0x60");
        if (!er_u32(sec, &params) || params > 8)
            return exec_fail(err, EXEC_ERROR_FORMAT, "too many params");
        eng->types[i].param_count = (int)params;
        for (uint32_t p = 0; p < params; p++) {
            uint8_t pt;
            if (!er_u8(sec, &pt))
                return exec_fail(err, EXEC_ERROR_FORMAT, "truncated param type");
            switch (pt) {
                case 0x7F: eng->types[i].params[p] = WASM_VALTYPE_I32; break;
                case 0x7E: eng->types[i].params[p] = WASM_VALTYPE_I64; break;
                case 0x7D: eng->types[i].params[p] = WASM_VALTYPE_F32; break;
                case 0x7C: eng->types[i].params[p] = WASM_VALTYPE_F64; break;
                case 0x7B: eng->types[i].params[p] = WASM_VALTYPE_V128; break;
                default:
                    return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported param type");
            }
        }
        if (!er_u32(sec, &results) || results > 1)
            return exec_fail(err, EXEC_ERROR_FORMAT, "unsupported result count");
        eng->types[i].has_result = (int)results;
        if (results == 1) {
            uint8_t rt;
            if (!er_u8(sec, &rt))
                return exec_fail(err, EXEC_ERROR_FORMAT, "truncated result type");
            switch (rt) {
                case 0x7F: eng->types[i].result = WASM_VALTYPE_I32; break;
                case 0x7E: eng->types[i].result = WASM_VALTYPE_I64; break;
                case 0x7D: eng->types[i].result = WASM_VALTYPE_F32; break;
                case 0x7C: eng->types[i].result = WASM_VALTYPE_F64; break;
                case 0x7B: eng->types[i].result = WASM_VALTYPE_V128; break;
                default:
                    return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported result type");
            }
        }
    }
    return EXEC_OK;
}

/* ---- Function section ---- */

static exec_status parse_funcs(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t count;
    if (!er_u32(sec, &count))
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid function count");
    if (count > EXEC_MAX_FUNCS)
        return exec_fail(err, EXEC_ERROR_FORMAT, "too many functions");
    eng->funcs = (exec_func *)calloc(count, sizeof(*eng->funcs));
    if (count && !eng->funcs)
        return exec_fail(err, EXEC_ERROR_FORMAT, "func alloc failed");
    eng->func_count = count;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t ti;
        if (!er_u32(sec, &ti) || ti >= eng->type_count)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid type index");
        eng->funcs[i].type_index = ti;
    }
    return EXEC_OK;
}

/* ---- Export section ---- */

static exec_status parse_exports(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t count;
    if (!er_u32(sec, &count))
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid export count");
    if (count > EXEC_MAX_EXPORTS)
        return exec_fail(err, EXEC_ERROR_FORMAT, "too many exports");
    eng->exports = (exec_export *)calloc(count, sizeof(*eng->exports));
    if (count && !eng->exports)
        return exec_fail(err, EXEC_ERROR_FORMAT, "export alloc failed");
    eng->export_count = count;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t name_len;
        const uint8_t *name;
        uint8_t kind;
        uint32_t idx;
        if (!er_u32(sec, &name_len) || name_len >= EXEC_MAX_NAME)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid export name length");
        if (!er_bytes(sec, name_len, &name))
            return exec_fail(err, EXEC_ERROR_FORMAT, "truncated export name");
        if (!er_u8(sec, &kind) || kind != 0)
            return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "non-function export");
        if (!er_u32(sec, &idx) || idx >= eng->func_count)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid export func index");
        memcpy(eng->exports[i].name, name, name_len);
        eng->exports[i].name[name_len] = '\0';
        eng->exports[i].func_index = idx;
    }
    return EXEC_OK;
}

/* ---- Code section ---- */

static exec_status parse_body(exec_func *func, exec_reader *body, exec_error *err) {
    uint32_t local_groups;
    if (!er_u32(body, &local_groups) || local_groups != 0)
        return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "locals not supported");

    /* Allocate instruction buffer based on remaining body size */
    size_t capacity = (size_t)(body->end - body->cursor);
    exec_instr *code = (exec_instr *)calloc(capacity + 1, sizeof(*code));
    if (!code)
        return exec_fail(err, EXEC_ERROR_FORMAT, "code alloc failed");

    uint32_t code_size = 0;
    while (body->cursor < body->end) {
        uint8_t byte;
        if (!er_u8(body, &byte)) {
            free(code);
            return exec_fail(err, EXEC_ERROR_FORMAT, "truncated instruction");
        }
        exec_instr instr;
        memset(&instr, 0, sizeof(instr));
        if (byte == 0x20) {
            uint32_t idx;
            if (!er_u32(body, &idx)) {
                free(code);
                return exec_fail(err, EXEC_ERROR_FORMAT, "truncated local.get");
            }
            instr.opcode  = 0x20;
            instr.u32_imm = idx;
        } else if (byte == 0xFD) {
            uint32_t simd_op;
            if (!er_u32(body, &simd_op)) {
                free(code);
                return exec_fail(err, EXEC_ERROR_FORMAT, "truncated SIMD op");
            }
            instr.opcode  = 0xFD;
            instr.simd_op = simd_op;
            if (simd_op == 12) {
                const uint8_t *imm;
                if (!er_bytes(body, 16, &imm)) {
                    free(code);
                    return exec_fail(err, EXEC_ERROR_FORMAT, "truncated v128.const");
                }
                memcpy(instr.v128_imm.bytes, imm, 16);
            }
        } else if (byte == 0x0B) {
            instr.opcode = 0x0B;
            code[code_size++] = instr;
            break; /* end of function */
        } else {
            free(code);
            return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported opcode");
        }
        if (code_size >= (uint32_t)(capacity + 1)) {
            free(code);
            return exec_fail(err, EXEC_ERROR_FORMAT, "instruction limit exceeded");
        }
        code[code_size++] = instr;
    }

    func->code      = code;
    func->code_size = code_size;
    return EXEC_OK;
}

static exec_status parse_code(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t count;
    if (!er_u32(sec, &count) || count != eng->func_count)
        return exec_fail(err, EXEC_ERROR_FORMAT, "code/function count mismatch");
    for (uint32_t i = 0; i < count; i++) {
        uint32_t body_size;
        const uint8_t *body_bytes;
        if (!er_u32(sec, &body_size) || !er_bytes(sec, body_size, &body_bytes))
            return exec_fail(err, EXEC_ERROR_FORMAT, "truncated function body");
        exec_reader body = { sec->start, body_bytes, body_bytes + body_size };
        exec_status st = parse_body(&eng->funcs[i], &body, err);
        if (st != EXEC_OK) return st;
    }
    return EXEC_OK;
}

/* ---- Module loader ---- */

exec_status exec_load(const uint8_t *bytes, size_t size,
                      waste_exec_engine **eng_out, exec_error *err) {
    if (!bytes || !eng_out)
        return exec_fail(err, EXEC_ERROR_FORMAT, "null input");
    *eng_out = NULL;

    static const uint8_t hdr[8] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
    if (size < 8 || memcmp(bytes, hdr, 8) != 0)
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid Wasm header");

    waste_exec_engine *eng = (waste_exec_engine *)calloc(1, sizeof(*eng));
    if (!eng)
        return exec_fail(err, EXEC_ERROR_FORMAT, "engine alloc failed");

    exec_reader r = { bytes, bytes + 8, bytes + size };
    uint8_t last_section = 0;

    while (r.cursor < r.end) {
        uint8_t section_id;
        uint32_t section_size;
        const uint8_t *section_bytes;
        if (!er_u8(&r, &section_id) || !er_u32(&r, &section_size) ||
            !er_bytes(&r, section_size, &section_bytes)) {
            exec_free(eng);
            return exec_fail(err, EXEC_ERROR_FORMAT, "truncated section");
        }
        if (section_id != 0) {
            if (section_id < last_section) {
                exec_free(eng);
                return exec_fail(err, EXEC_ERROR_FORMAT, "out-of-order section");
            }
            last_section = section_id;
        }
        exec_reader sec = { r.start, section_bytes, section_bytes + section_size };
        exec_status st = EXEC_OK;
        switch (section_id) {
            case 0:  /* custom: skip */ break;
            case 1:  st = parse_types(eng, &sec, err);   break;
            case 3:  st = parse_funcs(eng, &sec, err);   break;
            case 7:  st = parse_exports(eng, &sec, err); break;
            case 10: st = parse_code(eng, &sec, err);    break;
            default:
                /* skip unknown sections */
                break;
        }
        if (st != EXEC_OK) {
            exec_free(eng);
            return st;
        }
    }

    *eng_out = eng;
    return EXEC_OK;
}

void exec_free(waste_exec_engine *eng) {
    if (!eng) return;
    for (uint32_t i = 0; i < eng->func_count; i++)
        free(eng->funcs[i].code);
    free(eng->types);
    free(eng->funcs);
    free(eng->exports);
    free(eng);
}

exec_status exec_find_export(const waste_exec_engine *eng,
                             const char *name, uint32_t *func_idx,
                             exec_error *err) {
    if (!eng || !name || !func_idx)
        return exec_fail(err, EXEC_ERROR_FORMAT, "null argument");
    for (uint32_t i = 0; i < eng->export_count; i++) {
        if (strcmp(eng->exports[i].name, name) == 0) {
            *func_idx = eng->exports[i].func_index;
            return EXEC_OK;
        }
    }
    return exec_fail(err, EXEC_ERROR_NOT_FOUND, "export not found");
}

/* ---- Value stack ---- */

typedef struct {
    wasm_value vals[EXEC_MAX_STACK];
    int        top;
} exec_stack;

static int stack_push(exec_stack *s, wasm_value v) {
    if (s->top >= EXEC_MAX_STACK) return 0;
    s->vals[s->top++] = v;
    return 1;
}

static int stack_pop(exec_stack *s, wasm_value *out) {
    if (s->top <= 0) return 0;
    *out = s->vals[--s->top];
    return 1;
}

/* ---- v128 helpers as byte arrays ---- */

static wasm_value v128_from_bytes(const uint8_t *b) {
    wasm_value v;
    v.type = WASM_VALTYPE_V128;
    memcpy(v.v128.bytes, b, 16);
    return v;
}

/* i8x16.relaxed_laneselect: bitselect */
static wasm_value exec_i8x16_laneselect(wasm_value a, wasm_value b, wasm_value c) {
    uint8_t res[16];
    for (int i = 0; i < 16; i++)
        res[i] = (a.v128.bytes[i] & c.v128.bytes[i]) | (b.v128.bytes[i] & ~c.v128.bytes[i]);
    return v128_from_bytes(res);
}

/* Generic bitselect for other lane widths (same byte-level operation) */
static wasm_value exec_bitselect(wasm_value a, wasm_value b, wasm_value c) {
    uint8_t res[16];
    for (int i = 0; i < 16; i++)
        res[i] = (a.v128.bytes[i] & c.v128.bytes[i]) | (b.v128.bytes[i] & ~c.v128.bytes[i]);
    return v128_from_bytes(res);
}

/* i8x16.relaxed_swizzle: pshufb-style */
static wasm_value exec_i8x16_relaxed_swizzle(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 16; i++) {
        uint8_t idx = b.v128.bytes[i];
        res[i] = (idx < 16) ? a.v128.bytes[idx] : 0;
    }
    return v128_from_bytes(res);
}

/* i8x16.eq: byte-wise equality */
static wasm_value exec_i8x16_eq(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 16; i++)
        res[i] = (a.v128.bytes[i] == b.v128.bytes[i]) ? 0xFF : 0x00;
    return v128_from_bytes(res);
}

/* i16x8.eq: 16-bit lane equality */
static wasm_value exec_i16x8_eq(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 8; i++) {
        uint16_t av, bv;
        memcpy(&av, &a.v128.bytes[i*2], 2);
        memcpy(&bv, &b.v128.bytes[i*2], 2);
        uint16_t r = (av == bv) ? 0xFFFF : 0x0000;
        memcpy(&res[i*2], &r, 2);
    }
    return v128_from_bytes(res);
}

/* i32x4.eq: 32-bit lane equality */
static wasm_value exec_i32x4_eq(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        uint32_t av, bv;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        memcpy(&bv, &b.v128.bytes[i*4], 4);
        uint32_t r = (av == bv) ? 0xFFFFFFFFu : 0u;
        memcpy(&res[i*4], &r, 4);
    }
    return v128_from_bytes(res);
}

/* i64x2.eq */
static wasm_value exec_i64x2_eq(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        uint64_t av, bv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        memcpy(&bv, &b.v128.bytes[i*8], 8);
        uint64_t r = (av == bv) ? UINT64_MAX : 0u;
        memcpy(&res[i*8], &r, 8);
    }
    return v128_from_bytes(res);
}

/* f32x4.eq */
static wasm_value exec_f32x4_eq(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        float av, bv;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        memcpy(&bv, &b.v128.bytes[i*4], 4);
        uint32_t r = (av == bv) ? 0xFFFFFFFFu : 0u;
        memcpy(&res[i*4], &r, 4);
    }
    return v128_from_bytes(res);
}

/* f64x2.eq */
static wasm_value exec_f64x2_eq(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        double av, bv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        memcpy(&bv, &b.v128.bytes[i*8], 8);
        uint64_t r = (av == bv) ? UINT64_MAX : 0u;
        memcpy(&res[i*8], &r, 8);
    }
    return v128_from_bytes(res);
}

/* f32x4.relaxed_min: if either is NaN, result is NaN; else fmin */
static wasm_value exec_f32x4_relaxed_min(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        float av, bv, rv;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        memcpy(&bv, &b.v128.bytes[i*4], 4);
        if (isnan(av) || isnan(bv)) {
            rv = av; /* return first arg on NaN */
        } else {
            rv = av < bv ? av : bv;
            /* handle -0.0 vs +0.0: min should prefer -0.0 */
            if (av == 0.0f && bv == 0.0f) {
                uint32_t ai, bi;
                memcpy(&ai, &av, 4); memcpy(&bi, &bv, 4);
                if ((ai | bi) & 0x80000000u) {
                    uint32_t neg = 0x80000000u;
                    memcpy(&rv, &neg, 4);
                }
            }
        }
        memcpy(&res[i*4], &rv, 4);
    }
    return v128_from_bytes(res);
}

/* f32x4.relaxed_max */
static wasm_value exec_f32x4_relaxed_max(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        float av, bv, rv;
        uint32_t ai, bi;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        memcpy(&bv, &b.v128.bytes[i*4], 4);
        memcpy(&ai, &a.v128.bytes[i*4], 4);
        memcpy(&bi, &b.v128.bytes[i*4], 4);
        if (isnan(av) || isnan(bv)) {
            rv = av;
        } else if (av == 0.0f && bv == 0.0f) {
            /* Both zeros: return first arg (deterministic choice) */
            rv = av;
        } else {
            rv = av > bv ? av : bv;
        }
        memcpy(&res[i*4], &rv, 4);
    }
    return v128_from_bytes(res);
}

/* f64x2.relaxed_min */
static wasm_value exec_f64x2_relaxed_min(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        double av, bv, rv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        memcpy(&bv, &b.v128.bytes[i*8], 8);
        if (isnan(av) || isnan(bv)) {
            rv = av;
        } else {
            rv = av < bv ? av : bv;
            if (av == 0.0 && bv == 0.0) {
                uint64_t ai, bi;
                memcpy(&ai, &av, 8); memcpy(&bi, &bv, 8);
                if ((ai | bi) & 0x8000000000000000ULL) {
                    uint64_t neg = 0x8000000000000000ULL;
                    memcpy(&rv, &neg, 8);
                }
            }
        }
        memcpy(&res[i*8], &rv, 8);
    }
    return v128_from_bytes(res);
}

/* f64x2.relaxed_max */
static wasm_value exec_f64x2_relaxed_max(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        double av, bv, rv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        memcpy(&bv, &b.v128.bytes[i*8], 8);
        if (isnan(av) || isnan(bv)) {
            rv = av;
        } else {
            rv = av > bv ? av : bv;
            if (av == 0.0 && bv == 0.0) {
                /* Both zeros: return first arg (deterministic choice) */
                rv = av;
            }
        }
        memcpy(&res[i*8], &rv, 8);
    }
    return v128_from_bytes(res);
}

/* f32x4.relaxed_madd: fmaf(a, b, c) */
static wasm_value exec_f32x4_relaxed_madd(wasm_value a, wasm_value b, wasm_value c) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        float av, bv, cv, rv;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        memcpy(&bv, &b.v128.bytes[i*4], 4);
        memcpy(&cv, &c.v128.bytes[i*4], 4);
        rv = fmaf(av, bv, cv);
        memcpy(&res[i*4], &rv, 4);
    }
    return v128_from_bytes(res);
}

/* f32x4.relaxed_nmadd: fmaf(-a, b, c) */
static wasm_value exec_f32x4_relaxed_nmadd(wasm_value a, wasm_value b, wasm_value c) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        float av, bv, cv, rv;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        memcpy(&bv, &b.v128.bytes[i*4], 4);
        memcpy(&cv, &c.v128.bytes[i*4], 4);
        rv = fmaf(-av, bv, cv);
        memcpy(&res[i*4], &rv, 4);
    }
    return v128_from_bytes(res);
}

/* f64x2.relaxed_madd: fma(a, b, c) */
static wasm_value exec_f64x2_relaxed_madd(wasm_value a, wasm_value b, wasm_value c) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        double av, bv, cv, rv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        memcpy(&bv, &b.v128.bytes[i*8], 8);
        memcpy(&cv, &c.v128.bytes[i*8], 8);
        rv = fma(av, bv, cv);
        memcpy(&res[i*8], &rv, 8);
    }
    return v128_from_bytes(res);
}

/* f64x2.relaxed_nmadd: fma(-a, b, c) */
static wasm_value exec_f64x2_relaxed_nmadd(wasm_value a, wasm_value b, wasm_value c) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        double av, bv, cv, rv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        memcpy(&bv, &b.v128.bytes[i*8], 8);
        memcpy(&cv, &c.v128.bytes[i*8], 8);
        rv = fma(-av, bv, cv);
        memcpy(&res[i*8], &rv, 8);
    }
    return v128_from_bytes(res);
}

/* i32x4.relaxed_trunc_f32x4_s: saturating signed trunc */
static wasm_value exec_i32x4_relaxed_trunc_f32x4_s(wasm_value a) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        float av;
        int32_t rv;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        if (isnan(av) || av < -2147483648.0f) rv = INT32_MIN;
        else if (av >= 2147483648.0f) rv = INT32_MAX;
        else rv = (int32_t)av;
        memcpy(&res[i*4], &rv, 4);
    }
    return v128_from_bytes(res);
}

/* i32x4.relaxed_trunc_f32x4_u */
static wasm_value exec_i32x4_relaxed_trunc_f32x4_u(wasm_value a) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        float av;
        uint32_t rv;
        memcpy(&av, &a.v128.bytes[i*4], 4);
        if (isnan(av) || av < 0.0f) rv = 0;
        else if (av >= 4294967296.0f) rv = UINT32_MAX;
        else rv = (uint32_t)av;
        memcpy(&res[i*4], &rv, 4);
    }
    return v128_from_bytes(res);
}

/* i32x4.relaxed_trunc_f64x2_s_zero */
static wasm_value exec_i32x4_relaxed_trunc_f64x2_s_zero(wasm_value a) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        double av;
        int32_t rv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        if (isnan(av) || av < -2147483648.0) rv = INT32_MIN;
        else if (av >= 2147483648.0) rv = INT32_MAX;
        else rv = (int32_t)av;
        memcpy(&res[i*4], &rv, 4);
    }
    /* upper two lanes are zero */
    memset(&res[8], 0, 8);
    return v128_from_bytes(res);
}

/* i32x4.relaxed_trunc_f64x2_u_zero */
static wasm_value exec_i32x4_relaxed_trunc_f64x2_u_zero(wasm_value a) {
    uint8_t res[16];
    for (int i = 0; i < 2; i++) {
        double av;
        uint32_t rv;
        memcpy(&av, &a.v128.bytes[i*8], 8);
        if (isnan(av) || av < 0.0) rv = 0;
        else if (av >= 4294967296.0) rv = UINT32_MAX;
        else rv = (uint32_t)av;
        memcpy(&res[i*4], &rv, 4);
    }
    memset(&res[8], 0, 8);
    return v128_from_bytes(res);
}

/* i16x8.relaxed_q15mulr_s: saturating q15 multiply */
static wasm_value exec_i16x8_relaxed_q15mulr_s(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 8; i++) {
        int16_t av, bv;
        int32_t prod;
        int16_t rv;
        memcpy(&av, &a.v128.bytes[i*2], 2);
        memcpy(&bv, &b.v128.bytes[i*2], 2);
        prod = ((int32_t)av * (int32_t)bv + 0x4000) >> 15;
        if (prod > 32767) rv = 32767;
        else if (prod < -32768) rv = -32768;
        else rv = (int16_t)prod;
        memcpy(&res[i*2], &rv, 2);
    }
    return v128_from_bytes(res);
}

/* i16x8.relaxed_dot_i8x16_i7x16_s: signed dot product */
static wasm_value exec_i16x8_relaxed_dot_i8x16_i7x16_s(wasm_value a, wasm_value b) {
    uint8_t res[16];
    for (int i = 0; i < 8; i++) {
        int8_t  a0 = (int8_t)a.v128.bytes[i*2];
        int8_t  a1 = (int8_t)a.v128.bytes[i*2+1];
        int8_t  b0 = (int8_t)b.v128.bytes[i*2];
        int8_t  b1 = (int8_t)b.v128.bytes[i*2+1];
        int32_t dot = (int32_t)a0 * (int32_t)b0 + (int32_t)a1 * (int32_t)b1;
        int16_t rv = (dot > 32767) ? 32767 : (dot < -32768) ? -32768 : (int16_t)dot;
        memcpy(&res[i*2], &rv, 2);
    }
    return v128_from_bytes(res);
}

/* i32x4.relaxed_dot_i8x16_i7x16_add_s */
static wasm_value exec_i32x4_relaxed_dot_i8x16_i7x16_add_s(wasm_value a, wasm_value b, wasm_value c) {
    uint8_t res[16];
    for (int i = 0; i < 4; i++) {
        int32_t acc = 0;
        int32_t cv;
        memcpy(&cv, &c.v128.bytes[i*4], 4);
        for (int j = 0; j < 4; j++) {
            int8_t av = (int8_t)a.v128.bytes[i*4+j];
            int8_t bv = (int8_t)b.v128.bytes[i*4+j];
            acc += (int32_t)av * (int32_t)bv;
        }
        acc += cv;
        memcpy(&res[i*4], &acc, 4);
    }
    return v128_from_bytes(res);
}

/* ---- Invoke ---- */

exec_status exec_invoke(waste_exec_engine *eng,
                        uint32_t func_idx,
                        const wasm_value *args, int arg_count,
                        wasm_value *results, int *result_count,
                        exec_error *err) {
    if (!eng || func_idx >= eng->func_count)
        return exec_fail(err, EXEC_ERROR_NOT_FOUND, "function index out of range");

    exec_func      *func = &eng->funcs[func_idx];
    exec_func_type *type = &eng->types[func->type_index];

    /* Validate arg count */
    if (arg_count != type->param_count)
        return exec_fail(err, EXEC_ERROR_TRAP, "argument count mismatch");

    /* Set up locals from args */
    wasm_value locals[EXEC_MAX_LOCALS];
    memset(locals, 0, sizeof(locals));
    for (int i = 0; i < arg_count && i < EXEC_MAX_LOCALS; i++)
        locals[i] = args[i];

    exec_stack stack;
    stack.top = 0;

    for (uint32_t pc = 0; pc < func->code_size; pc++) {
        exec_instr *instr = &func->code[pc];

        if (instr->opcode == 0x0B) {
            /* end of function */
            break;
        }

        if (instr->opcode == 0x20) {
            /* local.get */
            if (instr->u32_imm >= (uint32_t)arg_count)
                return exec_fail(err, EXEC_ERROR_TRAP, "local.get out of range");
            if (!stack_push(&stack, locals[instr->u32_imm]))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0xFD) {
            uint32_t op = instr->simd_op;

            if (op == 12) {
                /* v128.const */
                wasm_value v;
                v.type = WASM_VALTYPE_V128;
                memcpy(v.v128.bytes, instr->v128_imm.bytes, 16);
                if (!stack_push(&stack, v))
                    return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                continue;
            }

            /* SIMD ops that take operands from the stack */
            wasm_value a, b, c;
            memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b)); memset(&c, 0, sizeof(c));

            switch (op) {
                /* Unary ops */
                case 257: /* i32x4.relaxed_trunc_f32x4_s */
                    if (!stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i32x4_relaxed_trunc_f32x4_s(a)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 258: /* i32x4.relaxed_trunc_f32x4_u */
                    if (!stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i32x4_relaxed_trunc_f32x4_u(a)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 259: /* i32x4.relaxed_trunc_f64x2_s_zero */
                    if (!stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i32x4_relaxed_trunc_f64x2_s_zero(a)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 260: /* i32x4.relaxed_trunc_f64x2_u_zero */
                    if (!stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i32x4_relaxed_trunc_f64x2_u_zero(a)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;

                /* Binary ops */
                case 256: /* i8x16.relaxed_swizzle */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i8x16_relaxed_swizzle(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 269: /* f32x4.relaxed_min */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f32x4_relaxed_min(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 270: /* f32x4.relaxed_max */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f32x4_relaxed_max(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 271: /* f64x2.relaxed_min */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f64x2_relaxed_min(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 272: /* f64x2.relaxed_max */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f64x2_relaxed_max(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 273: /* i16x8.relaxed_q15mulr_s */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i16x8_relaxed_q15mulr_s(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 274: /* i16x8.relaxed_dot_i8x16_i7x16_s */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i16x8_relaxed_dot_i8x16_i7x16_s(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;

                /* Ternary ops */
                case 261: /* f32x4.relaxed_madd */
                    if (!stack_pop(&stack, &c) || !stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f32x4_relaxed_madd(a, b, c)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 262: /* f32x4.relaxed_nmadd */
                    if (!stack_pop(&stack, &c) || !stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f32x4_relaxed_nmadd(a, b, c)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 263: /* f64x2.relaxed_madd */
                    if (!stack_pop(&stack, &c) || !stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f64x2_relaxed_madd(a, b, c)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 264: /* f64x2.relaxed_nmadd */
                    if (!stack_pop(&stack, &c) || !stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f64x2_relaxed_nmadd(a, b, c)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 265: /* i8x16.relaxed_laneselect */
                    if (!stack_pop(&stack, &c) || !stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i8x16_laneselect(a, b, c)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 266: /* i16x8.relaxed_laneselect */
                case 267: /* i32x4.relaxed_laneselect */
                case 268: /* i64x2.relaxed_laneselect */
                    if (!stack_pop(&stack, &c) || !stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_bitselect(a, b, c)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 275: /* i32x4.relaxed_dot_i8x16_i7x16_add_s */
                    if (!stack_pop(&stack, &c) || !stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i32x4_relaxed_dot_i8x16_i7x16_add_s(a, b, c)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;

                /* Eq ops */
                case 35: /* i8x16.eq */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i8x16_eq(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 37: /* i16x8.eq */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i16x8_eq(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 39: /* i32x4.eq */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i32x4_eq(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 214: /* i64x2.eq */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_i64x2_eq(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 65: /* f32x4.eq */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f32x4_eq(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;
                case 71: /* f64x2.eq */
                    if (!stack_pop(&stack, &b) || !stack_pop(&stack, &a))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack underflow");
                    if (!stack_push(&stack, exec_f64x2_eq(a, b)))
                        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                    break;

                default: {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "unsupported SIMD op %u", op);
                    return exec_fail(err, EXEC_ERROR_UNSUPPORTED, msg);
                }
            }
            continue;
        }

        return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported opcode");
    }

    /* Collect results */
    if (type->has_result) {
        wasm_value rv;
        if (!stack_pop(&stack, &rv))
            return exec_fail(err, EXEC_ERROR_TRAP, "missing result");
        if (results) results[0] = rv;
        if (result_count) *result_count = 1;
    } else {
        if (result_count) *result_count = 0;
    }

    return EXEC_OK;
}
