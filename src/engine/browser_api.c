#include "wast_linker.h"
#include "wast_runner.h"
#include "wast_encode.h"
#include "wast_stream.h"
#include "posix_stubs.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Engine state (existing per-module API) ---- */

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
static int read_leb64(bin_reader *r, uint64_t *v) {
    uint64_t out=0; int shift=0; uint8_t b;
    do {
        if(shift>=70||!read_u8(r,&b))return 0;
        if(shift==63&&(b&0xfeu))return 0;
        out|=(uint64_t)(b&0x7f)<<shift; shift+=7;
    } while(b&0x80);
    *v=out; return 1;
}
static int read_name(bin_reader *r, char *out) {
    uint32_t n; if(!read_leb(r,&n)||n>=WAST_MAX_EXPORT_NAME||(size_t)(r->end-r->p)<n)return 0;
    memcpy(out,r->p,n);out[n]='\0';r->p+=n;return 1;
}
static int skip_limits(bin_reader *r) {
    uint32_t flags; uint64_t value;
    if(!read_leb(r,&flags)||!read_leb64(r,&value))return 0;
    if(flags&1u) return read_leb64(r,&value); return 1;
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

/* ---- Result buffer for waste_wast_run_script ---- */

typedef struct {
    uint8_t pass;
    char func[63];
    char error[192];
} wast_browser_result;

#define MAX_BROWSER_RESULTS 16384
static wast_browser_result g_browser_results[MAX_BROWSER_RESULTS];
static int g_browser_result_count = 0;
static int g_browser_result_passed = 0;

static void add_result(int pass, const char *func, const char *err) {
    if (g_browser_result_count >= MAX_BROWSER_RESULTS) return;
    wast_browser_result *r = &g_browser_results[g_browser_result_count++];
    r->pass = pass ? 1 : 0;
    /* Copy func name (null-terminated, max 62 chars + null) */
    int i = 0;
    if (func) for (; i < 62 && func[i]; i++) r->func[i] = func[i];
    r->func[i] = '\0';
    /* Copy error (null-terminated, max 191 chars + null) */
    i = 0;
    if (err) for (; i < 191 && err[i]; i++) r->error[i] = err[i];
    r->error[i] = '\0';
    if (pass) g_browser_result_passed++;
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
            g_func_imports[nf]=(exec_host_import){req->module,req->name,
                spectest_noop,(void *)0,(void *)0,0,0,
                EXEC_HOST_CONTROL_NONE};nf++;continue;
        }
        if(!provider){set_error("unresolved registered module import");return EXEC_ERROR_NOT_FOUND;}
        memset(&g_error,0,sizeof(g_error));
        if(req->kind==0){uint32_t idx,type_index;if(g_linked_func_count>=LINK_MAX_IMPORTS)return EXEC_ERROR_FORMAT;st=exec_find_export(provider->engine,req->name,&idx,&g_error);if(st!=EXEC_OK)return st;
            st=exec_get_func_type_index(provider->engine,idx,&type_index,&g_error);if(st!=EXEC_OK)return st;
            linked_func *f=&g_linked_funcs[g_linked_func_count++];f->engine=provider->engine;f->func_idx=idx;g_func_imports[nf]=(exec_host_import){req->module,req->name,linked_call,f,provider->engine,type_index,1,EXEC_HOST_CONTROL_NONE};nf++;}
        else if(req->kind==1){exec_table *v;st=exec_find_export_table(provider->engine,req->name,&v,&g_error);if(st!=EXEC_OK)return st;g_table_imports[nt++]=(exec_table_import){req->module,req->name,v};}
        else if(req->kind==2){exec_memory *v;st=exec_find_export_memory(provider->engine,req->name,&v,&g_error);if(st!=EXEC_OK)return st;g_memory_imports[nm++]=(exec_memory_import){req->module,req->name,v};}
        else {exec_global *v;st=exec_find_export_global(provider->engine,req->name,&v,&g_error);if(st!=EXEC_OK)return st;g_global_imports[ng++]=(exec_global_import){req->module,req->name,v};}
    }
    imports=(exec_imports){g_func_imports,nf,g_global_imports,ng,
        g_memory_imports,nm,g_table_imports,nt,(void *)0,0};memset(&g_error,0,sizeof(g_error));
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
        case WASM_VALTYPE_ARRAYREF_NONNULL: memcpy(&v->ref, flat + 1, 4); break;
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
        case WASM_VALTYPE_ARRAYREF_NONNULL: {
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

    char name[WAST_MAX_EXPORT_NAME];
    if (name_len >= WAST_MAX_EXPORT_NAME) name_len = WAST_MAX_EXPORT_NAME - 1;
    memcpy(name, (const void *)(uintptr_t)name_ptr, name_len);
    name[name_len] = '\0';

    uint32_t func_idx = 0;
    memset(&g_error, 0, sizeof(g_error));
    exec_status st = exec_find_export(g_engine, name, &func_idx, &g_error);
    if (st != EXEC_OK) return 0;

    wasm_value args[WAST_MAX_ARGS];
    uint32_t nargs = arg_count < WAST_MAX_ARGS ? arg_count : WAST_MAX_ARGS;
    const uint8_t *flat_args = (const uint8_t *)(uintptr_t)args_ptr;
    for (uint32_t i = 0; i < nargs; i++)
        unpack_flat_value(flat_args + i * FLAT_VALUE_SIZE, &args[i]);

    wasm_value results[WAST_MAX_RESULTS];
    int nresults = 0;
    memset(&g_error, 0, sizeof(g_error));
    st = exec_invoke(g_engine, func_idx, args, (int)nargs,
                     results, &nresults, &g_error);
    if (st != EXEC_OK) return 0;

    if (alt_count == 0 || result_count == 0) return 1;
    if ((uint32_t)nresults != result_count) {
        const char *msg = "result count mismatch";
        int i = 0;
        while (msg[i] && i < 255) { g_error.message[i] = msg[i]; i++; }
        g_error.message[i] = '\0';
        return 0;
    }

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

    {
        const char *prefix = "result mismatch: ";
        int pos = 0;
        for (; *prefix && pos < 254; pos++, prefix++) g_error.message[pos] = *prefix;
        for (int k = 0; name[k] && pos < 255; k++, pos++) g_error.message[pos] = name[k];
        g_error.message[pos] = '\0';
    }
    return 0;
}

/* ---- waste_wast_run_script: run an entire WAST script ---- */

typedef struct {
    native_store store;
    wast_script **retained;
    uint32_t retained_count;
    uint32_t retained_capacity;
    wast_script *current_anonymous_script;
} browser_wast_context;

static uint32_t g_browser_command_line;

static void add_command_failure(const char *name, const char *message) {
    char located[256];
    snprintf(located, sizeof(located), "line %u: %s",
             g_browser_command_line, message ? message : "failure");
    add_result(0, name, located);
}

static void browser_forget_retained(browser_wast_context *context,
                                    wast_script *script) {
    if (!script) return;
    for (uint32_t i = 0; i < context->retained_count; i++) {
        if (context->retained[i] == script) {
            context->retained[i] = (void *)0;
            break;
        }
    }
    wast_script_free(script);
    free(script);
}

static void browser_release_superseded_anonymous(
        browser_wast_context *context) {
    if (!context->current_anonymous_script ||
        context->store.module_count == 0)
        return;
    native_linked_module *current =
        &context->store.modules[context->store.module_count - 1];
    if (current->id[0] || current->registered[0]) {
        context->current_anonymous_script = (void *)0;
        return;
    }
    int imports_table = 0;
    if (current->module) {
        for (int i = 0; i < current->module->table_count; i++)
            imports_table |= current->module->tables[i].is_import != 0;
    }
    if (imports_table) {
        if (!native_store_keep_orphan(&context->store, current->engine))
            return;
    } else {
        exec_free(current->engine);
    }
    memset(current, 0, sizeof(*current));
    context->store.module_count--;
    browser_forget_retained(context,
                            context->current_anonymous_script);
    context->current_anonymous_script = (void *)0;
}

static int browser_retain_script(browser_wast_context *context,
                                 wast_script *parsed) {
    if (parsed->group_count > 0 &&
        parsed->group_capacity != parsed->group_count) {
        wast_group *groups = realloc(
            parsed->groups,
            (size_t)parsed->group_count * sizeof(*parsed->groups));
        if (!groups) return 0;
        parsed->groups = groups;
        parsed->group_capacity = parsed->group_count;
    }
    if (context->retained_count == context->retained_capacity) {
        uint32_t capacity = context->retained_capacity ?
                            context->retained_capacity * 2u : 16u;
        wast_script **retained = realloc(
            context->retained, (size_t)capacity * sizeof(*retained));
        if (!retained) return 0;
        context->retained = retained;
        context->retained_capacity = capacity;
    }
    wast_script *holder = malloc(sizeof(*holder));
    if (!holder) return 0;
    *holder = *parsed;
    memset(parsed, 0, sizeof(*parsed));
    context->retained[context->retained_count++] = holder;
    return 1;
}

static const wast_module *browser_find_definition(
        const browser_wast_context *context, const char *id) {
    for (uint32_t script_index = context->retained_count;
         script_index > 0; script_index--) {
        const wast_script *script = context->retained[script_index - 1];
        if (!script) continue;
        for (int group_index = script->group_count; group_index > 0;
             group_index--) {
            const wast_module *module =
                &script->groups[group_index - 1].module;
            if (module->is_definition && strcmp(module->id, id) == 0)
                return module;
        }
    }
    return (void *)0;
}

/* ---- Yield/resume state for interactive (bash) mode ---- */
static int g_yield_active;
static browser_wast_context g_yield_context;
static wast_stream g_yield_stream;
static waste_exec_engine *g_yield_engine;
static uint32_t g_yield_func_idx;
static wasm_value g_yield_args[WAST_MAX_ARGS];
static int g_yield_arg_count;

static void browser_yield_cleanup(void) {
    native_store_free(&g_yield_context.store);
    for (uint32_t i = 0; i < g_yield_context.retained_count; i++) {
        if (!g_yield_context.retained[i]) continue;
        wast_script_free(g_yield_context.retained[i]);
        free(g_yield_context.retained[i]);
    }
    free(g_yield_context.retained);
    memset(&g_yield_context, 0, sizeof(g_yield_context));
}

static void browser_run_assertions(browser_wast_context *context,
                                   const wast_script *script,
                                   const wast_group *group) {
    for (int i = 0; i < group->assertion_count; i++) {
        const wast_assertion *assertion =
            &script->assertions[group->assertion_start + i];
        waste_exec_engine *selected =
            native_selected_engine(&context->store, assertion->module_id);
        exec_error error;
        memset(&error, 0, sizeof(error));
        exec_status status;
        if (!selected) {
            error.status = EXEC_ERROR_NOT_FOUND;
            snprintf(error.message, sizeof(error.message), "unknown module id");
            status = EXEC_ERROR_NOT_FOUND;
        } else {
            status = wast_run_assertion(selected, assertion, &error);
        }
        if (status == EXEC_YIELD) {
            g_yield_active = 1;
            g_yield_engine = selected;
            exec_find_export(selected, assertion->func_name,
                             &g_yield_func_idx, &error);
            g_yield_arg_count = assertion->arg_count;
            for (int j = 0; j < assertion->arg_count; j++)
                g_yield_args[j] = assertion->args[j];
            return;
        }
        add_result(status == EXEC_OK, assertion->func_name,
                   status == EXEC_OK ? (void *)0 : error.message);
    }
}

static void browser_process_module(browser_wast_context *context,
                                   wast_script *script,
                                   wast_group *group) {
    const wast_module *load_module = &group->module;
    if (group->module.instance_of[0]) {
        load_module = browser_find_definition(context,
                                              group->module.instance_of);
        if (!load_module) {
            add_result(0, "(module)", "unknown module definition");
            return;
        }
    }

    if (group->has_module_assertion && group->has_validation_error) {
        int ok = group->module_assert_kind == WAST_ASSERT_INVALID ||
                 group->module_assert_kind == WAST_ASSERT_MALFORMED;
        add_result(ok, "(module)", ok ? (void *)0 : group->validation_error);
        return;
    }

    char encode_error[256] = {0};
    size_t binary_size = 0;
    wast_group encode_group = *group;
    encode_group.module = *load_module;
    uint8_t *binary = encode_group_module(&encode_group, &binary_size,
                                          encode_error);
    if (!binary) {
        if (group->has_module_assertion) {
            int ok = group->module_assert_kind != WAST_ASSERT_TRAP;
            add_result(ok, "(module)", ok ? (void *)0 : encode_error);
        } else {
            add_command_failure("(module)", encode_error);
        }
        return;
    }

    waste_exec_engine *engine = (void *)0;
    exec_error error;
    memset(&error, 0, sizeof(error));
    exec_status status = native_load_module(&context->store, load_module,
                                            binary, binary_size, &engine,
                                            &error);
    free(binary);
    if (group->has_module_assertion) {
        int ok = group->module_assert_kind == WAST_ASSERT_TRAP ?
                 status == EXEC_ERROR_TRAP : status != EXEC_OK;
        add_result(ok, "(module)", ok ? (void *)0 :
                   (status == EXEC_OK ? "module unexpectedly instantiated" :
                    error.message));
        if (engine && !native_store_keep_orphan(&context->store, engine)) {
        }
        return;
    }
    if (status != EXEC_OK) {
        return;
    }
    if (!native_store_add(&context->store, engine, &group->module,
                          load_module)) {
        exec_free(engine);
        add_command_failure("(module)",
                            "out of memory retaining module instance");
        return;
    }
    browser_run_assertions(context, script, group);
}

static int browser_process_command(wast_stream_command_kind kind,
                                   const char *bytes, size_t length,
                                   size_t offset, unsigned line,
                                   wast_script *parsed, void *opaque) {
    browser_wast_context *context = (browser_wast_context *)opaque;
    (void)bytes;
    (void)length;
    (void)offset;
    g_browser_command_line = line;
    if (parsed->error[0]) {
        add_result(0, "(parse)", parsed->error);
        return 0;
    }
    if (kind == WAST_STREAM_REGISTER) {
        if (parsed->group_count != 1) {
            add_result(0, "(register)", "invalid register command");
            return 0;
        }
        wast_module *registration = &parsed->groups[0].module;
        native_linked_module *target = native_selected_module(
            &context->store, registration->register_target);
        if (!target) {
            add_result(0, "(register)", "unknown module id");
            return 0;
        }
        snprintf(target->registered, sizeof(target->registered), "%s",
                 registration->register_name);
        return 0;
    }
    if ((kind == WAST_STREAM_ASSERTION || kind == WAST_STREAM_INVOKE) &&
        parsed->group_count == 1 &&
        !parsed->groups[0].has_module_assertion) {
        browser_run_assertions(context, parsed, &parsed->groups[0]);
        return 0;
    }
    if (parsed->group_count == 0) return 0;

    if (kind == WAST_STREAM_MODULE &&
        !parsed->groups[0].has_module_assertion &&
        !parsed->groups[0].module.is_definition)
        browser_release_superseded_anonymous(context);

    int must_retain = 0;
    for (int i = 0; i < parsed->group_count; i++)
        if (parsed->groups[i].module.is_definition ||
            (!parsed->groups[i].has_module_assertion &&
             kind == WAST_STREAM_MODULE))
            must_retain = 1;
    if (must_retain && !browser_retain_script(context, parsed)) {
        add_result(0, "(module)", "out of memory retaining WAST command");
        return 0;
    }
    wast_script *script = must_retain ?
        context->retained[context->retained_count - 1] : parsed;
    for (int i = 0; i < script->group_count; i++) {
        wast_group *group = &script->groups[i];
        if (group->module.is_definition) continue;
        browser_process_module(context, script, group);
    }
    if (must_retain && script->group_count == 1 &&
        !script->groups[0].has_module_assertion &&
        !script->groups[0].module.is_definition &&
        !script->groups[0].module.id[0] &&
        !script->groups[0].module.register_name[0] &&
        context->store.module_count > 0 &&
        context->store.modules[context->store.module_count - 1].module ==
            &script->groups[0].module)
        context->current_anonymous_script = script;
    return 0;
}

static int browser_stream_loop(void) {
    for (;;) {
        int status = wast_stream_next(&g_yield_stream,
                                      browser_process_command,
                                      &g_yield_context);
        if (status == 0) break;
        if (status < 0) {
            add_result(0, "(parse)", g_yield_stream.error);
            break;
        }
        if (g_yield_active) return 1;
    }
    browser_yield_cleanup();
    return 0;
}

__attribute__((export_name("waste_wast_run_script")))
uint32_t waste_wast_run_script(uint32_t text_ptr, uint32_t text_len) {
    g_browser_result_count = 0;
    g_browser_result_passed = 0;
    g_browser_command_line = 1;
    /* Reset old per-module API state */
    memset(g_modules, 0, sizeof(g_modules));
    g_module_count = 0;
    g_linked_func_count = 0;
    g_engine = (void *)0;
    g_yield_active = 0;

    memset(&g_yield_context, 0, sizeof(g_yield_context));
    native_store_init(&g_yield_context.store);
    g_yield_context.store.host_resolver = browser_host_resolver;
    g_yield_context.store.host_context = &g_yield_context.store;
    wast_stream_init(&g_yield_stream,
                     (const char *)(uintptr_t)text_ptr, text_len);
    return browser_stream_loop();
}

__attribute__((export_name("waste_wast_resume")))
uint32_t waste_wast_resume(void) {
    if (!g_yield_active) return 0;

    wasm_value results[WAST_MAX_RESULTS];
    int result_count = 0;
    exec_error error;
    memset(&error, 0, sizeof(error));
    exec_status st = exec_invoke(g_yield_engine, g_yield_func_idx,
                                 g_yield_args, g_yield_arg_count,
                                 results, &result_count, &error);
    if (st == EXEC_YIELD) return 1;

    g_yield_active = 0;
    add_result(st == EXEC_OK || st == EXEC_ERROR_EXIT, "main",
               (st == EXEC_OK || st == EXEC_ERROR_EXIT)
                   ? (void *)0 : error.message);
    return browser_stream_loop();
}

/* ---- Result accessor exports ---- */

__attribute__((export_name("waste_wast_results_ptr")))
uint32_t waste_wast_results_ptr(void) {
    return (uint32_t)(uintptr_t)g_browser_results;
}

__attribute__((export_name("waste_wast_results_total")))
uint32_t waste_wast_results_total(void) {
    return (uint32_t)g_browser_result_count;
}

__attribute__((export_name("waste_wast_command_line")))
uint32_t waste_wast_command_line(void) {
    return g_browser_command_line;
}

__attribute__((export_name("waste_wast_results_passed")))
uint32_t waste_wast_results_passed(void) {
    return (uint32_t)g_browser_result_passed;
}
