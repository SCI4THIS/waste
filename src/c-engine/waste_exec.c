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
    EXEC_MAX_FUNCS    = WAST_MAX_FUNCS,
    EXEC_MAX_EXPORTS  = 128,
    EXEC_MAX_NAME     = 128,
    EXEC_MAX_INSTRS   = 4096,
    EXEC_MAX_STACK    = 64,
    EXEC_MAX_LOCALS   = 8,
    EXEC_MAX_CONTROL  = 64,
    EXEC_MAX_CALL_DEPTH = 64,
    EXEC_MAX_GLOBALS  = 128,
    EXEC_MAX_TABLES   = 16,
    EXEC_PAGE_SIZE    = 65536,
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
    wasm_valtype results[WAST_MAX_RESULTS];
    int          param_count;
    int          result_count;
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
    exec_memory    *memory;
    exec_memory     owned_memory;
    int             owns_memory;
    exec_table     *tables[EXEC_MAX_TABLES];
    exec_table      owned_tables[EXEC_MAX_TABLES];
    uint32_t        table_count;
    uint32_t        import_table_count;
    uint32_t        start_func;
    int             has_start;
};

static int same_value_type(const waste_exec_engine *left_engine, wasm_valtype left,
                           const waste_exec_engine *right_engine, wasm_valtype right,
                           unsigned depth) {
    int left_indexed=WASM_VALTYPE_IS_TYPE_REF(left);
    int right_indexed=WASM_VALTYPE_IS_TYPE_REF(right);
    if(!left_indexed||!right_indexed)return left==right;
    if(((unsigned)left<WASM_VALTYPE_TYPE_REF_BASE)!=
       ((unsigned)right<WASM_VALTYPE_TYPE_REF_BASE)||depth>WAST_MAX_TYPES||
       !left_engine||!right_engine)return 0;
    uint32_t li=WASM_VALTYPE_TYPE_REF_INDEX(left),ri=WASM_VALTYPE_TYPE_REF_INDEX(right);
    if(li>=left_engine->type_count||ri>=right_engine->type_count)return 0;
    const exec_func_type *a=&left_engine->types[li],*b=&right_engine->types[ri];
    if(a->param_count!=b->param_count||a->result_count!=b->result_count)return 0;
    for(int i=0;i<a->param_count;i++)if(!same_value_type(left_engine,a->params[i],right_engine,b->params[i],depth+1))return 0;
    for(int i=0;i<a->result_count;i++)if(!same_value_type(left_engine,a->results[i],right_engine,b->results[i],depth+1))return 0;
    return 1;
}

static int same_func_type(const waste_exec_engine *left_engine, uint32_t left_index,
                          const waste_exec_engine *right_engine, uint32_t right_index) {
    if(!left_engine||!right_engine||left_index>=left_engine->type_count||
       right_index>=right_engine->type_count)return 0;
    const exec_func_type *a=&left_engine->types[left_index],*b=&right_engine->types[right_index];
    if(a->param_count!=b->param_count||a->result_count!=b->result_count)return 0;
    for(int i=0;i<a->param_count;i++)if(!same_value_type(left_engine,a->params[i],right_engine,b->params[i],0))return 0;
    for(int i=0;i<a->result_count;i++)if(!same_value_type(left_engine,a->results[i],right_engine,b->results[i],0))return 0;
    return 1;
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
        if (shift < 64) value |= (uint64_t)(byte & 0x7fu) << shift;
        shift += 7;
        if (!(byte & 0x80u)) {
            if (shift < 64 && (byte & 0x40u)) value |= UINT64_MAX << shift;
            *out = (int64_t)value; return 1;
        }
    }
    return 0;
}

static int er_valtype(exec_reader *r, wasm_valtype *out) {
    uint8_t byte;
    int32_t heap;
    if (!er_u8(r, &byte)) return 0;
    switch (byte) {
        case 0x7f: *out=WASM_VALTYPE_I32; return 1;
        case 0x7e: *out=WASM_VALTYPE_I64; return 1;
        case 0x7d: *out=WASM_VALTYPE_F32; return 1;
        case 0x7c: *out=WASM_VALTYPE_F64; return 1;
        case 0x7b: *out=WASM_VALTYPE_V128; return 1;
        case 0x70: *out=WASM_VALTYPE_FUNCREF; return 1;
        case 0x6f: *out=WASM_VALTYPE_EXTERNREF; return 1;
        case 0x63: case 0x64:
            if (!er_i32(r,&heap)) return 0;
            if (heap == -16) *out=byte==0x63?WASM_VALTYPE_FUNCREF:WASM_VALTYPE_FUNCREF_NONNULL;
            else if (heap == -17) *out=byte==0x63?WASM_VALTYPE_EXTERNREF:WASM_VALTYPE_EXTERNREF_NONNULL;
            else if (heap >= 0 && heap < WAST_MAX_TYPES)
                *out=(wasm_valtype)((byte==0x63?WASM_VALTYPE_TYPE_REF_NULL_BASE:WASM_VALTYPE_TYPE_REF_BASE)+(uint32_t)heap);
            else return 0;
            return 1;
        default: return 0;
    }
}

static int is_reference_type(wasm_valtype type) {
    return type==WASM_VALTYPE_FUNCREF||type==WASM_VALTYPE_EXTERNREF||
           type==WASM_VALTYPE_FUNCREF_NONNULL||type==WASM_VALTYPE_EXTERNREF_NONNULL||
           WASM_VALTYPE_IS_TYPE_REF(type);
}

static int is_nullable_reference_type(wasm_valtype type) {
    return type==WASM_VALTYPE_FUNCREF||type==WASM_VALTYPE_EXTERNREF||
           (WASM_VALTYPE_IS_TYPE_REF(type) &&
            (unsigned)type<WASM_VALTYPE_TYPE_REF_BASE);
}

static int32_t reference_heap_type(wasm_valtype type) {
    if(type==WASM_VALTYPE_FUNCREF||type==WASM_VALTYPE_FUNCREF_NONNULL)return -16;
    if(type==WASM_VALTYPE_EXTERNREF||type==WASM_VALTYPE_EXTERNREF_NONNULL)return -17;
    return (int32_t)WASM_VALTYPE_TYPE_REF_INDEX(type);
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
            if (!er_valtype(sec,&eng->types[i].params[p]))
                return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported param type");
        }
        if (!er_u32(sec, &results) || results > WAST_MAX_RESULTS)
            return exec_fail(err, EXEC_ERROR_FORMAT, "unsupported result count");
        eng->types[i].result_count = (int)results;
        for (uint32_t result = 0; result < results; result++) {
            if (!er_valtype(sec,&eng->types[i].results[result]))
                return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported result type");
        }
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
        memcpy(module,module_bytes,module_len); module[module_len]='\0';
        memcpy(name,name_bytes,name_len); name[name_len]='\0';
        if (kind == 0) {
            uint32_t type_index;
            if (!er_u32(sec,&type_index) || type_index >= eng->type_count || eng->import_func_count >= EXEC_MAX_FUNCS)
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid function import type");
            const exec_host_import *binding = find_host_import(imports,module,name);
            if (!binding || !binding->function) return exec_fail(err, EXEC_ERROR_NOT_FOUND, "unresolved function import");
            if(binding->has_wasm_type&&!same_func_type(eng,type_index,binding->type_owner,binding->type_index))
                return exec_fail(err,EXEC_ERROR_FORMAT,"function import type mismatch");
            uint32_t index=eng->import_func_count++;
            eng->import_func_types[index]=type_index; eng->import_funcs[index]=binding->function;
            eng->import_host_data[index]=binding->host_data;
        } else if (kind == 1) {
            wasm_valtype type; uint32_t flags,initial,maximum=0;
            if (!er_valtype(sec,&type) || !is_reference_type(type) || !er_u32(sec,&flags) || flags>1 ||
                !er_u32(sec,&initial) || ((flags&1u) && !er_u32(sec,&maximum)) || ((flags&1u) && maximum<initial) ||
                eng->table_count>=EXEC_MAX_TABLES) return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table import type");
            exec_table *table=find_table_import(imports,module,name);
            if (!table) return exec_fail(err,EXEC_ERROR_NOT_FOUND,"unresolved table import");
            if (!same_value_type(eng,type,table->type_owner,table->element_type,0) ||
                table->size<initial || ((flags&1u) && (!table->has_max || table->max_size>maximum)))
                return exec_fail(err,EXEC_ERROR_FORMAT,"table import type mismatch");
            eng->tables[eng->table_count++]=table; eng->import_table_count++;
        } else if (kind == 2) {
            uint32_t flags,initial,maximum=0;
            if (!er_u32(sec,&flags) || flags>1 || !er_u32(sec,&initial) || ((flags&1u) && !er_u32(sec,&maximum)) ||
                initial>65536u || ((flags&1u) && maximum<initial)) return exec_fail(err,EXEC_ERROR_FORMAT,"invalid memory import type");
            exec_memory *memory=find_memory_import(imports,module,name);
            if (eng->memory) return exec_fail(err,EXEC_ERROR_UNSUPPORTED,"multiple memories unsupported");
            if (!memory) return exec_fail(err,EXEC_ERROR_NOT_FOUND,"unresolved memory import");
            if (memory->pages<initial || memory->pages>65536u || ((flags&1u) && (!memory->has_max || memory->max_pages>maximum)) ||
                (memory->pages && !memory->data)) return exec_fail(err,EXEC_ERROR_FORMAT,"memory import type mismatch");
            eng->memory=memory;
        } else if (kind == 3) {
            uint8_t mutability; wasm_valtype value_type;
            if (!er_valtype(sec,&value_type) || !er_u8(sec,&mutability) || mutability>1 || eng->global_count>=EXEC_MAX_GLOBALS)
                return exec_fail(err,EXEC_ERROR_FORMAT,"invalid global import type");
            exec_global *global=find_global_import(imports,module,name);
            if (!global) return exec_fail(err,EXEC_ERROR_NOT_FOUND,"unresolved global import");
            if (!global_type_is_compat(global->type_owner,global->value.type,eng,value_type,mutability) ||
                global->mutable_!=mutability)
                return exec_fail(err,EXEC_ERROR_FORMAT,"global import type mismatch");
            eng->globals[eng->global_count++]=global; eng->import_global_count++;
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
        if (!er_u8(sec, &kind) || kind > 3 || !er_u32(sec, &idx))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid export");
        if ((kind==0 && idx>=eng->import_func_count+eng->func_count) ||
            (kind==1 && idx>=eng->table_count) || (kind==2 && (!eng->memory || idx!=0)) ||
            (kind==3 && idx>=eng->global_count)) return exec_fail(err,EXEC_ERROR_FORMAT,"invalid export index");
        memcpy(eng->exports[i].name, name, name_len);
        eng->exports[i].name[name_len] = '\0';
        eng->exports[i].index = idx; eng->exports[i].kind=kind;
    }
    return EXEC_OK;
}

static exec_status parse_memory(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t count, flags, initial, maximum = 0;
    if (!er_u32(sec, &count) || count > 1) return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "multiple memories unsupported");
    if (!count) return EXEC_OK;
    if (eng->memory) return exec_fail(err,EXEC_ERROR_UNSUPPORTED,"multiple memories unsupported");
    if (!er_u32(sec, &flags) || flags > 1 || !er_u32(sec, &initial) ||
        ((flags & 1u) && !er_u32(sec, &maximum)) || initial > 65536u ||
        ((flags & 1u) && maximum < initial))
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid memory limits");
    size_t bytes = (size_t)initial * EXEC_PAGE_SIZE;
    eng->owned_memory.data = (uint8_t *)calloc(bytes ? bytes : 1, 1);
    if (!eng->owned_memory.data) return exec_fail(err, EXEC_ERROR_FORMAT, "memory allocation failed");
    eng->owned_memory.pages=initial; eng->owned_memory.has_max=(uint8_t)(flags&1u); eng->owned_memory.max_pages=maximum;
    eng->memory=&eng->owned_memory; eng->owns_memory=1;
    return EXEC_OK;
}

static exec_status parse_tables(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t count;
    if (!er_u32(sec,&count) || count>EXEC_MAX_TABLES-eng->table_count) return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table count");
    for(uint32_t i=0;i<count;i++) {
        wasm_valtype type; uint32_t flags,initial,maximum=0;
        if (!er_valtype(sec,&type) || !is_reference_type(type) || !er_u32(sec,&flags) || flags>1 ||
            !er_u32(sec,&initial) || ((flags&1u) && !er_u32(sec,&maximum)) || ((flags&1u) && maximum<initial))
            return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table type");
        uint32_t index=eng->table_count; exec_table *table=&eng->owned_tables[index];
        table->elements=(exec_table_element *)malloc((initial ? initial : 1)*sizeof(exec_table_element));
        if (!table->elements) return exec_fail(err,EXEC_ERROR_FORMAT,"table allocation failed");
        for(uint32_t j=0;j<initial;j++){table->elements[j].owner=NULL;table->elements[j].func_idx=0;}
        table->size=initial; table->has_max=(uint8_t)(flags&1u); table->max_size=maximum;
        table->element_type=type;
        table->type_owner=eng;
        eng->tables[eng->table_count++]=table;
    }
    return EXEC_OK;
}

static exec_status parse_globals(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t count;
    if (!er_u32(sec, &count) || count > EXEC_MAX_GLOBALS) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid global count");
    if (count > EXEC_MAX_GLOBALS-eng->global_count) return exec_fail(err,EXEC_ERROR_FORMAT,"too many globals");
    for (uint32_t i = 0; i < count; i++) {
        uint32_t index=eng->global_count+i; exec_global *global=&eng->owned_globals[index];
        wasm_valtype type; uint8_t mutability, opcode, end; const uint8_t *bits;
        if (!er_valtype(sec, &type) || !er_u8(sec, &mutability) || mutability > 1 || !er_u8(sec, &opcode))
            return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported global initializer");
        memset(global,0,sizeof(*global));
        global->type_owner = eng;
        if (type == WASM_VALTYPE_I32 && opcode == 0x41) {
            int32_t value; if (!er_i32(sec, &value)) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid i32 global");
            global->value.type=WASM_VALTYPE_I32; global->value.i32=value;
        } else if (type == WASM_VALTYPE_I64 && opcode == 0x42) {
            int64_t value; if (!er_i64(sec, &value)) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid i64 global");
            global->value.type=WASM_VALTYPE_I64; global->value.i64=value;
        } else if (type == WASM_VALTYPE_F32 && opcode == 0x43) {
            if (!er_bytes(sec, 4, &bits)) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid f32 global");
            global->value.type=WASM_VALTYPE_F32; memcpy(&global->value.f32,bits,4);
        } else if (type == WASM_VALTYPE_F64 && opcode == 0x44) {
            if (!er_bytes(sec, 8, &bits)) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid f64 global");
            global->value.type=WASM_VALTYPE_F64; memcpy(&global->value.f64,bits,8);
        } else if (is_reference_type(type) && opcode == 0xd0) {
            int32_t heap_type;
            if (!er_i32(sec, &heap_type) ||
                !is_nullable_reference_type(type) ||
                heap_type != reference_heap_type(type))
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid ref.null global");
            global->value.type = type;
            global->value.ref = UINT32_MAX;
        } else if (is_reference_type(type) && type != WASM_VALTYPE_EXTERNREF &&
                   type != WASM_VALTYPE_EXTERNREF_NONNULL && opcode == 0xd2) {
            uint32_t function;
            if (!er_u32(sec, &function) || function >= eng->import_func_count + eng->func_count)
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid ref.func global");
            global->value.type=type; global->value.ref=function;
        } else if (opcode == 0x23) {
            uint32_t import_index;
            if (!er_u32(sec, &import_index) || import_index >= eng->import_global_count)
                return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "global.get in init must reference an import");
            global->value = eng->globals[import_index]->value;
            global->type_owner = eng->globals[import_index]->type_owner;
        } else return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported global initializer");
        if (!er_u8(sec, &end) || end != 0x0b) return exec_fail(err, EXEC_ERROR_FORMAT, "unterminated global initializer");
        global->mutable_=mutability; eng->globals[index]=global;
    }
    eng->global_count += count;
    return EXEC_OK;
}

static exec_status parse_start(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t func_index;
    if (!er_u32(sec, &func_index) || func_index >= eng->import_func_count + eng->func_count)
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid start function index");
    eng->start_func = func_index;
    eng->has_start = 1;
    return EXEC_OK;
}

static exec_status parse_data(waste_exec_engine *eng, exec_reader *sec, exec_error *err) {
    uint32_t count;
    if (!er_u32(sec, &count)) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid data count");
    for (uint32_t i = 0; i < count; i++) {
        uint32_t mode, memory_index = 0, length; int32_t offset; uint8_t opcode, end; const uint8_t *data;
        if (!er_u32(sec, &mode)) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid data segment");
        if (mode == 1) {
            if (!er_u32(sec, &length) || !er_bytes(sec, length, &data)) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid passive data");
            continue;
        }
        if (mode == 2 && !er_u32(sec, &memory_index)) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid data memory");
        if ((mode != 0 && mode != 2) || memory_index != 0 || !eng->memory ||
            !er_u8(sec, &opcode) || opcode != 0x41 || !er_i32(sec, &offset) || offset < 0 ||
            !er_u8(sec, &end) || end != 0x0b || !er_u32(sec, &length) || !er_bytes(sec, length, &data))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid active data segment");
        if ((uint64_t)(uint32_t)offset + length >
            (uint64_t)eng->memory->pages * EXEC_PAGE_SIZE)
            return exec_fail(err, EXEC_ERROR_TRAP, "out of bounds memory access");
        memcpy(eng->memory->data + (uint32_t)offset, data, length);
    }
    return EXEC_OK;
}

static exec_status parse_elements(waste_exec_engine *eng, exec_reader *sec,
                                  exec_error *err) {
    uint32_t count;
    if (!er_u32(sec, &count))
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element count");
    for (uint32_t segment = 0; segment < count; segment++) {
        uint32_t mode, table_index = 0, item_count, offset = 0;
        wasm_valtype ref_type = WASM_VALTYPE_FUNCREF;
        int active;
        if (!er_u32(sec, &mode) || (mode != 4 && mode != 5 && mode != 6 && mode != 7))
            return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported element segment");
        active = mode == 4 || mode == 6;
        if (mode == 6 && !er_u32(sec, &table_index))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element table");
        if (active) {
            uint8_t opcode, end;
            int32_t signed_offset;
            if (!er_u8(sec, &opcode) || opcode != 0x41 ||
                !er_i32(sec, &signed_offset) || signed_offset < 0 ||
                !er_u8(sec, &end) || end != 0x0b)
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element offset");
            offset = (uint32_t)signed_offset;
        }
        if (mode != 4 && (!er_valtype(sec, &ref_type) ||
                          !is_reference_type(ref_type)))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element type");
        if (!er_u32(sec, &item_count))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element length");
        if (active && (table_index >= eng->table_count ||
            ref_type != eng->tables[table_index]->element_type))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid active element segment");
        if (active && (uint64_t)offset + item_count > eng->tables[table_index]->size)
            return exec_fail(err, EXEC_ERROR_TRAP, "out of bounds table access");
        for (uint32_t item = 0; item < item_count; item++) {
            uint8_t opcode, end;
            exec_table_element slot = {NULL, 0};
            if (!er_u8(sec, &opcode))
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element expression");
            if (opcode == 0xd2) {
                uint32_t value;
                if ((ref_type == WASM_VALTYPE_EXTERNREF || ref_type == WASM_VALTYPE_EXTERNREF_NONNULL) ||
                    !er_u32(sec, &value) ||
                    value >= eng->import_func_count + eng->func_count)
                    return exec_fail(err, EXEC_ERROR_FORMAT, "invalid ref.func element");
                slot.owner = eng; slot.func_idx = value;
            } else if (opcode == 0xd0) {
                int32_t heap_type;
                if (!er_i32(sec, &heap_type) ||
                    !is_nullable_reference_type(ref_type) ||
                    heap_type != reference_heap_type(ref_type))
                    return exec_fail(err, EXEC_ERROR_FORMAT, "invalid ref.null element");
                /* slot.owner remains NULL = null reference */
            } else {
                return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported element expression");
            }
            if (!er_u8(sec, &end) || end != 0x0b)
                return exec_fail(err, EXEC_ERROR_FORMAT, "unterminated element expression");
            if (active) eng->tables[table_index]->elements[offset + item] = slot;
        }
    }
    return EXEC_OK;
}

/* ---- Code section ---- */

static exec_status parse_body(waste_exec_engine *eng, exec_func *func, exec_reader *body, exec_error *err) {
    uint32_t local_groups;
    if (!er_u32(body, &local_groups))
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid local declarations");
    for (uint32_t group = 0; group < local_groups; group++) {
        uint32_t count; uint8_t type;
        if (!er_u32(body, &count) || !er_u8(body, &type) ||
            count > EXEC_MAX_LOCALS - func->local_count)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid local declaration");
        wasm_valtype value_type;
        switch (type) {
            case 0x7f: value_type = WASM_VALTYPE_I32; break;
            case 0x7e: value_type = WASM_VALTYPE_I64; break;
            case 0x7d: value_type = WASM_VALTYPE_F32; break;
            case 0x7c: value_type = WASM_VALTYPE_F64; break;
            case 0x7b: value_type = WASM_VALTYPE_V128; break;
            default: return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported local type");
        }
        for (uint32_t i = 0; i < count; i++) func->locals[func->local_count++] = value_type;
    }

    /* Allocate instruction buffer based on remaining body size */
    size_t capacity = (size_t)(body->end - body->cursor);
    exec_instr *code = (exec_instr *)calloc(capacity + 1, sizeof(*code));
    if (!code)
        return exec_fail(err, EXEC_ERROR_FORMAT, "code alloc failed");

    uint32_t code_size = 0;
    uint32_t controls[EXEC_MAX_CONTROL];
    uint32_t control_size = 0;
    while (body->cursor < body->end) {
        uint8_t byte;
        if (!er_u8(body, &byte)) {
            free(code);
            return exec_fail(err, EXEC_ERROR_FORMAT, "truncated instruction");
        }
        exec_instr instr;
        memset(&instr, 0, sizeof(instr));
        if (byte == 0x00 || byte == 0x01) {
            instr.opcode = byte;
        } else if (byte == 0x02 || byte == 0x03 || byte == 0x04) {
            uint8_t block_type;
            if (!er_u8(body, &block_type)) { free(code); return exec_fail(err, EXEC_ERROR_FORMAT, "missing block type"); }
            if (control_size >= EXEC_MAX_CONTROL) {
                free(code); return exec_fail(err, EXEC_ERROR_FORMAT, "control nesting limit exceeded");
            }
            instr.opcode = byte;
            if (block_type == 0x40) {
                instr.v128_imm.bytes[0] = 0; instr.v128_imm.bytes[1] = 0;
            } else if (block_type == 0x7f || block_type == 0x7e || block_type == 0x7d ||
                       block_type == 0x7c || block_type == 0x7b) {
                instr.v128_imm.bytes[0] = 0; instr.v128_imm.bytes[1] = 1;
            } else {
                body->cursor--;
                int32_t type_index;
                if (!er_i32(body, &type_index) || type_index < 0 || (uint32_t)type_index >= eng->type_count) {
                    free(code); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid block type index");
                }
                exec_func_type *block_sig = &eng->types[type_index];
                instr.v128_imm.bytes[0] = (uint8_t)block_sig->param_count;
                instr.v128_imm.bytes[1] = (uint8_t)block_sig->result_count;
            }
            controls[control_size++] = code_size;
        } else if (byte == 0x05) {
            if (!control_size || code[controls[control_size - 1]].opcode != 0x04) {
                free(code); return exec_fail(err, EXEC_ERROR_FORMAT, "else without if");
            }
            instr.opcode = byte;
            code[controls[control_size - 1]].u32_imm = code_size + 1;
        } else if (byte == 0x0c || byte == 0x0d) {
            uint32_t depth;
            if (!er_u32(body, &depth)) { free(code); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid branch depth"); }
            instr.opcode = byte; instr.u32_imm = depth;
        } else if (byte == 0x0e) {
            uint32_t count;
            if (!er_u32(body, &count) || count > 16) { free(code); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid br_table"); }
            instr.opcode = byte; instr.u32_imm = count;
            for (uint32_t i = 0; i <= count; i++) {
                uint32_t depth;
                if (!er_u32(body, &depth)) { free(code); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid br_table label"); }
                if (i < 16) instr.v128_imm.bytes[i] = (uint8_t)depth;
                else instr.simd_op = depth;
            }
        } else if (byte == 0x10 || (byte >= 0x20 && byte <= 0x24)) {
            uint32_t idx;
            if (!er_u32(body, &idx)) {
                free(code);
                return exec_fail(err, EXEC_ERROR_FORMAT, "truncated instruction immediate");
            }
            instr.opcode  = byte;
            instr.u32_imm = idx;
        } else if (byte == 0x11) {
            uint32_t type_index, table_index;
            if (!er_u32(body,&type_index) || type_index>=eng->type_count ||
                !er_u32(body,&table_index) || table_index>=eng->table_count) {
                free(code); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid call_indirect immediate");
            }
            instr.opcode=byte;instr.u32_imm=type_index;instr.simd_op=table_index;
        } else if (byte >= 0x28 && byte <= 0x3e) {
            uint32_t align, offset;
            if (!er_u32(body, &align) || !er_u32(body, &offset)) {
                free(code); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid memory immediate");
            }
            instr.opcode = byte; instr.simd_op = align; instr.u32_imm = offset;
        } else if (byte == 0x3f || byte == 0x40) {
            uint8_t memory_index;
            if (!er_u8(body, &memory_index) || memory_index != 0) {
                free(code); return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported memory index");
            }
            instr.opcode = byte;
        } else if (byte == 0xd0) {
            int32_t heap_type;
            if (!er_i32(body, &heap_type) || (heap_type != -16 && heap_type != -17)) {
                free(code); return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported ref.null type");
            }
            instr.opcode=byte; instr.u32_imm=(uint32_t)heap_type;
        } else if (byte == 0xd1) {
            instr.opcode=byte;
        } else if (byte == 0xd2) {
            uint32_t function;
            if (!er_u32(body, &function)) { free(code); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid ref.func"); }
            instr.opcode=byte; instr.u32_imm=function;
        } else if (byte == 0x0f || byte == 0x1a || byte == 0x1b) {
            instr.opcode = byte;
        } else if (byte == 0x41) {
            int32_t value;
            if (!er_i32(body, &value)) {
                free(code);
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid i32.const");
            }
            instr.opcode = byte;
            instr.u32_imm = (uint32_t)value;
        } else if (byte == 0x42) {
            int64_t value;
            if (!er_i64(body, &value)) { free(code); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid i64.const"); }
            instr.opcode = byte; memcpy(instr.v128_imm.bytes, &value, 8);
        } else if (byte == 0x43 || byte == 0x44) {
            size_t width = byte == 0x43 ? 4 : 8; const uint8_t *value;
            if (!er_bytes(body, width, &value)) { free(code); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid float const"); }
            instr.opcode = byte; memcpy(instr.v128_imm.bytes, value, width);
        } else if ((byte >= 0x45 && byte <= 0x4f) ||
                   (byte >= 0x67 && byte <= 0x78) ||
                   byte == 0xc0 || byte == 0xc1) {
            instr.opcode = byte;
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
        exec_status st = parse_body(eng, &eng->funcs[i], &body, err);
        if (st != EXEC_OK) return st;
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
            default:
                /* skip unknown sections */
                break;
        }
        if (st != EXEC_OK) {
            exec_free(eng);
            return st;
        }
    }

    if (eng->has_start) {
        wasm_value start_results[WAST_MAX_RESULTS];
        int start_result_count = 0;
        exec_error start_err;
        memset(&start_err, 0, sizeof(start_err));
        exec_status st2 = exec_invoke(eng, eng->start_func, NULL, 0,
                                      start_results, &start_result_count, &start_err);
        if (st2 != EXEC_OK) {
            if (err) *err = start_err;
            exec_free(eng);
            return st2;
        }
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
    for (uint32_t i = 0; i < eng->func_count; i++)
        free(eng->funcs[i].code);
    free(eng->types);
    free(eng->funcs);
    free(eng->exports);
    if (eng->owns_memory) free(eng->owned_memory.data);
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
    if(status==EXEC_OK) *memory=eng->memory;
    return status;
}
exec_status exec_find_export_table(const waste_exec_engine *eng, const char *name,
                                   exec_table **table, exec_error *err) {
    uint32_t index; if(!table) return exec_fail(err,EXEC_ERROR_FORMAT,"null argument");
    exec_status status=find_extern_export(eng,name,1,&index,err);
    if(status==EXEC_OK) *table=eng->tables[index];
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

static exec_status memory_address(waste_exec_engine *eng, uint32_t base, uint32_t offset,
                                  uint32_t width, uint32_t *address, exec_error *err) {
    uint64_t effective = (uint64_t)base + offset;
    uint64_t size = eng->memory ? (uint64_t)eng->memory->pages * EXEC_PAGE_SIZE : 0;
    if (!eng->memory || effective + width > size)
        return exec_fail(err, EXEC_ERROR_TRAP, "out of bounds memory access");
    *address = (uint32_t)effective;
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

    /* Set up locals from args */
    wasm_value locals[EXEC_MAX_LOCALS];
    memset(locals, 0, sizeof(locals));
    if ((uint32_t)arg_count + func->local_count > EXEC_MAX_LOCALS)
        return exec_fail(err, EXEC_ERROR_TRAP, "too many runtime locals");
    for (int i = 0; i < arg_count && i < EXEC_MAX_LOCALS; i++)
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

        if (instr->opcode == 0x00)
            return exec_fail(err, EXEC_ERROR_TRAP, "unreachable");
        if (instr->opcode == 0x01) continue;

        if (instr->opcode == 0x02 || instr->opcode == 0x03 || instr->opcode == 0x04) {
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

        if (instr->opcode == 0x0c || instr->opcode == 0x0d || instr->opcode == 0x0e) {
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
                depth = instr->v128_imm.bytes[selected];
            }
            if (depth >= (uint32_t)control_top)
                return exec_fail(err, EXEC_ERROR_TRAP, "branch depth out of range");
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
                if (!stack_pop(&stack, &value) || value.type != eng->globals[instr->u32_imm]->value.type)
                    return exec_fail(err, EXEC_ERROR_TRAP, "global value mismatch");
                eng->globals[instr->u32_imm]->value = value;
            }
            continue;
        }

        if (instr->opcode >= 0x28 && instr->opcode <= 0x35) {
            wasm_value base, value; uint32_t width, address; int sign = 0;
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
            exec_status status = memory_address(eng, (uint32_t)base.i32, instr->u32_imm, width, &address, err);
            if (status != EXEC_OK) return status;
            uint64_t bits = load_le(eng->memory->data, address, width);
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
            exec_status status = memory_address(eng, (uint32_t)base.i32, instr->u32_imm, width, &address, err);
            if (status != EXEC_OK) return status;
            store_le(eng->memory->data, address, bits, width);
            continue;
        }

        if (instr->opcode == 0x3f) {
            if (!stack_push(&stack, i32_value(eng->memory ? eng->memory->pages : 0))) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }
        if (instr->opcode == 0x40) {
            wasm_value delta;
            if (!stack_pop(&stack, &delta) || delta.type != WASM_VALTYPE_I32) return exec_fail(err, EXEC_ERROR_TRAP, "memory.grow operand missing");
            if (!eng->memory) return exec_fail(err,EXEC_ERROR_TRAP,"memory missing");
            uint32_t old = eng->memory->pages, add = (uint32_t)delta.i32;
            uint64_t pages = (uint64_t)old + add;
            if (pages > 65536u || (eng->memory->has_max && pages > eng->memory->max_pages)) {
                if (!stack_push(&stack, i32_value(UINT32_MAX))) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
                continue;
            }
            size_t new_size = (size_t)pages * EXEC_PAGE_SIZE;
            uint8_t *grown = (uint8_t *)calloc(new_size ? new_size : 1, 1);
            if (!grown) { if (!stack_push(&stack, i32_value(UINT32_MAX))) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow"); continue; }
            memcpy(grown,eng->memory->data,(size_t)old*EXEC_PAGE_SIZE); free(eng->memory->data);
            eng->memory->data=grown; eng->memory->pages=(uint32_t)pages;
            if (!stack_push(&stack, i32_value(old))) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }
        if (instr->opcode == 0xd0 || instr->opcode == 0xd2) {
            wasm_value value; memset(&value,0,sizeof(value));
            value.type = instr->opcode == 0xd0 && (int32_t)instr->u32_imm == -17 ?
                WASM_VALTYPE_EXTERNREF : WASM_VALTYPE_FUNCREF;
            value.ref = instr->opcode == 0xd0 ? UINT32_MAX : instr->u32_imm;
            if (instr->opcode == 0xd2 && instr->u32_imm >= eng->import_func_count + eng->func_count)
                return exec_fail(err, EXEC_ERROR_TRAP, "ref.func index out of range");
            if (!stack_push(&stack,value)) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }
        if (instr->opcode == 0xd1) {
            wasm_value value;
            if (!stack_pop(&stack,&value) || (value.type != WASM_VALTYPE_FUNCREF && value.type != WASM_VALTYPE_EXTERNREF))
                return exec_fail(err, EXEC_ERROR_TRAP, "ref.is_null operand missing");
            if (!stack_push(&stack,i32_value(value.ref == UINT32_MAX))) return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
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
            if (first.type != second.type)
                return exec_fail(err, EXEC_ERROR_TRAP, "select type mismatch");
            if (!stack_push(&stack, condition.i32 ? first : second))
                return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
            continue;
        }

        if (instr->opcode == 0x10) {
            if (instr->u32_imm >= eng->import_func_count + eng->func_count)
                return exec_fail(err, EXEC_ERROR_TRAP, "call target out of range");
            exec_func_type *callee_type = instr->u32_imm < eng->import_func_count ?
                &eng->types[eng->import_func_types[instr->u32_imm]] :
                &eng->types[eng->funcs[instr->u32_imm-eng->import_func_count].type_index];
            wasm_value call_args[8], call_results[WAST_MAX_RESULTS]; int call_result_count = 0;
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

        if (instr->opcode == 0x11) {
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
            exec_func_type *expected=&eng->types[instr->u32_imm];
            exec_func_type *actual=target<teng->import_func_count?
                &teng->types[teng->import_func_types[target]]:
                &teng->types[teng->funcs[target-teng->import_func_count].type_index];
            if(expected->param_count!=actual->param_count||expected->result_count!=actual->result_count)
                return exec_fail(err,EXEC_ERROR_TRAP,"indirect call type mismatch");
            for(int i=0;i<expected->param_count;i++)
                if(!same_value_type(eng,expected->params[i],teng,actual->params[i],0))
                    return exec_fail(err,EXEC_ERROR_TRAP,"indirect call type mismatch");
            for(int i=0;i<expected->result_count;i++)
                if(!same_value_type(eng,expected->results[i],teng,actual->results[i],0))
                    return exec_fail(err,EXEC_ERROR_TRAP,"indirect call type mismatch");
            wasm_value call_args[8],call_results[WAST_MAX_RESULTS];int call_result_count=0;
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

        if ((instr->opcode >= 0x45 && instr->opcode <= 0x4f) ||
            (instr->opcode >= 0x67 && instr->opcode <= 0x78) ||
            instr->opcode == 0xc0 || instr->opcode == 0xc1) {
            exec_status numeric = exec_i32_numeric(instr->opcode, &stack, err);
            if (numeric != EXEC_OK) return numeric;
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
