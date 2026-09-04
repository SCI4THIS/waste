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

#define LINK_MAX_MODULES 64
#define LINK_MAX_IMPORTS 512
typedef struct {
    waste_exec_engine *engine;
    char id[WAST_MAX_EXPORT_NAME];
    char registered[WAST_MAX_EXPORT_NAME];
} linked_module;
typedef struct { waste_exec_engine *engine; uint32_t func_idx; } linked_func;
typedef struct { const uint8_t *p, *end; } bin_reader;
typedef struct { char module[WAST_MAX_EXPORT_NAME], name[WAST_MAX_EXPORT_NAME]; uint8_t kind; } import_request;
static linked_module g_modules[LINK_MAX_MODULES];
static linked_func g_linked_funcs[LINK_MAX_IMPORTS];
static import_request g_import_requests[LINK_MAX_IMPORTS];
static exec_host_import g_func_imports[LINK_MAX_IMPORTS];
static exec_global_import g_global_imports[LINK_MAX_IMPORTS];
static exec_memory_import g_memory_imports[LINK_MAX_IMPORTS];
static exec_table_import g_table_imports[LINK_MAX_IMPORTS];
static uint32_t g_module_count, g_linked_func_count, g_current_module;

static void set_error(const char *message) {
    size_t i = 0;
    memset(&g_error, 0, sizeof(g_error));
    while (message[i] && i + 1 < sizeof(g_error.message)) {
        g_error.message[i] = message[i]; i++;
    }
}
static int read_u8(bin_reader *r, uint8_t *v) { if (r->p >= r->end) return 0; *v=*r->p++; return 1; }
static int read_leb(bin_reader *r, uint32_t *v) {
    uint32_t out=0; int shift=0; uint8_t b;
    do { if(shift>=35||!read_u8(r,&b))return 0; out|=(uint32_t)(b&0x7f)<<shift; shift+=7; } while(b&0x80);
    *v=out; return 1;
}
static int read_name(bin_reader *r, char *out) {
    uint32_t n; if(!read_leb(r,&n)||n>=WAST_MAX_EXPORT_NAME||(size_t)(r->end-r->p)<n)return 0;
    memcpy(out,r->p,n);out[n]='\0';r->p+=n;return 1;
}
static int skip_limits(bin_reader *r) {
    uint32_t flags, value; if(!read_leb(r,&flags)||!read_leb(r,&value))return 0;
    if(flags&1u) return read_leb(r,&value); return 1;
}
static int skip_valtype(bin_reader *r) {
    uint8_t type, byte;
    if(!read_u8(r,&type))return 0;
    if(type!=0x63&&type!=0x64)return 1;
    do { if(!read_u8(r,&byte))return 0; } while(byte&0x80);
    return 1;
}
static int scan_imports(const uint8_t *bytes,size_t size,import_request *req,uint32_t *count) {
    bin_reader r={bytes,bytes+size}; uint32_t section_size,n;
    if(size<8){return 0;} r.p+=8;
    while(r.p<r.end){uint8_t id;if(!read_u8(&r,&id)||!read_leb(&r,&section_size)||(size_t)(r.end-r.p)<section_size)return 0;
        bin_reader s={r.p,r.p+section_size};r.p+=section_size;if(id!=2)continue;
        if(!read_leb(&s,&n)||n>LINK_MAX_IMPORTS)return 0;
        for(uint32_t i=0;i<n;i++){uint8_t kind;uint32_t ignored;if(*count>=LINK_MAX_IMPORTS||!read_name(&s,req[*count].module)||!read_name(&s,req[*count].name)||!read_u8(&s,&kind))return 0;
            req[*count].kind=kind;(*count)++;
            if(kind==0){if(!read_leb(&s,&ignored))return 0;}
            else if(kind==1){if(!skip_valtype(&s)||!skip_limits(&s))return 0;}
            else if(kind==2){if(!skip_limits(&s))return 0;}
            else if(kind==3){if(!skip_valtype(&s)||!read_u8(&s,&kind))return 0;}
            else return 0;
        }
        return s.p==s.end;
    }
    return 1;
}
static linked_module *registered_module(const char *name) {
    for(uint32_t i=g_module_count;i>0;i--)if(strcmp(g_modules[i-1].registered,name)==0)return &g_modules[i-1];
    return (void *)0;
}
static exec_status linked_call(void *data,const wasm_value *args,int argc,wasm_value *results,int *result_count,exec_error *error) {
    linked_func *f=(linked_func *)data;return exec_invoke(f->engine,f->func_idx,args,argc,results,result_count,error);
}
static exec_status spectest_noop(void *data,const wasm_value *args,int argc,
                                 wasm_value *results,int *result_count,
                                 exec_error *error) {
    (void)data;(void)args;(void)argc;(void)results;(void)error;*result_count=0;return EXEC_OK;
}

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

__attribute__((export_name("waste_wast_reset")))
void waste_wast_reset(void) {
    for(uint32_t i=g_module_count;i>0;i--)exec_free(g_modules[i-1].engine);
    memset(g_modules,0,sizeof(g_modules));g_module_count=0;g_linked_func_count=0;g_engine=(void *)0;
}

__attribute__((export_name("waste_wast_load_linked_module")))
uint32_t waste_wast_load_linked_module(uint32_t ptr,uint32_t size,uint32_t id_ptr,uint32_t id_len) {
    uint32_t count=0,nf=0,ng=0,nm=0,nt=0,linked_start=g_linked_func_count;exec_imports imports;waste_exec_engine *engine=(void *)0;
    if(g_module_count>=LINK_MAX_MODULES||!scan_imports((const uint8_t *)(uintptr_t)ptr,size,g_import_requests,&count)){set_error("invalid module import section");return EXEC_ERROR_FORMAT;}
    for(uint32_t i=0;i<count;i++){import_request *req=&g_import_requests[i];linked_module *provider=registered_module(req->module);exec_status st;
        if(!provider&&req->kind==0&&strcmp(req->module,"spectest")==0){
            g_func_imports[nf]=(exec_host_import){req->module,req->name,spectest_noop,(void *)0,(void *)0,0,0};nf++;continue;
        }
        if(!provider){set_error("unresolved registered module import");return EXEC_ERROR_NOT_FOUND;}
        memset(&g_error,0,sizeof(g_error));
        if(req->kind==0){uint32_t idx,type_index;if(g_linked_func_count>=LINK_MAX_IMPORTS)return EXEC_ERROR_FORMAT;st=exec_find_export(provider->engine,req->name,&idx,&g_error);if(st!=EXEC_OK)return st;
            st=exec_get_func_type_index(provider->engine,idx,&type_index,&g_error);if(st!=EXEC_OK)return st;
            linked_func *f=&g_linked_funcs[g_linked_func_count++];f->engine=provider->engine;f->func_idx=idx;g_func_imports[nf]=(exec_host_import){req->module,req->name,linked_call,f,provider->engine,type_index,1};nf++;}
        else if(req->kind==1){exec_table *v;st=exec_find_export_table(provider->engine,req->name,&v,&g_error);if(st!=EXEC_OK)return st;g_table_imports[nt++]=(exec_table_import){req->module,req->name,v};}
        else if(req->kind==2){exec_memory *v;st=exec_find_export_memory(provider->engine,req->name,&v,&g_error);if(st!=EXEC_OK)return st;g_memory_imports[nm++]=(exec_memory_import){req->module,req->name,v};}
        else {exec_global *v;st=exec_find_export_global(provider->engine,req->name,&v,&g_error);if(st!=EXEC_OK)return st;g_global_imports[ng++]=(exec_global_import){req->module,req->name,v};}
    }
    imports=(exec_imports){g_func_imports,nf,g_global_imports,ng,g_memory_imports,nm,g_table_imports,nt};memset(&g_error,0,sizeof(g_error));
    exec_status st=exec_load_with_imports((const uint8_t *)(uintptr_t)ptr,size,&imports,&engine,&g_error);if(st!=EXEC_OK){g_linked_func_count=linked_start;return st;}
    linked_module *m=&g_modules[g_module_count];m->engine=engine;if(id_len>=WAST_MAX_EXPORT_NAME)id_len=WAST_MAX_EXPORT_NAME-1;
    memcpy(m->id,(const void *)(uintptr_t)id_ptr,id_len);m->id[id_len]='\0';g_current_module=g_module_count++;g_engine=engine;return EXEC_OK;
}

__attribute__((export_name("waste_wast_register_current")))
uint32_t waste_wast_register_current(uint32_t ptr,uint32_t len) {
    if(!g_module_count)return EXEC_ERROR_FORMAT;if(len>=WAST_MAX_EXPORT_NAME)len=WAST_MAX_EXPORT_NAME-1;
    linked_module *m=&g_modules[g_current_module];memcpy(m->registered,(const void *)(uintptr_t)ptr,len);m->registered[len]='\0';return EXEC_OK;
}

__attribute__((export_name("waste_wast_select_module")))
uint32_t waste_wast_select_module(uint32_t ptr,uint32_t len) {
    char id[WAST_MAX_EXPORT_NAME];if(len>=WAST_MAX_EXPORT_NAME)len=WAST_MAX_EXPORT_NAME-1;memcpy(id,(const void *)(uintptr_t)ptr,len);id[len]='\0';
    if(len==0&&g_module_count){g_current_module=g_module_count-1;g_engine=g_modules[g_current_module].engine;return EXEC_OK;}
    for(uint32_t i=g_module_count;i>0;i--)if(strcmp(g_modules[i-1].id,id)==0){g_current_module=i-1;g_engine=g_modules[i-1].engine;return EXEC_OK;}
    set_error("unknown module id");return EXEC_ERROR_NOT_FOUND;
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
        case WASM_VALTYPE_FUNCREF:
        case WASM_VALTYPE_EXTERNREF:
        case WASM_VALTYPE_FUNCREF_NONNULL:
        case WASM_VALTYPE_EXTERNREF_NONNULL: memcpy(&v->ref, flat + 1, 4); break;
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
        case WASM_VALTYPE_FUNCREF:
        case WASM_VALTYPE_EXTERNREF:
        case WASM_VALTYPE_FUNCREF_NONNULL:
        case WASM_VALTYPE_EXTERNREF_NONNULL: {
            uint32_t expected; memcpy(&expected, exp_data, 4);
            return actual->ref == expected;
        }
    }
    return 0;
}

__attribute__((export_name("waste_wast_assert_global")))
uint32_t waste_wast_assert_global(uint32_t name_ptr, uint32_t name_len,
                                  uint32_t alts_ptr, uint32_t alt_count,
                                  uint32_t result_count) {
    char name[WAST_MAX_EXPORT_NAME];
    exec_global *global = (void *)0;
    if (!g_engine) { set_error("no module loaded"); return 0; }
    if (name_len >= WAST_MAX_EXPORT_NAME) name_len = WAST_MAX_EXPORT_NAME - 1;
    memcpy(name, (const void *)(uintptr_t)name_ptr, name_len);
    name[name_len] = '\0';
    memset(&g_error, 0, sizeof(g_error));
    if (exec_find_export_global(g_engine, name, &global, &g_error) != EXEC_OK)
        return 0;
    if (result_count != 1) { set_error("global action requires one result"); return 0; }
    const uint8_t *alts = (const uint8_t *)(uintptr_t)alts_ptr;
    for (uint32_t i = 0; i < alt_count; i++)
        if (flat_value_matches(&global->value, alts + i * FLAT_VALUE_SIZE))
            return 1;
    set_error("global result mismatch");
    return 0;
}

__attribute__((export_name("waste_wast_assert_trap")))
uint32_t waste_wast_assert_trap(uint32_t name_ptr, uint32_t name_len,
                                uint32_t args_ptr, uint32_t arg_count) {
    char name[WAST_MAX_EXPORT_NAME];
    uint32_t func_idx;
    wasm_value args[WAST_MAX_ARGS], results[WAST_MAX_RESULTS];
    int result_count = 0;
    if (!g_engine) { set_error("no module loaded"); return 0; }
    if (name_len >= WAST_MAX_EXPORT_NAME) name_len = WAST_MAX_EXPORT_NAME - 1;
    memcpy(name, (const void *)(uintptr_t)name_ptr, name_len); name[name_len] = '\0';
    memset(&g_error, 0, sizeof(g_error));
    if (exec_find_export(g_engine, name, &func_idx, &g_error) != EXEC_OK) return 0;
    uint32_t count = arg_count < WAST_MAX_ARGS ? arg_count : WAST_MAX_ARGS;
    const uint8_t *flat_args = (const uint8_t *)(uintptr_t)args_ptr;
    for (uint32_t i = 0; i < count; i++)
        unpack_flat_value(flat_args + i * FLAT_VALUE_SIZE, &args[i]);
    exec_status status = exec_invoke(g_engine, func_idx, args, (int)count,
                                     results, &result_count, &g_error);
    if (status == EXEC_ERROR_TRAP) { memset(&g_error, 0, sizeof(g_error)); return 1; }
    if (status == EXEC_OK) set_error("expected invocation to trap");
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
    if ((uint32_t)nresults != result_count) {
        const char *msg = "result count mismatch";
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
        for (uint32_t r = 0; r < result_count; r++) {
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
