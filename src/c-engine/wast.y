%glr-parser
%pure-parser
%locations
%define parse.error verbose
%expect 35

%parse-param { wast_script *script }
%parse-param { void *scanner }
%lex-param   { void *scanner }

%code requires {
#include "wast_types.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#define MAX_LANE_COUNT 16

typedef struct {
    double   vals[MAX_LANE_COUNT];
    int64_t  ivals[MAX_LANE_COUNT];
    int      flags[MAX_LANE_COUNT];
    int      count;
} lane_list;
}

%code {
/* forward declaration of lex function */
int yylex(YYSTYPE *yylval, YYLTYPE *yylloc, void *scanner);
void yyerror(YYLTYPE *loc, wast_script *script, void *scanner, const char *msg);

/* lanes_to_v128 forward declaration */
static wasm_value lanes_to_v128(int lane_type, const lane_list *lanes, wast_script *script);

/* -----------------------------------------------------------------------
 * Parser state globals
 * --------------------------------------------------------------------- */

/* Current function being built */
static wast_func  g_cur_func;
static int        g_in_func  = 0;
static int        g_cur_group = 0;
static uint32_t   g_cur_func_index = 0;

/* Label stack for block depth resolution */
#define MAX_LABEL_DEPTH 256
static char g_labels[MAX_LABEL_DEPTH][WAST_MAX_EXPORT_NAME];
static int  g_label_depth = 0;

/* Local name table (params + locals for current function) */
static char g_local_names[WAST_MAX_PARAMS + WAST_MAX_LOCALS][WAST_MAX_EXPORT_NAME];
static int  g_local_name_count = 0;

/* Module-level name tables */
static char g_func_names[WAST_MAX_FUNCS][WAST_MAX_EXPORT_NAME];
static int  g_func_name_count = 0;

static char g_global_names[WAST_MAX_GLOBALS][WAST_MAX_EXPORT_NAME];
static int  g_global_name_count = 0;

static char g_type_names[WAST_MAX_TYPES][WAST_MAX_EXPORT_NAME];
static int  g_type_name_count = 0;
static char g_table_names[WAST_MAX_TABLES][WAST_MAX_EXPORT_NAME];
static int  g_table_name_count = 0;
static char g_memory_names[WAST_MAX_MEMORIES][WAST_MAX_EXPORT_NAME];
static int  g_memory_name_count = 0;
static char g_elem_names[WAST_MAX_ELEM_SEGS][WAST_MAX_EXPORT_NAME];
static int  g_elem_name_count = 0;

#define WAST_MAX_FUNC_FIXUPS 4096
typedef struct {
    uint32_t func_index;
    uint32_t code_offset;
    int line, column;
    char name[WAST_MAX_EXPORT_NAME];
} func_fixup;
static func_fixup g_func_fixups[WAST_MAX_FUNC_FIXUPS];
static int g_func_fixup_count = 0;
typedef struct { uint32_t func_index; int line, column; char name[WAST_MAX_EXPORT_NAME]; } type_fixup;
static type_fixup g_type_fixups[WAST_MAX_FUNCS];
static int g_type_fixup_count = 0;
typedef enum { IDX_FUNC, IDX_TYPE, IDX_GLOBAL, IDX_TABLE, IDX_MEMORY, IDX_ELEM } index_space;
typedef struct {
    index_space space;
    uint32_t func_index, code_offset;
    int line, column;
    char name[WAST_MAX_EXPORT_NAME];
} code_index_fixup;
static code_index_fixup g_code_fixups[WAST_MAX_FUNC_FIXUPS];
static int g_code_fixup_count = 0;
typedef enum { META_ELEM_FUNC, META_ELEM_TABLE, META_EXPORT, META_START, META_GLOBAL_INIT } meta_fixup_kind;
typedef struct {
    meta_fixup_kind kind;
    index_space space;
    uint32_t first, second;
    int line, column;
    char name[WAST_MAX_EXPORT_NAME];
} meta_fixup;
static meta_fixup g_meta_fixups[WAST_MAX_FUNC_FIXUPS];
static int g_meta_fixup_count = 0;

/* Assertion state */
static wast_assertion g_cur_assert;
static int            g_in_assert = 0;
static char           g_invoke_name[WAST_MAX_EXPORT_NAME];
static int            g_module_assert_action = 0;

/* br_table scratch buffer */
static uint32_t g_brtable_labels[256];
static int      g_brtable_count = 0;

/* Lane/byte immediate list for SIMD ops (e.g. i8x16.shuffle takes 16 ints) */
static uint32_t g_lane_imms[32];
static int      g_lane_imm_count = 0;

/* Pending import strings (set before import_desc is parsed) */
static char g_import_module[WAST_MAX_EXPORT_NAME];
static char g_import_name[WAST_MAX_EXPORT_NAME];
static int  g_export_kind;
static uint32_t g_export_index;

/* Global being built */
static wast_global g_cur_global;
static char g_global_export_name[WAST_MAX_EXPORT_NAME];

/* Data segment being built */
static wast_data_seg g_cur_data;

/* Element segment being built */
static wast_elem_seg g_cur_elem;

static uint32_t resolve_func(const char *s);
static uint32_t resolve_type(const char *s);
static uint32_t resolve_global(const char *s);
static uint32_t resolve_table(const char *s);
static uint32_t resolve_memory(const char *s);
static uint32_t resolve_elem(const char *s);
static void emit_init_byte(uint8_t *buf, int *len, int maxlen, uint8_t b);
static void emit_init_leb_s32(uint8_t *buf, int *len, int maxlen, int32_t v);

static wasm_valtype indexed_ref_type(wast_script *script, const char *name,
                                     int nullable) {
    uint32_t index = resolve_type(name);
    if (index == UINT32_MAX || index >= WAST_MAX_TYPES) {
        if (!script->error[0])
            snprintf(script->error, sizeof(script->error),
                     "unknown type in reference: %s", name);
        index = 0;
    }
    return (wasm_valtype)((nullable ? WASM_VALTYPE_TYPE_REF_NULL_BASE :
                          WASM_VALTYPE_TYPE_REF_BASE) + index);
}

static int32_t indexed_heap_type(wast_script *script, const char *name,
                                 int line, int column) {
    uint32_t index = resolve_type(name);
    if (index == UINT32_MAX || index >= WAST_MAX_TYPES) {
        if (!script->error[0])
            snprintf(script->error, sizeof(script->error),
                     "%d:%d: unknown heap type: %s", line, column, name);
        return 0;
    }
    return (int32_t)index;
}

/* -----------------------------------------------------------------------
 * Group helpers
 * --------------------------------------------------------------------- */

static wast_group *cur_group(wast_script *s) {
    if (g_cur_group < 0 || g_cur_group >= WAST_MAX_GROUPS) return &s->groups[0];
    return &s->groups[g_cur_group];
}

static void ensure_group(wast_script *s) {
    if (s->group_count == 0) { s->group_count = 1; g_cur_group = 0; }
}

static void set_register_name(wast_script *script, const char *name,
                              const char *module_id) {
    wast_group *target = cur_group(script);
    if (module_id && module_id[0]) {
        for (int i = 0; i < script->group_count; i++) {
            if (strcmp(script->groups[i].module.id, module_id) == 0) {
                target = &script->groups[i];
                break;
            }
        }
    }
    snprintf(target->module.register_name, WAST_MAX_EXPORT_NAME, "%s", name);
}

static void start_new_group(wast_script *s) {
    if (s->group_count < WAST_MAX_GROUPS) {
        g_cur_group = s->group_count++;
    } else {
        /* Overflow: reuse last slot with fresh module (parse continues, exec skipped) */
        g_cur_group = WAST_MAX_GROUPS - 1;
    }
    wast_group *g = &s->groups[g_cur_group];
    memset(g, 0, sizeof(*g));
    g->module.start_func = -1;
    g->assertion_start = s->assertion_count;
    g->assertion_count = 0;
    g_global_export_name[0] = '\0';
}

static void begin_module(wast_script *script) {
    start_new_group(script);
    g_func_name_count   = 0;
    g_func_fixup_count  = 0;
    g_type_fixup_count  = 0;
    g_code_fixup_count  = 0;
    g_meta_fixup_count  = 0;
    g_global_name_count = 0;
    g_type_name_count   = 0;
    g_table_name_count  = 0;
    g_memory_name_count = 0;
    g_elem_name_count   = 0;
}

static void commit_func(wast_script *s) {
    if (!g_in_func) return;
    ensure_group(s);
    wast_module *mod = &cur_group(s)->module;
    if (mod->func_count < WAST_MAX_FUNCS) {
        if (mod->func_count == mod->func_capacity) {
            int next_capacity = mod->func_capacity ? mod->func_capacity * 2 : 16;
            if (next_capacity > WAST_MAX_FUNCS) next_capacity = WAST_MAX_FUNCS;
            wast_func *next = realloc(mod->funcs,
                                      (size_t)next_capacity * sizeof(*next));
            if (!next) {
                if (s->error[0] == '\0')
                    snprintf(s->error, sizeof(s->error),
                             "out of memory growing function table");
                g_in_func = 0;
                return;
            }
            mod->funcs = next;
            mod->func_capacity = next_capacity;
        }
        if (g_func_name_count < WAST_MAX_FUNCS) {
            snprintf(g_func_names[g_func_name_count], WAST_MAX_EXPORT_NAME,
                     "%s", g_cur_func.id);
            g_func_name_count++;
        }
        mod->funcs[mod->func_count++] = g_cur_func;
    } else if (s->error[0] == '\0') {
        snprintf(s->error, sizeof(s->error), "too many functions (limit %d)",
                 WAST_MAX_FUNCS);
    }
    g_in_func = 0;
    g_label_depth = 0;
    g_local_name_count = 0;
}

static void append_assert(wast_script *s) {
    if (s->assertion_count < WAST_MAX_ASSERTIONS) {
        size_t n = strlen(g_invoke_name);
        if (n >= WAST_MAX_EXPORT_NAME) n = WAST_MAX_EXPORT_NAME - 1;
        memcpy(g_cur_assert.func_name, g_invoke_name, n);
        g_cur_assert.func_name[n] = '\0';
        s->assertions[s->assertion_count] = g_cur_assert;
        s->assertion_count++;
        cur_group(s)->assertion_count++;
    }
    g_in_assert = 0;
}

/* -----------------------------------------------------------------------
 * Binary emit helpers — write into g_cur_func.code[]
 * --------------------------------------------------------------------- */

static void emit_byte(wast_script *script, uint8_t b) {
    if (!g_in_func) return;
    if (g_cur_func.code_len >= WAST_MAX_CODE_BYTES) {
        if (script->error[0] == '\0')
            snprintf(script->error, 256, "code too large");
        return;
    }
    g_cur_func.code[g_cur_func.code_len++] = b;
}

static void emit_leb_u32(wast_script *script, uint32_t v) {
    do {
        uint8_t b = (uint8_t)(v & 0x7F); v >>= 7;
        if (v) b |= 0x80;
        emit_byte(script, b);
    } while (v);
}

static void emit_func_ref(wast_script *script, const char *name, int line, int column) {
    uint32_t index = resolve_func(name);
    if (!name || name[0] != '$' || index != UINT32_MAX) {
        emit_leb_u32(script, index);
        return;
    }
    if (g_func_fixup_count >= WAST_MAX_FUNC_FIXUPS ||
        g_cur_func.code_len > WAST_MAX_CODE_BYTES - 5) {
        if (script->error[0] == '\0')
            snprintf(script->error, sizeof(script->error),
                     "too many deferred function references");
        return;
    }
    func_fixup *fixup = &g_func_fixups[g_func_fixup_count++];
    fixup->func_index = g_cur_func_index;
    fixup->code_offset = (uint32_t)g_cur_func.code_len;
    fixup->line = line; fixup->column = column;
    snprintf(fixup->name, sizeof(fixup->name), "%s", name);
    /* A five-byte u32 LEB is valid and can be patched without moving code. */
    emit_byte(script, 0x80); emit_byte(script, 0x80);
    emit_byte(script, 0x80); emit_byte(script, 0x80); emit_byte(script, 0x00);
}

static uint32_t resolve_space(index_space space, const char *name) {
    switch (space) {
        case IDX_FUNC: return resolve_func(name);
        case IDX_TYPE: return resolve_type(name);
        case IDX_GLOBAL: return resolve_global(name);
        case IDX_TABLE: return resolve_table(name);
        case IDX_MEMORY: return resolve_memory(name);
        case IDX_ELEM: return resolve_elem(name);
    }
    return UINT32_MAX;
}

static const char *space_name(index_space space) {
    switch (space) {
        case IDX_FUNC: return "function"; case IDX_TYPE: return "type";
        case IDX_GLOBAL: return "global"; case IDX_TABLE: return "table";
        case IDX_MEMORY: return "memory";
        case IDX_ELEM: return "element segment";
    }
    return "index";
}

static void emit_index_ref(wast_script *script, index_space space, const char *name,
                           int line, int column) {
    uint32_t index = resolve_space(space, name);
    if (!name || name[0] != '$' || index != UINT32_MAX) { emit_leb_u32(script, index); return; }
    if (g_code_fixup_count >= WAST_MAX_FUNC_FIXUPS || g_cur_func.code_len > WAST_MAX_CODE_BYTES - 5) {
        if (!script->error[0]) snprintf(script->error,sizeof(script->error),"too many deferred index references");
        return;
    }
    code_index_fixup *f=&g_code_fixups[g_code_fixup_count++];
    f->space=space; f->func_index=g_cur_func_index; f->code_offset=(uint32_t)g_cur_func.code_len;
    f->line=line;f->column=column;
    snprintf(f->name,sizeof(f->name),"%s",name);
    emit_byte(script,0x80);emit_byte(script,0x80);emit_byte(script,0x80);emit_byte(script,0x80);emit_byte(script,0);
}

static void add_meta_fixup(wast_script *script, meta_fixup_kind kind, index_space space,
                           uint32_t first, uint32_t second, const char *name,
                           int line, int column) {
    if (g_meta_fixup_count >= WAST_MAX_FUNC_FIXUPS) {
        if (!script->error[0]) snprintf(script->error,sizeof(script->error),"too many deferred module references");
        return;
    }
    meta_fixup *f=&g_meta_fixups[g_meta_fixup_count++];
    f->kind=kind;f->space=space;f->first=first;f->second=second;
    f->line=line;f->column=column;
    snprintf(f->name,sizeof(f->name),"%s",name);
}

static uint32_t meta_index_ref(wast_script *script, meta_fixup_kind kind,
                               index_space space, uint32_t first, uint32_t second,
                               const char *name, int line, int column) {
    uint32_t index=resolve_space(space,name);
    if(name&&name[0]=='$'&&index==UINT32_MAX)
        add_meta_fixup(script,kind,space,first,second,name,line,column);
    return index;
}

static void append_elem_func_ref(wast_script *script, const char *name, int line, int column) {
    if(g_cur_elem.ref_count>=WAST_MAX_ELEM_REFS)return;
    uint32_t slot=(uint32_t)g_cur_elem.ref_count;
    uint32_t elem=(uint32_t)cur_group(script)->module.elem_count;
    g_cur_elem.refs[g_cur_elem.ref_count++]=meta_index_ref(script,META_ELEM_FUNC,IDX_FUNC,elem,slot,name,line,column);
}

static void emit_global_init_ref(wast_script *script, index_space space, const char *name,
                                 int line, int column) {
    uint32_t index=resolve_space(space,name);
    if(!name||name[0]!='$'||index!=UINT32_MAX){emit_init_leb_s32(g_cur_global.init_expr,&g_cur_global.init_len,32,(int32_t)index);return;}
    if(g_cur_global.init_len>27){if(!script->error[0])snprintf(script->error,sizeof(script->error),"global initializer too large");return;}
    uint32_t offset=(uint32_t)g_cur_global.init_len;
    add_meta_fixup(script,META_GLOBAL_INIT,space,(uint32_t)cur_group(script)->module.global_count,offset,name,line,column);
    for(int i=0;i<4;i++)emit_init_byte(g_cur_global.init_expr,&g_cur_global.init_len,32,0x80);
    emit_init_byte(g_cur_global.init_expr,&g_cur_global.init_len,32,0);
}

static void apply_func_fixups(wast_script *script) {
    wast_module *module = &cur_group(script)->module;
    for (int i = 0; i < g_func_fixup_count; i++) {
        func_fixup *fixup = &g_func_fixups[i];
        uint32_t index = resolve_func(fixup->name);
        if (index == UINT32_MAX || fixup->func_index >= (uint32_t)module->func_count ||
            (uint32_t)module->funcs[fixup->func_index].code_len < fixup->code_offset + 5u) {
            if (script->error[0] == '\0')
                snprintf(script->error, sizeof(script->error),
                         "%d:%d: unknown function: %s", fixup->line, fixup->column, fixup->name);
            continue;
        }
        uint8_t *dst = module->funcs[fixup->func_index].code + fixup->code_offset;
        dst[0] = (uint8_t)((index & 0x7fu) | 0x80u);
        dst[1] = (uint8_t)(((index >> 7) & 0x7fu) | 0x80u);
        dst[2] = (uint8_t)(((index >> 14) & 0x7fu) | 0x80u);
        dst[3] = (uint8_t)(((index >> 21) & 0x7fu) | 0x80u);
        dst[4] = (uint8_t)((index >> 28) & 0x0fu);
    }
    for (int i = 0; i < g_type_fixup_count; i++) {
        type_fixup *fixup = &g_type_fixups[i];
        uint32_t index = resolve_type(fixup->name);
        if (index == UINT32_MAX || fixup->func_index >= (uint32_t)module->func_count) {
            if (script->error[0] == '\0')
                snprintf(script->error, sizeof(script->error),
                         "%d:%d: unknown type: %s", fixup->line, fixup->column, fixup->name);
        } else {
            module->funcs[fixup->func_index].type_index = (int)index;
        }
    }
    for (int i=0;i<g_code_fixup_count;i++) {
        code_index_fixup *f=&g_code_fixups[i]; uint32_t index=resolve_space(f->space,f->name);
        if(index==UINT32_MAX||f->func_index>=(uint32_t)module->func_count||
           (uint32_t)module->funcs[f->func_index].code_len<f->code_offset+5u) {
            if(!script->error[0])snprintf(script->error,sizeof(script->error),"%d:%d: unknown %s: %s",f->line,f->column,space_name(f->space),f->name);
            continue;
        }
        uint8_t*d=module->funcs[f->func_index].code+f->code_offset;
        d[0]=(uint8_t)((index&0x7f)|0x80);d[1]=(uint8_t)(((index>>7)&0x7f)|0x80);
        d[2]=(uint8_t)(((index>>14)&0x7f)|0x80);d[3]=(uint8_t)(((index>>21)&0x7f)|0x80);d[4]=(uint8_t)((index>>28)&0x0f);
    }
    for(int i=0;i<g_meta_fixup_count;i++) {
        meta_fixup*f=&g_meta_fixups[i];uint32_t index=resolve_space(f->space,f->name);
        if(index==UINT32_MAX){if(!script->error[0])snprintf(script->error,sizeof(script->error),"%d:%d: unknown %s: %s",f->line,f->column,space_name(f->space),f->name);continue;}
        if(f->kind==META_ELEM_FUNC&&f->first<(uint32_t)module->elem_count&&f->second<(uint32_t)module->elem[f->first].ref_count)module->elem[f->first].refs[f->second]=index;
        else if(f->kind==META_ELEM_TABLE&&f->first<(uint32_t)module->elem_count)module->elem[f->first].table_index=(int)index;
        else if(f->kind==META_EXPORT&&f->first<(uint32_t)module->export_count)module->exports[f->first].index=index;
        else if(f->kind==META_START)module->start_func=(int)index;
        else if(f->kind==META_GLOBAL_INIT&&f->first<(uint32_t)module->global_count&&f->second+5u<=(uint32_t)module->globals[f->first].init_len){
            uint8_t*d=module->globals[f->first].init_expr+f->second;
            d[0]=(uint8_t)((index&0x7f)|0x80);d[1]=(uint8_t)(((index>>7)&0x7f)|0x80);
            d[2]=(uint8_t)(((index>>14)&0x7f)|0x80);d[3]=(uint8_t)(((index>>21)&0x7f)|0x80);d[4]=(uint8_t)((index>>28)&0x0f);
        }
    }
}

static void set_func_type_ref(wast_script *script, const char *name, int line, int column) {
    uint32_t index = resolve_type(name);
    if (!name || name[0] != '$' || index != UINT32_MAX) {
        g_cur_func.type_index = (int)index;
        return;
    }
    g_cur_func.type_index = -1;
    if (g_type_fixup_count >= WAST_MAX_FUNCS) {
        if (script->error[0] == '\0')
            snprintf(script->error, sizeof(script->error), "too many deferred type references");
        return;
    }
    type_fixup *fixup = &g_type_fixups[g_type_fixup_count++];
    fixup->func_index = g_cur_func_index;
    fixup->line = line; fixup->column = column;
    snprintf(fixup->name, sizeof(fixup->name), "%s", name);
}

static void emit_leb_s32(wast_script *script, int32_t v) {
    int more = 1;
    while (more) {
        uint8_t b = (uint8_t)(v & 0x7F);
        v >>= 7;
        more = !((v == 0 && !(b & 0x40)) || (v == -1 && (b & 0x40)));
        if (more) b |= 0x80;
        emit_byte(script, b);
    }
}

static void emit_leb_s64(wast_script *script, int64_t v) {
    int more = 1;
    while (more) {
        uint8_t b = (uint8_t)(v & 0x7F);
        v >>= 7;
        more = !((v == 0 && !(b & 0x40)) || (v == -1 && (b & 0x40)));
        if (more) b |= 0x80;
        emit_byte(script, b);
    }
}

static void emit_f32(wast_script *script, float f) {
    uint8_t buf[4]; memcpy(buf, &f, 4);
    for (int i = 0; i < 4; i++) emit_byte(script, buf[i]);
}

static void emit_f64(wast_script *script, double d) {
    uint8_t buf[8]; memcpy(buf, &d, 8);
    for (int i = 0; i < 8; i++) emit_byte(script, buf[i]);
}

/* Emit into a small buffer (for init expressions) */
static void emit_init_byte(uint8_t *buf, int *len, int maxlen, uint8_t b) {
    if (*len < maxlen) buf[(*len)++] = b;
}
static void emit_init_leb_s32(uint8_t *buf, int *len, int maxlen, int32_t v) {
    int more = 1;
    while (more) {
        uint8_t b = (uint8_t)(v & 0x7F); v >>= 7;
        more = !((v == 0 && !(b & 0x40)) || (v == -1 && (b & 0x40)));
        if (more) b |= 0x80;
        emit_init_byte(buf, len, maxlen, b);
    }
}
static void emit_init_leb_s64(uint8_t *buf, int *len, int maxlen, int64_t v) {
    int more = 1;
    while (more) {
        uint8_t b = (uint8_t)(v & 0x7F); v >>= 7;
        more = !((v == 0 && !(b & 0x40)) || (v == -1 && (b & 0x40)));
        if (more) b |= 0x80;
        emit_init_byte(buf, len, maxlen, b);
    }
}

/* -----------------------------------------------------------------------
 * Name resolution helpers
 * --------------------------------------------------------------------- */

static void push_label(const char *name) {
    if (g_label_depth < MAX_LABEL_DEPTH)
        snprintf(g_labels[g_label_depth++], WAST_MAX_EXPORT_NAME, "%s", name);
}
static void pop_label(void) {
    if (g_label_depth > 0) g_label_depth--;
}
static uint32_t resolve_label(const char *s) {
    if (!s || !s[0]) return 0;
    if (s[0] == '$') {
        for (int i = g_label_depth - 1; i >= 0; i--)
            if (strcmp(g_labels[i], s) == 0)
                return (uint32_t)(g_label_depth - 1 - i);
        return 0;
    }
    return (uint32_t)strtoul(s, NULL, 10);
}
static uint32_t resolve_local(const char *s) {
    if (s && s[0] == '$')
        for (int i = 0; i < g_local_name_count; i++)
            if (strcmp(g_local_names[i], s) == 0) return (uint32_t)i;
    if (s && s[0] == '$') return UINT32_MAX;
    return (uint32_t)strtoul(s, NULL, 10);
}
static uint32_t resolve_global(const char *s) {
    if (s && s[0] == '$')
        for (int i = 0; i < g_global_name_count; i++)
            if (strcmp(g_global_names[i], s) == 0) return (uint32_t)i;
    if (s && s[0] == '$') return UINT32_MAX;
    return (uint32_t)strtoul(s, NULL, 10);
}
static uint32_t resolve_func(const char *s) {
    if (s && s[0] == '$') {
        for (int i = 0; i < g_func_name_count; i++)
            if (strcmp(g_func_names[i], s) == 0) return (uint32_t)i;
        if (g_in_func && strcmp(g_cur_func.id, s) == 0)
            return g_cur_func_index;
        return UINT32_MAX;
    }
    return (uint32_t)strtoul(s, NULL, 10);
}
static uint32_t resolve_type(const char *s) {
    if (s && s[0] == '$')
        for (int i = 0; i < g_type_name_count; i++)
            if (strcmp(g_type_names[i], s) == 0) return (uint32_t)i;
    if (s && s[0] == '$') return UINT32_MAX;
    return (uint32_t)strtoul(s, NULL, 10);
}
static uint32_t resolve_table(const char *s) {
    if (s && s[0] == '$') {
        for (int i=0;i<g_table_name_count;i++)
            if (!strcmp(g_table_names[i],s)) return (uint32_t)i;
        return UINT32_MAX;
    }
    return (uint32_t)strtoul(s,NULL,10);
}
static uint32_t resolve_memory(const char *s) {
    if (s && s[0] == '$') {
        for (int i=0;i<g_memory_name_count;i++)
            if (!strcmp(g_memory_names[i],s)) return (uint32_t)i;
        return UINT32_MAX;
    }
    return (uint32_t)strtoul(s,NULL,10);
}
static uint32_t resolve_elem(const char *s) {
    if (s && s[0] == '$') {
        for (int i=0;i<g_elem_name_count;i++) if (!strcmp(g_elem_names[i],s)) return (uint32_t)i;
        return UINT32_MAX;
    }
    return (uint32_t)strtoul(s,NULL,10);
}

/* -----------------------------------------------------------------------
 * ATOM opcode table — returns 1 if handled, 0 if unknown
 * --------------------------------------------------------------------- */

static int emit_atom_op(wast_script *script, const char *name) {
    static const struct { const char *n; uint8_t op; } tbl[] = {
        /* control */
        {"unreachable",0x00},{"nop",0x01},{"return",0x0F},
        {"drop",0x1A},{"select",0x1B},
        /* i32 arithmetic */
        {"i32.eqz",0x45},{"i32.eq",0x46},{"i32.ne",0x47},
        {"i32.lt_s",0x48},{"i32.lt_u",0x49},{"i32.gt_s",0x4A},{"i32.gt_u",0x4B},
        {"i32.le_s",0x4C},{"i32.le_u",0x4D},{"i32.ge_s",0x4E},{"i32.ge_u",0x4F},
        {"i64.eqz",0x50},{"i64.eq",0x51},{"i64.ne",0x52},
        {"i64.lt_s",0x53},{"i64.lt_u",0x54},{"i64.gt_s",0x55},{"i64.gt_u",0x56},
        {"i64.le_s",0x57},{"i64.le_u",0x58},{"i64.ge_s",0x59},{"i64.ge_u",0x5A},
        {"f32.eq",0x5B},{"f32.ne",0x5C},{"f32.lt",0x5D},{"f32.gt",0x5E},
        {"f32.le",0x5F},{"f32.ge",0x60},
        {"f64.eq",0x61},{"f64.ne",0x62},{"f64.lt",0x63},{"f64.gt",0x64},
        {"f64.le",0x65},{"f64.ge",0x66},
        {"i32.clz",0x67},{"i32.ctz",0x68},{"i32.popcnt",0x69},
        {"i32.add",0x6A},{"i32.sub",0x6B},{"i32.mul",0x6C},
        {"i32.div_s",0x6D},{"i32.div_u",0x6E},{"i32.rem_s",0x6F},{"i32.rem_u",0x70},
        {"i32.and",0x71},{"i32.or",0x72},{"i32.xor",0x73},
        {"i32.shl",0x74},{"i32.shr_s",0x75},{"i32.shr_u",0x76},
        {"i32.rotl",0x77},{"i32.rotr",0x78},
        {"i64.clz",0x79},{"i64.ctz",0x7A},{"i64.popcnt",0x7B},
        {"i64.add",0x7C},{"i64.sub",0x7D},{"i64.mul",0x7E},
        {"i64.div_s",0x7F},{"i64.div_u",0x80},{"i64.rem_s",0x81},{"i64.rem_u",0x82},
        {"i64.and",0x83},{"i64.or",0x84},{"i64.xor",0x85},
        {"i64.shl",0x86},{"i64.shr_s",0x87},{"i64.shr_u",0x88},
        {"i64.rotl",0x89},{"i64.rotr",0x8A},
        {"f32.abs",0x8B},{"f32.neg",0x8C},{"f32.ceil",0x8D},{"f32.floor",0x8E},
        {"f32.trunc",0x8F},{"f32.nearest",0x90},{"f32.sqrt",0x91},
        {"f32.add",0x92},{"f32.sub",0x93},{"f32.mul",0x94},{"f32.div",0x95},
        {"f32.min",0x96},{"f32.max",0x97},{"f32.copysign",0x98},
        {"f64.abs",0x99},{"f64.neg",0x9A},{"f64.ceil",0x9B},{"f64.floor",0x9C},
        {"f64.trunc",0x9D},{"f64.nearest",0x9E},{"f64.sqrt",0x9F},
        {"f64.add",0xA0},{"f64.sub",0xA1},{"f64.mul",0xA2},{"f64.div",0xA3},
        {"f64.min",0xA4},{"f64.max",0xA5},{"f64.copysign",0xA6},
        /* conversions */
        {"i32.wrap_i64",0xA7},
        {"i32.trunc_f32_s",0xA8},{"i32.trunc_f32_u",0xA9},
        {"i32.trunc_f64_s",0xAA},{"i32.trunc_f64_u",0xAB},
        {"i64.extend_i32_s",0xAC},{"i64.extend_i32_u",0xAD},
        {"i64.trunc_f32_s",0xAE},{"i64.trunc_f32_u",0xAF},
        {"i64.trunc_f64_s",0xB0},{"i64.trunc_f64_u",0xB1},
        {"f32.convert_i32_s",0xB2},{"f32.convert_i32_u",0xB3},
        {"f32.convert_i64_s",0xB4},{"f32.convert_i64_u",0xB5},
        {"f32.demote_f64",0xB6},
        {"f64.convert_i32_s",0xB7},{"f64.convert_i32_u",0xB8},
        {"f64.convert_i64_s",0xB9},{"f64.convert_i64_u",0xBA},
        {"f64.promote_f32",0xBB},
        {"f32.reinterpret_i32",0xBC},{"f64.reinterpret_i64",0xBD},
        {"i32.reinterpret_f32",0xBE},{"i64.reinterpret_f64",0xBF},
        /* sign-extension */
        {"i32.extend8_s",0xC0},{"i32.extend16_s",0xC1},
        {"i64.extend8_s",0xC2},{"i64.extend16_s",0xC3},
        {"i64.extend32_s",0xC4},
        /* ref */
        {"ref.is_null",0xD1},
        {NULL,0}
    };
    for (int i = 0; tbl[i].n; i++) {
        if (strcmp(name, tbl[i].n) == 0) {
            emit_byte(script, tbl[i].op);
            return 1;
        }
    }
    /* 0xFC prefix ops (trunc_sat) */
    static const struct { const char *n; uint32_t sub; } fc_tbl[] = {
        {"i32.trunc_sat_f32_s",0},{"i32.trunc_sat_f32_u",1},
        {"i32.trunc_sat_f64_s",2},{"i32.trunc_sat_f64_u",3},
        {"i64.trunc_sat_f32_s",4},{"i64.trunc_sat_f32_u",5},
        {"i64.trunc_sat_f64_s",6},{"i64.trunc_sat_f64_u",7},
        {NULL,0}
    };
    for (int i = 0; fc_tbl[i].n; i++) {
        if (strcmp(name, fc_tbl[i].n) == 0) {
            emit_byte(script, 0xFC);
            emit_leb_u32(script, fc_tbl[i].sub);
            return 1;
        }
    }
    /* Memory load/store ops — default memarg 0,0 */
    static const struct { const char *n; uint8_t op; } mem_tbl[] = {
        {"i32.load",0x28},{"i64.load",0x29},{"f32.load",0x2A},{"f64.load",0x2B},
        {"i32.load8_s",0x2C},{"i32.load8_u",0x2D},
        {"i32.load16_s",0x2E},{"i32.load16_u",0x2F},
        {"i64.load8_s",0x30},{"i64.load8_u",0x31},
        {"i64.load16_s",0x32},{"i64.load16_u",0x33},
        {"i64.load32_s",0x34},{"i64.load32_u",0x35},
        {"i32.store",0x36},{"i64.store",0x37},{"f32.store",0x38},{"f64.store",0x39},
        {"i32.store8",0x3A},{"i32.store16",0x3B},
        {"i64.store8",0x3C},{"i64.store16",0x3D},{"i64.store32",0x3E},
        {NULL,0}
    };
    for (int i = 0; mem_tbl[i].n; i++) {
        if (strcmp(name, mem_tbl[i].n) == 0) {
            emit_byte(script, mem_tbl[i].op);
            emit_byte(script, 0x00); /* align */
            emit_byte(script, 0x00); /* offset */
            return 1;
        }
    }
    return 0;
}

/* Memory opcode lookup by name (for fold_memop rule) */
static uint8_t memop_by_name(const char *name) {
    static const struct { const char *n; uint8_t op; } tbl[] = {
        {"i32.load",0x28},{"i64.load",0x29},{"f32.load",0x2A},{"f64.load",0x2B},
        {"i32.load8_s",0x2C},{"i32.load8_u",0x2D},
        {"i32.load16_s",0x2E},{"i32.load16_u",0x2F},
        {"i64.load8_s",0x30},{"i64.load8_u",0x31},
        {"i64.load16_s",0x32},{"i64.load16_u",0x33},
        {"i64.load32_s",0x34},{"i64.load32_u",0x35},
        {"i32.store",0x36},{"i64.store",0x37},{"f32.store",0x38},{"f64.store",0x39},
        {"i32.store8",0x3A},{"i32.store16",0x3B},
        {"i64.store8",0x3C},{"i64.store16",0x3D},{"i64.store32",0x3E},
        {NULL,0}
    };
    for (int i = 0; tbl[i].n; i++)
        if (strcmp(name, tbl[i].n) == 0) return tbl[i].op;
    return 0;
}

/* Natural default alignment for memory ops */
static uint32_t default_align(uint8_t op) {
    switch(op) {
        case 0x28: case 0x36: return 2; /* i32.load/store */
        case 0x29: case 0x37: return 3; /* i64.load/store */
        case 0x2A: case 0x38: return 2; /* f32.load/store */
        case 0x2B: case 0x39: return 3; /* f64.load/store */
        case 0x2C: case 0x2D: case 0x3A: return 0; /* load8/store8 */
        case 0x2E: case 0x2F: case 0x3B: return 1; /* load16/store16 */
        case 0x30: case 0x31: case 0x3C: return 0;
        case 0x32: case 0x33: case 0x3D: return 1;
        case 0x34: case 0x35: case 0x3E: return 2; /* load32/store32 */
        default: return 0;
    }
}

/* Valtype byte encoding */
static uint8_t valtype_byte(wasm_valtype vt) {
    switch (vt) {
        case WASM_VALTYPE_I32:      return 0x7F;
        case WASM_VALTYPE_I64:      return 0x7E;
        case WASM_VALTYPE_F32:      return 0x7D;
        case WASM_VALTYPE_F64:      return 0x7C;
        case WASM_VALTYPE_V128:     return 0x7B;
        case WASM_VALTYPE_FUNCREF:  return 0x70;
        case WASM_VALTYPE_EXTERNREF:return 0x6F;
        default:                    return 0x7F;
    }
}

/* Emit blocktype byte(s) */
static void emit_blocktype(wast_script *script, int bt) {
    if (bt == -1) { emit_byte(script, 0x40); return; } /* void */
    /* bt >= 0: it's a wasm_valtype */
    emit_byte(script, valtype_byte((wasm_valtype)bt));
}

} /* end %code */

%union {
    int64_t      i64_val;
    double       f64_val;
    uint32_t     u32_val;
    uint64_t     u64_val;
    char         str_val[WAST_MAX_EXPORT_NAME];
    wasm_valtype valtype_val;
    wasm_value   value_val;
    lane_list    lane_list_val;
    int          int_val;
    wast_limits  limits_val;
}

/* -----------------------------------------------------------------------
 * Token declarations
 * --------------------------------------------------------------------- */

%token LPAREN RPAREN
%token <str_val>  STRING ATOM ID
%token <u32_val>  SIMD_OP OFFSET_IMM ALIGN_IMM
%token <i64_val>  INT HEXINT
%token <f64_val>  FLOAT

%token KW_V128_CONST KW_LOCAL_GET KW_LOCAL_SET KW_LOCAL_TEE
%token KW_GLOBAL_GET KW_GLOBAL_SET
%token KW_I32_CONST KW_I64_CONST KW_F32_CONST KW_F64_CONST
%token KW_CALL KW_CALL_INDIRECT
%token KW_BR KW_BR_IF KW_BR_TABLE
%token KW_BLOCK KW_LOOP KW_IF KW_ELSE KW_THEN KW_END KW_RETURN
%token KW_SELECT KW_DROP KW_UNREACHABLE KW_NOP
%token KW_REF_FUNC KW_REF_NULL KW_REF_IS_NULL KW_REF_EXTERN KW_REF_TYPE KW_NULL KW_EXTERN
%token KW_MEMORY_SIZE KW_MEMORY_GROW
%token KW_MEMORY_INIT KW_MEMORY_COPY KW_MEMORY_FILL
%token KW_TABLE_GET KW_TABLE_SET KW_TABLE_GROW KW_TABLE_SIZE
%token KW_TABLE_INIT KW_TABLE_COPY KW_TABLE_FILL
%token KW_MODULE KW_FUNC KW_EXPORT KW_IMPORT KW_PARAM KW_RESULT
%token KW_TABLE KW_MEMORY KW_GLOBAL KW_DATA KW_ELEM KW_TYPE KW_START
%token KW_LOCAL KW_MUT KW_DECLARE KW_ITEM KW_OFFSET_KW KW_REGISTER
%token KW_FUNCREF KW_EXTERNREF
%token KW_V128 KW_I32 KW_I64 KW_F32 KW_F64
%token KW_I8X16 KW_I16X8 KW_I32X4 KW_I64X2 KW_F32X4 KW_F64X2
%token KW_NAN_CANONICAL KW_NAN_ARITHMETIC KW_NEG_NAN KW_NEG_INF
%token KW_POS_NAN KW_POS_INF KW_NAN KW_INF
%token KW_ASSERT_RETURN KW_ASSERT_TRAP KW_ASSERT_EXHAUSTION
%token KW_ASSERT_INVALID KW_ASSERT_MALFORMED KW_ASSERT_UNLINKABLE
%token KW_INVOKE KW_EITHER KW_GET

/* -----------------------------------------------------------------------
 * Type declarations for non-terminal symbols
 * --------------------------------------------------------------------- */

%type <valtype_val>   valtype reftype_as_valtype table_reftype
%type <value_val>     const_val result_const f32_val f64_val
%type <lane_list_val> lane_vals lane_val
%type <int_val>       lane_type blocktype reftype opt_table_idx
%type <str_val>       opt_id opt_label any_idx
%type <i64_val>       any_int any_nat
%type <u64_val>       memarg
%type <int_val>       typeuse typeuse_items typeuse_item blocktype_items blocktype_item
%type <limits_val>    limits
%type <u64_val>       memarg_nonempty

%%

/* -----------------------------------------------------------------------
 * Top level
 * --------------------------------------------------------------------- */

script:
    commands
    ;

commands:
    /* empty */
  | commands command
    ;

command:
    module_cmd
  | assert_cmd
  | register_cmd
  | LPAREN KW_INVOKE STRING { strncpy(g_invoke_name,$3,WAST_MAX_EXPORT_NAME-1); g_invoke_name[WAST_MAX_EXPORT_NAME-1]='\0'; } const_list RPAREN
    /* top-level invoke — ignore result */
  | LPAREN ATOM error RPAREN
  | LPAREN error RPAREN
    ;

/* -----------------------------------------------------------------------
 * Module
 * --------------------------------------------------------------------- */

module_cmd:
    /* (module binary "...") or (module quote "...") — skip inline binary/text modules */
    LPAREN KW_MODULE ATOM string_list RPAREN {
        /* binary/quote module at top level — treated as empty module */
        start_new_group(script);
    }
  | LPAREN KW_MODULE {
        begin_module(script);
    }
    opt_module_id module_fields RPAREN { apply_func_fixups(script); }
    ;

opt_module_id:
    /* empty */
  | ID      { snprintf(cur_group(script)->module.id, WAST_MAX_EXPORT_NAME, "%s", $1); }
  | ATOM    { snprintf(cur_group(script)->module.id, WAST_MAX_EXPORT_NAME, "%s", $1); }
  | STRING  { snprintf(cur_group(script)->module.id, WAST_MAX_EXPORT_NAME, "%s", $1); }
    ;

module_fields:
    /* empty */
  | module_fields module_field
    ;

module_field:
    func_item
  | import_item
  | memory_item
  | global_item
  | table_item
  | data_item
  | elem_item
  | type_item
  | export_item
  | start_item
    ;

/* -----------------------------------------------------------------------
 * Function definition
 * --------------------------------------------------------------------- */

func_item:
    LPAREN KW_FUNC {
        memset(&g_cur_func, 0, sizeof(g_cur_func));
        g_cur_func.type_index = -1;
        g_in_func   = 1;
        g_cur_func_index = (uint32_t)cur_group(script)->module.func_count;
        g_label_depth = 0;
        g_local_name_count = 0;
    }
    func_attrs RPAREN
    ;

func_attrs:
    opt_id func_attr_list func_body {
        if (!g_cur_func.is_import) {
            emit_byte(script, 0x0B); /* end */
        }
        commit_func(script);
    }
    ;

/*
 * Flat list of func header attributes — export, import, type, param, result, local.
 * Flattened so LALR(1) can distinguish them by their keyword after the first LPAREN.
 */
func_attr_list:
    /* empty */
  | func_attr_list LPAREN KW_EXPORT STRING RPAREN {
        if (g_in_func && g_cur_func.export_name[0] == '\0') {
            size_t n = strlen($4);
            if (n >= WAST_MAX_EXPORT_NAME) n = WAST_MAX_EXPORT_NAME - 1;
            memcpy(g_cur_func.export_name, $4, n);
            g_cur_func.export_name[n] = '\0';
        }
    }
  | func_attr_list LPAREN KW_IMPORT STRING STRING RPAREN {
        snprintf(g_cur_func.import_module, WAST_MAX_EXPORT_NAME, "%s", $4);
        snprintf(g_cur_func.import_name,   WAST_MAX_EXPORT_NAME, "%s", $5);
        g_cur_func.is_import = 1;
    }
  | func_attr_list LPAREN KW_TYPE any_idx RPAREN {
        set_func_type_ref(script, $4, @4.first_line, @4.first_column);
    }
  | func_attr_list LPAREN KW_PARAM param_items RPAREN
  | func_attr_list LPAREN KW_RESULT result_valtype_list RPAREN
  | func_attr_list LPAREN KW_LOCAL local_items RPAREN
    ;

opt_id:
    /* empty */  { $$[0] = '\0'; }
  | ID {
        snprintf($$, WAST_MAX_EXPORT_NAME, "%s", $1);
        if (g_in_func)
            snprintf(g_cur_func.id, WAST_MAX_EXPORT_NAME, "%s", $1);
    }
  | ATOM {
        snprintf($$, WAST_MAX_EXPORT_NAME, "%s", $1);
        if (g_in_func && $1[0] == '$')
            snprintf(g_cur_func.id, WAST_MAX_EXPORT_NAME, "%s", $1);
    }
    ;

/* These remain for use in other grammar rules (import_desc, type decls, etc.) */
export_list:
    /* empty */
  | export_list LPAREN KW_EXPORT STRING RPAREN {
        if (g_in_func && g_cur_func.export_name[0] == '\0') {
            size_t n = strlen($4);
            if (n >= WAST_MAX_EXPORT_NAME) n = WAST_MAX_EXPORT_NAME - 1;
            memcpy(g_cur_func.export_name, $4, n);
            g_cur_func.export_name[n] = '\0';
        }
    }
    ;

func_typeuse:
    /* empty */
  | LPAREN KW_TYPE any_idx RPAREN {
        set_func_type_ref(script, $3, @3.first_line, @3.first_column);
    }
    ;

param_list:
    /* empty */
  | param_list LPAREN KW_PARAM param_items RPAREN
    ;

param_items:
    /* empty */
  | param_items ID valtype {
        /* named param: $name type */
        if (g_in_func && g_cur_func.param_count < WAST_MAX_PARAMS) {
            g_cur_func.params[g_cur_func.param_count] = $3;
            g_cur_func.param_count++;
            if (g_local_name_count < WAST_MAX_PARAMS + WAST_MAX_LOCALS)
                snprintf(g_local_names[g_local_name_count++], WAST_MAX_EXPORT_NAME, "%s", $2);
        }
    }
  | param_items ATOM valtype {
        /* named param: $name type */
        if (g_in_func && g_cur_func.param_count < WAST_MAX_PARAMS) {
            int idx = g_cur_func.param_count;
            g_cur_func.params[idx] = $3;
            g_cur_func.param_count++;
            if (g_local_name_count < WAST_MAX_PARAMS + WAST_MAX_LOCALS)
                snprintf(g_local_names[g_local_name_count++], WAST_MAX_EXPORT_NAME, "%s", $2);
        }
    }
  | param_items valtype {
        if (g_in_func && g_cur_func.param_count < WAST_MAX_PARAMS) {
            g_cur_func.params[g_cur_func.param_count++] = $2;
            if (g_local_name_count < WAST_MAX_PARAMS + WAST_MAX_LOCALS)
                g_local_names[g_local_name_count++][0] = '\0';
        }
    }
    ;

valtype_list:
    valtype {
        if (g_in_func && g_cur_func.param_count < WAST_MAX_PARAMS) {
            g_cur_func.params[g_cur_func.param_count++] = $1;
            if (g_local_name_count < WAST_MAX_PARAMS + WAST_MAX_LOCALS)
                g_local_names[g_local_name_count++][0] = '\0';
        }
    }
  | valtype_list valtype {
        if (g_in_func && g_cur_func.param_count < WAST_MAX_PARAMS) {
            g_cur_func.params[g_cur_func.param_count++] = $2;
            /* unnamed param — push empty name to keep indices aligned */
            if (g_local_name_count < WAST_MAX_PARAMS + WAST_MAX_LOCALS)
                g_local_names[g_local_name_count++][0] = '\0';
        }
    }
    ;

result_list:
    /* empty */
  | result_list LPAREN KW_RESULT result_valtype_list RPAREN
    ;

result_valtype_list:
    /* empty */
  | result_valtype_list valtype {
        if (g_in_func && g_cur_func.result_count < WAST_MAX_RESULTS) {
            g_cur_func.results[g_cur_func.result_count++] = $2;
        }
    }
    ;

local_list:
    /* empty */
  | local_list LPAREN KW_LOCAL local_items RPAREN
    ;

local_items:
    /* empty */
  | local_items ID valtype {
        /* named local */
        if (g_in_func && g_cur_func.local_count < WAST_MAX_LOCALS) {
            g_cur_func.locals[g_cur_func.local_count++] = $3;
            if (g_local_name_count < WAST_MAX_PARAMS + WAST_MAX_LOCALS)
                snprintf(g_local_names[g_local_name_count++], WAST_MAX_EXPORT_NAME, "%s", $2);
        }
    }
  | local_items ATOM valtype {
        /* named local */
        if (g_in_func && g_cur_func.local_count < WAST_MAX_LOCALS) {
            g_cur_func.locals[g_cur_func.local_count++] = $3;
            if (g_local_name_count < WAST_MAX_PARAMS + WAST_MAX_LOCALS)
                snprintf(g_local_names[g_local_name_count++], WAST_MAX_EXPORT_NAME, "%s", $2);
        }
    }
  | local_items valtype {
        if (g_in_func && g_cur_func.local_count < WAST_MAX_LOCALS) {
            g_cur_func.locals[g_cur_func.local_count++] = $2;
            if (g_local_name_count < WAST_MAX_PARAMS + WAST_MAX_LOCALS)
                g_local_names[g_local_name_count++][0] = '\0';
        }
    }
    ;

local_valtype_list:
    valtype {
        if (g_in_func && g_cur_func.local_count < WAST_MAX_LOCALS) {
            g_cur_func.locals[g_cur_func.local_count++] = $1;
            if (g_local_name_count < WAST_MAX_PARAMS + WAST_MAX_LOCALS)
                g_local_names[g_local_name_count++][0] = '\0';
        }
    }
  | local_valtype_list valtype {
        if (g_in_func && g_cur_func.local_count < WAST_MAX_LOCALS) {
            g_cur_func.locals[g_cur_func.local_count++] = $2;
            if (g_local_name_count < WAST_MAX_PARAMS + WAST_MAX_LOCALS)
                g_local_names[g_local_name_count++][0] = '\0';
        }
    }
    ;

/* -----------------------------------------------------------------------
 * Function body — sequence of instructions
 * --------------------------------------------------------------------- */

func_body:
    /* empty */
  | func_body instr
    ;

instr:
    fold_instr
  | plain_instr
    ;

/* -----------------------------------------------------------------------
 * Plain (non-folded) instructions
 * --------------------------------------------------------------------- */

plain_instr:
    KW_UNREACHABLE { emit_byte(script,0x00); }
  | KW_NOP         { emit_byte(script,0x01); }
  | KW_RETURN      { emit_byte(script,0x0F); }
  | KW_DROP        { emit_byte(script,0x1A); }
  | KW_SELECT      { emit_byte(script,0x1B); }
  | KW_LOCAL_GET any_idx  { emit_byte(script,0x20); emit_leb_u32(script,resolve_local($2)); }
  | KW_LOCAL_SET any_idx  { emit_byte(script,0x21); emit_leb_u32(script,resolve_local($2)); }
  | KW_LOCAL_TEE any_idx  { emit_byte(script,0x22); emit_leb_u32(script,resolve_local($2)); }
  | KW_GLOBAL_GET any_idx { emit_byte(script,0x23); emit_index_ref(script,IDX_GLOBAL,$2,@2.first_line,@2.first_column); }
  | KW_GLOBAL_SET any_idx { emit_byte(script,0x24); emit_index_ref(script,IDX_GLOBAL,$2,@2.first_line,@2.first_column); }
  | KW_MEMORY_SIZE { emit_byte(script,0x3F); emit_byte(script,0x00); }
  | KW_MEMORY_GROW { emit_byte(script,0x40); emit_byte(script,0x00); }
  | KW_BR    any_idx { emit_byte(script,0x0C); emit_leb_u32(script,resolve_label($2)); }
  | KW_BR_IF any_idx { emit_byte(script,0x0D); emit_leb_u32(script,resolve_label($2)); }
  | KW_CALL  any_idx { emit_byte(script,0x10); emit_func_ref(script,$2,@2.first_line,@2.first_column); }
  | KW_REF_NULL  reftype { emit_byte(script,0xD0); emit_byte(script,(uint8_t)$2); }
  | KW_REF_IS_NULL       { emit_byte(script,0xD1); }
  | KW_REF_FUNC any_idx  { emit_byte(script,0xD2); emit_func_ref(script,$2,@2.first_line,@2.first_column); }
  | KW_I32_CONST any_int { emit_byte(script,0x41); emit_leb_s32(script,(int32_t)$2); }
  | KW_I64_CONST any_int { emit_byte(script,0x42); emit_leb_s64(script,(int64_t)$2); }
  | KW_F32_CONST float_or_int {
        /* handled via fold_f32_plain helper rule below — not reached */
    }
  | KW_F64_CONST float_or_int_64 {
        /* handled via fold_f64_plain helper rule below — not reached */
    }
  | KW_V128_CONST lane_type lane_vals {
        wasm_value v = lanes_to_v128($2, &$3, script);
        emit_byte(script, 0xFD);
        emit_leb_u32(script, 12);
        for (int i = 0; i < 16; i++) emit_byte(script, v.v128.bytes[i]);
    }
  | SIMD_OP {
        emit_byte(script, 0xFD);
        emit_leb_u32(script, $1);
    }
  | block_plain
  | loop_plain
  | if_plain
  | br_table_plain
  | call_indirect_plain
  | ATOM { emit_atom_op(script,$1); }
    ;

/* f32.const / f64.const plain forms - we handle via inline rules */
float_or_int:
    FLOAT   { emit_byte(script,0x43); emit_f32(script,(float)$1); }
  | any_int { emit_byte(script,0x43); uint32_t b=(uint32_t)(uint64_t)$1; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_NAN_CANONICAL { emit_byte(script,0x43); uint32_t b=0x7FC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_NAN_ARITHMETIC{ emit_byte(script,0x43); uint32_t b=0x7FC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_NEG_NAN       { emit_byte(script,0x43); uint32_t b=0xFFC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_POS_NAN       { emit_byte(script,0x43); uint32_t b=0x7FC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_NAN           { emit_byte(script,0x43); uint32_t b=0x7FC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_INF           { emit_byte(script,0x43); float f=1.0f/0.0f; emit_f32(script,f); }
  | KW_POS_INF       { emit_byte(script,0x43); float f=1.0f/0.0f; emit_f32(script,f); }
  | KW_NEG_INF       { emit_byte(script,0x43); float f=-1.0f/0.0f; emit_f32(script,f); }
    ;

float_or_int_64:
    FLOAT   { emit_byte(script,0x44); emit_f64(script,$1); }
  | any_int { emit_byte(script,0x44); double d; uint64_t b=(uint64_t)$1; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_NAN_CANONICAL { emit_byte(script,0x44); uint64_t b=0x7FF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_NAN_ARITHMETIC{ emit_byte(script,0x44); uint64_t b=0x7FF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_NEG_NAN       { emit_byte(script,0x44); uint64_t b=0xFFF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_POS_NAN       { emit_byte(script,0x44); uint64_t b=0x7FF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_NAN           { emit_byte(script,0x44); uint64_t b=0x7FF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_INF           { emit_byte(script,0x44); double d=1.0/0.0; emit_f64(script,d); }
  | KW_POS_INF       { emit_byte(script,0x44); double d=1.0/0.0; emit_f64(script,d); }
  | KW_NEG_INF       { emit_byte(script,0x44); double d=-1.0/0.0; emit_f64(script,d); }
    ;

/* -----------------------------------------------------------------------
 * Block / loop / if — plain (non-folded) forms
 * --------------------------------------------------------------------- */

block_plain:
    KW_BLOCK opt_label blocktype {
        push_label($2);
        emit_byte(script, 0x02);
        emit_blocktype(script, $3);
    }
    func_body KW_END opt_label_end
    {
        emit_byte(script, 0x0B);
        pop_label();
    }
    ;

loop_plain:
    KW_LOOP opt_label blocktype {
        push_label($2);
        emit_byte(script, 0x03);
        emit_blocktype(script, $3);
    }
    func_body KW_END opt_label_end
    {
        emit_byte(script, 0x0B);
        pop_label();
    }
    ;

if_plain:
    KW_IF opt_label blocktype {
        push_label($2);
        emit_byte(script, 0x04);
        emit_blocktype(script, $3);
    }
    func_body opt_else_plain KW_END opt_label_end
    {
        emit_byte(script, 0x0B);
        pop_label();
    }
    ;

opt_else_plain:
    /* empty */
  | KW_ELSE opt_label_end { emit_byte(script, 0x05); } func_body
    ;

opt_label_end:
    /* empty */
  | ID
  | ATOM
    ;

/* -----------------------------------------------------------------------
 * br_table — plain form
 * --------------------------------------------------------------------- */

br_table_plain:
    KW_BR_TABLE {
        g_brtable_count = 0;
    }
    br_table_all_labels {
        /* last element is default target, rest are table entries */
        uint32_t def = (g_brtable_count > 0) ? g_brtable_labels[--g_brtable_count] : 0;
        emit_byte(script, 0x0E);
        emit_leb_u32(script, (uint32_t)g_brtable_count);
        for (int i = 0; i < g_brtable_count; i++)
            emit_leb_u32(script, g_brtable_labels[i]);
        emit_leb_u32(script, def);
    }
    ;

br_table_all_labels:
    any_idx {
        if (g_brtable_count < 256)
            g_brtable_labels[g_brtable_count++] = resolve_label($1);
    }
  | br_table_all_labels any_idx {
        if (g_brtable_count < 256)
            g_brtable_labels[g_brtable_count++] = resolve_label($2);
    }
    ;

/* -----------------------------------------------------------------------
 * call_indirect — plain form
 * --------------------------------------------------------------------- */

call_indirect_plain:
    KW_CALL_INDIRECT opt_table_idx typeuse {
        emit_byte(script, 0x11);
        emit_leb_u32(script, (uint32_t)$3); /* type index */
        emit_leb_u32(script, (uint32_t)$2); /* table index */
    }
    ;

opt_table_idx:
    /* empty */  { $$ = 0; }
  | any_idx      { $$ = (int)resolve_func($1); /* reuse numeric parse */ }
    ;

typeuse:
    /* empty */                                   { $$ = 0; }
  | typeuse_items                                 { $$ = $1; }
    ;

typeuse_items:
    typeuse_item                                  { $$ = $1; }
  | typeuse_items typeuse_item                    { $$ = $2 >= 0 ? $2 : $1; }
    ;

typeuse_item:
    LPAREN KW_TYPE any_idx RPAREN                 { $$ = (int)resolve_type($3); }
  | LPAREN KW_PARAM inline_param_list RPAREN      { $$ = -1; }
  | LPAREN KW_RESULT inline_result_list RPAREN    { $$ = -1; }
    ;

inline_typeuse:
    LPAREN KW_PARAM inline_param_list RPAREN
  | LPAREN KW_RESULT inline_result_list RPAREN
    ;

inline_param_list:
    /* empty */
  | inline_param_list valtype
    ;

inline_result_list:
    /* empty */
  | inline_result_list valtype
    ;

/* -----------------------------------------------------------------------
 * Folded instructions
 * --------------------------------------------------------------------- */

fold_instr:
    LPAREN KW_UNREACHABLE RPAREN { emit_byte(script,0x00); }
  | LPAREN KW_NOP RPAREN         { emit_byte(script,0x01); }
  | LPAREN KW_RETURN   fold_arg_list RPAREN { emit_byte(script,0x0F); }
  | LPAREN KW_DROP     fold_arg_list RPAREN { emit_byte(script,0x1A); }
  | LPAREN KW_SELECT   fold_arg_list RPAREN { emit_byte(script,0x1B); }

  /* locals / globals */
  | LPAREN KW_LOCAL_GET  any_idx RPAREN { emit_byte(script,0x20); emit_leb_u32(script,resolve_local($3)); }
  | LPAREN KW_LOCAL_SET  any_idx fold_arg_list RPAREN { emit_byte(script,0x21); emit_leb_u32(script,resolve_local($3)); }
  | LPAREN KW_LOCAL_TEE  any_idx fold_arg_list RPAREN { emit_byte(script,0x22); emit_leb_u32(script,resolve_local($3)); }
  | LPAREN KW_GLOBAL_GET any_idx RPAREN               { emit_byte(script,0x23); emit_index_ref(script,IDX_GLOBAL,$3,@3.first_line,@3.first_column); }
  | LPAREN KW_GLOBAL_SET any_idx fold_arg_list RPAREN { emit_byte(script,0x24); emit_index_ref(script,IDX_GLOBAL,$3,@3.first_line,@3.first_column); }

  /* memory */
  | LPAREN KW_MEMORY_SIZE RPAREN                       { emit_byte(script,0x3F); emit_byte(script,0x00); }
  | LPAREN KW_MEMORY_GROW fold_arg_list RPAREN         { emit_byte(script,0x40); emit_byte(script,0x00); }
  | LPAREN KW_MEMORY_INIT any_idx fold_arg_list RPAREN {
        emit_byte(script,0xFC); emit_leb_u32(script,8);
        emit_index_ref(script,IDX_ELEM,$3,@3.first_line,@3.first_column);
        emit_byte(script,0x00);
    }
  | LPAREN KW_MEMORY_COPY fold_arg_list RPAREN {
        emit_byte(script,0xFC); emit_leb_u32(script,10);
        emit_byte(script,0x00); emit_byte(script,0x00);
    }
  | LPAREN KW_MEMORY_FILL fold_arg_list RPAREN {
        emit_byte(script,0xFC); emit_leb_u32(script,11); emit_byte(script,0x00);
    }

  /* table */
  | LPAREN KW_TABLE_GET  any_idx fold_arg_list RPAREN {
        emit_byte(script,0x25); emit_index_ref(script,IDX_TABLE,$3,@3.first_line,@3.first_column);
    }
  | LPAREN KW_TABLE_SET  any_idx fold_arg_list RPAREN {
        emit_byte(script,0x26); emit_index_ref(script,IDX_TABLE,$3,@3.first_line,@3.first_column);
    }
  | LPAREN KW_TABLE_GROW any_idx fold_arg_list RPAREN {
        emit_byte(script,0xFC); emit_leb_u32(script,15);
        emit_index_ref(script,IDX_TABLE,$3,@3.first_line,@3.first_column);
    }
  | LPAREN KW_TABLE_SIZE any_idx RPAREN {
        emit_byte(script,0xFC); emit_leb_u32(script,16);
        emit_index_ref(script,IDX_TABLE,$3,@3.first_line,@3.first_column);
    }
  | LPAREN KW_TABLE_INIT any_idx any_idx fold_arg_list RPAREN {
        emit_byte(script,0xFC); emit_leb_u32(script,12);
        emit_index_ref(script,IDX_ELEM,$3,@3.first_line,@3.first_column);
        emit_index_ref(script,IDX_TABLE,$4,@4.first_line,@4.first_column);
    }
  | LPAREN KW_TABLE_COPY any_idx any_idx fold_arg_list RPAREN {
        emit_byte(script,0xFC); emit_leb_u32(script,14);
        emit_index_ref(script,IDX_TABLE,$3,@3.first_line,@3.first_column);
        emit_index_ref(script,IDX_TABLE,$4,@4.first_line,@4.first_column);
    }
  | LPAREN KW_TABLE_FILL any_idx fold_arg_list RPAREN {
        emit_byte(script,0xFC); emit_leb_u32(script,17);
        emit_index_ref(script,IDX_TABLE,$3,@3.first_line,@3.first_column);
    }

  /* control — br / call */
  | LPAREN KW_BR    any_idx fold_arg_list RPAREN { emit_byte(script,0x0C); emit_leb_u32(script,resolve_label($3)); }
  | LPAREN KW_BR_IF any_idx fold_arg_list RPAREN { emit_byte(script,0x0D); emit_leb_u32(script,resolve_label($3)); }
  | LPAREN KW_CALL  any_idx fold_arg_list RPAREN { emit_byte(script,0x10); emit_func_ref(script,$3,@3.first_line,@3.first_column); }

  /* call_indirect folded */
  | LPAREN KW_CALL_INDIRECT opt_table_idx typeuse fold_arg_list RPAREN {
        emit_byte(script,0x11);
        emit_leb_u32(script,(uint32_t)$4); /* type index */
        emit_leb_u32(script,(uint32_t)$3); /* table index */
    }

  /* br_table folded */
  | LPAREN KW_BR_TABLE {
        g_brtable_count = 0;
    } br_table_all_labels fold_arg_list RPAREN {
        uint32_t def = (g_brtable_count > 0) ? g_brtable_labels[--g_brtable_count] : 0;
        emit_byte(script,0x0E);
        emit_leb_u32(script,(uint32_t)g_brtable_count);
        for (int i = 0; i < g_brtable_count; i++)
            emit_leb_u32(script, g_brtable_labels[i]);
        emit_leb_u32(script, def);
    }

  /* ref instructions */
  | LPAREN KW_REF_NULL  reftype RPAREN        { emit_byte(script,0xD0); emit_byte(script,(uint8_t)$3); }
  | LPAREN KW_REF_IS_NULL fold_arg_list RPAREN{ emit_byte(script,0xD1); }
  | LPAREN KW_REF_FUNC  any_idx RPAREN        { emit_byte(script,0xD2); emit_func_ref(script,$3,@3.first_line,@3.first_column); }

  /* consts */
  | LPAREN KW_I32_CONST any_int RPAREN { emit_byte(script,0x41); emit_leb_s32(script,(int32_t)$3); }
  | LPAREN KW_I64_CONST any_int RPAREN { emit_byte(script,0x42); emit_leb_s64(script,(int64_t)$3); }

  | LPAREN KW_F32_CONST f32_imm RPAREN
  | LPAREN KW_F64_CONST f64_imm RPAREN

  /* block / loop / if folded */
  | fold_block
  | fold_loop
  | fold_if

  /* SIMD */
  | LPAREN KW_V128_CONST lane_type lane_vals RPAREN {
        wasm_value v = lanes_to_v128($3, &$4, script);
        emit_byte(script, 0xFD);
        emit_leb_u32(script, 12);
        for (int i = 0; i < 16; i++) emit_byte(script, v.v128.bytes[i]);
    }
  | LPAREN SIMD_OP fold_arg_list RPAREN {
        emit_byte(script, 0xFD);
        emit_leb_u32(script, $2);
    }

  /* Memory load/store with explicit memarg (non-empty: offset= or align=) */
  | LPAREN ATOM memarg_nonempty fold_arg_list RPAREN {
        uint8_t op = memop_by_name($2);
        if (op) {
            uint32_t align_log2 = (uint32_t)($3 >> 32);
            uint32_t offset     = (uint32_t)($3 & 0xFFFFFFFF);
            if (align_log2 == 0 && offset == 0) align_log2 = default_align(op);
            emit_byte(script, op);
            emit_leb_u32(script, align_log2);
            emit_leb_u32(script, offset);
        } else {
            emit_atom_op(script, $2);
        }
    }

  /* SIMD lane ops: (atom INT... args...) — one or more integer immediates */
  | LPAREN ATOM {
        g_lane_imm_count = 0;
    } lane_imm_list fold_arg_list RPAREN {
        emit_atom_op(script, $2);
        for (int _i = 0; _i < g_lane_imm_count; _i++)
            emit_leb_u32(script, g_lane_imms[_i]);
    }

  /* SIMD lane load/store with memarg before lane index(es) */
  | LPAREN ATOM memarg_nonempty {
        g_lane_imm_count = 0;
    } lane_imm_list fold_arg_list RPAREN {
        emit_atom_op(script, $2);
        for (int _i = 0; _i < g_lane_imm_count; _i++)
            emit_leb_u32(script, g_lane_imms[_i]);
    }

  /* Multi-memory: (atom INT memarg INT... args...) — memidx + memarg + lane */
  | LPAREN ATOM any_int memarg_nonempty {
        g_lane_imm_count = 0;
    } lane_imm_list fold_arg_list RPAREN {
        emit_atom_op(script, $2);
        for (int _i = 0; _i < g_lane_imm_count; _i++)
            emit_leb_u32(script, g_lane_imms[_i]);
    }

  /* Multi-memory: (atom ATOM/ID INT... args...) — named memidx + lane (with or without memarg) */
  | LPAREN ATOM ATOM {
        g_lane_imm_count = 0;
    } lane_imm_list fold_arg_list RPAREN {
        emit_atom_op(script, $2);
        for (int _i = 0; _i < g_lane_imm_count; _i++)
            emit_leb_u32(script, g_lane_imms[_i]);
    }
  | LPAREN ATOM ID {
        g_lane_imm_count = 0;
    } lane_imm_list fold_arg_list RPAREN {
        emit_atom_op(script, $2);
        for (int _i = 0; _i < g_lane_imm_count; _i++)
            emit_leb_u32(script, g_lane_imms[_i]);
    }
  | LPAREN ATOM ATOM memarg_nonempty {
        g_lane_imm_count = 0;
    } lane_imm_list fold_arg_list RPAREN {
        emit_atom_op(script, $2);
        for (int _i = 0; _i < g_lane_imm_count; _i++)
            emit_leb_u32(script, g_lane_imms[_i]);
    }
  | LPAREN ATOM ID memarg_nonempty {
        g_lane_imm_count = 0;
    } lane_imm_list fold_arg_list RPAREN {
        emit_atom_op(script, $2);
        for (int _i = 0; _i < g_lane_imm_count; _i++)
            emit_leb_u32(script, g_lane_imms[_i]);
    }

  /* Generic ATOM opcode (no explicit memarg — uses defaults) */
  | LPAREN ATOM fold_arg_list RPAREN {
        uint8_t op = memop_by_name($2);
        if (op) {
            emit_byte(script, op);
            emit_leb_u32(script, default_align(op));
            emit_leb_u32(script, 0);
        } else {
            emit_atom_op(script, $2);
        }
    }
    ;

/* f32/f64 immediates in folded const form */
f32_imm:
    FLOAT   { emit_byte(script,0x43); emit_f32(script,(float)$1); }
  | any_int { emit_byte(script,0x43); uint32_t b=(uint32_t)(uint64_t)$1; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_NAN_CANONICAL { emit_byte(script,0x43); uint32_t b=0x7FC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_NAN_ARITHMETIC{ emit_byte(script,0x43); uint32_t b=0x7FC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_NEG_NAN       { emit_byte(script,0x43); uint32_t b=0xFFC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_POS_NAN       { emit_byte(script,0x43); uint32_t b=0x7FC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_NAN           { emit_byte(script,0x43); uint32_t b=0x7FC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_INF           { emit_byte(script,0x43); float f=1.0f/0.0f;  emit_f32(script,f); }
  | KW_POS_INF       { emit_byte(script,0x43); float f=1.0f/0.0f;  emit_f32(script,f); }
  | KW_NEG_INF       { emit_byte(script,0x43); float f=-1.0f/0.0f; emit_f32(script,f); }
    ;

f64_imm:
    FLOAT   { emit_byte(script,0x44); emit_f64(script,$1); }
  | any_int { emit_byte(script,0x44); double d; uint64_t b=(uint64_t)$1; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_NAN_CANONICAL { emit_byte(script,0x44); uint64_t b=0x7FF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_NAN_ARITHMETIC{ emit_byte(script,0x44); uint64_t b=0x7FF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_NEG_NAN       { emit_byte(script,0x44); uint64_t b=0xFFF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_POS_NAN       { emit_byte(script,0x44); uint64_t b=0x7FF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_NAN           { emit_byte(script,0x44); uint64_t b=0x7FF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_INF           { emit_byte(script,0x44); double d=1.0/0.0;  emit_f64(script,d); }
  | KW_POS_INF       { emit_byte(script,0x44); double d=1.0/0.0;  emit_f64(script,d); }
  | KW_NEG_INF       { emit_byte(script,0x44); double d=-1.0/0.0; emit_f64(script,d); }
    ;

/* -----------------------------------------------------------------------
 * Folded block / loop / if
 * --------------------------------------------------------------------- */

fold_block:
    LPAREN KW_BLOCK opt_label blocktype {
        push_label($3);
        emit_byte(script, 0x02);
        emit_blocktype(script, $4);
    }
    func_body RPAREN {
        emit_byte(script, 0x0B);
        pop_label();
    }
    ;

fold_loop:
    LPAREN KW_LOOP opt_label blocktype {
        push_label($3);
        emit_byte(script, 0x03);
        emit_blocktype(script, $4);
    }
    func_body RPAREN {
        emit_byte(script, 0x0B);
        pop_label();
    }
    ;

fold_if:
    LPAREN KW_IF opt_label blocktype fold_arg_list {
        push_label($3);
        emit_byte(script, 0x04);
        emit_blocktype(script, $4);
    }
    LPAREN KW_THEN func_body RPAREN
    opt_fold_else
    RPAREN {
        emit_byte(script, 0x0B);
        pop_label();
    }
    ;

opt_fold_else:
    /* empty */
  | LPAREN KW_ELSE opt_label_end { emit_byte(script, 0x05); } func_body RPAREN
    ;

/* -----------------------------------------------------------------------
 * fold_arg_list — list of folded instrs used as arguments
 * --------------------------------------------------------------------- */

fold_arg_list:
    /* empty */
  | fold_arg_list fold_instr
    ;

/* -----------------------------------------------------------------------
 * Shared helper types
 * --------------------------------------------------------------------- */

opt_label:
    /* empty */ { $$[0] = '\0'; }
  | ID          { snprintf($$, WAST_MAX_EXPORT_NAME, "%s", $1); }
  | ATOM        { snprintf($$, WAST_MAX_EXPORT_NAME, "%s", $1); }
    ;

blocktype:
    /* empty */                           { $$ = -1; /* void */ }
  | blocktype_items                       { $$ = $1; }
    ;

blocktype_items:
    blocktype_item                        { $$ = $1; }
  | blocktype_items blocktype_item        { $$ = $2 >= 0 ? $2 : $1; }
    ;

blocktype_item:
    LPAREN KW_RESULT valtype RPAREN             { $$ = (int)$3; }
  | LPAREN KW_PARAM inline_param_list RPAREN    { $$ = -1; }
  | LPAREN KW_TYPE any_idx RPAREN               { $$ = -1; (void)$3; }
    ;

reftype:
    KW_FUNCREF    { $$ = 0x70; }
  | KW_EXTERNREF  { $$ = 0x6F; }
  | KW_FUNC       { $$ = 0x70; }
  | KW_EXTERN     { $$ = 0x6F; }
  | KW_REF_EXTERN { $$ = 0x6F; }
  | LPAREN KW_REF_TYPE KW_NULL KW_FUNC RPAREN { $$ = 0x70; }
  | LPAREN KW_REF_TYPE KW_NULL KW_EXTERN RPAREN { $$ = 0x6F; }
    ;

reftype_as_valtype:
    KW_FUNCREF   { $$ = WASM_VALTYPE_FUNCREF; }
  | KW_EXTERNREF { $$ = WASM_VALTYPE_EXTERNREF; }
    ;

table_reftype:
    KW_FUNCREF   { $$ = WASM_VALTYPE_FUNCREF; }
  | KW_EXTERNREF { $$ = WASM_VALTYPE_EXTERNREF; }
  | KW_FUNC      { $$ = WASM_VALTYPE_FUNCREF; }
  | KW_EXTERN    { $$ = WASM_VALTYPE_EXTERNREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_FUNC RPAREN { $$ = WASM_VALTYPE_FUNCREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_EXTERN RPAREN { $$ = WASM_VALTYPE_EXTERNREF; }
  | LPAREN KW_REF_TYPE KW_FUNC RPAREN { $$ = WASM_VALTYPE_FUNCREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_EXTERN RPAREN { $$ = WASM_VALTYPE_EXTERNREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_NULL any_idx RPAREN { $$ = indexed_ref_type(script,$4,1); }
  | LPAREN KW_REF_TYPE any_idx RPAREN { $$ = indexed_ref_type(script,$3,0); }
    ;

any_idx:
    INT    { snprintf($$, WAST_MAX_EXPORT_NAME, "%lld", (long long)$1); }
  | HEXINT { snprintf($$, WAST_MAX_EXPORT_NAME, "%llu", (unsigned long long)(uint64_t)$1); }
  | ID     { snprintf($$, WAST_MAX_EXPORT_NAME, "%s", $1); }
  | ATOM   { snprintf($$, WAST_MAX_EXPORT_NAME, "%s", $1); }
    ;

any_int:
    INT    { $$ = $1; }
  | HEXINT { $$ = $1; }
    ;

any_nat:
    INT    { $$ = ($1 < 0) ? 0 : $1; }
  | HEXINT { $$ = $1; }
    ;

memarg:
    /* empty */              { $$ = 0ULL; }
  | memarg_nonempty          { $$ = $1; }
    ;

memarg_nonempty:
    OFFSET_IMM               { $$ = (uint64_t)$1; }
  | ALIGN_IMM                { $$ = ((uint64_t)$1 << 32); }
  | OFFSET_IMM ALIGN_IMM     { $$ = (uint64_t)$1 | ((uint64_t)$2 << 32); }
  | ALIGN_IMM  OFFSET_IMM    { $$ = (uint64_t)$2 | ((uint64_t)$1 << 32); }
    ;

valtype:
    KW_I32  { $$ = WASM_VALTYPE_I32; }
  | KW_I64  { $$ = WASM_VALTYPE_I64; }
  | KW_F32  { $$ = WASM_VALTYPE_F32; }
  | KW_F64  { $$ = WASM_VALTYPE_F64; }
  | KW_V128 { $$ = WASM_VALTYPE_V128; }
  | KW_FUNCREF   { $$ = WASM_VALTYPE_FUNCREF; }
  | KW_EXTERNREF { $$ = WASM_VALTYPE_EXTERNREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_FUNC RPAREN { $$ = WASM_VALTYPE_FUNCREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_EXTERN RPAREN { $$ = WASM_VALTYPE_EXTERNREF; }
  | LPAREN KW_REF_TYPE KW_FUNC RPAREN { $$ = WASM_VALTYPE_FUNCREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_EXTERN RPAREN { $$ = WASM_VALTYPE_EXTERNREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_NULL any_idx RPAREN { $$ = indexed_ref_type(script, $4, 1); }
  | LPAREN KW_REF_TYPE any_idx RPAREN { $$ = indexed_ref_type(script, $3, 0); }
    ;

/* -----------------------------------------------------------------------
 * Module section fields
 * --------------------------------------------------------------------- */

/* --- type --- */
type_item:
    LPAREN KW_TYPE opt_id {
        /* start a fake func to collect params/results */
        memset(&g_cur_func, 0, sizeof(g_cur_func));
        g_cur_func.type_index = -1;
        g_in_func = 1; /* collect the signature using the function helpers */
        snprintf(g_cur_func.id, WAST_MAX_EXPORT_NAME, "%s", $3);
    }
    LPAREN KW_FUNC param_list result_list RPAREN RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->type_count < WAST_MAX_TYPES) {
            wast_type *t = &mod->types[mod->type_count++];
            memset(t, 0, sizeof(*t));
            t->param_count  = g_cur_func.param_count;
            t->result_count = g_cur_func.result_count;
            for (int i = 0; i < t->param_count;  i++) t->params[i]  = g_cur_func.params[i];
            for (int i = 0; i < t->result_count; i++) t->results[i] = g_cur_func.results[i];
            snprintf(t->id, WAST_MAX_EXPORT_NAME, "%s", g_cur_func.id);
            if (g_type_name_count < WAST_MAX_TYPES)
                snprintf(g_type_names[g_type_name_count++], WAST_MAX_EXPORT_NAME, "%s", t->id);
        }
        g_in_func = 0;
    }
    ;

/* --- import (top-level) --- */
import_item:
    LPAREN KW_IMPORT STRING STRING {
        snprintf(g_import_module, WAST_MAX_EXPORT_NAME, "%s", $3);
        snprintf(g_import_name,   WAST_MAX_EXPORT_NAME, "%s", $4);
    }
    import_desc RPAREN
    ;

import_desc:
    LPAREN KW_FUNC {
        memset(&g_cur_func, 0, sizeof(g_cur_func));
        g_cur_func.type_index = -1;
        g_in_func = 1; /* collect the imported function id and signature */
        g_cur_func_index = (uint32_t)cur_group(script)->module.func_count;
        g_local_name_count = 0;
        snprintf(g_cur_func.import_module, WAST_MAX_EXPORT_NAME, "%s", g_import_module);
        snprintf(g_cur_func.import_name,   WAST_MAX_EXPORT_NAME, "%s", g_import_name);
        g_cur_func.is_import = 1;
    }
    opt_id func_typeuse param_list result_list RPAREN {
        g_in_func = 1; /* trick commit_func into registering it */
        commit_func(script);
    }
  | LPAREN KW_MEMORY opt_id limits RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->memory_count < WAST_MAX_MEMORIES) {
            wast_memory *m = &mod->memories[mod->memory_count++];
            memset(m, 0, sizeof(*m));
            m->limits = $4;
            snprintf(m->import_module, WAST_MAX_EXPORT_NAME, "%s", g_import_module);
            snprintf(m->import_name,   WAST_MAX_EXPORT_NAME, "%s", g_import_name);
            m->is_import = 1;
            snprintf(m->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_memory_name_count < WAST_MAX_MEMORIES) snprintf(g_memory_names[g_memory_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  | LPAREN KW_GLOBAL opt_id global_type RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->global_count < WAST_MAX_GLOBALS) {
            wast_global *g = &mod->globals[mod->global_count++];
            *g = g_cur_global;
            snprintf(g->import_module, WAST_MAX_EXPORT_NAME, "%s", g_import_module);
            snprintf(g->import_name,   WAST_MAX_EXPORT_NAME, "%s", g_import_name);
            g->is_import = 1;
            snprintf(g->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_global_name_count < WAST_MAX_GLOBALS)
                snprintf(g_global_names[g_global_name_count++], WAST_MAX_EXPORT_NAME, "%s", $3);
        }
    }
  | LPAREN KW_TABLE opt_id limits table_reftype RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->table_count < WAST_MAX_TABLES) {
            wast_table *t = &mod->tables[mod->table_count++];
            memset(t, 0, sizeof(*t));
            t->limits = $4;
            t->reftype = $5;
            snprintf(t->import_module, WAST_MAX_EXPORT_NAME, "%s", g_import_module);
            snprintf(t->import_name,   WAST_MAX_EXPORT_NAME, "%s", g_import_name);
            t->is_import = 1;
            snprintf(t->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_table_name_count < WAST_MAX_TABLES) snprintf(g_table_names[g_table_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
    ;

/* --- memory --- */
memory_item:
    LPAREN KW_MEMORY opt_id LPAREN KW_IMPORT STRING STRING RPAREN limits RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->memory_count < WAST_MAX_MEMORIES) {
            wast_memory *m = &mod->memories[mod->memory_count++];
            memset(m, 0, sizeof(*m));
            m->limits = $9;
            snprintf(m->import_module, WAST_MAX_EXPORT_NAME, "%s", $6);
            snprintf(m->import_name,   WAST_MAX_EXPORT_NAME, "%s", $7);
            m->is_import = 1;
            snprintf(m->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_memory_name_count < WAST_MAX_MEMORIES) snprintf(g_memory_names[g_memory_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  | LPAREN KW_MEMORY opt_id LPAREN KW_EXPORT STRING RPAREN limits RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->memory_count < WAST_MAX_MEMORIES) {
            wast_memory *m = &mod->memories[mod->memory_count++];
            memset(m, 0, sizeof(*m));
            m->limits = $8;
            snprintf(m->export_name, WAST_MAX_EXPORT_NAME, "%s", $6);
            snprintf(m->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_memory_name_count < WAST_MAX_MEMORIES) snprintf(g_memory_names[g_memory_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  | LPAREN KW_MEMORY opt_id limits RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->memory_count < WAST_MAX_MEMORIES) {
            wast_memory *m = &mod->memories[mod->memory_count++];
            memset(m, 0, sizeof(*m));
            m->limits = $4;
            snprintf(m->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_memory_name_count < WAST_MAX_MEMORIES) snprintf(g_memory_names[g_memory_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  | LPAREN KW_MEMORY opt_id LPAREN KW_DATA string_list RPAREN RPAREN {
        /* inline data memory — treat as memory with default limits */
        wast_module *mod = &cur_group(script)->module;
        if (mod->memory_count < WAST_MAX_MEMORIES) {
            wast_memory *m = &mod->memories[mod->memory_count++];
            memset(m, 0, sizeof(*m));
            m->limits.min = 1; m->limits.max = 1; m->limits.has_max = 1;
            snprintf(m->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_memory_name_count < WAST_MAX_MEMORIES) snprintf(g_memory_names[g_memory_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
    ;

limits:
    any_nat           { $$.min=(uint32_t)$1; $$.max=0;           $$.has_max=0; $$.is_shared=0; }
  | any_nat any_nat   { $$.min=(uint32_t)$1; $$.max=(uint32_t)$2; $$.has_max=1; $$.is_shared=0; }
    ;

/* --- global --- */
global_item:
    LPAREN KW_GLOBAL opt_id LPAREN KW_IMPORT STRING STRING RPAREN global_type RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->global_count < WAST_MAX_GLOBALS) {
            wast_global *g = &mod->globals[mod->global_count++];
            *g = g_cur_global;
            snprintf(g->import_module, WAST_MAX_EXPORT_NAME, "%s", $6);
            snprintf(g->import_name,   WAST_MAX_EXPORT_NAME, "%s", $7);
            g->is_import = 1;
            snprintf(g->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_global_name_count < WAST_MAX_GLOBALS)
                snprintf(g_global_names[g_global_name_count++], WAST_MAX_EXPORT_NAME, "%s", $3);
        }
    }
  | LPAREN KW_GLOBAL opt_id global_export_list global_type global_init RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->global_count < WAST_MAX_GLOBALS) {
            wast_global *g = &mod->globals[mod->global_count++];
            *g = g_cur_global;
            snprintf(g->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_global_name_count < WAST_MAX_GLOBALS)
                snprintf(g_global_names[g_global_name_count++], WAST_MAX_EXPORT_NAME, "%s", $3);
        }
    }
    ;

global_export_list:
    /* empty */ { g_global_export_name[0] = '\0'; }
  | global_export_list LPAREN KW_EXPORT STRING RPAREN {
        snprintf(g_global_export_name, WAST_MAX_EXPORT_NAME, "%s", $4);
    }
    ;

global_type:
    valtype {
        memset(&g_cur_global, 0, sizeof(g_cur_global));
        g_cur_global.valtype     = $1;
        g_cur_global.is_mutable  = 0;
        snprintf(g_cur_global.export_name, WAST_MAX_EXPORT_NAME, "%s", g_global_export_name);
    }
  | LPAREN KW_MUT valtype RPAREN {
        memset(&g_cur_global, 0, sizeof(g_cur_global));
        g_cur_global.valtype     = $3;
        g_cur_global.is_mutable  = 1;
        snprintf(g_cur_global.export_name, WAST_MAX_EXPORT_NAME, "%s", g_global_export_name);
    }
    ;

global_init:
    LPAREN KW_I32_CONST any_int RPAREN {
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x41);
        emit_init_leb_s32(g_cur_global.init_expr, &g_cur_global.init_len, 32, (int32_t)$3);
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x0B);
    }
  | LPAREN KW_I64_CONST any_int RPAREN {
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x42);
        emit_init_leb_s64(g_cur_global.init_expr, &g_cur_global.init_len, 32, (int64_t)$3);
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x0B);
    }
  | LPAREN KW_F32_CONST FLOAT RPAREN {
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x43);
        float f = (float)$3;
        uint8_t buf[4]; memcpy(buf,&f,4);
        for (int i=0;i<4;i++) emit_init_byte(g_cur_global.init_expr,&g_cur_global.init_len,32,buf[i]);
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x0B);
    }
  | LPAREN KW_F32_CONST any_int RPAREN {
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x43);
        float f = (float)$3;
        uint8_t buf[4]; memcpy(buf,&f,4);
        for (int i=0;i<4;i++) emit_init_byte(g_cur_global.init_expr,&g_cur_global.init_len,32,buf[i]);
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x0B);
    }
  | LPAREN KW_F64_CONST FLOAT RPAREN {
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x44);
        uint8_t buf[8]; memcpy(buf,&$3,8);
        for (int i=0;i<8;i++) emit_init_byte(g_cur_global.init_expr,&g_cur_global.init_len,32,buf[i]);
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x0B);
    }
  | LPAREN KW_F64_CONST any_int RPAREN {
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x44);
        double d = (double)$3;
        uint8_t buf[8]; memcpy(buf,&d,8);
        for (int i=0;i<8;i++) emit_init_byte(g_cur_global.init_expr,&g_cur_global.init_len,32,buf[i]);
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x0B);
    }
  | LPAREN KW_REF_NULL reftype RPAREN {
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0xD0);
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, (uint8_t)$3);
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x0B);
    }
  | LPAREN KW_REF_NULL any_idx RPAREN {
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0xD0);
        emit_init_leb_s32(g_cur_global.init_expr, &g_cur_global.init_len, 32,
                          indexed_heap_type(script,$3,@3.first_line,@3.first_column));
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x0B);
    }
  | LPAREN KW_REF_FUNC any_idx RPAREN {
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0xD2);
        emit_global_init_ref(script,IDX_FUNC,$3,@3.first_line,@3.first_column);
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x0B);
    }
  | LPAREN KW_GLOBAL_GET any_idx RPAREN {
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x23);
        emit_global_init_ref(script,IDX_GLOBAL,$3,@3.first_line,@3.first_column);
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x0B);
    }
  | LPAREN KW_V128_CONST lane_type lane_vals RPAREN {
        /* v128.const global init — emit FD 0C + 16 bytes + 0B */
        wasm_value v = lanes_to_v128($3, &$4, script);
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0xFD);
        /* LEB128 for 12 (v128.const prefix) */
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x0C);
        for (int i = 0; i < 16; i++)
            emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, v.v128.bytes[i]);
        emit_init_byte(g_cur_global.init_expr, &g_cur_global.init_len, 32, 0x0B);
    }
    ;

/* --- table --- */
table_item:
    LPAREN KW_TABLE opt_id LPAREN KW_IMPORT STRING STRING RPAREN limits table_reftype RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->table_count < WAST_MAX_TABLES) {
            wast_table *t = &mod->tables[mod->table_count++];
            memset(t, 0, sizeof(*t));
            t->limits = $9;
            t->reftype = $10;
            snprintf(t->import_module, WAST_MAX_EXPORT_NAME, "%s", $6);
            snprintf(t->import_name,   WAST_MAX_EXPORT_NAME, "%s", $7);
            t->is_import = 1;
            snprintf(t->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_table_name_count < WAST_MAX_TABLES) snprintf(g_table_names[g_table_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  | LPAREN KW_TABLE opt_id LPAREN KW_EXPORT STRING RPAREN limits table_reftype RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->table_count < WAST_MAX_TABLES) {
            wast_table *t = &mod->tables[mod->table_count++];
            memset(t, 0, sizeof(*t));
            t->limits = $8;
            t->reftype = $9;
            snprintf(t->export_name, WAST_MAX_EXPORT_NAME, "%s", $6);
            snprintf(t->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_table_name_count < WAST_MAX_TABLES) snprintf(g_table_names[g_table_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  | LPAREN KW_TABLE opt_id limits table_reftype RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->table_count < WAST_MAX_TABLES) {
            wast_table *t = &mod->tables[mod->table_count++];
            memset(t, 0, sizeof(*t));
            t->limits = $4;
            t->reftype = $5;
            snprintf(t->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_table_name_count < WAST_MAX_TABLES) snprintf(g_table_names[g_table_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  | LPAREN KW_TABLE opt_id table_reftype LPAREN KW_ELEM {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.reftype = $4;
    } elem_func_list RPAREN RPAREN {
        /* Shorthand defines both the table and an active segment at offset 0. */
        wast_module *mod = &cur_group(script)->module;
        if (mod->table_count < WAST_MAX_TABLES) {
            wast_table *t = &mod->tables[mod->table_count++];
            memset(t, 0, sizeof(*t));
            t->limits.min = (uint32_t)g_cur_elem.ref_count;
            t->reftype = g_cur_elem.reftype;
            snprintf(t->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_table_name_count < WAST_MAX_TABLES) snprintf(g_table_names[g_table_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
        if (mod->elem_count < WAST_MAX_ELEM_SEGS) {
            g_cur_elem.table_index = 0;
            g_cur_elem.offset_expr[0] = 0x41;
            g_cur_elem.offset_expr[1] = 0x00;
            g_cur_elem.offset_expr[2] = 0x0B;
            g_cur_elem.offset_len = 3;
            mod->elem[mod->elem_count++] = g_cur_elem;
            if (g_elem_name_count < WAST_MAX_ELEM_SEGS) g_elem_names[g_elem_name_count++][0]='\0';
        }
    }
    ;

/* --- data --- */
data_item:
    LPAREN KW_DATA opt_id data_offset string_list RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->data_count < WAST_MAX_DATA_SEGS)
            mod->data[mod->data_count++] = g_cur_data;
    }
  | LPAREN KW_DATA opt_id string_list RPAREN {
        /* passive data segment */
        g_cur_data.is_passive = 1;
        wast_module *mod = &cur_group(script)->module;
        if (mod->data_count < WAST_MAX_DATA_SEGS)
            mod->data[mod->data_count++] = g_cur_data;
    }
    ;

data_offset:
    LPAREN KW_OFFSET_KW LPAREN KW_I32_CONST any_int RPAREN RPAREN {
        memset(&g_cur_data, 0, sizeof(g_cur_data));
        g_cur_data.is_passive   = 0;
        g_cur_data.memory_index = 0;
        emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, 0x41);
        emit_init_leb_s32(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, (int32_t)$5);
        emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, 0x0B);
    }
  | LPAREN KW_I32_CONST any_int RPAREN {
        /* shorthand offset form */
        memset(&g_cur_data, 0, sizeof(g_cur_data));
        g_cur_data.is_passive   = 0;
        g_cur_data.memory_index = 0;
        emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, 0x41);
        emit_init_leb_s32(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, (int32_t)$3);
        emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, 0x0B);
    }
    ;

/* One or more integer immediates (lane indices for SIMD ops) */
lane_imm_list:
    any_int {
        if (g_lane_imm_count < 32) g_lane_imms[g_lane_imm_count++] = (uint32_t)$1;
    }
  | lane_imm_list any_int {
        if (g_lane_imm_count < 32) g_lane_imms[g_lane_imm_count++] = (uint32_t)$2;
    }
    ;

string_list:
    /* empty */
  | string_list STRING {
        if (!g_cur_data.bytes) {
            g_cur_data.bytes = (uint8_t*)malloc(WAST_MAX_DATA_BYTES);
            g_cur_data.len   = 0;
        }
        if (g_cur_data.bytes) {
            const char *src = $2;
            while (*src && g_cur_data.len < WAST_MAX_DATA_BYTES - 1) {
                uint8_t byte;
                if (*src == '\\') {
                    src++;
                    if (!*src) break;
                    if (*src == 't')       { byte = 0x09; src++; }
                    else if (*src == 'n')  { byte = 0x0a; src++; }
                    else if (*src == 'r')  { byte = 0x0d; src++; }
                    else if (*src == '"')  { byte = 0x22; src++; }
                    else if (*src == '\'') { byte = 0x27; src++; }
                    else if (*src == '\\') { byte = 0x5c; src++; }
                    else if (src[1] && ((unsigned char)*src <= '9' ? (*src >= '0') : ((*src|0x20) >= 'a' && (*src|0x20) <= 'f')) &&
                                       ((unsigned char)src[1] <= '9' ? (src[1] >= '0') : ((src[1]|0x20) >= 'a' && (src[1]|0x20) <= 'f'))) {
                        unsigned char hi = (unsigned char)*src++;
                        unsigned char lo = (unsigned char)*src++;
                        byte = (uint8_t)(((hi <= '9' ? hi - '0' : (hi|0x20) - 'a' + 10) << 4) |
                                          (lo <= '9' ? lo - '0' : (lo|0x20) - 'a' + 10));
                    } else { byte = (uint8_t)*src++; }
                } else {
                    byte = (uint8_t)*src++;
                }
                g_cur_data.bytes[g_cur_data.len++] = byte;
            }
        }
    }
    ;

/* --- elem --- */
elem_item:
    LPAREN KW_ELEM opt_id elem_offset elem_func_list RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->elem_count < WAST_MAX_ELEM_SEGS) {
            mod->elem[mod->elem_count++] = g_cur_elem;
            if (g_elem_name_count < WAST_MAX_ELEM_SEGS) snprintf(g_elem_names[g_elem_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  | LPAREN KW_ELEM opt_id KW_DECLARE KW_FUNC {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.is_declarative = 1;
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
    } elem_funcs RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->elem_count < WAST_MAX_ELEM_SEGS) {
            mod->elem[mod->elem_count++] = g_cur_elem;
            if (g_elem_name_count < WAST_MAX_ELEM_SEGS)
                snprintf(g_elem_names[g_elem_name_count++], WAST_MAX_EXPORT_NAME, "%s", $3);
        }
    }
  | LPAREN KW_ELEM opt_id KW_DECLARE reftype {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.is_declarative = 1;
        g_cur_elem.reftype = ($5 == 0x6F) ? WASM_VALTYPE_EXTERNREF : WASM_VALTYPE_FUNCREF;
    } elem_item_list RPAREN {
        /* declarative element segment */
        wast_module *mod = &cur_group(script)->module;
        if (mod->elem_count < WAST_MAX_ELEM_SEGS) {
            mod->elem[mod->elem_count++] = g_cur_elem;
            if (g_elem_name_count < WAST_MAX_ELEM_SEGS) snprintf(g_elem_names[g_elem_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  | LPAREN KW_ELEM opt_id reftype {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.is_passive = 1;
        g_cur_elem.reftype = ($4 == 0x6F) ? WASM_VALTYPE_EXTERNREF : WASM_VALTYPE_FUNCREF;
    } elem_item_list RPAREN {
        /* passive element segment */
        wast_module *mod = &cur_group(script)->module;
        if (mod->elem_count < WAST_MAX_ELEM_SEGS) {
            mod->elem[mod->elem_count++] = g_cur_elem;
            if (g_elem_name_count < WAST_MAX_ELEM_SEGS) snprintf(g_elem_names[g_elem_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  | LPAREN KW_ELEM opt_id RPAREN {
        wast_module *mod=&cur_group(script)->module;
        if(mod->elem_count<WAST_MAX_ELEM_SEGS){memset(&g_cur_elem,0,sizeof(g_cur_elem));g_cur_elem.is_passive=1;g_cur_elem.reftype=WASM_VALTYPE_FUNCREF;mod->elem[mod->elem_count++]=g_cur_elem;
            if(g_elem_name_count<WAST_MAX_ELEM_SEGS)snprintf(g_elem_names[g_elem_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);}
    }
    ;
    ;

elem_offset:
    LPAREN KW_OFFSET_KW LPAREN KW_I32_CONST any_int RPAREN RPAREN {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.table_index = 0;
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x41);
        emit_init_leb_s32(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, (int32_t)$5);
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x0B);
    }
  | LPAREN KW_I32_CONST any_int RPAREN {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.table_index = 0;
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x41);
        emit_init_leb_s32(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, (int32_t)$3);
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x0B);
    }
  | LPAREN KW_TABLE any_idx LPAREN KW_OFFSET_KW LPAREN KW_I32_CONST any_int RPAREN RPAREN RPAREN {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.table_index = (int)meta_index_ref(script,META_ELEM_TABLE,IDX_TABLE,
            (uint32_t)cur_group(script)->module.elem_count,0,$3,@3.first_line,@3.first_column);
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x41);
        emit_init_leb_s32(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, (int32_t)$8);
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x0B);
    }
    ;

elem_func_list:
    /* empty */
  | KW_FUNC elem_funcs
  | elem_funcs
    ;

elem_funcs:
    any_idx {
        append_elem_func_ref(script,$1,@1.first_line,@1.first_column);
    }
  | elem_funcs any_idx {
        append_elem_func_ref(script,$2,@2.first_line,@2.first_column);
    }
    ;

elem_item_list:
    /* empty */
  | elem_item_list LPAREN KW_ITEM LPAREN KW_REF_FUNC any_idx RPAREN RPAREN {
        append_elem_func_ref(script,$6,@6.first_line,@6.first_column);
    }
  | elem_item_list LPAREN KW_REF_FUNC any_idx RPAREN {
        append_elem_func_ref(script,$4,@4.first_line,@4.first_column);
    }
  | elem_item_list LPAREN KW_REF_NULL reftype RPAREN {
        if (g_cur_elem.ref_count < WAST_MAX_ELEM_REFS)
            g_cur_elem.refs[g_cur_elem.ref_count++] = UINT32_MAX; /* null */
    }
    ;

/* --- export (standalone) --- */
export_item:
    LPAREN KW_EXPORT STRING export_desc RPAREN {
        ensure_group(script);
        wast_module *mod = &cur_group(script)->module;
        if (mod->export_count < WAST_MAX_EXPORTS) {
            wast_export *e = &mod->exports[mod->export_count++];
            snprintf(e->name, WAST_MAX_EXPORT_NAME, "%s", $3);
            e->kind = g_export_kind;
            e->index = g_export_index;
        }
    }
    ;

export_desc:
    LPAREN KW_FUNC any_idx RPAREN {
        g_export_kind = 0; g_export_index = meta_index_ref(script,META_EXPORT,IDX_FUNC,
            (uint32_t)cur_group(script)->module.export_count,0,$3,@3.first_line,@3.first_column);
    }
  | LPAREN KW_TABLE any_idx RPAREN {
        g_export_kind = 1; g_export_index = meta_index_ref(script,META_EXPORT,IDX_TABLE,
            (uint32_t)cur_group(script)->module.export_count,0,$3,@3.first_line,@3.first_column);
    }
  | LPAREN KW_MEMORY any_idx RPAREN {
        g_export_kind = 2; g_export_index = meta_index_ref(script,META_EXPORT,IDX_MEMORY,
            (uint32_t)cur_group(script)->module.export_count,0,$3,@3.first_line,@3.first_column);
    }
  | LPAREN KW_GLOBAL any_idx RPAREN {
        g_export_kind = 3; g_export_index = meta_index_ref(script,META_EXPORT,IDX_GLOBAL,
            (uint32_t)cur_group(script)->module.export_count,0,$3,@3.first_line,@3.first_column);
    }
    ;

/* --- start --- */
start_item:
    LPAREN KW_START any_idx RPAREN {
        ensure_group(script);
        cur_group(script)->module.start_func = (int)meta_index_ref(script,META_START,IDX_FUNC,0,0,$3,@3.first_line,@3.first_column);
    }
    ;

/* -----------------------------------------------------------------------
 * Assertions
 * --------------------------------------------------------------------- */

assert_cmd:
    LPAREN KW_ASSERT_RETURN {
        memset(&g_cur_assert, 0, sizeof(g_cur_assert));
        g_cur_assert.kind = WAST_ASSERT_RETURN;
        g_in_assert = 1;
    }
    action result_spec RPAREN {
        ensure_group(script);
        append_assert(script);
    }
  | LPAREN KW_ASSERT_TRAP {
        memset(&g_cur_assert, 0, sizeof(g_cur_assert));
        g_cur_assert.kind = WAST_ASSERT_TRAP;
        g_in_assert = 1;
        g_module_assert_action = 0;
    }
    action STRING RPAREN {
        if (g_module_assert_action) {
            wast_group *group = cur_group(script);
            group->has_module_assertion = 1;
            group->module_assert_kind = WAST_ASSERT_TRAP;
            snprintf(group->expected_module_error, WAST_MAX_EXPORT_NAME, "%s", $5);
            g_in_assert = 0;
        } else {
            snprintf(g_cur_assert.expected_trap, WAST_MAX_EXPORT_NAME, "%s", $5);
            ensure_group(script);
            append_assert(script);
        }
    }
  | LPAREN KW_ASSERT_EXHAUSTION {
        memset(&g_cur_assert, 0, sizeof(g_cur_assert));
        g_cur_assert.kind = WAST_ASSERT_EXHAUSTION;
        g_in_assert = 1;
    }
    action STRING RPAREN {
        ensure_group(script);
        append_assert(script);
    }
  | LPAREN KW_ASSERT_INVALID {
        g_in_assert = 1;
        begin_module(script);
    }
    LPAREN KW_MODULE opt_module_id module_fields RPAREN STRING RPAREN {
        apply_func_fixups(script);
        wast_group *group = cur_group(script);
        group->has_module_assertion = 1;
        group->module_assert_kind = WAST_ASSERT_INVALID;
        snprintf(group->expected_module_error, WAST_MAX_EXPORT_NAME, "%s", $9);
        g_in_assert = 0;
    }
  | LPAREN KW_ASSERT_INVALID LPAREN KW_MODULE ATOM string_list RPAREN STRING RPAREN {
        /* assert_invalid (module quote "..." ...) "msg" — skip */
    }
  | LPAREN KW_ASSERT_INVALID LPAREN KW_MODULE ATOM string_list RPAREN RPAREN {
        /* assert_invalid (module quote "..." ...) — skip */
    }
  | LPAREN KW_ASSERT_MALFORMED {
        g_in_assert = 1;
        begin_module(script);
    }
    LPAREN KW_MODULE opt_module_id module_fields RPAREN STRING RPAREN {
        apply_func_fixups(script);
        wast_group *group = cur_group(script);
        group->has_module_assertion = 1;
        group->module_assert_kind = WAST_ASSERT_MALFORMED;
        snprintf(group->expected_module_error, WAST_MAX_EXPORT_NAME, "%s", $9);
        g_in_assert = 0;
    }
  | LPAREN KW_ASSERT_MALFORMED LPAREN KW_MODULE ATOM string_list RPAREN STRING RPAREN {
        /* assert_malformed (module quote "..." ...) "msg" — skip, we can't re-parse WAT strings */
    }
  | LPAREN KW_ASSERT_MALFORMED LPAREN KW_MODULE ATOM string_list RPAREN RPAREN {
        /* assert_malformed (module quote "..." ...) — skip */
    }
  | LPAREN KW_ASSERT_UNLINKABLE {
        g_in_assert = 1;
        begin_module(script);
    }
    LPAREN KW_MODULE opt_module_id module_fields RPAREN STRING RPAREN {
        apply_func_fixups(script);
        wast_group *group = cur_group(script);
        group->has_module_assertion = 1;
        group->module_assert_kind = WAST_ASSERT_UNLINKABLE;
        snprintf(group->expected_module_error, WAST_MAX_EXPORT_NAME, "%s", $9);
        g_in_assert = 0;
    }
    ;

action:
    LPAREN KW_INVOKE STRING {
        strncpy(g_invoke_name, $3, WAST_MAX_EXPORT_NAME - 1);
        g_invoke_name[WAST_MAX_EXPORT_NAME - 1] = '\0';
        g_in_assert = 1;
    }
    const_list RPAREN
  | LPAREN KW_INVOKE ID STRING {
        snprintf(g_cur_assert.module_id, WAST_MAX_EXPORT_NAME, "%s", $3);
        snprintf(g_invoke_name, WAST_MAX_EXPORT_NAME, "%s", $4);
        g_in_assert = 1;
    }
    const_list RPAREN
  | LPAREN KW_GET STRING RPAREN {
        g_cur_assert.action_kind = WAST_ACTION_GET;
        strncpy(g_invoke_name, $3, WAST_MAX_EXPORT_NAME - 1);
        g_invoke_name[WAST_MAX_EXPORT_NAME - 1] = '\0';
    }
  | LPAREN KW_GET ID STRING RPAREN {
        g_cur_assert.action_kind = WAST_ACTION_GET;
        snprintf(g_cur_assert.module_id, WAST_MAX_EXPORT_NAME, "%s", $3);
        snprintf(g_invoke_name, WAST_MAX_EXPORT_NAME, "%s", $4);
    }
  | LPAREN KW_MODULE {
        begin_module(script);
        g_module_assert_action = 1;
    } opt_module_id module_fields RPAREN {
        apply_func_fixups(script);
    }
    ;

const_list:
    /* empty */
  | const_list const_val {
        if (g_in_assert && g_cur_assert.arg_count < WAST_MAX_ARGS)
            g_cur_assert.args[g_cur_assert.arg_count++] = $2;
    }
    ;

result_spec:
    /* empty */
  | result_const {
        g_cur_assert.result_count    = 1;
        g_cur_assert.alt_count       = 1;
        g_cur_assert.alternatives[0][0] = $1;
    }
  | result_const result_consts {
        /* multi-value result — first in alternatives[0][0], rest in [0][1..] */
        g_cur_assert.alternatives[0][0] = $1;
    }
  | LPAREN KW_EITHER either_alts RPAREN
    ;

result_consts:
    result_const {
        if (g_cur_assert.result_count < WAST_MAX_RESULTS)
            g_cur_assert.alternatives[0][g_cur_assert.result_count++] = $1;
    }
  | result_consts result_const {
        if (g_cur_assert.result_count < WAST_MAX_RESULTS)
            g_cur_assert.alternatives[0][g_cur_assert.result_count++] = $2;
    }
    ;

either_alts:
    /* empty */
  | either_alts result_const {
        int idx = g_cur_assert.alt_count;
        if (idx < WAST_MAX_ALTERNATIVES) {
            g_cur_assert.alternatives[idx][0] = $2;
            g_cur_assert.result_count = 1;
            g_cur_assert.alt_count++;
        }
    }
    ;

result_const:
    const_val { $$ = $1; }
    ;

/* -----------------------------------------------------------------------
 * Const values (for assertions)
 * --------------------------------------------------------------------- */

const_val:
    LPAREN KW_I32_CONST any_int RPAREN {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_I32;
        $$.i32  = (int32_t)$3;
    }
  | LPAREN KW_I64_CONST any_int RPAREN {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_I64;
        $$.i64  = (int64_t)$3;
    }
  | LPAREN KW_F32_CONST f32_val RPAREN { $$ = $3; }
  | LPAREN KW_F64_CONST f64_val RPAREN { $$ = $3; }
  | LPAREN KW_V128_CONST lane_type lane_vals RPAREN {
        $$ = lanes_to_v128($3, &$4, script);
    }
  | LPAREN KW_REF_NULL reftype RPAREN {
        memset(&$$, 0, sizeof($$));
        $$.type = ($3 == 0x70) ? WASM_VALTYPE_FUNCREF : WASM_VALTYPE_EXTERNREF;
        $$.ref  = UINT32_MAX; /* null */
    }
  | LPAREN KW_REF_FUNC any_int RPAREN {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_FUNCREF;
        $$.ref  = (uint32_t)$3;
    }
  | LPAREN KW_REF_EXTERN any_int RPAREN {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_EXTERNREF;
        $$.ref  = (uint32_t)$3;
    }
    ;

/* -----------------------------------------------------------------------
 * f32 / f64 value rules (for const_val in assertions)
 * --------------------------------------------------------------------- */

f32_val:
    FLOAT {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F32;
        $$.f32  = (float)$1;
    }
  | any_int {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F32;
        uint32_t bits = (uint32_t)(uint64_t)$1;
        memcpy(&$$.f32, &bits, 4);
    }
  | KW_NAN_CANONICAL {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F32;
        uint32_t b = 0x7FC00000u; memcpy(&$$.f32, &b, 4);
        $$.nan_mode[0] = NAN_MATCH_F32_CANON;
    }
  | KW_NAN_ARITHMETIC {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F32;
        uint32_t b = 0x7FC00000u; memcpy(&$$.f32, &b, 4);
        $$.nan_mode[0] = NAN_MATCH_F32_ARITH;
    }
  | KW_NEG_NAN {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F32;
        uint32_t b = 0xFFC00000u; memcpy(&$$.f32, &b, 4);
        $$.nan_mode[0] = NAN_MATCH_F32_ARITH;
    }
  | KW_POS_NAN {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F32;
        uint32_t b = 0x7FC00000u; memcpy(&$$.f32, &b, 4);
        $$.nan_mode[0] = NAN_MATCH_F32_ARITH;
    }
  | KW_NAN {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F32;
        uint32_t b = 0x7FC00000u; memcpy(&$$.f32, &b, 4);
        $$.nan_mode[0] = NAN_MATCH_F32_ARITH;
    }
  | KW_INF {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F32;
        $$.f32  = 1.0f / 0.0f;
    }
  | KW_POS_INF {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F32;
        $$.f32  = 1.0f / 0.0f;
    }
  | KW_NEG_INF {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F32;
        $$.f32  = -1.0f / 0.0f;
    }
  | ATOM {
        /* nan:0xHEX or other NaN payload forms — treat as arithmetic NaN */
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F32;
        uint32_t b = 0x7FC00000u; memcpy(&$$.f32, &b, 4);
        $$.nan_mode[0] = NAN_MATCH_F32_ARITH;
    }
    ;

f64_val:
    FLOAT {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F64;
        $$.f64  = $1;
    }
  | any_int {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F64;
        uint64_t bits = (uint64_t)$1;
        memcpy(&$$.f64, &bits, 8);
    }
  | KW_NAN_CANONICAL {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F64;
        uint64_t b = 0x7FF8000000000000ULL; memcpy(&$$.f64, &b, 8);
        $$.nan_mode[0] = NAN_MATCH_F64_CANON;
    }
  | KW_NAN_ARITHMETIC {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F64;
        uint64_t b = 0x7FF8000000000000ULL; memcpy(&$$.f64, &b, 8);
        $$.nan_mode[0] = NAN_MATCH_F64_ARITH;
    }
  | KW_NEG_NAN {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F64;
        uint64_t b = 0xFFF8000000000000ULL; memcpy(&$$.f64, &b, 8);
        $$.nan_mode[0] = NAN_MATCH_F64_ARITH;
    }
  | KW_POS_NAN {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F64;
        uint64_t b = 0x7FF8000000000000ULL; memcpy(&$$.f64, &b, 8);
        $$.nan_mode[0] = NAN_MATCH_F64_ARITH;
    }
  | KW_NAN {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F64;
        uint64_t b = 0x7FF8000000000000ULL; memcpy(&$$.f64, &b, 8);
        $$.nan_mode[0] = NAN_MATCH_F64_ARITH;
    }
  | KW_INF {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F64;
        $$.f64  = 1.0 / 0.0;
    }
  | KW_POS_INF {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F64;
        $$.f64  = 1.0 / 0.0;
    }
  | KW_NEG_INF {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F64;
        $$.f64  = -1.0 / 0.0;
    }
  | ATOM {
        /* nan:0xHEX or other NaN payload forms — treat as arithmetic NaN */
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_F64;
        uint64_t b = 0x7FF8000000000000ULL; memcpy(&$$.f64, &b, 8);
        $$.nan_mode[0] = NAN_MATCH_F64_ARITH;
    }
    ;

/* -----------------------------------------------------------------------
 * SIMD lane infrastructure (unchanged from original)
 * --------------------------------------------------------------------- */

lane_type:
    KW_I8X16 { $$ = 0; }
  | KW_I16X8 { $$ = 1; }
  | KW_I32X4 { $$ = 2; }
  | KW_I64X2 { $$ = 3; }
  | KW_F32X4 { $$ = 4; }
  | KW_F64X2 { $$ = 5; }
    ;

lane_vals:
    /* empty */ { memset(&$$, 0, sizeof($$)); }
  | lane_vals lane_val {
        $$ = $1;
        int idx = $$.count;
        if (idx < MAX_LANE_COUNT) {
            $$.vals[idx]  = $2.vals[0];
            $$.ivals[idx] = $2.ivals[0];
            $$.flags[idx] = $2.flags[0];
            $$.count++;
        }
    }
    ;

lane_val:
    INT {
        memset(&$$, 0, sizeof($$));
        $$.ivals[0] = $1;
        $$.vals[0]  = (double)$1;
        $$.count    = 1;
    }
  | HEXINT {
        memset(&$$, 0, sizeof($$));
        $$.ivals[0] = $1;
        $$.vals[0]  = (double)(uint64_t)$1;
        $$.count    = 1;
    }
  | FLOAT {
        memset(&$$, 0, sizeof($$));
        $$.vals[0]  = $1;
        $$.ivals[0] = (int64_t)$1;
        $$.count    = 1;
    }
  | KW_NAN_CANONICAL {
        memset(&$$, 0, sizeof($$));
        $$.flags[0] = 1;
        $$.count    = 1;
    }
  | KW_NAN_ARITHMETIC {
        memset(&$$, 0, sizeof($$));
        $$.flags[0] = 2;
        $$.count    = 1;
    }
  | KW_NEG_NAN {
        memset(&$$, 0, sizeof($$));
        $$.flags[0] = 3;
        $$.count    = 1;
    }
  | KW_NEG_INF {
        memset(&$$, 0, sizeof($$));
        $$.vals[0]  = -1.0 / 0.0;
        $$.count    = 1;
    }
  | KW_POS_NAN {
        memset(&$$, 0, sizeof($$));
        $$.flags[0] = 4;
        $$.count    = 1;
    }
  | KW_POS_INF {
        memset(&$$, 0, sizeof($$));
        $$.vals[0]  = 1.0 / 0.0;
        $$.count    = 1;
    }
  | KW_NAN {
        memset(&$$, 0, sizeof($$));
        $$.flags[0] = 4;
        $$.count    = 1;
    }
  | KW_INF {
        memset(&$$, 0, sizeof($$));
        $$.vals[0]  = 1.0 / 0.0;
        $$.count    = 1;
    }
  | ATOM {
        /* nan:0xHEX or other atom forms — treat as arithmetic NaN */
        memset(&$$, 0, sizeof($$));
        $$.flags[0] = 2; /* NAN_MATCH_F32_ARITH */
        $$.count    = 1;
    }
    ;

/* -----------------------------------------------------------------------
 * register command
 * --------------------------------------------------------------------- */

register_cmd:
    LPAREN KW_REGISTER STRING RPAREN {
        ensure_group(script);
        snprintf(cur_group(script)->module.register_name, WAST_MAX_EXPORT_NAME, "%s", $3);
    }
  | LPAREN KW_REGISTER STRING ID RPAREN {
        ensure_group(script);
        set_register_name(script, $3, $4);
    }
  | LPAREN KW_REGISTER STRING ATOM RPAREN {
        ensure_group(script);
        set_register_name(script, $3, $4);
    }
    ;

%%

/* -----------------------------------------------------------------------
 * yyerror
 * --------------------------------------------------------------------- */

void yyerror(YYLTYPE *loc, wast_script *script, void *scanner, const char *msg) {
    (void)scanner;
    if (script->error[0] == '\0')
        snprintf(script->error, sizeof(script->error), "parse error at %d:%d: %s",
                 loc->first_line, loc->first_column, msg);
}

/* -----------------------------------------------------------------------
 * lanes_to_v128 (unchanged from original)
 * --------------------------------------------------------------------- */

static wasm_value lanes_to_v128(int lane_type, const lane_list *lanes, wast_script *script) {
    (void)script;
    wasm_value v;
    memset(&v, 0, sizeof(v));
    v.type = WASM_VALTYPE_V128;

    switch (lane_type) {
        case 0: {
            for (int i = 0; i < 16 && i < lanes->count; i++)
                v.v128.bytes[i] = (uint8_t)(int8_t)lanes->ivals[i];
            break;
        }
        case 1: {
            for (int i = 0; i < 8 && i < lanes->count; i++) {
                uint16_t x = (uint16_t)(int16_t)lanes->ivals[i];
                v.v128.bytes[i*2]   = (uint8_t)(x & 0xFF);
                v.v128.bytes[i*2+1] = (uint8_t)(x >> 8);
            }
            break;
        }
        case 2: {
            for (int i = 0; i < 4 && i < lanes->count; i++) {
                uint32_t x = (uint32_t)(int32_t)lanes->ivals[i];
                v.v128.bytes[i*4]   = (uint8_t)(x & 0xFF);
                v.v128.bytes[i*4+1] = (uint8_t)((x >> 8)  & 0xFF);
                v.v128.bytes[i*4+2] = (uint8_t)((x >> 16) & 0xFF);
                v.v128.bytes[i*4+3] = (uint8_t)(x >> 24);
            }
            break;
        }
        case 3: {
            for (int i = 0; i < 2 && i < lanes->count; i++) {
                uint64_t x = (uint64_t)lanes->ivals[i];
                for (int b = 0; b < 8; b++)
                    v.v128.bytes[i*8+b] = (uint8_t)(x >> (b*8));
            }
            break;
        }
        case 4: {
            for (int i = 0; i < 4 && i < lanes->count; i++) {
                uint32_t bits;
                uint8_t nm = NAN_MATCH_EXACT;
                if      (lanes->flags[i] == 1) { bits = 0x7FC00000u; nm = NAN_MATCH_F32_CANON; }
                else if (lanes->flags[i] == 2) { bits = 0x7FC00000u; nm = NAN_MATCH_F32_ARITH; }
                else if (lanes->flags[i] == 3) { bits = 0xFFC00000u; nm = NAN_MATCH_F32_ARITH; }
                else if (lanes->flags[i] == 4) { bits = 0x7FC00000u; nm = NAN_MATCH_F32_ARITH; }
                else { float f = (float)lanes->vals[i]; memcpy(&bits, &f, 4); }
                v.v128.bytes[i*4]   = (uint8_t)(bits & 0xFF);
                v.v128.bytes[i*4+1] = (uint8_t)((bits >> 8)  & 0xFF);
                v.v128.bytes[i*4+2] = (uint8_t)((bits >> 16) & 0xFF);
                v.v128.bytes[i*4+3] = (uint8_t)(bits >> 24);
                v.nan_mode[i*4] = nm;
            }
            break;
        }
        case 5: {
            for (int i = 0; i < 2 && i < lanes->count; i++) {
                uint64_t bits;
                uint8_t nm = NAN_MATCH_EXACT;
                if      (lanes->flags[i] == 1) { bits = 0x7FF8000000000000ULL; nm = NAN_MATCH_F64_CANON; }
                else if (lanes->flags[i] == 2) { bits = 0x7FF8000000000000ULL; nm = NAN_MATCH_F64_ARITH; }
                else if (lanes->flags[i] == 3) { bits = 0xFFF8000000000000ULL; nm = NAN_MATCH_F64_ARITH; }
                else if (lanes->flags[i] == 4) { bits = 0x7FF8000000000000ULL; nm = NAN_MATCH_F64_ARITH; }
                else { double d = lanes->vals[i]; memcpy(&bits, &d, 8); }
                for (int b = 0; b < 8; b++)
                    v.v128.bytes[i*8+b] = (uint8_t)(bits >> (b*8));
                v.nan_mode[i*8] = nm;
            }
            break;
        }
        default:
            break;
    }
    return v;
}
