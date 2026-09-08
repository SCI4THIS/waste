#include "waste_exec.h"
#include "wast_simd.h"

#ifdef WASTE_FREESTANDING
/* Freestanding build: use only clang built-in headers; implementations
 * are provided by browser_wast.c (bump allocator + stubs). */
#include <stddef.h>
#include <stdint.h>
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void  free(void *p);
int   snprintf(char *buf, size_t n, const char *fmt, ...);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
int   strcmp(const char *a, const char *b);
/* isnan/isinf: use compiler builtins */
#define isnan(x)   __builtin_isnan(x)
#define isinf(x)   __builtin_isinf(x)
/* math functions provided by browser_wast.c */
float  fmaf(float a, float b, float c);
double fma(double a, double b, double c);
float  fabsf(float x);
float  ceilf(float x);
float  floorf(float x);
float  truncf(float x);
float  nearbyintf(float x);
float  sqrtf(float x);
double fabs(double x);
double ceil(double x);
double floor(double x);
double trunc(double x);
double nearbyint(double x);
double sqrt(double x);
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
    EXEC_MAX_FUNCS    = WAST_MAX_FUNCS,
    EXEC_MAX_EXPORTS  = 65536,
    EXEC_MAX_NAME     = WAST_MAX_EXPORT_NAME,
    EXEC_MAX_INSTRS   = 4096,
    EXEC_MAX_STACK    = 256,
    EXEC_MAX_LOCALS   = WAST_MAX_LOCALS,
    EXEC_MAX_CONTROL  = 64,
    EXEC_MAX_CALL_DEPTH = 256,
    EXEC_MAX_GLOBALS  = 128,
    EXEC_MAX_TABLES   = 16,
    EXEC_PAGE_SIZE    = 65536,
};

/* ---- Internal instruction representation ---- */

typedef struct {
    uint8_t kind;
    uint32_t tag_index;
    uint32_t depth;
} exec_catch;

typedef struct {
    uint32_t  opcode;   /* 0x20=local.get, 0xFD=SIMD, 0x0B=end */
    uint32_t  simd_op;  /* for SIMD ops */
    uint32_t  u32_imm;  /* for local.get */
    uint32_t  memory_index; /* memory selected by a memarg */
    uint32_t  source_memory_index; /* source memory for memory.copy */
    uint32_t  alignment; /* memarg alignment exponent */
    uint32_t  lane_index; /* SIMD lane immediate */
    wasm_v128 v128_imm; /* for v128.const (simd_op==12) */
    int32_t   block_type_index; /* >=0 for a block type use, -1 otherwise */
    wasm_valtype block_result_type; /* direct single-result block type */
    uint8_t   has_block_result_type;
    exec_catch *catches;
    uint32_t catch_count;
} exec_instr;

/* ---- Function type ---- */

typedef struct {
    wasm_valtype params[EXEC_MAX_LOCALS];
    wasm_valtype results[WAST_MAX_RESULTS];
    int          param_count;
    int          result_count;
    wast_type_kind kind;
    uint32_t rec_group_start;
    uint32_t rec_group_size;
    wasm_valtype fields[WAST_MAX_TYPE_FIELDS];
    uint8_t field_mutable[WAST_MAX_TYPE_FIELDS];
    uint8_t field_packed[WAST_MAX_TYPE_FIELDS];
    int field_count;
} exec_func_type;

/* ---- Decoded function ---- */

typedef struct {
    uint32_t    type_index;
    wasm_valtype locals[EXEC_MAX_LOCALS];
    uint32_t    local_count;
    exec_instr *code;
    uint32_t    code_size;
} exec_func;

/* ---- Export ---- */

typedef struct {
    char     name[EXEC_MAX_NAME];
    uint32_t index;
    uint8_t  kind;
} exec_export;

static int valid_utf8(const uint8_t *bytes, size_t length) {
    size_t i = 0;
    while (i < length) {
        uint8_t first = bytes[i++];
        if (first <= 0x7f) continue;
        if (first >= 0xc2 && first <= 0xdf) {
            if (i >= length || bytes[i] < 0x80 || bytes[i] > 0xbf) return 0;
            i++;
        } else if (first >= 0xe0 && first <= 0xef) {
            if (i + 1 >= length) return 0;
            uint8_t second = bytes[i], third = bytes[i + 1];
            if (third < 0x80 || third > 0xbf ||
                (first == 0xe0 && (second < 0xa0 || second > 0xbf)) ||
                (first == 0xed && (second < 0x80 || second > 0x9f)) ||
                (first != 0xe0 && first != 0xed &&
                 (second < 0x80 || second > 0xbf))) return 0;
            i += 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            if (i + 2 >= length) return 0;
            uint8_t second = bytes[i], third = bytes[i + 1], fourth = bytes[i + 2];
            if (third < 0x80 || third > 0xbf || fourth < 0x80 || fourth > 0xbf ||
                (first == 0xf0 && (second < 0x90 || second > 0xbf)) ||
                (first == 0xf4 && (second < 0x80 || second > 0x8f)) ||
                (first != 0xf0 && first != 0xf4 &&
                 (second < 0x80 || second > 0xbf))) return 0;
            i += 3;
        } else return 0;
    }
    return 1;
}

/* ---- Engine struct ---- */

struct waste_exec_engine {
    exec_func_type *types;
    uint32_t        type_count;
    exec_func      *funcs;
    uint32_t        func_count;
    uint32_t        import_func_count;
    uint32_t        import_func_types[EXEC_MAX_FUNCS];
    exec_host_func  import_funcs[EXEC_MAX_FUNCS];
    void           *import_host_data[EXEC_MAX_FUNCS];
    exec_export    *exports;
    uint32_t        export_count;
    exec_global    *globals[EXEC_MAX_GLOBALS];
    exec_global     owned_globals[EXEC_MAX_GLOBALS];
    uint32_t        global_count;
    uint32_t        import_global_count;
    exec_memory    *memories[WAST_MAX_MEMORIES];
    exec_memory     owned_memories[WAST_MAX_MEMORIES];
    uint8_t         owns_memories[WAST_MAX_MEMORIES];
    uint32_t        memory_count;
    uint32_t        import_memory_count;
    /* Compatibility alias for the default memory used by instructions whose
     * binary form has no explicit memory index. */
    exec_memory    *memory;
    exec_table     *tables[EXEC_MAX_TABLES];
    exec_table      owned_tables[EXEC_MAX_TABLES];
    uint32_t        table_count;
    uint32_t        import_table_count;
    exec_tag       *tags[WAST_MAX_TAGS];
    exec_tag        owned_tags[WAST_MAX_TAGS];
    uint32_t        tag_types[WAST_MAX_TAGS];
    uint32_t        tag_count;
    uint32_t        import_tag_count;
    uint32_t        start_func;
    int             has_start;
    uint32_t        declared_data_count;
    uint8_t         has_data_count;
    uint8_t         uses_data_count_instruction;
    uint8_t         declared_funcs[EXEC_MAX_FUNCS];
    exec_table_element *elem_values[WAST_MAX_ELEM_SEGS];
    uint32_t        elem_lengths[WAST_MAX_ELEM_SEGS];
    wasm_valtype    elem_types[WAST_MAX_ELEM_SEGS];
    uint8_t         elem_dropped[WAST_MAX_ELEM_SEGS];
    uint32_t        elem_count;
    /* Passive data segments (for memory.init / data.drop) */
    uint8_t        *data_segs[WAST_MAX_DATA_SEGS];
    uint32_t        data_seg_lengths[WAST_MAX_DATA_SEGS];
    uint8_t         data_dropped[WAST_MAX_DATA_SEGS];
    uint32_t        data_count;
    uint8_t         instantiation_trapped;
    char            instantiation_error[256];
    wasm_value      *local_frames[EXEC_MAX_CALL_DEPTH];
    uint32_t         local_frame_capacities[EXEC_MAX_CALL_DEPTH];
};

static int value_type_is_defined(const waste_exec_engine *eng,
                                 wasm_valtype type) {
    return !WASM_VALTYPE_IS_TYPE_REF(type) ||
           WASM_VALTYPE_TYPE_REF_INDEX(type) < eng->type_count;
}

typedef struct {
    const waste_exec_engine *left_engine;
    const waste_exec_engine *right_engine;
    uint32_t left_group;
    uint32_t right_group;
} exec_type_pair;

typedef struct {
    exec_type_pair pairs[WAST_MAX_TYPES];
    uint32_t count;
} exec_type_compare;

static int same_value_type_ctx(const waste_exec_engine *left_engine,
                               wasm_valtype left,
                               const waste_exec_engine *right_engine,
                               wasm_valtype right,
                               exec_type_compare *compare);

static int same_type_index_ctx(const waste_exec_engine *left_engine,
                               uint32_t left_index,
                               const waste_exec_engine *right_engine,
                               uint32_t right_index,
                               exec_type_compare *compare) {
    if (!left_engine || !right_engine ||
        left_index >= left_engine->type_count ||
        right_index >= right_engine->type_count)
        return 0;
    const exec_func_type *left = &left_engine->types[left_index];
    const exec_func_type *right = &right_engine->types[right_index];
    uint32_t left_start = left->rec_group_start;
    uint32_t right_start = right->rec_group_start;
    if (!left->rec_group_size || !right->rec_group_size ||
        left_start + left->rec_group_size > left_engine->type_count ||
        right_start + right->rec_group_size > right_engine->type_count ||
        left->rec_group_size != right->rec_group_size ||
        left_index - left_start != right_index - right_start)
        return 0;

    for (uint32_t i = 0; i < compare->count; i++) {
        const exec_type_pair *p = &compare->pairs[i];
        if (p->left_engine == left_engine && p->right_engine == right_engine &&
            p->left_group == left_start && p->right_group == right_start)
            return 1;
        /* Structural equivalence is symmetric */
        if (p->left_engine == right_engine && p->right_engine == left_engine &&
            p->left_group == right_start && p->right_group == left_start)
            return 1;
    }
    if (compare->count >= WAST_MAX_TYPES) return 0;
    compare->pairs[compare->count].left_engine = left_engine;
    compare->pairs[compare->count].right_engine = right_engine;
    compare->pairs[compare->count].left_group = left_start;
    compare->pairs[compare->count].right_group = right_start;
    compare->count++;

    for (uint32_t member = 0; member < left->rec_group_size; member++) {
        const exec_func_type *a = &left_engine->types[left_start + member];
        const exec_func_type *b = &right_engine->types[right_start + member];
        if (a->kind != b->kind) return 0;
        if (a->kind == WAST_TYPE_FUNC) {
            if (a->param_count != b->param_count ||
                a->result_count != b->result_count)
                return 0;
            for (int i = 0; i < a->param_count; i++)
                if (!same_value_type_ctx(left_engine, a->params[i],
                                         right_engine, b->params[i], compare))
                    return 0;
            for (int i = 0; i < a->result_count; i++)
                if (!same_value_type_ctx(left_engine, a->results[i],
                                         right_engine, b->results[i], compare))
                    return 0;
        } else {
            if (a->field_count != b->field_count) return 0;
            for (int i = 0; i < a->field_count; i++)
                if (a->field_mutable[i] != b->field_mutable[i] ||
                    a->field_packed[i] != b->field_packed[i] ||
                    !same_value_type_ctx(left_engine, a->fields[i],
                                         right_engine, b->fields[i], compare))
                    return 0;
        }
    }
    return 1;
}

static int same_value_type_ctx(const waste_exec_engine *left_engine,
                               wasm_valtype left,
                               const waste_exec_engine *right_engine,
                               wasm_valtype right,
                               exec_type_compare *compare) {
    int left_indexed = WASM_VALTYPE_IS_TYPE_REF(left);
    int right_indexed = WASM_VALTYPE_IS_TYPE_REF(right);
    if (!left_indexed || !right_indexed) return left == right;
    if (((unsigned)left < WASM_VALTYPE_TYPE_REF_BASE) !=
        ((unsigned)right < WASM_VALTYPE_TYPE_REF_BASE))
        return 0;
    uint32_t li = WASM_VALTYPE_TYPE_REF_INDEX(left);
    uint32_t ri = WASM_VALTYPE_TYPE_REF_INDEX(right);
    /* When comparing inside a rec group, check whether each type ref is
     * intra-group (referencing a member of a group currently being compared)
     * or extra-group.  Intra-group refs must match by relative position;
     * mixed intra/extra is a mismatch. */
    for (uint32_t p = 0; p < compare->count; p++) {
        const exec_type_pair *pair = &compare->pairs[p];
        int l_intra = (left_engine == pair->left_engine &&
                       li >= pair->left_group &&
                       li < pair->left_group +
                            pair->left_engine->types[pair->left_group].rec_group_size);
        int r_intra = (right_engine == pair->right_engine &&
                       ri >= pair->right_group &&
                       ri < pair->right_group +
                            pair->right_engine->types[pair->right_group].rec_group_size);
        if (l_intra || r_intra) {
            if (l_intra != r_intra) return 0;
            return (li - pair->left_group) == (ri - pair->right_group);
        }
    }
    return same_type_index_ctx(left_engine, li, right_engine, ri, compare);
}

static int same_value_type(const waste_exec_engine *left_engine, wasm_valtype left,
                           const waste_exec_engine *right_engine, wasm_valtype right,
                           unsigned depth) {
    exec_type_compare compare = {0};
    (void)depth;
    return same_value_type_ctx(left_engine, left, right_engine, right, &compare);
}

static int same_func_type(const waste_exec_engine *left_engine, uint32_t left_index,
                          const waste_exec_engine *right_engine, uint32_t right_index) {
    exec_type_compare compare = {0};
    if (!left_engine || !right_engine ||
        left_index >= left_engine->type_count ||
        right_index >= right_engine->type_count ||
        left_engine->types[left_index].kind != WAST_TYPE_FUNC ||
        right_engine->types[right_index].kind != WAST_TYPE_FUNC)
        return 0;
    return same_type_index_ctx(left_engine, left_index, right_engine, right_index,
                               &compare);
}

/* Check if 'actual' value type is a subtype of 'required' value type.
 * For mutable globals, use same_value_type (exact structural equality).
 * For immutable globals, applies Wasm GC subtype rules:
 *   FUNCREF_NONNULL    <: FUNCREF
 *   EXTERNREF_NONNULL  <: EXTERNREF
 *   (ref null T)       <: (ref null func)    for any func heap type T
 *   (ref T)            <: (ref null func)    for any func heap type T
 *   (ref T)            <: (ref func)         for any func heap type T
 *   (ref T)            <: (ref null T)       (non-null subtype of nullable, same type)
 */
static int global_type_is_compat(const waste_exec_engine *aeng, wasm_valtype actual,
                                  const waste_exec_engine *reng, wasm_valtype required,
                                  int mutable_) {
    if (mutable_) return same_value_type(aeng, actual, reng, required, 0);
    /* Structural equality covers exact-match cases */
    if (same_value_type(aeng, actual, reng, required, 0)) return 1;
    /* Non-null builtins <: nullable counterparts */
    if (actual == WASM_VALTYPE_FUNCREF_NONNULL && required == WASM_VALTYPE_FUNCREF) return 1;
    if (actual == WASM_VALTYPE_EXTERNREF_NONNULL && required == WASM_VALTYPE_EXTERNREF) return 1;
    /* Bottom heap types are subtypes of the corresponding nullable reference
     * type.  Runtime assertion arguments retain these precise types so typed
     * select must accept them just as the validator does. */
    if (actual == WASM_VALTYPE_NULLFUNCREF && required == WASM_VALTYPE_FUNCREF) return 1;
    if (actual == WASM_VALTYPE_NULLFUNCREF && WASM_VALTYPE_IS_TYPE_REF(required))
        return reng && WASM_VALTYPE_TYPE_REF_INDEX(required) < reng->type_count;
    if (actual == WASM_VALTYPE_NULLEXTERNREF &&
        required == WASM_VALTYPE_EXTERNREF) return 1;
    if (actual == WASM_VALTYPE_NULLEXNREF &&
        required == WASM_VALTYPE_EXNREF) return 1;
    if (actual == WASM_VALTYPE_EXNREF_NONNULL &&
        required == WASM_VALTYPE_EXNREF) return 1;
    if (actual == WASM_VALTYPE_NULLREF &&
        (required == WASM_VALTYPE_ANYREF || required == WASM_VALTYPE_EQREF ||
         required == WASM_VALTYPE_I31REF || required == WASM_VALTYPE_STRUCTREF ||
         required == WASM_VALTYPE_ARRAYREF)) return 1;
    int a_isref = WASM_VALTYPE_IS_TYPE_REF(actual);
    int r_isref = WASM_VALTYPE_IS_TYPE_REF(required);
    /* Any (ref null T) or (ref T) where T is a func type <: (ref null func) */
    if (a_isref && required == WASM_VALTYPE_FUNCREF) return 1;
    /* (ref T) <: (ref func) */
    if (a_isref && required == WASM_VALTYPE_FUNCREF_NONNULL)
        return (unsigned)actual >= WASM_VALTYPE_TYPE_REF_BASE;
    /* (ref T) <: (ref null T) — non-null subtype of nullable, same structural type */
    if (a_isref && r_isref &&
        (unsigned)actual >= WASM_VALTYPE_TYPE_REF_BASE &&
        (unsigned)required < WASM_VALTYPE_TYPE_REF_BASE) {
        wasm_valtype actual_nullable = (wasm_valtype)((unsigned)actual - 0x100u);
        return same_value_type(aeng, actual_nullable, reng, required, 0);
    }
    return 0;
}

static const exec_host_import *find_host_import(const exec_imports *imports,
                                                 const char *module, const char *name) {
    if (!imports) return NULL;
    for (size_t i = 0; i < imports->function_count; i++)
        if (strcmp(imports->functions[i].module, module) == 0 &&
            strcmp(imports->functions[i].name, name) == 0) return &imports->functions[i];
    return NULL;
}

static exec_global *find_global_import(const exec_imports *imports, const char *module, const char *name) {
    if (!imports) return NULL;
    for (size_t i=0;i<imports->global_count;i++)
        if (strcmp(imports->globals[i].module,module)==0 && strcmp(imports->globals[i].name,name)==0)
            return imports->globals[i].global;
    return NULL;
}
static exec_memory *find_memory_import(const exec_imports *imports, const char *module, const char *name) {
    if (!imports) return NULL;
    for (size_t i=0;i<imports->memory_count;i++)
        if (strcmp(imports->memories[i].module,module)==0 && strcmp(imports->memories[i].name,name)==0)
            return imports->memories[i].memory;
    return NULL;
}
static exec_table *find_table_import(const exec_imports *imports, const char *module, const char *name) {
    if (!imports) return NULL;
    for (size_t i=0;i<imports->table_count;i++)
        if (strcmp(imports->tables[i].module,module)==0 && strcmp(imports->tables[i].name,name)==0)
            return imports->tables[i].table;
    return NULL;
}
static exec_tag *find_tag_import(const exec_imports *imports, const char *module,
                                 const char *name) {
    if(!imports)return NULL;
    for(size_t i=0;i<imports->tag_count;i++)
        if(strcmp(imports->tags[i].module,module)==0&&
           strcmp(imports->tags[i].name,name)==0)return imports->tags[i].tag;
    return NULL;
}

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

static int er_i32(exec_reader *r, int32_t *out) {
    uint32_t value = 0;
    unsigned shift = 0;
    uint8_t byte = 0;
    for (unsigned i = 0; i < 5; i++) {
        if (!er_u8(r, &byte)) return 0;
        if (i == 4) {
            uint8_t payload = byte & 0x7fu;
            uint8_t extension = (payload & 0x08u) ? 0x70u : 0x00u;
            if ((payload & 0x70u) != extension) return 0;
        }
        value |= (uint32_t)(byte & 0x7fu) << shift;
        shift += 7;
        if (!(byte & 0x80u)) {
            if (shift < 32 && (byte & 0x40u)) value |= UINT32_MAX << shift;
            *out = (int32_t)value;
            return 1;
        }
    }
    return 0;
}

static int er_i64(exec_reader *r, int64_t *out) {
    uint64_t value = 0; unsigned shift = 0; uint8_t byte = 0;
    for (unsigned i = 0; i < 10; i++) {
        if (!er_u8(r, &byte)) return 0;
        if (i == 9) {
            uint8_t payload = byte & 0x7fu;
            uint8_t extension = (payload & 0x01u) ? 0x7eu : 0x00u;
            if ((payload & 0x7eu) != extension) return 0;
        }
        if (shift < 64)
            value |= (uint64_t)(byte & (shift == 63 ? 0x01u : 0x7fu)) << shift;
        shift += 7;
        if (!(byte & 0x80u)) {
            if (shift < 64 && (byte & 0x40u)) value |= UINT64_MAX << shift;
            *out = (int64_t)value; return 1;
        }
    }
    return 0;
}

static int byte_valtype(uint8_t byte, wasm_valtype *out) {
    switch (byte) {
        case 0x7f: *out=WASM_VALTYPE_I32; return 1;
        case 0x7e: *out=WASM_VALTYPE_I64; return 1;
        case 0x7d: *out=WASM_VALTYPE_F32; return 1;
        case 0x7c: *out=WASM_VALTYPE_F64; return 1;
        case 0x7b: *out=WASM_VALTYPE_V128; return 1;
        case 0x70: *out=WASM_VALTYPE_FUNCREF; return 1;
        case 0x6f: *out=WASM_VALTYPE_EXTERNREF; return 1;
        case 0x6e: *out=WASM_VALTYPE_ANYREF; return 1;
        case 0x6d: *out=WASM_VALTYPE_EQREF; return 1;
        case 0x6c: *out=WASM_VALTYPE_I31REF; return 1;
        case 0x6b: *out=WASM_VALTYPE_STRUCTREF; return 1;
        case 0x6a: *out=WASM_VALTYPE_ARRAYREF; return 1;
        case 0x71: *out=WASM_VALTYPE_NULLREF; return 1;
        case 0x73: *out=WASM_VALTYPE_NULLFUNCREF; return 1;
        case 0x74: *out=WASM_VALTYPE_NULLEXNREF; return 1;
        case 0x72: *out=WASM_VALTYPE_NULLEXTERNREF; return 1;
        case 0x69: *out=WASM_VALTYPE_EXNREF; return 1;
        default: return 0;
    }
}

static int er_valtype(exec_reader *r, wasm_valtype *out) {
    uint8_t byte;
    int32_t heap;
    if (!er_u8(r, &byte)) return 0;
    if (byte_valtype(byte, out)) return 1;
    switch (byte) {
        case 0x63: case 0x64:
            if (!er_i32(r,&heap)) return 0;
            if (heap == -16) *out=byte==0x63?WASM_VALTYPE_FUNCREF:WASM_VALTYPE_FUNCREF_NONNULL;
            else if (heap == -17) *out=byte==0x63?WASM_VALTYPE_EXTERNREF:WASM_VALTYPE_EXTERNREF_NONNULL;
            else if (heap == -18) *out=byte==0x63?WASM_VALTYPE_ANYREF:WASM_VALTYPE_ANYREF_NONNULL;
            else if (heap == -19) *out=byte==0x63?WASM_VALTYPE_EQREF:WASM_VALTYPE_EQREF_NONNULL;
            else if (heap == -20) *out=byte==0x63?WASM_VALTYPE_I31REF:WASM_VALTYPE_I31REF_NONNULL;
            else if (heap == -21) *out=byte==0x63?WASM_VALTYPE_STRUCTREF:WASM_VALTYPE_STRUCTREF_NONNULL;
            else if (heap == -22) *out=byte==0x63?WASM_VALTYPE_ARRAYREF:WASM_VALTYPE_ARRAYREF_NONNULL;
            else if (heap == -23) *out=byte==0x63?WASM_VALTYPE_EXNREF:WASM_VALTYPE_EXNREF_NONNULL;
            else if (byte == 0x63 && heap == -15) *out=WASM_VALTYPE_NULLREF;
            else if (byte == 0x63 && heap == -14) *out=WASM_VALTYPE_NULLEXTERNREF;
            else if (byte == 0x63 && heap == -13) *out=WASM_VALTYPE_NULLFUNCREF;
            else if (byte == 0x63 && heap == -12) *out=WASM_VALTYPE_NULLEXNREF;
            else if (heap >= 0 && heap < WAST_MAX_TYPES)
                *out=(wasm_valtype)((byte==0x63?WASM_VALTYPE_TYPE_REF_NULL_BASE:WASM_VALTYPE_TYPE_REF_BASE)+(uint32_t)heap);
            else return 0;
            return 1;
        default: return 0;
    }
}

static int is_reference_type(wasm_valtype type) {
    return ((unsigned)type >= (unsigned)WASM_VALTYPE_FUNCREF &&
            (unsigned)type <= (unsigned)WASM_VALTYPE_NULLEXTERNREF) ||
           WASM_VALTYPE_IS_TYPE_REF(type);
}

static int is_nullable_reference_type(wasm_valtype type) {
    return type == WASM_VALTYPE_FUNCREF ||
           type == WASM_VALTYPE_EXTERNREF ||
           type == WASM_VALTYPE_ANYREF ||
           type == WASM_VALTYPE_EQREF ||
           type == WASM_VALTYPE_I31REF ||
           type == WASM_VALTYPE_STRUCTREF ||
           type == WASM_VALTYPE_ARRAYREF ||
           type == WASM_VALTYPE_EXNREF ||
           type == WASM_VALTYPE_NULLREF ||
           type == WASM_VALTYPE_NULLFUNCREF ||
           type == WASM_VALTYPE_NULLEXNREF ||
           type == WASM_VALTYPE_NULLEXTERNREF ||
           (WASM_VALTYPE_IS_TYPE_REF(type) &&
            (unsigned)type < WASM_VALTYPE_TYPE_REF_BASE);
}

static int is_function_reference_type(wasm_valtype type) {
    return type == WASM_VALTYPE_FUNCREF ||
           type == WASM_VALTYPE_FUNCREF_NONNULL ||
           type == WASM_VALTYPE_NULLFUNCREF ||
           WASM_VALTYPE_IS_TYPE_REF(type);
}

static wasm_valtype nonnullable_reference_type(wasm_valtype type) {
    if (type == WASM_VALTYPE_FUNCREF) return WASM_VALTYPE_FUNCREF_NONNULL;
    if (type == WASM_VALTYPE_EXTERNREF) return WASM_VALTYPE_EXTERNREF_NONNULL;
    if (type == WASM_VALTYPE_ANYREF) return WASM_VALTYPE_ANYREF_NONNULL;
    if (type == WASM_VALTYPE_EQREF) return WASM_VALTYPE_EQREF_NONNULL;
    if (type == WASM_VALTYPE_I31REF) return WASM_VALTYPE_I31REF_NONNULL;
    if (type == WASM_VALTYPE_STRUCTREF) return WASM_VALTYPE_STRUCTREF_NONNULL;
    if (type == WASM_VALTYPE_ARRAYREF) return WASM_VALTYPE_ARRAYREF_NONNULL;
    if (type == WASM_VALTYPE_EXNREF) return WASM_VALTYPE_EXNREF_NONNULL;
    if (WASM_VALTYPE_IS_TYPE_REF(type) &&
        (unsigned)type < WASM_VALTYPE_TYPE_REF_BASE)
        return (wasm_valtype)((unsigned)type + 0x100u);
    return type;
}

/* ---- Type section ---- */

static int parse_storage_type(exec_reader *sec, wasm_valtype *type,
                              uint8_t *packed) {
    uint8_t first;
    if (!er_u8(sec, &first)) return 0;
    if (first == 0x78 || first == 0x77) {
        *type = WASM_VALTYPE_I32;
        *packed = first == 0x78 ? 1 : 2;
        return 1;
    }
    sec->cursor--;
    *packed = 0;
    return er_valtype(sec, type);
}

static exec_status parse_composite_type(waste_exec_engine *eng,
                                        exec_reader *sec, uint8_t form,
                                        uint32_t index,
                                        uint32_t group_start,
                                        uint32_t group_size,
                                        exec_error *err) {
    exec_func_type *type = &eng->types[index];
    type->rec_group_start = group_start;
    type->rec_group_size = group_size;
    if (form == 0x60) {
        uint32_t params, results;
        type->kind = WAST_TYPE_FUNC;
        if (!er_u32(sec, &params) || params > EXEC_MAX_LOCALS)
            return exec_fail(err, EXEC_ERROR_FORMAT, "too many params");
        type->param_count = (int)params;
        for (uint32_t i = 0; i < params; i++)
            if (!er_valtype(sec, &type->params[i]))
                return exec_fail(err, EXEC_ERROR_UNSUPPORTED,
                                 "unsupported param type");
        if (!er_u32(sec, &results) || results > WAST_MAX_RESULTS)
            return exec_fail(err, EXEC_ERROR_FORMAT,
                             "unsupported result count");
        type->result_count = (int)results;
        for (uint32_t i = 0; i < results; i++)
            if (!er_valtype(sec, &type->results[i]))
                return exec_fail(err, EXEC_ERROR_UNSUPPORTED,
                                 "unsupported result type");
        return EXEC_OK;
    }
    if (form == 0x5f) {
        uint32_t fields;
        type->kind = WAST_TYPE_STRUCT;
        if (!er_u32(sec, &fields) || fields > WAST_MAX_TYPE_FIELDS)
            return exec_fail(err, EXEC_ERROR_FORMAT,
                             "invalid struct field count");
        type->field_count = (int)fields;
        for (uint32_t i = 0; i < fields; i++) {
            uint8_t mutable_;
            if (!parse_storage_type(sec, &type->fields[i],
                                    &type->field_packed[i]) ||
                !er_u8(sec, &mutable_) || mutable_ > 1)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid struct field type");
            type->field_mutable[i] = mutable_;
        }
        return EXEC_OK;
    }
    if (form == 0x5e) {
        uint8_t mutable_;
        type->kind = WAST_TYPE_ARRAY;
        type->field_count = 1;
        if (!parse_storage_type(sec, &type->fields[0],
                                &type->field_packed[0]) ||
            !er_u8(sec, &mutable_) || mutable_ > 1)
            return exec_fail(err, EXEC_ERROR_FORMAT,
                             "invalid array field type");
        type->field_mutable[0] = mutable_;
        return EXEC_OK;
    }
    return exec_fail(err, EXEC_ERROR_FORMAT, "invalid composite type");
}

static int type_reference_in_scope(const exec_func_type *type,
                                   wasm_valtype value_type,
                                   uint32_t total_types) {
    if (!WASM_VALTYPE_IS_TYPE_REF(value_type)) return 1;
    uint32_t target = WASM_VALTYPE_TYPE_REF_INDEX(value_type);
    uint32_t group_end = type->rec_group_start + type->rec_group_size;
    return target < total_types && target < group_end;
}

static exec_status parse_types(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t entries;
    if (!er_u32(sec, &entries))
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid type count");
    if (entries > EXEC_MAX_TYPES)
        return exec_fail(err, EXEC_ERROR_FORMAT, "too many types");
    eng->types = (exec_func_type *)calloc(EXEC_MAX_TYPES, sizeof(*eng->types));
    if (entries && !eng->types)
        return exec_fail(err, EXEC_ERROR_FORMAT, "type alloc failed");
    uint32_t index = 0;
    for (uint32_t entry = 0; entry < entries; entry++) {
        uint8_t form;
        if (!er_u8(sec, &form))
            return exec_fail(err, EXEC_ERROR_FORMAT, "truncated type entry");
        uint32_t group_size = 1;
        if (form == 0x4e) {
            if (!er_u32(sec, &group_size) || !group_size)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid recursive type group");
        }
        if (group_size > EXEC_MAX_TYPES - index)
            return exec_fail(err, EXEC_ERROR_FORMAT, "too many types");
        uint32_t group_start = index;
        for (uint32_t member = 0; member < group_size; member++, index++) {
            if (form == 0x4e && !er_u8(sec, &form))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "truncated recursive type group");
            exec_status status = parse_composite_type(
                eng, sec, form, index, group_start, group_size, err);
            if (status != EXEC_OK) return status;
            form = 0x4e;
        }
    }
    eng->type_count = index;
    for (uint32_t i = 0; i < eng->type_count; i++) {
        exec_func_type *type = &eng->types[i];
        for (int p = 0; p < type->param_count; p++)
            if (!type_reference_in_scope(type, type->params[p], eng->type_count))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "type reference outside recursive group");
        for (int result = 0; result < type->result_count; result++)
            if (!type_reference_in_scope(type, type->results[result],
                                         eng->type_count))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "type reference outside recursive group");
        for (int field = 0; field < type->field_count; field++)
            if (!type_reference_in_scope(type, type->fields[field],
                                         eng->type_count))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "type reference outside recursive group");
    }
    return EXEC_OK;
}

static exec_status parse_imports(waste_exec_engine *eng, exec_reader *sec,
                                 const exec_imports *imports, exec_error *err) {
    uint32_t count;
    if (!er_u32(sec, &count) || count > EXEC_MAX_FUNCS)
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid import count");
    for (uint32_t i = 0; i < count; i++) {
        uint32_t module_len, name_len; const uint8_t *module_bytes, *name_bytes; uint8_t kind;
        char module[EXEC_MAX_NAME], name[EXEC_MAX_NAME];
        if (!er_u32(sec,&module_len) || module_len >= EXEC_MAX_NAME || !er_bytes(sec,module_len,&module_bytes) ||
            !er_u32(sec,&name_len) || name_len >= EXEC_MAX_NAME || !er_bytes(sec,name_len,&name_bytes) || !er_u8(sec,&kind))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid import");
        if (!valid_utf8(module_bytes, module_len) ||
            !valid_utf8(name_bytes, name_len))
            return exec_fail(err, EXEC_ERROR_FORMAT, "malformed UTF-8 encoding");
        memcpy(module,module_bytes,module_len); module[module_len]='\0';
        memcpy(name,name_bytes,name_len); name[name_len]='\0';
        if (kind == 0) {
            uint32_t type_index;
            if (!er_u32(sec,&type_index) || type_index >= eng->type_count ||
                eng->types[type_index].kind != WAST_TYPE_FUNC ||
                eng->import_func_count >= EXEC_MAX_FUNCS)
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid function import type");
            const exec_host_import *binding = find_host_import(imports,module,name);
            if (!binding || !binding->function) return exec_fail(err, EXEC_ERROR_NOT_FOUND, "unresolved function import");
            if(binding->has_wasm_type&&!same_func_type(eng,type_index,binding->type_owner,binding->type_index))
                return exec_fail(err,EXEC_ERROR_FORMAT,"function import type mismatch");
            uint32_t index=eng->import_func_count++;
            eng->import_func_types[index]=type_index; eng->import_funcs[index]=binding->function;
            eng->import_host_data[index]=binding->host_data;
        } else if (kind == 1) {
            wasm_valtype type; uint8_t flags; uint32_t initial,maximum=0;
            if (!er_valtype(sec,&type) || !value_type_is_defined(eng, type) ||
                !is_reference_type(type) || !er_u8(sec,&flags) || flags>1 ||
                !er_u32(sec,&initial) || ((flags&1u) && !er_u32(sec,&maximum)) || ((flags&1u) && maximum<initial) ||
                eng->table_count>=EXEC_MAX_TABLES) return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table import type");
            exec_table *table=find_table_import(imports,module,name);
            if (!table) return exec_fail(err,EXEC_ERROR_NOT_FOUND,"unresolved table import");
            if (!same_value_type(eng,type,table->type_owner,table->element_type,0) ||
                table->size<initial || ((flags&1u) && (!table->has_max || table->max_size>maximum)))
                return exec_fail(err,EXEC_ERROR_FORMAT,"table import type mismatch");
            eng->tables[eng->table_count++]=table; eng->import_table_count++;
        } else if (kind == 2) {
            uint8_t flags; uint32_t initial,maximum=0;
            if (!er_u8(sec,&flags) || flags>1 || !er_u32(sec,&initial) || ((flags&1u) && !er_u32(sec,&maximum)) ||
                initial>65536u || ((flags&1u) && maximum<initial) ||
                eng->memory_count >= WAST_MAX_MEMORIES)
                return exec_fail(err,EXEC_ERROR_FORMAT,"invalid memory import type");
            exec_memory *memory=find_memory_import(imports,module,name);
            if (!memory) return exec_fail(err,EXEC_ERROR_NOT_FOUND,"unresolved memory import");
            if (memory->pages<initial || memory->pages>65536u || ((flags&1u) && (!memory->has_max || memory->max_pages>maximum)) ||
                (memory->pages && !memory->data)) return exec_fail(err,EXEC_ERROR_FORMAT,"memory import type mismatch");
            eng->memories[eng->memory_count++] = memory;
            eng->import_memory_count++;
            if (!eng->memory) eng->memory=memory;
        } else if (kind == 3) {
            uint8_t mutability; wasm_valtype value_type;
            if (!er_valtype(sec,&value_type) ||
                !value_type_is_defined(eng, value_type) ||
                !er_u8(sec,&mutability) || mutability>1 || eng->global_count>=EXEC_MAX_GLOBALS)
                return exec_fail(err,EXEC_ERROR_FORMAT,"invalid global import type");
            exec_global *global=find_global_import(imports,module,name);
            if (!global) return exec_fail(err,EXEC_ERROR_NOT_FOUND,"unresolved global import");
            if (!global_type_is_compat(global->type_owner,global->value.type,eng,value_type,mutability) ||
                global->mutable_!=mutability)
                return exec_fail(err,EXEC_ERROR_FORMAT,"global import type mismatch");
            eng->globals[eng->global_count++]=global; eng->import_global_count++;
        } else if (kind == 4) {
            uint32_t attribute, type_index;
            if (!er_u32(sec, &attribute) || attribute != 0 ||
                !er_u32(sec, &type_index) || type_index >= eng->type_count ||
                eng->types[type_index].kind != WAST_TYPE_FUNC ||
                eng->types[type_index].result_count != 0 ||
                eng->tag_count >= WAST_MAX_TAGS)
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid tag import");
            exec_tag *tag = find_tag_import(imports, module, name);
            if (!tag)
                return exec_fail(err, EXEC_ERROR_NOT_FOUND,
                                 "unresolved tag import");
            if (!same_func_type(eng, type_index, tag->type_owner,
                                tag->type_index))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "tag import type mismatch");
            eng->tag_types[eng->tag_count] = type_index;
            eng->tags[eng->tag_count++] = tag;
            eng->import_tag_count++;
        } else return exec_fail(err,EXEC_ERROR_FORMAT,"invalid import kind");
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
        if (!er_u32(sec, &ti) || ti >= eng->type_count ||
            eng->types[ti].kind != WAST_TYPE_FUNC)
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
        if (!valid_utf8(name, name_len))
            return exec_fail(err, EXEC_ERROR_FORMAT, "malformed UTF-8 encoding");
        if (!er_u8(sec, &kind) || kind > 4 || !er_u32(sec, &idx))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid export");
        if ((kind==0 && idx>=eng->import_func_count+eng->func_count) ||
            (kind==1 && idx>=eng->table_count) ||
            (kind==2 && idx>=eng->memory_count) ||
            (kind==3 && idx>=eng->global_count) ||
            (kind==4 && idx>=eng->tag_count)) return exec_fail(err,EXEC_ERROR_FORMAT,"invalid export index");
        memcpy(eng->exports[i].name, name, name_len);
        eng->exports[i].name[name_len] = '\0';
        eng->exports[i].index = idx; eng->exports[i].kind=kind;
        if (kind == 0) eng->declared_funcs[idx] = 1;
        for (uint32_t j = 0; j < i; j++)
            if (strcmp(eng->exports[j].name, eng->exports[i].name) == 0)
                return exec_fail(err, EXEC_ERROR_FORMAT, "duplicate export name");
    }
    return EXEC_OK;
}

static exec_status parse_memory(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t count;
    if (!er_u32(sec, &count) || count > WAST_MAX_MEMORIES - eng->memory_count)
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid memory count");
    for (uint32_t i = 0; i < count; i++) {
        uint32_t initial, maximum = 0;
        uint8_t flags;
        if (!er_u8(sec, &flags) || flags > 1 || !er_u32(sec, &initial) ||
            ((flags & 1u) && !er_u32(sec, &maximum)) || initial > 65536u ||
            ((flags & 1u) && (maximum > 65536u || maximum < initial)))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid memory limits");
        uint32_t index = eng->memory_count;
        exec_memory *memory = &eng->owned_memories[index];
        size_t bytes = (size_t)initial * EXEC_PAGE_SIZE;
        memory->data = (uint8_t *)calloc(bytes ? bytes : 1, 1);
        if (!memory->data)
            return exec_fail(err, EXEC_ERROR_FORMAT, "memory allocation failed");
        memory->pages=initial; memory->has_max=(uint8_t)(flags&1u);
        memory->max_pages=maximum;
        eng->memories[index]=memory;
        eng->owns_memories[index]=1;
        eng->memory_count++;
        if (!eng->memory) eng->memory=memory;
    }
    return EXEC_OK;
}

typedef struct {
    wasm_value value;
    const waste_exec_engine *type_owner;
} exec_const_value;

static exec_status eval_constexpr(waste_exec_engine *eng,
                                  exec_reader *sec,
                                  uint32_t global_limit,
                                  wasm_valtype declared_type,
                                  exec_const_value *result,
                                  exec_error *err);

static exec_status parse_tables(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t count;
    if (!er_u32(sec,&count) || count>EXEC_MAX_TABLES-eng->table_count) return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table count");
    for(uint32_t i=0;i<count;i++) {
        uint8_t has_initializer = 0;
        wasm_valtype type; uint8_t flags; uint32_t initial,maximum=0;
        /* The typed-function-references table form is encoded as
         * 0x40 0x00 tabletype constexpr.  The reserved zero distinguishes it
         * from the legacy tabletype whose first byte is a reftype. */
        if (sec->cursor < sec->end && *sec->cursor == 0x40) {
            uint8_t marker, reserved;
            if (!er_u8(sec, &marker) || !er_u8(sec, &reserved) || reserved != 0)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid table initializer marker");
            has_initializer = 1;
        }
        if (!er_valtype(sec,&type) || !value_type_is_defined(eng, type) ||
            !is_reference_type(type) || !er_u8(sec,&flags) || flags>1 ||
            !er_u32(sec,&initial) || ((flags&1u) && !er_u32(sec,&maximum)) || ((flags&1u) && maximum<initial))
            return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table type");
        if (!has_initializer && !is_nullable_reference_type(type))
            return exec_fail(err, EXEC_ERROR_FORMAT,
                             "non-nullable table requires initializer");
        uint32_t index=eng->table_count; exec_table *table=&eng->owned_tables[index];
        table->elements=(exec_table_element *)malloc((initial ? initial : 1)*sizeof(exec_table_element));
        if (!table->elements) return exec_fail(err,EXEC_ERROR_FORMAT,"table allocation failed");
        for(uint32_t j=0;j<initial;j++){table->elements[j].owner=NULL;table->elements[j].func_idx=0;}
        table->size=initial; table->has_max=(uint8_t)(flags&1u); table->max_size=maximum;
        table->element_type=type;
        table->type_owner=eng;
        eng->tables[eng->table_count++]=table;
        if (has_initializer) {
            exec_const_value value;
            exec_status status = eval_constexpr(
                eng, sec, eng->global_count, type, &value, err);
            if (status != EXEC_OK) return status;
            for (uint32_t j = 0; j < initial; j++) {
                if (value.value.ref != UINT32_MAX) {
                    table->elements[j].owner =
                        (waste_exec_engine *)value.type_owner;
                    table->elements[j].func_idx = value.value.ref;
                }
            }
        }
    }
    return EXEC_OK;
}

static int nullable_reference_for_heap(const waste_exec_engine *eng,
                                       int32_t heap_type,
                                       wasm_valtype *type) {
    switch (heap_type) {
        case -16: *type = WASM_VALTYPE_FUNCREF; return 1;
        case -17: *type = WASM_VALTYPE_EXTERNREF; return 1;
        case -18: *type = WASM_VALTYPE_ANYREF; return 1;
        case -19: *type = WASM_VALTYPE_EQREF; return 1;
        case -20: *type = WASM_VALTYPE_I31REF; return 1;
        case -21: *type = WASM_VALTYPE_STRUCTREF; return 1;
        case -22: *type = WASM_VALTYPE_ARRAYREF; return 1;
        case -23: *type = WASM_VALTYPE_EXNREF; return 1;
        case -15: *type = WASM_VALTYPE_NULLREF; return 1;
        case -14: *type = WASM_VALTYPE_NULLEXTERNREF; return 1;
        case -13: *type = WASM_VALTYPE_NULLFUNCREF; return 1;
        case -12: *type = WASM_VALTYPE_NULLEXNREF; return 1;
        default:
            if (heap_type < 0 || (uint32_t)heap_type >= eng->type_count)
                return 0;
            *type = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_NULL_BASE +
                                  (uint32_t)heap_type);
            return 1;
    }
}

static exec_status eval_constexpr(waste_exec_engine *eng,
                                  exec_reader *sec,
                                  uint32_t global_limit,
                                  wasm_valtype declared_type,
                                  exec_const_value *result,
                                  exec_error *err) {
    exec_const_value stack[32];
    int top = 0;
    for (;;) {
        uint8_t opcode;
        if (!er_u8(sec, &opcode))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "unterminated constant expression");
        if (opcode == 0x0b) break;
        exec_const_value value;
        memset(&value, 0, sizeof(value));
        value.type_owner = eng;
        if (opcode == 0x41) {
            int32_t immediate;
            if (!er_i32(sec, &immediate))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid i32 global initializer");
            value.value.type = WASM_VALTYPE_I32;
            value.value.i32 = immediate;
        } else if (opcode == 0x42) {
            int64_t immediate;
            if (!er_i64(sec, &immediate))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid i64 global initializer");
            value.value.type = WASM_VALTYPE_I64;
            value.value.i64 = immediate;
        } else if (opcode == 0x43 || opcode == 0x44) {
            const uint8_t *bits;
            size_t width = opcode == 0x43 ? 4u : 8u;
            if (!er_bytes(sec, width, &bits))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid float global initializer");
            value.value.type = opcode == 0x43 ? WASM_VALTYPE_F32 :
                                               WASM_VALTYPE_F64;
            if (opcode == 0x43) memcpy(&value.value.f32, bits, 4);
            else memcpy(&value.value.f64, bits, 8);
        } else if (opcode == 0x23) {
            uint32_t source;
            if (!er_u32(sec, &source) || source >= global_limit ||
                !eng->globals[source] || eng->globals[source]->mutable_)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid global.get initializer");
            value.value = eng->globals[source]->value;
            value.type_owner = eng->globals[source]->type_owner;
        } else if (opcode == 0xd0) {
            int32_t heap_type;
            if (!er_i32(sec, &heap_type) ||
                !nullable_reference_for_heap(eng, heap_type,
                                             &value.value.type))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid ref.null initializer");
            value.value.ref = UINT32_MAX;
        } else if (opcode == 0xd2) {
            uint32_t function, type_index;
            if (!er_u32(sec, &function) ||
                function >= eng->import_func_count + eng->func_count)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid ref.func initializer");
            type_index = function < eng->import_func_count ?
                eng->import_func_types[function] :
                eng->funcs[function - eng->import_func_count].type_index;
            value.value.type = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE +
                                              type_index);
            value.value.ref = function;
            eng->declared_funcs[function] = 1;
        } else if (opcode == 0xfd) {
            uint32_t simd_op;
            const uint8_t *bytes;
            if (!er_u32(sec, &simd_op) || simd_op != 0x0c ||
                !er_bytes(sec, 16, &bytes))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid v128 global initializer");
            value.value.type = WASM_VALTYPE_V128;
            memcpy(value.value.v128.bytes, bytes, 16);
        } else if (opcode == 0x6a || opcode == 0x6b || opcode == 0x6c ||
                   opcode == 0x7c || opcode == 0x7d || opcode == 0x7e) {
            wasm_valtype operand_type = opcode < 0x7c ? WASM_VALTYPE_I32 :
                                                       WASM_VALTYPE_I64;
            if (top < 2 || stack[top - 1].value.type != operand_type ||
                stack[top - 2].value.type != operand_type)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "global initializer type mismatch");
            exec_const_value right = stack[--top];
            exec_const_value *left = &stack[top - 1];
            if (operand_type == WASM_VALTYPE_I32) {
                uint32_t a = (uint32_t)left->value.i32;
                uint32_t b = (uint32_t)right.value.i32;
                left->value.i32 = (int32_t)(opcode == 0x6a ? a + b :
                                            opcode == 0x6b ? a - b : a * b);
            } else {
                uint64_t a = (uint64_t)left->value.i64;
                uint64_t b = (uint64_t)right.value.i64;
                left->value.i64 = (int64_t)(opcode == 0x7c ? a + b :
                                            opcode == 0x7d ? a - b : a * b);
            }
            continue;
        } else if (opcode == 0xac || opcode == 0xad) {
            if (top < 1 || stack[top - 1].value.type != WASM_VALTYPE_I32)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "global initializer type mismatch");
            int32_t input = stack[top - 1].value.i32;
            stack[top - 1].value.type = WASM_VALTYPE_I64;
            stack[top - 1].value.i64 = opcode == 0xac ?
                (int64_t)input : (int64_t)(uint32_t)input;
            continue;
        } else {
            return exec_fail(err, EXEC_ERROR_UNSUPPORTED,
                             "unsupported global initializer");
        }
        if (top >= (int)(sizeof(stack) / sizeof(stack[0])))
            return exec_fail(err, EXEC_ERROR_FORMAT,
                             "global initializer stack overflow");
        stack[top++] = value;
    }
    if (top != 1 ||
        !global_type_is_compat(stack[0].type_owner, stack[0].value.type,
                               eng, declared_type, 0))
        return exec_fail(err, EXEC_ERROR_FORMAT,
                         "global initializer type mismatch");
    *result = stack[0];
    return EXEC_OK;
}

static exec_status parse_globals(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t count;
    if (!er_u32(sec, &count) || count > EXEC_MAX_GLOBALS) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid global count");
    if (count > EXEC_MAX_GLOBALS-eng->global_count) return exec_fail(err,EXEC_ERROR_FORMAT,"too many globals");
    for (uint32_t i = 0; i < count; i++) {
        uint32_t index=eng->global_count+i; exec_global *global=&eng->owned_globals[index];
        wasm_valtype type; uint8_t mutability;
        if (!er_valtype(sec, &type) || !value_type_is_defined(eng, type) ||
            !er_u8(sec, &mutability) || mutability > 1)
            return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported global initializer");
        memset(global,0,sizeof(*global));
        exec_const_value initial;
        exec_status status = eval_constexpr(eng, sec, index, type,
                                            &initial, err);
        if (status != EXEC_OK) return status;
        global->value = initial.value;
        /* A global's externally visible type is its declared type, not the
         * possibly narrower type of its initializer.  In particular, a
         * (ref.func ...) initializer must not turn a declared (ref func)
         * global into a specific indexed reference type for later imports.
         * The reference payload is unchanged; only its static type is
         * canonicalized at the global boundary. */
        global->value.type = type;
        global->type_owner = eng;
        global->mutable_=mutability; eng->globals[index]=global;
    }
    eng->global_count += count;
    return EXEC_OK;
}

static exec_status parse_tags(waste_exec_engine *eng, exec_reader *sec,
                              exec_error *err) {
    uint32_t count;
    if (!er_u32(sec, &count) || count > WAST_MAX_TAGS - eng->tag_count)
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid tag count");
    for (uint32_t i = 0; i < count; i++) {
        uint32_t attribute, type_index;
        if (!er_u32(sec, &attribute) || attribute != 0 ||
            !er_u32(sec, &type_index) || type_index >= eng->type_count ||
            eng->types[type_index].result_count != 0)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid tag type");
        uint32_t index = eng->tag_count;
        exec_tag *tag = &eng->owned_tags[index];
        tag->type_owner = eng;
        tag->type_index = type_index;
        eng->tag_types[index] = type_index;
        eng->tags[index] = tag;
        eng->tag_count++;
    }
    return EXEC_OK;
}

static exec_status parse_start(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t func_index;
    if (!er_u32(sec, &func_index) || func_index >= eng->import_func_count + eng->func_count)
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid start function index");
    uint32_t type_index = func_index < eng->import_func_count ?
        eng->import_func_types[func_index] :
        eng->funcs[func_index - eng->import_func_count].type_index;
    if (type_index >= eng->type_count ||
        eng->types[type_index].param_count != 0 ||
        eng->types[type_index].result_count != 0)
        return exec_fail(err, EXEC_ERROR_FORMAT, "start function must have type () -> ()");
    eng->start_func = func_index;
    eng->has_start = 1;
    return EXEC_OK;
}

static exec_status parse_data(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t count;
    if (!er_u32(sec, &count)) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid data count");
    if (eng->has_data_count && count != eng->declared_data_count)
        return exec_fail(err, EXEC_ERROR_FORMAT, "data count mismatch");
    if (count > WAST_MAX_DATA_SEGS)
        return exec_fail(err, EXEC_ERROR_FORMAT, "too many data segments");
    eng->data_count = count;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t mode, memory_index = 0, length, offset;
        const uint8_t *data;
        if (!er_u32(sec, &mode)) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid data segment");
        if (mode == 1) {
            /* passive segment — save bytes for memory.init */
            if (!er_u32(sec, &length) || !er_bytes(sec, length, &data))
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid passive data");
            if (length) {
                eng->data_segs[i] = (uint8_t *)malloc(length);
                if (!eng->data_segs[i]) return exec_fail(err, EXEC_ERROR_FORMAT, "out of memory");
                memcpy(eng->data_segs[i], data, length);
            }
            eng->data_seg_lengths[i] = length;
            continue;
        }
        if (mode == 2 && !er_u32(sec, &memory_index)) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid data memory");
        if ((mode != 0 && mode != 2) || memory_index >= eng->memory_count)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid active data segment");
        exec_memory *memory = eng->memories[memory_index];
        exec_const_value initial;
        exec_status status = eval_constexpr(eng, sec, eng->global_count,
                                            WASM_VALTYPE_I32, &initial, err);
        if (status != EXEC_OK)
            return status;
        offset = (uint32_t)initial.value.i32;
        if (!er_u32(sec, &length) || !er_bytes(sec, length, &data))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid active data segment");
        if ((uint64_t)offset + length >
            (uint64_t)memory->pages * EXEC_PAGE_SIZE) {
            if (!eng->instantiation_trapped) {
                eng->instantiation_trapped = 1;
                snprintf(eng->instantiation_error,
                         sizeof(eng->instantiation_error),
                         "out of bounds memory access");
            }
        } else if (!eng->instantiation_trapped) {
            memcpy(memory->data + offset, data, length);
        }
        /* active segments are considered dropped after instantiation */
        eng->data_dropped[i] = 1;
    }
    return EXEC_OK;
}

static exec_status parse_elements(waste_exec_engine *eng, exec_reader *sec,
                                  exec_error *err) {
    uint32_t count;
    if (!er_u32(sec, &count) || count > WAST_MAX_ELEM_SEGS)
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element count");
    eng->elem_count = count;
    for (uint32_t segment = 0; segment < count; segment++) {
        uint32_t mode, table_index = 0, item_count, offset = 0;
        wasm_valtype ref_type = WASM_VALTYPE_FUNCREF;
        int active, uses_expressions;
        if (!er_u32(sec, &mode) || mode > 7)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element segment mode");
        /* Modes 0-3: funcidx vectors; modes 4-7: expression vectors */
        uses_expressions = mode >= 4;
        active = (mode == 0 || mode == 2 || mode == 4 || mode == 6);
        /* Read explicit table index for modes 2 and 6 */
        if ((mode == 2 || mode == 6) && !er_u32(sec, &table_index))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element table");
        /* Read offset expression for active segments */
        if (active) {
            exec_const_value initial;
            exec_status status = eval_constexpr(eng, sec, eng->global_count,
                                                WASM_VALTYPE_I32, &initial,
                                                err);
            if (status != EXEC_OK) return status;
            offset = (uint32_t)initial.value.i32;
        }
        /* Read type/kind: modes 1,2,3 have elemkind; modes 5,6,7 have reftype;
           modes 0,4 imply funcref */
        if (mode == 1 || mode == 2 || mode == 3) {
            uint8_t elemkind;
            if (!er_u8(sec, &elemkind) || elemkind != 0x00)
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element kind");
            ref_type = WASM_VALTYPE_FUNCREF;
        } else if (mode == 5 || mode == 6 || mode == 7) {
            if (!er_valtype(sec, &ref_type) ||
                !value_type_is_defined(eng, ref_type) ||
                !is_reference_type(ref_type))
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element type");
        }
        /* else mode 0 or 4: ref_type stays FUNCREF */
        if (!er_u32(sec, &item_count))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element length");
        eng->elem_types[segment] = ref_type;
        eng->elem_lengths[segment] = item_count;
        eng->elem_dropped[segment] =
            (uint8_t)(active || mode == 3 || mode == 7);
        if (item_count) {
            eng->elem_values[segment] =
                (exec_table_element *)calloc(item_count,
                                              sizeof(exec_table_element));
            if (!eng->elem_values[segment])
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "element segment allocation failed");
        }
        if (active && table_index >= eng->table_count)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid active element segment");
        if (uses_expressions) {
            /* Modes 4-7: each item is an init expression */
            for (uint32_t item = 0; item < item_count; item++) {
                exec_table_element slot = {NULL, 0};
                exec_const_value value;
                exec_status status = eval_constexpr(eng, sec,
                                                    eng->global_count,
                                                    ref_type, &value, err);
                if (status != EXEC_OK) return status;
                if (value.value.ref != UINT32_MAX) {
                    slot.owner = (waste_exec_engine *)value.type_owner;
                    slot.func_idx = value.value.ref;
                }
                eng->elem_values[segment][item] = slot;
            }
        } else {
            /* Modes 0-3: each item is a bare funcidx */
            for (uint32_t item = 0; item < item_count; item++) {
                uint32_t func_idx;
                if (!er_u32(sec, &func_idx) ||
                    func_idx >= eng->import_func_count + eng->func_count)
                    return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element funcidx");
                exec_table_element slot = {eng, func_idx};
                eng->declared_funcs[func_idx] = 1;
                eng->elem_values[segment][item] = slot;
            }
        }
        /* For active segments, the segment's effective type must be a subtype
         * of the table's element type.  Modes 0-3 (bare funcidx) items are
         * non-null function references by construction, so their effective
         * type is (ref func) even though the declared encoding is funcref. */
        wasm_valtype segment_eff_type = ref_type;
        if (!uses_expressions && ref_type == WASM_VALTYPE_FUNCREF)
            segment_eff_type = WASM_VALTYPE_FUNCREF_NONNULL;
        if (active && !global_type_is_compat(
                eng, segment_eff_type,
                eng->tables[table_index]->type_owner,
                eng->tables[table_index]->element_type, 0))
            return exec_fail(err, EXEC_ERROR_FORMAT,
                             "invalid active element segment");
        int apply_segment = active && !eng->instantiation_trapped;
        if (apply_segment &&
            (uint64_t)offset + item_count > eng->tables[table_index]->size) {
            eng->instantiation_trapped = 1;
            snprintf(eng->instantiation_error,
                     sizeof(eng->instantiation_error),
                     "out of bounds table access");
            apply_segment = 0;
        }
        if (apply_segment)
            for (uint32_t item = 0; item < item_count; item++)
                eng->tables[table_index]->elements[offset + item] =
                    eng->elem_values[segment][item];
    }
    return EXEC_OK;
}

/* ---- Code section ---- */

typedef enum {
    SELECT_VALIDATION_INVALID = -1,
    SELECT_VALIDATION_INCONCLUSIVE = 0,
    SELECT_VALIDATION_VALID = 1
} select_validation_result;

#define SELECT_VALIDATION_STACK 256
#define SELECT_BOTTOM_TYPE ((wasm_valtype)0x7fff)

typedef struct {
    const waste_exec_engine *engine;
    int height;
    int unreachable;
    int tail_call_seen;
    uint8_t kind;
    uint8_t has_else;
    int param_count;
    int result_count;
    wasm_valtype params[WAST_MAX_PARAMS];
    wasm_valtype results[WAST_MAX_RESULTS];
    uint8_t entry_initialized[(EXEC_MAX_LOCALS + 7) / 8];
} select_validation_control;

static int validation_local_initialized(const uint8_t *bits, uint32_t index) {
    return (bits[index / 8] & (uint8_t)(1u << (index % 8))) != 0;
}

static void validation_initialize_local(uint8_t *bits, uint32_t index) {
    bits[index / 8] |= (uint8_t)(1u << (index % 8));
}

static int select_validation_pop(wasm_valtype *stack, int *top,
                                 const select_validation_control *control,
                                 wasm_valtype *value) {
    if (*top > control->height) {
        *value = stack[--*top];
        return 1;
    }
    if (control->unreachable) {
        *value = SELECT_BOTTOM_TYPE;
        return 1;
    }
    return 0;
}

static int select_validation_pop_type(wasm_valtype *stack, int *top,
                                      const select_validation_control *control,
                                      wasm_valtype expected) {
    wasm_valtype actual;
    if (!select_validation_pop(stack, top, control, &actual)) return 0;
    if (actual == SELECT_BOTTOM_TYPE ||
        global_type_is_compat(control->engine, actual, control->engine,
                              expected, 0)) return 1;
    if (actual == WASM_VALTYPE_FUNCREF_NONNULL &&
        expected == WASM_VALTYPE_FUNCREF) return 1;
    if (actual == WASM_VALTYPE_EXTERNREF_NONNULL &&
        expected == WASM_VALTYPE_EXTERNREF) return 1;
    if (WASM_VALTYPE_IS_TYPE_REF(actual) &&
        expected == WASM_VALTYPE_FUNCREF) return 1;
    if (WASM_VALTYPE_IS_TYPE_REF(actual) &&
        WASM_VALTYPE_IS_TYPE_REF(expected) &&
        (unsigned)actual >= WASM_VALTYPE_TYPE_REF_BASE &&
        (unsigned)expected < WASM_VALTYPE_TYPE_REF_BASE &&
        WASM_VALTYPE_TYPE_REF_INDEX(actual) ==
        WASM_VALTYPE_TYPE_REF_INDEX(expected)) return 1;
    return 0;
}

static int select_validation_push(wasm_valtype *stack, int *top,
                                  wasm_valtype value) {
    if (*top >= SELECT_VALIDATION_STACK) return 0;
    stack[(*top)++] = value;
    return 1;
}

static int select_validation_unary(wasm_valtype *stack, int *top,
                                   const select_validation_control *control,
                                   wasm_valtype operand,
                                   wasm_valtype result) {
    return select_validation_pop_type(stack, top, control, operand) &&
           select_validation_push(stack, top, result);
}

static int select_validation_binary(wasm_valtype *stack, int *top,
                                    const select_validation_control *control,
                                    wasm_valtype operand,
                                    wasm_valtype result) {
    return select_validation_pop_type(stack, top, control, operand) &&
           select_validation_pop_type(stack, top, control, operand) &&
           select_validation_push(stack, top, result);
}

static int select_validation_local_type(const exec_func_type *signature,
                                        const exec_func *func,
                                        uint32_t index,
                                        wasm_valtype *type) {
    if (index < (uint32_t)signature->param_count) {
        *type = signature->params[index];
        return 1;
    }
    index -= (uint32_t)signature->param_count;
    if (index >= func->local_count) return 0;
    *type = func->locals[index];
    return 1;
}

/* Incremental core validator.  It is deliberately conservative: a function
 * containing an instruction or control signature outside the covered slice
 * is INCONCLUSIVE, while any error in a modeled instruction is INVALID.  This
 * lets assert_invalid observe real validation failures without guessing about
 * proposal instructions that the native executor does not validate yet. */
static select_validation_result validate_select_function(
    const waste_exec_engine *eng, const exec_func *func,
    const exec_instr *code, uint32_t code_size) {
    if (func->type_index >= eng->type_count) return SELECT_VALIDATION_INVALID;
    const exec_func_type *signature = &eng->types[func->type_index];
    wasm_valtype stack[SELECT_VALIDATION_STACK];
    int top = 0;
    select_validation_control controls[EXEC_MAX_CONTROL + 1];
    uint8_t initialized[(EXEC_MAX_LOCALS + 7) / 8];
    int control_top = 0;
    memset(&controls[0], 0, sizeof(controls[0]));
    memset(initialized, 0, sizeof(initialized));
    for (int i = 0; i < signature->param_count; i++)
        validation_initialize_local(initialized, (uint32_t)i);
    for (uint32_t i = 0; i < func->local_count; i++) {
        wasm_valtype type = func->locals[i];
        if (!is_reference_type(type) || is_nullable_reference_type(type))
            validation_initialize_local(
                initialized, (uint32_t)signature->param_count + i);
    }
    controls[0].engine = eng;

    for (uint32_t pc = 0; pc < code_size; pc++) {
        const exec_instr *instr = &code[pc];
        select_validation_control *control = &controls[control_top];
#ifndef WASTE_FREESTANDING
        if (getenv("WAST_DEBUG_VALIDATE_TRACE"))
            fprintf(stderr, "validate pc=%u op=%02x top=%d control=%d height=%d unreachable=%d\n",
                    pc, instr->opcode, top, control_top, control->height,
                    control->unreachable);
#endif
        switch (instr->opcode) {
            case 0x00:
                top = control->height;
                control->unreachable = 1;
                break;
            case 0x01:
                break;
            case 0x02:
            case 0x03:
            case 0x04:
            case 0x1f: {
                const wasm_valtype *params = NULL;
                const wasm_valtype *results = NULL;
                int param_count = 0;
                int result_count = 0;
                wasm_valtype direct_result;
                if (instr->block_type_index >= 0) {
                    if ((uint32_t)instr->block_type_index >= eng->type_count)
                        return SELECT_VALIDATION_INVALID;
                    const exec_func_type *block_type =
                        &eng->types[instr->block_type_index];
                    params = block_type->params;
                    param_count = block_type->param_count;
                    results = block_type->results;
                    result_count = block_type->result_count;
                } else if (instr->has_block_result_type) {
                    direct_result = instr->block_result_type;
                    results = &direct_result;
                    result_count = 1;
                }
                if (instr->opcode == 0x04 &&
                    !select_validation_pop_type(stack, &top, control,
                                                WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                if (instr->opcode == 0x1f)
                    for (uint32_t i = 0; i < instr->catch_count; i++)
                        if (instr->catches[i].depth >
                            (uint32_t)(control_top + 1))
                            return SELECT_VALIDATION_INVALID;
                for (int i = param_count; i > 0; i--)
                    if (!select_validation_pop_type(
                            stack, &top, control, params[i - 1]))
                        return SELECT_VALIDATION_INVALID;
                if (param_count > WAST_MAX_PARAMS)
                    return SELECT_VALIDATION_INCONCLUSIVE;
                if (control_top >= EXEC_MAX_CONTROL)
                    return SELECT_VALIDATION_INCONCLUSIVE;
                select_validation_control *next = &controls[++control_top];
                memset(next, 0, sizeof(*next));
                next->engine = eng;
                next->height = top;
                next->kind = (uint8_t)instr->opcode;
                next->param_count = param_count;
                next->result_count = result_count;
                memcpy(next->entry_initialized, initialized,
                       sizeof(next->entry_initialized));
                if (param_count)
                    memcpy(next->params, params,
                           (size_t)param_count * sizeof(params[0]));
                if (result_count)
                    memcpy(next->results, results,
                           (size_t)result_count * sizeof(results[0]));
                for (int i = 0; i < param_count; i++)
                    if (!select_validation_push(stack, &top, params[i]))
                        return SELECT_VALIDATION_INCONCLUSIVE;
                break;
            }
            case 0x08: {
                if (instr->u32_imm >= eng->tag_count)
                    return SELECT_VALIDATION_INVALID;
                uint32_t type_index = eng->tag_types[instr->u32_imm];
                if (type_index >= eng->type_count)
                    return SELECT_VALIDATION_INVALID;
                exec_func_type *tag_type = &eng->types[type_index];
                for (int i = tag_type->param_count; i > 0; i--)
                    if (!select_validation_pop_type(
                            stack, &top, control, tag_type->params[i - 1]))
                        return SELECT_VALIDATION_INVALID;
                top = control->height;
                control->unreachable = 1;
                break;
            }
            case 0x05:
                if (control_top == 0 || control->kind != 0x04 ||
                    control->has_else)
                    return SELECT_VALIDATION_INVALID;
                control = &controls[control_top];
                for (int i = control->result_count; i > 0; i--)
                    if (!select_validation_pop_type(
                            stack, &top, control,
                            control->results[i - 1]))
                        return SELECT_VALIDATION_INVALID;
                if (top != control->height)
                    return SELECT_VALIDATION_INVALID;
                top = control->height;
                control->unreachable = 0;
                control->has_else = 1;
                memcpy(initialized, control->entry_initialized,
                       sizeof(initialized));
                for (int i = 0; i < control->param_count; i++)
                    if (!select_validation_push(stack, &top,
                                                control->params[i]))
                        return SELECT_VALIDATION_INCONCLUSIVE;
                break;
            case 0x0b:
                if (control_top > 0) {
                    if (control->kind == 0x04 && !control->has_else) {
                        if (control->param_count != control->result_count)
                            return SELECT_VALIDATION_INVALID;
                        for (int i = 0; i < control->param_count; i++)
                            if (control->params[i] != control->results[i])
                                return SELECT_VALIDATION_INVALID;
                    }
                    for (int i = control->result_count; i > 0; i--)
                        if (!select_validation_pop_type(
                                stack, &top, control,
                                control->results[i - 1]))
                            return SELECT_VALIDATION_INVALID;
                    if (top != control->height)
                        return SELECT_VALIDATION_INVALID;
                    wasm_valtype end_types[WAST_MAX_RESULTS];
                    int end_count = control->result_count;
                    if (end_count)
                        memcpy(end_types, control->results,
                               (size_t)end_count * sizeof(end_types[0]));
                    top = control->height;
                    memcpy(initialized, control->entry_initialized,
                           sizeof(initialized));
                    control_top--;
                    for (int i = 0; i < end_count; i++)
                        if (!select_validation_push(stack, &top,
                                                    end_types[i]))
                            return SELECT_VALIDATION_INCONCLUSIVE;
                    break;
                }
                for (int i = signature->result_count; i > 0; i--)
                    if (!select_validation_pop_type(
                            stack, &top, control,
                            signature->results[i - 1]))
                        return SELECT_VALIDATION_INVALID;
                /* Stack polymorphism supplies missing operands at the frame
                 * height; it does not discard concrete operands pushed after
                 * the path became unreachable.  After consuming the declared
                 * results the function stack must therefore always be empty. */
                if (top != 0)
                    return SELECT_VALIDATION_INVALID;
                return SELECT_VALIDATION_VALID;
            case 0x0f:
                for (int i = signature->result_count; i > 0; i--)
                    if (!select_validation_pop_type(
                            stack, &top, control,
                            signature->results[i - 1]))
                        return SELECT_VALIDATION_INVALID;
                top = control->height;
                control->unreachable = 1;
                break;
            case 0x0c:
            case 0x0d: {
                uint32_t depth = instr->u32_imm;
                if (instr->opcode == 0x0d &&
                    !select_validation_pop_type(stack, &top, control,
                                                WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                if (depth > (uint32_t)control_top)
                    return SELECT_VALIDATION_INVALID;
                const wasm_valtype *label_types;
                int label_count;
                if (depth == (uint32_t)control_top) {
                    label_types = signature->results;
                    label_count = signature->result_count;
                } else {
                    const select_validation_control *target =
                        &controls[control_top - (int)depth];
                    if (target->kind == 0x03) {
                        label_types = target->params;
                        label_count = target->param_count;
                    } else {
                        label_types = target->results;
                        label_count = target->result_count;
                    }
                }
                for (int i = label_count; i > 0; i--)
                    if (!select_validation_pop_type(
                            stack, &top, control, label_types[i - 1]))
                        return SELECT_VALIDATION_INVALID;
                if (instr->opcode == 0x0d) {
                    for (int i = 0; i < label_count; i++)
                        if (!select_validation_push(stack, &top,
                                                    label_types[i]))
                            return SELECT_VALIDATION_INCONCLUSIVE;
                } else {
                    top = control->height;
                    control->unreachable = 1;
                }
                break;
            }
            case 0xd4: {
                wasm_valtype reference = SELECT_BOTTOM_TYPE;
                if (!select_validation_pop(stack, &top, control, &reference) ||
                    (reference != SELECT_BOTTOM_TYPE &&
                     !is_reference_type(reference))) {
#ifndef WASTE_FREESTANDING
                    if (getenv("WAST_DEBUG_VALIDATION"))
                        fprintf(stderr, "ref-branch pop/type pc=%u op=%x ref=%u top=%d height=%d unreachable=%d\n",
                                pc, instr->opcode, (unsigned)reference, top,
                                control->height, control->unreachable);
#endif
                    return SELECT_VALIDATION_INVALID;
                }
                if (!select_validation_push(
                        stack, &top,
                        reference == SELECT_BOTTOM_TYPE ? reference :
                        nonnullable_reference_type(reference)))
                    return SELECT_VALIDATION_INCONCLUSIVE;
                break;
            }
            case 0xd5:
            case 0xd6: {
                wasm_valtype reference = SELECT_BOTTOM_TYPE;
                if (!select_validation_pop(stack, &top, control, &reference) ||
                    (reference != SELECT_BOTTOM_TYPE &&
                     !is_reference_type(reference))) {
#ifndef WASTE_FREESTANDING
                    if (getenv("WAST_DEBUG_VALIDATION"))
                        fprintf(stderr, "ref-branch pop/type pc=%u op=%x ref=%u top=%d height=%d unreachable=%d\n",
                                pc, instr->opcode, (unsigned)reference, top,
                                control->height, control->unreachable);
#endif
                    return SELECT_VALIDATION_INVALID;
                }
                wasm_valtype refined = reference == SELECT_BOTTOM_TYPE ?
                    reference : nonnullable_reference_type(reference);
                uint32_t depth = instr->u32_imm;
                if (depth > (uint32_t)control_top) {
#ifndef WASTE_FREESTANDING
                    if (getenv("WAST_DEBUG_VALIDATION"))
                        fprintf(stderr, "ref-branch depth pc=%u depth=%u control=%d\n",
                                pc, depth, control_top);
#endif
                    return SELECT_VALIDATION_INVALID;
                }
                const wasm_valtype *label_types;
                int label_count;
                if (depth == (uint32_t)control_top) {
                    label_types = signature->results;
                    label_count = signature->result_count;
                } else {
                    const select_validation_control *target =
                        &controls[control_top - (int)depth];
                    label_types = target->kind == 0x03 ? target->params :
                                                        target->results;
                    label_count = target->kind == 0x03 ? target->param_count :
                                                        target->result_count;
                }
                int carried_count = label_count;
                if (instr->opcode == 0xd6) {
                    if (label_count < 1 ||
                        (refined != SELECT_BOTTOM_TYPE &&
                         !global_type_is_compat(eng, refined, eng,
                                                label_types[label_count - 1],
                                                0))) {
#ifndef WASTE_FREESTANDING
                        if (getenv("WAST_DEBUG_VALIDATION"))
                            fprintf(stderr, "ref-branch result pc=%u ref=%u refined=%u labels=%d last=%u\n",
                                    pc, (unsigned)reference, (unsigned)refined,
                                    label_count, label_count ?
                                    (unsigned)label_types[label_count - 1] : 0u);
#endif
                        return SELECT_VALIDATION_INVALID;
                    }
                    carried_count--;
                }
                wasm_valtype carried[WAST_MAX_RESULTS];
                for (int i = carried_count; i > 0; i--)
                    if (!select_validation_pop(stack, &top, control,
                                               &carried[i - 1]) ||
                        (carried[i - 1] != SELECT_BOTTOM_TYPE &&
                         !global_type_is_compat(eng, carried[i - 1], eng,
                                                label_types[i - 1], 0))) {
#ifndef WASTE_FREESTANDING
                        if (getenv("WAST_DEBUG_VALIDATION"))
                            fprintf(stderr, "ref-branch carried pc=%u i=%d actual=%u expected=%u top=%d height=%d\n",
                                    pc, i, (unsigned)carried[i - 1],
                                    (unsigned)label_types[i - 1], top,
                                    control->height);
#endif
                        return SELECT_VALIDATION_INVALID;
                    }
                for (int i = 0; i < carried_count; i++)
                    /* Pop then re-push at the label type.  Preserving a
                     * narrower operand type here is unsound: subsequent
                     * fall-through instructions only know the branch label's
                     * declared type (GC issue 516). */
                    if (!select_validation_push(stack, &top, label_types[i]))
                        return SELECT_VALIDATION_INCONCLUSIVE;
                if (instr->opcode == 0xd5 &&
                    !select_validation_push(stack, &top, refined))
                    return SELECT_VALIDATION_INCONCLUSIVE;
                break;
            }
            case 0x0e: {
                if (!select_validation_pop_type(stack, &top, control,
                                                WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                uint32_t *depths;
                memcpy(&depths, instr->v128_imm.bytes, sizeof(depths));
                int common_count = -1;
                for (uint32_t i = 0; i <= instr->u32_imm; i++) {
                    uint32_t depth = depths[i];
                    int label_count;
                    if (depth > (uint32_t)control_top)
                        return SELECT_VALIDATION_INVALID;
                    if (depth == (uint32_t)control_top) {
                        label_count = signature->result_count;
                    } else {
                        const select_validation_control *target =
                            &controls[control_top - (int)depth];
                        if (target->kind == 0x03) {
                            label_count = target->param_count;
                        } else {
                            label_count = target->result_count;
                        }
                    }
                    if (common_count < 0) {
                        common_count = label_count;
                    } else if (common_count != label_count)
                        return SELECT_VALIDATION_INVALID;
                }
                wasm_valtype operands[WAST_MAX_RESULTS];
                for (int i = common_count; i > 0; i--)
                    if (!select_validation_pop(
                            stack, &top, control, &operands[i - 1]))
                        return SELECT_VALIDATION_INVALID;
                for (uint32_t i = 0; i <= instr->u32_imm; i++) {
                    uint32_t depth = depths[i];
                    const wasm_valtype *label_types;
                    if (depth == (uint32_t)control_top) {
                        label_types = signature->results;
                    } else {
                        const select_validation_control *target =
                            &controls[control_top - (int)depth];
                        label_types = target->kind == 0x03 ?
                                      target->params : target->results;
                    }
                    for (int j = 0; j < common_count; j++)
                        if (operands[j] != SELECT_BOTTOM_TYPE &&
                            !global_type_is_compat(
                                eng, operands[j], eng, label_types[j], 0))
                            return SELECT_VALIDATION_INVALID;
                }
                top = control->height;
                control->unreachable = 1;
                break;
            }
            case 0x1a: {
                wasm_valtype ignored;
                if (!select_validation_pop(stack, &top, control, &ignored))
                    return SELECT_VALIDATION_INVALID;
                break;
            }
            case 0x1b: {
                wasm_valtype condition, second, first;
                if (!select_validation_pop(stack, &top, control, &condition) ||
                    !select_validation_pop(stack, &top, control, &second) ||
                    !select_validation_pop(stack, &top, control, &first) ||
                    (condition != SELECT_BOTTOM_TYPE &&
                     condition != WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                wasm_valtype result = first == SELECT_BOTTOM_TYPE ? second : first;
                if (!instr->simd_op && second != SELECT_BOTTOM_TYPE &&
                    first != SELECT_BOTTOM_TYPE &&
                    first != second)
                    return SELECT_VALIDATION_INVALID;
                if (!instr->simd_op) {
                    if (result != SELECT_BOTTOM_TYPE &&
                        is_reference_type(result))
                        return SELECT_VALIDATION_INVALID;
                } else {
                    wasm_valtype selected_type;
                    memcpy(&selected_type, instr->v128_imm.bytes,
                           sizeof(selected_type));
                    if ((first != SELECT_BOTTOM_TYPE &&
                         !global_type_is_compat(eng, first, eng,
                                                selected_type, 0)) ||
                        (second != SELECT_BOTTOM_TYPE &&
                         !global_type_is_compat(eng, second, eng,
                                                selected_type, 0)))
                        return SELECT_VALIDATION_INVALID;
                    result = selected_type;
                }
                if (!select_validation_push(stack, &top, result))
                    return SELECT_VALIDATION_INCONCLUSIVE;
                break;
            }
            case 0x20: {
                uint32_t index = instr->u32_imm;
                wasm_valtype type;
                if (!select_validation_local_type(signature, func, index, &type) ||
                    !validation_local_initialized(initialized, index))
                    return SELECT_VALIDATION_INVALID;
                if (!select_validation_push(stack, &top, type))
                    return SELECT_VALIDATION_INCONCLUSIVE;
                break;
            }
            case 0x21:
            case 0x22: {
                wasm_valtype type;
                if (!select_validation_local_type(signature, func,
                                                  instr->u32_imm, &type) ||
                    !select_validation_pop_type(stack, &top, control, type))
                    return SELECT_VALIDATION_INVALID;
                validation_initialize_local(initialized, instr->u32_imm);
                if (instr->opcode == 0x22 &&
                    !select_validation_push(stack, &top, type))
                    return SELECT_VALIDATION_INCONCLUSIVE;
                break;
            }
            case 0x23:
                if (instr->u32_imm >= eng->global_count)
                    return SELECT_VALIDATION_INVALID;
                if (!select_validation_push(
                        stack, &top,
                        eng->globals[instr->u32_imm]->value.type))
                    return SELECT_VALIDATION_INCONCLUSIVE;
                break;
            case 0x24:
                if (instr->u32_imm >= eng->global_count ||
                    !eng->globals[instr->u32_imm]->mutable_ ||
                    !select_validation_pop_type(
                        stack, &top, control,
                        eng->globals[instr->u32_imm]->value.type))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x25:
                if (instr->u32_imm >= eng->table_count ||
                    !select_validation_pop_type(stack, &top, control,
                                                WASM_VALTYPE_I32) ||
                    !select_validation_push(
                        stack, &top,
                        eng->tables[instr->u32_imm]->element_type))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x26:
                if (instr->u32_imm >= eng->table_count ||
                    !select_validation_pop_type(
                        stack, &top, control,
                        eng->tables[instr->u32_imm]->element_type) ||
                    !select_validation_pop_type(stack, &top, control,
                                                WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x10:
            case 0x12: {
                const exec_func_type *callee;
                if (instr->u32_imm < eng->import_func_count)
                    callee = &eng->types[
                        eng->import_func_types[instr->u32_imm]];
                else {
                    uint32_t index = instr->u32_imm - eng->import_func_count;
                    if (index >= eng->func_count)
                        return SELECT_VALIDATION_INVALID;
                    callee = &eng->types[eng->funcs[index].type_index];
                }
                for (int i = callee->param_count; i > 0; i--)
                    if (!select_validation_pop_type(
                            stack, &top, control, callee->params[i - 1]))
                        return SELECT_VALIDATION_INVALID;
                if (instr->opcode == 0x12) {
                    if (callee->result_count != signature->result_count)
                        return SELECT_VALIDATION_INVALID;
                    for (int i = 0; i < callee->result_count; i++)
                        if (!global_type_is_compat(
                                eng, callee->results[i], eng,
                                signature->results[i], 0))
                            return SELECT_VALIDATION_INVALID;
                    top = control->height;
                    control->unreachable = 1;
                    control->tail_call_seen = 1;
                } else {
                    for (int i = 0; i < callee->result_count; i++)
                        if (!select_validation_push(stack, &top,
                                                    callee->results[i]))
                            return SELECT_VALIDATION_INCONCLUSIVE;
                }
                break;
            }
            case 0x11:
            case 0x13: {
                if (instr->u32_imm >= eng->type_count ||
                    instr->simd_op >= eng->table_count ||
                    !is_function_reference_type(
                        eng->tables[instr->simd_op]->element_type) ||
                    !select_validation_pop_type(stack, &top, control,
                                                WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                const exec_func_type *callee = &eng->types[instr->u32_imm];
                for (int i = callee->param_count; i > 0; i--)
                    if (!select_validation_pop_type(
                            stack, &top, control, callee->params[i - 1]))
                        return SELECT_VALIDATION_INVALID;
                if (instr->opcode == 0x13) {
                    if (callee->result_count != signature->result_count)
                        return SELECT_VALIDATION_INVALID;
                    for (int i = 0; i < callee->result_count; i++)
                        if (!global_type_is_compat(
                                eng, callee->results[i], eng,
                                signature->results[i], 0))
                            return SELECT_VALIDATION_INVALID;
                    top = control->height;
                    control->unreachable = 1;
                    control->tail_call_seen = 1;
                } else {
                    for (int i = 0; i < callee->result_count; i++)
                        if (!select_validation_push(stack, &top,
                                                    callee->results[i]))
                            return SELECT_VALIDATION_INCONCLUSIVE;
                }
                break;
            }
            case 0x14:
            case 0x15: {
                if (instr->u32_imm >= eng->type_count)
                    return SELECT_VALIDATION_INVALID;
                const exec_func_type *callee = &eng->types[instr->u32_imm];
                wasm_valtype reference;
                if (!select_validation_pop(stack, &top, control, &reference) ||
                    (reference != SELECT_BOTTOM_TYPE &&
                     (!WASM_VALTYPE_IS_TYPE_REF(reference) ||
                      !same_func_type(
                          eng, WASM_VALTYPE_TYPE_REF_INDEX(reference),
                          eng, instr->u32_imm))))
                    return SELECT_VALIDATION_INVALID;
                for (int i = callee->param_count; i > 0; i--)
                    if (!select_validation_pop_type(
                            stack, &top, control, callee->params[i - 1]))
                        return SELECT_VALIDATION_INVALID;
                if (instr->opcode == 0x15) {
                    if (callee->result_count != signature->result_count)
                        return SELECT_VALIDATION_INVALID;
                    for (int i = 0; i < callee->result_count; i++)
                        if (!global_type_is_compat(
                                eng, callee->results[i], eng,
                                signature->results[i], 0))
                            return SELECT_VALIDATION_INVALID;
                    top = control->height;
                    control->unreachable = 1;
                    control->tail_call_seen = 1;
                } else {
                    for (int i = 0; i < callee->result_count; i++)
                        if (!select_validation_push(stack, &top,
                                                    callee->results[i]))
                            return SELECT_VALIDATION_INCONCLUSIVE;
                }
                break;
            }
            case 0x41:
                if (!select_validation_push(stack, &top, WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INCONCLUSIVE;
                break;
            case 0x42:
                if (!select_validation_push(stack, &top, WASM_VALTYPE_I64))
                    return SELECT_VALIDATION_INCONCLUSIVE;
                break;
            case 0x43:
                if (!select_validation_push(stack, &top, WASM_VALTYPE_F32))
                    return SELECT_VALIDATION_INCONCLUSIVE;
                break;
            case 0x44:
                if (!select_validation_push(stack, &top, WASM_VALTYPE_F64))
                    return SELECT_VALIDATION_INCONCLUSIVE;
                break;
            case 0x28: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
            case 0x29: case 0x30: case 0x31: case 0x32: case 0x33:
            case 0x34: case 0x35:
            case 0x2a: case 0x2b: {
                uint32_t natural = instr->opcode == 0x29 ||
                                   instr->opcode == 0x2b ? 3u :
                                   instr->opcode == 0x28 ||
                                   instr->opcode == 0x2a ||
                                   instr->opcode == 0x2e ||
                                   instr->opcode == 0x2f ||
                                   instr->opcode == 0x32 ||
                                   instr->opcode == 0x33 ||
                                   instr->opcode == 0x34 ||
                                   instr->opcode == 0x35 ? 2u :
                                   instr->opcode == 0x30 ||
                                   instr->opcode == 0x31 ? 0u : 0u;
                if (instr->opcode == 0x2e || instr->opcode == 0x2f ||
                    instr->opcode == 0x32 || instr->opcode == 0x33)
                    natural = 1;
                wasm_valtype result = instr->opcode == 0x29 ||
                                      (instr->opcode >= 0x30 &&
                                       instr->opcode <= 0x35) ?
                                      WASM_VALTYPE_I64 :
                                      instr->opcode == 0x2a ?
                                      WASM_VALTYPE_F32 :
                                      instr->opcode == 0x2b ?
                                      WASM_VALTYPE_F64 : WASM_VALTYPE_I32;
                if (instr->memory_index >= eng->memory_count ||
                    instr->simd_op > natural ||
                    !select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32, result))
                    return SELECT_VALIDATION_INVALID;
                break;
            }
            case 0x36: case 0x3a: case 0x3b:
            case 0x37: case 0x3c: case 0x3d: case 0x3e:
            case 0x38: case 0x39: {
                uint32_t natural = instr->opcode == 0x37 ||
                                   instr->opcode == 0x39 ? 3u :
                                   instr->opcode == 0x36 ||
                                   instr->opcode == 0x38 ||
                                   instr->opcode == 0x3e ? 2u :
                                   instr->opcode == 0x3b ||
                                   instr->opcode == 0x3d ? 1u : 0u;
                wasm_valtype value_type = instr->opcode == 0x37 ||
                                          (instr->opcode >= 0x3c &&
                                           instr->opcode <= 0x3e) ?
                                          WASM_VALTYPE_I64 :
                                          instr->opcode == 0x38 ?
                                          WASM_VALTYPE_F32 :
                                          instr->opcode == 0x39 ?
                                          WASM_VALTYPE_F64 : WASM_VALTYPE_I32;
                if (instr->memory_index >= eng->memory_count ||
                    instr->simd_op > natural ||
                    !select_validation_pop_type(stack, &top, control,
                                                value_type) ||
                    !select_validation_pop_type(stack, &top, control,
                                                WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            }
            case 0x3f:
                if (instr->memory_index >= eng->memory_count ||
                    !select_validation_push(stack, &top,
                                            WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x40:
                if (instr->memory_index >= eng->memory_count ||
                    !select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x45:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a:
            case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
                if (!select_validation_binary(stack, &top, control,
                                              WASM_VALTYPE_I32,
                                              WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x50:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I64,
                                             WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x51: case 0x52: case 0x53: case 0x54: case 0x55:
            case 0x56: case 0x57: case 0x58: case 0x59: case 0x5a:
                if (!select_validation_binary(stack, &top, control,
                                              WASM_VALTYPE_I64,
                                              WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x5b: case 0x5c: case 0x5d:
            case 0x5e: case 0x5f: case 0x60:
                if (!select_validation_binary(stack, &top, control,
                                              WASM_VALTYPE_F32,
                                              WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x61: case 0x62: case 0x63:
            case 0x64: case 0x65: case 0x66:
                if (!select_validation_binary(stack, &top, control,
                                              WASM_VALTYPE_F64,
                                              WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x67: case 0x68: case 0x69:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e:
            case 0x6f: case 0x70: case 0x71: case 0x72: case 0x73:
            case 0x74: case 0x75: case 0x76: case 0x77: case 0x78:
                if (!select_validation_binary(stack, &top, control,
                                              WASM_VALTYPE_I32,
                                              WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x79: case 0x7a: case 0x7b:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I64,
                                             WASM_VALTYPE_I64))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x7c: case 0x7d: case 0x7e: case 0x7f: case 0x80:
            case 0x81: case 0x82: case 0x83: case 0x84: case 0x85:
            case 0x86: case 0x87: case 0x88: case 0x89: case 0x8a:
                if (!select_validation_binary(stack, &top, control,
                                              WASM_VALTYPE_I64,
                                              WASM_VALTYPE_I64))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x8b: case 0x8c: case 0x8d: case 0x8e:
            case 0x8f: case 0x90: case 0x91:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F32,
                                             WASM_VALTYPE_F32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x92: case 0x93: case 0x94: case 0x95:
            case 0x96: case 0x97: case 0x98:
                if (!select_validation_binary(stack, &top, control,
                                              WASM_VALTYPE_F32,
                                              WASM_VALTYPE_F32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0x99: case 0x9a: case 0x9b: case 0x9c:
            case 0x9d: case 0x9e: case 0x9f:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F64,
                                             WASM_VALTYPE_F64))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xa0: case 0xa1: case 0xa2: case 0xa3:
            case 0xa4: case 0xa5: case 0xa6:
                if (!select_validation_binary(stack, &top, control,
                                              WASM_VALTYPE_F64,
                                              WASM_VALTYPE_F64))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xa7:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I64,
                                             WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xa8: case 0xa9:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F32,
                                             WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xaa: case 0xab:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F64,
                                             WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xac: case 0xad:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_I64))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xae: case 0xaf:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F32,
                                             WASM_VALTYPE_I64))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xb0: case 0xb1:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F64,
                                             WASM_VALTYPE_I64))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xb2: case 0xb3:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_F32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xb4: case 0xb5:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I64,
                                             WASM_VALTYPE_F32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xb6:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F64,
                                             WASM_VALTYPE_F32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xb7: case 0xb8:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_F64))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xb9: case 0xba:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I64,
                                             WASM_VALTYPE_F64))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xbb:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F32,
                                             WASM_VALTYPE_F64))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xbc:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F32,
                                             WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xbd:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_F64,
                                             WASM_VALTYPE_I64))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xbe:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_F32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xbf:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I64,
                                             WASM_VALTYPE_F64))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xc0: case 0xc1:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I32,
                                             WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xc2: case 0xc3: case 0xc4:
                if (!select_validation_unary(stack, &top, control,
                                             WASM_VALTYPE_I64,
                                             WASM_VALTYPE_I64))
                    return SELECT_VALIDATION_INVALID;
                break;
            case 0xd0: {
                int32_t heap_type = (int32_t)instr->u32_imm;
                wasm_valtype type;
                if (!nullable_reference_for_heap(eng, heap_type, &type))
                    return SELECT_VALIDATION_INVALID;
                if (!select_validation_push(stack, &top, type))
                    return SELECT_VALIDATION_INCONCLUSIVE;
                break;
            }
            case 0xd1: {
                wasm_valtype value;
                if (!select_validation_pop(stack, &top, control, &value) ||
                    (value != SELECT_BOTTOM_TYPE &&
                     !is_reference_type(value)) ||
                    !select_validation_push(stack, &top,
                                            WASM_VALTYPE_I32))
                    return SELECT_VALIDATION_INVALID;
                break;
            }
            case 0xd2:
                if (instr->u32_imm >=
                    eng->import_func_count + eng->func_count ||
                    !eng->declared_funcs[instr->u32_imm])
                    return SELECT_VALIDATION_INVALID;
                {
                    uint32_t function_type =
                        instr->u32_imm < eng->import_func_count ?
                        eng->import_func_types[instr->u32_imm] :
                        eng->funcs[instr->u32_imm -
                                   eng->import_func_count].type_index;
                    if (function_type >= 0x100u ||
                        !select_validation_push(
                            stack, &top,
                            (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE +
                                           function_type)))
                        return SELECT_VALIDATION_INCONCLUSIVE;
                }
                break;
            case 0xFD: {
                uint32_t op = instr->simd_op;
                wast_simd_info info;
                if (!wast_simd_get_info(op, &info))
                    return SELECT_VALIDATION_INVALID;
                if ((info.immediate == WAST_SIMD_IMM_MEMARG ||
                     info.immediate == WAST_SIMD_IMM_MEMARG_LANE) &&
                    (instr->memory_index >= eng->memory_count ||
                     instr->alignment > info.natural_alignment))
                    return SELECT_VALIDATION_INVALID;
                if (op == 0x0c) {
                    if (!select_validation_push(stack,&top,WASM_VALTYPE_V128))
                        return SELECT_VALIDATION_INCONCLUSIVE;
                } else if (op <= 0x0a || op == 0x5c || op == 0x5d) {
                    if (!select_validation_unary(stack,&top,control,
                            WASM_VALTYPE_I32,WASM_VALTYPE_V128))
                        return SELECT_VALIDATION_INVALID;
                } else if (op == 0x0b) {
                    if (!select_validation_pop_type(stack,&top,control,WASM_VALTYPE_V128) ||
                        !select_validation_pop_type(stack,&top,control,WASM_VALTYPE_I32))
                        return SELECT_VALIDATION_INVALID;
                } else if (op >= 0x54 && op <= 0x57) {
                    if (!select_validation_pop_type(stack,&top,control,WASM_VALTYPE_V128) ||
                        !select_validation_pop_type(stack,&top,control,WASM_VALTYPE_I32) ||
                        !select_validation_push(stack,&top,WASM_VALTYPE_V128))
                        return SELECT_VALIDATION_INVALID;
                } else if (op >= 0x58 && op <= 0x5b) {
                    if (!select_validation_pop_type(stack,&top,control,WASM_VALTYPE_V128) ||
                        !select_validation_pop_type(stack,&top,control,WASM_VALTYPE_I32))
                        return SELECT_VALIDATION_INVALID;
                } else if (op >= 0x0f && op <= 0x14) {
                    wasm_valtype input = op <= 0x11 ? WASM_VALTYPE_I32 :
                        op == 0x12 ? WASM_VALTYPE_I64 :
                        op == 0x13 ? WASM_VALTYPE_F32 : WASM_VALTYPE_F64;
                    if (!select_validation_unary(stack,&top,control,input,WASM_VALTYPE_V128))
                        return SELECT_VALIDATION_INVALID;
                } else if (op >= 0x15 && op <= 0x22) {
                    int replace = op==0x17||op==0x1a||op==0x1c||op==0x1e||op==0x20||op==0x22;
                    wasm_valtype scalar = op <= 0x1c ? WASM_VALTYPE_I32 :
                        op <= 0x1e ? WASM_VALTYPE_I64 :
                        op <= 0x20 ? WASM_VALTYPE_F32 : WASM_VALTYPE_F64;
                    if (replace) {
                        if (!select_validation_pop_type(stack,&top,control,scalar) ||
                            !select_validation_pop_type(stack,&top,control,WASM_VALTYPE_V128) ||
                            !select_validation_push(stack,&top,WASM_VALTYPE_V128))
                            return SELECT_VALIDATION_INVALID;
                    } else if (!select_validation_unary(stack,&top,control,
                                   WASM_VALTYPE_V128,scalar))
                        return SELECT_VALIDATION_INVALID;
                } else if (op==0x53||op==0x63||op==0x64||op==0x83||op==0x84||
                           op==0xa3||op==0xa4||op==0xc3||op==0xc4) {
                    if (!select_validation_unary(stack,&top,control,
                            WASM_VALTYPE_V128,WASM_VALTYPE_I32))
                        return SELECT_VALIDATION_INVALID;
                } else if (op==0x6b||op==0x6c||op==0x6d||op==0x8b||op==0x8c||
                           op==0x8d||op==0xab||op==0xac||op==0xad||op==0xcb||
                           op==0xcc||op==0xcd) {
                    if (!select_validation_pop_type(stack,&top,control,WASM_VALTYPE_I32) ||
                        !select_validation_pop_type(stack,&top,control,WASM_VALTYPE_V128) ||
                        !select_validation_push(stack,&top,WASM_VALTYPE_V128))
                        return SELECT_VALIDATION_INVALID;
                } else if (op==0x52||(op>=0x105&&op<=0x10c)||op==0x113) {
                    for (int operand=0;operand<3;operand++)
                        if (!select_validation_pop_type(stack,&top,control,WASM_VALTYPE_V128))
                            return SELECT_VALIDATION_INVALID;
                    if (!select_validation_push(stack,&top,WASM_VALTYPE_V128))
                        return SELECT_VALIDATION_INCONCLUSIVE;
                } else {
                    int unary = op==0x4d||op==0x5e||op==0x5f||
                        (op>=0x60&&op<=0x62)||(op>=0x67&&op<=0x6a)||
                        op==0x74||op==0x75||op==0x7a||(op>=0x7c&&op<=0x81)||
                        (op>=0x87&&op<=0x8a)||op==0x94||op==0xa0||op==0xa1||
                        (op>=0xa7&&op<=0xaa)||op==0xc0||op==0xc1||
                        (op>=0xc7&&op<=0xca)||op==0xe0||op==0xe1||op==0xe3||
                        op==0xec||op==0xed||op==0xef||(op>=0xf8&&op<=0xff)||
                        (op>=0x101&&op<=0x104);
                    int valid = unary ?
                        select_validation_unary(stack,&top,control,
                            WASM_VALTYPE_V128,WASM_VALTYPE_V128) :
                        select_validation_binary(stack,&top,control,
                            WASM_VALTYPE_V128,WASM_VALTYPE_V128);
                    if (!valid) return SELECT_VALIDATION_INVALID;
                }
                break;
            }
            case 0xFC: {
                uint32_t sub = instr->simd_op;
                if (sub <= 7) {
                    /* i32/i64.trunc_sat_f32/f64_s/u */
                    wasm_valtype from = (sub < 4) ?
                        ((sub & 2) ? WASM_VALTYPE_F64 : WASM_VALTYPE_F32) :
                        ((sub & 2) ? WASM_VALTYPE_F64 : WASM_VALTYPE_F32);
                    wasm_valtype to = (sub < 4) ? WASM_VALTYPE_I32 : WASM_VALTYPE_I64;
                    if (!select_validation_unary(stack, &top, control, from, to))
                        return SELECT_VALIDATION_INVALID;
                } else if (sub == 10) { /* memory.copy: [i32 i32 i32] -> [] */
                    uint32_t src_mem = instr->source_memory_index;
                    if (instr->memory_index >= eng->memory_count ||
                        src_mem >= eng->memory_count ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32))
                        return SELECT_VALIDATION_INVALID;
                } else if (sub == 11) { /* memory.fill: [i32 i32 i32] -> [] */
                    if (instr->memory_index >= eng->memory_count ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32))
                        return SELECT_VALIDATION_INVALID;
                } else if (sub == 8) { /* memory.init: [i32 i32 i32] -> [] */
                    if (instr->memory_index >= eng->memory_count ||
                        !eng->has_data_count ||
                        instr->u32_imm >= eng->declared_data_count ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32))
                        return SELECT_VALIDATION_INVALID;
                } else if (sub == 9) { /* data.drop: [] -> [] */
                    if (!eng->has_data_count ||
                        instr->u32_imm >= eng->declared_data_count)
                        return SELECT_VALIDATION_INVALID;
                } else if (sub == 12) { /* table.init: [i32 i32 i32] -> [] */
                    uint32_t table_index = instr->v128_imm.bytes[0];
                    if (instr->u32_imm >= eng->elem_count ||
                        table_index >= eng->table_count ||
                        !global_type_is_compat(
                            eng, eng->elem_types[instr->u32_imm],
                            eng->tables[table_index]->type_owner,
                            eng->tables[table_index]->element_type, 0) ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32))
                        return SELECT_VALIDATION_INVALID;
                } else if (sub == 13) { /* elem.drop: [] -> [] */
                    if (instr->u32_imm >= eng->elem_count)
                        return SELECT_VALIDATION_INVALID;
                } else if (sub == 14) { /* table.copy: [i32 i32 i32] -> [] */
                    if (!select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32))
                        return SELECT_VALIDATION_INVALID;
                } else if (sub == 15) { /* table.grow: [ref i32] -> [i32] */
                    if (instr->u32_imm >= eng->table_count ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !select_validation_pop_type(
                            stack, &top, control,
                            eng->tables[instr->u32_imm]->element_type) ||
                        !select_validation_push(stack, &top, WASM_VALTYPE_I32))
                        return SELECT_VALIDATION_INVALID;
                } else if (sub == 16) { /* table.size: [] -> [i32] */
                    if (!select_validation_push(stack, &top, WASM_VALTYPE_I32))
                        return SELECT_VALIDATION_INCONCLUSIVE;
                } else if (sub == 17) { /* table.fill: [i32 ref i32] -> [] */
                    wasm_valtype ref;
                    if (!select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32) ||
                        !select_validation_pop(stack, &top, control, &ref) ||
                        !select_validation_pop_type(stack, &top, control, WASM_VALTYPE_I32))
                        return SELECT_VALIDATION_INVALID;
                } else {
                    return SELECT_VALIDATION_INCONCLUSIVE;
                }
                break;
            }
            default:
                return SELECT_VALIDATION_INCONCLUSIVE;
        }
    }
    return SELECT_VALIDATION_INVALID;
}

static exec_status parse_body(waste_exec_engine *eng, exec_func *func, exec_reader *body, exec_error *err) {
    uint32_t local_groups;
    if (!er_u32(body, &local_groups))
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid local declarations");
    for (uint32_t group = 0; group < local_groups; group++) {
        uint32_t count;
        wasm_valtype value_type;
        if (!er_u32(body, &count) || !er_valtype(body, &value_type) ||
            !value_type_is_defined(eng, value_type) ||
            count > EXEC_MAX_LOCALS - func->local_count)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid local declaration");
        for (uint32_t i = 0; i < count; i++) func->locals[func->local_count++] = value_type;
    }

    /* Allocate instruction buffer based on remaining body size */
    size_t capacity = (size_t)(body->end - body->cursor);
    exec_instr *code = (exec_instr *)calloc(capacity + 1, sizeof(*code));
    if (!code)
        return exec_fail(err, EXEC_ERROR_FORMAT, "code alloc failed");

#define FREE_CODE(c, sz) do { \
    for (uint32_t _i = 0; _i < (sz); _i++) { \
        if ((c)[_i].opcode == 0x0e) { \
            uint32_t *_d; memcpy(&_d, (c)[_i].v128_imm.bytes, sizeof(_d)); free(_d); \
        } \
        free((c)[_i].catches); \
    } \
    free(c); \
} while(0)

    uint32_t code_size = 0;
    uint32_t controls[EXEC_MAX_CONTROL];
    uint32_t control_size = 0;
    while (body->cursor < body->end) {
        uint8_t byte;
        if (!er_u8(body, &byte)) {
            FREE_CODE(code, code_size);
            return exec_fail(err, EXEC_ERROR_FORMAT, "truncated instruction");
        }
        exec_instr instr;
        memset(&instr, 0, sizeof(instr));
        instr.block_type_index = -1;
        if (byte == 0x00 || byte == 0x01) {
            instr.opcode = byte;
        } else if (byte == 0x02 || byte == 0x03 || byte == 0x04 ||
                   byte == 0x1f) {
            uint8_t block_type;
            if (!er_u8(body, &block_type)) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "missing block type"); }
            if (control_size >= EXEC_MAX_CONTROL) {
                FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "control nesting limit exceeded");
            }
            instr.opcode = byte;
            /* A one-byte value type encodes a single-result block. */
            wasm_valtype direct_block_result = WASM_VALTYPE_I32;
            int is_valtype = byte_valtype(block_type,
                                          &direct_block_result);
            if (block_type == 0x40) {
                instr.v128_imm.bytes[0] = 0; instr.v128_imm.bytes[1] = 0;
            } else if (is_valtype) {
                instr.v128_imm.bytes[0] = 0; instr.v128_imm.bytes[1] = 1;
                instr.block_result_type = direct_block_result;
                instr.has_block_result_type = 1;
            } else {
                body->cursor--;
                int32_t type_index;
                if (!er_i32(body, &type_index) || type_index < 0 || (uint32_t)type_index >= eng->type_count) {
                    FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid block type index");
                }
                exec_func_type *block_sig = &eng->types[type_index];
                instr.v128_imm.bytes[0] = (uint8_t)block_sig->param_count;
                instr.v128_imm.bytes[1] = (uint8_t)block_sig->result_count;
                instr.block_type_index = type_index;
            }
            if (byte == 0x1f) {
                uint32_t catch_count;
                if (!er_u32(body, &catch_count) ||
                    catch_count > WAST_MAX_TAGS) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "invalid try_table catches");
                }
                instr.catches = (exec_catch *)calloc(
                    catch_count ? catch_count : 1, sizeof(*instr.catches));
                if (!instr.catches) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "catch allocation failed");
                }
                instr.catch_count = catch_count;
                for (uint32_t i = 0; i < catch_count; i++) {
                    exec_catch *catch_ = &instr.catches[i];
                    if (!er_u8(body, &catch_->kind) || catch_->kind > 3 ||
                        (catch_->kind < 2 &&
                         (!er_u32(body, &catch_->tag_index) ||
                          catch_->tag_index >= eng->tag_count)) ||
                        !er_u32(body, &catch_->depth)) {
                        free(instr.catches);
                        instr.catches = NULL;
                        FREE_CODE(code, code_size);
                        return exec_fail(err, EXEC_ERROR_FORMAT,
                                         "invalid try_table catch");
                    }
                }
            }
            controls[control_size++] = code_size;
        } else if (byte == 0x08) {
            uint32_t tag_index;
            if (!er_u32(body, &tag_index) || tag_index >= eng->tag_count) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid throw tag index");
            }
            instr.opcode = byte;
            instr.u32_imm = tag_index;
        } else if (byte == 0x05) {
            if (!control_size || code[controls[control_size - 1]].opcode != 0x04) {
                FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "else without if");
            }
            instr.opcode = byte;
            code[controls[control_size - 1]].u32_imm = code_size + 1;
        } else if (byte == 0x0c || byte == 0x0d) {
            uint32_t depth;
            if (!er_u32(body, &depth)) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid branch depth"); }
            instr.opcode = byte; instr.u32_imm = depth;
        } else if (byte == 0x0e) {
            /* br_table: u32_imm = count, v128_imm stores uint32_t* to depths[count+1] */
            uint32_t count;
            if (!er_u32(body, &count) || count > 0xFFFF) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid br_table"); }
            uint32_t *depths = (uint32_t *)malloc((count + 1) * sizeof(uint32_t));
            if (!depths) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "br_table alloc failed"); }
            for (uint32_t i = 0; i <= count; i++) {
                if (!er_u32(body, &depths[i])) { free(depths); FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid br_table label"); }
            }
            instr.opcode = byte; instr.u32_imm = count;
            memcpy(instr.v128_imm.bytes, &depths, sizeof(depths)); /* store pointer */
        } else if (byte == 0x10 || byte == 0x12 || byte == 0x14 ||
                   byte == 0x15 || (byte >= 0x20 && byte <= 0x26)) {
            uint32_t idx;
            if (!er_u32(body, &idx)) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT, "truncated instruction immediate");
            }
            instr.opcode  = byte;
            instr.u32_imm = idx;
        } else if (byte == 0x11 || byte == 0x13) {
            uint32_t type_index, table_index;
            if (!er_u32(body,&type_index) || type_index>=eng->type_count ||
                !er_u32(body,&table_index) || table_index>=eng->table_count) {
                FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid call_indirect immediate");
            }
            instr.opcode=byte;instr.u32_imm=type_index;instr.simd_op=table_index;
        } else if (byte >= 0x28 && byte <= 0x3e) {
            uint32_t align, offset, memory_index = 0;
            if (!er_u32(body, &align) || align >= 0x80u ||
                ((align & 0x40u) && !er_u32(body, &memory_index)) ||
                !er_u32(body, &offset)) {
                FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid memory immediate");
            }
            instr.opcode = byte; instr.simd_op = align & 0x3fu;
            instr.u32_imm = offset; instr.memory_index = memory_index;
        } else if (byte == 0x3f || byte == 0x40) {
            uint32_t memory_index;
            if (!er_u32(body, &memory_index)) {
                FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid memory index");
            }
            instr.opcode = byte; instr.memory_index = memory_index;
        } else if (byte == 0xd0) {
            int32_t heap_type;
            if (!er_i32(body, &heap_type)) {
                FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "truncated ref.null");
            }
            instr.opcode=byte; instr.u32_imm=(uint32_t)heap_type;
        } else if (byte == 0xd1 || byte == 0xd4) {
            instr.opcode=byte;
        } else if (byte == 0xd5 || byte == 0xd6) {
            uint32_t depth;
            if (!er_u32(body, &depth)) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid reference branch depth");
            }
            instr.opcode = byte;
            instr.u32_imm = depth;
        } else if (byte == 0xd2) {
            uint32_t function;
            if (!er_u32(body, &function)) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid ref.func"); }
            instr.opcode=byte; instr.u32_imm=function;
        } else if (byte == 0x1c) {
            uint32_t count;
            wasm_valtype selected_type;
            if (!er_u32(body, &count) || count != 1 ||
                !er_valtype(body, &selected_type) ||
                !value_type_is_defined(eng, selected_type)) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid typed select immediate");
            }
            instr.opcode = 0x1b;
            instr.simd_op = 1;
            memcpy(instr.v128_imm.bytes, &selected_type,
                   sizeof(selected_type));
        } else if (byte == 0x0f || byte == 0x1a || byte == 0x1b) {
            instr.opcode = byte;
        } else if (byte == 0x41) {
            int32_t value;
            if (!er_i32(body, &value)) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid i32.const");
            }
            instr.opcode = byte;
            instr.u32_imm = (uint32_t)value;
        } else if (byte == 0x42) {
            int64_t value;
            if (!er_i64(body, &value)) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid i64.const"); }
            instr.opcode = byte; memcpy(instr.v128_imm.bytes, &value, 8);
        } else if (byte == 0x43 || byte == 0x44) {
            size_t width = byte == 0x43 ? 4 : 8; const uint8_t *value;
            if (!er_bytes(body, width, &value)) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid float const"); }
            instr.opcode = byte; memcpy(instr.v128_imm.bytes, value, width);
        } else if ((byte >= 0x45 && byte <= 0x4f) ||   /* i32 cmp */
                   (byte >= 0x50 && byte <= 0x5a) ||   /* i64 cmp */
                   (byte >= 0x5b && byte <= 0x60) ||   /* f32 cmp */
                   (byte >= 0x61 && byte <= 0x66) ||   /* f64 cmp */
                   (byte >= 0x67 && byte <= 0x78) ||   /* i32 arith */
                   (byte >= 0x79 && byte <= 0x8a) ||   /* i64 arith */
                   (byte >= 0x8b && byte <= 0x98) ||   /* f32 arith */
                   (byte >= 0x99 && byte <= 0xa6) ||   /* f64 arith */
                   (byte >= 0xa7 && byte <= 0xbf) ||   /* conversions */
                   byte == 0xc0 || byte == 0xc1 ||     /* i32.extendN_s */
                   (byte >= 0xc2 && byte <= 0xc4)) {   /* i64.extendN_s */
            instr.opcode = byte;
        } else if (byte == 0xFC) {
            /* Saturating trunc (0x00-0x07) and bulk memory ops */
            uint32_t sub_op;
            if (!er_u32(body, &sub_op)) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "truncated 0xFC op"); }
            instr.opcode  = 0xFC;
            instr.simd_op = sub_op;
            if (sub_op >= 8 && sub_op <= 11) {
                /* memory.init / data.drop / memory.copy / memory.fill */
                uint32_t seg = 0, mem = 0;
                if (sub_op == 10) { /* memory.copy: dst mem, src mem */
                    if (!er_u32(body,&mem)||!er_u32(body,&seg)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid memory.copy"); }
                    instr.memory_index = mem;
                    instr.source_memory_index = seg;
                } else if (sub_op == 11) { /* memory.fill: one mem index */
                    if (!er_u32(body,&mem)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid memory.fill"); }
                    instr.memory_index = mem;
                } else if (sub_op == 8) { /* memory.init: seg, mem */
                    if (!er_u32(body,&seg)||!er_u32(body,&mem)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid memory.init"); }
                    eng->uses_data_count_instruction = 1;
                    instr.u32_imm = seg;
                    instr.memory_index = mem;
                } else { /* data.drop: seg */
                    if (!er_u32(body,&seg)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid data.drop"); }
                    eng->uses_data_count_instruction = 1;
                    instr.u32_imm = seg;
                }
            } else if (sub_op >= 12 && sub_op <= 17) {
                /* table ops: table.init(12), elem.drop(13), table.copy(14),
                   table.grow(15), table.size(16), table.fill(17) */
                uint32_t a, b;
                if (sub_op == 12) { /* table.init: elem idx, table idx */
                    if (!er_u32(body,&a)||!er_u32(body,&b)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table.init"); }
                    instr.u32_imm = a; /* elem segment */
                    instr.v128_imm.bytes[0]=(uint8_t)b; /* table idx */
                } else if (sub_op == 13) { /* elem.drop: elem idx */
                    if (!er_u32(body,&a)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid elem.drop"); }
                    instr.u32_imm = a;
                } else { /* table.copy, table.grow, table.size, table.fill: one or two table indices */
                    if (sub_op == 14) { /* table.copy: dst, src */
                        if (!er_u32(body,&a)||!er_u32(body,&b)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table.copy"); }
                        instr.u32_imm = a; instr.v128_imm.bytes[0]=(uint8_t)b;
                    } else { /* table.grow/size/fill: table idx */
                        if (!er_u32(body,&a)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table op"); }
                        instr.u32_imm = a;
                    }
                }
            }
            /* sub_op 0x00-0x07 (sat trunc) have no immediates */
        } else if (byte == 0xFD) {
            uint32_t simd_op;
            wast_simd_info simd_info;
            if (!er_u32(body, &simd_op)) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT, "truncated SIMD op");
            }
            instr.opcode  = 0xFD;
            instr.simd_op = simd_op;
            if (!wast_simd_get_info(simd_op, &simd_info)) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT, "unknown SIMD opcode");
            }
            if (simd_info.immediate == WAST_SIMD_IMM_CONST ||
                simd_info.immediate == WAST_SIMD_IMM_SHUFFLE) {
                const uint8_t *imm;
                if (!er_bytes(body, 16, &imm)) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT, "truncated SIMD immediate");
                }
                memcpy(instr.v128_imm.bytes, imm, 16);
                if (simd_info.immediate == WAST_SIMD_IMM_SHUFFLE) {
                    for (uint32_t lane = 0; lane < 16; lane++) {
                        if (instr.v128_imm.bytes[lane] >= 32) {
                            FREE_CODE(code, code_size);
                            return exec_fail(err, EXEC_ERROR_FORMAT,
                                             "SIMD shuffle lane out of range");
                        }
                    }
                }
            } else if (simd_info.immediate == WAST_SIMD_IMM_LANE) {
                const uint8_t *lane;
                if (!er_bytes(body, 1, &lane) || *lane >= simd_info.lane_count) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "SIMD lane out of range");
                }
                instr.lane_index = *lane;
            } else if (simd_info.immediate == WAST_SIMD_IMM_MEMARG ||
                       simd_info.immediate == WAST_SIMD_IMM_MEMARG_LANE) {
                uint32_t align, offset, memory_index = 0;
                if (!er_u32(body, &align)) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT, "invalid SIMD memarg");
                }
                if (align & 0x40u) {
                    align &= ~0x40u;
                    if (!er_u32(body, &memory_index)) {
                        FREE_CODE(code, code_size);
                        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid SIMD memory index");
                    }
                }
                if (!er_u32(body, &offset)) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT, "invalid SIMD offset");
                }
                instr.alignment = align;
                instr.memory_index = memory_index;
                instr.u32_imm = offset;
                if (simd_info.immediate == WAST_SIMD_IMM_MEMARG_LANE) {
                    const uint8_t *lane;
                    if (!er_bytes(body, 1, &lane) || *lane >= simd_info.lane_count) {
                        FREE_CODE(code, code_size);
                        return exec_fail(err, EXEC_ERROR_FORMAT,
                                         "SIMD lane out of range");
                    }
                    instr.lane_index = *lane;
                }
            }
        } else if (byte == 0x0B) {
            instr.opcode = 0x0B;
            code[code_size++] = instr;
            if (control_size) {
                uint32_t open = controls[--control_size];
                code[open].simd_op = code_size - 1;
                if (code[open].opcode == 0x04) {
                    if (code[open].u32_imm == 0) code[open].u32_imm = code_size - 1;
                    else code[code[open].u32_imm - 1].u32_imm = code_size - 1;
                }
                continue;
            } else break; /* function end */
        } else {
            FREE_CODE(code, code_size);
            return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported opcode");
        }
        if (code_size >= (uint32_t)(capacity + 1)) {
            FREE_CODE(code, code_size);
            return exec_fail(err, EXEC_ERROR_FORMAT, "instruction limit exceeded");
        }
        code[code_size++] = instr;
    }

    select_validation_result validation =
        validate_select_function(eng, func, code, code_size);
    if (validation == SELECT_VALIDATION_INVALID) {
        FREE_CODE(code, code_size);
        return exec_fail(err, EXEC_ERROR_FORMAT, "select type mismatch");
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
        exec_status st = parse_body(eng, &eng->funcs[i], &body, err);
        if (st != EXEC_OK) {
            if (err && err->message[0]) {
                char detail[sizeof(err->message)];
                snprintf(detail, sizeof(detail), "%s", err->message);
                snprintf(err->message, sizeof(err->message),
                         "function %u: %.220s", i, detail);
            }
            return st;
        }
    }
    return EXEC_OK;
}

/* ---- Module loader ---- */

exec_status exec_load_with_imports(const uint8_t *bytes, size_t size,
                                   const exec_imports *imports,
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
    uint8_t last_section_rank = 0;
    uint8_t seen_sections[14] = {0};

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
            if (section_id > 13) {
                exec_free(eng);
                return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported standard section");
            }
            if (section_id <= 13 && seen_sections[section_id]) {
                exec_free(eng);
                return exec_fail(err, EXEC_ERROR_FORMAT, "duplicate standard section");
            }
            if (section_id <= 13) seen_sections[section_id] = 1;
            uint8_t section_rank = section_id <= 5 ? section_id :
                                   section_id == 13 ? 6 :
                                   section_id <= 9 ? (uint8_t)(section_id + 1) :
                                   section_id == 12 ? 11 :
                                   section_id == 10 ? 12 : 13;
            if (section_rank < last_section_rank) {
                exec_free(eng);
                return exec_fail(err, EXEC_ERROR_FORMAT, "out-of-order section");
            }
            last_section_rank = section_rank;
        }
        exec_reader sec = { r.start, section_bytes, section_bytes + section_size };
        exec_status st = EXEC_OK;
        switch (section_id) {
            case 0: { /* custom section: its leading name is UTF-8 */
                uint32_t name_length;
                const uint8_t *name_bytes;
                if (!er_u32(&sec, &name_length) ||
                    !er_bytes(&sec, name_length, &name_bytes) ||
                    !valid_utf8(name_bytes, name_length))
                    st = exec_fail(err, EXEC_ERROR_FORMAT,
                                   "malformed UTF-8 encoding");
                else
                    sec.cursor = sec.end;
                break;
            }
            case 1:  st = parse_types(eng, &sec, err);   break;
            case 2:  st = parse_imports(eng, &sec, imports, err); break;
            case 3:  st = parse_funcs(eng, &sec, err);   break;
            case 4:  st = parse_tables(eng, &sec, err);  break;
            case 5:  st = parse_memory(eng, &sec, err);  break;
            case 6:  st = parse_globals(eng, &sec, err); break;
            case 7:  st = parse_exports(eng, &sec, err); break;
            case 8:  st = parse_start(eng, &sec, err);   break;
            case 9:  st = parse_elements(eng, &sec, err); break;
            case 10: st = parse_code(eng, &sec, err);    break;
            case 11: st = parse_data(eng, &sec, err);    break;
            case 12:
                if (!er_u32(&sec, &eng->declared_data_count))
                    st = exec_fail(err, EXEC_ERROR_FORMAT,
                                   "invalid data count section");
                else
                    eng->has_data_count = 1;
                break;
            case 13: st = parse_tags(eng, &sec, err); break;
            default: st = exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported standard section"); break;
        }
        if (st != EXEC_OK) {
            exec_free(eng);
            return st;
        }
        if (section_id != 0 && sec.cursor != sec.end) {
            exec_free(eng);
            return exec_fail(err, EXEC_ERROR_FORMAT, "trailing bytes in section");
        }
    }

    if ((eng->func_count != 0 && !seen_sections[10]) ||
        (eng->uses_data_count_instruction && !eng->has_data_count) ||
        (eng->has_data_count && !seen_sections[11] &&
         eng->declared_data_count != 0)) {
        const char *message = eng->func_count != 0 && !seen_sections[10] ?
            "function and code section count mismatch" :
            "data count section required or inconsistent";
        exec_free(eng);
        return exec_fail(err, EXEC_ERROR_FORMAT, message);
    }

    if (eng->has_start && !eng->instantiation_trapped) {
        wasm_value start_results[WAST_MAX_RESULTS];
        int start_result_count = 0;
        exec_error start_err;
        memset(&start_err, 0, sizeof(start_err));
        exec_status st2 = exec_invoke(eng, eng->start_func, NULL, 0,
                                      start_results, &start_result_count, &start_err);
        if (st2 != EXEC_OK) {
            if (err) *err = start_err;
            if (st2 == EXEC_ERROR_TRAP) {
                /* Instantiation failure does not roll back writes performed
                 * by element/data initialization or by the start function.
                 * Return an unaddressable live instance so references that
                 * escaped through imported tables remain callable. */
                *eng_out = eng;
                return st2;
            }
            exec_free(eng);
            return st2;
        }
    }

    if (eng->instantiation_trapped) {
        *eng_out = eng;
        return exec_fail(err, EXEC_ERROR_TRAP,
                         eng->instantiation_error[0] ?
                         eng->instantiation_error :
                         "module instantiation trapped");
    }

    *eng_out = eng;
    return EXEC_OK;
}

exec_status exec_load(const uint8_t *bytes, size_t size,
                      waste_exec_engine **eng_out, exec_error *err) {
    return exec_load_with_imports(bytes, size, NULL, eng_out, err);
}

void exec_free(waste_exec_engine *eng) {
    if (!eng) return;
    for (uint32_t i = 0; i < EXEC_MAX_CALL_DEPTH; i++)
        free(eng->local_frames[i]);
    for (uint32_t i = 0; i < eng->elem_count; i++)
        free(eng->elem_values[i]);
    for (uint32_t i = 0; i < eng->data_count; i++)
        free(eng->data_segs[i]);
    for (uint32_t i = 0; i < eng->func_count; i++) {
        if (eng->funcs[i].code) {
            /* Free br_table depth arrays */
            for (uint32_t j = 0; j < eng->funcs[i].code_size; j++) {
                if (eng->funcs[i].code[j].opcode == 0x0e) {
                    uint32_t *depths; memcpy(&depths, eng->funcs[i].code[j].v128_imm.bytes, sizeof(depths));
                    free(depths);
                }
                free(eng->funcs[i].code[j].catches);
            }
        }
        free(eng->funcs[i].code);
    }
    free(eng->types);
    free(eng->funcs);
    free(eng->exports);
    for (uint32_t i = 0; i < eng->memory_count; i++)
        if (eng->owns_memories[i]) free(eng->memories[i]->data);
    for(uint32_t i=eng->import_table_count;i<eng->table_count;i++) free(eng->owned_tables[i].elements);
    free(eng);
}

exec_status exec_find_export(const waste_exec_engine *eng,
                             const char *name, uint32_t *func_idx,
                             exec_error *err) {
    if (!eng || !name || !func_idx)
        return exec_fail(err, EXEC_ERROR_FORMAT, "null argument");
    for (uint32_t i = 0; i < eng->export_count; i++) {
        if (strcmp(eng->exports[i].name, name) == 0) {
            if (eng->exports[i].kind != 0) continue;
            *func_idx = eng->exports[i].index;
            return EXEC_OK;
        }
    }
    return exec_fail(err, EXEC_ERROR_NOT_FOUND, "export not found");
}

exec_status exec_get_func_type_index(const waste_exec_engine *eng,
                                     uint32_t func_idx,uint32_t *type_index,
                                     exec_error *err) {
    if(!eng||!type_index||func_idx>=eng->import_func_count+eng->func_count)
        return exec_fail(err,EXEC_ERROR_FORMAT,"invalid function index");
    *type_index=func_idx<eng->import_func_count?eng->import_func_types[func_idx]:
        eng->funcs[func_idx-eng->import_func_count].type_index;
    return EXEC_OK;
}

static exec_status find_extern_export(const waste_exec_engine *eng, const char *name,
                                      uint8_t kind, uint32_t *index, exec_error *err) {
    if (!eng || !name || !index) return exec_fail(err,EXEC_ERROR_FORMAT,"null argument");
    for(uint32_t i=0;i<eng->export_count;i++) {
        if (eng->exports[i].kind==kind && strcmp(eng->exports[i].name,name)==0) {
            *index=eng->exports[i].index; return EXEC_OK;
        }
    }
    return exec_fail(err,EXEC_ERROR_NOT_FOUND,"export not found");
}

exec_status exec_find_export_global(const waste_exec_engine *eng, const char *name,
                                    exec_global **global, exec_error *err) {
    uint32_t index; if(!global) return exec_fail(err,EXEC_ERROR_FORMAT,"null argument");
    exec_status status=find_extern_export(eng,name,3,&index,err);
    if(status==EXEC_OK) *global=eng->globals[index];
    return status;
}
exec_status exec_find_export_memory(const waste_exec_engine *eng, const char *name,
                                    exec_memory **memory, exec_error *err) {
    uint32_t index; if(!memory) return exec_fail(err,EXEC_ERROR_FORMAT,"null argument");
    exec_status status=find_extern_export(eng,name,2,&index,err);
    if(status==EXEC_OK) *memory=eng->memories[index];
    return status;
}
exec_status exec_find_export_table(const waste_exec_engine *eng, const char *name,
                                   exec_table **table, exec_error *err) {
    uint32_t index; if(!table) return exec_fail(err,EXEC_ERROR_FORMAT,"null argument");
    exec_status status=find_extern_export(eng,name,1,&index,err);
    if(status==EXEC_OK) *table=eng->tables[index];
    return status;
}
exec_status exec_find_export_tag(const waste_exec_engine *eng, const char *name,
                                 exec_tag **tag, exec_error *err) {
    uint32_t index;
    if(!tag) return exec_fail(err,EXEC_ERROR_FORMAT,"null argument");
    exec_status status=find_extern_export(eng,name,4,&index,err);
    if(status==EXEC_OK) *tag=eng->tags[index];
    return status;
}

/* ---- Value stack ---- */

typedef struct {
    wasm_value vals[EXEC_MAX_STACK];
    int        top;
} exec_stack;

typedef struct {
    uint32_t kind;
    uint32_t start_pc;
    uint32_t end_pc;
    int stack_height;
    int branch_arity;
    int end_arity;
} exec_control;

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

static wasm_value i32_value(uint32_t bits);
static uint32_t trunc_sat_i32_s_f32(float v);
static uint32_t trunc_sat_i32_u_f32(float v);
static uint32_t trunc_sat_i32_s_f64(double v);
static uint32_t trunc_sat_i32_u_f64(double v);

/* ---- v128 helpers as byte arrays ---- */

static wasm_value v128_from_bytes(const uint8_t *b) {
    wasm_value v;
    v.type = WASM_VALTYPE_V128;
    memcpy(v.v128.bytes, b, 16);
    return v;
}

static uint64_t simd_lane_u(const wasm_value *v, uint32_t lane,
                            uint32_t width) {
    uint64_t value = 0;
    memcpy(&value, v->v128.bytes + lane * width, width);
    return value;
}

static int64_t simd_lane_s(const wasm_value *v, uint32_t lane,
                           uint32_t width) {
    uint64_t value = simd_lane_u(v, lane, width);
    if (width < 8 && (value & (UINT64_C(1) << (width * 8 - 1))))
        value |= UINT64_MAX << (width * 8);
    return (int64_t)value;
}

static void simd_set_lane(wasm_value *v, uint32_t lane, uint32_t width,
                          uint64_t value) {
    memcpy(v->v128.bytes + lane * width, &value, width);
}

static wasm_value simd_zero(void) {
    uint8_t bytes[16] = {0};
    return v128_from_bytes(bytes);
}

static int64_t simd_sat_s(int64_t value, uint32_t bits) {
    int64_t min = -(INT64_C(1) << (bits - 1));
    int64_t max = (INT64_C(1) << (bits - 1)) - 1;
    return value < min ? min : value > max ? max : value;
}

static int simd_pop(exec_stack *stack, wasm_value *value) {
    return stack_pop(stack, value) && value->type == WASM_VALTYPE_V128;
}

/* Execute the fixed-width integer, lane, comparison, and bitwise portion of
 * the standard SIMD instruction set.  Returns 1 when the opcode is handled,
 * 0 when a later SIMD family must handle it, and -1 on an execution error. */
static int exec_standard_simd_integer(uint32_t op, uint32_t lane_index,
                                      const wasm_v128 *immediate,
                                      exec_stack *stack, exec_error *err) {
    wasm_value a, b, c, out;
    uint32_t width = 0, lanes, kind = 0;
    uint64_t mask;

    /* Splat and lane access. */
    if (op >= 0x0f && op <= 0x14) {
        wasm_value scalar;
        if (!stack_pop(stack, &scalar)) {
            exec_fail(err, EXEC_ERROR_TRAP, "SIMD splat operand missing");
            return -1;
        }
        out = simd_zero();
        width = op <= 0x0f ? 1 : op == 0x10 ? 2 :
                (op == 0x11 || op == 0x13) ? 4 : 8;
        uint64_t bits = 0;
        if (op <= 0x11) bits = (uint32_t)scalar.i32;
        else if (op == 0x12) bits = (uint64_t)scalar.i64;
        else if (op == 0x13) memcpy(&bits, &scalar.f32, 4);
        else memcpy(&bits, &scalar.f64, 8);
        for (uint32_t i = 0; i < 16 / width; i++)
            simd_set_lane(&out, i, width, bits);
        if (!stack_push(stack, out)) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            return -1;
        }
        return 1;
    }
    if (op >= 0x15 && op <= 0x22) {
        int replace = op == 0x17 || op == 0x1a || op == 0x1c ||
                      op == 0x1e || op == 0x20 || op == 0x22;
        width = op <= 0x17 ? 1 : op <= 0x1a ? 2 :
                op <= 0x1c ? 4 : op <= 0x1e ? 8 :
                op <= 0x20 ? 4 : 8;
        if (replace) {
            wasm_value scalar;
            if (!stack_pop(stack, &scalar) || !simd_pop(stack, &a)) {
                exec_fail(err, EXEC_ERROR_TRAP, "SIMD replace_lane operands missing");
                return -1;
            }
            uint64_t bits = 0;
            if (op <= 0x1c) bits = (uint32_t)scalar.i32;
            else if (op == 0x1e) bits = (uint64_t)scalar.i64;
            else if (op == 0x20) memcpy(&bits, &scalar.f32, 4);
            else memcpy(&bits, &scalar.f64, 8);
            simd_set_lane(&a, lane_index, width, bits);
            if (!stack_push(stack, a)) {
                exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                return -1;
            }
        } else {
            if (!simd_pop(stack, &a)) {
                exec_fail(err, EXEC_ERROR_TRAP, "SIMD extract_lane operand missing");
                return -1;
            }
            uint64_t bits = simd_lane_u(&a, lane_index, width);
            memset(&out, 0, sizeof(out));
            if (op == 0x15 || op == 0x18) {
                out.type = WASM_VALTYPE_I32;
                out.i32 = (int32_t)simd_lane_s(&a, lane_index, width);
            } else if (op == 0x16 || op == 0x19 || op == 0x1b) {
                out.type = WASM_VALTYPE_I32; out.i32 = (int32_t)bits;
            } else if (op == 0x1d) {
                out.type = WASM_VALTYPE_I64; out.i64 = (int64_t)bits;
            } else if (op == 0x1f) {
                out.type = WASM_VALTYPE_F32; memcpy(&out.f32, &bits, 4);
            } else {
                out.type = WASM_VALTYPE_F64; memcpy(&out.f64, &bits, 8);
            }
            if (!stack_push(stack, out)) {
                exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                return -1;
            }
        }
        return 1;
    }

    if (op == 0x0d) { /* i8x16.shuffle */
        if (!simd_pop(stack, &b) || !simd_pop(stack, &a)) {
            exec_fail(err, EXEC_ERROR_TRAP, "SIMD shuffle operands missing");
            return -1;
        }
        out = simd_zero();
        for (uint32_t i = 0; i < 16; i++) {
            uint8_t index = immediate->bytes[i];
            out.v128.bytes[i] = index < 16 ? a.v128.bytes[index] :
                                  b.v128.bytes[index - 16];
        }
        if (!stack_push(stack, out)) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); return -1;
        }
        return 1;
    }
    if (op == 0x0e) { /* i8x16.swizzle */
        if (!simd_pop(stack, &b) || !simd_pop(stack, &a)) {
            exec_fail(err, EXEC_ERROR_TRAP, "SIMD swizzle operands missing");
            return -1;
        }
        out = simd_zero();
        for (uint32_t i = 0; i < 16; i++)
            out.v128.bytes[i] = b.v128.bytes[i] < 16 ?
                                a.v128.bytes[b.v128.bytes[i]] : 0;
        if (!stack_push(stack, out)) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); return -1;
        }
        return 1;
    }

    if (op >= 0x4d && op <= 0x53) {
        if (!simd_pop(stack, &a)) {
            exec_fail(err, EXEC_ERROR_TRAP, "SIMD bitwise operand missing");
            return -1;
        }
        if (op == 0x53) {
            uint32_t any = 0;
            for (uint32_t i = 0; i < 16; i++) any |= a.v128.bytes[i];
            if (!stack_push(stack, i32_value(any != 0))) {
                exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); return -1;
            }
            return 1;
        }
        if (op != 0x4d && (!simd_pop(stack, &b) ||
            (op == 0x52 && !simd_pop(stack, &c)))) {
            exec_fail(err, EXEC_ERROR_TRAP, "SIMD bitwise operands missing");
            return -1;
        }
        out = simd_zero();
        for (uint32_t i = 0; i < 16; i++) {
            if (op == 0x4d) out.v128.bytes[i] = (uint8_t)~a.v128.bytes[i];
            else if (op == 0x4e) out.v128.bytes[i] = b.v128.bytes[i] & a.v128.bytes[i];
            else if (op == 0x4f) out.v128.bytes[i] = b.v128.bytes[i] & (uint8_t)~a.v128.bytes[i];
            else if (op == 0x50) out.v128.bytes[i] = b.v128.bytes[i] | a.v128.bytes[i];
            else if (op == 0x51) out.v128.bytes[i] = b.v128.bytes[i] ^ a.v128.bytes[i];
            else out.v128.bytes[i] = (c.v128.bytes[i] & a.v128.bytes[i]) |
                                     (b.v128.bytes[i] & (uint8_t)~a.v128.bytes[i]);
        }
        if (!stack_push(stack, out)) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); return -1;
        }
        return 1;
    }

    /* Integer comparisons. */
    if (op >= 0x23 && op <= 0x40) {
        if (op <= 0x2c) { width = 1; kind = op - 0x23; }
        else if (op <= 0x36) { width = 2; kind = op - 0x2d; }
        else { width = 4; kind = op - 0x37; }
    } else if (op >= 0xd6 && op <= 0xdb) {
        width = 8;
        static const uint8_t kinds[] = {0,1,2,4,6,8};
        kind = kinds[op - 0xd6];
    }
    if (width) {
        if (!simd_pop(stack, &b) || !simd_pop(stack, &a)) {
            exec_fail(err, EXEC_ERROR_TRAP, "SIMD comparison operands missing");
            return -1;
        }
        out = simd_zero(); lanes = 16 / width;
        mask = width == 8 ? UINT64_MAX : (UINT64_C(1) << (width * 8)) - 1;
        for (uint32_t i = 0; i < lanes; i++) {
            uint64_t au = simd_lane_u(&a, i, width), bu = simd_lane_u(&b, i, width);
            int64_t as = simd_lane_s(&a, i, width), bs = simd_lane_s(&b, i, width);
            int yes = kind == 0 ? au == bu : kind == 1 ? au != bu :
                      kind == 2 ? as < bs : kind == 3 ? au < bu :
                      kind == 4 ? as > bs : kind == 5 ? au > bu :
                      kind == 6 ? as <= bs : kind == 7 ? au <= bu :
                      kind == 8 ? as >= bs : au >= bu;
            simd_set_lane(&out, i, width, yes ? mask : 0);
        }
        if (!stack_push(stack, out)) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); return -1;
        }
        return 1;
    }

    /* Integer lane-width conversions and pairwise products. */
    if (op == 0x65 || op == 0x66 || op == 0x85 || op == 0x86) {
        uint32_t source_width = op <= 0x66 ? 2 : 4;
        uint32_t dest_width = source_width / 2;
        int signed_ = op == 0x65 || op == 0x85;
        if (!simd_pop(stack,&b) || !simd_pop(stack,&a)) {
            exec_fail(err,EXEC_ERROR_TRAP,"SIMD narrow operands missing");return -1;
        }
        out=simd_zero(); lanes=16/source_width;
        for(uint32_t half=0;half<2;half++) for(uint32_t i=0;i<lanes;i++) {
            wasm_value *source=half?&b:&a;
            uint64_t r;
            if(signed_) r=(uint64_t)simd_sat_s(simd_lane_s(source,i,source_width),dest_width*8);
            else {
                int64_t x=simd_lane_s(source,i,source_width);
                uint64_t max=(UINT64_C(1)<<(dest_width*8))-1;
                r=x<0?0:(uint64_t)x>max?max:(uint64_t)x;
            }
            simd_set_lane(&out,half*lanes+i,dest_width,r);
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}
        return 1;
    }
    if ((op>=0x87&&op<=0x8a)||(op>=0xa7&&op<=0xaa)||(op>=0xc7&&op<=0xca)) {
        uint32_t source_width=op<=0x8a?1:op<=0xaa?2:4;
        uint32_t dest_width=source_width*2;
        uint32_t variant=op-(op<=0x8a?0x87:op<=0xaa?0xa7:0xc7);
        int high=(variant&1)!=0, signed_=(variant&2)==0;
        if(!simd_pop(stack,&a)){exec_fail(err,EXEC_ERROR_TRAP,"SIMD extend operand missing");return -1;}
        out=simd_zero();lanes=16/dest_width;
        for(uint32_t i=0;i<lanes;i++) {
            uint32_t source_lane=i+(high?lanes:0);
            uint64_t r=signed_?(uint64_t)simd_lane_s(&a,source_lane,source_width):simd_lane_u(&a,source_lane,source_width);
            simd_set_lane(&out,i,dest_width,r);
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}
        return 1;
    }
    if ((op>=0x9c&&op<=0x9f)||(op>=0xbc&&op<=0xbf)||(op>=0xdc&&op<=0xdf)) {
        uint32_t source_width=op<=0x9f?1:op<=0xbf?2:4;
        uint32_t dest_width=source_width*2;
        uint32_t variant=op-(op<=0x9f?0x9c:op<=0xbf?0xbc:0xdc);
        int high=(variant&1)!=0, signed_=(variant&2)==0;
        if(!simd_pop(stack,&b)||!simd_pop(stack,&a)){exec_fail(err,EXEC_ERROR_TRAP,"SIMD extmul operands missing");return -1;}
        out=simd_zero();lanes=16/dest_width;
        for(uint32_t i=0;i<lanes;i++) {
            uint32_t source_lane=i+(high?lanes:0);uint64_t r;
            if(signed_) r=(uint64_t)(simd_lane_s(&a,source_lane,source_width)*simd_lane_s(&b,source_lane,source_width));
            else r=simd_lane_u(&a,source_lane,source_width)*simd_lane_u(&b,source_lane,source_width);
            simd_set_lane(&out,i,dest_width,r);
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}
        return 1;
    }
    if(op>=0x7c&&op<=0x7f) {
        uint32_t source_width=op<=0x7d?1:2,dest_width=source_width*2;
        int signed_=(op&1)==0;
        if(!simd_pop(stack,&a)){exec_fail(err,EXEC_ERROR_TRAP,"SIMD pairwise operand missing");return -1;}
        out=simd_zero();lanes=16/dest_width;
        for(uint32_t i=0;i<lanes;i++) {
            uint64_t r=signed_?(uint64_t)(simd_lane_s(&a,i*2,source_width)+simd_lane_s(&a,i*2+1,source_width)):
                      simd_lane_u(&a,i*2,source_width)+simd_lane_u(&a,i*2+1,source_width);
            simd_set_lane(&out,i,dest_width,r);
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}
        return 1;
    }
    if(op==0x82) {
        if(!simd_pop(stack,&b)||!simd_pop(stack,&a)){exec_fail(err,EXEC_ERROR_TRAP,"SIMD q15mulr operands missing");return -1;}
        out=simd_zero();
        for(uint32_t i=0;i<8;i++) {
            int64_t r=(simd_lane_s(&a,i,2)*simd_lane_s(&b,i,2)+0x4000)>>15;
            simd_set_lane(&out,i,2,(uint64_t)simd_sat_s(r,16));
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}return 1;
    }
    if(op==0xba) {
        if(!simd_pop(stack,&b)||!simd_pop(stack,&a)){exec_fail(err,EXEC_ERROR_TRAP,"SIMD dot operands missing");return -1;}
        out=simd_zero();
        for(uint32_t i=0;i<4;i++) {
            int64_t r=simd_lane_s(&a,i*2,2)*simd_lane_s(&b,i*2,2)+
                      simd_lane_s(&a,i*2+1,2)*simd_lane_s(&b,i*2+1,2);
            simd_set_lane(&out,i,4,(uint64_t)r);
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}return 1;
    }

    /* Regular integer lane operations. kind: abs, neg, popcnt, all_true,
     * bitmask, shl, shr_s, shr_u, add, add_sat_s/u, sub, sub_sat_s/u,
     * mul, min_s/u, max_s/u, avgr_u. */
    switch (op) {
        case 0x60: width=1;kind=1;break; case 0x61:width=1;kind=2;break;
        case 0x62:width=1;kind=3;break; case 0x63:width=1;kind=4;break;
        case 0x64:width=1;kind=5;break; case 0x6b:width=1;kind=6;break;
        case 0x6c:width=1;kind=7;break; case 0x6d:width=1;kind=8;break;
        case 0x6e:width=1;kind=9;break; case 0x6f:width=1;kind=10;break;
        case 0x70:width=1;kind=11;break; case 0x71:width=1;kind=12;break;
        case 0x72:width=1;kind=13;break; case 0x73:width=1;kind=14;break;
        case 0x76:width=1;kind=16;break; case 0x77:width=1;kind=17;break;
        case 0x78:width=1;kind=18;break; case 0x79:width=1;kind=19;break;
        case 0x7b:width=1;kind=20;break;
        case 0x80:width=2;kind=1;break; case 0x81:width=2;kind=2;break;
        case 0x83:width=2;kind=4;break; case 0x84:width=2;kind=5;break;
        case 0x8b:width=2;kind=6;break; case 0x8c:width=2;kind=7;break;
        case 0x8d:width=2;kind=8;break; case 0x8e:width=2;kind=9;break;
        case 0x8f:width=2;kind=10;break; case 0x90:width=2;kind=11;break;
        case 0x91:width=2;kind=12;break; case 0x92:width=2;kind=13;break;
        case 0x93:width=2;kind=14;break; case 0x95:width=2;kind=15;break;
        case 0x96:width=2;kind=16;break; case 0x97:width=2;kind=17;break;
        case 0x98:width=2;kind=18;break; case 0x99:width=2;kind=19;break;
        case 0x9b:width=2;kind=20;break;
        case 0xa0:width=4;kind=1;break; case 0xa1:width=4;kind=2;break;
        case 0xa3:width=4;kind=4;break; case 0xa4:width=4;kind=5;break;
        case 0xab:width=4;kind=6;break; case 0xac:width=4;kind=7;break;
        case 0xad:width=4;kind=8;break; case 0xae:width=4;kind=9;break;
        case 0xb1:width=4;kind=12;break; case 0xb5:width=4;kind=15;break;
        case 0xb6:width=4;kind=16;break; case 0xb7:width=4;kind=17;break;
        case 0xb8:width=4;kind=18;break; case 0xb9:width=4;kind=19;break;
        case 0xc0:width=8;kind=1;break; case 0xc1:width=8;kind=2;break;
        case 0xc3:width=8;kind=4;break; case 0xc4:width=8;kind=5;break;
        case 0xcb:width=8;kind=6;break; case 0xcc:width=8;kind=7;break;
        case 0xcd:width=8;kind=8;break; case 0xce:width=8;kind=9;break;
        case 0xd1:width=8;kind=12;break; case 0xd5:width=8;kind=15;break;
        default: break;
    }
    if (!width) return 0;
    int unary = kind <= 5;
    if ((kind >= 6 && kind <= 8)) {
        if (!stack_pop(stack, &b) || b.type != WASM_VALTYPE_I32 || !simd_pop(stack, &a)) {
            exec_fail(err, EXEC_ERROR_TRAP, "SIMD shift operands missing"); return -1;
        }
    } else if (!simd_pop(stack, &b) || (!unary && !simd_pop(stack, &a))) {
        exec_fail(err, EXEC_ERROR_TRAP, "SIMD integer operands missing"); return -1;
    }
    if (unary) a = b;
    lanes = 16 / width;
    if (kind == 4 || kind == 5) {
        uint32_t result = kind == 4 ? 1u : 0u;
        for (uint32_t i = 0; i < lanes; i++) {
            uint64_t x = simd_lane_u(&a, i, width);
            if (kind == 4) result &= x != 0;
            else result |= (uint32_t)(x >> (width * 8 - 1)) << i;
        }
        if (!stack_push(stack, i32_value(result))) {
            exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); return -1;
        }
        return 1;
    }
    out = simd_zero();
    mask = width == 8 ? UINT64_MAX : (UINT64_C(1) << (width * 8)) - 1;
    for (uint32_t i = 0; i < lanes; i++) {
        uint64_t au=simd_lane_u(&a,i,width), bu=simd_lane_u(&b,i,width), r=0;
        int64_t as=simd_lane_s(&a,i,width), bs=simd_lane_s(&b,i,width);
        uint32_t shift = (uint32_t)b.i32 & (width * 8 - 1);
        switch (kind) {
            case 1: r = as < 0 ? (uint64_t)(0 - au) : au; break;
            case 2: r = 0 - au; break;
            case 3: r = (uint64_t)__builtin_popcount((unsigned)au); break;
            case 6: r = au << shift; break;
            case 7: r = (uint64_t)(as >> shift); break;
            case 8: r = au >> shift; break;
            case 9: r = au + bu; break;
            case 10: r = (uint64_t)simd_sat_s(as + bs, width*8); break;
            case 11: { uint64_t sum=au+bu; r=sum>mask?mask:sum; break; }
            case 12: r = au - bu; break;
            case 13: r = (uint64_t)simd_sat_s(as - bs, width*8); break;
            case 14: r = au < bu ? 0 : au - bu; break;
            case 15: r = au * bu; break;
            case 16: r = (uint64_t)(as < bs ? as : bs); break;
            case 17: r = au < bu ? au : bu; break;
            case 18: r = (uint64_t)(as > bs ? as : bs); break;
            case 19: r = au > bu ? au : bu; break;
            case 20: r = (au + bu + 1) >> 1; break;
            default: break;
        }
        simd_set_lane(&out, i, width, r & mask);
    }
    if (!stack_push(stack, out)) {
        exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); return -1;
    }
    return 1;
}

static float simd_f32_minmax(float a, float b, int maximum) {
    uint32_t ai, bi, ri;
    memcpy(&ai, &a, 4); memcpy(&bi, &b, 4);
    if (isnan(a) || isnan(b)) {
        ri = UINT32_C(0x7fc00000); memcpy(&a, &ri, 4); return a;
    }
    if (a == 0.0f && b == 0.0f) {
        ri = maximum ? ai & bi : ai | bi; memcpy(&a, &ri, 4); return a;
    }
    return maximum ? (a > b ? a : b) : (a < b ? a : b);
}

static double simd_f64_minmax(double a, double b, int maximum) {
    uint64_t ai, bi, ri;
    memcpy(&ai, &a, 8); memcpy(&bi, &b, 8);
    if (isnan(a) || isnan(b)) {
        ri = UINT64_C(0x7ff8000000000000); memcpy(&a, &ri, 8); return a;
    }
    if (a == 0.0 && b == 0.0) {
        ri = maximum ? ai & bi : ai | bi; memcpy(&a, &ri, 8); return a;
    }
    return maximum ? (a > b ? a : b) : (a < b ? a : b);
}

static int exec_standard_simd_float(uint32_t op, exec_stack *stack,
                                    exec_error *err) {
    wasm_value a, b, out;
    uint32_t width = 0, lanes = 0, kind = 0;

    if (op >= 0x41 && op <= 0x46) { width=4;lanes=4;kind=op-0x41; }
    else if (op >= 0x47 && op <= 0x4c) { width=8;lanes=2;kind=op-0x47; }
    if (width) {
        if (!simd_pop(stack,&b) || !simd_pop(stack,&a)) {
            exec_fail(err,EXEC_ERROR_TRAP,"SIMD float comparison operands missing");
            return -1;
        }
        out=simd_zero();
        for (uint32_t i=0;i<lanes;i++) {
            int yes;
            if (width==4) {
                float x,y; memcpy(&x,a.v128.bytes+i*4,4); memcpy(&y,b.v128.bytes+i*4,4);
                yes=kind==0?x==y:kind==1?x!=y:kind==2?x<y:kind==3?x>y:kind==4?x<=y:x>=y;
                simd_set_lane(&out,i,4,yes?UINT32_MAX:0);
            } else {
                double x,y; memcpy(&x,a.v128.bytes+i*8,8); memcpy(&y,b.v128.bytes+i*8,8);
                yes=kind==0?x==y:kind==1?x!=y:kind==2?x<y:kind==3?x>y:kind==4?x<=y:x>=y;
                simd_set_lane(&out,i,8,yes?UINT64_MAX:0);
            }
        }
        if (!stack_push(stack,out)) { exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1; }
        return 1;
    }

    /* Unary rounding/arithmetic operations. */
    switch(op) {
        case 0x67:width=4;kind=1;break; case 0x68:width=4;kind=2;break;
        case 0x69:width=4;kind=3;break; case 0x6a:width=4;kind=4;break;
        case 0x74:width=8;kind=1;break; case 0x75:width=8;kind=2;break;
        case 0x7a:width=8;kind=3;break; case 0x94:width=8;kind=4;break;
        case 0xe0:width=4;kind=5;break; case 0xe1:width=4;kind=6;break;
        case 0xe3:width=4;kind=7;break; case 0xec:width=8;kind=5;break;
        case 0xed:width=8;kind=6;break; case 0xef:width=8;kind=7;break;
        default:break;
    }
    if (width) {
        if (!simd_pop(stack,&a)) { exec_fail(err,EXEC_ERROR_TRAP,"SIMD float operand missing");return -1; }
        out=simd_zero(); lanes=16/width;
        for(uint32_t i=0;i<lanes;i++) {
            if(width==4) {
                float x,r; uint32_t bits;
                memcpy(&x,a.v128.bytes+i*4,4);
                if(kind==5) { memcpy(&bits,&x,4);bits&=UINT32_C(0x7fffffff);memcpy(&r,&bits,4); }
                else if(kind==6) { memcpy(&bits,&x,4);bits^=UINT32_C(0x80000000);memcpy(&r,&bits,4); }
                else r=kind==1?ceilf(x):kind==2?floorf(x):kind==3?truncf(x):kind==4?nearbyintf(x):sqrtf(x);
                memcpy(out.v128.bytes+i*4,&r,4);
            } else {
                double x,r; uint64_t bits;
                memcpy(&x,a.v128.bytes+i*8,8);
                if(kind==5) { memcpy(&bits,&x,8);bits&=UINT64_C(0x7fffffffffffffff);memcpy(&r,&bits,8); }
                else if(kind==6) { memcpy(&bits,&x,8);bits^=UINT64_C(0x8000000000000000);memcpy(&r,&bits,8); }
                else r=kind==1?ceil(x):kind==2?floor(x):kind==3?trunc(x):kind==4?nearbyint(x):sqrt(x);
                memcpy(out.v128.bytes+i*8,&r,8);
            }
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}
        return 1;
    }

    /* Binary float arithmetic. */
    if(op>=0xe4&&op<=0xeb){width=4;kind=op-0xe4;}
    else if(op>=0xf0&&op<=0xf7){width=8;kind=op-0xf0;}
    if(width) {
        if(!simd_pop(stack,&b)||!simd_pop(stack,&a)){exec_fail(err,EXEC_ERROR_TRAP,"SIMD float operands missing");return -1;}
        out=simd_zero();lanes=16/width;
        for(uint32_t i=0;i<lanes;i++) {
            if(width==4) {
                float x,y,r;memcpy(&x,a.v128.bytes+i*4,4);memcpy(&y,b.v128.bytes+i*4,4);
                if(kind==0)r=x+y;else if(kind==1)r=x-y;else if(kind==2)r=x*y;else if(kind==3)r=x/y;
                else if(kind==4)r=simd_f32_minmax(x,y,0);else if(kind==5)r=simd_f32_minmax(x,y,1);
                else if(kind==6)r=y<x?y:x;else r=x<y?y:x;
                memcpy(out.v128.bytes+i*4,&r,4);
            } else {
                double x,y,r;memcpy(&x,a.v128.bytes+i*8,8);memcpy(&y,b.v128.bytes+i*8,8);
                if(kind==0)r=x+y;else if(kind==1)r=x-y;else if(kind==2)r=x*y;else if(kind==3)r=x/y;
                else if(kind==4)r=simd_f64_minmax(x,y,0);else if(kind==5)r=simd_f64_minmax(x,y,1);
                else if(kind==6)r=y<x?y:x;else r=x<y?y:x;
                memcpy(out.v128.bytes+i*8,&r,8);
            }
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}
        return 1;
    }

    /* Numeric vector conversions. */
    if(op==0x5e||op==0x5f||(op>=0xf8&&op<=0xff)) {
        if(!simd_pop(stack,&a)){exec_fail(err,EXEC_ERROR_TRAP,"SIMD conversion operand missing");return -1;}
        out=simd_zero();
        if(op==0x5e) {
            for(uint32_t i=0;i<2;i++){double x;float r;memcpy(&x,a.v128.bytes+i*8,8);r=(float)x;memcpy(out.v128.bytes+i*4,&r,4);}
        } else if(op==0x5f) {
            for(uint32_t i=0;i<2;i++){float x;double r;memcpy(&x,a.v128.bytes+i*4,4);r=(double)x;memcpy(out.v128.bytes+i*8,&r,8);}
        } else if(op==0xf8||op==0xf9) {
            for(uint32_t i=0;i<4;i++){float x;uint32_t r;memcpy(&x,a.v128.bytes+i*4,4);r=op==0xf8?trunc_sat_i32_s_f32(x):trunc_sat_i32_u_f32(x);simd_set_lane(&out,i,4,r);}
        } else if(op==0xfa||op==0xfb) {
            for(uint32_t i=0;i<4;i++){uint32_t x=(uint32_t)simd_lane_u(&a,i,4);float r=op==0xfa?(float)(int32_t)x:(float)x;memcpy(out.v128.bytes+i*4,&r,4);}
        } else if(op==0xfc||op==0xfd) {
            for(uint32_t i=0;i<2;i++){double x;uint32_t r;memcpy(&x,a.v128.bytes+i*8,8);r=op==0xfc?trunc_sat_i32_s_f64(x):trunc_sat_i32_u_f64(x);simd_set_lane(&out,i,4,r);}
        } else {
            for(uint32_t i=0;i<2;i++){uint32_t x=(uint32_t)simd_lane_u(&a,i,4);double r=op==0xfe?(double)(int32_t)x:(double)x;memcpy(out.v128.bytes+i*8,&r,8);}
        }
        if(!stack_push(stack,out)){exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");return -1;}
        return 1;
    }
    return 0;
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

static wasm_value i32_value(uint32_t bits) {
    wasm_value value;
    memset(&value, 0, sizeof(value));
    value.type = WASM_VALTYPE_I32;
    value.i32 = (int32_t)bits;
    return value;
}

static uint32_t rotl32(uint32_t value, uint32_t count) {
    count &= 31u;
    return count ? (value << count) | (value >> (32u - count)) : value;
}

static uint32_t rotr32(uint32_t value, uint32_t count) {
    count &= 31u;
    return count ? (value >> count) | (value << (32u - count)) : value;
}

static uint64_t load_le(const uint8_t *memory, uint32_t address, uint32_t width) {
    uint64_t value = 0;
    for (uint32_t i = 0; i < width; i++) value |= (uint64_t)memory[address + i] << (8u * i);
    return value;
}

static void store_le(uint8_t *memory, uint32_t address, uint64_t value, uint32_t width) {
    for (uint32_t i = 0; i < width; i++) memory[address + i] = (uint8_t)(value >> (8u * i));
}

static exec_status memory_address(exec_memory *memory, uint32_t base, uint32_t offset,
                                  uint32_t width, uint32_t *address, exec_error *err) {
    uint64_t effective = (uint64_t)base + offset;
    uint64_t size = memory ? (uint64_t)memory->pages * EXEC_PAGE_SIZE : 0;
    if (!memory || effective + width > size)
        return exec_fail(err, EXEC_ERROR_TRAP, "out of bounds memory access");
    *address = (uint32_t)effective;
    return EXEC_OK;
}

/* ---- helper: raise-to-i64 for trunc operations ---- */
static uint32_t trunc_sat_i32_s_f32(float v) {
    if (v != v) return 0;
    if (v <= -2147483648.0f) return UINT32_C(0x80000000);
    if (v >= 2147483648.0f) return UINT32_C(0x7fffffff);
    return (uint32_t)(int32_t)v;
}
static uint32_t trunc_sat_i32_u_f32(float v) {
    if (v != v || v <= 0.0f) return 0;
    if (v >= 4294967296.0f) return UINT32_MAX;
    return (uint32_t)v;
}
static uint32_t trunc_sat_i32_s_f64(double v) {
    if (v != v) return 0;
    if (v <= -2147483648.0) return UINT32_C(0x80000000);
    if (v >= 2147483648.0) return UINT32_C(0x7fffffff);
    return (uint32_t)(int32_t)v;
}
static uint32_t trunc_sat_i32_u_f64(double v) {
    if (v != v || v <= 0.0) return 0;
    if (v >= 4294967296.0) return UINT32_MAX;
    return (uint32_t)v;
}
static uint64_t trunc_sat_i64_s_f32(float v) {
    if (v != v) return 0;
    if (v <= -9223372036854775808.0f)
        return UINT64_C(0x8000000000000000);
    if (v >= 9223372036854775808.0f)
        return UINT64_C(0x7fffffffffffffff);
    return (uint64_t)(int64_t)v;
}
static uint64_t trunc_sat_i64_u_f32(float v) {
    if (v != v || v <= 0.0f) return 0;
    if (v >= 18446744073709551616.0f) return UINT64_MAX;
    return (uint64_t)v;
}
static uint64_t trunc_sat_i64_s_f64(double v) {
    if (v != v) return 0;
    if (v <= -9223372036854775808.0)
        return UINT64_C(0x8000000000000000);
    if (v >= 9223372036854775808.0)
        return UINT64_C(0x7fffffffffffffff);
    return (uint64_t)(int64_t)v;
}
static uint64_t trunc_sat_i64_u_f64(double v) {
    if (v != v || v <= 0.0) return 0;
    if (v >= 18446744073709551616.0) return UINT64_MAX;
    return (uint64_t)v;
}

static exec_status exec_i64_numeric(uint32_t opcode, exec_stack *stack,
                                    exec_error *err) {
    wasm_value left, right;
    uint64_t a = 0, b;
    int unary = opcode == 0x50 || opcode == 0x79 || opcode == 0x7a ||
                opcode == 0x7b || opcode == 0xc2 || opcode == 0xc3 || opcode == 0xc4;
    if (!stack_pop(stack, &right) || right.type != WASM_VALTYPE_I64)
        return exec_fail(err, EXEC_ERROR_TRAP, "i64 operand missing");
    b = (uint64_t)right.i64;
    if (!unary) {
        if (!stack_pop(stack, &left) || left.type != WASM_VALTYPE_I64)
            return exec_fail(err, EXEC_ERROR_TRAP, "i64 operand missing");
        a = (uint64_t)left.i64;
    }
    /* i64 comparison ops → push i32 result */
    if (opcode <= 0x5a) {
        int32_t result = 0;
        switch (opcode) {
            case 0x50: result = b == 0; break;
            case 0x51: result = a == b; break;
            case 0x52: result = a != b; break;
            case 0x53: result = (int64_t)a < (int64_t)b; break;
            case 0x54: result = a < b; break;
            case 0x55: result = (int64_t)a > (int64_t)b; break;
            case 0x56: result = a > b; break;
            case 0x57: result = (int64_t)a <= (int64_t)b; break;
            case 0x58: result = a <= b; break;
            case 0x59: result = (int64_t)a >= (int64_t)b; break;
            case 0x5a: result = a >= b; break;
            default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported i64 cmp");
        }
        if (!stack_push(stack, i32_value((uint32_t)result)))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }
    /* i64 arithmetic → push i64 result */
    uint64_t result = 0;
    switch (opcode) {
        case 0x79: result = b ? (uint64_t)__builtin_clzll(b) : 64u; break;
        case 0x7a: result = b ? (uint64_t)__builtin_ctzll(b) : 64u; break;
        case 0x7b: result = (uint64_t)__builtin_popcountll(b); break;
        case 0x7c: result = a + b; break;
        case 0x7d: result = a - b; break;
        case 0x7e: result = a * b; break;
        case 0x7f:
            if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero");
            if (a == (uint64_t)INT64_MIN && b == UINT64_MAX)
                return exec_fail(err, EXEC_ERROR_TRAP, "integer overflow");
            result = (uint64_t)((int64_t)a / (int64_t)b); break;
        case 0x80: if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero"); result = a / b; break;
        case 0x81:
            if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero");
            result = (a == (uint64_t)INT64_MIN && b == UINT64_MAX) ? 0 : (uint64_t)((int64_t)a % (int64_t)b); break;
        case 0x82: if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero"); result = a % b; break;
        case 0x83: result = a & b; break;
        case 0x84: result = a | b; break;
        case 0x85: result = a ^ b; break;
        case 0x86: result = a << (b & 63u); break;
        case 0x87: result = (uint64_t)((int64_t)a >> (b & 63u)); break;
        case 0x88: result = a >> (b & 63u); break;
        case 0x89: { uint32_t n = (uint32_t)(b & 63u); result = n ? (a << n) | (a >> (64u - n)) : a; break; }
        case 0x8a: { uint32_t n = (uint32_t)(b & 63u); result = n ? (a >> n) | (a << (64u - n)) : a; break; }
        case 0xc2: result = (uint64_t)(int64_t)(int8_t)b; break;
        case 0xc3: result = (uint64_t)(int64_t)(int16_t)b; break;
        case 0xc4: result = (uint64_t)(int64_t)(int32_t)b; break;
        default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported i64 opcode");
    }
    wasm_value out; memset(&out, 0, sizeof(out));
    out.type = WASM_VALTYPE_I64; out.i64 = (int64_t)result;
    if (!stack_push(stack, out))
        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

static exec_status exec_f32_numeric(uint32_t opcode, exec_stack *stack,
                                    exec_error *err) {
    wasm_value left, right;
    float a = 0.0f, b;
    int unary = opcode == 0x8b || opcode == 0x8c || opcode == 0x8d ||
                opcode == 0x8e || opcode == 0x8f || opcode == 0x90 || opcode == 0x91;
    /* comparisons are always binary */
    int is_cmp = opcode >= 0x5b && opcode <= 0x60;
    if (!stack_pop(stack, &right) || right.type != WASM_VALTYPE_F32)
        return exec_fail(err, EXEC_ERROR_TRAP, "f32 operand missing");
    memcpy(&b, &right.f32, 4);
    if (!unary || is_cmp) {
        if (!stack_pop(stack, &left) || left.type != WASM_VALTYPE_F32)
            return exec_fail(err, EXEC_ERROR_TRAP, "f32 operand missing");
        memcpy(&a, &left.f32, 4);
    }
    if (is_cmp) {
        int32_t result = 0;
        switch (opcode) {
            case 0x5b: result = a == b; break;
            case 0x5c: result = a != b; break;
            case 0x5d: result = a <  b; break;
            case 0x5e: result = a >  b; break;
            case 0x5f: result = a <= b; break;
            case 0x60: result = a >= b; break;
            default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported f32 cmp");
        }
        if (!stack_push(stack, i32_value((uint32_t)result)))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }
    float result;
    switch (opcode) {
        case 0x8b: result = fabsf(b); break;
        case 0x8c: result = -b; break;
        case 0x8d: result = ceilf(b); break;
        case 0x8e: result = floorf(b); break;
        case 0x8f: result = truncf(b); break;
        case 0x90: result = nearbyintf(b); break;
        case 0x91: result = sqrtf(b); break;
        case 0x92: result = a + b; break;
        case 0x93: result = a - b; break;
        case 0x94: result = a * b; break;
        case 0x95: result = a / b; break;
        case 0x96: { /* f32.min: Wasm semantics — NaN → canonical NaN, -0 < +0 */
            uint32_t ai, bi;
            memcpy(&ai, &a, 4); memcpy(&bi, &b, 4);
            int a_nan = ((ai & 0x7F800000u) == 0x7F800000u) && (ai & 0x007FFFFFu);
            int b_nan = ((bi & 0x7F800000u) == 0x7F800000u) && (bi & 0x007FFFFFu);
            if (a_nan || b_nan) { uint32_t r = 0x7FC00000u; memcpy(&result, &r, 4); }
            else if (a == 0.0f && b == 0.0f) { /* -0 vs +0: pick most-negative */
                uint32_t r = ai | bi; memcpy(&result, &r, 4);
            } else result = a < b ? a : b;
            break;
        }
        case 0x97: { /* f32.max: Wasm semantics — NaN → canonical NaN, +0 > -0 */
            uint32_t ai, bi;
            memcpy(&ai, &a, 4); memcpy(&bi, &b, 4);
            int a_nan = ((ai & 0x7F800000u) == 0x7F800000u) && (ai & 0x007FFFFFu);
            int b_nan = ((bi & 0x7F800000u) == 0x7F800000u) && (bi & 0x007FFFFFu);
            if (a_nan || b_nan) { uint32_t r = 0x7FC00000u; memcpy(&result, &r, 4); }
            else if (a == 0.0f && b == 0.0f) { /* -0 vs +0: pick most-positive */
                uint32_t r = ai & bi; memcpy(&result, &r, 4);
            } else result = a > b ? a : b;
            break;
        }
        case 0x98: { /* f32.copysign */
            uint32_t ai, bi; memcpy(&ai, &a, 4); memcpy(&bi, &b, 4);
            uint32_t ri = (ai & 0x7fffffffu) | (bi & 0x80000000u);
            memcpy(&result, &ri, 4); break;
        }
        default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported f32 opcode");
    }
    wasm_value out; memset(&out, 0, sizeof(out));
    out.type = WASM_VALTYPE_F32; memcpy(&out.f32, &result, 4);
    if (!stack_push(stack, out))
        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

static exec_status exec_f64_numeric(uint32_t opcode, exec_stack *stack,
                                    exec_error *err) {
    wasm_value left, right;
    double a = 0.0, b;
    int unary = opcode == 0x99 || opcode == 0x9a || opcode == 0x9b ||
                opcode == 0x9c || opcode == 0x9d || opcode == 0x9e || opcode == 0x9f;
    int is_cmp = opcode >= 0x61 && opcode <= 0x66;
    if (!stack_pop(stack, &right) || right.type != WASM_VALTYPE_F64)
        return exec_fail(err, EXEC_ERROR_TRAP, "f64 operand missing");
    memcpy(&b, &right.f64, 8);
    if (!unary || is_cmp) {
        if (!stack_pop(stack, &left) || left.type != WASM_VALTYPE_F64)
            return exec_fail(err, EXEC_ERROR_TRAP, "f64 operand missing");
        memcpy(&a, &left.f64, 8);
    }
    if (is_cmp) {
        int32_t result = 0;
        switch (opcode) {
            case 0x61: result = a == b; break;
            case 0x62: result = a != b; break;
            case 0x63: result = a <  b; break;
            case 0x64: result = a >  b; break;
            case 0x65: result = a <= b; break;
            case 0x66: result = a >= b; break;
            default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported f64 cmp");
        }
        if (!stack_push(stack, i32_value((uint32_t)result)))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }
    double result;
    switch (opcode) {
        case 0x99: result = fabs(b); break;
        case 0x9a: result = -b; break;
        case 0x9b: result = ceil(b); break;
        case 0x9c: result = floor(b); break;
        case 0x9d: result = trunc(b); break;
        case 0x9e: result = nearbyint(b); break;
        case 0x9f: result = sqrt(b); break;
        case 0xa0: result = a + b; break;
        case 0xa1: result = a - b; break;
        case 0xa2: result = a * b; break;
        case 0xa3: result = a / b; break;
        case 0xa4: { /* f64.min: Wasm semantics */
            uint64_t ai, bi;
            memcpy(&ai, &a, 8); memcpy(&bi, &b, 8);
            int a_nan = ((ai & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (ai & 0x000FFFFFFFFFFFFFULL);
            int b_nan = ((bi & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (bi & 0x000FFFFFFFFFFFFFULL);
            if (a_nan || b_nan) { uint64_t r = 0x7FF8000000000000ULL; memcpy(&result, &r, 8); }
            else if (a == 0.0 && b == 0.0) { uint64_t r = ai | bi; memcpy(&result, &r, 8); }
            else result = a < b ? a : b;
            break;
        }
        case 0xa5: { /* f64.max: Wasm semantics */
            uint64_t ai, bi;
            memcpy(&ai, &a, 8); memcpy(&bi, &b, 8);
            int a_nan = ((ai & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (ai & 0x000FFFFFFFFFFFFFULL);
            int b_nan = ((bi & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (bi & 0x000FFFFFFFFFFFFFULL);
            if (a_nan || b_nan) { uint64_t r = 0x7FF8000000000000ULL; memcpy(&result, &r, 8); }
            else if (a == 0.0 && b == 0.0) { uint64_t r = ai & bi; memcpy(&result, &r, 8); }
            else result = a > b ? a : b;
            break;
        }
        case 0xa6: { /* f64.copysign */
            uint64_t ai, bi; memcpy(&ai, &a, 8); memcpy(&bi, &b, 8);
            uint64_t ri = (ai & UINT64_C(0x7fffffffffffffff)) | (bi & UINT64_C(0x8000000000000000));
            memcpy(&result, &ri, 8); break;
        }
        default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported f64 opcode");
    }
    wasm_value out; memset(&out, 0, sizeof(out));
    out.type = WASM_VALTYPE_F64; memcpy(&out.f64, &result, 8);
    if (!stack_push(stack, out))
        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

/* Conversion ops: 0xa7-0xbf (and saturating 0xFC 0x00-0x07) */
static exec_status exec_conversion(uint32_t opcode, exec_stack *stack,
                                   exec_error *err) {
    wasm_value v;
    if (!stack_pop(stack, &v))
        return exec_fail(err, EXEC_ERROR_TRAP, "conversion operand missing");
    wasm_value out; memset(&out, 0, sizeof(out));
    float f32v; double f64v; uint32_t u32; uint64_t u64;
    switch (opcode) {
        /* i32.wrap_i64 */
        case 0xa7: out.type=WASM_VALTYPE_I32; out.i32=(int32_t)(int32_t)v.i64; break;
        /* i32.trunc_f32_s */
        case 0xa8:
            memcpy(&f32v,&v.f32,4);
            if (f32v!=f32v||f32v<=-2147483904.0f||f32v>=2147483648.0f)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I32; out.i32=(int32_t)f32v; break;
        /* i32.trunc_f32_u */
        case 0xa9:
            memcpy(&f32v,&v.f32,4);
            if (f32v!=f32v||f32v<=-1.0f||f32v>=4294967296.0f)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I32; out.i32=(int32_t)(uint32_t)f32v; break;
        /* i32.trunc_f64_s */
        case 0xaa:
            memcpy(&f64v,&v.f64,8);
            if (f64v!=f64v||f64v<=-2147483649.0||f64v>=2147483648.0)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I32; out.i32=(int32_t)f64v; break;
        /* i32.trunc_f64_u */
        case 0xab:
            memcpy(&f64v,&v.f64,8);
            if (f64v!=f64v||f64v<=-1.0||f64v>=4294967296.0)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I32; out.i32=(int32_t)(uint32_t)f64v; break;
        /* i64.extend_i32_s */
        case 0xac: out.type=WASM_VALTYPE_I64; out.i64=(int64_t)(int32_t)v.i32; break;
        /* i64.extend_i32_u */
        case 0xad: out.type=WASM_VALTYPE_I64; out.i64=(int64_t)(uint32_t)(uint32_t)v.i32; break;
        /* i64.trunc_f32_s */
        case 0xae:
            memcpy(&f32v,&v.f32,4);
            if (f32v!=f32v||f32v< -9223372036854775808.0f||f32v>=9223372036854775808.0f)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I64; out.i64=(int64_t)f32v; break;
        /* i64.trunc_f32_u */
        case 0xaf:
            memcpy(&f32v,&v.f32,4);
            if (f32v!=f32v||f32v<=-1.0f||f32v>=18446744073709551616.0f)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I64; out.i64=(int64_t)(uint64_t)f32v; break;
        /* i64.trunc_f64_s */
        case 0xb0:
            memcpy(&f64v,&v.f64,8);
            if (f64v!=f64v||f64v< -9223372036854775808.0||f64v>=9223372036854775808.0)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I64; out.i64=(int64_t)f64v; break;
        /* i64.trunc_f64_u */
        case 0xb1:
            memcpy(&f64v,&v.f64,8);
            if (f64v!=f64v||f64v<=-1.0||f64v>=18446744073709551616.0)
                return exec_fail(err,EXEC_ERROR_TRAP,"invalid conversion to integer");
            out.type=WASM_VALTYPE_I64; out.i64=(int64_t)(uint64_t)f64v; break;
        /* f32.convert_i32_s */
        case 0xb2: { float r=(float)(int32_t)v.i32; out.type=WASM_VALTYPE_F32; memcpy(&out.f32,&r,4); break; }
        /* f32.convert_i32_u */
        case 0xb3: { float r=(float)(uint32_t)v.i32; out.type=WASM_VALTYPE_F32; memcpy(&out.f32,&r,4); break; }
        /* f32.convert_i64_s */
        case 0xb4: { float r=(float)(int64_t)v.i64; out.type=WASM_VALTYPE_F32; memcpy(&out.f32,&r,4); break; }
        /* f32.convert_i64_u */
        case 0xb5: { float r=(float)(uint64_t)v.i64; out.type=WASM_VALTYPE_F32; memcpy(&out.f32,&r,4); break; }
        /* f32.demote_f64 */
        case 0xb6: { memcpy(&f64v,&v.f64,8); float r=(float)f64v; out.type=WASM_VALTYPE_F32; memcpy(&out.f32,&r,4); break; }
        /* f64.convert_i32_s */
        case 0xb7: { double r=(double)(int32_t)v.i32; out.type=WASM_VALTYPE_F64; memcpy(&out.f64,&r,8); break; }
        /* f64.convert_i32_u */
        case 0xb8: { double r=(double)(uint32_t)v.i32; out.type=WASM_VALTYPE_F64; memcpy(&out.f64,&r,8); break; }
        /* f64.convert_i64_s */
        case 0xb9: { double r=(double)(int64_t)v.i64; out.type=WASM_VALTYPE_F64; memcpy(&out.f64,&r,8); break; }
        /* f64.convert_i64_u */
        case 0xba: { double r=(double)(uint64_t)v.i64; out.type=WASM_VALTYPE_F64; memcpy(&out.f64,&r,8); break; }
        /* f64.promote_f32 */
        case 0xbb: { memcpy(&f32v,&v.f32,4); double r=(double)f32v; out.type=WASM_VALTYPE_F64; memcpy(&out.f64,&r,8); break; }
        /* i32.reinterpret_f32 */
        case 0xbc: memcpy(&u32,&v.f32,4); out.type=WASM_VALTYPE_I32; out.i32=(int32_t)u32; break;
        /* i64.reinterpret_f64 */
        case 0xbd: memcpy(&u64,&v.f64,8); out.type=WASM_VALTYPE_I64; out.i64=(int64_t)u64; break;
        /* f32.reinterpret_i32 */
        case 0xbe: u32=(uint32_t)v.i32; out.type=WASM_VALTYPE_F32; memcpy(&out.f32,&u32,4); break;
        /* f64.reinterpret_i64 */
        case 0xbf: u64=(uint64_t)v.i64; out.type=WASM_VALTYPE_F64; memcpy(&out.f64,&u64,8); break;
        default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported conversion");
    }
    if (!stack_push(stack, out))
        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

/* Saturating truncation (0xFC prefix) */
static exec_status exec_sat_trunc(uint32_t sub_op, exec_stack *stack,
                                  exec_error *err) {
    wasm_value v;
    if (!stack_pop(stack, &v))
        return exec_fail(err, EXEC_ERROR_TRAP, "sat_trunc operand missing");
    wasm_value out; memset(&out, 0, sizeof(out));
    float f32v; double f64v;
    switch (sub_op) {
        case 0: memcpy(&f32v,&v.f32,4); out.type=WASM_VALTYPE_I32; out.i32=(int32_t)trunc_sat_i32_s_f32(f32v); break;
        case 1: memcpy(&f32v,&v.f32,4); out.type=WASM_VALTYPE_I32; out.i32=(int32_t)trunc_sat_i32_u_f32(f32v); break;
        case 2: memcpy(&f64v,&v.f64,8); out.type=WASM_VALTYPE_I32; out.i32=(int32_t)trunc_sat_i32_s_f64(f64v); break;
        case 3: memcpy(&f64v,&v.f64,8); out.type=WASM_VALTYPE_I32; out.i32=(int32_t)trunc_sat_i32_u_f64(f64v); break;
        case 4: memcpy(&f32v,&v.f32,4); out.type=WASM_VALTYPE_I64; out.i64=(int64_t)trunc_sat_i64_s_f32(f32v); break;
        case 5: memcpy(&f32v,&v.f32,4); out.type=WASM_VALTYPE_I64; out.i64=(int64_t)trunc_sat_i64_u_f32(f32v); break;
        case 6: memcpy(&f64v,&v.f64,8); out.type=WASM_VALTYPE_I64; out.i64=(int64_t)trunc_sat_i64_s_f64(f64v); break;
        case 7: memcpy(&f64v,&v.f64,8); out.type=WASM_VALTYPE_I64; out.i64=(int64_t)trunc_sat_i64_u_f64(f64v); break;
        default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported sat_trunc");
    }
    if (!stack_push(stack, out))
        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

static exec_status exec_i32_numeric(uint32_t opcode, exec_stack *stack,
                                    exec_error *err) {
    wasm_value left, right;
    uint32_t a, b, result = 0;
    int unary = opcode == 0x45 || opcode == 0x67 || opcode == 0x68 ||
                opcode == 0x69 || opcode == 0xc0 || opcode == 0xc1;
    if (!stack_pop(stack, &right) || right.type != WASM_VALTYPE_I32)
        return exec_fail(err, EXEC_ERROR_TRAP, "i32 operand missing");
    b = (uint32_t)right.i32;
    if (!unary) {
        if (!stack_pop(stack, &left) || left.type != WASM_VALTYPE_I32)
            return exec_fail(err, EXEC_ERROR_TRAP, "i32 operand missing");
        a = (uint32_t)left.i32;
    } else a = 0;
    switch (opcode) {
        case 0x45: result = b == 0; break;
        case 0x46: result = a == b; break; case 0x47: result = a != b; break;
        case 0x48: result = (int32_t)a < (int32_t)b; break;
        case 0x49: result = a < b; break;
        case 0x4a: result = (int32_t)a > (int32_t)b; break;
        case 0x4b: result = a > b; break;
        case 0x4c: result = (int32_t)a <= (int32_t)b; break;
        case 0x4d: result = a <= b; break;
        case 0x4e: result = (int32_t)a >= (int32_t)b; break;
        case 0x4f: result = a >= b; break;
        case 0x67: result = b ? (uint32_t)__builtin_clz(b) : 32u; break;
        case 0x68: result = b ? (uint32_t)__builtin_ctz(b) : 32u; break;
        case 0x69: result = (uint32_t)__builtin_popcount(b); break;
        case 0x6a: result = a + b; break; case 0x6b: result = a - b; break;
        case 0x6c: result = a * b; break;
        case 0x6d:
            if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero");
            if (a == 0x80000000u && b == UINT32_MAX)
                return exec_fail(err, EXEC_ERROR_TRAP, "integer overflow");
            result = (uint32_t)((int32_t)a / (int32_t)b); break;
        case 0x6e: if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero"); result = a / b; break;
        case 0x6f: if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero"); result = (a == 0x80000000u && b == UINT32_MAX) ? 0 : (uint32_t)((int32_t)a % (int32_t)b); break;
        case 0x70: if (!b) return exec_fail(err, EXEC_ERROR_TRAP, "integer divide by zero"); result = a % b; break;
        case 0x71: result = a & b; break; case 0x72: result = a | b; break;
        case 0x73: result = a ^ b; break; case 0x74: result = a << (b & 31u); break;
        case 0x75: result = (uint32_t)((int32_t)a >> (b & 31u)); break;
        case 0x76: result = a >> (b & 31u); break;
        case 0x77: result = rotl32(a, b); break; case 0x78: result = rotr32(a, b); break;
        case 0xc0: result = (uint32_t)(int32_t)(int8_t)b; break;
        case 0xc1: result = (uint32_t)(int32_t)(int16_t)b; break;
        default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported i32 opcode");
    }
    if (!stack_push(stack, i32_value(result)))
        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

static exec_status exec_invoke_depth(waste_exec_engine *eng,
                                     uint32_t func_idx,
                                     const wasm_value *args, int arg_count,
                                     wasm_value *results, int *result_count,
                                     exec_error *err, uint32_t depth) {
    /* Tail-call arguments: these are updated in-place by return_call */
    wasm_value tail_args[WAST_MAX_ARGS];
    int tail_arg_count;

tail_entry:
    if (!eng || func_idx >= eng->import_func_count + eng->func_count)
        return exec_fail(err, EXEC_ERROR_NOT_FOUND, "function index out of range");
    if (depth >= EXEC_MAX_CALL_DEPTH)
        return exec_fail(err, EXEC_ERROR_TRAP, "call stack exhausted");

    if (func_idx < eng->import_func_count) {
        exec_func_type *type=&eng->types[eng->import_func_types[func_idx]];
        if (arg_count != type->param_count) return exec_fail(err, EXEC_ERROR_TRAP, "import argument count mismatch");
        int count=0;
        exec_status status=eng->import_funcs[func_idx](eng->import_host_data[func_idx],args,arg_count,results,&count,err);
        if (status != EXEC_OK) return status;
        if (count != type->result_count) return exec_fail(err, EXEC_ERROR_TRAP, "import result count mismatch");
        if (result_count) *result_count=count;
        return EXEC_OK;
    }

    uint32_t defined_index=func_idx-eng->import_func_count;

    exec_func      *func = &eng->funcs[defined_index];
    exec_func_type *type = &eng->types[func->type_index];

    /* Validate arg count */
    if (arg_count != type->param_count)
        return exec_fail(err, EXEC_ERROR_TRAP, "argument count mismatch");

    /* Locals are engine-owned per invocation depth.  Their storage therefore
     * scales with the module declaration without consuming the browser/C
     * control stack (the guard-page conformance case has 1,056 i64 locals). */
    uint32_t local_count = (uint32_t)arg_count + func->local_count;
    if (local_count > EXEC_MAX_LOCALS)
        return exec_fail(err, EXEC_ERROR_TRAP, "too many runtime locals");
    uint32_t storage_count = local_count ? local_count : 1;
    if (eng->local_frame_capacities[depth] < storage_count) {
        wasm_value *next = (wasm_value *)realloc(
            eng->local_frames[depth],
            (size_t)storage_count * sizeof(*next));
        if (!next)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "runtime locals allocation failed");
        eng->local_frames[depth] = next;
        eng->local_frame_capacities[depth] = storage_count;
    }
    wasm_value *locals = eng->local_frames[depth];
    memset(locals, 0, (size_t)storage_count * sizeof(*locals));
    for (int i = 0; i < arg_count; i++)
        locals[i] = args[i];
    for (uint32_t i = 0; i < func->local_count; i++)
        locals[arg_count + i].type = func->locals[i];

    exec_stack stack;
    stack.top = 0;
    exec_control controls[EXEC_MAX_CONTROL];
    int control_top = 0;

    for (uint32_t pc = 0; pc < func->code_size; pc++) {
        exec_instr *instr = &func->code[pc];

        if (instr->opcode == 0x0B) {
            if (control_top > 0) {
                exec_control target = controls[--control_top];
                wasm_value values[WAST_MAX_RESULTS];
                if (target.end_arity > stack.top - target.stack_height)
                    return exec_fail(err, EXEC_ERROR_TRAP, "block results missing");
                for (int i = target.end_arity; i-- > 0;)
                    stack_pop(&stack, &values[i]);
                stack.top = target.stack_height;
                for (int i = 0; i < target.end_arity; i++)
                    if (!stack_push(&stack, values[i])) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            }
            else break;
            continue;
        }

        if (instr->opcode == 0x08) {
            if (instr->u32_imm >= eng->tag_count)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "throw tag index out of range");
            uint32_t tag_type_index = eng->tag_types[instr->u32_imm];
            if (tag_type_index >= eng->type_count)
                return exec_fail(err, EXEC_ERROR_TRAP, "invalid tag type");
            exec_func_type *tag_type = &eng->types[tag_type_index];
            wasm_value payload[WAST_MAX_PARAMS];
            if (tag_type->param_count > stack.top)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "throw payload missing");
            for (int i = tag_type->param_count; i-- > 0;)
                stack_pop(&stack, &payload[i]);

            int try_index = -1;
            const exec_catch *selected_catch = NULL;
            for (int i = control_top - 1; i >= 0 && !selected_catch; i--) {
                if (controls[i].kind != 0x1f) continue;
                exec_instr *try_instr =
                    &func->code[controls[i].start_pc - 1];
                for (uint32_t j = 0; j < try_instr->catch_count; j++) {
                    exec_catch *catch_ = &try_instr->catches[j];
                    if (catch_->kind == 2 ||
                        (catch_->kind == 0 &&
                         eng->tags[catch_->tag_index] ==
                         eng->tags[instr->u32_imm])) {
                        try_index = i;
                        selected_catch = catch_;
                        break;
                    }
                }
            }
            if (!selected_catch)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "uncaught exception");
            if (selected_catch->depth > (uint32_t)(try_index + 1))
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "catch branch depth out of range");

            stack.top = controls[try_index].stack_height;
            if (selected_catch->kind == 0)
                for (int i = 0; i < tag_type->param_count; i++)
                    if (!stack_push(&stack, payload[i]))
                        return exec_fail(err, EXEC_ERROR_TRAP,
                                         "stack overflow");
            if (selected_catch->depth == (uint32_t)(try_index + 1))
                goto func_return;
            int target_index = try_index - (int)selected_catch->depth;
            exec_control target = controls[target_index];
            wasm_value carried[WAST_MAX_RESULTS];
            if (target.branch_arity > stack.top - target.stack_height)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "catch branch values missing");
            for (int i = target.branch_arity; i-- > 0;)
                stack_pop(&stack, &carried[i]);
            stack.top = target.stack_height;
            for (int i = 0; i < target.branch_arity; i++)
                if (!stack_push(&stack, carried[i]))
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "stack overflow");
            if (target.kind == 0x03) {
                control_top = target_index + 1;
                pc = target.start_pc - 1;
            } else {
                control_top = target_index;
                pc = target.end_pc;
            }
            continue;
        }

        if (instr->opcode == 0x00)
            return exec_fail(err, EXEC_ERROR_TRAP, "unreachable");
        if (instr->opcode == 0x01) continue;

        if (instr->opcode == 0x02 || instr->opcode == 0x03 ||
            instr->opcode == 0x04 || instr->opcode == 0x1f) {
            int condition = 1;
            if (instr->opcode == 0x04) {
                wasm_value value;
                if (!stack_pop(&stack, &value) || value.type != WASM_VALTYPE_I32)
                    return exec_fail(err, EXEC_ERROR_TRAP, "if condition missing");
                condition = value.i32 != 0;
            }
            if (control_top >= EXEC_MAX_CONTROL)
                return exec_fail(err, EXEC_ERROR_TRAP, "control stack overflow");
            int parameter_count = instr->v128_imm.bytes[0];
            int result_count_for_block = instr->v128_imm.bytes[1];
            if (parameter_count > stack.top)
                return exec_fail(err, EXEC_ERROR_TRAP, "block parameters missing");
            controls[control_top++] = (exec_control){
                instr->opcode, pc + 1, instr->simd_op, stack.top - parameter_count,
                instr->opcode == 0x03 ? parameter_count : result_count_for_block,
                result_count_for_block
            };
            if (!condition) pc = instr->u32_imm - 1;
            continue;
        }

        if (instr->opcode == 0x05) {
            pc = instr->u32_imm - 1;
            continue;
        }

        if (instr->opcode == 0x0c || instr->opcode == 0x0d ||
            instr->opcode == 0x0e || instr->opcode == 0xd5 ||
            instr->opcode == 0xd6) {
            uint32_t depth = instr->u32_imm;
            if (instr->opcode == 0x0d) {
                wasm_value condition;
                if (!stack_pop(&stack, &condition) || condition.type != WASM_VALTYPE_I32)
                    return exec_fail(err, EXEC_ERROR_TRAP, "br_if condition missing");
                if (!condition.i32) continue;
            } else if (instr->opcode == 0x0e) {
                wasm_value index;
                if (!stack_pop(&stack, &index) || index.type != WASM_VALTYPE_I32)
                    return exec_fail(err, EXEC_ERROR_TRAP, "br_table index missing");
                uint32_t selected = (uint32_t)index.i32;
                if (selected > instr->u32_imm) selected = instr->u32_imm;
                uint32_t *depths; memcpy(&depths, instr->v128_imm.bytes, sizeof(depths));
                depth = depths[selected];
            } else if (instr->opcode == 0xd5 || instr->opcode == 0xd6) {
                wasm_value reference;
                if (!stack_pop(&stack, &reference) ||
                    !is_reference_type(reference.type))
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "reference branch operand missing");
                int nonnull = reference.ref != UINT32_MAX;
                reference.type = nonnullable_reference_type(reference.type);
                if (instr->opcode == 0xd5 && !nonnull) {
                    /* null: take the branch without carrying the reference */
                } else if (instr->opcode == 0xd5) {
                    if (!stack_push(&stack, reference))
                        return exec_fail(err, EXEC_ERROR_TRAP,
                                         "stack overflow");
                    continue;
                } else if (!nonnull) {
                    /* br_on_non_null consumes the null reference on its
                     * fall-through path. */
                    continue;
                } else if (!stack_push(&stack, reference)) {
                    return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                }
            }
            if (depth > (uint32_t)control_top)
                return exec_fail(err, EXEC_ERROR_TRAP, "branch depth out of range");
            if (depth == (uint32_t)control_top) {
                /* Branch to implicit function body block — act as return */
                wasm_value carried[WAST_MAX_RESULTS];
                int arity = type->result_count;
                if (arity > stack.top)
                    return exec_fail(err, EXEC_ERROR_TRAP, "branch values missing");
                for (int i = arity; i-- > 0;) stack_pop(&stack, &carried[i]);
                stack.top = 0;
                for (int i = 0; i < arity; i++)
                    if (!stack_push(&stack, carried[i])) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                goto func_return;
            }
            {
            int target_index = control_top - 1 - (int)depth;
            exec_control target = controls[target_index];
            wasm_value carried[WAST_MAX_RESULTS];
            if (target.branch_arity > stack.top - target.stack_height)
                return exec_fail(err, EXEC_ERROR_TRAP, "branch values missing");
            for (int i = target.branch_arity; i-- > 0;) stack_pop(&stack, &carried[i]);
            stack.top = target.stack_height;
            for (int i = 0; i < target.branch_arity; i++)
                if (!stack_push(&stack, carried[i])) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            if (target.kind == 0x03) {
                control_top = target_index + 1;
                pc = target.start_pc - 1;
            } else {
                control_top = target_index;
                pc = target.end_pc;
            }
            }
            continue;
        }

        if (instr->opcode == 0x20) {
            /* local.get */
            if (instr->u32_imm >= (uint32_t)arg_count + func->local_count)
                return exec_fail(err, EXEC_ERROR_TRAP, "local.get out of range");
            if (!stack_push(&stack, locals[instr->u32_imm]))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0x21 || instr->opcode == 0x22) {
            wasm_value value;
            if (instr->u32_imm >= (uint32_t)arg_count + func->local_count)
                return exec_fail(err, EXEC_ERROR_TRAP, "local index out of range");
            if (!stack_pop(&stack, &value))
                return exec_fail(err, EXEC_ERROR_TRAP, "local value missing");
            locals[instr->u32_imm] = value;
            if (instr->opcode == 0x22 && !stack_push(&stack, value))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0x23 || instr->opcode == 0x24) {
            if (instr->u32_imm >= eng->global_count)
                return exec_fail(err, EXEC_ERROR_TRAP, "global index out of range");
            if (instr->opcode == 0x23) {
                if (!stack_push(&stack, eng->globals[instr->u32_imm]->value)) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            } else {
                wasm_value value;
                if (!eng->globals[instr->u32_imm]->mutable_) return exec_fail(err, EXEC_ERROR_TRAP, "immutable global");
                exec_global *global = eng->globals[instr->u32_imm];
                if (!stack_pop(&stack, &value) ||
                    !global_type_is_compat(eng, value.type,
                                           global->type_owner,
                                           global->value.type, 0))
                    return exec_fail(err, EXEC_ERROR_TRAP, "global value mismatch");
                value.type = global->value.type;
                global->value = value;
            }
            continue;
        }

        if (instr->opcode == 0x25 || instr->opcode == 0x26) {
            if (instr->u32_imm >= eng->table_count)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "table index out of range");
            exec_table *table = eng->tables[instr->u32_imm];
            wasm_value reference;
            wasm_value index;
            if (instr->opcode == 0x26) {
                if (!stack_pop(&stack, &reference))
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "table.set value missing");
            }
            if (!stack_pop(&stack, &index) ||
                index.type != WASM_VALTYPE_I32)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "table index operand missing");
            uint32_t element_index = (uint32_t)index.i32;
            if (element_index >= table->size)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "out of bounds table access");
            if (instr->opcode == 0x25) {
                exec_table_element element = table->elements[element_index];
                memset(&reference, 0, sizeof(reference));
                reference.type = table->element_type;
                reference.ref = element.owner ? element.func_idx : UINT32_MAX;
                if (!stack_push(&stack, reference))
                    return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            } else {
                if (!global_type_is_compat(eng, reference.type,
                                           table->type_owner,
                                           table->element_type, 0))
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "table.set type mismatch");
                exec_table_element element = {NULL, 0};
                if (reference.ref != UINT32_MAX) {
                    element.owner = eng;
                    element.func_idx = reference.ref;
                }
                table->elements[element_index] = element;
            }
            continue;
        }

        if (instr->opcode >= 0x28 && instr->opcode <= 0x35) {
            wasm_value base, value; uint32_t width, address; int sign = 0;
            exec_memory *memory = instr->memory_index < eng->memory_count ?
                eng->memories[instr->memory_index] : NULL;
            if (!stack_pop(&stack, &base) || base.type != WASM_VALTYPE_I32)
                return exec_fail(err, EXEC_ERROR_TRAP, "load address missing");
            switch (instr->opcode) {
                case 0x28: width=4; value.type=WASM_VALTYPE_I32; break;
                case 0x29: width=8; value.type=WASM_VALTYPE_I64; break;
                case 0x2a: width=4; value.type=WASM_VALTYPE_F32; break;
                case 0x2b: width=8; value.type=WASM_VALTYPE_F64; break;
                case 0x2c: width=1; value.type=WASM_VALTYPE_I32; sign=1; break;
                case 0x2d: width=1; value.type=WASM_VALTYPE_I32; break;
                case 0x2e: width=2; value.type=WASM_VALTYPE_I32; sign=1; break;
                case 0x2f: width=2; value.type=WASM_VALTYPE_I32; break;
                case 0x30: width=1; value.type=WASM_VALTYPE_I64; sign=1; break;
                case 0x31: width=1; value.type=WASM_VALTYPE_I64; break;
                case 0x32: width=2; value.type=WASM_VALTYPE_I64; sign=1; break;
                case 0x33: width=2; value.type=WASM_VALTYPE_I64; break;
                case 0x34: width=4; value.type=WASM_VALTYPE_I64; sign=1; break;
                default: width=4; value.type=WASM_VALTYPE_I64; break;
            }
            exec_status status = memory_address(memory, (uint32_t)base.i32,
                                                instr->u32_imm, width,
                                                &address, err);
            if (status != EXEC_OK) return status;
            uint64_t bits = load_le(memory->data, address, width);
            if (sign && width < 8 && (bits & ((uint64_t)1 << (width * 8u - 1u)))) bits |= UINT64_MAX << (width * 8u);
            memset(value.nan_mode, 0, sizeof(value.nan_mode));
            if (value.type == WASM_VALTYPE_I32) value.i32=(int32_t)bits;
            else if (value.type == WASM_VALTYPE_I64) value.i64=(int64_t)bits;
            else if (value.type == WASM_VALTYPE_F32) { uint32_t b=(uint32_t)bits; memcpy(&value.f32,&b,4); }
            else memcpy(&value.f64,&bits,8);
            if (!stack_push(&stack, value)) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode >= 0x36 && instr->opcode <= 0x3e) {
            wasm_value value, base; uint32_t width, address; uint64_t bits;
            exec_memory *memory = instr->memory_index < eng->memory_count ?
                eng->memories[instr->memory_index] : NULL;
            if (!stack_pop(&stack, &value) || !stack_pop(&stack, &base) || base.type != WASM_VALTYPE_I32)
                return exec_fail(err, EXEC_ERROR_TRAP, "store operands missing");
            switch (instr->opcode) {
                case 0x36: width=4; bits=(uint32_t)value.i32; break;
                case 0x37: width=8; bits=(uint64_t)value.i64; break;
                case 0x38: { uint32_t b; width=4; memcpy(&b,&value.f32,4); bits=b; break; }
                case 0x39: width=8; memcpy(&bits,&value.f64,8); break;
                case 0x3a: width=1; bits=(uint32_t)value.i32; break;
                case 0x3b: width=2; bits=(uint32_t)value.i32; break;
                case 0x3c: width=1; bits=(uint64_t)value.i64; break;
                case 0x3d: width=2; bits=(uint64_t)value.i64; break;
                default: width=4; bits=(uint64_t)value.i64; break;
            }
            exec_status status = memory_address(memory, (uint32_t)base.i32,
                                                instr->u32_imm, width,
                                                &address, err);
            if (status != EXEC_OK) return status;
            store_le(memory->data, address, bits, width);
            continue;
        }

        if (instr->opcode == 0x3f) {
            exec_memory *memory = instr->memory_index < eng->memory_count ?
                eng->memories[instr->memory_index] : NULL;
            if (!memory) return exec_fail(err, EXEC_ERROR_TRAP, "memory missing");
            if (!stack_push(&stack, i32_value(memory->pages))) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }
        if (instr->opcode == 0x40) {
            wasm_value delta;
            if (!stack_pop(&stack, &delta) || delta.type != WASM_VALTYPE_I32) return exec_fail(err, EXEC_ERROR_TRAP, "memory.grow operand missing");
            exec_memory *memory = instr->memory_index < eng->memory_count ?
                eng->memories[instr->memory_index] : NULL;
            if (!memory) return exec_fail(err,EXEC_ERROR_TRAP,"memory missing");
            uint32_t old = memory->pages, add = (uint32_t)delta.i32;
            uint64_t pages = (uint64_t)old + add;
            if (pages > 65536u || (memory->has_max && pages > memory->max_pages)) {
                if (!stack_push(&stack, i32_value(UINT32_MAX))) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                continue;
            }
            size_t new_size = (size_t)pages * EXEC_PAGE_SIZE;
            uint8_t *grown = (uint8_t *)calloc(new_size ? new_size : 1, 1);
            if (!grown) { if (!stack_push(&stack, i32_value(UINT32_MAX))) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); continue; }
            memcpy(grown,memory->data,(size_t)old*EXEC_PAGE_SIZE); free(memory->data);
            memory->data=grown; memory->pages=(uint32_t)pages;
            if (!stack_push(&stack, i32_value(old))) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }
        if (instr->opcode == 0xd0 || instr->opcode == 0xd2) {
            wasm_value value; memset(&value,0,sizeof(value));
            if (instr->opcode == 0xd0) {
                if (!nullable_reference_for_heap(
                        eng, (int32_t)instr->u32_imm, &value.type))
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "invalid ref.null heap type");
            } else if (instr->opcode == 0xd2) {
                uint32_t function_type =
                    instr->u32_imm < eng->import_func_count ?
                    eng->import_func_types[instr->u32_imm] :
                    eng->funcs[instr->u32_imm -
                               eng->import_func_count].type_index;
                value.type = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE +
                                            function_type);
            }
            value.ref = instr->opcode == 0xd0 ? UINT32_MAX : instr->u32_imm;
            if (instr->opcode == 0xd2 && instr->u32_imm >= eng->import_func_count + eng->func_count)
                return exec_fail(err, EXEC_ERROR_TRAP, "ref.func index out of range");
            if (!stack_push(&stack,value)) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }
        if (instr->opcode == 0xd1) {
            wasm_value value;
            if (!stack_pop(&stack,&value) ||
                !is_reference_type(value.type))
                return exec_fail(err, EXEC_ERROR_TRAP, "ref.is_null operand missing");
            if (!stack_push(&stack,i32_value(value.ref == UINT32_MAX))) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0xd4) {
            wasm_value value;
            if (!stack_pop(&stack, &value) || !is_reference_type(value.type))
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "ref.as_non_null operand missing");
            if (value.ref == UINT32_MAX)
                return exec_fail(err, EXEC_ERROR_TRAP, "null reference");
            value.type = nonnullable_reference_type(value.type);
            if (!stack_push(&stack, value))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0x1a) {
            wasm_value ignored;
            if (!stack_pop(&stack, &ignored))
                return exec_fail(err, EXEC_ERROR_TRAP, "drop operand missing");
            continue;
        }

        if (instr->opcode == 0x1b) {
            wasm_value condition, second, first;
            if (!stack_pop(&stack, &condition) || !stack_pop(&stack, &second) ||
                !stack_pop(&stack, &first) || condition.type != WASM_VALTYPE_I32)
                return exec_fail(err, EXEC_ERROR_TRAP, "select operands missing");
            if (!instr->simd_op &&
                !same_value_type(eng, first.type, eng, second.type, 0))
                return exec_fail(err, EXEC_ERROR_TRAP, "select type mismatch");
            if (instr->simd_op) {
                wasm_valtype selected_type;
                memcpy(&selected_type, instr->v128_imm.bytes,
                       sizeof(selected_type));
                if (!global_type_is_compat(eng, first.type, eng,
                                           selected_type, 0) ||
                    !global_type_is_compat(eng, second.type, eng,
                                           selected_type, 0))
                {
                    char message[96];
                    snprintf(message, sizeof(message),
                             "typed select type mismatch (%u, %u; expected %u)",
                             (unsigned)first.type, (unsigned)second.type,
                             (unsigned)selected_type);
                    return exec_fail(err, EXEC_ERROR_TRAP, message);
                }
            }
            if (!stack_push(&stack, condition.i32 ? first : second))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0x10 || instr->opcode == 0x12) {
            if (instr->u32_imm >= eng->import_func_count + eng->func_count)
                return exec_fail(err, EXEC_ERROR_TRAP, "call target out of range");
            exec_func_type *callee_type = instr->u32_imm < eng->import_func_count ?
                &eng->types[eng->import_func_types[instr->u32_imm]] :
                &eng->types[eng->funcs[instr->u32_imm-eng->import_func_count].type_index];
            if (instr->opcode == 0x12) {
                /* return_call: tail call — restart function without recursion */
                for (int i = callee_type->param_count; i-- > 0;)
                    if (!stack_pop(&stack, &tail_args[i]))
                        return exec_fail(err, EXEC_ERROR_TRAP, "call arguments missing");
                tail_arg_count = callee_type->param_count;
                func_idx = instr->u32_imm;
                args = tail_args;
                arg_count = tail_arg_count;
                goto tail_entry;
            }
            wasm_value call_args[WAST_MAX_ARGS], call_results[WAST_MAX_RESULTS]; int call_result_count = 0;
            for (int i = callee_type->param_count; i-- > 0;) {
                if (!stack_pop(&stack, &call_args[i]))
                    return exec_fail(err, EXEC_ERROR_TRAP, "call arguments missing");
            }
            exec_status status = exec_invoke_depth(eng, instr->u32_imm, call_args,
                callee_type->param_count, call_results, &call_result_count, err, depth + 1);
            if (status != EXEC_OK) return status;
            for (int i = 0; i < call_result_count; i++)
                if (!stack_push(&stack, call_results[i]))
                    return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0x11 || instr->opcode == 0x13) {
            wasm_value table_operand;
            if (!stack_pop(&stack,&table_operand) || table_operand.type!=WASM_VALTYPE_I32)
                return exec_fail(err,EXEC_ERROR_TRAP,"call_indirect table operand missing");
            exec_table *table=eng->tables[instr->simd_op];
            uint32_t element=(uint32_t)table_operand.i32;
            if(element>=table->size)return exec_fail(err,EXEC_ERROR_TRAP,"undefined element");
            exec_table_element slot=table->elements[element];
            if(!slot.owner)return exec_fail(err,EXEC_ERROR_TRAP,"uninitialized element");
            waste_exec_engine *teng=slot.owner;
            uint32_t target=slot.func_idx;
            if(target>=teng->import_func_count+teng->func_count)
                return exec_fail(err,EXEC_ERROR_TRAP,"call_indirect target out of range");
            uint32_t actual_type_index=target<teng->import_func_count?
                teng->import_func_types[target]:
                teng->funcs[target-teng->import_func_count].type_index;
            if(!same_func_type(eng,instr->u32_imm,teng,actual_type_index))
                return exec_fail(err,EXEC_ERROR_TRAP,"indirect call type mismatch");
            exec_func_type *expected=&eng->types[instr->u32_imm];
            if (instr->opcode == 0x13) {
                /* return_call_indirect: tail call — restart without recursion */
                for(int i=expected->param_count;i-->0;)
                    if(!stack_pop(&stack,&tail_args[i]))
                        return exec_fail(err,EXEC_ERROR_TRAP,"call_indirect arguments missing");
                eng = teng;
                func_idx = target;
                args = tail_args;
                arg_count = expected->param_count;
                goto tail_entry;
            }
            wasm_value call_args[WAST_MAX_ARGS],call_results[WAST_MAX_RESULTS];int call_result_count=0;
            for(int i=expected->param_count;i-->0;)
                if(!stack_pop(&stack,&call_args[i]))
                    return exec_fail(err,EXEC_ERROR_TRAP,"call_indirect arguments missing");
            exec_status status=exec_invoke_depth(teng,target,call_args,expected->param_count,
                call_results,&call_result_count,err,depth+1);
            if(status!=EXEC_OK)return status;
            for(int i=0;i<call_result_count;i++)if(!stack_push(&stack,call_results[i]))
                return exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");
            continue;
        }

        if (instr->opcode == 0x14 || instr->opcode == 0x15) {
            if (instr->u32_imm >= eng->type_count)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "call_ref type out of range");
            exec_func_type *callee_type = &eng->types[instr->u32_imm];
            wasm_value reference;
            if (!stack_pop(&stack, &reference))
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "call_ref target missing");
            if (reference.ref == UINT32_MAX)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "null function reference");
            uint32_t target = reference.ref;
            if (target >= eng->import_func_count + eng->func_count)
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "call_ref target out of range");
            uint32_t actual_type_index = target < eng->import_func_count ?
                eng->import_func_types[target] :
                eng->funcs[target - eng->import_func_count].type_index;
            if (!same_func_type(eng, instr->u32_imm,
                                eng, actual_type_index))
                return exec_fail(err, EXEC_ERROR_TRAP,
                                 "call_ref type mismatch");
            if (instr->opcode == 0x15) {
                /* return_call_ref: tail call — restart without recursion */
                for (int i = callee_type->param_count; i-- > 0;)
                    if (!stack_pop(&stack, &tail_args[i]))
                        return exec_fail(err, EXEC_ERROR_TRAP,
                                         "call_ref arguments missing");
                func_idx = target;
                args = tail_args;
                arg_count = callee_type->param_count;
                goto tail_entry;
            }
            wasm_value call_args[WAST_MAX_ARGS];
            wasm_value call_results[WAST_MAX_RESULTS];
            int call_result_count = 0;
            for (int i = callee_type->param_count; i-- > 0;)
                if (!stack_pop(&stack, &call_args[i]))
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "call_ref arguments missing");
            exec_status status = exec_invoke_depth(
                eng, target, call_args, callee_type->param_count,
                call_results, &call_result_count, err, depth + 1);
            if (status != EXEC_OK) return status;
            for (int i = 0; i < call_result_count; i++)
                if (!stack_push(&stack, call_results[i]))
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "stack overflow");
            continue;
        }

        if (instr->opcode == 0x0f) break;

        if (instr->opcode == 0x41) {
            if (!stack_push(&stack, i32_value(instr->u32_imm)))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }
        if (instr->opcode == 0x42 || instr->opcode == 0x43 || instr->opcode == 0x44) {
            wasm_value value; memset(&value, 0, sizeof(value));
            if (instr->opcode == 0x42) { value.type=WASM_VALTYPE_I64; memcpy(&value.i64,instr->v128_imm.bytes,8); }
            else if (instr->opcode == 0x43) { value.type=WASM_VALTYPE_F32; memcpy(&value.f32,instr->v128_imm.bytes,4); }
            else { value.type=WASM_VALTYPE_F64; memcpy(&value.f64,instr->v128_imm.bytes,8); }
            if (!stack_push(&stack, value)) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        /* i32 ops: comparisons 0x45-0x4f, unary/binary 0x67-0x78, extend 0xc0-0xc1 */
        if ((instr->opcode >= 0x45 && instr->opcode <= 0x4f) ||
            (instr->opcode >= 0x67 && instr->opcode <= 0x78) ||
            instr->opcode == 0xc0 || instr->opcode == 0xc1) {
            exec_status numeric = exec_i32_numeric(instr->opcode, &stack, err);
            if (numeric != EXEC_OK) return numeric;
            continue;
        }

        /* i64 ops: comparisons 0x50-0x5a, unary/binary 0x79-0x8a, extend 0xc2-0xc4 */
        if ((instr->opcode >= 0x50 && instr->opcode <= 0x5a) ||
            (instr->opcode >= 0x79 && instr->opcode <= 0x8a) ||
            (instr->opcode >= 0xc2 && instr->opcode <= 0xc4)) {
            exec_status numeric = exec_i64_numeric(instr->opcode, &stack, err);
            if (numeric != EXEC_OK) return numeric;
            continue;
        }

        /* f32 ops: comparisons 0x5b-0x60, unary/binary 0x8b-0x98 */
        if ((instr->opcode >= 0x5b && instr->opcode <= 0x60) ||
            (instr->opcode >= 0x8b && instr->opcode <= 0x98)) {
            exec_status numeric = exec_f32_numeric(instr->opcode, &stack, err);
            if (numeric != EXEC_OK) return numeric;
            continue;
        }

        /* f64 ops: comparisons 0x61-0x66, unary/binary 0x99-0xa6 */
        if ((instr->opcode >= 0x61 && instr->opcode <= 0x66) ||
            (instr->opcode >= 0x99 && instr->opcode <= 0xa6)) {
            exec_status numeric = exec_f64_numeric(instr->opcode, &stack, err);
            if (numeric != EXEC_OK) return numeric;
            continue;
        }

        /* conversion ops 0xa7-0xbf */
        if (instr->opcode >= 0xa7 && instr->opcode <= 0xbf) {
            exec_status st = exec_conversion(instr->opcode, &stack, err);
            if (st != EXEC_OK) return st;
            continue;
        }

        /* 0xFC: saturating trunc (0x00-0x07) and bulk memory/table ops */
        if (instr->opcode == 0xFC) {
            uint32_t sub = instr->simd_op;
            if (sub <= 7) {
                exec_status st = exec_sat_trunc(sub, &stack, err);
                if (st != EXEC_OK) return st;
            } else if (sub == 10) { /* memory.copy */
                wasm_value n_v, src_v, dst_v;
                if (!stack_pop(&stack,&n_v)||!stack_pop(&stack,&src_v)||!stack_pop(&stack,&dst_v))
                    return exec_fail(err,EXEC_ERROR_TRAP,"memory.copy operands missing");
                if (instr->memory_index >= eng->memory_count ||
                    instr->source_memory_index >= eng->memory_count)
                    return exec_fail(err,EXEC_ERROR_TRAP,"memory index out of range");
                exec_memory *dst_memory = eng->memories[instr->memory_index];
                exec_memory *src_memory = eng->memories[instr->source_memory_index];
                uint32_t dst=(uint32_t)dst_v.i32,src=(uint32_t)src_v.i32,n=(uint32_t)n_v.i32;
                uint64_t dst_size=(uint64_t)dst_memory->pages*EXEC_PAGE_SIZE;
                uint64_t src_size=(uint64_t)src_memory->pages*EXEC_PAGE_SIZE;
                if ((uint64_t)dst+n>dst_size||(uint64_t)src+n>src_size)
                    return exec_fail(err,EXEC_ERROR_TRAP,"out of bounds memory access");
                if (dst_memory == src_memory)
                    memmove(dst_memory->data+dst,src_memory->data+src,n);
                else if (n)
                    memcpy(dst_memory->data+dst,src_memory->data+src,n);
            } else if (sub == 11) { /* memory.fill */
                wasm_value n_v, val_v, dst_v;
                if (!stack_pop(&stack,&n_v)||!stack_pop(&stack,&val_v)||!stack_pop(&stack,&dst_v))
                    return exec_fail(err,EXEC_ERROR_TRAP,"memory.fill operands missing");
                if (instr->memory_index >= eng->memory_count)
                    return exec_fail(err,EXEC_ERROR_TRAP,"memory index out of range");
                exec_memory *memory = eng->memories[instr->memory_index];
                uint32_t dst=(uint32_t)dst_v.i32,n=(uint32_t)n_v.i32;
                uint64_t mem_size=(uint64_t)memory->pages*EXEC_PAGE_SIZE;
                if ((uint64_t)dst+n>mem_size) return exec_fail(err,EXEC_ERROR_TRAP,"out of bounds memory access");
                memset(memory->data+dst,(uint8_t)val_v.i32,n);
            } else if (sub == 8) { /* memory.init */
                wasm_value n_v, src_v, dst_v;
                if (!stack_pop(&stack,&n_v)||!stack_pop(&stack,&src_v)||!stack_pop(&stack,&dst_v))
                    return exec_fail(err,EXEC_ERROR_TRAP,"memory.init operands missing");
                if (instr->memory_index >= eng->memory_count ||
                    instr->u32_imm >= eng->data_count)
                    return exec_fail(err,EXEC_ERROR_TRAP,"memory.init index out of range");
                exec_memory *memory = eng->memories[instr->memory_index];
                uint32_t dst=(uint32_t)dst_v.i32,src=(uint32_t)src_v.i32,n=(uint32_t)n_v.i32;
                uint32_t data_length = eng->data_dropped[instr->u32_imm] ? 0 :
                    eng->data_seg_lengths[instr->u32_imm];
                uint64_t mem_size=(uint64_t)memory->pages*EXEC_PAGE_SIZE;
                if ((uint64_t)dst+n>mem_size || (uint64_t)src+n>data_length)
                    return exec_fail(err,EXEC_ERROR_TRAP,"out of bounds memory access");
                if (n)
                    memcpy(memory->data+dst,eng->data_segs[instr->u32_imm]+src,n);
            } else if (sub == 9) { /* data.drop */
                if (instr->u32_imm >= eng->data_count)
                    return exec_fail(err,EXEC_ERROR_TRAP,"data segment index out of range");
                eng->data_dropped[instr->u32_imm] = 1;
            } else if (sub == 12) { /* table.init */
                wasm_value n_v, src_v, dst_v;
                if (!stack_pop(&stack, &n_v) || !stack_pop(&stack, &src_v) ||
                    !stack_pop(&stack, &dst_v))
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "table.init operands missing");
                uint32_t elem = instr->u32_imm;
                uint32_t table_index = instr->v128_imm.bytes[0];
                if (elem >= eng->elem_count || table_index >= eng->table_count)
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "table.init index out of range");
                exec_table *table = eng->tables[table_index];
                uint32_t n = (uint32_t)n_v.i32;
                uint32_t src = (uint32_t)src_v.i32;
                uint32_t dst = (uint32_t)dst_v.i32;
                uint32_t length = eng->elem_dropped[elem] ? 0 :
                                  eng->elem_lengths[elem];
                if ((uint64_t)src + n > length ||
                    (uint64_t)dst + n > table->size)
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "out of bounds table access");
                if (n)
                    memcpy(table->elements + dst,
                           eng->elem_values[elem] + src,
                           (size_t)n * sizeof(exec_table_element));
            } else if (sub == 13) { /* elem.drop */
                if (instr->u32_imm >= eng->elem_count)
                    return exec_fail(err, EXEC_ERROR_TRAP,
                                     "element index out of range");
                eng->elem_dropped[instr->u32_imm] = 1;
            } else if (sub == 15) { /* table.grow */
                wasm_value delta_v, init_v;
                if (!stack_pop(&stack,&delta_v)||!stack_pop(&stack,&init_v))
                    return exec_fail(err,EXEC_ERROR_TRAP,"table.grow operands missing");
                uint32_t tidx=instr->u32_imm;
                if (tidx>=eng->table_count||!eng->tables[tidx])
                    return exec_fail(err,EXEC_ERROR_TRAP,"table index out of range");
                exec_table *tbl=eng->tables[tidx];
                uint32_t old_size=tbl->size,delta=(uint32_t)delta_v.i32;
                if ((uint64_t)old_size+delta>0xFFFFFFFFu||
                    (tbl->has_max&&old_size+delta>tbl->max_size)) {
                    if (!stack_push(&stack,i32_value(UINT32_MAX))) return exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");
                } else {
                    uint32_t new_size=old_size+delta;
                    exec_table_element *nel=(exec_table_element*)realloc(tbl->elements,
                        new_size?new_size*sizeof(*nel):1);
                    if (!nel) { if (!stack_push(&stack,i32_value(UINT32_MAX))) return exec_fail(err,EXEC_ERROR_TRAP,"stack overflow"); }
                    else {
                        for (uint32_t i=old_size;i<new_size;i++) {
                            nel[i].owner = init_v.ref == UINT32_MAX ? NULL : eng;
                            nel[i].func_idx = init_v.ref == UINT32_MAX ? 0 : init_v.ref;
                        }
                        tbl->elements=nel; tbl->size=new_size;
                        if (!stack_push(&stack,i32_value(old_size))) return exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");
                    }
                }
            } else if (sub == 16) { /* table.size */
                uint32_t tidx=instr->u32_imm;
                if (tidx>=eng->table_count||!eng->tables[tidx])
                    return exec_fail(err,EXEC_ERROR_TRAP,"table index out of range");
                if (!stack_push(&stack,i32_value(eng->tables[tidx]->size)))
                    return exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");
            } else if (sub == 14) { /* table.copy */
                wasm_value n_v, src_v, dst_v;
                if (!stack_pop(&stack,&n_v)||!stack_pop(&stack,&src_v)||!stack_pop(&stack,&dst_v))
                    return exec_fail(err,EXEC_ERROR_TRAP,"table.copy operands missing");
                uint32_t dtidx=instr->u32_imm,stidx=instr->v128_imm.bytes[0];
                if (dtidx>=eng->table_count||stidx>=eng->table_count)
                    return exec_fail(err,EXEC_ERROR_TRAP,"table index out of range");
                exec_table *dt=eng->tables[dtidx],*st=eng->tables[stidx];
                uint32_t dst=(uint32_t)dst_v.i32,src=(uint32_t)src_v.i32,n=(uint32_t)n_v.i32;
                if ((uint64_t)dst+n>dt->size||(uint64_t)src+n>st->size)
                    return exec_fail(err,EXEC_ERROR_TRAP,"out of bounds table access");
                memmove(dt->elements+dst,st->elements+src,n*sizeof(exec_table_element));
            } else if (sub == 17) { /* table.fill */
                wasm_value n_v, val_v, dst_v;
                if (!stack_pop(&stack,&n_v)||!stack_pop(&stack,&val_v)||!stack_pop(&stack,&dst_v))
                    return exec_fail(err,EXEC_ERROR_TRAP,"table.fill operands missing");
                uint32_t tidx=instr->u32_imm;
                if (tidx>=eng->table_count||!eng->tables[tidx])
                    return exec_fail(err,EXEC_ERROR_TRAP,"table index out of range");
                exec_table *tbl=eng->tables[tidx];
                uint32_t dst=(uint32_t)dst_v.i32,n=(uint32_t)n_v.i32;
                if ((uint64_t)dst+n>tbl->size) return exec_fail(err,EXEC_ERROR_TRAP,"out of bounds table access");
                /* set val_v as table element — only funcref/externref supported */
                exec_table_element fill_el;
                if (val_v.ref==UINT32_MAX) { fill_el.owner=NULL; fill_el.func_idx=0; }
                else { fill_el.owner=eng; fill_el.func_idx=val_v.ref; }
                for (uint32_t i=0;i<n;i++) tbl->elements[dst+i]=fill_el;
            } else {
                /* Unsupported 0xFC operation. */
            }
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

            if (op <= 0x0b || (op >= 0x54 && op <= 0x5d)) {
                wasm_value base, vector, out;
                uint32_t address, width = 16;
                int lane_memory = op >= 0x54 && op <= 0x5b;
                int store = op == 0x0b || (op >= 0x58 && op <= 0x5b);
                if (instr->memory_index >= eng->memory_count)
                    return exec_fail(err,EXEC_ERROR_TRAP,"memory index out of range");
                exec_memory *memory = eng->memories[instr->memory_index];
                if (lane_memory || store) {
                    if (!simd_pop(&stack,&vector) || !stack_pop(&stack,&base) ||
                        base.type != WASM_VALTYPE_I32)
                        return exec_fail(err,EXEC_ERROR_TRAP,"SIMD memory operands missing");
                } else if (!stack_pop(&stack,&base) || base.type != WASM_VALTYPE_I32) {
                    return exec_fail(err,EXEC_ERROR_TRAP,"SIMD memory address missing");
                }
                if (lane_memory) width = UINT32_C(1) << ((op - 0x54) & 3u);
                else if (op == 0x01 || op == 0x02) width=8;
                else if (op == 0x03 || op == 0x04) width=8;
                else if (op == 0x05 || op == 0x06) width=8;
                else if (op >= 0x07 && op <= 0x0a) width=UINT32_C(1)<<(op-0x07);
                else if (op == 0x5c) width=4;
                else if (op == 0x5d) width=8;
                exec_status mem_status=memory_address(memory,(uint32_t)base.i32,
                    instr->u32_imm,width,&address,err);
                if(mem_status!=EXEC_OK)return mem_status;
                if(store) {
                    if(lane_memory) {
                        uint32_t lane_width=UINT32_C(1)<<((op-0x58)&3u);
                        memcpy(memory->data+address,
                               vector.v128.bytes+instr->lane_index*lane_width,
                               lane_width);
                    } else memcpy(memory->data+address,vector.v128.bytes,16);
                    continue;
                }
                if(lane_memory) {
                    memcpy(vector.v128.bytes+instr->lane_index*width,
                           memory->data+address,width);
                    if(!stack_push(&stack,vector))return exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");
                    continue;
                }
                out=simd_zero();
                if(op==0x00) memcpy(out.v128.bytes,memory->data+address,16);
                else if(op>=0x01&&op<=0x06) {
                    uint32_t source_width=op<=0x02?1:op<=0x04?2:4;
                    uint32_t dest_width=source_width*2;
                    int signed_=(op&1)!=0;
                    for(uint32_t i=0;i<16/dest_width;i++) {
                        uint64_t raw=0;memcpy(&raw,memory->data+address+i*source_width,source_width);
                        if(signed_&&source_width<8&&(raw&(UINT64_C(1)<<(source_width*8-1))))
                            raw|=UINT64_MAX<<(source_width*8);
                        simd_set_lane(&out,i,dest_width,raw);
                    }
                } else if(op>=0x07&&op<=0x0a) {
                    uint64_t raw=0;memcpy(&raw,memory->data+address,width);
                    for(uint32_t i=0;i<16/width;i++)simd_set_lane(&out,i,width,raw);
                } else memcpy(out.v128.bytes,memory->data+address,width);
                if(!stack_push(&stack,out))return exec_fail(err,EXEC_ERROR_TRAP,"stack overflow");
                continue;
            }

            int standard_simd = exec_standard_simd_integer(
                op, instr->lane_index, &instr->v128_imm, &stack, err);
            if (standard_simd < 0) return err->status;
            if (standard_simd > 0) continue;
            standard_simd = exec_standard_simd_float(op, &stack, err);
            if (standard_simd < 0) return err->status;
            if (standard_simd > 0) continue;

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
func_return:
    /* Collect results */
    if (stack.top < type->result_count)
        return exec_fail(err, EXEC_ERROR_TRAP, "missing result");
    for (int i = type->result_count; i-- > 0;)
        if (results) stack_pop(&stack, &results[i]); else { wasm_value ignored; stack_pop(&stack, &ignored); }
    if (result_count) *result_count = type->result_count;

    return EXEC_OK;
}

exec_status exec_invoke(waste_exec_engine *eng,
                        uint32_t func_idx,
                        const wasm_value *args, int arg_count,
                        wasm_value *results, int *result_count,
                        exec_error *err) {
    return exec_invoke_depth(eng, func_idx, args, arg_count, results,
                             result_count, err, 0);
}
