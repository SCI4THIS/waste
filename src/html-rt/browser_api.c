#include "store.h"
#include "vfs.h"
#include "test_suite.h"
#include "runtime_internal.h"
#include "wat/name.h"
#include "wast/runner.h"
#include "wasm/encode.h"
#include "wast/stream.h"
#include "wast/handler.h"
#include "wast/setup.h"
#include "posix_stubs.h"
#include "process_driver.h"
#include "lib/include/kernel.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

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
    if (!wast_name_from_bytes(r->p, n, out, WAST_MAX_EXPORT_NAME)) return 0;
    r->p+=n;return 1;
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
static exec_status linked_call(void *data,const wasm_value *args,int argc,wasm_value *results,int *result_count,exec_error *error,const waste_exec_engine *caller) {
    linked_func *f=(linked_func *)data;
    waste_exec_engine *provider = exec_clone_resolve(caller, f->engine);
    return exec_invoke(provider,f->func_idx,args,argc,results,result_count,error);
}
static exec_status spectest_noop(void *data,const wasm_value *args,int argc,
                                 wasm_value *results,int *result_count,
                                 exec_error *error,
                                 const waste_exec_engine *caller) {
    (void)data;(void)args;(void)argc;(void)results;(void)error;(void)caller;*result_count=0;return EXEC_OK;
}

/* ---- Result buffer for waste_wast_run_script ---- */

typedef struct {
    uint8_t pass;
    char func[63];
    char error[192];
} wast_browser_result;

#define MAX_BROWSER_RESULTS 16384
static wast_browser_result g_browser_results[MAX_BROWSER_RESULTS];
/* The legacy 256-byte result record keeps its short display name. Separate
 * bounded names let current clients preserve the parser's full UTF-8 identity
 * without changing that record layout for older host-boundary probes. */
static char g_browser_result_names[MAX_BROWSER_RESULTS][WAST_MAX_EXPORT_NAME];
static int g_browser_result_count = 0;
static int g_browser_result_passed = 0;
static wast_setup_report g_browser_setup;
static int g_browser_script_completed;
/* Script result survives store cleanup, just like assertion records. These
 * exports distinguish explicit guest exit from ordinary script completion. */
static int g_browser_guest_exited, g_browser_guest_exit_status;
static uint32_t g_execution_timeout_ms, g_execution_cancel_ms;
static uint64_t g_execution_started_ns, g_execution_last_ns;
static exec_stop_reason g_execution_stop;
/* Cooperative pump: when non-zero, the engine yields after this many
 * wall-clock nanoseconds of guest execution so the worker can drain its
 * event loop and process external cancel messages. */
static uint32_t g_pump_quantum_ms;
static uint64_t browser_pump_clock_now(void *opaque) {
    (void)opaque;
    return waste_browser_realtime_now(NULL);
}
/* Scripted deterministic clocks for host-independent guest tests.  The browser
 * execution-policy clock keeps real time; only the kernel-visible realtime and
 * monotonic callbacks observe the override. */
static int g_clock_realtime_fixed, g_clock_monotonic_fixed;
static uint64_t g_clock_realtime_ns, g_clock_monotonic_ns;

static uint64_t browser_guest_realtime_now(void *data) {
    if (g_clock_realtime_fixed) return g_clock_realtime_ns;
    return waste_browser_realtime_now(data);
}
static uint64_t browser_guest_monotonic_now(void *data) {
    if (g_clock_monotonic_fixed) return g_clock_monotonic_ns;
    return waste_browser_realtime_now(data);
}

static exec_stop_reason browser_execution_poll(void *opaque) {
    (void)opaque;
    uint64_t now = waste_browser_realtime_now(NULL);
    if (now < g_execution_last_ns) now = g_execution_last_ns;
    g_execution_last_ns = now;
    uint64_t elapsed = now - g_execution_started_ns;
    if (g_execution_cancel_ms && (!g_execution_timeout_ms ||
        g_execution_cancel_ms <= g_execution_timeout_ms) &&
        elapsed >= (uint64_t)g_execution_cancel_ms * 1000000u)
        g_execution_stop = EXEC_STOP_CANCELLED;
    else if (g_execution_timeout_ms &&
             elapsed >= (uint64_t)g_execution_timeout_ms * 1000000u)
        g_execution_stop = EXEC_STOP_TIMEOUT;
    return g_execution_stop;
}
static native_process_driver g_shared_process;
#define BROWSER_TRANSITION_EVIDENCE_MAX 2048
static char g_browser_transition_evidence[BROWSER_TRANSITION_EVIDENCE_MAX];
static size_t g_browser_transition_evidence_length;

void waste_browser_record_transition(const char *event) {
    size_t event_length;
    size_t required;
    if (!event) return;
    event_length = strlen(event);
    required = event_length +
               (g_browser_transition_evidence_length != 0 ? 1u : 0u);
    if (required >= BROWSER_TRANSITION_EVIDENCE_MAX)
        event_length = BROWSER_TRANSITION_EVIDENCE_MAX - 2;
    required = event_length +
               (g_browser_transition_evidence_length != 0 ? 1u : 0u);
    if (g_browser_transition_evidence_length + required >=
        BROWSER_TRANSITION_EVIDENCE_MAX) {
        size_t keep = BROWSER_TRANSITION_EVIDENCE_MAX / 2;
        if (g_browser_transition_evidence_length > keep) {
            memmove(g_browser_transition_evidence,
                    g_browser_transition_evidence +
                        g_browser_transition_evidence_length - keep,
                    keep);
            g_browser_transition_evidence_length = keep;
        } else {
            g_browser_transition_evidence_length = 0;
        }
    }
    if (event_length == 0) return;
    if (g_browser_transition_evidence_length != 0) {
        g_browser_transition_evidence[
            g_browser_transition_evidence_length++] = ',';
    }
    memcpy(g_browser_transition_evidence +
               g_browser_transition_evidence_length,
           event, event_length);
    g_browser_transition_evidence_length += event_length;
    g_browser_transition_evidence[g_browser_transition_evidence_length] = '\0';
}

static void browser_record_engine_transition(const char *label,
                                             waste_exec_engine *engine) {
    char event[64];
    snprintf(event, sizeof(event), "%s-f%u-i%u", label,
             engine ? engine->func_count : 0,
             engine ? engine->import_func_count : 0);
    waste_browser_record_transition(event);
}

static void add_result(int pass, const char *func, const char *err) {
    if (g_browser_result_count >= MAX_BROWSER_RESULTS) return;
    int index = g_browser_result_count++;
    wast_browser_result *r = &g_browser_results[index];
    snprintf(g_browser_result_names[index], WAST_MAX_EXPORT_NAME, "%s", func ? func : "");
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

__attribute__((export_name("waste_wast_free")))
void waste_wast_free(uint32_t ptr) {
    free((void *)(uintptr_t)ptr);
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
        g_memory_imports,nm,g_table_imports,nt,(void *)0,0,NULL};memset(&g_error,0,sizeof(g_error));
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
    if (name_len >= WAST_MAX_EXPORT_NAME) {
        set_error("export name exceeds supported representation");
        return -1;
    }
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
    if (name_len >= WAST_MAX_EXPORT_NAME) {
        set_error("export name exceeds supported representation");
        return 0;
    }
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
    if (name_len >= WAST_MAX_EXPORT_NAME) {
        set_error("export name exceeds supported representation");
        return 0;
    }
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
    if (name_len >= WAST_MAX_EXPORT_NAME) {
        set_error("export name exceeds supported representation");
        return 0;
    }
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
    wast_process_handler handler;
} browser_wast_context;

static uint32_t g_browser_command_line;
static int g_shared_file_page_probe_requested;
static int g_shared_file_page_probe_result;
int32_t waste_wast_shared_file_page_probe(void);

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
    int process_owned = current->engine == g_shared_process.parent_engine;
    native_process_capsule *active_capsule =
        native_store_active_capsule(&context->store);
    process_owned = process_owned ||
                    (active_capsule && active_capsule->engine == current->engine);
    if (current->module) {
        for (int i = 0; i < current->module->table_count; i++)
            imports_table |= current->module->tables[i].is_import != 0;
    }
    if (imports_table || process_owned) {
        if (!native_store_keep_orphan(&context->store, current->engine))
            return;
    } else {
        browser_record_engine_transition("release-current", current->engine);
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
static exec_yield_reason g_yield_reason;
/* Own the parsed expectation across ordinary return-based evaluator yields.
 * The stream releases its temporary command immediately after the callback. */
static int g_assertion_pending;
static wast_assertion g_pending_assertion;
static waste_exec_engine *g_pending_assertion_engine;
/* The process driver may switch from the suspended Bash child to a newly
 * committed executable image before exposing a terminal/select yield. */
static int g_terminal_requested;
static int g_test_suite_requested;

static uint8_t *g_boot_executable;
static size_t g_boot_executable_size;
static waste_vfs g_boot_vfs;

/* Boot-time packaged VFS manifest.  The browser submits only bounded path
 * metadata; file contents and executable bytes remain separate staged images
 * until the VFS data plane is added in Stage 7. */
/* Keep enough room for the launcher directories, the complete packaged
 * Coreutils wave (including /bin aliases), evidence fixtures, and subsequent
 * package growth.  The full offline page stages these entries together; the
 * focused utility harnesses stage only a subset and therefore did not expose
 * the former 24-entry ceiling. */
#define BROWSER_VFS_MANIFEST_MAX 64
typedef struct {
    char path[POSIX_PATH_NODE_NAME_MAX];
    posix_path_metadata metadata;
    uint8_t *data;
    size_t data_length;
    char *link_target;
} browser_vfs_manifest_entry;
static browser_vfs_manifest_entry g_vfs_manifest[BROWSER_VFS_MANIFEST_MAX];
static uint32_t g_vfs_manifest_count;
static int g_build_mtime_valid;
static int64_t g_build_mtime_sec;
static int64_t g_build_mtime_nsec;
/* Optional initial cwd selected by the browser launcher before the process
 * store is created.  Keep this as bounded host-side state so the path remains
 * valid after the staging allocation is released. */
static char g_initial_cwd[POSIX_PATH_NODE_NAME_MAX];

/* The browser-facing scheduler owns one explicit driver record.  The store
 * remains authoritative for process state; this record only identifies the
 * capsule currently being driven and the reason JavaScript must resume it. */
typedef enum {
    BROWSER_DRIVER_IDLE = 0,
    BROWSER_DRIVER_RUNNING,
    BROWSER_DRIVER_BROWSER_WAIT,
    BROWSER_DRIVER_DONE,
    BROWSER_DRIVER_ERROR
} browser_driver_state;

typedef struct {
    browser_driver_state state;
    int pid;
    waste_exec_engine *engine;
    uint32_t func_idx;
    wasm_value args[WAST_MAX_ARGS];
    int arg_count;
    exec_yield_reason wait_reason;
} browser_process_driver;

static browser_process_driver g_process_driver;

static void browser_driver_reset(void) {
    memset(&g_process_driver, 0, sizeof(g_process_driver));
    g_process_driver.state = BROWSER_DRIVER_IDLE;
}

static void browser_driver_publish_wait(void) {
    g_process_driver.state = BROWSER_DRIVER_BROWSER_WAIT;
    g_process_driver.wait_reason = g_yield_reason;
    g_yield_engine = g_process_driver.engine;
    g_yield_func_idx = g_process_driver.func_idx;
    g_yield_arg_count = g_process_driver.arg_count;
    for (int i = 0; i < g_yield_arg_count; i++)
        g_yield_args[i] = g_process_driver.args[i];
}

static void browser_process_state_init(void) {
    if (g_shared_process.initialized) return;
    native_process_driver_init(&g_shared_process);
    browser_driver_reset();
}
static void browser_process_state_reset(void) {
    native_process_driver_destroy(&g_shared_process);
    browser_driver_reset();
}

static void browser_yield_cleanup(void) {
    g_assertion_pending = 0;
    g_pending_assertion_engine = NULL;
    browser_process_state_reset();
    wast_process_handler_reset(&g_yield_context.handler);
    wast_stream_destroy(&g_yield_stream);
    native_store_free(&g_yield_context.store);
    wast_process_handler_destroy(&g_yield_context.handler);
    for (uint32_t i = 0; i < g_yield_context.retained_count; i++) {
        if (!g_yield_context.retained[i]) continue;
        wast_script_free(g_yield_context.retained[i]);
        free(g_yield_context.retained[i]);
    }
    free(g_yield_context.retained);
    memset(&g_yield_context, 0, sizeof(g_yield_context));
}

static void browser_handler_result(void *opaque, int passed,
                                   const char *name, const char *message) {
    (void)opaque;
    add_result(passed, name, message);
}
static void browser_driver_trace(void *opaque, const char *event) {
    (void)opaque;
    waste_browser_record_transition(event);
}
static exec_status browser_driver_handler_step(const uint8_t *source, size_t size,
    size_t offset, unsigned line, size_t *next_offset, unsigned *next_line, void *opaque) {
    wast_process_handler *handler = opaque;
    exec_status status = wast_process_handler_step(source, size, offset, line,
                                                   next_offset, next_line, handler);
    g_shared_process.handler_wait_reason = handler->wait_reason;
    return status;
}
static exec_status browser_invoke_process(browser_wast_context *context,
    waste_exec_engine *engine, uint32_t function, const wasm_value *args, int count,
    wasm_value *results, int *result_count, exec_error *error) {
    browser_process_state_init();
    g_shared_process.handler_step = browser_driver_handler_step;
    g_shared_process.handler_reset = wast_process_handler_reset;
    g_shared_process.handler_context = &context->handler;
    g_shared_process.trace = browser_driver_trace;
    exec_status status = native_process_driver_invoke(&g_shared_process, &context->store,
        engine, function, args, count, results, result_count, error);
    if (status == EXEC_ERROR_EXIT) {
        g_browser_guest_exited = 1;
        g_browser_guest_exit_status = error->exit_code;
    }
    native_driver_selection *selected = &g_shared_process.selection;
    g_process_driver.pid = selected->pid;
    g_process_driver.engine = selected->engine;
    g_process_driver.func_idx = selected->func_idx;
    g_process_driver.arg_count = selected->arg_count;
    memcpy(g_process_driver.args, selected->args, sizeof(selected->args));
    g_process_driver.wait_reason = selected->wait_reason;
    return status;
}

static exec_status browser_assertion_invoke(void *data,
                                            waste_exec_engine *engine,
                                            uint32_t func_idx,
                                            const wasm_value *args, int arg_count,
                                            wasm_value *results, int *result_count,
                                            exec_error *error) {
    return browser_invoke_process((browser_wast_context *)data, engine,
                                   func_idx, args, arg_count, results,
                                   result_count, error);
}

static exec_status browser_assertion_resume(void *data,
                                             waste_exec_engine *engine,
                                             uint32_t func_idx,
                                             const wasm_value *args, int arg_count,
                                             wasm_value *results, int *result_count,
                                             exec_error *error) {
    /* The expected value belongs to the original assertion, but fork/exec or
     * a child WAST handler may have selected a different live continuation. */
    (void)engine; (void)func_idx; (void)args; (void)arg_count;
    return browser_invoke_process((browser_wast_context *)data, g_yield_engine,
        g_yield_func_idx, g_yield_args, g_yield_arg_count, results, result_count, error);
}

static void browser_run_assertions(browser_wast_context *context,
                                   const wast_script *script,
                                   const wast_group *group) {
    /* Executable manifests are immutable store state; refresh the active
     * process namespace after terminal/kernel replacement or fork cloning. */
    (void)native_store_bind_executable_paths(&context->store);
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
            status = wast_run_assertion_with_invoke(
                selected, assertion, &error, browser_assertion_invoke, context);
        }
        if (status == EXEC_YIELD) {
            g_pending_assertion = *assertion;
            g_pending_assertion_engine = selected;
            g_assertion_pending = 1;
            g_yield_active = 1;
            g_yield_reason = error.yield_reason;
            if (!g_shared_process.active_engine) {
                g_process_driver.engine = selected;
                g_process_driver.func_idx = 0;
                g_process_driver.arg_count = assertion->arg_count;
                exec_find_export(selected, assertion->func_name,
                                 &g_process_driver.func_idx, &error);
                for (int j = 0; j < assertion->arg_count; j++)
                    g_process_driver.args[j] = assertion->args[j];
            }
            g_process_driver.pid = native_store_getpid(&context->store);
            /* Pump yields are time-slice boundaries, not guest I/O.  Publish
             * the saved engine/args for resume; the worker distinguishes
             * via waste_wast_wait_kind == EXEC_YIELD_PUMP and drives the
             * cooperative drain itself. */
            browser_driver_publish_wait();
            return;
        }
        add_result(status == EXEC_OK, assertion->func_name,
                   status == EXEC_OK ? (void *)0 : error.message);
        if (status == EXEC_ERROR_INTERRUPTED) return;
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
            wast_setup_record(&g_browser_setup, g_browser_command_line,
                EXEC_ERROR_NOT_FOUND, WAST_SETUP_DEFINITION, "unknown module definition");
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
            wast_setup_record(&g_browser_setup, g_browser_command_line,
                EXEC_ERROR_FORMAT, WAST_SETUP_ENCODE, encode_error);
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
    if (status == EXEC_ERROR_INTERRUPTED) {
        if (!group->has_module_assertion)
            wast_setup_record(&g_browser_setup, g_browser_command_line,
                status, WAST_SETUP_LOAD, error.message);
        add_result(0, "(module)", error.message);
        if (engine && !native_store_keep_orphan(&context->store, engine))
            exec_free(engine);
        return;
    }
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
        wast_setup_record(&g_browser_setup, g_browser_command_line,
            status, WAST_SETUP_LOAD, error.message);
        if (engine && !native_store_keep_orphan(&context->store, engine)) {
            exec_free(engine);
            add_command_failure("(module)", "cannot retain failed setup engine");
        }
        return;
    }
    if (!native_store_add(&context->store, engine, &group->module,
                          load_module)) {
        wast_setup_record(&g_browser_setup, g_browser_command_line,
            EXEC_ERROR_TRAP, WAST_SETUP_RETAIN, "cannot retain module instance");
        exec_free(engine);
        add_command_failure("(module)",
                            "out of memory retaining module instance");
        return;
    }
    wast_setup_record(&g_browser_setup, g_browser_command_line,
                     EXEC_OK, WAST_SETUP_LOAD, NULL);
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
    int modules_before = context->store.module_count;
    for (int i = 0; i < script->group_count; i++) {
        wast_group *group = &script->groups[i];
        if (group->module.is_definition) continue;
        browser_process_module(context, script, group);
    }
    /* Failed ordinary modules have no published metadata. Release their parse
     * after iteration, never while the loop still reads script->group_count. */
    if (must_retain && script->group_count == 1 &&
        !script->groups[0].module.is_definition &&
        context->store.module_count == modules_before) {
        browser_forget_retained(context, script);
        return 0;
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
    if (g_yield_context.store.execution_control.stopped) {
        browser_yield_cleanup();
        return 0;
    }
    for (;;) {
        exec_error error = {0};
        if (exec_execution_check(&g_yield_context.store.execution_control, &error) != EXEC_OK) {
            add_result(0, "(execution)", error.message);
            break;
        }
        int status = wast_stream_next(&g_yield_stream,
                                      browser_process_command,
                                      &g_yield_context);
        if (status == 0) { g_browser_script_completed = 1; break; }
        if (status < 0) {
            add_result(0, "(parse)", g_yield_stream.error);
            break;
        }
        if (g_yield_active) return 1;
        if (g_yield_context.store.execution_control.stopped) break;
    }
    browser_yield_cleanup();
    return 0;
}

__attribute__((export_name("waste_wast_run_script")))
uint32_t waste_wast_run_script(uint32_t text_ptr, uint32_t text_len) {
    browser_process_state_reset();
    g_browser_result_count = 0;
    g_browser_result_passed = 0;
    wast_setup_reset(&g_browser_setup);
    g_browser_script_completed = 0;
    g_browser_guest_exited = 0;
    g_browser_guest_exit_status = 0;
    g_execution_stop = EXEC_STOP_NONE;
    g_execution_started_ns = g_execution_last_ns = waste_browser_realtime_now(NULL);
    g_shared_file_page_probe_result = 0;
    g_browser_command_line = 1;
    /* Reset old per-module API state */
    memset(g_modules, 0, sizeof(g_modules));
    g_module_count = 0;
    g_linked_func_count = 0;
    g_engine = (void *)0;
    g_yield_active = 0;
    g_yield_reason = EXEC_YIELD_NONE;
    g_assertion_pending = 0;
    g_pending_assertion_engine = NULL;
    g_browser_transition_evidence_length = 0;
    g_browser_transition_evidence[0] = '\0';
    waste_browser_record_transition("run-start");

    memset(&g_yield_context, 0, sizeof(g_yield_context));
    native_store_init(&g_yield_context.store);
    g_yield_context.store.test_suite_enabled = g_test_suite_requested;
    g_test_suite_requested = 0;
    if (g_execution_timeout_ms || g_execution_cancel_ms)
        g_yield_context.store.execution_control.poll = browser_execution_poll;
    if (g_pump_quantum_ms) {
        /* Dispatch-loop safepoint only runs when poll != NULL, so install a
         * no-op poll when only pump is configured.  browser_execution_poll
         * already returns EXEC_STOP_NONE until the timeout/cancel windows
         * engage; it is safe to reuse. */
        if (!g_yield_context.store.execution_control.poll)
            g_yield_context.store.execution_control.poll = browser_execution_poll;
        g_yield_context.store.execution_control.pump_quantum_ns =
            (uint64_t)g_pump_quantum_ms * 1000000ull;
        g_yield_context.store.execution_control.pump_clock_now =
            browser_pump_clock_now;
        g_yield_context.store.execution_control.pump_clock_context = NULL;
        g_yield_context.store.execution_control.last_pump_ns = 0;
    }
    wast_process_handler_init(&g_yield_context.handler, &g_yield_context.store,
                               browser_handler_result, NULL);
    if (g_boot_executable) {
        (void)native_store_register_executable(
            &g_yield_context.store, "/bin/waste-probe", g_boot_executable,
            g_boot_executable_size, 0755u, 1u, "_start");
        free(g_boot_executable);
        g_boot_executable = NULL;
        g_boot_executable_size = 0;
    }
    if (g_terminal_requested) {
        native_store_enable_terminal(&g_yield_context.store);
        g_terminal_requested = 0;
    }
    posix_kernel_set_realtime_clock(g_yield_context.store.kernel,
                                    browser_guest_realtime_now, NULL);
    posix_kernel_set_clock(g_yield_context.store.kernel,
                           browser_guest_monotonic_now, NULL);
    (void)native_store_bind_interpreter_paths(&g_yield_context.store);
    if (g_boot_vfs.count) {
        int rc = waste_vfs_mount(g_yield_context.store.kernel, &g_boot_vfs);
        waste_vfs_free(&g_boot_vfs);
        if (rc) {
            add_result(0, "(vfs)", "cannot mount installed VFS files");
            browser_yield_cleanup();
            return 0;
        }
    }
    /* Terminal creation replaces the initial noninteractive kernel, so bind
     * packaged metadata only after that optional replacement. */
    for (uint32_t i = 0; i < g_vfs_manifest_count; i++) {
        int install_status;
        if (g_vfs_manifest[i].metadata.kind == POSIX_NODE_SYMLINK)
            install_status = posix_kernel_path_add_symlink(
                g_yield_context.store.kernel, g_vfs_manifest[i].path,
                &g_vfs_manifest[i].metadata, g_vfs_manifest[i].link_target);
        else
            install_status = posix_kernel_path_add_data(
                g_yield_context.store.kernel, g_vfs_manifest[i].path,
                &g_vfs_manifest[i].metadata, g_vfs_manifest[i].data,
                g_vfs_manifest[i].data_length);
        if (install_status != 0) {
            char error[192];
            snprintf(error, sizeof(error),
                     "cannot install packaged VFS path %s: %d",
                     g_vfs_manifest[i].path, install_status);
            add_result(0, "(vfs)", error);
        }
        free(g_vfs_manifest[i].data);
        free(g_vfs_manifest[i].link_target);
        g_vfs_manifest[i].data = NULL;
        g_vfs_manifest[i].link_target = NULL;
    }
    g_vfs_manifest_count = 0;
    if (g_build_mtime_valid) {
        static const char *const engine_paths[] = {
            "/", "/bin", "/usr", "/usr/bin", "/bin/wat", "/bin/wast"
        };
        for (size_t i = 0; i < sizeof(engine_paths) / sizeof(engine_paths[0]);
             i++) {
            const char *path = engine_paths[i];
            (void)posix_kernel_path_set_mtime(
                g_yield_context.store.kernel, (const uint8_t *)path,
                strlen(path), g_build_mtime_sec, g_build_mtime_nsec);
        }
        g_build_mtime_valid = 0;
    }
    if (g_initial_cwd[0]) {
        (void)posix_kernel_path_set_cwd(g_yield_context.store.kernel,
                                        g_initial_cwd);
        g_initial_cwd[0] = '\0';
    }
    if (g_shared_file_page_probe_requested) {
        g_shared_file_page_probe_result =
            waste_wast_shared_file_page_probe();
        g_shared_file_page_probe_requested = 0;
    }
    g_yield_context.store.host_resolver = browser_host_resolver;
    g_yield_context.store.host_context = &g_yield_context.store;
    wast_stream_init(&g_yield_stream,
                     (const char *)(uintptr_t)text_ptr, text_len);
    return browser_stream_loop();
}

__attribute__((export_name("waste_wast_resume")))
uint32_t waste_wast_resume(void) {
    exec_yield_reason stored_reason = EXEC_YIELD_NONE;
    char resume_event[96];
    snprintf(resume_event, sizeof(resume_event), "resume-entry-a%d-p%d-d%d",
             g_yield_active, native_store_getpid(&g_yield_context.store),
             g_process_driver.pid);
    waste_browser_record_transition(resume_event);
    if (!g_yield_active) {
        waste_browser_record_transition("resume-no-yield");
        return 0;
    }

    /* Resume the exact capsule that published the browser wait.  Internal
     * fork/exec transitions never come through this entry point. */
    if (g_process_driver.pid > 0 &&
        native_store_set_active_process(&g_yield_context.store,
                                        g_process_driver.pid) != 0) {
        waste_browser_record_transition("resume-setpid-fail");
        g_process_driver.state = BROWSER_DRIVER_ERROR;
        g_yield_active = 0;
        add_result(0, "main", "browser process capsule is no longer runnable");
        browser_stream_loop();
        return 0;
    }
    if (native_store_process_handler_wait_reason(
            &g_yield_context.store, &stored_reason) == 0 &&
        stored_reason != EXEC_YIELD_NONE) {
        if (native_store_resume_process_handler(&g_yield_context.store) != 0) {
            g_process_driver.state = BROWSER_DRIVER_ERROR;
            g_yield_active = 0;
            add_result(0, "main", "browser WAST handler could not resume");
            browser_stream_loop();
            return 0;
        }
        g_yield_reason = stored_reason;
        g_yield_active = 0;
        g_yield_reason = EXEC_YIELD_NONE;
    }

    wasm_value results[WAST_MAX_RESULTS];
    int result_count = 0;
    exec_error error;
    memset(&error, 0, sizeof(error));
    exec_status st = g_assertion_pending ?
        wast_run_assertion_with_invoke(g_pending_assertion_engine,
            &g_pending_assertion, &error, browser_assertion_resume, &g_yield_context) :
        browser_invoke_process(&g_yield_context, g_yield_engine,
            g_yield_func_idx, g_yield_args, g_yield_arg_count, results,
            &result_count, &error);
    snprintf(resume_event, sizeof(resume_event), "resume-status-s%d-y%d",
             st, error.yield_reason);
    waste_browser_record_transition(resume_event);
    if (st == EXEC_YIELD) {
        /* Handler execution clears this marker while it drives a command.
         * A resumed Bash parent can yield again for terminal input, so the
         * next browser resume must remain armed instead of returning early. */
        g_yield_active = 1;
        g_yield_reason = error.yield_reason;
        /* browser_invoke_process already copied the exact shared selection.
         * Pump yields reuse the same save/restore path; the worker detects
         * them via waste_wast_wait_kind == EXEC_YIELD_PUMP (6) and drains
         * its event loop (which may invoke waste_wast_request_cancel)
         * before calling resume again.  Handler waits intentionally have
         * no engine entry descriptor. */
        browser_driver_publish_wait();
        return 1;
    }

    g_yield_active = 0;
    g_yield_reason = EXEC_YIELD_NONE;
    g_process_driver.state = st == EXEC_OK || st == EXEC_ERROR_EXIT ?
                             BROWSER_DRIVER_DONE : BROWSER_DRIVER_ERROR;
    int passed = st == EXEC_OK || (!g_assertion_pending && st == EXEC_ERROR_EXIT);
    const char *result_name = g_assertion_pending ? g_pending_assertion.func_name : "main";
    g_assertion_pending = 0;
    g_pending_assertion_engine = NULL;
    add_result(passed, result_name, passed ? (void *)0 : error.message);
    return browser_stream_loop();
}

__attribute__((export_name("waste_wast_wait_kind")))
uint32_t waste_wast_wait_kind(void) {
    exec_yield_reason stored_reason = EXEC_YIELD_NONE;
    if (native_store_process_handler_wait_reason(
            &g_yield_context.store, &stored_reason) == 0 &&
        stored_reason != EXEC_YIELD_NONE)
        return g_yield_active ? (uint32_t)stored_reason : EXEC_YIELD_NONE;
    return g_yield_active ? (uint32_t)g_yield_reason : EXEC_YIELD_NONE;
}

__attribute__((export_name("waste_wast_set_execution_limits")))
int32_t waste_wast_set_execution_limits(uint32_t timeout_ms, uint32_t cancel_ms) {
    if (g_yield_active || timeout_ms > 3600000 || cancel_ms > 3600000) return -1;
    g_execution_timeout_ms = timeout_ms;
    g_execution_cancel_ms = cancel_ms;
    return 0;
}

__attribute__((export_name("waste_wast_execution_stop_reason")))
uint32_t waste_wast_execution_stop_reason(void) {
    return (uint32_t)g_execution_stop;
}

/* Configure the cooperative dispatch-pump quantum (wall-clock milliseconds).
 * The engine yields EXEC_YIELD_PUMP (resume returns 2) after this many ms of
 * guest execution so the worker can process pending onmessage events.  Zero
 * disables the pump.  Must be called before run_script; rejected while a
 * script is active. */
__attribute__((export_name("waste_wast_set_pump_quantum_ms")))
int32_t waste_wast_set_pump_quantum_ms(uint32_t quantum_ms) {
    if (g_yield_active || quantum_ms > 60000) return -1;
    g_pump_quantum_ms = quantum_ms;
    return 0;
}

/* Request immediate cancellation of the running script.  Unlike the scheduled
 * `waste_wast_set_execution_limits` cancel, this is callable from a worker
 * onmessage handler during a pump yield; the next dispatch-loop safepoint
 * then returns EXEC_ERROR_INTERRUPTED with EXEC_STOP_CANCELLED. */
__attribute__((export_name("waste_wast_request_cancel")))
void waste_wast_request_cancel(void) {
    g_execution_stop = EXEC_STOP_CANCELLED;
    g_yield_context.store.execution_control.stopped = EXEC_STOP_CANCELLED;
}

/* Freeze the kernel-visible realtime/monotonic clock at a scripted value for
 * host-independent guest tests.  Host-side execution-policy time keeps real
 * elapsed-time semantics.  Pass low == 0 && high == 0 as a reset sentinel.
 * Must be called before run_script; rejected while a script is active. */
__attribute__((export_name("waste_wast_set_clock_realtime_ns")))
int32_t waste_wast_set_clock_realtime_ns(uint32_t low, uint32_t high) {
    if (g_yield_active) return -1;
    uint64_t value = ((uint64_t)high << 32) | (uint64_t)low;
    g_clock_realtime_fixed = value != 0;
    g_clock_realtime_ns = value;
    return 0;
}

__attribute__((export_name("waste_wast_set_clock_monotonic_ns")))
int32_t waste_wast_set_clock_monotonic_ns(uint32_t low, uint32_t high) {
    if (g_yield_active) return -1;
    uint64_t value = ((uint64_t)high << 32) | (uint64_t)low;
    g_clock_monotonic_fixed = value != 0;
    g_clock_monotonic_ns = value;
    return 0;
}

/* Advance an explicitly frozen guest clock at an external wait safe point.
 * This never changes the host clock used for execution/cancellation limits. */
__attribute__((export_name("waste_wast_advance_clock_monotonic_ns")))
int32_t waste_wast_advance_clock_monotonic_ns(uint32_t low, uint32_t high) {
    uint64_t value = ((uint64_t)high << 32) | low;
    uint32_t reason = waste_wast_wait_kind();
    if (!g_yield_active || !g_clock_monotonic_fixed ||
        (reason != EXEC_YIELD_READ && reason != EXEC_YIELD_SELECT) ||
        value < g_clock_monotonic_ns) return -1;
    g_clock_monotonic_ns = value;
    return 0;
}

__attribute__((export_name("waste_wast_guest_exited")))
uint32_t waste_wast_guest_exited(void) { return (uint32_t)g_browser_guest_exited; }

__attribute__((export_name("waste_wast_guest_exit_status")))
int32_t waste_wast_guest_exit_status(void) { return g_browser_guest_exit_status; }

__attribute__((export_name("waste_wast_process_id")))
int32_t waste_wast_process_id(void) { return native_store_getpid(&g_yield_context.store); }

/* ---- Host I/O query/response exports for upload/download ---- */

__attribute__((export_name("waste_wast_host_io_kind")))
uint32_t waste_wast_host_io_kind(void) {
    return (uint32_t)g_yield_context.store.host_io.kind;
}

__attribute__((export_name("waste_wast_host_io_path_ptr")))
uint32_t waste_wast_host_io_path_ptr(void) {
    return (uint32_t)(uintptr_t)g_yield_context.store.host_io.path;
}

__attribute__((export_name("waste_wast_host_io_path_len")))
uint32_t waste_wast_host_io_path_len(void) {
    return (uint32_t)g_yield_context.store.host_io.path_len;
}

__attribute__((export_name("waste_wast_host_io_data_ptr")))
uint32_t waste_wast_host_io_data_ptr(void) {
    return (uint32_t)(uintptr_t)g_yield_context.store.host_io.data;
}

__attribute__((export_name("waste_wast_host_io_data_len")))
uint32_t waste_wast_host_io_data_len(void) {
    return (uint32_t)g_yield_context.store.host_io.data_len;
}

__attribute__((export_name("waste_wast_host_io_provide_upload")))
int32_t waste_wast_host_io_provide_upload(uint32_t ptr, uint32_t len) {
    native_host_io_state *host_io = &g_yield_context.store.host_io;
    if (host_io->kind != NATIVE_HOST_IO_UPLOAD || host_io->result != 0)
        return -1;
    uint8_t *copy = (uint8_t *)malloc(len);
    if (!copy) return -1;
    memcpy(copy, (const void *)(uintptr_t)ptr, len);
    free(host_io->data);
    host_io->data = copy;
    host_io->data_len = len;
    host_io->result = 1;
    return 0;
}

__attribute__((export_name("waste_wast_host_io_verbose")))
uint32_t waste_wast_host_io_verbose(void) {
    return (uint32_t)g_yield_context.store.host_io.verbose;
}

__attribute__((export_name("waste_wast_host_io_cancel")))
int32_t waste_wast_host_io_cancel(void) {
    g_yield_context.store.host_io.result = -1;
    return 0;
}

__attribute__((export_name("waste_wast_host_io_complete")))
int32_t waste_wast_host_io_complete(void) {
    g_yield_context.store.host_io.result = 1;
    return 0;
}

/* Host service is enabled only by the shell runtime, never batch workers. */
__attribute__((export_name("waste_wast_enable_test_suite")))
int32_t waste_wast_enable_test_suite(void) {
    if (g_yield_active) return -1;
    g_test_suite_requested = 1;
    return 0;
}

__attribute__((export_name("waste_wast_test_suite_reply")))
int32_t waste_wast_test_suite_reply(uint32_t ptr, uint32_t len) {
    native_host_io_state *io = &g_yield_context.store.host_io;
    if (io->kind != NATIVE_HOST_IO_TEST_SUITE || io->result || len < 16 || len > 16u*1024u*1024u)
        return -1;
    uint8_t *copy = malloc(len);
    if (!copy) return -1;
    memcpy(copy, (const void *)(uintptr_t)ptr, len);
    free(io->data); io->data = copy; io->data_len = len; io->result = 1;
    return 0;
}

__attribute__((export_name("waste_wast_enable_terminal")))
int32_t waste_wast_enable_terminal(void) {
    g_terminal_requested = 1;
    return 0;
}


/* Inventory metadata and extracted file bytes remain separate. The shared
 * validator accepts a complete set before a fresh kernel can be mounted. */
__attribute__((export_name("waste_wast_stage_vfs_inventory")))
int32_t waste_wast_stage_vfs_inventory(uint32_t ptr, uint32_t size) {
    if (g_yield_active) return -1;
    return waste_vfs_inventory((const char *)(uintptr_t)ptr, size, &g_boot_vfs);
}

__attribute__((export_name("waste_wast_stage_vfs_file")))
int32_t waste_wast_stage_vfs_file(uint32_t index, uint32_t ptr, uint32_t size) {
    if (g_yield_active) return -1;
    return waste_vfs_set_file(&g_boot_vfs, index, (const uint8_t *)(uintptr_t)ptr, size);
}

__attribute__((export_name("waste_wast_vfs_ready")))
int32_t waste_wast_vfs_ready(void) { return waste_vfs_ready(&g_boot_vfs) ? 0 : -1; }

__attribute__((export_name("waste_wast_stage_executable")))
int32_t waste_wast_stage_executable(uint32_t ptr, uint32_t size) {
    uint8_t *copy;
    if (size == 0 || size > NATIVE_EXEC_BYTES_MAX) return -POSIX_EINVAL;
    copy = (uint8_t *)malloc(size);
    if (!copy) return -POSIX_ENOMEM;
    memcpy(copy, (const void *)(uintptr_t)ptr, size);
    free(g_boot_executable);
    g_boot_executable = copy;
    g_boot_executable_size = size;
    return 0;
}

__attribute__((export_name("waste_wast_stage_path")))
int32_t waste_wast_stage_path(uint32_t ptr, uint32_t length,
                              uint32_t kind, uint32_t mode, uint32_t size) {
    browser_vfs_manifest_entry *entry;
    if (!length || length >= POSIX_PATH_NODE_NAME_MAX ||
        g_vfs_manifest_count >= BROWSER_VFS_MANIFEST_MAX ||
        kind == POSIX_NODE_NONE || kind >= POSIX_NODE_SYMLINK)
        return -POSIX_EINVAL;
    entry = &g_vfs_manifest[g_vfs_manifest_count];
    memcpy(entry->path, (const void *)(uintptr_t)ptr, length);
    entry->path[length] = '\0';
    entry->metadata.kind = kind;
    entry->metadata.mode = mode;
    entry->metadata.uid = 0;
    entry->metadata.gid = 0;
    entry->metadata.size = size;
    entry->metadata.inode = 1000u + g_vfs_manifest_count;
    g_vfs_manifest_count++;
    return 0;
}

/* Apply source metadata to the most recently staged entry without widening
   the established staging calls. Seconds use an explicit little-endian pair
   so JavaScript never has to pass an i64/BigInt across this API. */
__attribute__((export_name("waste_wast_stage_mtime")))
int32_t waste_wast_stage_mtime(uint32_t seconds_low, uint32_t seconds_high,
                               uint32_t nanoseconds) {
    if (g_vfs_manifest_count == 0 || nanoseconds >= 1000000000u)
        return -POSIX_EINVAL;
    browser_vfs_manifest_entry *entry =
        &g_vfs_manifest[g_vfs_manifest_count - 1u];
    entry->metadata.mtime_sec = (int64_t)(
        (uint64_t)seconds_low | ((uint64_t)seconds_high << 32));
    entry->metadata.mtime_nsec = (int64_t)nanoseconds;
    return 0;
}

/* Record the timestamp of the engine image used to construct virtual nodes
   that do not have a corresponding packaged source file. */
__attribute__((export_name("waste_wast_stage_build_mtime")))
int32_t waste_wast_stage_build_mtime(uint32_t seconds_low,
                                     uint32_t seconds_high,
                                     uint32_t nanoseconds) {
    if (nanoseconds >= 1000000000u) return -POSIX_EINVAL;
    g_build_mtime_sec = (int64_t)(
        (uint64_t)seconds_low | ((uint64_t)seconds_high << 32));
    g_build_mtime_nsec = (int64_t)nanoseconds;
    g_build_mtime_valid = 1;
    return 0;
}

__attribute__((export_name("waste_wast_stage_symlink")))
int32_t waste_wast_stage_symlink(uint32_t path_ptr, uint32_t path_length,
                                 uint32_t target_ptr, uint32_t target_length,
                                 uint32_t mode) {
    browser_vfs_manifest_entry *entry;
    if (!path_length || path_length >= POSIX_PATH_NODE_NAME_MAX ||
        !target_length || target_length >= POSIX_PATH_NODE_NAME_MAX ||
        g_vfs_manifest_count >= BROWSER_VFS_MANIFEST_MAX)
        return -POSIX_EINVAL;
    entry = &g_vfs_manifest[g_vfs_manifest_count];
    entry->link_target = malloc(target_length + 1u);
    if (!entry->link_target) return -POSIX_ENOMEM;
    memcpy(entry->path, (const void *)(uintptr_t)path_ptr, path_length);
    entry->path[path_length] = '\0';
    memcpy(entry->link_target, (const void *)(uintptr_t)target_ptr,
           target_length);
    entry->link_target[target_length] = '\0';
    entry->metadata = (posix_path_metadata){ POSIX_NODE_SYMLINK, mode,
        0, 0, (int64_t)target_length, 1000u + g_vfs_manifest_count, 0, 0 };
    g_vfs_manifest_count++;
    return 0;
}

__attribute__((export_name("waste_wast_stage_cwd")))
int32_t waste_wast_stage_cwd(uint32_t ptr, uint32_t length) {
    if (!ptr || !length || length >= sizeof(g_initial_cwd) ||
        ((const char *)(uintptr_t)ptr)[0] != '/')
        return -POSIX_EINVAL;
    memcpy(g_initial_cwd, (const void *)(uintptr_t)ptr, length);
    g_initial_cwd[length] = '\0';
    return 0;
}

__attribute__((export_name("waste_wast_stage_file")))
int32_t waste_wast_stage_file(uint32_t path_ptr, uint32_t path_length,
                              uint32_t data_ptr, uint32_t data_length,
                              uint32_t mode) {
    if (!path_length || path_length >= POSIX_PATH_NODE_NAME_MAX ||
        data_length > NATIVE_EXEC_BYTES_MAX ||
        g_vfs_manifest_count >= BROWSER_VFS_MANIFEST_MAX)
        return -POSIX_EINVAL;
    browser_vfs_manifest_entry *entry = &g_vfs_manifest[g_vfs_manifest_count];
    entry->data = data_length ? malloc(data_length) : NULL;
    if (data_length && !entry->data) return -POSIX_ENOMEM;
    memcpy(entry->path, (const void *)(uintptr_t)path_ptr, path_length);
    entry->path[path_length] = '\0';
    if (data_length) memcpy(entry->data, (const void *)(uintptr_t)data_ptr, data_length);
    entry->data_length = data_length;
    entry->metadata = (posix_path_metadata){ POSIX_NODE_REGULAR, mode,
        0, 0, (int64_t)data_length, 1000u + g_vfs_manifest_count, 0, 0 };
    g_vfs_manifest_count++;
    return 0;
}

/* Mounted batch inputs. Each batch worker owns one instance and executes one
 * test; the shell instance is never touched. The catalogue kernel is temporary,
 * and execution remounts the same validated installed files in run_script. */
static waste_suite g_suite;
static char g_suite_error[256];
static uint32_t g_suite_source_length;

static uint8_t *browser_suite_read(posix_kernel *kernel, const char *path,
                                   size_t *length) {
    int fd = posix_kernel_open(kernel, (const uint8_t *)path, strlen(path), 0, 0);
    uint64_t size = 0;
    uint8_t *bytes = NULL;
    if (fd >= 0 && !posix_kernel_file_size(kernel, fd, &size) &&
        size <= WASTE_SUITE_MAX_BYTES) {
        bytes = malloc((size_t)size + 1u);
        if (bytes && posix_kernel_read(kernel, fd, bytes, (int)size) != (int)size) {
            free(bytes);
            bytes = NULL;
        }
    }
    if (fd >= 0) posix_kernel_close(kernel, fd);
    if (bytes) { bytes[size] = 0; *length = (size_t)size; }
    return bytes;
}

__attribute__((export_name("waste_wast_suite_error_ptr")))
uint32_t waste_wast_suite_error_ptr(void) {
    return (uint32_t)(uintptr_t)g_suite_error;
}

__attribute__((export_name("waste_wast_suite_load")))
int32_t waste_wast_suite_load(void) {
    if (g_yield_active || !waste_vfs_ready(&g_boot_vfs)) {
        snprintf(g_suite_error, sizeof(g_suite_error), "batch requires an idle instance and installed VFS");
        return -1;
    }
    waste_suite_free(&g_suite);
    g_suite_error[0] = 0;
    native_store catalogue;
    native_store_init(&catalogue);
    size_t length = 0;
    uint8_t *bytes = NULL;
    int result = -1;
    if (catalogue.kernel && !waste_vfs_mount(catalogue.kernel, &g_boot_vfs))
        bytes = browser_suite_read(catalogue.kernel, "/tests/manifest.json", &length);
    if (!bytes)
        snprintf(g_suite_error, sizeof(g_suite_error), "cannot read mounted /tests/manifest.json");
    else if (!waste_suite_decode((const char *)bytes, length, &g_suite,
                                  g_suite_error, sizeof(g_suite_error)))
        result = (int)g_suite.count;
    free(bytes);
    native_store_free(&catalogue);
    return result;
}

/* Strings are borrowed until the next load. Fields: identity, group, mounted
 * path, reporting filename, execution mode, skip reason. */
__attribute__((export_name("waste_wast_suite_field")))
uint32_t waste_wast_suite_field(uint32_t index, uint32_t field) {
    if (index >= g_suite.count) return 0;
    const waste_suite_test *test = &g_suite.tests[index];
    const char *value;
    switch (field) {
    case 0: value = test->identity; break;
    case 1: value = test->group; break;
    case 2: value = test->path; break;
    case 3: value = test->file; break;
    case 4: value = test->mode; break;
    case 5: value = test->skip_reason; break;
    default: return 0;
    }
    return (uint32_t)(uintptr_t)value;
}

__attribute__((export_name("waste_wast_suite_expected_failure")))
uint32_t waste_wast_suite_expected_failure(uint32_t index) {
    return index < g_suite.count && g_suite.tests[index].expect_failure;
}

__attribute__((export_name("waste_wast_suite_source_length")))
uint32_t waste_wast_suite_source_length(void) { return g_suite_source_length; }

/* Returns owned source bytes (release with waste_wast_free), or zero. Assets
 * are read from mounted support paths and copied through ordinary staging. */
__attribute__((export_name("waste_wast_suite_prepare")))
uint32_t waste_wast_suite_prepare(uint32_t index) {
    g_suite_source_length = 0;
    if (g_yield_active || !waste_vfs_ready(&g_boot_vfs) || index >= g_suite.count ||
        g_suite.tests[index].skip_reason[0]) {
        snprintf(g_suite_error, sizeof(g_suite_error), "invalid batch execution request");
        return 0;
    }
    const waste_suite_test *test = &g_suite.tests[index];
    native_store catalogue;
    native_store_init(&catalogue);
    uint8_t *source = NULL;
    size_t length;
    if (!catalogue.kernel || waste_vfs_mount(catalogue.kernel, &g_boot_vfs)) {
        snprintf(g_suite_error, sizeof(g_suite_error), "cannot mount batch input files");
        goto finish;
    }
    for (unsigned i = 0; i < test->asset_count; i++) {
        const waste_suite_asset *asset = &test->assets[i];
        uint8_t *bytes = browser_suite_read(catalogue.kernel, asset->path, &length);
        int ok = bytes && !waste_wast_stage_file(
            (uint32_t)(uintptr_t)asset->mount_path, (uint32_t)strlen(asset->mount_path),
            (uint32_t)(uintptr_t)bytes, (uint32_t)length, asset->mode);
        free(bytes);
        if (!ok) {
            snprintf(g_suite_error, sizeof(g_suite_error), "cannot stage mounted companion: %s", asset->path);
            goto finish;
        }
    }
    source = browser_suite_read(catalogue.kernel, test->path, &length);
    if (source) g_suite_source_length = (uint32_t)length;
    else snprintf(g_suite_error, sizeof(g_suite_error), "cannot read mounted WAST: %s", test->path);
finish:
    native_store_free(&catalogue);
    return (uint32_t)(uintptr_t)source;
}

__attribute__((export_name("waste_wast_path_access")))
int32_t waste_wast_path_access(uint32_t ptr, uint32_t length, uint32_t mode) {
    if (!g_yield_context.store.kernel || length >= POSIX_PATH_MAX)
        return -POSIX_EINVAL;
    return posix_kernel_path_access(g_yield_context.store.kernel,
                                    (const uint8_t *)(uintptr_t)ptr, length,
                                    (int)mode, 0);
}

__attribute__((export_name("waste_wast_resize_terminal")))
int32_t waste_wast_resize_terminal(uint32_t columns, uint32_t rows) {
    if (!g_yield_context.store.kernel_terminal || columns == 0 || rows == 0 ||
        columns > UINT16_MAX || rows > UINT16_MAX)
        return -POSIX_EINVAL;
    posix_winsize size = {(uint16_t)rows, (uint16_t)columns, 0, 0};
    return posix_kernel_terminal_set_winsize(g_yield_context.store.kernel, 0,
                                             &size);
}

__attribute__((export_name("waste_wast_enqueue_input")))
int32_t waste_wast_enqueue_input(uint32_t ptr, uint32_t length) {
    int32_t result = -POSIX_EINVAL;
    if (g_yield_context.store.kernel_terminal && length <= INT32_MAX)
        result = posix_kernel_terminal_enqueue(
            g_yield_context.store.kernel, 0,
            (const uint8_t *)(uintptr_t)ptr, (int)length);
    free((void *)(uintptr_t)ptr);
    if (result == 0)
        waste_browser_record_transition("input-enqueue-ok");
    else
        waste_browser_record_transition("input-enqueue-fail");
    return result;
}

__attribute__((export_name("waste_wast_enqueue_eof")))
int32_t waste_wast_enqueue_eof(void) {
    if (!g_yield_context.store.kernel_terminal) return -POSIX_EINVAL;
    return posix_kernel_terminal_signal_eof(g_yield_context.store.kernel, 0);
}

__attribute__((export_name("waste_wast_raise_signal")))
int32_t waste_wast_raise_signal(uint32_t signal) {
    if (!g_yield_context.store.kernel_terminal) return -POSIX_EINVAL;
    return posix_kernel_signal_raise(g_yield_context.store.kernel,
                                     (int)signal);
}

/* Route a signal to a specific PID in the shared store.  pid == 0 falls back
 * to the active-kernel queue (identical to waste_wast_raise_signal) so one
 * worker/export entry point covers both backgrounded and active targets.
 * Store errors (unknown/zombie process, invalid signal) stay visible. */
__attribute__((export_name("waste_wast_raise_signal_pid")))
int32_t waste_wast_raise_signal_pid(uint32_t signal, uint32_t pid) {
    if (!g_yield_context.store.kernel_terminal) return -POSIX_EINVAL;
    if (!pid)
        return posix_kernel_signal_raise(g_yield_context.store.kernel,
                                         (int)signal);
    return native_store_signal_process(&g_yield_context.store,
                                       (int)pid, (int)signal);
}

/* Fan a signal across every live member of a process group in the shared
 * store.  pgid must be positive; a zero pgid is rejected so callers never
 * conflate "deliver to active" (use waste_wast_raise_signal) with the group
 * fan-out code path.  Returns the number of delivered members (0 for an
 * empty group) or a negative POSIX errno. */
__attribute__((export_name("waste_wast_raise_signal_pgid")))
int32_t waste_wast_raise_signal_pgid(uint32_t signal, uint32_t pgid) {
    if (!g_yield_context.store.kernel_terminal) return -POSIX_EINVAL;
    if (!pgid) return -POSIX_EINVAL;
    return native_store_signal_process_group(&g_yield_context.store,
                                              (int)pgid, (int)signal);
}

/* Browser evidence probe for the store-owned MAP_SHARED page cache.  This is
 * intentionally a narrow engine test: both memories are independent objects,
 * while the file page is acquired from the real browser store cache. */
__attribute__((export_name("waste_wast_shared_file_page_probe")))
int32_t waste_wast_shared_file_page_probe(void) {
    static const uint8_t path[] = "/vm-f-shared-page-probe";
    static const uint8_t initial[] = "cache";
    posix_path_metadata metadata = {
        POSIX_NODE_REGULAR, 0666, 0, 0, 1, 9101, 0, 0
    };
    native_store *store = &g_yield_context.store;
    exec_memory shared_memory;
    exec_memory observer_memory;
    exec_memory private_memory;
    exec_memory_page *cached_page = NULL;
    posix_file_object *file_object = NULL;
    int writable = 0;
    int fd = -1;
    uint8_t observed = 0;
    uint8_t value = 0x91;
    int result = -POSIX_EINVAL;
    memset(&shared_memory, 0, sizeof(shared_memory));
    memset(&observer_memory, 0, sizeof(observer_memory));
    memset(&private_memory, 0, sizeof(private_memory));
    if (!store->kernel ||
        posix_kernel_path_add_data(store->kernel, (const char *)path,
                                    &metadata, initial, sizeof(initial) - 1) != 0)
        return -POSIX_EIO;
    fd = posix_kernel_open(store->kernel, path, sizeof(path) - 1,
                           POSIX_O_RDWR, 0);
    if (fd < 0 || posix_kernel_file_retain(store->kernel, fd, &file_object,
                                            &writable) != 0 || !writable)
        goto done;
    shared_memory.max_pages = 2;
    observer_memory.max_pages = 2;
    private_memory.max_pages = 2;
    if (exec_memory_resize_pages(&shared_memory, 1, NULL) != EXEC_OK ||
        exec_memory_resize_pages(&observer_memory, 1, NULL) != EXEC_OK ||
        exec_memory_resize_pages(&private_memory, 1, NULL) != EXEC_OK ||
        native_store_shared_file_page(store, file_object, 0, &cached_page) != 0 ||
        exec_memory_bind_shared_page(&shared_memory, 0, cached_page, NULL) != EXEC_OK ||
        exec_memory_bind_shared_page(&observer_memory, 0, cached_page, NULL) != EXEC_OK ||
        exec_memory_write(&shared_memory, 0, &value, 1, NULL) != EXEC_OK ||
        exec_memory_read(&observer_memory, 0, &observed, 1, NULL) != EXEC_OK ||
        observed != value ||
        exec_memory_write(&private_memory, 0, cached_page->bytes, 1, NULL) != EXEC_OK ||
        exec_memory_write(&private_memory, 0, &initial[0], 1, NULL) != EXEC_OK ||
        exec_memory_read(&shared_memory, 0, &observed, 1, NULL) != EXEC_OK ||
        observed != value)
        goto done;
    result = 0;
done:
    exec_memory_release(&private_memory);
    exec_memory_release(&observer_memory);
    exec_memory_release(&shared_memory);
    if (file_object) posix_kernel_file_release(file_object);
    if (fd >= 0) (void)posix_kernel_close(store->kernel, fd);
    return result;
}

__attribute__((export_name("waste_wast_request_shared_file_page_probe")))
void waste_wast_request_shared_file_page_probe(void) {
    g_shared_file_page_probe_requested = 1;
}

__attribute__((export_name("waste_wast_shared_file_page_probe_result")))
int32_t waste_wast_shared_file_page_probe_result(void) {
    return g_shared_file_page_probe_result;
}

/* ---- Result accessor exports ---- */

__attribute__((export_name("waste_wast_results_ptr")))
uint32_t waste_wast_results_ptr(void) {
    return (uint32_t)(uintptr_t)g_browser_results;
}

__attribute__((export_name("waste_wast_transition_evidence_ptr")))
uint32_t waste_wast_transition_evidence_ptr(void) {
    return (uint32_t)(uintptr_t)g_browser_transition_evidence;
}

__attribute__((export_name("waste_wast_transition_evidence_len")))
uint32_t waste_wast_transition_evidence_len(void) {
    return (uint32_t)g_browser_transition_evidence_length;
}

__attribute__((export_name("waste_wast_results_total")))
uint32_t waste_wast_results_total(void) {
    return (uint32_t)g_browser_result_count;
}

__attribute__((export_name("waste_wast_result_name_ptr")))
uint32_t waste_wast_result_name_ptr(uint32_t index) {
    return index < (uint32_t)g_browser_result_count ?
        (uint32_t)(uintptr_t)g_browser_result_names[index] : 0;
}

__attribute__((export_name("waste_wast_result_name_len")))
uint32_t waste_wast_result_name_len(uint32_t index) {
    return index < (uint32_t)g_browser_result_count ?
        (uint32_t)strlen(g_browser_result_names[index]) : 0;
}

__attribute__((export_name("waste_wast_command_line")))
uint32_t waste_wast_command_line(void) {
    return g_browser_command_line;
}

__attribute__((export_name("waste_wast_results_passed")))
uint32_t waste_wast_results_passed(void) {
    return (uint32_t)g_browser_result_passed;
}

__attribute__((export_name("waste_wast_setup_total")))
uint32_t waste_wast_setup_total(void) { return g_browser_setup.total; }
__attribute__((export_name("waste_wast_setup_passed")))
uint32_t waste_wast_setup_passed(void) { return g_browser_setup.passed; }
__attribute__((export_name("waste_wast_setup_complete")))
uint32_t waste_wast_setup_complete(void) { return !g_browser_setup.incomplete; }
__attribute__((export_name("waste_wast_script_completed")))
uint32_t waste_wast_script_completed(void) { return g_browser_script_completed; }
__attribute__((export_name("waste_wast_setup_failure_count")))
uint32_t waste_wast_setup_failure_count(void) { return g_browser_setup.count; }
/* Each bounded diagnostic is line/status/phase (three 32-bit words), then
 * a NUL-terminated 256-byte error. Valid until the next run_script call. */
__attribute__((export_name("waste_wast_setup_failure_ptr")))
uint32_t waste_wast_setup_failure_ptr(uint32_t index) {
    return index < g_browser_setup.count ?
        (uint32_t)(uintptr_t)&g_browser_setup.failures[index] : 0;
}
