%define api.pure true
%define lr.type ielr
%locations
%define parse.error verbose
%expect 24
%debug

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
#include "wast_simd.h"

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
void *yyget_extra(void *scanner);

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
static uint32_t g_type_name_indices[WAST_MAX_TYPES];
static int  g_type_name_count = 0;
static int  g_in_rec_group = 0;
static int  g_parsing_type_definition = 0;
static uint32_t g_rec_group_start = 0;
static char g_table_names[WAST_MAX_TABLES][WAST_MAX_EXPORT_NAME];
static int  g_table_name_count = 0;
static char g_memory_names[WAST_MAX_MEMORIES][WAST_MAX_EXPORT_NAME];
static char g_data_names[WAST_MAX_DATA_SEGS][WAST_MAX_EXPORT_NAME];
static int  g_data_name_count = 0;
static int  g_memory_name_count = 0;
static char g_elem_names[WAST_MAX_ELEM_SEGS][WAST_MAX_EXPORT_NAME];
static int  g_elem_name_count = 0;
static char g_tag_names[WAST_MAX_TAGS][WAST_MAX_EXPORT_NAME];
static int  g_tag_name_count = 0;
static wast_tag g_cur_tag;

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
typedef enum { IDX_FUNC, IDX_TYPE, IDX_GLOBAL, IDX_TABLE, IDX_MEMORY, IDX_ELEM,
               IDX_TAG, IDX_DATA } index_space;
typedef struct {
    index_space space;
    uint32_t func_index, code_offset;
    int line, column;
    char name[WAST_MAX_EXPORT_NAME];
} code_index_fixup;
static code_index_fixup g_code_fixups[WAST_MAX_FUNC_FIXUPS];
static int g_code_fixup_count = 0;
typedef enum {
    META_ELEM_FUNC, META_ELEM_GLOBAL, META_ELEM_TABLE, META_EXPORT, META_START,
    META_GLOBAL_INIT, META_TABLE_INIT
} meta_fixup_kind;
typedef struct {
    meta_fixup_kind kind;
    index_space space;
    uint32_t first, second;
    int line, column;
    char name[WAST_MAX_EXPORT_NAME];
} meta_fixup;
static meta_fixup g_meta_fixups[WAST_MAX_FUNC_FIXUPS];
static int g_meta_fixup_count = 0;

/* Rec-group forward-reference fixups: when parsing type bodies inside a rec
 * group, a reference like (ref $t3) may appear before $t3's type_item has
 * been reached.  We store a patch record and resolve it after all types in
 * the rec group have been committed. */
typedef struct {
    uint32_t type_index;   /* index into mod->types[] that will hold this type */
    int      slot;         /* param/result/field index within the type */
    int      location;     /* 0=param, 1=result, 2=field */
    int      nullable;
    char     name[WAST_MAX_EXPORT_NAME];
} rec_type_fixup;
#define WAST_MAX_REC_FIXUPS 256
static rec_type_fixup g_rec_fixups[WAST_MAX_REC_FIXUPS];
static int g_rec_fixup_count = 0;

/* Assertion state */
static wast_assertion g_cur_assert;
static int            g_in_assert = 0;
static char           g_invoke_name[WAST_MAX_EXPORT_NAME];
static int            g_module_assert_action = 0;

/* br_table scratch buffer.  The core suite deliberately contains a table
 * with 16K entries, so this bound must agree with the native decoder's
 * 0xffff-entry limit rather than the much smaller operand-stack bounds. */
#define WAST_MAX_BR_TABLE_LABELS 65536
static uint32_t g_brtable_labels[WAST_MAX_BR_TABLE_LABELS];
static int      g_brtable_count = 0;
typedef struct {
    uint8_t kind;
    uint32_t tag;
    uint32_t depth;
} parsed_catch;
static parsed_catch g_try_catches[WAST_MAX_TAGS];
static int          g_try_catch_count = 0;
static wasm_valtype g_select_result_types[WAST_MAX_RESULTS];
static int          g_select_result_count = 0;

/* Lane/byte immediate list for SIMD ops (e.g. i8x16.shuffle takes 16 ints) */
static uint32_t g_lane_imms[32];
static int      g_lane_imm_count = 0;

/* Inline type accumulation for call_indirect without explicit (type $t) */
static wasm_valtype g_inline_params[WAST_MAX_PARAMS];
static int          g_inline_param_count = 0;
static wasm_valtype g_inline_results[WAST_MAX_RESULTS];
static int          g_inline_result_count = 0;

/* Pending import strings (set before import_desc is parsed) */
static char g_import_module[WAST_MAX_EXPORT_NAME];
static char g_import_name[WAST_MAX_EXPORT_NAME];
static int  g_export_kind;
static uint32_t g_export_index;
static char g_instance_args[2][WAST_MAX_EXPORT_NAME];
static int  g_instance_arg_count;

/* Global being built */
static wast_global g_cur_global;
static wast_type   g_cur_type;

/* Data segment being built */
static wast_data_seg g_cur_data;

/* Element segment being built */
static wast_elem_seg g_cur_elem;
static uint8_t       *g_constexpr_target = NULL;
static int           *g_constexpr_length = NULL;
static int            g_constexpr_capacity = 0;

typedef enum {
    CONSTEXPR_TYPE_MISMATCH = -1,
    CONSTEXPR_NOT_CONSTANT = 0,
    CONSTEXPR_VALID = 1
} constexpr_check;

/* Block signature accumulator. The OCaml grammar parses typeuse, parameters,
 * and results before handing off to the instruction list. */
static wasm_valtype g_blocktype_params[WAST_MAX_PARAMS];
static int          g_blocktype_param_count = 0;
static wasm_valtype g_blocktype_results[WAST_MAX_RESULTS];
static int          g_blocktype_result_count = 0;
static int          g_blocktype_explicit = -1;
static int          g_typeuse_field_stage = 0;
static int          g_signature_seen_result = 0;

static uint32_t resolve_func(const char *s);
static uint32_t resolve_type(const char *s);
static uint32_t resolve_global(const char *s);
static uint32_t resolve_table(const char *s);
static uint32_t resolve_memory(const char *s);
static uint32_t resolve_data(const char *s);
static uint32_t resolve_elem(const char *s);
static uint32_t resolve_tag(const char *s);

static uint64_t parse_hex_payload(const char *s) {
    uint64_t value = 0;
    while (*s) {
        if (*s != '_') {
            int digit = *s >= '0' && *s <= '9' ? *s - '0' :
                *s >= 'a' && *s <= 'f' ? *s - 'a' + 10 :
                *s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : -1;
            if (digit < 0) break;
            value = (value << 4) | (uint64_t)digit;
        }
        s++;
    }
    return value;
}

static wasm_valtype heap_reftype_to_value_type(int heap_type) {
    if (WASM_VALTYPE_IS_TYPE_REF((wasm_valtype)heap_type))
        return (wasm_valtype)heap_type;
    switch (heap_type) {
        case 0x70: return WASM_VALTYPE_FUNCREF;
        case 0x6f: return WASM_VALTYPE_EXTERNREF;
        case 0x6e: return WASM_VALTYPE_ANYREF;
        case 0x6d: return WASM_VALTYPE_EQREF;
        case 0x6c: return WASM_VALTYPE_I31REF;
        case 0x6b: return WASM_VALTYPE_STRUCTREF;
        case 0x6a: return WASM_VALTYPE_ARRAYREF;
        case 0x69: return WASM_VALTYPE_EXNREF;
        case 0x71: return WASM_VALTYPE_NULLREF;
        case 0x73: return WASM_VALTYPE_NULLFUNCREF;
        case 0x74: return WASM_VALTYPE_NULLEXNREF;
        case 0x72: return WASM_VALTYPE_NULLEXTERNREF;
        default: return WASM_VALTYPE_FUNCREF;
    }
}
static void emit_init_byte(uint8_t *buf, int *len, int maxlen, uint8_t b);
static void emit_init_leb_s32(uint8_t *buf, int *len, int maxlen, int32_t v);
static void emit_global_init_ref(wast_script *script, index_space space,
                                 const char *name, int line, int column);
static void report_validation_error(wast_script *script, const char *message);
static constexpr_check check_global_constexpr(wast_script *script,
                                              const wast_global *initializer);

/* Forward declarations for helpers defined after %% */
static wast_group *cur_group(wast_script *s);

/* Search module type table for a matching function signature, or add one.
 * Used when call_indirect has inline (param)/(result) without (type $t). */
static int resolve_inline_functype(wast_script *script) {
    wast_module *mod = &cur_group(script)->module;
    for (int i = 0; i < mod->type_count; i++) {
        wast_type *t = &mod->types[i];
        if (t->kind != WAST_TYPE_FUNC) continue;
        if (t->param_count != g_inline_param_count) continue;
        if (t->result_count != g_inline_result_count) continue;
        int match = 1;
        for (int j = 0; j < t->param_count && match; j++)
            if (t->params[j] != g_inline_params[j]) match = 0;
        for (int j = 0; j < t->result_count && match; j++)
            if (t->results[j] != g_inline_results[j]) match = 0;
        if (match) return i;
    }
    if (mod->type_count < WAST_MAX_TYPES) {
        wast_type *t = &mod->types[mod->type_count];
        memset(t, 0, sizeof(*t));
        t->kind = WAST_TYPE_FUNC;
        t->is_final = 1;
        t->supertype = -1;
        t->rec_group_start = (uint32_t)mod->type_count;
        t->rec_group_size = 1;
        t->param_count = g_inline_param_count;
        t->result_count = g_inline_result_count;
        for (int j = 0; j < g_inline_param_count; j++)
            t->params[j] = g_inline_params[j];
        for (int j = 0; j < g_inline_result_count; j++)
            t->results[j] = g_inline_results[j];
        int index = mod->type_count++;
        if (g_type_name_count < WAST_MAX_TYPES) {
            g_type_names[g_type_name_count][0] = '\0';
            g_type_name_indices[g_type_name_count++] = (uint32_t)index;
        }
        return index;
    }
    return 0;
}

/* Sentinel range for deferred rec-group forward references.
 * We use type indices WAST_MAX_TYPES - WAST_MAX_REC_FIXUPS .. WAST_MAX_TYPES-1
 * as temporary placeholders.  These are patched in the rec_item closing action
 * before anything else sees them. */
#define REC_FIXUP_SENTINEL_BASE (WAST_MAX_TYPES - WAST_MAX_REC_FIXUPS)

static wasm_valtype indexed_ref_type(wast_script *script, const char *name,
                                     int nullable) {
    uint32_t index = resolve_type(name);
    if (index == UINT32_MAX || index >= WAST_MAX_TYPES) {
        /* Module fields may precede the type section in text; the encoder
         * orders all types before tables/globals/functions in binary.  A
         * forward reference from within a non-recursive type declaration is
         * different and remains invalid. */
        if (g_in_rec_group && name && name[0] == '$' &&
            g_rec_fixup_count < WAST_MAX_REC_FIXUPS) {
            /* Queue a deferred fixup; use a sentinel index so the closing
             * action of rec_item can locate and patch every occurrence. */
            uint32_t sentinel = REC_FIXUP_SENTINEL_BASE +
                                (uint32_t)g_rec_fixup_count;
            rec_type_fixup *f = &g_rec_fixups[g_rec_fixup_count++];
            f->nullable = nullable;
            snprintf(f->name, WAST_MAX_EXPORT_NAME, "%s", name);
            return (wasm_valtype)((nullable ? WASM_VALTYPE_TYPE_REF_NULL_BASE :
                                  WASM_VALTYPE_TYPE_REF_BASE) + sentinel);
        }
        if (!g_in_rec_group &&
            (g_parsing_type_definition || !name || name[0] != '$'))
            report_validation_error(script, "unknown type");
        index = 0;
    } else {
        wast_module *mod = &cur_group(script)->module;
        if (!g_in_rec_group &&
            index >= (uint32_t)mod->type_count &&
            !(g_parsing_type_definition &&
              index == (uint32_t)mod->type_count))
            report_validation_error(script, "unknown type");
    }
    return (wasm_valtype)((nullable ? WASM_VALTYPE_TYPE_REF_NULL_BASE :
                          WASM_VALTYPE_TYPE_REF_BASE) + index);
}

static int32_t indexed_heap_type(wast_script *script, const char *name,
                                 int line, int column) {
    uint32_t index = resolve_type(name);
    if (index == UINT32_MAX || index >= WAST_MAX_TYPES) {
        (void)line; (void)column;
        if (!g_in_rec_group)
            report_validation_error(script, "unknown type");
        return 0;
    } else {
        wast_module *mod = &cur_group(script)->module;
        if (!g_in_rec_group &&
            index >= (uint32_t)mod->type_count &&
            !(g_parsing_type_definition &&
              index == (uint32_t)mod->type_count))
            report_validation_error(script, "unknown type");
    }
    return (int32_t)index;
}

/* -----------------------------------------------------------------------
 * Group helpers
 * --------------------------------------------------------------------- */

static wast_group *cur_group(wast_script *s) {
    if (!s->groups || s->group_count <= 0) return NULL;
    if (g_cur_group < 0 || g_cur_group >= s->group_count) return &s->groups[0];
    return &s->groups[g_cur_group];
}

static int captures_validation_error(wast_script *script) {
    wast_group *group = cur_group(script);
    return group->has_module_assertion &&
           (group->module_assert_kind == WAST_ASSERT_INVALID ||
            group->module_assert_kind == WAST_ASSERT_MALFORMED);
}

static void report_validation_error(wast_script *script, const char *message) {
#ifndef WASTE_FREESTANDING
    if (getenv("WAST_DEBUG_VALIDATION"))
        fprintf(stderr, "parser validation group=%d: %s\n", g_cur_group,
                message);
#endif
    if (captures_validation_error(script)) {
        wast_group *group = cur_group(script);
        if (!group->has_validation_error) {
            group->has_validation_error = 1;
            snprintf(group->validation_error, sizeof(group->validation_error),
                     "%s", message);
        }
    } else if (!script->error[0]) {
        snprintf(script->error, sizeof(script->error), "%s", message);
    }
}

static void start_new_group(wast_script *s);

static void ensure_group(wast_script *s) {
    if (s->group_count == 0) start_new_group(s);
}

static void set_register_name(wast_script *script, const char *name,
                              const char *module_id) {
    wast_group *target = cur_group(script);
    if (module_id && module_id[0])
        snprintf(cur_group(script)->module.register_target,
                 WAST_MAX_EXPORT_NAME, "%s", module_id);
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
    if (s->group_count == s->group_capacity) {
        int next_capacity = s->group_capacity ? s->group_capacity * 2 : 8;
        if (next_capacity < s->group_capacity ||
            (size_t)next_capacity > SIZE_MAX / sizeof(*s->groups)) {
            if (!s->error[0])
                snprintf(s->error, sizeof(s->error),
                         "too many WAST module groups");
            return;
        }
        wast_group *next = realloc(s->groups,
                                   (size_t)next_capacity * sizeof(*next));
        if (!next) {
            if (!s->error[0])
                snprintf(s->error, sizeof(s->error),
                         "out of memory growing WAST module groups");
            return;
        }
        s->groups = next;
        s->group_capacity = next_capacity;
    }
    g_cur_group = s->group_count++;
    wast_group *g = &s->groups[g_cur_group];
    memset(g, 0, sizeof(*g));
    g->module.start_func = -1;
    g->assertion_start = s->assertion_count;
    g->assertion_count = 0;
}

static void attach_raw_module(wast_script *script,
                              wast_raw_module_kind expected_kind) {
    if (script->raw_module_cursor >= script->raw_module_count) {
        if (!script->error[0])
            snprintf(script->error, sizeof(script->error),
                     "missing quoted module payload");
        return;
    }
    wast_raw_module *raw = &script->raw_modules[script->raw_module_cursor++];
    if (raw->kind != expected_kind) {
        if (!script->error[0])
            snprintf(script->error, sizeof(script->error),
                     "quoted module payload kind mismatch");
        return;
    }
    wast_group *group = cur_group(script);
    group->raw_module = *raw;
    raw->bytes = NULL;
    raw->length = 0;
}

static void begin_module(wast_script *script) {
    start_new_group(script);
    g_constexpr_target  = NULL;
    g_constexpr_length  = NULL;
    g_constexpr_capacity = 0;
    g_func_name_count   = 0;
    g_func_fixup_count  = 0;
    g_type_fixup_count  = 0;
    g_code_fixup_count  = 0;
    g_meta_fixup_count  = 0;
    g_global_name_count = 0;
    g_type_name_count   = 0;
    g_table_name_count  = 0;
    g_memory_name_count = 0;
    g_data_name_count   = 0;
    g_elem_name_count   = 0;
    g_tag_name_count    = 0;
    g_parsing_type_definition = 0;
}

static void append_instance_arg(wast_script *script, const char *arg) {
    if (g_instance_arg_count >= 2) {
        report_validation_error(script, "too many module instance identifiers");
        return;
    }
    snprintf(g_instance_args[g_instance_arg_count++], WAST_MAX_EXPORT_NAME,
             "%s", arg);
}

static void finish_module_instance(wast_script *script) {
    wast_module *module = &cur_group(script)->module;
#ifndef WASTE_FREESTANDING
    if (getenv("WAST_DEBUG_INSTANCE"))
        fprintf(stderr, "instance group=%d argc=%d arg0='%s' arg1='%s'\n",
                g_cur_group, g_instance_arg_count, g_instance_args[0],
                g_instance_args[1]);
#endif
    if (g_instance_arg_count == 1) {
        snprintf(module->instance_of, sizeof(module->instance_of), "%s",
                 g_instance_args[0]);
    } else if (g_instance_arg_count == 2) {
        snprintf(module->id, sizeof(module->id), "%s", g_instance_args[0]);
        snprintf(module->instance_of, sizeof(module->instance_of), "%s",
                 g_instance_args[1]);
    } else {
        report_validation_error(script, "module instance requires a definition");
    }
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

static void commit_tag(wast_script *script, const char *id) {
    ensure_group(script);
    wast_module *module = &cur_group(script)->module;
    if (module->tag_count >= WAST_MAX_TAGS) {
        report_validation_error(script, "too many tags");
        return;
    }
    snprintf(g_cur_tag.id, sizeof(g_cur_tag.id), "%s", id ? id : "");
    module->tags[module->tag_count++] = g_cur_tag;
    if (g_tag_name_count < WAST_MAX_TAGS)
        snprintf(g_tag_names[g_tag_name_count++], WAST_MAX_EXPORT_NAME,
                 "%s", g_cur_tag.id);
}

static void append_assert(wast_script *s) {
    ensure_group(s);
    if (s->assertion_count == s->assertion_capacity) {
        int next_capacity = s->assertion_capacity ?
                            s->assertion_capacity * 2 : 64;
        if (next_capacity < s->assertion_capacity ||
            (size_t)next_capacity > SIZE_MAX / sizeof(*s->assertions)) {
            if (!s->error[0])
                snprintf(s->error, sizeof(s->error),
                         "too many WAST assertions");
            g_in_assert = 0;
            return;
        }
        wast_assertion *next = realloc(
            s->assertions, (size_t)next_capacity * sizeof(*next));
        if (!next) {
            if (!s->error[0])
                snprintf(s->error, sizeof(s->error),
                         "out of memory growing WAST assertions");
            g_in_assert = 0;
            return;
        }
        s->assertions = next;
        s->assertion_capacity = next_capacity;
    }
    size_t n = strlen(g_invoke_name);
    if (n >= WAST_MAX_EXPORT_NAME) n = WAST_MAX_EXPORT_NAME - 1;
    memcpy(g_cur_assert.func_name, g_invoke_name, n);
    g_cur_assert.func_name[n] = '\0';
    s->assertions[s->assertion_count] = g_cur_assert;
    s->assertion_count++;
    cur_group(s)->assertion_count++;
    g_in_assert = 0;
}

/* -----------------------------------------------------------------------
 * Binary emit helpers — write into the selected constexpr or function body.
 * --------------------------------------------------------------------- */

static void begin_constexpr(uint8_t *target, int *length, int capacity) {
    g_constexpr_target = target;
    g_constexpr_length = length;
    g_constexpr_capacity = capacity;
    *length = 0;
}

static void end_constexpr(void) {
    g_constexpr_target = NULL;
    g_constexpr_length = NULL;
    g_constexpr_capacity = 0;
}

static void emit_byte(wast_script *script, uint8_t b) {
    if (g_constexpr_target) {
        if (*g_constexpr_length >= g_constexpr_capacity) {
            if (!g_in_assert && script->error[0] == '\0')
                snprintf(script->error, sizeof(script->error),
                         "constant expression too large");
            return;
        }
        g_constexpr_target[(*g_constexpr_length)++] = b;
        return;
    }
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

static void emit_leb_u64(wast_script *script, uint64_t v) {
    do {
        uint8_t b = (uint8_t)(v & UINT64_C(0x7f)); v >>= 7;
        if (v) b |= 0x80;
        emit_byte(script, b);
    } while (v);
}

static void emit_func_ref(wast_script *script, const char *name, int line, int column) {
    if (g_constexpr_target == g_cur_global.init_expr) {
        emit_global_init_ref(script, IDX_FUNC, name, line, column);
        return;
    }
    if (g_constexpr_target) {
        emit_leb_u32(script, resolve_func(name));
        return;
    }
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
        case IDX_TAG: return resolve_tag(name);
        case IDX_DATA: return resolve_data(name);
    }
    return UINT32_MAX;
}

static const char *space_name(index_space space) {
    switch (space) {
        case IDX_FUNC: return "function"; case IDX_TYPE: return "type";
        case IDX_GLOBAL: return "global"; case IDX_TABLE: return "table";
        case IDX_MEMORY: return "memory";
        case IDX_ELEM: return "element segment";
        case IDX_TAG: return "tag";
        case IDX_DATA: return "data segment";
    }
    return "index";
}

static void emit_index_ref(wast_script *script, index_space space, const char *name,
                           int line, int column) {
    if (g_constexpr_target == g_cur_global.init_expr) {
        emit_global_init_ref(script, space, name, line, column);
        return;
    }
    if (g_constexpr_target) {
        emit_leb_u32(script, resolve_space(space, name));
        return;
    }
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
    g_cur_elem.ref_opcodes[g_cur_elem.ref_count]=0xd2;
    g_cur_elem.refs[g_cur_elem.ref_count++]=meta_index_ref(script,META_ELEM_FUNC,IDX_FUNC,elem,slot,name,line,column);
}

static void append_elem_global_ref(wast_script *script, const char *name, int line, int column) {
    if(g_cur_elem.ref_count>=WAST_MAX_ELEM_REFS)return;
    uint32_t slot=(uint32_t)g_cur_elem.ref_count;
    uint32_t elem=(uint32_t)cur_group(script)->module.elem_count;
    g_cur_elem.ref_opcodes[g_cur_elem.ref_count]=0x23;
    g_cur_elem.refs[g_cur_elem.ref_count++]=meta_index_ref(script,META_ELEM_GLOBAL,IDX_GLOBAL,elem,slot,name,line,column);
}

static void append_elem_null_ref(wasm_valtype type) {
    if (g_cur_elem.ref_count >= WAST_MAX_ELEM_REFS) return;
    int slot = g_cur_elem.ref_count++;
    g_cur_elem.ref_opcodes[slot] = 0xd0;
    g_cur_elem.ref_types[slot] = type;
    g_cur_elem.refs[slot] = UINT32_MAX;
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

static void set_table_null_init(wast_table *table, int heap_type) {
    table->has_explicit_init = 1;
    table->init_len = 0;
    emit_init_byte(table->init_expr, &table->init_len, 32, 0xd0);
    emit_init_byte(table->init_expr, &table->init_len, 32,
                   (uint8_t)heap_type);
}

static void set_table_index_init(wast_script *script, wast_table *table,
                                 uint32_t table_index, uint8_t opcode,
                                 index_space space, const char *name,
                                 int line, int column) {
    uint32_t index = resolve_space(space, name);
    table->has_explicit_init = 1;
    table->init_len = 0;
    emit_init_byte(table->init_expr, &table->init_len, 32, opcode);
    if (name && name[0] == '$' && index == UINT32_MAX) {
        uint32_t offset = (uint32_t)table->init_len;
        add_meta_fixup(script, META_TABLE_INIT, space, table_index, offset,
                       name, line, column);
        for (int i = 0; i < 4; i++)
            emit_init_byte(table->init_expr, &table->init_len, 32, 0x80);
        emit_init_byte(table->init_expr, &table->init_len, 32, 0x00);
    } else {
        emit_init_leb_s32(table->init_expr, &table->init_len, 32,
                          (int32_t)index);
    }
}

static void check_duplicate_names(wast_script *script,
                                  char names[][WAST_MAX_EXPORT_NAME],
                                  int count, const char *kind) {
    for (int i = 0; i < count; i++) {
        if (names[i][0] != '$') continue;
        for (int j = 0; j < i; j++) {
            if (strcmp(names[i], names[j]) == 0) {
                char message[256];
                snprintf(message, sizeof(message), "duplicate %s identifier",
                         kind);
                report_validation_error(script, message);
                return;
            }
        }
    }
}

static void apply_func_fixups(wast_script *script) {
    wast_module *module = &cur_group(script)->module;
    check_duplicate_names(script, g_type_names, g_type_name_count, "type");
    check_duplicate_names(script, g_func_names, g_func_name_count, "function");
    check_duplicate_names(script, g_global_names, g_global_name_count, "global");
    check_duplicate_names(script, g_table_names, g_table_name_count, "table");
    check_duplicate_names(script, g_memory_names, g_memory_name_count, "memory");
    check_duplicate_names(script, g_elem_names, g_elem_name_count,
                          "element segment");
    check_duplicate_names(script, g_tag_names, g_tag_name_count, "tag");
    for (int i = 0; i < g_func_fixup_count; i++) {
        func_fixup *fixup = &g_func_fixups[i];
        uint32_t index = resolve_func(fixup->name);
        if (index == UINT32_MAX || fixup->func_index >= (uint32_t)module->func_count ||
            (uint32_t)module->funcs[fixup->func_index].code_len < fixup->code_offset + 5u) {
            char message[256];
            snprintf(message, sizeof(message), "%d:%d: unknown function: %s",
                     fixup->line, fixup->column, fixup->name);
            report_validation_error(script, message);
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
            char message[256];
            snprintf(message, sizeof(message), "%d:%d: unknown type: %s",
                     fixup->line, fixup->column, fixup->name);
            report_validation_error(script, message);
        } else {
            module->funcs[fixup->func_index].type_index = (int)index;
        }
    }
    for (int i=0;i<g_code_fixup_count;i++) {
        code_index_fixup *f=&g_code_fixups[i]; uint32_t index=resolve_space(f->space,f->name);
        if(index==UINT32_MAX||f->func_index>=(uint32_t)module->func_count||
           (uint32_t)module->funcs[f->func_index].code_len<f->code_offset+5u) {
            char message[256];
            snprintf(message,sizeof(message),"%d:%d: unknown %s: %s",
                     f->line,f->column,space_name(f->space),f->name);
            report_validation_error(script,message);
            continue;
        }
        uint8_t*d=module->funcs[f->func_index].code+f->code_offset;
        d[0]=(uint8_t)((index&0x7f)|0x80);d[1]=(uint8_t)(((index>>7)&0x7f)|0x80);
        d[2]=(uint8_t)(((index>>14)&0x7f)|0x80);d[3]=(uint8_t)(((index>>21)&0x7f)|0x80);d[4]=(uint8_t)((index>>28)&0x0f);
    }
    for(int i=0;i<g_meta_fixup_count;i++) {
        meta_fixup*f=&g_meta_fixups[i];uint32_t index=resolve_space(f->space,f->name);
        if(index==UINT32_MAX){
            char message[256];
            snprintf(message,sizeof(message),"%d:%d: unknown %s: %s",
                     f->line,f->column,space_name(f->space),f->name);
            report_validation_error(script,message);
            continue;
        }
        if((f->kind==META_ELEM_FUNC||f->kind==META_ELEM_GLOBAL)&&f->first<(uint32_t)module->elem_count&&f->second<(uint32_t)module->elem[f->first].ref_count)module->elem[f->first].refs[f->second]=index;
        else if(f->kind==META_ELEM_TABLE&&f->first<(uint32_t)module->elem_count)module->elem[f->first].table_index=(int)index;
        else if(f->kind==META_EXPORT&&f->first<(uint32_t)module->export_count)module->exports[f->first].index=index;
        else if(f->kind==META_START)module->start_func=(int)index;
        else if(f->kind==META_GLOBAL_INIT&&f->first<(uint32_t)module->global_count&&f->second+5u<=(uint32_t)module->globals[f->first].init_len){
            uint8_t*d=module->globals[f->first].init_expr+f->second;
            d[0]=(uint8_t)((index&0x7f)|0x80);d[1]=(uint8_t)(((index>>7)&0x7f)|0x80);
            d[2]=(uint8_t)(((index>>14)&0x7f)|0x80);d[3]=(uint8_t)(((index>>21)&0x7f)|0x80);d[4]=(uint8_t)((index>>28)&0x0f);
        } else if(f->kind==META_TABLE_INIT&&f->first<(uint32_t)module->table_count&&f->second+5u<=(uint32_t)module->tables[f->first].init_len){
            uint8_t*d=module->tables[f->first].init_expr+f->second;
            d[0]=(uint8_t)((index&0x7f)|0x80);d[1]=(uint8_t)(((index>>7)&0x7f)|0x80);
            d[2]=(uint8_t)(((index>>14)&0x7f)|0x80);d[3]=(uint8_t)(((index>>21)&0x7f)|0x80);d[4]=(uint8_t)((index>>28)&0x0f);
        }
    }
    if (!script->error[0]) {
        for (int i = 0; i < module->global_count; i++) {
            wast_global *global = &module->globals[i];
            if (global->is_import) continue;
            constexpr_check check = check_global_constexpr(script, global);
            if (check != CONSTEXPR_VALID) {
#ifndef WASTE_FREESTANDING
                if (getenv("WAST_DEBUG_CONSTEXPR")) {
                    fprintf(stderr, "constexpr group=%d global=%d type=%d check=%d types=%d funcs=%d func0_type=%d bytes=",
                            g_cur_group, i, (int)global->valtype, (int)check,
                            module->type_count, module->func_count,
                            module->func_count ? module->funcs[0].type_index : -1);
                    for (int j = 0; j < global->init_len; j++)
                        fprintf(stderr, "%02x", global->init_expr[j]);
                    fputc('\n', stderr);
                }
#endif
                report_validation_error(script,
                    check == CONSTEXPR_NOT_CONSTANT ?
                    "constant expression required" : "type mismatch");
                break;
            }
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

static void emit_fold_float_special(wast_script *script, const char *op, int kind) {
    int is64 = (strcmp(op, "f64.const") == 0);
    if (kind == 2 || kind == 3) {
        if (is64) { double d = (kind == 3) ? -INFINITY : INFINITY; emit_f64(script, d); }
        else { float f = (kind == 3) ? -INFINITY : INFINITY; emit_f32(script, f); }
        return;
    }
    if (is64) {
        uint64_t bits = (kind == 1) ? 0xFFF8000000000000ULL : 0x7FF8000000000000ULL;
        double d; memcpy(&d, &bits, sizeof(d)); emit_f64(script, d);
    } else {
        uint32_t bits = (kind == 1) ? 0xFFC00000u : 0x7FC00000u;
        float f; memcpy(&f, &bits, sizeof(f)); emit_f32(script, f);
    }
}

/* kind: 0=pos NaN, 1=neg NaN, 2=pos inf, 3=neg inf
 * nan_match: NAN_MATCH_EXACT(0), NAN_MATCH_F32_CANON(1), NAN_MATCH_F32_ARITH(2),
 *            NAN_MATCH_F64_CANON(3), NAN_MATCH_F64_ARITH(4) */
static wasm_value folded_special_value(const char *op, int kind) {
    wasm_value v; memset(&v, 0, sizeof(v));
    int is64 = (strcmp(op, "f64.const") == 0);
    if (is64) {
        uint64_t b = (kind == 3) ? 0xFFF0000000000000ULL : (kind >= 2 ? 0x7FF0000000000000ULL : (kind == 1 ? 0xFFF8000000000000ULL : 0x7FF8000000000000ULL));
        memcpy(&v.f64, &b, sizeof(b)); v.type = WASM_VALTYPE_F64;
    } else {
        uint32_t b = (kind == 3) ? 0xFF800000U : (kind >= 2 ? 0x7F800000U : (kind == 1 ? 0xFFC00000U : 0x7FC00000U));
        memcpy(&v.f32, &b, sizeof(b)); v.type = WASM_VALTYPE_F32;
    }
    return v;
}

static wasm_value folded_nan_value(const char *op, int kind, uint8_t nan_match) {
    wasm_value v = folded_special_value(op, kind);
    v.nan_mode[0] = nan_match;
    return v;
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

static int read_init_leb(const uint8_t *bytes, size_t length, size_t *offset,
                         unsigned max_bytes, uint64_t *value) {
    uint64_t result = 0;
    unsigned shift = 0;
    for (unsigned i = 0; i < max_bytes && *offset < length; i++) {
        uint8_t byte = bytes[(*offset)++];
        result |= (uint64_t)(byte & 0x7fu) << shift;
        if ((byte & 0x80u) == 0) {
            *value = result;
            return 1;
        }
        shift += 7;
    }
    return 0;
}

static int read_init_sleb(const uint8_t *bytes, size_t length, size_t *offset,
                          unsigned max_bytes, int64_t *value) {
    uint64_t result = 0;
    unsigned shift = 0;
    uint8_t byte = 0;
    for (unsigned i = 0; i < max_bytes && *offset < length; i++) {
        byte = bytes[(*offset)++];
        result |= (uint64_t)(byte & 0x7fu) << shift;
        shift += 7;
        if ((byte & 0x80u) == 0) {
            if (shift < 64 && (byte & 0x40u))
                result |= UINT64_MAX << shift;
            *value = (int64_t)result;
            return 1;
        }
    }
    return 0;
}

static int constexpr_type_compatible(wast_script *script, wasm_valtype actual,
                                     wasm_valtype expected);

typedef struct {
    uint32_t left_start;
    uint32_t right_start;
} type_group_pair;

typedef struct {
    type_group_pair pairs[WAST_MAX_TYPES];
    int count;
} type_equivalence_state;

static int type_indices_equivalent(const wast_module *module,
                                   uint32_t left_index,
                                   uint32_t right_index,
                                   type_equivalence_state *state);

static int type_value_equivalent(const wast_module *module,
                                 wasm_valtype left, wasm_valtype right,
                                 uint32_t left_start, uint32_t left_size,
                                 uint32_t right_start, uint32_t right_size,
                                 type_equivalence_state *state) {
    if (left == right) return 1;
    if (!WASM_VALTYPE_IS_TYPE_REF(left) ||
        !WASM_VALTYPE_IS_TYPE_REF(right))
        return 0;
    if (((unsigned)left < WASM_VALTYPE_TYPE_REF_BASE) !=
        ((unsigned)right < WASM_VALTYPE_TYPE_REF_BASE))
        return 0;

    uint32_t left_index = WASM_VALTYPE_TYPE_REF_INDEX(left);
    uint32_t right_index = WASM_VALTYPE_TYPE_REF_INDEX(right);
    int left_internal = left_index >= left_start &&
                        left_index - left_start < left_size;
    int right_internal = right_index >= right_start &&
                         right_index - right_start < right_size;
    if (left_internal || right_internal)
        return left_internal && right_internal &&
               left_index - left_start == right_index - right_start;
    return type_indices_equivalent(module, left_index, right_index, state);
}

static int type_indices_equivalent(const wast_module *module,
                                   uint32_t left_index,
                                   uint32_t right_index,
                                   type_equivalence_state *state) {
    if (left_index == right_index) return 1;
    if (left_index >= (uint32_t)module->type_count ||
        right_index >= (uint32_t)module->type_count)
        return 0;

    const wast_type *left = &module->types[left_index];
    const wast_type *right = &module->types[right_index];
    uint32_t left_start = left->rec_group_start;
    uint32_t right_start = right->rec_group_start;
    uint32_t left_size = left->rec_group_size ? left->rec_group_size : 1;
    uint32_t right_size = right->rec_group_size ? right->rec_group_size : 1;
    if (left_size != right_size ||
        left_start >= (uint32_t)module->type_count ||
        right_start >= (uint32_t)module->type_count ||
        left_size > (uint32_t)module->type_count - left_start ||
        right_size > (uint32_t)module->type_count - right_start ||
        left_index - left_start != right_index - right_start)
        return 0;

    for (int i = 0; i < state->count; i++)
        if (state->pairs[i].left_start == left_start &&
            state->pairs[i].right_start == right_start)
            return 1;
    if (state->count >= WAST_MAX_TYPES) return 0;
    state->pairs[state->count].left_start = left_start;
    state->pairs[state->count].right_start = right_start;
    state->count++;

    for (uint32_t member = 0; member < left_size; member++) {
        const wast_type *a = &module->types[left_start + member];
        const wast_type *b = &module->types[right_start + member];
        if (a->kind != b->kind || a->param_count != b->param_count ||
            a->result_count != b->result_count ||
            a->field_count != b->field_count)
            return 0;
        for (int i = 0; i < a->param_count; i++)
            if (!type_value_equivalent(module, a->params[i], b->params[i],
                                       left_start, left_size,
                                       right_start, right_size, state))
                return 0;
        for (int i = 0; i < a->result_count; i++)
            if (!type_value_equivalent(module, a->results[i], b->results[i],
                                       left_start, left_size,
                                       right_start, right_size, state))
                return 0;
        for (int i = 0; i < a->field_count; i++) {
            if (a->field_mutable[i] != b->field_mutable[i] ||
                a->field_packed[i] != b->field_packed[i] ||
                !type_value_equivalent(module, a->fields[i], b->fields[i],
                                       left_start, left_size,
                                       right_start, right_size, state))
                return 0;
        }
    }
    return 1;
}

static int type_index_is_subtype(const wast_module *module,
                                 uint32_t actual, uint32_t expected) {
    for (uint32_t steps = 0; steps <= (uint32_t)module->type_count; steps++) {
        if (actual == expected) return 1;
        if (actual >= (uint32_t)module->type_count) return 0;
        type_equivalence_state state = {0};
        if (type_indices_equivalent(module, actual, expected, &state))
            return 1;
        int32_t parent = module->types[actual].supertype;
        if (parent < 0) return 0;
        actual = (uint32_t)parent;
    }
    return 0;
}

static int constexpr_pop(wast_script *script, wasm_valtype *stack, size_t *depth,
                         wasm_valtype expected) {
    if (*depth == 0 ||
        !constexpr_type_compatible(script, stack[*depth - 1], expected))
        return 0;
    (*depth)--;
    return 1;
}

static int constexpr_push(wasm_valtype *stack, size_t *depth,
                          wasm_valtype type) {
    if (*depth >= 32) return 0;
    stack[(*depth)++] = type;
    return 1;
}

static int constexpr_type_compatible(wast_script *script, wasm_valtype actual,
                                     wasm_valtype expected) {
    if (actual == expected) return 1;
    if (actual == WASM_VALTYPE_NULLFUNCREF) {
        if (expected == WASM_VALTYPE_FUNCREF) return 1;
        if (WASM_VALTYPE_IS_TYPE_REF(expected)) {
            unsigned index = WASM_VALTYPE_TYPE_REF_INDEX(expected);
            wast_module *module = &cur_group(script)->module;
            return index < (unsigned)module->type_count &&
                   module->types[index].kind == WAST_TYPE_FUNC;
        }
    }
    if (actual == WASM_VALTYPE_NULLREF &&
        (expected == WASM_VALTYPE_ANYREF || expected == WASM_VALTYPE_EQREF ||
         expected == WASM_VALTYPE_I31REF || expected == WASM_VALTYPE_STRUCTREF ||
         expected == WASM_VALTYPE_ARRAYREF))
        return 1;
    if (actual == WASM_VALTYPE_NULLEXTERNREF &&
        expected == WASM_VALTYPE_EXTERNREF)
        return 1;
    if (actual == WASM_VALTYPE_NULLEXNREF &&
        expected == WASM_VALTYPE_EXNREF)
        return 1;
    if (actual == WASM_VALTYPE_FUNCREF_NONNULL && expected == WASM_VALTYPE_FUNCREF)
        return 1;
    if (actual == WASM_VALTYPE_EXTERNREF_NONNULL && expected == WASM_VALTYPE_EXTERNREF)
        return 1;
    if (actual == WASM_VALTYPE_EXNREF_NONNULL && expected == WASM_VALTYPE_EXNREF)
        return 1;
    if (actual == WASM_VALTYPE_ANYREF_NONNULL && expected == WASM_VALTYPE_ANYREF)
        return 1;
    if (actual == WASM_VALTYPE_EQREF_NONNULL &&
        (expected == WASM_VALTYPE_EQREF || expected == WASM_VALTYPE_ANYREF))
        return 1;
    if (actual == WASM_VALTYPE_I31REF_NONNULL &&
        (expected == WASM_VALTYPE_I31REF || expected == WASM_VALTYPE_EQREF ||
         expected == WASM_VALTYPE_ANYREF))
        return 1;
    if (actual == WASM_VALTYPE_STRUCTREF_NONNULL &&
        (expected == WASM_VALTYPE_STRUCTREF || expected == WASM_VALTYPE_EQREF ||
         expected == WASM_VALTYPE_ANYREF))
        return 1;
    if (actual == WASM_VALTYPE_ARRAYREF_NONNULL &&
        (expected == WASM_VALTYPE_ARRAYREF || expected == WASM_VALTYPE_EQREF ||
         expected == WASM_VALTYPE_ANYREF))
        return 1;
    if ((actual == WASM_VALTYPE_EQREF || actual == WASM_VALTYPE_EQREF_NONNULL) &&
        expected == WASM_VALTYPE_ANYREF)
        return 1;
    if (actual == WASM_VALTYPE_EQREF_NONNULL &&
        expected == WASM_VALTYPE_ANYREF_NONNULL)
        return 1;
    if ((actual == WASM_VALTYPE_I31REF || actual == WASM_VALTYPE_I31REF_NONNULL) &&
        (expected == WASM_VALTYPE_EQREF || expected == WASM_VALTYPE_ANYREF))
        return 1;
    if (actual == WASM_VALTYPE_I31REF_NONNULL &&
        (expected == WASM_VALTYPE_EQREF_NONNULL ||
         expected == WASM_VALTYPE_ANYREF_NONNULL))
        return 1;
    if ((actual == WASM_VALTYPE_STRUCTREF || actual == WASM_VALTYPE_STRUCTREF_NONNULL) &&
        (expected == WASM_VALTYPE_EQREF || expected == WASM_VALTYPE_ANYREF))
        return 1;
    if (actual == WASM_VALTYPE_STRUCTREF_NONNULL &&
        (expected == WASM_VALTYPE_EQREF_NONNULL ||
         expected == WASM_VALTYPE_ANYREF_NONNULL))
        return 1;
    if ((actual == WASM_VALTYPE_ARRAYREF || actual == WASM_VALTYPE_ARRAYREF_NONNULL) &&
        (expected == WASM_VALTYPE_EQREF || expected == WASM_VALTYPE_ANYREF))
        return 1;
    if (actual == WASM_VALTYPE_ARRAYREF_NONNULL &&
        (expected == WASM_VALTYPE_EQREF_NONNULL ||
         expected == WASM_VALTYPE_ANYREF_NONNULL))
        return 1;
    if (WASM_VALTYPE_IS_TYPE_REF(actual) && !WASM_VALTYPE_IS_TYPE_REF(expected)) {
        wast_module *module = &cur_group(script)->module;
        unsigned index = WASM_VALTYPE_TYPE_REF_INDEX(actual);
        if (index >= (unsigned)module->type_count) return 0;
        wast_type_kind kind = module->types[index].kind;
        int nullable = (unsigned)actual < WASM_VALTYPE_TYPE_REF_BASE;
        if (!nullable && expected == WASM_VALTYPE_FUNCREF_NONNULL)
            return kind == WAST_TYPE_FUNC;
        if (expected == WASM_VALTYPE_FUNCREF)
            return kind == WAST_TYPE_FUNC;
        if (!nullable && expected == WASM_VALTYPE_STRUCTREF_NONNULL)
            return kind == WAST_TYPE_STRUCT;
        if (!nullable && expected == WASM_VALTYPE_ARRAYREF_NONNULL)
            return kind == WAST_TYPE_ARRAY;
        if (expected == WASM_VALTYPE_STRUCTREF)
            return kind == WAST_TYPE_STRUCT;
        if (expected == WASM_VALTYPE_ARRAYREF)
            return kind == WAST_TYPE_ARRAY;
        if (expected == WASM_VALTYPE_EQREF || expected == WASM_VALTYPE_ANYREF)
            return kind == WAST_TYPE_STRUCT || kind == WAST_TYPE_ARRAY;
        if (!nullable && (expected == WASM_VALTYPE_EQREF_NONNULL ||
                          expected == WASM_VALTYPE_ANYREF_NONNULL))
            return kind == WAST_TYPE_STRUCT || kind == WAST_TYPE_ARRAY;
    }
    if (WASM_VALTYPE_IS_TYPE_REF(actual) && WASM_VALTYPE_IS_TYPE_REF(expected)) {
        unsigned actual_index = WASM_VALTYPE_TYPE_REF_INDEX(actual);
        unsigned expected_index = WASM_VALTYPE_TYPE_REF_INDEX(expected);
        type_equivalence_state state = {0};
        const wast_module *module = &cur_group(script)->module;
        return (type_index_is_subtype(module, actual_index, expected_index) ||
                type_indices_equivalent(module, actual_index, expected_index,
                                        &state)) &&
               ((unsigned)actual >= WASM_VALTYPE_TYPE_REF_BASE ||
                (unsigned)expected < WASM_VALTYPE_TYPE_REF_BASE);
    }
    return 0;
}

static int constexpr_is_reference(wasm_valtype type) {
    return type == WASM_VALTYPE_FUNCREF ||
           type == WASM_VALTYPE_EXTERNREF ||
           type == WASM_VALTYPE_FUNCREF_NONNULL ||
           type == WASM_VALTYPE_EXTERNREF_NONNULL ||
           type == WASM_VALTYPE_ANYREF ||
           type == WASM_VALTYPE_EQREF ||
           type == WASM_VALTYPE_I31REF ||
           type == WASM_VALTYPE_STRUCTREF ||
           type == WASM_VALTYPE_ARRAYREF ||
           type == WASM_VALTYPE_ANYREF_NONNULL ||
           type == WASM_VALTYPE_EQREF_NONNULL ||
           type == WASM_VALTYPE_I31REF_NONNULL ||
           type == WASM_VALTYPE_STRUCTREF_NONNULL ||
           type == WASM_VALTYPE_ARRAYREF_NONNULL ||
           type == WASM_VALTYPE_EXNREF ||
           type == WASM_VALTYPE_EXNREF_NONNULL ||
           type == WASM_VALTYPE_NULLREF ||
           type == WASM_VALTYPE_NULLFUNCREF ||
           type == WASM_VALTYPE_NULLEXNREF ||
           type == WASM_VALTYPE_NULLEXTERNREF ||
           WASM_VALTYPE_IS_TYPE_REF(type);
}

static int constexpr_is_extern_reference(wasm_valtype type) {
    return type == WASM_VALTYPE_EXTERNREF ||
           type == WASM_VALTYPE_EXTERNREF_NONNULL;
}

static int constexpr_is_any_reference(wast_script *script, wasm_valtype type) {
    if (type == WASM_VALTYPE_ANYREF || type == WASM_VALTYPE_EQREF ||
        type == WASM_VALTYPE_I31REF || type == WASM_VALTYPE_STRUCTREF ||
        type == WASM_VALTYPE_ARRAYREF ||
        type == WASM_VALTYPE_ANYREF_NONNULL ||
        type == WASM_VALTYPE_EQREF_NONNULL ||
        type == WASM_VALTYPE_I31REF_NONNULL ||
        type == WASM_VALTYPE_STRUCTREF_NONNULL ||
        type == WASM_VALTYPE_ARRAYREF_NONNULL)
        return 1;
    if (WASM_VALTYPE_IS_TYPE_REF(type)) {
        wast_module *module = &cur_group(script)->module;
        unsigned index = WASM_VALTYPE_TYPE_REF_INDEX(type);
        return index < (unsigned)module->type_count &&
               module->types[index].kind != WAST_TYPE_FUNC;
    }
    return 0;
}

static int constexpr_is_nullable(wasm_valtype type) {
    return type == WASM_VALTYPE_FUNCREF ||
           type == WASM_VALTYPE_EXTERNREF ||
           type == WASM_VALTYPE_ANYREF ||
           type == WASM_VALTYPE_EQREF ||
           type == WASM_VALTYPE_I31REF ||
           type == WASM_VALTYPE_STRUCTREF ||
           type == WASM_VALTYPE_ARRAYREF ||
           type == WASM_VALTYPE_NULLREF ||
           type == WASM_VALTYPE_NULLFUNCREF ||
           type == WASM_VALTYPE_NULLEXNREF ||
           type == WASM_VALTYPE_NULLEXTERNREF ||
           (WASM_VALTYPE_IS_TYPE_REF(type) &&
            (unsigned)type < WASM_VALTYPE_TYPE_REF_BASE);
}

static int constexpr_defaultable(wasm_valtype type) {
    return !constexpr_is_reference(type) || constexpr_is_nullable(type);
}

static int constexpr_function_type(const wast_module *module,
                                   const wast_func *func) {
    if (func->type_index >= 0 && func->type_index < module->type_count &&
        module->types[func->type_index].kind == WAST_TYPE_FUNC)
        return func->type_index;
    for (int i = 0; i < module->type_count; i++) {
        const wast_type *type = &module->types[i];
        if (type->kind != WAST_TYPE_FUNC ||
            type->rec_group_size != 1 ||
            type->param_count != func->param_count ||
            type->result_count != func->result_count)
            continue;
        if (memcmp(type->params, func->params,
                   (size_t)func->param_count * sizeof(func->params[0])) != 0 ||
            memcmp(type->results, func->results,
                   (size_t)func->result_count * sizeof(func->results[0])) != 0)
            continue;
        return i;
    }
    return -1;
}

/* Mirror the OCaml validator's check_const shape after the ordinary
 * instruction parser has produced raw, terminator-free bytes: first reject
 * non-constant opcodes, then require the instruction sequence to leave exactly
 * the declared global type on the stack. */
static constexpr_check check_global_constexpr(wast_script *script,
                                              const wast_global *initializer) {
    const uint8_t *bytes = initializer->init_expr;
    size_t length = (size_t)initializer->init_len;
    size_t offset = 0;
    wasm_valtype stack[32];
    size_t depth = 0;

    while (offset < length) {
        uint8_t opcode = bytes[offset++];
        uint64_t immediate = 0;
        wasm_valtype operand_type;

        switch (opcode) {
            case 0x41:
                if (!read_init_leb(bytes, length, &offset, 5, &immediate))
                    return CONSTEXPR_TYPE_MISMATCH;
                operand_type = WASM_VALTYPE_I32;
                break;
            case 0x42:
                if (!read_init_leb(bytes, length, &offset, 10, &immediate))
                    return CONSTEXPR_TYPE_MISMATCH;
                operand_type = WASM_VALTYPE_I64;
                break;
            case 0x43:
                if (length - offset < 4) return CONSTEXPR_TYPE_MISMATCH;
                offset += 4;
                operand_type = WASM_VALTYPE_F32;
                break;
            case 0x44:
                if (length - offset < 8) return CONSTEXPR_TYPE_MISMATCH;
                offset += 8;
                operand_type = WASM_VALTYPE_F64;
                break;
            case 0xFD:
                if (!read_init_leb(bytes, length, &offset, 5, &immediate) ||
                    immediate != 12 || length - offset < 16)
                    return CONSTEXPR_NOT_CONSTANT;
                offset += 16;
                operand_type = WASM_VALTYPE_V128;
                break;
            case 0x23: {
                if (!read_init_leb(bytes, length, &offset, 5, &immediate))
                    return CONSTEXPR_TYPE_MISMATCH;
                wast_module *module = &cur_group(script)->module;
                if (immediate >= (uint64_t)module->global_count)
                    return CONSTEXPR_TYPE_MISMATCH;
                wast_global *global = &module->globals[immediate];
                if (global->is_mutable) return CONSTEXPR_NOT_CONSTANT;
                operand_type = global->valtype;
                break;
            }
            case 0xD0: {
                int64_t heap_type;
                if (!read_init_sleb(bytes, length, &offset, 5, &heap_type))
                    return CONSTEXPR_TYPE_MISMATCH;
                if (heap_type == -16)
                    operand_type = WASM_VALTYPE_FUNCREF;
                else if (heap_type == -17)
                    operand_type = WASM_VALTYPE_EXTERNREF;
                else if (heap_type == -18)
                    operand_type = WASM_VALTYPE_ANYREF;
                else if (heap_type == -19)
                    operand_type = WASM_VALTYPE_EQREF;
                else if (heap_type == -20)
                    operand_type = WASM_VALTYPE_I31REF;
                else if (heap_type == -21)
                    operand_type = WASM_VALTYPE_STRUCTREF;
                else if (heap_type == -22)
                    operand_type = WASM_VALTYPE_ARRAYREF;
                else if (heap_type == -15)
                    operand_type = WASM_VALTYPE_NULLREF;
                else if (heap_type == -13)
                    operand_type = WASM_VALTYPE_NULLFUNCREF;
                else if (heap_type == -12)
                    operand_type = WASM_VALTYPE_NULLEXNREF;
                else if (heap_type == -14)
                    operand_type = WASM_VALTYPE_NULLEXTERNREF;
                else if (heap_type == -23)
                    operand_type = WASM_VALTYPE_EXNREF;
                else if (heap_type >= 0 &&
                         heap_type < cur_group(script)->module.type_count)
                    operand_type = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_NULL_BASE +
                                                 (unsigned)heap_type);
                else
                    return CONSTEXPR_TYPE_MISMATCH;
                break;
            }
            case 0xD2:
                if (!read_init_leb(bytes, length, &offset, 5, &immediate))
                    return CONSTEXPR_TYPE_MISMATCH;
                if (immediate >= (uint64_t)cur_group(script)->module.func_count)
                    return CONSTEXPR_TYPE_MISMATCH;
                {
                    wast_module *module = &cur_group(script)->module;
                    int type_index = constexpr_function_type(
                        module, &module->funcs[immediate]);
                    operand_type = type_index >= 0 ?
                        (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE +
                                       (unsigned)type_index) :
                        WASM_VALTYPE_FUNCREF_NONNULL;
                }
                break;

            case 0xFB: {
                wast_module *module = &cur_group(script)->module;
                uint64_t subopcode;
                if (!read_init_leb(bytes, length, &offset, 5, &subopcode))
                    return CONSTEXPR_TYPE_MISMATCH;
                if (subopcode == 0x1c) { /* ref.i31 */
                    if (!constexpr_pop(script, stack, &depth, WASM_VALTYPE_I32))
                        return CONSTEXPR_TYPE_MISMATCH;
                    operand_type = WASM_VALTYPE_I31REF_NONNULL;
                    break;
                }
                if (subopcode == 0x1a || subopcode == 0x1b) {
                    wasm_valtype source;
                    if (depth == 0) return CONSTEXPR_TYPE_MISMATCH;
                    source = stack[--depth];
                    if (subopcode == 0x1a) { /* any.convert_extern */
                        if (!constexpr_is_extern_reference(source))
                            return CONSTEXPR_TYPE_MISMATCH;
                        operand_type = constexpr_is_nullable(source) ?
                            WASM_VALTYPE_ANYREF : WASM_VALTYPE_ANYREF_NONNULL;
                    } else { /* extern.convert_any */
                        if (!constexpr_is_any_reference(script, source))
                            return CONSTEXPR_TYPE_MISMATCH;
                        operand_type = constexpr_is_nullable(source) ?
                            WASM_VALTYPE_EXTERNREF : WASM_VALTYPE_EXTERNREF_NONNULL;
                    }
                    break;
                }
                if (subopcode != 0x00 && subopcode != 0x01 &&
                    subopcode != 0x06 && subopcode != 0x07 &&
                    subopcode != 0x08)
                    return CONSTEXPR_NOT_CONSTANT;
                if (!read_init_leb(bytes, length, &offset, 5, &immediate) ||
                    immediate >= (uint64_t)module->type_count)
                    return CONSTEXPR_TYPE_MISMATCH;
                wast_type *type = &module->types[immediate];
                if (subopcode == 0x00 || subopcode == 0x01) {
                    if (type->kind != WAST_TYPE_STRUCT)
                        return CONSTEXPR_TYPE_MISMATCH;
                    if (subopcode == 0x00) {
                        for (int i = type->field_count; i > 0; i--)
                            if (!constexpr_pop(script, stack, &depth, type->fields[i - 1]))
                                return CONSTEXPR_TYPE_MISMATCH;
                    } else {
                        for (int i = 0; i < type->field_count; i++)
                            if (!constexpr_defaultable(type->fields[i]))
                                return CONSTEXPR_TYPE_MISMATCH;
                    }
                } else {
                    if (type->kind != WAST_TYPE_ARRAY || type->field_count != 1)
                        return CONSTEXPR_TYPE_MISMATCH;
                    if (subopcode == 0x06) {
                        if (!constexpr_pop(script, stack, &depth, WASM_VALTYPE_I32) ||
                            !constexpr_pop(script, stack, &depth, type->fields[0]))
                            return CONSTEXPR_TYPE_MISMATCH;
                    } else if (subopcode == 0x07) {
                        if (!constexpr_defaultable(type->fields[0]) ||
                            !constexpr_pop(script, stack, &depth, WASM_VALTYPE_I32))
                            return CONSTEXPR_TYPE_MISMATCH;
                    } else {
                        uint64_t count;
                        if (!read_init_leb(bytes, length, &offset, 5, &count) ||
                            count > 32)
                            return CONSTEXPR_TYPE_MISMATCH;
                        for (uint64_t i = 0; i < count; i++)
                            if (!constexpr_pop(script, stack, &depth, type->fields[0]))
                                return CONSTEXPR_TYPE_MISMATCH;
                    }
                }
                operand_type = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE +
                                             (unsigned)immediate);
                break;
            }

            case 0x6A: case 0x6B: case 0x6C:
                operand_type = WASM_VALTYPE_I32;
                goto binary;
            case 0x7C: case 0x7D: case 0x7E:
                operand_type = WASM_VALTYPE_I64;
binary:
                if (depth < 2 || stack[depth - 1] != operand_type ||
                    stack[depth - 2] != operand_type)
                    return CONSTEXPR_TYPE_MISMATCH;
                depth--;
                stack[depth - 1] = operand_type;
                continue;

            default:
                return CONSTEXPR_NOT_CONSTANT;
        }

        if (!constexpr_push(stack, &depth, operand_type))
            return CONSTEXPR_TYPE_MISMATCH;
    }
    if (depth != 1 ||
        !constexpr_type_compatible(script, stack[0], initializer->valtype))
        return CONSTEXPR_TYPE_MISMATCH;
    return CONSTEXPR_VALID;
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
static void check_label_end(wast_script *script, const char *end_label,
                            int line, int column) {
    if (!end_label || !end_label[0]) return; /* no end label is always ok */
    if (g_label_depth <= 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%d:%d: mismatching label", line, column);
        report_validation_error(script, msg);
        return;
    }
    const char *start_label = g_labels[g_label_depth - 1];
    if (!start_label[0] || strcmp(start_label, end_label) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%d:%d: mismatching label", line, column);
        report_validation_error(script, msg);
    }
}
static uint32_t resolve_label(wast_script *script, const char *s,
                              int line, int column) {
    if (!s || !s[0]) return 0;
    if (s[0] == '$') {
        for (int i = g_label_depth - 1; i >= 0; i--)
            if (strcmp(g_labels[i], s) == 0)
                return (uint32_t)(g_label_depth - 1 - i);
        char message[256];
        snprintf(message, sizeof(message), "%d:%d: unknown label: %s",
                 line, column, s);
        report_validation_error(script, message);
        return 0;
    }
    char *end = NULL;
    unsigned long value = strtoul(s, &end, 10);
    if (!end || *end != '\0' || value > UINT32_MAX) {
        char message[256];
        snprintf(message, sizeof(message), "%d:%d: malformed label index: %s",
                 line, column, s);
        report_validation_error(script, message);
        return 0;
    }
    return (uint32_t)value;
}
static uint32_t resolve_local(const char *s) {
    if (s && s[0] == '$')
        for (int i = 0; i < g_local_name_count; i++)
            if (strcmp(g_local_names[i], s) == 0) return (uint32_t)i;
    if (s && s[0] == '$') return UINT32_MAX;
    return (uint32_t)strtoul(s, NULL, 10);
}
static int local_name_is_duplicate(const char *name) {
    if (!name || name[0] != '$') return 0;
    for (int i = 0; i < g_local_name_count; i++)
        if (strcmp(g_local_names[i], name) == 0) return 1;
    return 0;
}
static void append_local_name(wast_script *script, const char *name) {
    if (local_name_is_duplicate(name)) {
        report_validation_error(script, "duplicate local identifier");
        return;
    }
    if (g_local_name_count < WAST_MAX_PARAMS + WAST_MAX_LOCALS)
        snprintf(g_local_names[g_local_name_count++], WAST_MAX_EXPORT_NAME,
                 "%s", name ? name : "");
}
static void ensure_inherited_param_slots(wast_script *script) {
    if (!g_in_func || g_cur_func.has_inline_params ||
        g_cur_func.type_index < 0 || g_local_name_count != 0)
        return;
    wast_module *module = &cur_group(script)->module;
    if (g_cur_func.type_index >= module->type_count ||
        module->types[g_cur_func.type_index].kind != WAST_TYPE_FUNC)
        return;
    int count = module->types[g_cur_func.type_index].param_count;
    while (g_local_name_count < count &&
           g_local_name_count < WAST_MAX_PARAMS + WAST_MAX_LOCALS)
        g_local_names[g_local_name_count++][0] = '\0';
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
            if (strcmp(g_type_names[i], s) == 0) return g_type_name_indices[i];
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
static uint32_t resolve_data(const char *s) {
    if (s && s[0] == '$') {
        for (int i=0;i<g_data_name_count;i++)
            if (!strcmp(g_data_names[i],s)) return (uint32_t)i;
        return UINT32_MAX;
    }
    return (uint32_t)strtoul(s,NULL,10);
}
static void record_data_name(const char *name) {
    if (g_data_name_count >= WAST_MAX_DATA_SEGS) return;
    snprintf(g_data_names[g_data_name_count++], WAST_MAX_EXPORT_NAME,
             "%s", name ? name : "");
}
static uint32_t resolve_elem(const char *s) {
    if (s && s[0] == '$') {
        for (int i=0;i<g_elem_name_count;i++) if (!strcmp(g_elem_names[i],s)) return (uint32_t)i;
        return UINT32_MAX;
    }
    return (uint32_t)strtoul(s,NULL,10);
}
static uint32_t resolve_tag(const char *s) {
    if (s && s[0] == '$') {
        for (int i = 0; i < g_tag_name_count; i++)
            if (strcmp(g_tag_names[i], s) == 0) return (uint32_t)i;
        return UINT32_MAX;
    }
    return (uint32_t)strtoul(s, NULL, 10);
}

/* -----------------------------------------------------------------------
 * ATOM opcode table — returns 1 if handled, 0 if unknown
 * --------------------------------------------------------------------- */

static int emit_atom_op(wast_script *script, const char *name) {
    /* Memory size/grow carry the reserved memory index immediate even in the
     * single-memory encoding.  They arrive here in folded form after their
     * operands have been emitted. */
    if (strcmp(name, "memory.size") == 0 ||
        strcmp(name, "memory.grow") == 0) {
        emit_byte(script, strcmp(name, "memory.size") == 0 ? 0x3f : 0x40);
        emit_byte(script, 0x00);
        return 1;
    }
    if (strcmp(name, "table.get") == 0 ||
        strcmp(name, "table.set") == 0) {
        emit_byte(script, strcmp(name, "table.get") == 0 ? 0x25 : 0x26);
        emit_byte(script, 0x00);
        return 1;
    }
    if (strcmp(name, "table.size") == 0) {
        emit_byte(script, 0xFC); emit_leb_u32(script, 16); emit_byte(script, 0x00);
        return 1;
    }
    if (strcmp(name, "table.grow") == 0) {
        emit_byte(script, 0xFC); emit_leb_u32(script, 15); emit_byte(script, 0x00);
        return 1;
    }
    if (strcmp(name, "table.fill") == 0) {
        emit_byte(script, 0xFC); emit_leb_u32(script, 17); emit_byte(script, 0x00);
        return 1;
    }
    if (strcmp(name, "table.copy") == 0) {
        emit_byte(script, 0xFC); emit_leb_u32(script, 14);
        emit_byte(script, 0x00); emit_byte(script, 0x00);
        return 1;
    }
    if (strcmp(name, "memory.copy") == 0) {
        emit_byte(script, 0xFC); emit_leb_u32(script, 10);
        emit_byte(script, 0x00); emit_byte(script, 0x00);
        return 1;
    }
    if (strcmp(name, "memory.fill") == 0) {
        emit_byte(script, 0xFC); emit_leb_u32(script, 11); emit_byte(script, 0x00);
        return 1;
    }
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
        {"i32.reinterpret_f32",0xBC},{"i64.reinterpret_f64",0xBD},
        {"f32.reinterpret_i32",0xBE},{"f64.reinterpret_i64",0xBF},
        /* sign-extension */
        {"i32.extend8_s",0xC0},{"i32.extend16_s",0xC1},
        {"i64.extend8_s",0xC2},{"i64.extend16_s",0xC3},
        {"i64.extend32_s",0xC4},
        /* ref */
        {"ref.is_null",0xD1},{"ref.eq",0xD3},{"ref.as_non_null",0xD4},
        {NULL,0}
    };
    for (int i = 0; tbl[i].n; i++) {
        if (strcmp(name, tbl[i].n) == 0) {
            emit_byte(script, tbl[i].op);
            return 1;
        }
    }
    /* Folded dotted operators arrive through the generic folded-start token.
     * The shared table mirrors the official interpreter's mnemonic mapping;
     * immediate shape and semantic checking remain in the C grammar/runtime. */
    wast_simd_info simd;
    if (wast_simd_lookup(name, &simd)) {
        emit_byte(script, 0xFD);
        emit_leb_u32(script, simd.opcode);
        return 1;
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
    static const struct { const char *n; uint32_t sub; } gc_tbl[] = {
        {"any.convert_extern",0x1a},
        {"extern.convert_any",0x1b},
        {"ref.i31",0x1c},
        {"i31.get_s",0x1d},
        {"i31.get_u",0x1e},
        {"array.len",0x0f},
        {NULL,0}
    };
    for (int i = 0; gc_tbl[i].n; i++) {
        if (strcmp(name, gc_tbl[i].n) == 0) {
            emit_byte(script, 0xFB);
            emit_leb_u32(script, gc_tbl[i].sub);
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

static void emit_const_immediate_atom(wast_script *script, const char *name,
                                      int64_t value) {
    if (strcmp(name, "i32.const") == 0) {
        emit_byte(script, 0x41);
        emit_leb_s32(script, (int32_t)value);
    } else if (strcmp(name, "i64.const") == 0) {
        emit_byte(script, 0x42);
        emit_leb_s64(script, value);
    } else if (strcmp(name, "f32.const") == 0) {
        emit_byte(script, 0x43);
        emit_f32(script, (float)value);
    } else if (strcmp(name, "f64.const") == 0) {
        emit_byte(script, 0x44);
        emit_f64(script, (double)value);
    } else if (strcmp(name, "global.get") == 0) {
        emit_byte(script, 0x23);
        emit_leb_u32(script, (uint32_t)value);
    } else if (strcmp(name, "ref.func") == 0) {
        emit_byte(script, 0xd2);
        emit_leb_u32(script, (uint32_t)value);
    } else {
        /* Preserve the actual operator so post-parse constexpr validation can
         * reject non-constant instructions instead of accepting a fabricated
         * i32.const placeholder. */
        (void)emit_atom_op(script, name);
        emit_leb_s64(script, value);
    }
}

static int emit_gc_constructor(wast_script *script, const char *name,
                               const char *type_name, int line, int column) {
    uint32_t subopcode;
    if (strcmp(name, "struct.new") == 0) subopcode = 0x00;
    else if (strcmp(name, "struct.new_default") == 0) subopcode = 0x01;
    else if (strcmp(name, "array.new") == 0) subopcode = 0x06;
    else if (strcmp(name, "array.new_default") == 0) subopcode = 0x07;
    else if (strcmp(name, "array.new_fixed") == 0) subopcode = 0x08;
    else return 0;
    emit_byte(script, 0xFB);
    emit_leb_u32(script, subopcode);
    emit_index_ref(script, IDX_TYPE, type_name, line, column);
    return 1;
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

static uint32_t explicit_align_exponent(uint32_t alignment) {
    uint32_t exponent = 0;
    if (alignment == 0 || (alignment & (alignment - 1u)) != 0)
        return UINT32_MAX;
    while (alignment > 1u) {
        alignment >>= 1;
        exponent++;
    }
    return exponent;
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
        case WASM_VALTYPE_ANYREF:   return 0x6E;
        case WASM_VALTYPE_EQREF:    return 0x6D;
        case WASM_VALTYPE_I31REF:   return 0x6C;
        case WASM_VALTYPE_STRUCTREF:return 0x6B;
        case WASM_VALTYPE_ARRAYREF: return 0x6A;
        case WASM_VALTYPE_EXNREF: return 0x69;
        case WASM_VALTYPE_NULLREF: return 0x71;
        case WASM_VALTYPE_NULLFUNCREF: return 0x73;
        case WASM_VALTYPE_NULLEXNREF: return 0x74;
        case WASM_VALTYPE_NULLEXTERNREF: return 0x72;
        default:                    return 0x7F;
    }
}

static void begin_block_type(void) {
    g_blocktype_param_count = 0;
    g_blocktype_result_count = 0;
    g_blocktype_explicit = -1;
}

/* Find an existing type (params...) -> (results...) or append a new one.
 * Used to encode block signatures that cannot use a single value-type byte. */
static int find_or_create_block_type(wast_script *script) {
    wast_module *mod = &cur_group(script)->module;
    for (int i = 0; i < mod->type_count; i++) {
        wast_type *t = &mod->types[i];
        if (t->kind != WAST_TYPE_FUNC ||
            t->param_count != g_blocktype_param_count ||
            t->result_count != g_blocktype_result_count)
            continue;
        int match = 1;
        for (int j = 0; j < g_blocktype_param_count; j++)
            if (t->params[j] != g_blocktype_params[j]) { match = 0; break; }
        for (int j = 0; j < g_blocktype_result_count; j++)
            if (t->results[j] != g_blocktype_results[j]) { match = 0; break; }
        if (match) return i;
    }
    if (mod->type_count >= WAST_MAX_TYPES) return 0;
    wast_type *t = &mod->types[mod->type_count];
    memset(t, 0, sizeof(*t));
    t->kind = WAST_TYPE_FUNC;
    t->is_final = 1;
    t->supertype = -1;
    t->rec_group_start = (uint32_t)mod->type_count;
    t->rec_group_size = 1;
    t->param_count = g_blocktype_param_count;
    t->result_count = g_blocktype_result_count;
    for (int j = 0; j < g_blocktype_param_count; j++)
        t->params[j] = g_blocktype_params[j];
    for (int j = 0; j < g_blocktype_result_count; j++)
        t->results[j] = g_blocktype_results[j];
    int index = mod->type_count++;
    if (g_type_name_count < WAST_MAX_TYPES) {
        g_type_names[g_type_name_count++][0] = '\0';
        g_type_name_indices[g_type_name_count - 1] = (uint32_t)index;
    }
    return index;
}

static int finish_block_type(wast_script *script) {
    if (g_blocktype_explicit >= 0)
        return -(g_blocktype_explicit + 2);
    if (g_blocktype_param_count == 0 && g_blocktype_result_count == 0)
        return -1;
    if (g_blocktype_param_count == 0 && g_blocktype_result_count == 1) {
        wasm_valtype result = g_blocktype_results[0];
        /* Only the compact one-byte value types can be emitted directly as a
         * block type.  Indexed and explicitly non-null reference types have
         * multi-byte encodings, so represent those with a synthesized
         * function type just like any other non-trivial block signature. */
        if (!WASM_VALTYPE_IS_TYPE_REF(result) &&
            result != WASM_VALTYPE_FUNCREF_NONNULL &&
            result != WASM_VALTYPE_EXTERNREF_NONNULL &&
            result != WASM_VALTYPE_ANYREF_NONNULL &&
            result != WASM_VALTYPE_EQREF_NONNULL &&
            result != WASM_VALTYPE_I31REF_NONNULL &&
            result != WASM_VALTYPE_STRUCTREF_NONNULL &&
            result != WASM_VALTYPE_ARRAYREF_NONNULL &&
            result != WASM_VALTYPE_EXNREF_NONNULL)
            return (int)result;
    }
    return -(find_or_create_block_type(script) + 2);
}

static void begin_gc_type(const char *id, wast_type_kind kind) {
    memset(&g_cur_type, 0, sizeof(g_cur_type));
    g_cur_type.kind = kind;
    g_cur_type.supertype = -1;
    g_cur_type.is_final = 1;
    snprintf(g_cur_type.id, sizeof(g_cur_type.id), "%s", id);
}

static void append_gc_field(uint64_t encoded) {
    if (g_cur_type.field_count >= WAST_MAX_TYPE_FIELDS) return;
    int field = g_cur_type.field_count++;
    g_cur_type.fields[field] = (wasm_valtype)(encoded & 0xffffu);
    g_cur_type.field_mutable[field] = (uint8_t)((encoded >> 16) & 1u);
    g_cur_type.field_packed[field] = (uint8_t)((encoded >> 17) & 3u);
}

static void append_gc_field_named(uint64_t encoded, const char *name) {
    if (g_cur_type.field_count >= WAST_MAX_TYPE_FIELDS) return;
    int field = g_cur_type.field_count;
    if (name && name[0])
        snprintf(g_cur_type.field_names[field], 64, "%s", name);
    append_gc_field(encoded);
}

/* Resolve a field name to its index within a type */
static uint32_t resolve_field(wast_script *script, uint32_t type_idx,
                              const char *name) {
    wast_module *mod = &cur_group(script)->module;
    if (type_idx >= (uint32_t)mod->type_count) return 0;
    const wast_type *t = &mod->types[type_idx];
    /* Numeric reference */
    if (name[0] != '$') return (uint32_t)strtoul(name, NULL, 0);
    for (int i = 0; i < t->field_count; i++) {
        if (strcmp(t->field_names[i], name) == 0)
            return (uint32_t)i;
    }
    return 0;
}

/* Emit GC accessor/mutator instructions: struct.get/set, array.get/set, etc.
 * Returns 1 if the name matched, 0 otherwise. */
static int emit_gc_accessor(wast_script *script, const char *name,
                            const char *type_name, int line, int column) {
    uint32_t subopcode;
    if (strcmp(name, "struct.get") == 0) subopcode = 0x02;
    else if (strcmp(name, "struct.get_s") == 0) subopcode = 0x03;
    else if (strcmp(name, "struct.get_u") == 0) subopcode = 0x04;
    else if (strcmp(name, "struct.set") == 0) subopcode = 0x05;
    else if (strcmp(name, "array.get") == 0) subopcode = 0x0B;
    else if (strcmp(name, "array.get_s") == 0) subopcode = 0x0C;
    else if (strcmp(name, "array.get_u") == 0) subopcode = 0x0D;
    else if (strcmp(name, "array.set") == 0) subopcode = 0x0E;
    else if (strcmp(name, "array.fill") == 0) subopcode = 0x10;
    else return 0;
    emit_byte(script, 0xFB);
    emit_leb_u32(script, subopcode);
    emit_index_ref(script, IDX_TYPE, type_name, line, column);
    return 1;
}

/* Emit GC two-index instructions: array.copy, array.new_data, etc.
 * First index is always type, second depends on the instruction.
 * Returns 1 if matched. */
static int emit_gc_two_index(wast_script *script, const char *name,
                             const char *idx1, int l1, int c1,
                             const char *idx2, int l2, int c2) {
    if (strcmp(name, "array.copy") == 0) {
        emit_byte(script, 0xFB); emit_leb_u32(script, 0x13);
        emit_index_ref(script, IDX_TYPE, idx1, l1, c1);
        emit_index_ref(script, IDX_TYPE, idx2, l2, c2);
        return 1;
    }
    if (strcmp(name, "array.new_data") == 0) {
        emit_byte(script, 0xFB); emit_leb_u32(script, 0x09);
        emit_index_ref(script, IDX_TYPE, idx1, l1, c1);
        emit_index_ref(script, IDX_DATA, idx2, l2, c2);
        return 1;
    }
    if (strcmp(name, "array.new_elem") == 0) {
        emit_byte(script, 0xFB); emit_leb_u32(script, 0x0A);
        emit_index_ref(script, IDX_TYPE, idx1, l1, c1);
        emit_index_ref(script, IDX_ELEM, idx2, l2, c2);
        return 1;
    }
    if (strcmp(name, "array.init_data") == 0) {
        emit_byte(script, 0xFB); emit_leb_u32(script, 0x11);
        emit_index_ref(script, IDX_TYPE, idx1, l1, c1);
        emit_index_ref(script, IDX_DATA, idx2, l2, c2);
        return 1;
    }
    if (strcmp(name, "array.init_elem") == 0) {
        emit_byte(script, 0xFB); emit_leb_u32(script, 0x12);
        emit_index_ref(script, IDX_TYPE, idx1, l1, c1);
        emit_index_ref(script, IDX_ELEM, idx2, l2, c2);
        return 1;
    }
    return 0;
}

/* Emit a GC struct.get/set with a resolved field index */
static void emit_gc_struct_access(wast_script *script, const char *name,
                                  const char *type_name, int tl, int tc,
                                  const char *field_name, int fl, int fc) {
    uint32_t subopcode;
    if (strcmp(name, "struct.get") == 0) subopcode = 0x02;
    else if (strcmp(name, "struct.get_s") == 0) subopcode = 0x03;
    else if (strcmp(name, "struct.get_u") == 0) subopcode = 0x04;
    else if (strcmp(name, "struct.set") == 0) subopcode = 0x05;
    else return;
    emit_byte(script, 0xFB);
    emit_leb_u32(script, subopcode);
    emit_index_ref(script, IDX_TYPE, type_name, tl, tc);
    /* Resolve field index — numeric or named */
    uint32_t type_idx = resolve_type(type_name);
    uint32_t field_idx = resolve_field(script, type_idx, field_name);
    emit_leb_u32(script, field_idx);
}

/* GC cast type: encodes nullability + heap type in int64_t.
 * Bits 0-31: heap type (signed for abstract, positive for concrete).
 *   Abstract: (int8_t)byte (e.g., 0x70→-16 for func)
 *   Concrete: non-negative type index
 * Bit 32: nullable flag
 * Bit 33: is_abstract flag */
#define GC_CT_NULLABLE (INT64_C(1) << 32)
#define GC_CT_ABSTRACT (INT64_C(1) << 33)
#define GC_CT_ABS_NULL(byte)  (GC_CT_ABSTRACT | GC_CT_NULLABLE | ((int64_t)(uint8_t)(byte)))
#define GC_CT_ABS_NONNULL(byte) (GC_CT_ABSTRACT | ((int64_t)(uint8_t)(byte)))
#define GC_CT_IDX_NULL(idx)   (GC_CT_NULLABLE | ((int64_t)(uint32_t)(idx)))
#define GC_CT_IDX_NONNULL(idx) ((int64_t)(uint32_t)(idx))

static void emit_gc_heaptype(wast_script *script, int64_t ct) {
    if (ct & GC_CT_ABSTRACT)
        emit_byte(script, (uint8_t)(ct & 0xff));
    else
        emit_leb_u32(script, (uint32_t)(ct & 0xffffffffLL));
}

static void emit_ref_test_cast(wast_script *script, const char *name,
                               int64_t ct) {
    int is_cast = (strcmp(name, "ref.cast") == 0);
    int nullable = (ct & GC_CT_NULLABLE) ? 1 : 0;
    emit_byte(script, 0xFB);
    if (is_cast)
        emit_leb_u32(script, nullable ? 0x17 : 0x16);
    else
        emit_leb_u32(script, nullable ? 0x15 : 0x14);
    emit_gc_heaptype(script, ct);
}

static void emit_br_on_cast(wast_script *script, const char *name,
                            uint32_t label, int64_t rt1, int64_t rt2) {
    int is_fail = (strcmp(name, "br_on_cast_fail") == 0);
    uint8_t flags = 0;
    if (rt1 & GC_CT_NULLABLE) flags |= 1;
    if (rt2 & GC_CT_NULLABLE) flags |= 2;
    emit_byte(script, 0xFB);
    emit_leb_u32(script, is_fail ? 0x19 : 0x18);
    emit_byte(script, flags);
    emit_leb_u32(script, label);
    emit_gc_heaptype(script, rt1);
    emit_gc_heaptype(script, rt2);
}

static void commit_current_type(wast_script *script) {
    wast_module *mod = &cur_group(script)->module;
    if (mod->type_count >= WAST_MAX_TYPES) return;
    if (!g_in_rec_group) {
        g_cur_type.rec_group_start = (uint32_t)mod->type_count;
        g_cur_type.rec_group_size = 1;
    }
    mod->types[mod->type_count++] = g_cur_type;
    /* Name already pre-registered in type_item; just update the name in case
     * the type definition changed it (shouldn't happen, but keep in sync). */
}

/* Emit blocktype byte(s).
 *   bt == -1           → void (0x40)
 *   bt >= 0            → single valtype byte
 *   bt <= -2           → multi-value: type index = -(bt + 2), emit as s33 LEB */
static void emit_blocktype(wast_script *script, int bt) {
    if (bt == -1) { emit_byte(script, 0x40); return; }
    if (bt >= 0)  { emit_byte(script, valtype_byte((wasm_valtype)bt)); return; }
    /* bt <= -2: type index */
    emit_leb_s32(script, (int32_t)(-(bt + 2)));
}

} /* end %code */

%union {
    int64_t      i64_val;
    double       f64_val;
    uint32_t     u32_val;
    uint64_t     u64_val;
    char         str_val[WAST_MAX_EXPORT_NAME];
    char        *string_val;
    wasm_valtype valtype_val;
    wasm_value   value_val;
    lane_list    lane_list_val;
    wast_memarg  memarg_val;
    int          int_val;
    wast_limits  limits_val;
}

/* -----------------------------------------------------------------------
 * Token declarations
 * --------------------------------------------------------------------- */

%token LPAREN RPAREN
/* Lexer-classified folded control boundaries. These tokens consume the
 * opening parenthesis and keyword together so folded control constructs enter
 * their dedicated grammar without semantic lexer actions. Function field/body
 * ordering itself is handled by the recursive phase productions below. */
%token FOLD_BLOCK_START FOLD_LOOP_START FOLD_IF_START
%token FOLD_LOCAL_GET_START FOLD_LOCAL_SET_START FOLD_LOCAL_TEE_START
%token FOLD_THEN_START FOLD_ELSE_START
%token FOLD_DROP_START FOLD_NOP_START FOLD_UNREACHABLE_START FOLD_RETURN_START
%token FOLD_GLOBAL_GET_START FOLD_GLOBAL_SET_START
%token FOLD_REF_NULL_START
%token FOLD_REF_I31_START FOLD_ARRAY_NEW_START FOLD_ARRAY_NEW_FIXED_START
%token MODULE_GLOBAL_START
%token MODULE_GLOBAL_MUT_START
%token MODULE_BINARY_START MODULE_QUOTE_START
%token MODULE_ID_BINARY_START MODULE_ID_QUOTE_START
%token MODULE_MEMORY_START
%token FOLD_BR_TABLE_START
%token FOLD_BR_START FOLD_BR_IF_START FOLD_BR_ON_NULL_START FOLD_BR_ON_NON_NULL_START
%token FOLD_CALL_START FOLD_CALL_REF_START
%token FOLD_RETURN_CALL_START FOLD_RETURN_CALL_REF_START FOLD_RETURN_CALL_INDIRECT_START
%token FOLD_SELECT_START
%token FOLD_CALL_INDIRECT_START
%token FOLD_TRY_TABLE_START FOLD_CATCH_START FOLD_CATCH_REF_START
%token FOLD_CATCH_ALL_START FOLD_CATCH_ALL_REF_START
%token FOLD_THROW_START FOLD_THROW_REF_START
%token <str_val> FOLD_ATOM_START FOLD_MEMOP_START
%token <str_val> FOLD_SIMD_MEM_START FOLD_SIMD_MEM_LANE_START
%token <str_val> FOLD_SIMD_LANE_START FOLD_SIMD_SHUFFLE_START
%token <str_val> SIMD_SHUFFLE_OP
%token <str_val> OP
%token <string_val> STRING
%token <str_val>  ATOM ID
%token <u32_val>  SIMD_OP ALIGN_IMM
%token <u64_val>  OFFSET_IMM
%token <i64_val>  INT HEXINT POSINT
%token <f64_val>  FLOAT

%token KW_V128_CONST KW_LOCAL_GET KW_LOCAL_SET KW_LOCAL_TEE
%token KW_GLOBAL_GET KW_GLOBAL_SET
%token KW_I32_CONST KW_I64_CONST KW_F32_CONST KW_F64_CONST
%token KW_CALL KW_CALL_INDIRECT KW_CALL_REF
%token KW_RETURN_CALL KW_RETURN_CALL_INDIRECT KW_RETURN_CALL_REF
%token KW_BR KW_BR_IF KW_BR_TABLE KW_BR_ON_NULL KW_BR_ON_NON_NULL
%token KW_BLOCK KW_LOOP KW_IF KW_ELSE KW_THEN KW_END KW_RETURN
%token KW_SELECT KW_DROP KW_UNREACHABLE KW_NOP
%token KW_REF_FUNC KW_REF_NULL KW_REF_IS_NULL KW_REF_EXTERN KW_REF_TYPE KW_NULL KW_EXTERN KW_REF_AS_NON_NULL
%token KW_MEMORY_SIZE KW_MEMORY_GROW
%token KW_MEMORY_INIT KW_MEMORY_COPY KW_MEMORY_FILL
%token KW_TABLE_GET KW_TABLE_SET KW_TABLE_GROW KW_TABLE_SIZE
%token KW_TABLE_INIT KW_TABLE_COPY KW_TABLE_FILL
%token KW_MODULE KW_FUNC KW_EXPORT KW_IMPORT KW_PARAM KW_RESULT
%token KW_TABLE KW_MEMORY KW_GLOBAL KW_DATA KW_ELEM KW_TYPE KW_START KW_TAG
%token KW_LOCAL KW_MUT KW_DECLARE KW_ITEM KW_OFFSET_KW KW_REGISTER
%token KW_DEFINITION KW_INSTANCE KW_EXN KW_REC KW_SUB KW_FINAL
%token KW_STRUCT_GET KW_STRUCT_GET_S KW_STRUCT_GET_U KW_STRUCT_SET
%token KW_FUNCREF KW_EXTERNREF KW_ANYREF KW_EQREF KW_I31REF KW_STRUCTREF KW_ARRAYREF KW_EXNREF
%token KW_NULLREF KW_NULLFUNCREF KW_NULLEXNREF KW_NULLEXTERNREF
%token KW_V128 KW_I32 KW_I64 KW_F32 KW_F64
%token KW_ANY KW_EQ KW_I31 KW_STRUCT KW_ARRAY KW_FIELD KW_I8 KW_I16
%token KW_NONE KW_NOFUNC KW_NOEXN KW_NOEXTERN
%token KW_I8X16 KW_I16X8 KW_I32X4 KW_I64X2 KW_F32X4 KW_F64X2
%token KW_NAN_CANONICAL KW_NAN_ARITHMETIC KW_NEG_NAN KW_NEG_INF
%token KW_POS_NAN KW_POS_INF KW_NAN KW_INF
%token KW_ASSERT_RETURN KW_ASSERT_TRAP KW_ASSERT_EXCEPTION KW_ASSERT_EXHAUSTION
%token KW_ASSERT_INVALID KW_ASSERT_MALFORMED KW_ASSERT_UNLINKABLE
%token KW_INVOKE KW_EITHER KW_GET
%token KW_QUOTE KW_BINARY

/* -----------------------------------------------------------------------
 * Type declarations for non-terminal symbols
 * --------------------------------------------------------------------- */

%type <valtype_val>   valtype reftype_as_valtype table_reftype
%type <value_val>     const_val result_const f32_val f64_val
%type <lane_list_val> lane_vals lane_val
%type <int_val>       lane_type blocktype block_param_type block_result_type
%type <int_val>       reftype opt_table_idx
%type <str_val>       opt_id opt_label opt_label_end any_idx index_ref fold_if_start
%type <i64_val>       any_int any_nat lane_index gc_casttype
%type <memarg_val>    memarg
%type <u64_val>       storage_type
%type <int_val>       typeuse typeuse_items typeuse_item
%type <int_val>       module_assert_body
%type <int_val>       global_fields
%type <int_val>       constexpr_expr
%type <limits_val>    limits
%type <memarg_val>    memarg_nonempty

%%

/* -----------------------------------------------------------------------
 * Top level
 * --------------------------------------------------------------------- */

script:
    commands
    ;

commands:
    /* empty */
  | commands command { script->command_count++; }
    ;

command:
    module_cmd
  | assert_cmd
  | register_cmd
  | LPAREN KW_INVOKE {
        memset(&g_cur_assert, 0, sizeof(g_cur_assert));
        g_cur_assert.kind = WAST_ASSERT_RETURN;
        g_in_assert = 1;
    } STRING {
        strncpy(g_invoke_name, $4, WAST_MAX_EXPORT_NAME - 1);
        g_invoke_name[WAST_MAX_EXPORT_NAME - 1] = '\0';
    } const_list RPAREN {
        append_assert(script);
    }
  | LPAREN KW_INVOKE {
        memset(&g_cur_assert, 0, sizeof(g_cur_assert));
        g_cur_assert.kind = WAST_ASSERT_RETURN;
        g_in_assert = 1;
    } ID STRING {
        snprintf(g_cur_assert.module_id, WAST_MAX_EXPORT_NAME, "%s", $4);
        snprintf(g_invoke_name, WAST_MAX_EXPORT_NAME, "%s", $5);
    } const_list RPAREN {
        append_assert(script);
    }
  | LPAREN ATOM error RPAREN
  | LPAREN error RPAREN
    ;

/* -----------------------------------------------------------------------
 * Module
 * --------------------------------------------------------------------- */

module_cmd:
    /* (module binary "...") or (module quote "...") — skip inline binary/text modules */
    MODULE_BINARY_START string_list RPAREN {
        start_new_group(script);
        attach_raw_module(script, WAST_RAW_BINARY);
    }
  | MODULE_QUOTE_START string_list RPAREN {
        start_new_group(script);
        attach_raw_module(script, WAST_RAW_QUOTE);
    }
  | MODULE_ID_BINARY_START string_list RPAREN {
        start_new_group(script);
        attach_raw_module(script, WAST_RAW_BINARY);
    }
  | MODULE_ID_QUOTE_START string_list RPAREN {
        start_new_group(script);
        attach_raw_module(script, WAST_RAW_QUOTE);
    }
  | LPAREN KW_MODULE ATOM string_list RPAREN {
        /* binary/quote module at top level — treated as empty module */
        start_new_group(script);
    }
  /* (module definition $id? ...) — defines a module without instantiating */
  | LPAREN KW_MODULE KW_DEFINITION {
        begin_module(script);
        cur_group(script)->module.is_definition = 1;
    }
    opt_module_id module_fields RPAREN { apply_func_fixups(script); }
  /* (module instance ...) — instantiates a previously defined module */
  | LPAREN KW_MODULE KW_INSTANCE {
        start_new_group(script);
        g_instance_arg_count = 0;
    } instance_args RPAREN { finish_module_instance(script); }
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

instance_args:
    /* empty */
  | instance_args ID   { append_instance_arg(script, $2); }
  | instance_args ATOM { append_instance_arg(script, $2); }
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
  | rec_item
  | export_item
  | start_item
  | tag_item
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
        g_signature_seen_result = 0;
    }
    func_attrs RPAREN
    ;

func_attrs:
    opt_id func_fields {
        if (!g_cur_func.is_import) {
            emit_byte(script, 0x0B); /* end */
        }
        commit_func(script);
    }
    ;

/*
 * Mirror the phase structure of the reference OCaml text parser.  Each
 * recursive production owns one syntactic part of a function and then hands
 * off to the narrower phase that may follow it:
 *
 *   exports/import -> typeuse -> params -> results -> locals -> instructions
 *
 * Keeping the phases right-recursive lets the parser shift the common LPAREN
 * and use its following keyword to distinguish a header field from a folded
 * instruction.  No lexer-only function-body sentinel is required.
 */
func_fields:
    /* empty */
  | func_inline_export func_fields
  | LPAREN KW_IMPORT STRING STRING RPAREN {
        snprintf(g_cur_func.import_module, WAST_MAX_EXPORT_NAME, "%s", $3);
        snprintf(g_cur_func.import_name,   WAST_MAX_EXPORT_NAME, "%s", $4);
        g_cur_func.is_import = 1;
    } func_import_fields
  | LPAREN KW_TYPE any_idx RPAREN {
        set_func_type_ref(script, $3, @3.first_line, @3.first_column);
    } func_param_fields
  | LPAREN KW_PARAM param_items RPAREN func_param_fields
  | LPAREN KW_RESULT result_valtype_list RPAREN func_result_fields
  | LPAREN KW_LOCAL local_items RPAREN func_local_fields
  | instr func_body
    ;

func_inline_export:
    LPAREN KW_EXPORT STRING RPAREN {
        if (g_in_func && !g_cur_func.has_export_name) {
            size_t n = strlen($3);
            if (n >= WAST_MAX_EXPORT_NAME) n = WAST_MAX_EXPORT_NAME - 1;
            memcpy(g_cur_func.export_name, $3, n);
            g_cur_func.export_name[n] = '\0';
            g_cur_func.has_export_name = 1;
        }
    }
    ;

func_param_fields:
    /* empty */
  | func_inline_export func_param_fields
  | LPAREN KW_PARAM param_items RPAREN func_param_fields
  | LPAREN KW_RESULT result_valtype_list RPAREN func_result_fields
  | LPAREN KW_LOCAL local_items RPAREN func_local_fields
  | instr func_body
    ;

func_result_fields:
    /* empty */
  | func_inline_export func_result_fields
  | LPAREN KW_RESULT result_valtype_list RPAREN func_result_fields
  | LPAREN KW_LOCAL local_items RPAREN func_local_fields
  | instr func_body
    ;

func_local_fields:
    /* empty */
  | func_inline_export func_local_fields
  | LPAREN KW_LOCAL local_items RPAREN func_local_fields
  | instr func_body
    ;

/* Inline imports have a signature but no local declarations or body. */
func_import_fields:
    /* empty */
  | LPAREN KW_TYPE any_idx RPAREN {
        set_func_type_ref(script, $3, @3.first_line, @3.first_column);
    } func_import_param_fields
  | LPAREN KW_PARAM param_items RPAREN func_import_param_fields
  | LPAREN KW_RESULT result_valtype_list RPAREN func_import_result_fields
    ;

func_import_param_fields:
    /* empty */
  | LPAREN KW_PARAM param_items RPAREN func_import_param_fields
  | LPAREN KW_RESULT result_valtype_list RPAREN func_import_result_fields
    ;

func_import_result_fields:
    /* empty */
  | LPAREN KW_RESULT result_valtype_list RPAREN func_import_result_fields
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

param_list:
    /* empty */
  | param_list LPAREN KW_PARAM {
        if (g_signature_seen_result)
            report_validation_error(script,
                                    "parameter field after result field");
    } param_items RPAREN
  /* Permit the following signature attribute to be consumed while resolving
   * the shared LPAREN prefix; result_list remains a no-op afterwards. */
  | param_list LPAREN KW_RESULT {
        g_signature_seen_result = 1;
    } result_valtype_list RPAREN
    ;

param_items:
    /* empty */ {
        if (g_in_func) g_cur_func.has_inline_params = 1;
    }
  | param_items ID valtype {
        /* named param: $name type */
        if (g_in_func && g_cur_func.param_count < WAST_MAX_PARAMS) {
            g_cur_func.params[g_cur_func.param_count] = $3;
            g_cur_func.param_count++;
            append_local_name(script, $2);
        }
    }
  | param_items ATOM valtype {
        /* named param: $name type */
        if (g_in_func && g_cur_func.param_count < WAST_MAX_PARAMS) {
            int idx = g_cur_func.param_count;
            g_cur_func.params[idx] = $3;
            g_cur_func.param_count++;
            append_local_name(script, $2);
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
    /* empty */ {
        if (g_in_func) g_cur_func.has_inline_results = 1;
    }
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
    /* empty */ {
        ensure_inherited_param_slots(script);
    }
  | local_items ID valtype {
        /* named local */
        if (g_in_func && g_cur_func.local_count < WAST_MAX_LOCALS) {
            g_cur_func.locals[g_cur_func.local_count++] = $3;
            append_local_name(script, $2);
        }
    }
  | local_items ATOM valtype {
        /* named local */
        if (g_in_func && g_cur_func.local_count < WAST_MAX_LOCALS) {
            g_cur_func.locals[g_cur_func.local_count++] = $3;
            append_local_name(script, $2);
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
  | KW_SELECT select_result_fields {
        emit_byte(script,0x1C);
        emit_leb_u32(script,(uint32_t)g_select_result_count);
        for (int i = 0; i < g_select_result_count; i++)
            emit_byte(script,valtype_byte(g_select_result_types[i]));
    }
  | KW_LOCAL_GET any_idx  { emit_byte(script,0x20); emit_leb_u32(script,resolve_local($2)); }
  | KW_LOCAL_SET any_idx  { emit_byte(script,0x21); emit_leb_u32(script,resolve_local($2)); }
  | KW_LOCAL_TEE any_idx  { emit_byte(script,0x22); emit_leb_u32(script,resolve_local($2)); }
  | KW_GLOBAL_GET any_idx { emit_byte(script,0x23); emit_index_ref(script,IDX_GLOBAL,$2,@2.first_line,@2.first_column); }
  | KW_GLOBAL_SET any_idx { emit_byte(script,0x24); emit_index_ref(script,IDX_GLOBAL,$2,@2.first_line,@2.first_column); }
  | KW_MEMORY_SIZE { emit_byte(script,0x3F); emit_byte(script,0x00); }
  | KW_MEMORY_SIZE index_ref { emit_byte(script,0x3F); emit_index_ref(script,IDX_MEMORY,$2,@2.first_line,@2.first_column); }
  | KW_MEMORY_GROW { emit_byte(script,0x40); emit_byte(script,0x00); }
  | KW_MEMORY_GROW index_ref { emit_byte(script,0x40); emit_index_ref(script,IDX_MEMORY,$2,@2.first_line,@2.first_column); }
  | KW_MEMORY_FILL { emit_byte(script,0xFC); emit_leb_u32(script,11); emit_byte(script,0x00); }
  | KW_MEMORY_FILL index_ref { emit_byte(script,0xFC); emit_leb_u32(script,11); emit_index_ref(script,IDX_MEMORY,$2,@2.first_line,@2.first_column); }
  | KW_MEMORY_COPY { emit_byte(script,0xFC); emit_leb_u32(script,10); emit_byte(script,0x00); emit_byte(script,0x00); }
  | KW_MEMORY_COPY index_ref index_ref { emit_byte(script,0xFC); emit_leb_u32(script,10); emit_index_ref(script,IDX_MEMORY,$2,@2.first_line,@2.first_column); emit_index_ref(script,IDX_MEMORY,$3,@3.first_line,@3.first_column); }
  | KW_MEMORY_INIT index_ref { emit_byte(script,0xFC); emit_leb_u32(script,8); emit_index_ref(script,IDX_DATA,$2,@2.first_line,@2.first_column); emit_byte(script,0x00); }
  | KW_MEMORY_INIT index_ref index_ref { emit_byte(script,0xFC); emit_leb_u32(script,8); emit_index_ref(script,IDX_DATA,$3,@3.first_line,@3.first_column); emit_index_ref(script,IDX_MEMORY,$2,@2.first_line,@2.first_column); }
  | OP index_ref {
        uint8_t op = memop_by_name($1);
        if (op) {
            emit_byte(script, op);
            emit_leb_u32(script, default_align(op) | 0x40u);
            emit_index_ref(script, IDX_MEMORY, $2,
                           @2.first_line, @2.first_column);
            emit_leb_u32(script, 0);
        } else {
            wast_simd_info simd;
            if (wast_simd_lookup($1, &simd)) {
                emit_byte(script, 0xFD); emit_leb_u32(script, simd.opcode);
                if (simd.immediate == WAST_SIMD_IMM_LANE) {
                    int64_t lane = (int64_t)strtol($2, NULL, 0);
                    if (lane < 0 || lane >= simd.lane_count)
                        report_validation_error(script,"lane index out of range");
                    emit_byte(script, (uint8_t)lane);
                }
                else if (simd.immediate == WAST_SIMD_IMM_MEMARG) {
                    emit_leb_u32(script, simd.natural_alignment | 0x40u);
                    emit_index_ref(script, IDX_MEMORY, $2,
                                   @2.first_line, @2.first_column);
                    emit_leb_u32(script, 0);
                }
            } else {
                /* GC single-immediate instructions: array.get, array.set, etc. */
                emit_gc_accessor(script, $1, $2, @2.first_line, @2.first_column);
            }
        }
    }
  | OP index_ref memarg_nonempty {
        uint8_t op = memop_by_name($1);
        if (op) {
            uint32_t align_log2 = $3.alignment;
            emit_byte(script, op); emit_leb_u32(script, align_log2 | 0x40u);
            emit_index_ref(script, IDX_MEMORY, $2, @2.first_line, @2.first_column);
            emit_leb_u64(script, $3.offset);
        } else {
            wast_simd_info simd;
            if (wast_simd_lookup($1, &simd) &&
                simd.immediate == WAST_SIMD_IMM_MEMARG) {
                uint32_t align_log2 = $3.alignment;
                emit_byte(script, 0xFD); emit_leb_u32(script, simd.opcode);
                emit_leb_u32(script, align_log2 | 0x40u);
                emit_index_ref(script, IDX_MEMORY, $2,
                               @2.first_line, @2.first_column);
                emit_leb_u64(script, $3.offset);
            }
        }
    }
  | KW_TABLE_SIZE           { emit_byte(script,0xFC); emit_leb_u32(script,16); emit_byte(script,0x00); }
  | KW_TABLE_SIZE any_idx   { emit_byte(script,0xFC); emit_leb_u32(script,16); emit_index_ref(script,IDX_TABLE,$2,@2.first_line,@2.first_column); }
  | KW_TABLE_GET  any_idx   { emit_byte(script,0x25); emit_index_ref(script,IDX_TABLE,$2,@2.first_line,@2.first_column); }
  | KW_TABLE_SET  any_idx   { emit_byte(script,0x26); emit_index_ref(script,IDX_TABLE,$2,@2.first_line,@2.first_column); }
  | KW_TABLE_GROW any_idx   { emit_byte(script,0xFC); emit_leb_u32(script,15); emit_index_ref(script,IDX_TABLE,$2,@2.first_line,@2.first_column); }
  | KW_BR    any_idx { emit_byte(script,0x0C); emit_leb_u32(script,resolve_label(script,$2,@2.first_line,@2.first_column)); }
  | KW_BR_IF       any_idx { emit_byte(script,0x0D); emit_leb_u32(script,resolve_label(script,$2,@2.first_line,@2.first_column)); }
  | KW_BR_ON_NULL     any_idx { emit_byte(script,0xD5); emit_leb_u32(script,resolve_label(script,$2,@2.first_line,@2.first_column)); }
  | KW_BR_ON_NON_NULL any_idx { emit_byte(script,0xD6); emit_leb_u32(script,resolve_label(script,$2,@2.first_line,@2.first_column)); }
  | KW_CALL  any_idx { emit_byte(script,0x10); emit_func_ref(script,$2,@2.first_line,@2.first_column); }
  | KW_CALL_REF any_idx { emit_byte(script,0x14); emit_leb_u32(script,(uint32_t)resolve_type($2)); }
  | KW_RETURN_CALL any_idx { emit_byte(script,0x12); emit_func_ref(script,$2,@2.first_line,@2.first_column); }
  | KW_RETURN_CALL_REF any_idx { emit_byte(script,0x15); emit_leb_u32(script,(uint32_t)resolve_type($2)); }
  | KW_REF_NULL  reftype  { emit_byte(script,0xD0); emit_byte(script,(uint8_t)$2); }
  | KW_REF_NULL  any_idx  { emit_byte(script,0xD0); emit_leb_s32(script,indexed_heap_type(script,$2,@2.first_line,@2.first_column)); }
  | KW_REF_IS_NULL        { emit_byte(script,0xD1); }
  | KW_REF_AS_NON_NULL    { emit_byte(script,0xD4); }
  | KW_REF_FUNC any_idx   { emit_byte(script,0xD2); emit_func_ref(script,$2,@2.first_line,@2.first_column); }
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
  | SIMD_SHUFFLE_OP lane_index lane_index lane_index lane_index lane_index lane_index lane_index lane_index
                    lane_index lane_index lane_index lane_index lane_index lane_index lane_index lane_index {
        wast_simd_info simd;
        wast_simd_lookup($1, &simd);
        emit_byte(script, 0xFD); emit_leb_u32(script, simd.opcode);
        const int64_t lanes[16] = {$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17};
        for (int i = 0; i < 16; i++) {
            if (lanes[i] < 0 || lanes[i] >= 32)
                report_validation_error(script,"shuffle lane out of range");
            emit_byte(script, (uint8_t)lanes[i]);
        }
    }
  /* GC plain instructions */
  | OP gc_casttype {
        emit_ref_test_cast(script, $1, $2);
    }
  | OP any_idx gc_casttype gc_casttype {
        uint32_t label = resolve_label(script, $2, @2.first_line, @2.first_column);
        emit_br_on_cast(script, $1, label, $3, $4);
    }
  /* struct.get/set/get_s/get_u plain form — dedicated keyword tokens */
  | KW_STRUCT_GET any_idx any_idx {
        emit_gc_struct_access(script, "struct.get", $2, @2.first_line, @2.first_column,
                              $3, @3.first_line, @3.first_column);
    }
  | KW_STRUCT_GET_S any_idx any_idx {
        emit_gc_struct_access(script, "struct.get_s", $2, @2.first_line, @2.first_column,
                              $3, @3.first_line, @3.first_column);
    }
  | KW_STRUCT_GET_U any_idx any_idx {
        emit_gc_struct_access(script, "struct.get_u", $2, @2.first_line, @2.first_column,
                              $3, @3.first_line, @3.first_column);
    }
  | KW_STRUCT_SET any_idx any_idx {
        emit_gc_struct_access(script, "struct.set", $2, @2.first_line, @2.first_column,
                              $3, @3.first_line, @3.first_column);
    }
  | block_plain
  | loop_plain
  | if_plain
  | br_table_plain
  | call_indirect_plain
  | ATOM memarg {
        uint8_t op = memop_by_name($1);
        if (op) {
            uint32_t align_log2 = $2.alignment;
            uint64_t offset = $2.offset;
            if (align_log2 == 0 && offset == 0) align_log2 = default_align(op);
            emit_byte(script, op); emit_leb_u32(script, align_log2); emit_leb_u64(script, offset);
        } else {
            if (!emit_atom_op(script,$1) && script->strict_wat_mode)
                report_validation_error(script,"unknown instruction operator");
        }
    }
  | OP {
        uint8_t op = memop_by_name($1);
        if (op) {
            emit_byte(script, op);
            emit_leb_u32(script, default_align(op));
            emit_leb_u32(script, 0);
        } else {
            wast_simd_info simd;
            if (wast_simd_lookup($1, &simd) &&
                simd.immediate == WAST_SIMD_IMM_MEMARG) {
                emit_atom_op(script,$1);
                emit_leb_u32(script, simd.natural_alignment);
                emit_leb_u32(script, 0);
            } else if (!emit_atom_op(script,$1) && script->strict_wat_mode)
                report_validation_error(script,"unknown instruction operator");
        }
    }
  | OP memarg_nonempty {
        uint8_t op = memop_by_name($1);
        if (op) {
            uint32_t align_log2 = $2.alignment;
            emit_byte(script, op); emit_leb_u32(script, align_log2); emit_leb_u64(script, $2.offset);
        } else {
            wast_simd_info simd;
            if (wast_simd_lookup($1, &simd) &&
                simd.immediate == WAST_SIMD_IMM_MEMARG) {
                emit_atom_op(script,$1);
                emit_leb_u32(script, $2.alignment);
                emit_leb_u64(script, $2.offset);
            } else if (!emit_atom_op(script,$1) && script->strict_wat_mode)
                report_validation_error(script,"unknown instruction operator");
        }
    }
    ;

/* f32.const / f64.const plain forms - we handle via inline rules */
float_or_int:
    FLOAT   { emit_byte(script,0x43); emit_f32(script,(float)$1); }
  | any_int { emit_byte(script,0x43); emit_f32(script,(float)$1); }
  | KW_NAN_CANONICAL { emit_byte(script,0x43); uint32_t b=0x7FC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_NAN_ARITHMETIC{ emit_byte(script,0x43); uint32_t b=0x7FC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_NEG_NAN       { emit_byte(script,0x43); uint32_t b=0xFFC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_POS_NAN       { emit_byte(script,0x43); uint32_t b=0x7FC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_NAN           { emit_byte(script,0x43); uint32_t b=0x7FC00000u; float f; memcpy(&f,&b,4); emit_f32(script,f); }
  | KW_INF           { emit_byte(script,0x43); float f=1.0f/0.0f; emit_f32(script,f); }
  | KW_POS_INF       { emit_byte(script,0x43); float f=1.0f/0.0f; emit_f32(script,f); }
  | KW_NEG_INF       { emit_byte(script,0x43); float f=-1.0f/0.0f; emit_f32(script,f); }
  | ATOM { /* nan:0xHEX payload — preserve exact payload bits, no quiet-bit injection */
        emit_byte(script,0x43);
        const char *s = $1; int neg = (*s == '-'); if (neg || *s == '+') s++;
        uint32_t b = 0x7FC00000u;
        if (strncmp(s,"nan:0x",6)==0) b = (neg?0xFF800000u:0x7F800000u)|(uint32_t)(strtoul(s+6,NULL,16)&0x7FFFFFu);
        else if (neg) b = 0xFFC00000u;
        float f; memcpy(&f,&b,4); emit_f32(script,f);
    }
    ;

float_or_int_64:
    FLOAT   { emit_byte(script,0x44); emit_f64(script,$1); }
  | any_int { emit_byte(script,0x44); emit_f64(script,(double)$1); }
  | KW_NAN_CANONICAL { emit_byte(script,0x44); uint64_t b=0x7FF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_NAN_ARITHMETIC{ emit_byte(script,0x44); uint64_t b=0x7FF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_NEG_NAN       { emit_byte(script,0x44); uint64_t b=0xFFF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_POS_NAN       { emit_byte(script,0x44); uint64_t b=0x7FF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_NAN           { emit_byte(script,0x44); uint64_t b=0x7FF8000000000000ULL; double d; memcpy(&d,&b,8); emit_f64(script,d); }
  | KW_INF           { emit_byte(script,0x44); double d=1.0/0.0; emit_f64(script,d); }
  | KW_POS_INF       { emit_byte(script,0x44); double d=1.0/0.0; emit_f64(script,d); }
  | KW_NEG_INF       { emit_byte(script,0x44); double d=-1.0/0.0; emit_f64(script,d); }
  | ATOM { /* nan:0xHEX payload — preserve exact payload bits, no quiet-bit injection */
        emit_byte(script,0x44);
        const char *s = $1; int neg = (*s == '-'); if (neg || *s == '+') s++;
        uint64_t b = 0x7FF8000000000000ULL;
        if (strncmp(s,"nan:0x",6)==0) b = (neg?0xFFF0000000000000ULL:0x7FF0000000000000ULL)|(strtoull(s+6,NULL,16)&0x000FFFFFFFFFFFFFULL);
        else if (neg) b = 0xFFF8000000000000ULL;
        double d; memcpy(&d,&b,8); emit_f64(script,d);
    }
    ;

/* -----------------------------------------------------------------------
 * Block / loop / if — plain (non-folded) forms
 * --------------------------------------------------------------------- */

block_plain:
    KW_BLOCK opt_label { begin_block_type(); } blocktype {
        push_label($2);
        emit_byte(script, 0x02);
        emit_blocktype(script, $4);
    }
    func_body KW_END opt_label_end
    {
        check_label_end(script, $8, @8.first_line, @8.first_column);
        emit_byte(script, 0x0B);
        pop_label();
    }
    ;

loop_plain:
    KW_LOOP opt_label { begin_block_type(); } blocktype {
        push_label($2);
        emit_byte(script, 0x03);
        emit_blocktype(script, $4);
    }
    func_body KW_END opt_label_end
    {
        check_label_end(script, $8, @8.first_line, @8.first_column);
        emit_byte(script, 0x0B);
        pop_label();
    }
    ;

if_plain:
    KW_IF opt_label { begin_block_type(); } blocktype {
        push_label($2);
        emit_byte(script, 0x04);
        emit_blocktype(script, $4);
    }
    func_body opt_else_plain KW_END opt_label_end
    {
        check_label_end(script, $9, @9.first_line, @9.first_column);
        emit_byte(script, 0x0B);
        pop_label();
    }
    ;

opt_else_plain:
    /* empty */
  | KW_ELSE opt_label_end {
        check_label_end(script, $2, @2.first_line, @2.first_column);
        emit_byte(script, 0x05);
    } func_body
    ;

opt_label_end:
    /* empty */ { $$[0] = '\0'; }
  | ID          { snprintf($$, WAST_MAX_EXPORT_NAME, "%s", $1); }
  | ATOM        { snprintf($$, WAST_MAX_EXPORT_NAME, "%s", $1); }
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
        if (g_brtable_count < WAST_MAX_BR_TABLE_LABELS)
            g_brtable_labels[g_brtable_count++] = resolve_label(script,$1,@1.first_line,@1.first_column);
        else
            report_validation_error(script, "br_table has too many labels");
    }
  | br_table_all_labels any_idx {
        if (g_brtable_count < WAST_MAX_BR_TABLE_LABELS)
            g_brtable_labels[g_brtable_count++] = resolve_label(script,$2,@2.first_line,@2.first_column);
        else
            report_validation_error(script, "br_table has too many labels");
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
  | KW_RETURN_CALL_INDIRECT opt_table_idx typeuse {
        emit_byte(script, 0x13);
        emit_leb_u32(script, (uint32_t)$3); /* type index */
        emit_leb_u32(script, (uint32_t)$2); /* table index */
    }
    ;

opt_table_idx:
    /* empty */  { $$ = 0; }
  | any_idx      { $$ = (int)resolve_table($1); }
    ;

typeuse:
    /* empty */ {
        g_inline_param_count = 0;
        g_inline_result_count = 0;
        $$ = resolve_inline_functype(script);
    }
  | {
        g_inline_param_count = 0;
        g_inline_result_count = 0;
        g_typeuse_field_stage = 0;
    } typeuse_items {
        if ($2 >= 0) $$ = $2;
        else $$ = resolve_inline_functype(script);
    }
    ;

typeuse_items:
    typeuse_item                                  { $$ = $1; }
  | typeuse_items typeuse_item                    { $$ = $2 >= 0 ? $2 : $1; }
    ;

typeuse_item:
    LPAREN KW_TYPE any_idx RPAREN {
        if (g_typeuse_field_stage != 0)
            report_validation_error(script,
                                    "type field must precede parameter and result fields");
        g_typeuse_field_stage = 1;
        uint32_t tidx = resolve_type($3);
        if (tidx > INT32_MAX || tidx >= WAST_MAX_TYPES) {
            report_validation_error(script, "unknown type");
            tidx = 0;
        }
        /* Numeric type uses can refer to an implicit function signature that
         * is synthesized only once the complete text module is encoded.
         * Unresolved uses likewise belong in binary validation so a WAST
         * assert_invalid command can observe the error without aborting the
         * surrounding command stream. */
        $$ = (int)tidx;
    }
  | LPAREN KW_PARAM inline_param_list RPAREN {
        if (g_typeuse_field_stage > 1)
            report_validation_error(script,
                                    "parameter field must precede result fields");
        if (g_typeuse_field_stage < 1) g_typeuse_field_stage = 1;
        $$ = -1;
    }
  | LPAREN KW_RESULT inline_result_list RPAREN {
        g_typeuse_field_stage = 2;
        $$ = -1;
    }
    ;

inline_typeuse:
    LPAREN KW_PARAM inline_param_list RPAREN
  | LPAREN KW_RESULT inline_result_list RPAREN
    ;

inline_param_list:
    /* empty */
  | inline_param_list valtype {
        if (g_inline_param_count < WAST_MAX_PARAMS)
            g_inline_params[g_inline_param_count++] = $2;
    }
    ;

inline_result_list:
    /* empty */
  | inline_result_list valtype {
        if (g_inline_result_count < WAST_MAX_RESULTS)
            g_inline_results[g_inline_result_count++] = $2;
    }
    ;

/* -----------------------------------------------------------------------
 * Folded instructions
 * --------------------------------------------------------------------- */

fold_instr:
    FOLD_REF_I31_START fold_arg_list RPAREN {
        emit_byte(script, 0xfb); emit_leb_u32(script, 0x1c);
    }
  | FOLD_ARRAY_NEW_START any_idx fold_arg_list RPAREN {
        emit_byte(script, 0xfb); emit_leb_u32(script, 0x06);
        emit_index_ref(script, IDX_TYPE, $2,
                       @2.first_line, @2.first_column);
    }
  | FOLD_ARRAY_NEW_FIXED_START any_idx any_nat fold_arg_list RPAREN {
        emit_byte(script, 0xfb); emit_leb_u32(script, 0x08);
        emit_index_ref(script, IDX_TYPE, $2,
                       @2.first_line, @2.first_column);
        emit_leb_u32(script, (uint32_t)$3);
    }
  | FOLD_SIMD_MEM_START index_ref fold_arg_list RPAREN {
        wast_simd_info simd; wast_simd_lookup($1, &simd);
        emit_byte(script,0xFD); emit_leb_u32(script,simd.opcode);
        emit_leb_u32(script,simd.natural_alignment|0x40u);
        emit_index_ref(script,IDX_MEMORY,$2,@2.first_line,@2.first_column);
        emit_leb_u32(script,0);
    }
  | FOLD_SIMD_MEM_START index_ref memarg_nonempty fold_arg_list RPAREN {
        wast_simd_info simd; wast_simd_lookup($1, &simd);
        emit_byte(script,0xFD); emit_leb_u32(script,simd.opcode);
        emit_leb_u32(script,$3.alignment|0x40u);
        emit_index_ref(script,IDX_MEMORY,$2,@2.first_line,@2.first_column);
        emit_leb_u64(script,$3.offset);
    }
  | FOLD_SIMD_MEM_START fold_arg_list RPAREN {
        wast_simd_info simd; wast_simd_lookup($1, &simd);
        emit_byte(script,0xFD); emit_leb_u32(script,simd.opcode);
        emit_leb_u32(script,simd.natural_alignment); emit_leb_u32(script,0);
    }
  | FOLD_SIMD_MEM_START memarg_nonempty fold_arg_list RPAREN {
        wast_simd_info simd; wast_simd_lookup($1, &simd);
        emit_byte(script,0xFD); emit_leb_u32(script,simd.opcode);
        emit_leb_u32(script,$2.alignment); emit_leb_u64(script,$2.offset);
    }
  | FOLD_SIMD_LANE_START lane_index fold_arg_list RPAREN {
        wast_simd_info simd; wast_simd_lookup($1, &simd);
        if ($2 < 0 || $2 >= simd.lane_count)
            report_validation_error(script,"lane index out of range");
        emit_byte(script,0xFD); emit_leb_u32(script,simd.opcode); emit_byte(script,(uint8_t)$2);
    }
  | FOLD_SIMD_SHUFFLE_START lane_index lane_index lane_index lane_index lane_index lane_index lane_index lane_index
                            lane_index lane_index lane_index lane_index lane_index lane_index lane_index lane_index
                            fold_arg_list RPAREN {
        wast_simd_info simd; wast_simd_lookup($1, &simd);
        emit_byte(script,0xFD); emit_leb_u32(script,simd.opcode);
        const int64_t lanes[16] = {$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17};
        for (int i=0;i<16;i++) {
            if (lanes[i] < 0 || lanes[i] >= 32)
                report_validation_error(script,"shuffle lane out of range");
            emit_byte(script,(uint8_t)lanes[i]);
        }
    }
  | FOLD_SIMD_MEM_LANE_START lane_index fold_arg_list RPAREN {
        wast_simd_info simd; wast_simd_lookup($1, &simd);
        if ($2 < 0 || $2 >= simd.lane_count)
            report_validation_error(script,"lane index out of range");
        emit_byte(script,0xFD); emit_leb_u32(script,simd.opcode);
        emit_leb_u32(script,simd.natural_alignment); emit_leb_u32(script,0); emit_byte(script,(uint8_t)$2);
    }
  | FOLD_SIMD_MEM_LANE_START memarg_nonempty lane_index fold_arg_list RPAREN {
        wast_simd_info simd; wast_simd_lookup($1, &simd);
        if ($3 < 0 || $3 >= simd.lane_count)
            report_validation_error(script,"lane index out of range");
        emit_byte(script,0xFD); emit_leb_u32(script,simd.opcode);
        emit_leb_u32(script,$2.alignment); emit_leb_u64(script,$2.offset); emit_byte(script,(uint8_t)$3);
    }
  | FOLD_SIMD_MEM_LANE_START index_ref lane_index fold_arg_list RPAREN {
        wast_simd_info simd; wast_simd_lookup($1, &simd);
        if ($3 < 0 || $3 >= simd.lane_count)
            report_validation_error(script,"lane index out of range");
        emit_byte(script,0xFD); emit_leb_u32(script,simd.opcode);
        emit_leb_u32(script,simd.natural_alignment|0x40u);
        emit_index_ref(script,IDX_MEMORY,$2,@2.first_line,@2.first_column); emit_leb_u32(script,0); emit_byte(script,(uint8_t)$3);
    }
  | FOLD_SIMD_MEM_LANE_START index_ref memarg_nonempty lane_index fold_arg_list RPAREN {
        wast_simd_info simd; wast_simd_lookup($1, &simd);
        if ($4 < 0 || $4 >= simd.lane_count)
            report_validation_error(script,"lane index out of range");
        emit_byte(script,0xFD); emit_leb_u32(script,simd.opcode);
        emit_leb_u32(script,$3.alignment|0x40u);
        emit_index_ref(script,IDX_MEMORY,$2,@2.first_line,@2.first_column); emit_leb_u64(script,$3.offset); emit_byte(script,(uint8_t)$4);
    }
  | FOLD_MEMOP_START index_ref fold_arg_list RPAREN {
        uint8_t op = memop_by_name($1);
        emit_byte(script, op);
        emit_leb_u32(script, default_align(op) | 0x40u);
        emit_index_ref(script, IDX_MEMORY, $2,
                       @2.first_line, @2.first_column);
        emit_leb_u32(script, 0);
    }
  | FOLD_MEMOP_START index_ref memarg_nonempty fold_arg_list RPAREN {
        uint8_t op = memop_by_name($1);
        uint32_t align_log2 = $3.alignment;
        emit_byte(script, op);
        emit_leb_u32(script, align_log2 | 0x40u);
        emit_index_ref(script, IDX_MEMORY, $2,
                       @2.first_line, @2.first_column);
        emit_leb_u64(script, $3.offset);
    }
  | FOLD_MEMOP_START fold_arg_list RPAREN {
        uint8_t op = memop_by_name($1);
        emit_byte(script, op);
        emit_leb_u32(script, default_align(op));
        emit_leb_u32(script, 0);
    }
  | FOLD_MEMOP_START memarg_nonempty fold_arg_list RPAREN {
        uint8_t op = memop_by_name($1);
        uint32_t align_log2 = $2.alignment;
        emit_byte(script, op);
        emit_leb_u32(script, align_log2);
        emit_leb_u64(script, $2.offset);
    }
  | FOLD_SELECT_START RPAREN { emit_byte(script,0x1B); }
  | FOLD_SELECT_START fold_arg_list RPAREN { emit_byte(script,0x1B); }
  | FOLD_SELECT_START LPAREN KW_RESULT {
        g_select_result_count = 0;
    } select_result_types RPAREN fold_arg_list RPAREN {
        emit_byte(script,0x1C);
        emit_leb_u32(script,(uint32_t)g_select_result_count);
        for (int i = 0; i < g_select_result_count; i++)
            emit_byte(script,valtype_byte(g_select_result_types[i]));
    }
  | FOLD_DROP_START RPAREN { emit_byte(script,0x1A); }
  | FOLD_DROP_START fold_arg_list RPAREN { emit_byte(script,0x1A); }
  | FOLD_NOP_START RPAREN { emit_byte(script,0x01); }
  | FOLD_UNREACHABLE_START RPAREN { emit_byte(script,0x00); }
  | FOLD_RETURN_START RPAREN { emit_byte(script,0x0F); }
  | FOLD_RETURN_START fold_arg_list RPAREN { emit_byte(script,0x0F); }
  | FOLD_THROW_START any_idx fold_arg_list RPAREN {
        emit_byte(script, 0x08);
        emit_index_ref(script, IDX_TAG, $2,
                       @2.first_line, @2.first_column);
    }
  | FOLD_THROW_REF_START fold_arg_list RPAREN { emit_byte(script, 0x0a); }
  | fold_try_table

  /* control — br / call */
  | FOLD_BR_START    any_idx fold_arg_list RPAREN { emit_byte(script,0x0C); emit_leb_u32(script,resolve_label(script,$2,@2.first_line,@2.first_column)); }
  | FOLD_BR_IF_START any_idx fold_arg_list RPAREN { emit_byte(script,0x0D); emit_leb_u32(script,resolve_label(script,$2,@2.first_line,@2.first_column)); }
  | FOLD_BR_ON_NULL_START     any_idx fold_arg_list RPAREN { emit_byte(script,0xD5); emit_leb_u32(script,resolve_label(script,$2,@2.first_line,@2.first_column)); }
  | FOLD_BR_ON_NON_NULL_START any_idx fold_arg_list RPAREN { emit_byte(script,0xD6); emit_leb_u32(script,resolve_label(script,$2,@2.first_line,@2.first_column)); }
  | FOLD_CALL_START any_idx fold_arg_list RPAREN { emit_byte(script,0x10); emit_func_ref(script,$2,@2.first_line,@2.first_column); }
  | FOLD_CALL_REF_START any_idx fold_arg_list RPAREN { emit_byte(script,0x14); emit_leb_u32(script,(uint32_t)resolve_type($2)); }
  | FOLD_RETURN_CALL_START any_idx fold_arg_list RPAREN { emit_byte(script,0x12); emit_func_ref(script,$2,@2.first_line,@2.first_column); }
  | FOLD_RETURN_CALL_REF_START any_idx fold_arg_list RPAREN { emit_byte(script,0x15); emit_leb_u32(script,(uint32_t)resolve_type($2)); }

  /* call_indirect / return_call_indirect folded */
  | FOLD_CALL_INDIRECT_START opt_table_idx typeuse fold_arg_list RPAREN {
        emit_byte(script,0x11);
        emit_leb_u32(script,(uint32_t)$3); /* type index */
        emit_leb_u32(script,(uint32_t)$2); /* table index */
    }
  | FOLD_RETURN_CALL_INDIRECT_START opt_table_idx typeuse fold_arg_list RPAREN {
        emit_byte(script,0x13);
        emit_leb_u32(script,(uint32_t)$3); /* type index */
        emit_leb_u32(script,(uint32_t)$2); /* table index */
    }

  /* br_table folded */
  | FOLD_BR_TABLE_START {
        g_brtable_count = 0;
    } br_table_all_labels fold_arg_list RPAREN {
        uint32_t def = (g_brtable_count > 0) ? g_brtable_labels[--g_brtable_count] : 0;
        emit_byte(script,0x0E);
        emit_leb_u32(script,(uint32_t)g_brtable_count);
        for (int i = 0; i < g_brtable_count; i++)
            emit_leb_u32(script, g_brtable_labels[i]);
        emit_leb_u32(script, def);
    }

  /* consts */
  | FOLD_LOCAL_GET_START any_idx RPAREN { emit_byte(script,0x20); emit_leb_u32(script,resolve_local($2)); }
  | FOLD_LOCAL_SET_START any_idx fold_arg_list RPAREN { emit_byte(script,0x21); emit_leb_u32(script,resolve_local($2)); }
  | FOLD_LOCAL_TEE_START any_idx fold_arg_list RPAREN { emit_byte(script,0x22); emit_leb_u32(script,resolve_local($2)); }
  | FOLD_GLOBAL_GET_START any_idx RPAREN { emit_byte(script,0x23); emit_index_ref(script,IDX_GLOBAL,$2,@2.first_line,@2.first_column); }
  | FOLD_REF_NULL_START reftype RPAREN { emit_byte(script,0xD0); emit_byte(script,(uint8_t)$2); }
  | FOLD_REF_NULL_START any_idx RPAREN {
        emit_byte(script,0xD0);
        emit_leb_s32(script,indexed_heap_type(script,$2,@2.first_line,@2.first_column));
    }
  | FOLD_GLOBAL_SET_START any_idx fold_arg_list RPAREN { emit_byte(script,0x24); emit_index_ref(script,IDX_GLOBAL,$2,@2.first_line,@2.first_column); }
  | FOLD_ATOM_START any_int RPAREN {
        if (strcmp($1, "i32.const") == 0) {
            emit_byte(script, 0x41); emit_leb_s32(script, (int32_t)$2);
        } else if (strcmp($1, "i64.const") == 0) {
            emit_byte(script, 0x42); emit_leb_s64(script, (int64_t)$2);
        } else if (strcmp($1, "f32.const") == 0) {
            emit_byte(script, 0x43); emit_f32(script, (float)$2);
        } else if (strcmp($1, "f64.const") == 0) {
            emit_byte(script, 0x44); emit_f64(script, (double)$2);
        } else if (strcmp($1, "ref.func") == 0) {
            emit_byte(script, 0xD2); emit_leb_u32(script, (uint32_t)$2);
        } else if (strcmp($1, "data.drop") == 0) {
            emit_byte(script, 0xfc); emit_leb_u32(script, 9);
            emit_leb_u32(script, (uint32_t)$2);
        } else if (strcmp($1, "elem.drop") == 0) {
            emit_byte(script, 0xfc); emit_leb_u32(script, 13);
            emit_leb_u32(script, (uint32_t)$2);
        } else if (strcmp($1, "memory.init") == 0) {
            emit_byte(script, 0xfc); emit_leb_u32(script, 8);
            emit_leb_u32(script, (uint32_t)$2); emit_byte(script, 0x00);
        } else if (strcmp($1, "table.init") == 0) {
            emit_byte(script, 0xfc); emit_leb_u32(script, 12);
            emit_leb_u32(script, (uint32_t)$2); emit_byte(script, 0x00);
        } else {
            if (!emit_atom_op(script, $1) && script->strict_wat_mode)
                report_validation_error(script,"unknown instruction operator");
            emit_leb_s64(script, (int64_t)$2);
        }
    }
  | FOLD_ATOM_START ID RPAREN {
        if (strcmp($1, "ref.func") == 0) {
            emit_byte(script, 0xD2);
            emit_func_ref(script, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "table.size") == 0) {
            emit_byte(script, 0xFC); emit_leb_u32(script, 16);
            emit_index_ref(script, IDX_TABLE, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "table.get") == 0) {
            emit_byte(script, 0x25);
            emit_index_ref(script, IDX_TABLE, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "table.set") == 0) {
            emit_byte(script, 0x26);
            emit_index_ref(script, IDX_TABLE, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "table.fill") == 0) {
            /* Preserve malformed folded instructions so binary validation can
             * reject them for assert_invalid instead of silently omitting the
             * operator. */
            emit_byte(script, 0xFC); emit_leb_u32(script, 17);
            emit_index_ref(script, IDX_TABLE, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "memory.size") == 0) {
            emit_byte(script, 0x3F);
            emit_index_ref(script, IDX_MEMORY, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "data.drop") == 0) {
            emit_byte(script, 0xFC); emit_leb_u32(script, 9);
            emit_index_ref(script, IDX_DATA, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "elem.drop") == 0) {
            emit_byte(script, 0xFC); emit_leb_u32(script, 13);
            emit_index_ref(script, IDX_ELEM, $2, @2.first_line, @2.first_column);
        } else emit_gc_constructor(script, $1, $2,
                                   @2.first_line, @2.first_column);
    }
  | FOLD_ATOM_START ID ID fold_arg_list RPAREN {
        if (strcmp($1, "table.init") == 0) {
            emit_byte(script, 0xfc); emit_leb_u32(script, 12);
            emit_index_ref(script, IDX_ELEM, $3, @3.first_line, @3.first_column);
            emit_index_ref(script, IDX_TABLE, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "table.copy") == 0) {
            emit_byte(script, 0xfc); emit_leb_u32(script, 14);
            emit_index_ref(script, IDX_TABLE, $2, @2.first_line, @2.first_column);
            emit_index_ref(script, IDX_TABLE, $3, @3.first_line, @3.first_column);
        } else if (strcmp($1, "memory.copy") == 0) {
            emit_byte(script, 0xfc); emit_leb_u32(script, 10);
            emit_index_ref(script, IDX_MEMORY, $2, @2.first_line, @2.first_column);
            emit_index_ref(script, IDX_MEMORY, $3, @3.first_line, @3.first_column);
        } else if (strcmp($1, "memory.init") == 0) {
            /* (memory.init $mem $data ...) → binary: [0xFC, 8, dataidx, memidx] */
            emit_byte(script, 0xfc); emit_leb_u32(script, 8);
            emit_index_ref(script, IDX_DATA,   $3, @3.first_line, @3.first_column);
            emit_index_ref(script, IDX_MEMORY, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "struct.get") == 0 || strcmp($1, "struct.get_s") == 0 ||
                   strcmp($1, "struct.get_u") == 0 || strcmp($1, "struct.set") == 0) {
            emit_gc_struct_access(script, $1, $2, @2.first_line, @2.first_column,
                                  $3, @3.first_line, @3.first_column);
        } else if (!emit_gc_two_index(script, $1,
                                      $2, @2.first_line, @2.first_column,
                                      $3, @3.first_line, @3.first_column)) {
            /* fallthrough: not a recognized instruction */
        }
    }
  | FOLD_ATOM_START ID fold_arg_list_nonempty RPAREN {
        uint8_t memory_op = memop_by_name($1);
        if (memory_op) {
            emit_byte(script, memory_op);
            emit_leb_u32(script, default_align(memory_op) | 0x40u);
            emit_index_ref(script, IDX_MEMORY, $2,
                           @2.first_line, @2.first_column);
            emit_leb_u32(script, 0); /* offset */
        } else if (strcmp($1, "table.init") == 0) {
            emit_byte(script, 0xfc); emit_leb_u32(script, 12);
            emit_index_ref(script, IDX_ELEM, $2,
                           @2.first_line, @2.first_column);
            emit_leb_u32(script, 0); /* abbreviated form uses table 0 */
        } else if (strcmp($1, "table.get") == 0 ||
            strcmp($1, "table.set") == 0) {
            emit_byte(script, strcmp($1, "table.get") == 0 ? 0x25 : 0x26);
            emit_index_ref(script, IDX_TABLE, $2,
                           @2.first_line, @2.first_column);
        } else if (strcmp($1, "table.grow") == 0) {
            emit_byte(script, 0xFC); emit_leb_u32(script, 15);
            emit_index_ref(script, IDX_TABLE, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "table.fill") == 0) {
            emit_byte(script, 0xFC); emit_leb_u32(script, 17);
            emit_index_ref(script, IDX_TABLE, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "table.size") == 0) {
            emit_byte(script, 0xFC); emit_leb_u32(script, 16);
            emit_index_ref(script, IDX_TABLE, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "memory.grow") == 0) {
            emit_byte(script, 0x40);
            emit_index_ref(script, IDX_MEMORY, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "memory.fill") == 0) {
            emit_byte(script, 0xFC); emit_leb_u32(script, 11);
            emit_index_ref(script, IDX_MEMORY, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "memory.init") == 0) {
            /* (memory.init $data ...) — abbreviated, memory 0 */
            emit_byte(script, 0xFC); emit_leb_u32(script, 8);
            emit_index_ref(script, IDX_DATA, $2, @2.first_line, @2.first_column);
            emit_byte(script, 0x00); /* memidx = 0 */
        } else if (!emit_gc_accessor(script, $1, $2, @2.first_line, @2.first_column)) {
            emit_gc_constructor(script, $1, $2,
                                @2.first_line, @2.first_column);
        }
    }
  | FOLD_ATOM_START ID any_nat fold_arg_list RPAREN {
        if (strcmp($1, "memory.init") == 0) {
            /* (memory.init $mem N ...) where N is numeric data segment index */
            emit_byte(script, 0xfc); emit_leb_u32(script, 8);
            emit_leb_u32(script, (uint32_t)$3); /* dataidx */
            emit_index_ref(script, IDX_MEMORY, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "table.init") == 0) {
            emit_byte(script, 0xfc); emit_leb_u32(script, 12);
            emit_leb_u32(script, (uint32_t)$3);
            emit_index_ref(script, IDX_TABLE, $2, @2.first_line, @2.first_column);
        } else if (strcmp($1, "table.copy") == 0) {
            emit_byte(script, 0xfc); emit_leb_u32(script, 14);
            emit_index_ref(script, IDX_TABLE, $2, @2.first_line, @2.first_column);
            emit_leb_u32(script, (uint32_t)$3);
        } else if (strcmp($1, "memory.copy") == 0) {
            emit_byte(script, 0xfc); emit_leb_u32(script, 10);
            emit_index_ref(script, IDX_MEMORY, $2, @2.first_line, @2.first_column);
            emit_leb_u32(script, (uint32_t)$3);
        } else if (strcmp($1, "struct.get") == 0 ||
                   strcmp($1, "struct.get_s") == 0 ||
                   strcmp($1, "struct.get_u") == 0 ||
                   strcmp($1, "struct.set") == 0) {
            char field_name[32];
            snprintf(field_name, sizeof(field_name), "%lld",
                     (long long)$3);
            emit_gc_struct_access(script, $1, $2,
                                  @2.first_line, @2.first_column,
                                  field_name, @3.first_line, @3.first_column);
        } else if (emit_gc_constructor(script, $1, $2,
                                       @2.first_line, @2.first_column)) {
            emit_leb_u32(script, (uint32_t)$3);
        }
    }
  | FOLD_ATOM_START any_int fold_arg_list_nonempty RPAREN {
        if (strcmp($1, "memory.init") == 0) {
            emit_byte(script, 0xfc); emit_leb_u32(script, 8);
            emit_leb_u32(script, (uint32_t)$2);
            emit_byte(script, 0x00);
        } else if (strcmp($1, "table.init") == 0) {
            emit_byte(script, 0xfc); emit_leb_u32(script, 12);
            emit_leb_u32(script, (uint32_t)$2);
            emit_byte(script, 0x00);
        } else {
            char type_name[32];
            snprintf(type_name, sizeof(type_name), "%lld", (long long)$2);
            emit_gc_constructor(script, $1, type_name,
                                @2.first_line, @2.first_column);
        }
    }
  | FOLD_ATOM_START any_int any_nat fold_arg_list RPAREN {
        char type_name[32];
        snprintf(type_name, sizeof(type_name), "%lld", (long long)$2);
        if (strcmp($1, "struct.get") == 0 ||
            strcmp($1, "struct.get_s") == 0 ||
            strcmp($1, "struct.get_u") == 0 ||
            strcmp($1, "struct.set") == 0) {
            char field_name[32];
            snprintf(field_name, sizeof(field_name), "%lld",
                     (long long)$3);
            emit_gc_struct_access(script, $1, type_name,
                                  @2.first_line, @2.first_column,
                                  field_name, @3.first_line, @3.first_column);
        } else if (emit_gc_constructor(script, $1, type_name,
                                @2.first_line, @2.first_column))
            emit_leb_u32(script, (uint32_t)$3);
    }
  | FOLD_ATOM_START FLOAT RPAREN {
        if (strcmp($1, "f64.const") == 0) { emit_byte(script,0x44); emit_f64(script,$2); }
        else { emit_byte(script,0x43); emit_f32(script,(float)$2); }
    }
  | FOLD_ATOM_START ATOM RPAREN {
        /* Handles ±nan:0xHEX, ±inf — preserve exact payload bits */
        int is64 = (strcmp($1, "f64.const") == 0);
        if (is64) {
            uint64_t b = 0x7FF8000000000000ULL;
            const char *s = $2; int neg = (*s == '-'); if (neg || *s == '+') s++;
            if (strncmp(s, "nan:0x", 6) == 0) b = (neg ? 0xFFF0000000000000ULL : 0x7FF0000000000000ULL) | (strtoull(s+6,NULL,16) & 0x000FFFFFFFFFFFFFULL);
            else if (strcmp(s, "inf") == 0) b = neg ? 0xFFF0000000000000ULL : 0x7FF0000000000000ULL;
            double d; memcpy(&d,&b,8); emit_byte(script,0x44); emit_f64(script,d);
        } else {
            uint32_t b = 0x7FC00000U;
            const char *s = $2; int neg = (*s == '-'); if (neg || *s == '+') s++;
            if (strncmp(s, "nan:0x", 6) == 0) b = (neg ? 0xFF800000U : 0x7F800000U) | (uint32_t)(strtoul(s+6,NULL,16) & 0x7FFFFFU);
            else if (strcmp(s, "inf") == 0) b = neg ? 0xFF800000U : 0x7F800000U;
            float f; memcpy(&f,&b,4); emit_byte(script,0x43); emit_f32(script,f);
        }
    }
  | FOLD_ATOM_START KW_NAN RPAREN            { emit_byte(script, strcmp($1,"f64.const")==0?0x44:0x43); emit_fold_float_special(script,$1,0); }
  | FOLD_ATOM_START KW_NAN_CANONICAL RPAREN  { emit_byte(script, strcmp($1,"f64.const")==0?0x44:0x43); emit_fold_float_special(script,$1,0); }
  | FOLD_ATOM_START KW_NAN_ARITHMETIC RPAREN { emit_byte(script, strcmp($1,"f64.const")==0?0x44:0x43); emit_fold_float_special(script,$1,0); }
  | FOLD_ATOM_START KW_NEG_NAN RPAREN        { emit_byte(script, strcmp($1,"f64.const")==0?0x44:0x43); emit_fold_float_special(script,$1,1); }
  | FOLD_ATOM_START KW_POS_NAN RPAREN        { emit_byte(script, strcmp($1,"f64.const")==0?0x44:0x43); emit_fold_float_special(script,$1,0); }
  | FOLD_ATOM_START KW_INF RPAREN            { emit_byte(script, strcmp($1,"f64.const")==0?0x44:0x43); emit_fold_float_special(script,$1,2); }
  | FOLD_ATOM_START KW_POS_INF RPAREN        { emit_byte(script, strcmp($1,"f64.const")==0?0x44:0x43); emit_fold_float_special(script,$1,2); }
  | FOLD_ATOM_START KW_NEG_INF RPAREN        { emit_byte(script, strcmp($1,"f64.const")==0?0x44:0x43); emit_fold_float_special(script,$1,3); }
  | FOLD_ATOM_START lane_type lane_vals RPAREN {
        wasm_value v = lanes_to_v128($2, &$3, script);
        if (strcmp($1, "v128.const") != 0)
            report_validation_error(script,
                                    "lane literal is only valid for v128.const");
        emit_byte(script, 0xFD);
        emit_leb_u32(script, 12);
        for (int i = 0; i < 16; i++) emit_byte(script, v.v128.bytes[i]);
    }
  /* GC folded forms: ref.test/ref.cast with reftype immediate */
  | FOLD_ATOM_START gc_casttype fold_arg_list_nonempty RPAREN {
        emit_ref_test_cast(script, $1, $2);
    }
  | FOLD_ATOM_START gc_casttype RPAREN {
        emit_ref_test_cast(script, $1, $2);
    }
  /* GC folded: br_on_cast/br_on_cast_fail with label + two reftypes */
  | FOLD_ATOM_START any_idx gc_casttype gc_casttype fold_arg_list RPAREN {
        if (strcmp($1, "br_on_cast") == 0 || strcmp($1, "br_on_cast_fail") == 0) {
            uint32_t label = resolve_label(script, $2, @2.first_line, @2.first_column);
            emit_br_on_cast(script, $1, label, $3, $4);
        }
    }
  /* GC folded: struct.get/set with numeric type + named field */
  | FOLD_ATOM_START any_int ID fold_arg_list RPAREN {
        char type_name[32];
        snprintf(type_name, sizeof(type_name), "%lld", (long long)$2);
        if (strcmp($1, "struct.get") == 0 || strcmp($1, "struct.get_s") == 0 ||
            strcmp($1, "struct.get_u") == 0 || strcmp($1, "struct.set") == 0) {
            emit_gc_struct_access(script, $1, type_name, @2.first_line, @2.first_column,
                                  $3, @3.first_line, @3.first_column);
        }
    }
  | FOLD_ATOM_START RPAREN {
        if (strcmp($1, "table.fill") == 0 ||
            strcmp($1, "memory.fill") == 0 ||
            strcmp($1, "table.copy") == 0 ||
            strcmp($1, "memory.copy") == 0)
            report_validation_error(script, "type mismatch");
        if (!emit_atom_op(script, $1) && script->strict_wat_mode)
            report_validation_error(script,"unknown instruction operator");
    }
  | FOLD_ATOM_START fold_arg_list_nonempty RPAREN {
        if (!emit_atom_op(script, $1) && script->strict_wat_mode)
            report_validation_error(script,"unknown instruction operator");
    }

  /* block / loop / if folded */
  | fold_block
  | fold_loop
  | fold_if

    ;

fold_try_table:
    FOLD_TRY_TABLE_START {
        begin_block_type();
        g_try_catch_count = 0;
    } blocktype {
        emit_byte(script, 0x1f);
        emit_blocktype(script, $3);
    } try_catch_list {
        emit_leb_u32(script, (uint32_t)g_try_catch_count);
        for (int i = 0; i < g_try_catch_count; i++) {
            emit_byte(script, g_try_catches[i].kind);
            if (g_try_catches[i].kind < 2)
                emit_leb_u32(script, g_try_catches[i].tag);
            emit_leb_u32(script, g_try_catches[i].depth);
        }
        /* Catch destinations are resolved outside the try_table label.  The
         * body itself can branch to the try_table, so install that label only
         * after all catch immediates have been parsed. */
        push_label("");
    } fold_arg_list RPAREN {
        emit_byte(script, 0x0b);
        pop_label();
    }
    ;

try_catch_list:
    /* empty */
  | try_catch_list FOLD_CATCH_START any_idx any_idx RPAREN {
        if (g_try_catch_count < WAST_MAX_TAGS) {
            parsed_catch *catch_ = &g_try_catches[g_try_catch_count++];
            catch_->kind = 0;
            catch_->tag = resolve_tag($3);
            catch_->depth = resolve_label(script, $4,
                                          @4.first_line, @4.first_column);
        }
    }
  | try_catch_list FOLD_CATCH_ALL_START any_idx RPAREN {
        if (g_try_catch_count < WAST_MAX_TAGS) {
            parsed_catch *catch_ = &g_try_catches[g_try_catch_count++];
            catch_->kind = 2;
            catch_->tag = 0;
            catch_->depth = resolve_label(script, $3,
                                          @3.first_line, @3.first_column);
        }
    }
  | try_catch_list FOLD_CATCH_REF_START any_idx any_idx RPAREN {
        if (g_try_catch_count < WAST_MAX_TAGS) {
            parsed_catch *catch_ = &g_try_catches[g_try_catch_count++];
            catch_->kind = 1;
            catch_->tag = resolve_tag($3);
            catch_->depth = resolve_label(script, $4,
                                          @4.first_line, @4.first_column);
        }
    }
  | try_catch_list FOLD_CATCH_ALL_REF_START any_idx RPAREN {
        if (g_try_catch_count < WAST_MAX_TAGS) {
            parsed_catch *catch_ = &g_try_catches[g_try_catch_count++];
            catch_->kind = 3;
            catch_->tag = 0;
            catch_->depth = resolve_label(script, $3,
                                          @3.first_line, @3.first_column);
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
  | ATOM { /* nan:0xHEX payload — preserve exact payload bits, no quiet-bit injection */
        emit_byte(script,0x43);
        const char *s = $1; int neg = (*s == '-'); if (neg || *s == '+') s++;
        uint32_t b = 0x7FC00000u;
        if (strncmp(s,"nan:0x",6)==0) b = (neg?0xFF800000u:0x7F800000u)|(uint32_t)(strtoul(s+6,NULL,16)&0x7FFFFFu);
        else if (neg) b = 0xFFC00000u;
        float f; memcpy(&f,&b,4); emit_f32(script,f);
    }
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
  | ATOM { /* nan:0xHEX payload — preserve exact payload bits, no quiet-bit injection */
        emit_byte(script,0x44);
        const char *s = $1; int neg = (*s == '-'); if (neg || *s == '+') s++;
        uint64_t b = 0x7FF8000000000000ULL;
        if (strncmp(s,"nan:0x",6)==0) b = (neg?0xFFF0000000000000ULL:0x7FF0000000000000ULL)|(strtoull(s+6,NULL,16)&0x000FFFFFFFFFFFFFULL);
        else if (neg) b = 0xFFF8000000000000ULL;
        double d; memcpy(&d,&b,8); emit_f64(script,d);
    }
    ;

/* -----------------------------------------------------------------------
 * Folded block / loop / if
 * --------------------------------------------------------------------- */

fold_block:
    FOLD_BLOCK_START opt_label { begin_block_type(); } blocktype {
        push_label($2);
        emit_byte(script, 0x02);
        emit_blocktype(script, $4);
    }
    fold_body RPAREN {
        emit_byte(script, 0x0B);
        pop_label();
    }
    ;

fold_loop:
    FOLD_LOOP_START opt_label { begin_block_type(); } blocktype {
        push_label($2);
        emit_byte(script, 0x03);
        emit_blocktype(script, $4);
    }
    fold_body RPAREN {
        emit_byte(script, 0x0B);
        pop_label();
    }
    ;

fold_if:
    fold_if_start blocktype {
        push_label($1); emit_byte(script, 0x04); emit_blocktype(script, $2);
    }
    FOLD_THEN_START fold_body RPAREN opt_fold_else RPAREN {
        emit_byte(script, 0x0B); pop_label();
    }
  |
    fold_if_start blocktype fold_arg_list_nonempty {
        push_label($1);
        emit_byte(script, 0x04);
        emit_blocktype(script, $2);
    }
    FOLD_THEN_START fold_body RPAREN
    opt_fold_else
    RPAREN {
        emit_byte(script, 0x0B);
        pop_label();
    }
    ;

fold_if_start:
    FOLD_IF_START opt_label {
        begin_block_type();
        snprintf($$, WAST_MAX_EXPORT_NAME, "%s", $2);
    }
    ;

opt_fold_else:
    /* empty */
  | FOLD_ELSE_START opt_label_end { emit_byte(script, 0x05); } fold_body RPAREN
    ;

/* -----------------------------------------------------------------------
 * fold_arg_list — list of folded instrs used as arguments
 * --------------------------------------------------------------------- */

select_result_types:
    /* empty */
  | select_result_types valtype {
        if (g_select_result_count < WAST_MAX_RESULTS)
            g_select_result_types[g_select_result_count++] = $2;
    }
    ;

select_result_fields:
    LPAREN KW_RESULT {
        g_select_result_count = 0;
    } select_result_types RPAREN
  | select_result_fields LPAREN KW_RESULT select_result_types RPAREN
    ;

fold_arg_list:
    /* empty */
  | fold_arg_list fold_instr
    ;

fold_arg_list_nonempty:
    fold_instr
  | fold_arg_list_nonempty fold_instr
    ;

/* A folded control construct contains only complete folded expressions.  Keep
 * this boundary distinct from the top-level function body: allowing plain
 * instructions and an empty recursive body here makes RPAREN/LPAREN choices
 * ambiguous in a deterministic parser. */
fold_body:
    /* empty */
  | fold_body instr
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
    /* empty */                           { $$ = finish_block_type(script); }
  | block_typeuse block_param_type        { $$ = $2; }
  | block_param_field block_param_type    { $$ = $2; }
  | block_result_field block_result_type  { $$ = $2; }
    ;

block_param_type:
    /* empty */                           { $$ = finish_block_type(script); }
  | block_param_field block_param_type    { $$ = $2; }
  | block_result_field block_result_type  { $$ = $2; }
    ;

block_result_type:
    /* empty */                           { $$ = finish_block_type(script); }
  | block_result_field block_result_type  { $$ = $2; }
    ;

block_typeuse:
    LPAREN KW_TYPE any_idx RPAREN {
        g_blocktype_explicit = (int)resolve_type($3);
    }
    ;

block_param_field:
    LPAREN KW_PARAM blocktype_param_types RPAREN
    ;

block_result_field:
    LPAREN KW_RESULT blocktype_result_types RPAREN
    ;

blocktype_param_types:
    /* empty */
  | blocktype_param_types valtype {
        if (g_blocktype_param_count < WAST_MAX_PARAMS)
            g_blocktype_params[g_blocktype_param_count++] = $2;
    }
    ;

blocktype_result_types:
    /* empty */
  | blocktype_result_types valtype {
        if (g_blocktype_result_count < WAST_MAX_RESULTS)
            g_blocktype_results[g_blocktype_result_count++] = $2;
    }
    ;

reftype:
    KW_FUNCREF    { $$ = 0x70; }
  | KW_EXTERNREF  { $$ = 0x6F; }
  | KW_ANYREF     { $$ = 0x6E; }
  | KW_EQREF      { $$ = 0x6D; }
  | KW_I31REF     { $$ = 0x6C; }
  | KW_STRUCTREF  { $$ = 0x6B; }
  | KW_ARRAYREF   { $$ = 0x6A; }
  | KW_FUNC       { $$ = 0x70; }
  | KW_EXTERN     { $$ = 0x6F; }
  | KW_ANY        { $$ = 0x6E; }
  | KW_EQ         { $$ = 0x6D; }
  | KW_I31        { $$ = 0x6C; }
  | KW_STRUCT     { $$ = 0x6B; }
  | KW_ARRAY      { $$ = 0x6A; }
  | KW_EXN        { $$ = 0x69; }
  | KW_NONE       { $$ = 0x71; }
  | KW_NOFUNC     { $$ = 0x73; }
  | KW_NOEXN      { $$ = 0x74; }
  | KW_NOEXTERN   { $$ = 0x72; }
  | KW_EXNREF     { $$ = 0x69; }
  | KW_REF_EXTERN { $$ = 0x6F; }
  | LPAREN KW_REF_TYPE KW_NULL KW_FUNC RPAREN { $$ = 0x70; }
  | LPAREN KW_REF_TYPE KW_NULL KW_EXTERN RPAREN { $$ = 0x6F; }
  | LPAREN KW_REF_TYPE KW_NULL KW_EXN RPAREN { $$ = 0x69; }
  | LPAREN KW_REF_TYPE KW_NULL KW_ANY RPAREN { $$ = 0x6E; }
  | LPAREN KW_REF_TYPE KW_NULL KW_EQ RPAREN { $$ = 0x6D; }
  | LPAREN KW_REF_TYPE KW_NULL KW_I31 RPAREN { $$ = 0x6C; }
  | LPAREN KW_REF_TYPE KW_NULL KW_STRUCT RPAREN { $$ = 0x6B; }
  | LPAREN KW_REF_TYPE KW_NULL KW_ARRAY RPAREN { $$ = 0x6A; }
  | LPAREN KW_REF_TYPE KW_FUNC RPAREN { $$ = 0x70; }
  | LPAREN KW_REF_TYPE KW_EXTERN RPAREN { $$ = 0x6F; }
  | LPAREN KW_REF_TYPE KW_ANY RPAREN { $$ = 0x6E; }
  | LPAREN KW_REF_TYPE KW_EQ RPAREN { $$ = 0x6D; }
  | LPAREN KW_REF_TYPE KW_I31 RPAREN { $$ = 0x6C; }
  | LPAREN KW_REF_TYPE KW_STRUCT RPAREN { $$ = 0x6B; }
  | LPAREN KW_REF_TYPE KW_ARRAY RPAREN { $$ = 0x6A; }
  | LPAREN KW_REF_TYPE KW_EXN RPAREN { $$ = 0x69; }
  | LPAREN KW_REF_TYPE KW_NULL any_idx RPAREN {
        $$ = indexed_ref_type(script, $4, 1);
    }
  | LPAREN KW_REF_TYPE any_idx RPAREN {
        $$ = indexed_ref_type(script, $3, 0);
    }
    ;

reftype_as_valtype:
    KW_FUNCREF   { $$ = WASM_VALTYPE_FUNCREF; }
  | KW_EXTERNREF { $$ = WASM_VALTYPE_EXTERNREF; }
  | KW_ANYREF    { $$ = WASM_VALTYPE_ANYREF; }
  | KW_EQREF     { $$ = WASM_VALTYPE_EQREF; }
  | KW_I31REF    { $$ = WASM_VALTYPE_I31REF; }
  | KW_STRUCTREF { $$ = WASM_VALTYPE_STRUCTREF; }
  | KW_ARRAYREF  { $$ = WASM_VALTYPE_ARRAYREF; }
  | KW_EXNREF    { $$ = WASM_VALTYPE_EXNREF; }
    ;

/* gc_casttype: returns int64_t encoding nullability + heap type for ref.test/ref.cast/br_on_cast */
gc_casttype:
    KW_FUNCREF     { $$ = GC_CT_ABS_NULL(0x70); }
  | KW_EXTERNREF   { $$ = GC_CT_ABS_NULL(0x6F); }
  | KW_ANYREF      { $$ = GC_CT_ABS_NULL(0x6E); }
  | KW_EQREF       { $$ = GC_CT_ABS_NULL(0x6D); }
  | KW_I31REF      { $$ = GC_CT_ABS_NULL(0x6C); }
  | KW_STRUCTREF   { $$ = GC_CT_ABS_NULL(0x6B); }
  | KW_ARRAYREF    { $$ = GC_CT_ABS_NULL(0x6A); }
  | KW_NULLREF     { $$ = GC_CT_ABS_NULL(0x71); }
  | KW_NULLFUNCREF { $$ = GC_CT_ABS_NULL(0x73); }
  | KW_NULLEXTERNREF { $$ = GC_CT_ABS_NULL(0x72); }
  | KW_EXNREF      { $$ = GC_CT_ABS_NULL(0x69); }
  | LPAREN KW_REF_TYPE KW_NULL KW_FUNC RPAREN   { $$ = GC_CT_ABS_NULL(0x70); }
  | LPAREN KW_REF_TYPE KW_NULL KW_EXTERN RPAREN  { $$ = GC_CT_ABS_NULL(0x6F); }
  | LPAREN KW_REF_TYPE KW_NULL KW_ANY RPAREN     { $$ = GC_CT_ABS_NULL(0x6E); }
  | LPAREN KW_REF_TYPE KW_NULL KW_EQ RPAREN      { $$ = GC_CT_ABS_NULL(0x6D); }
  | LPAREN KW_REF_TYPE KW_NULL KW_I31 RPAREN     { $$ = GC_CT_ABS_NULL(0x6C); }
  | LPAREN KW_REF_TYPE KW_NULL KW_STRUCT RPAREN   { $$ = GC_CT_ABS_NULL(0x6B); }
  | LPAREN KW_REF_TYPE KW_NULL KW_ARRAY RPAREN    { $$ = GC_CT_ABS_NULL(0x6A); }
  | LPAREN KW_REF_TYPE KW_NULL KW_NONE RPAREN     { $$ = GC_CT_ABS_NULL(0x71); }
  | LPAREN KW_REF_TYPE KW_NULL KW_NOFUNC RPAREN   { $$ = GC_CT_ABS_NULL(0x73); }
  | LPAREN KW_REF_TYPE KW_NULL KW_NOEXTERN RPAREN { $$ = GC_CT_ABS_NULL(0x72); }
  | LPAREN KW_REF_TYPE KW_NULL KW_EXN RPAREN      { $$ = GC_CT_ABS_NULL(0x69); }
  | LPAREN KW_REF_TYPE KW_FUNC RPAREN   { $$ = GC_CT_ABS_NONNULL(0x70); }
  | LPAREN KW_REF_TYPE KW_EXTERN RPAREN  { $$ = GC_CT_ABS_NONNULL(0x6F); }
  | LPAREN KW_REF_TYPE KW_ANY RPAREN     { $$ = GC_CT_ABS_NONNULL(0x6E); }
  | LPAREN KW_REF_TYPE KW_EQ RPAREN      { $$ = GC_CT_ABS_NONNULL(0x6D); }
  | LPAREN KW_REF_TYPE KW_I31 RPAREN     { $$ = GC_CT_ABS_NONNULL(0x6C); }
  | LPAREN KW_REF_TYPE KW_STRUCT RPAREN   { $$ = GC_CT_ABS_NONNULL(0x6B); }
  | LPAREN KW_REF_TYPE KW_ARRAY RPAREN    { $$ = GC_CT_ABS_NONNULL(0x6A); }
  | LPAREN KW_REF_TYPE KW_NONE RPAREN     { $$ = GC_CT_ABS_NONNULL(0x71); }
  | LPAREN KW_REF_TYPE KW_NOFUNC RPAREN   { $$ = GC_CT_ABS_NONNULL(0x73); }
  | LPAREN KW_REF_TYPE KW_NOEXTERN RPAREN { $$ = GC_CT_ABS_NONNULL(0x72); }
  | LPAREN KW_REF_TYPE KW_EXN RPAREN      { $$ = GC_CT_ABS_NONNULL(0x69); }
  | LPAREN KW_REF_TYPE KW_NULL any_idx RPAREN {
        $$ = GC_CT_IDX_NULL(resolve_type($4));
    }
  | LPAREN KW_REF_TYPE any_idx RPAREN {
        $$ = GC_CT_IDX_NONNULL(resolve_type($3));
    }
    ;

table_reftype:
    KW_FUNCREF   { $$ = WASM_VALTYPE_FUNCREF; }
  | KW_EXTERNREF { $$ = WASM_VALTYPE_EXTERNREF; }
  | KW_ANYREF    { $$ = WASM_VALTYPE_ANYREF; }
  | KW_EQREF     { $$ = WASM_VALTYPE_EQREF; }
  | KW_I31REF    { $$ = WASM_VALTYPE_I31REF; }
  | KW_STRUCTREF { $$ = WASM_VALTYPE_STRUCTREF; }
  | KW_ARRAYREF  { $$ = WASM_VALTYPE_ARRAYREF; }
  | KW_FUNC      { $$ = WASM_VALTYPE_FUNCREF; }
  | KW_EXTERN    { $$ = WASM_VALTYPE_EXTERNREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_FUNC RPAREN { $$ = WASM_VALTYPE_FUNCREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_EXTERN RPAREN { $$ = WASM_VALTYPE_EXTERNREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_ANY RPAREN { $$ = WASM_VALTYPE_ANYREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_EQ RPAREN { $$ = WASM_VALTYPE_EQREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_I31 RPAREN { $$ = WASM_VALTYPE_I31REF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_STRUCT RPAREN { $$ = WASM_VALTYPE_STRUCTREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_ARRAY RPAREN { $$ = WASM_VALTYPE_ARRAYREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_NONE RPAREN { $$ = WASM_VALTYPE_NULLREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_NOFUNC RPAREN { $$ = WASM_VALTYPE_NULLFUNCREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_NOEXN RPAREN { $$ = WASM_VALTYPE_NULLEXNREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_NOEXTERN RPAREN { $$ = WASM_VALTYPE_NULLEXTERNREF; }
  | LPAREN KW_REF_TYPE KW_FUNC RPAREN { $$ = WASM_VALTYPE_FUNCREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_EXTERN RPAREN { $$ = WASM_VALTYPE_EXTERNREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_ANY RPAREN { $$ = WASM_VALTYPE_ANYREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_EQ RPAREN { $$ = WASM_VALTYPE_EQREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_I31 RPAREN { $$ = WASM_VALTYPE_I31REF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_STRUCT RPAREN { $$ = WASM_VALTYPE_STRUCTREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_ARRAY RPAREN { $$ = WASM_VALTYPE_ARRAYREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_NONE RPAREN { $$ = WASM_VALTYPE_NULLREF; }
  | LPAREN KW_REF_TYPE KW_NOFUNC RPAREN { $$ = WASM_VALTYPE_NULLFUNCREF; }
  | LPAREN KW_REF_TYPE KW_NOEXN RPAREN { $$ = WASM_VALTYPE_NULLEXNREF; }
  | LPAREN KW_REF_TYPE KW_NOEXTERN RPAREN { $$ = WASM_VALTYPE_NULLEXTERNREF; }
  | LPAREN KW_REF_TYPE KW_NULL any_idx RPAREN { $$ = indexed_ref_type(script,$4,1); }
  | LPAREN KW_REF_TYPE any_idx RPAREN { $$ = indexed_ref_type(script,$3,0); }
    ;

any_idx:
    INT    { snprintf($$, WAST_MAX_EXPORT_NAME, "%lld", (long long)$1); }
  | HEXINT { snprintf($$, WAST_MAX_EXPORT_NAME, "%llu", (unsigned long long)(uint64_t)$1); }
  | ID     { snprintf($$, WAST_MAX_EXPORT_NAME, "%s", $1); }
  | ATOM   { snprintf($$, WAST_MAX_EXPORT_NAME, "%s", $1); }
    ;

/* Core indices are naturals or symbolic identifiers.  Keeping arbitrary
 * atoms out of this production lets a following plain instruction remain an
 * unambiguous command boundary when an instruction has an optional index. */
index_ref:
    INT    { snprintf($$, WAST_MAX_EXPORT_NAME, "%lld", (long long)$1); }
  | HEXINT { snprintf($$, WAST_MAX_EXPORT_NAME, "%llu", (unsigned long long)(uint64_t)$1); }
  | ID     { snprintf($$, WAST_MAX_EXPORT_NAME, "%s", $1); }
    ;

any_int:
    INT    { $$ = $1; }
  | HEXINT { $$ = $1; }
  | POSINT { $$ = $1; }
    ;

lane_index:
    INT    { $$ = $1; }
  | HEXINT { $$ = $1; }
    ;

any_nat:
    INT    { $$ = ($1 < 0) ? 0 : $1; }
  | HEXINT { $$ = $1; }
  | POSINT { $$ = $1; }
    ;

memarg:
    /* empty */              { $$.offset = 0; $$.alignment = 0; }
  | memarg_nonempty          { $$ = $1; }
    ;

memarg_nonempty:
    OFFSET_IMM               {
        $$.offset = $1; $$.alignment = 0;
        wast_lex_state *ls = (wast_lex_state *)yyget_extra(scanner);
        if (ls && ls->offset_overflow) {
            report_validation_error(script, "offset out of range");
            ls->offset_overflow = 0;
        }
    }
  | ALIGN_IMM                {
        $$.offset = 0; $$.alignment = explicit_align_exponent($1);
        wast_lex_state *ls = (wast_lex_state *)yyget_extra(scanner);
        if (ls && ls->offset_overflow) {
            report_validation_error(script, "alignment must not be larger than natural");
            ls->offset_overflow = 0;
        }
    }
  | OFFSET_IMM ALIGN_IMM     {
        $$.offset = $1; $$.alignment = explicit_align_exponent($2);
        wast_lex_state *ls = (wast_lex_state *)yyget_extra(scanner);
        if (ls && ls->offset_overflow) {
            report_validation_error(script, "offset out of range");
            ls->offset_overflow = 0;
        }
    }
  | ALIGN_IMM  OFFSET_IMM    {
        $$.offset = $2; $$.alignment = explicit_align_exponent($1);
        wast_lex_state *ls = (wast_lex_state *)yyget_extra(scanner);
        if (ls && ls->offset_overflow) {
            report_validation_error(script, "offset out of range");
            ls->offset_overflow = 0;
        }
    }
    ;

valtype:
    KW_I32  { $$ = WASM_VALTYPE_I32; }
  | KW_I64  { $$ = WASM_VALTYPE_I64; }
  | KW_F32  { $$ = WASM_VALTYPE_F32; }
  | KW_F64  { $$ = WASM_VALTYPE_F64; }
  | KW_V128 { $$ = WASM_VALTYPE_V128; }
  | KW_FUNCREF   { $$ = WASM_VALTYPE_FUNCREF; }
  | KW_EXTERNREF { $$ = WASM_VALTYPE_EXTERNREF; }
  | KW_ANYREF    { $$ = WASM_VALTYPE_ANYREF; }
  | KW_EQREF     { $$ = WASM_VALTYPE_EQREF; }
  | KW_I31REF    { $$ = WASM_VALTYPE_I31REF; }
  | KW_STRUCTREF { $$ = WASM_VALTYPE_STRUCTREF; }
  | KW_ARRAYREF  { $$ = WASM_VALTYPE_ARRAYREF; }
  | KW_EXNREF    { $$ = WASM_VALTYPE_EXNREF; }
  | KW_NULLREF       { $$ = WASM_VALTYPE_NULLREF; }
  | KW_NULLFUNCREF   { $$ = WASM_VALTYPE_NULLFUNCREF; }
  | KW_NULLEXNREF    { $$ = WASM_VALTYPE_NULLEXNREF; }
  | KW_NULLEXTERNREF { $$ = WASM_VALTYPE_NULLEXTERNREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_FUNC RPAREN { $$ = WASM_VALTYPE_FUNCREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_EXTERN RPAREN { $$ = WASM_VALTYPE_EXTERNREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_ANY RPAREN { $$ = WASM_VALTYPE_ANYREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_EQ RPAREN { $$ = WASM_VALTYPE_EQREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_I31 RPAREN { $$ = WASM_VALTYPE_I31REF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_STRUCT RPAREN { $$ = WASM_VALTYPE_STRUCTREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_ARRAY RPAREN { $$ = WASM_VALTYPE_ARRAYREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_EXN RPAREN { $$ = WASM_VALTYPE_EXNREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_NONE RPAREN { $$ = WASM_VALTYPE_NULLREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_NOFUNC RPAREN { $$ = WASM_VALTYPE_NULLFUNCREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_NOEXN RPAREN { $$ = WASM_VALTYPE_NULLEXNREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_NOEXTERN RPAREN { $$ = WASM_VALTYPE_NULLEXTERNREF; }
  | LPAREN KW_REF_TYPE KW_EXN RPAREN { $$ = WASM_VALTYPE_EXNREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_FUNC RPAREN { $$ = WASM_VALTYPE_FUNCREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_EXTERN RPAREN { $$ = WASM_VALTYPE_EXTERNREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_ANY RPAREN { $$ = WASM_VALTYPE_ANYREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_EQ RPAREN { $$ = WASM_VALTYPE_EQREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_I31 RPAREN { $$ = WASM_VALTYPE_I31REF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_STRUCT RPAREN { $$ = WASM_VALTYPE_STRUCTREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_ARRAY RPAREN { $$ = WASM_VALTYPE_ARRAYREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_NONE RPAREN { $$ = WASM_VALTYPE_NULLREF; }
  | LPAREN KW_REF_TYPE KW_NOFUNC RPAREN { $$ = WASM_VALTYPE_NULLFUNCREF; }
  | LPAREN KW_REF_TYPE KW_NOEXN RPAREN { $$ = WASM_VALTYPE_NULLEXNREF; }
  | LPAREN KW_REF_TYPE KW_NOEXTERN RPAREN { $$ = WASM_VALTYPE_NULLEXTERNREF; }
  | LPAREN KW_REF_TYPE KW_NULL any_idx RPAREN { $$ = indexed_ref_type(script, $4, 1); }
  | LPAREN KW_REF_TYPE any_idx RPAREN { $$ = indexed_ref_type(script, $3, 0); }
    ;

/* -----------------------------------------------------------------------
 * Module section fields
 * --------------------------------------------------------------------- */

/* --- type --- */
type_item:
    LPAREN KW_TYPE opt_id {
        g_parsing_type_definition = 1;
        g_local_name_count = 0;
        g_signature_seen_result = 0;
        begin_gc_type($3, WAST_TYPE_FUNC);
        memset(&g_cur_func, 0, sizeof(g_cur_func));
        g_cur_func.type_index = -1;
        g_in_func = 1;
        snprintf(g_cur_func.id, WAST_MAX_EXPORT_NAME, "%s", $3);
        /* Pre-register type name for forward refs in rec groups.
         * The name index == type_count (the slot commit_current_type will fill). */
        if ($3[0] == '$' && g_type_name_count < WAST_MAX_TYPES) {
            snprintf(g_type_names[g_type_name_count], WAST_MAX_EXPORT_NAME, "%s", $3);
            g_type_name_indices[g_type_name_count++] =
                (uint32_t)cur_group(script)->module.type_count;
        } else if (g_type_name_count < WAST_MAX_TYPES) {
            g_type_names[g_type_name_count][0] = '\0';
            g_type_name_indices[g_type_name_count++] =
                (uint32_t)cur_group(script)->module.type_count;
        }
    } type_definition RPAREN { g_parsing_type_definition = 0; }
    ;

/* (rec (type ...) (type ...) ...) — recursive type group */
rec_item:
    LPAREN KW_REC {
        g_in_rec_group = 1;
        g_rec_group_start = (uint32_t)cur_group(script)->module.type_count;
        g_rec_fixup_count = 0;
    } rec_type_list RPAREN {
        wast_module *mod = &cur_group(script)->module;
        uint32_t size = (uint32_t)mod->type_count - g_rec_group_start;
        for (uint32_t i = g_rec_group_start;
             i < (uint32_t)mod->type_count; i++) {
            mod->types[i].rec_group_start = g_rec_group_start;
            mod->types[i].rec_group_size = size;
        }
        /* Resolve deferred forward references within this rec group.
         * Sentinels encode REC_FIXUP_SENTINEL_BASE + fixup_index as the
         * type-ref index.  Scan all type slots in the group and replace
         * any sentinel with the now-resolved real type index. */
        for (int fx = 0; fx < g_rec_fixup_count; fx++) {
            uint32_t sentinel = REC_FIXUP_SENTINEL_BASE + (uint32_t)fx;
            uint32_t resolved = resolve_type(g_rec_fixups[fx].name);
            if (resolved == UINT32_MAX || resolved >= WAST_MAX_TYPES) {
                report_validation_error(script, "unknown type in rec group");
                resolved = 0;
            }
            wasm_valtype old_null = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_NULL_BASE + sentinel);
            wasm_valtype old_nn   = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE + sentinel);
            wasm_valtype new_null = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_NULL_BASE + resolved);
            wasm_valtype new_nn   = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE + resolved);
            for (uint32_t ti = g_rec_group_start; ti < (uint32_t)mod->type_count; ti++) {
                wast_type *t = &mod->types[ti];
                for (int j = 0; j < t->param_count; j++) {
                    if (t->params[j] == old_null) t->params[j] = new_null;
                    else if (t->params[j] == old_nn) t->params[j] = new_nn;
                }
                for (int j = 0; j < t->result_count; j++) {
                    if (t->results[j] == old_null) t->results[j] = new_null;
                    else if (t->results[j] == old_nn) t->results[j] = new_nn;
                }
                for (int j = 0; j < t->field_count; j++) {
                    if (t->fields[j] == old_null) t->fields[j] = new_null;
                    else if (t->fields[j] == old_nn) t->fields[j] = new_nn;
                }
            }
        }
        g_rec_fixup_count = 0;
        g_in_rec_group = 0;
    }
    ;

rec_type_list:
    /* empty */
  | rec_type_list type_item
    ;

type_definition:
    LPAREN KW_FUNC param_list result_list RPAREN {
        g_cur_type.kind = WAST_TYPE_FUNC;
        g_cur_type.param_count = g_cur_func.param_count;
        g_cur_type.result_count = g_cur_func.result_count;
        for (int i = 0; i < g_cur_type.param_count; i++)
            g_cur_type.params[i] = g_cur_func.params[i];
        for (int i = 0; i < g_cur_type.result_count; i++)
            g_cur_type.results[i] = g_cur_func.results[i];
        commit_current_type(script);
        g_in_func = 0;
    }
  | LPAREN KW_STRUCT {
        g_cur_type.kind = WAST_TYPE_STRUCT;
        g_in_func = 0;
    } struct_field_list RPAREN {
        commit_current_type(script);
    }
  | LPAREN KW_ARRAY {
        g_cur_type.kind = WAST_TYPE_ARRAY;
        g_in_func = 0;
    } storage_type RPAREN {
        append_gc_field($4);
        commit_current_type(script);
    }
  | LPAREN KW_SUB sub_super {
        g_cur_type.is_final = 0;
    } type_definition RPAREN
  | LPAREN KW_SUB KW_FINAL sub_super {
        g_cur_type.is_final = 1;
    } type_definition RPAREN
    ;

sub_super:
    /* empty — no explicit supertype */ { g_cur_type.supertype = -1; }
  | any_idx {
        g_cur_type.supertype = (int32_t)resolve_type($1);
    }
    ;

struct_field_list:
    /* empty */
  | struct_field_list LPAREN KW_FIELD opt_id {
        /* Save field name if present — must save before struct_field_types
         * because append_gc_field increments field_count */
        if ($4[0] == '$' || $4[0] != '\0') {
            int next = g_cur_type.field_count;
            for (int i = 0; i < next; i++)
                if (strcmp(g_cur_type.field_names[i], $4) == 0)
                    report_validation_error(script, "duplicate field");
            if (next < WAST_MAX_TYPE_FIELDS)
                snprintf(g_cur_type.field_names[next], 64, "%s", $4);
        }
    } struct_field_types RPAREN
    ;

struct_field_types:
    /* empty */
  | struct_field_types storage_type { append_gc_field($2); }
    ;

storage_type:
    valtype { $$ = (uint64_t)$1; }
  | KW_I8  { $$ = (uint64_t)WASM_VALTYPE_I32 | (UINT64_C(1) << 17); }
  | KW_I16 { $$ = (uint64_t)WASM_VALTYPE_I32 | (UINT64_C(2) << 17); }
  | MODULE_GLOBAL_MUT_START storage_type RPAREN {
        $$ = $2 | (UINT64_C(1) << 16);
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
    opt_id import_func_attrs RPAREN {
        g_in_func = 1; /* trick commit_func into registering it */
        commit_func(script);
    }
  | MODULE_MEMORY_START opt_id limits RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->memory_count < WAST_MAX_MEMORIES) {
            wast_memory *m = &mod->memories[mod->memory_count++];
            memset(m, 0, sizeof(*m));
            m->limits = $3;
            snprintf(m->import_module, WAST_MAX_EXPORT_NAME, "%s", g_import_module);
            snprintf(m->import_name,   WAST_MAX_EXPORT_NAME, "%s", g_import_name);
            m->is_import = 1;
            snprintf(m->id, WAST_MAX_EXPORT_NAME, "%s", $2);
            if (g_memory_name_count < WAST_MAX_MEMORIES) snprintf(g_memory_names[g_memory_name_count++],WAST_MAX_EXPORT_NAME,"%s",$2);
        }
    }
  | MODULE_GLOBAL_START opt_id global_type RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->global_count < WAST_MAX_GLOBALS) {
            wast_global *g = &mod->globals[mod->global_count++];
            *g = g_cur_global;
            snprintf(g->import_module, WAST_MAX_EXPORT_NAME, "%s", g_import_module);
            snprintf(g->import_name,   WAST_MAX_EXPORT_NAME, "%s", g_import_name);
            g->is_import = 1;
            snprintf(g->id, WAST_MAX_EXPORT_NAME, "%s", $2);
            if (g_global_name_count < WAST_MAX_GLOBALS)
                snprintf(g_global_names[g_global_name_count++], WAST_MAX_EXPORT_NAME, "%s", $2);
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
  | LPAREN KW_TAG {
        memset(&g_cur_tag, 0, sizeof(g_cur_tag));
        g_cur_tag.type_index = -1;
        g_cur_tag.is_import = 1;
        snprintf(g_cur_tag.import_module, WAST_MAX_EXPORT_NAME, "%s",
                 g_import_module);
        snprintf(g_cur_tag.import_name, WAST_MAX_EXPORT_NAME, "%s",
                 g_import_name);
    } opt_id tag_attr_list RPAREN {
        commit_tag(script, $4);
    }
    ;

/* Function import attributes share an opening parenthesis, so keep type,
 * parameter, and result attributes in one deterministic list.  In
 * particular, do not put a nullable typeuse before the parameter list. */
import_func_attrs:
    /* empty */
  | import_func_attrs LPAREN KW_TYPE any_idx RPAREN {
        set_func_type_ref(script, $4, @4.first_line, @4.first_column);
    }
  | import_func_attrs LPAREN KW_PARAM param_items RPAREN
  | import_func_attrs LPAREN KW_RESULT result_valtype_list RPAREN
    ;

/* --- memory --- */
memory_item:
    MODULE_MEMORY_START opt_id LPAREN KW_EXPORT STRING RPAREN
    LPAREN KW_IMPORT STRING STRING RPAREN limits RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->memory_count < WAST_MAX_MEMORIES) {
            wast_memory *m = &mod->memories[mod->memory_count++];
            memset(m, 0, sizeof(*m));
            m->limits = $12;
            m->is_import = 1;
            snprintf(m->id, WAST_MAX_EXPORT_NAME, "%s", $2);
            snprintf(m->export_name, WAST_MAX_EXPORT_NAME, "%s", $5);
            m->has_export_name = 1;
            snprintf(m->import_module, WAST_MAX_EXPORT_NAME, "%s", $9);
            snprintf(m->import_name, WAST_MAX_EXPORT_NAME, "%s", $10);
            if (g_memory_name_count < WAST_MAX_MEMORIES)
                snprintf(g_memory_names[g_memory_name_count++], WAST_MAX_EXPORT_NAME, "%s", $2);
        }
    }
  |
    MODULE_MEMORY_START opt_id LPAREN KW_IMPORT STRING STRING RPAREN limits RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->memory_count < WAST_MAX_MEMORIES) {
            wast_memory *m = &mod->memories[mod->memory_count++];
            memset(m, 0, sizeof(*m));
            m->limits = $8;
            snprintf(m->import_module, WAST_MAX_EXPORT_NAME, "%s", $5);
            snprintf(m->import_name,   WAST_MAX_EXPORT_NAME, "%s", $6);
            m->is_import = 1;
            snprintf(m->id, WAST_MAX_EXPORT_NAME, "%s", $2);
            if (g_memory_name_count < WAST_MAX_MEMORIES) snprintf(g_memory_names[g_memory_name_count++],WAST_MAX_EXPORT_NAME,"%s",$2);
        }
    }
  | MODULE_MEMORY_START opt_id LPAREN KW_EXPORT STRING RPAREN limits RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->memory_count < WAST_MAX_MEMORIES) {
            wast_memory *m = &mod->memories[mod->memory_count++];
            memset(m, 0, sizeof(*m));
            m->limits = $7;
            snprintf(m->export_name, WAST_MAX_EXPORT_NAME, "%s", $5);
            m->has_export_name = 1;
            snprintf(m->id, WAST_MAX_EXPORT_NAME, "%s", $2);
            if (g_memory_name_count < WAST_MAX_MEMORIES) snprintf(g_memory_names[g_memory_name_count++],WAST_MAX_EXPORT_NAME,"%s",$2);
        }
    }
  | MODULE_MEMORY_START opt_id limits RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->memory_count < WAST_MAX_MEMORIES) {
            wast_memory *m = &mod->memories[mod->memory_count++];
            memset(m, 0, sizeof(*m));
            m->limits = $3;
            snprintf(m->id, WAST_MAX_EXPORT_NAME, "%s", $2);
            if (g_memory_name_count < WAST_MAX_MEMORIES) snprintf(g_memory_names[g_memory_name_count++],WAST_MAX_EXPORT_NAME,"%s",$2);
        }
    }
  | MODULE_MEMORY_START opt_id LPAREN KW_DATA {
        memset(&g_cur_data, 0, sizeof(g_cur_data));
    } data_string_list RPAREN RPAREN {
        /* Inline data memory defines both the memory and its active segment. */
        wast_module *mod = &cur_group(script)->module;
        if (mod->memory_count < WAST_MAX_MEMORIES) {
            wast_memory *m = &mod->memories[mod->memory_count++];
            memset(m, 0, sizeof(*m));
            m->limits.min = (uint32_t)((g_cur_data.len + 65535) / 65536);
            m->limits.max = m->limits.min; m->limits.has_max = 1;
            snprintf(m->id, WAST_MAX_EXPORT_NAME, "%s", $2);
            if (g_memory_name_count < WAST_MAX_MEMORIES) snprintf(g_memory_names[g_memory_name_count++],WAST_MAX_EXPORT_NAME,"%s",$2);
        }
        if (mod->data_count < WAST_MAX_DATA_SEGS) {
            g_cur_data.memory_index = mod->memory_count - 1;
            g_cur_data.offset_expr[0] = 0x41;
            g_cur_data.offset_expr[1] = 0x00;
            g_cur_data.offset_expr[2] = 0x0b;
            g_cur_data.offset_len = 3;
            record_data_name($2);
            mod->data[mod->data_count++] = g_cur_data;
        } else {
            free(g_cur_data.bytes);
            g_cur_data.bytes = NULL;
        }
    }
  | MODULE_MEMORY_START opt_id KW_I64 LPAREN KW_DATA {
        memset(&g_cur_data, 0, sizeof(g_cur_data));
    } data_string_list RPAREN RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->memory_count < WAST_MAX_MEMORIES) {
            wast_memory *m = &mod->memories[mod->memory_count++];
            memset(m, 0, sizeof(*m));
            m->limits.min = (uint64_t)((g_cur_data.len + 65535) / 65536);
            m->limits.max = m->limits.min;
            m->limits.has_max = 1;
            m->limits.is_64 = 1;
            snprintf(m->id, WAST_MAX_EXPORT_NAME, "%s", $2);
            if (g_memory_name_count < WAST_MAX_MEMORIES)
                snprintf(g_memory_names[g_memory_name_count++],
                         WAST_MAX_EXPORT_NAME, "%s", $2);
        }
        if (mod->data_count < WAST_MAX_DATA_SEGS) {
            g_cur_data.memory_index = mod->memory_count - 1;
            g_cur_data.offset_expr[0] = 0x42;
            g_cur_data.offset_expr[1] = 0x00;
            g_cur_data.offset_expr[2] = 0x0b;
            g_cur_data.offset_len = 3;
            record_data_name($2);
            mod->data[mod->data_count++] = g_cur_data;
        } else {
            free(g_cur_data.bytes);
            g_cur_data.bytes = NULL;
        }
    }
    ;

limits:
    any_nat           { $$.min=(uint64_t)$1; $$.max=0;           $$.has_max=0; $$.is_shared=0; $$.is_64=0;
                        if ((uint64_t)$1 > 0xFFFFFFFFu) report_validation_error(script, "memory size"); }
  | any_nat any_nat   { $$.min=(uint64_t)$1; $$.max=(uint64_t)$2; $$.has_max=1; $$.is_shared=0; $$.is_64=0;
                        if ((uint64_t)$1 > 0xFFFFFFFFu || (uint64_t)$2 > 0xFFFFFFFFu) report_validation_error(script, "memory size"); }
  | KW_I64 any_nat    { $$.min=(uint64_t)$2; $$.max=0;           $$.has_max=0; $$.is_shared=0; $$.is_64=1; }
  | KW_I64 any_nat any_nat
                      { $$.min=(uint64_t)$2; $$.max=(uint64_t)$3; $$.has_max=1; $$.is_shared=0; $$.is_64=1; }
    ;

/* --- global --- */
global_item:
    MODULE_GLOBAL_START opt_id {
        memset(&g_cur_global, 0, sizeof(g_cur_global));
        g_import_module[0] = '\0';
        g_import_name[0] = '\0';
    } global_fields RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->global_count < WAST_MAX_GLOBALS) {
            wast_global *g = &mod->globals[mod->global_count++];
            *g = g_cur_global;
            snprintf(g->id, WAST_MAX_EXPORT_NAME, "%s", $2);
            if (g_global_name_count < WAST_MAX_GLOBALS)
                snprintf(g_global_names[g_global_name_count++], WAST_MAX_EXPORT_NAME, "%s", $2);
        }
    }
    ;

/* OCaml-compatible global field spine.  Export attributes recurse over the
 * remaining fields, while imports end at the global type and definitions end
 * at the general constexpr instruction list. */
global_fields:
    global_type global_init {
        $$ = 0;
    }
  | LPAREN KW_IMPORT STRING STRING RPAREN global_type {
        snprintf(g_cur_global.import_module, WAST_MAX_EXPORT_NAME, "%s", $3);
        snprintf(g_cur_global.import_name, WAST_MAX_EXPORT_NAME, "%s", $4);
        g_cur_global.is_import = 1;
        $$ = 1;
    }
  | LPAREN KW_EXPORT STRING RPAREN global_fields {
        snprintf(g_cur_global.export_name, WAST_MAX_EXPORT_NAME, "%s", $3);
        g_cur_global.has_export_name = 1;
        $$ = $5;
    }
    ;

global_type:
    valtype {
        memset(&g_cur_global, 0, sizeof(g_cur_global));
        g_cur_global.valtype     = $1;
        g_cur_global.is_mutable  = 0;
    }
  | MODULE_GLOBAL_MUT_START valtype RPAREN {
        memset(&g_cur_global, 0, sizeof(g_cur_global));
        g_cur_global.valtype = $2;
        g_cur_global.is_mutable = 1;
    }
    ;

/* As in the OCaml parser, a constexpr is syntactically an ordinary instruction
 * list.  Its restricted opcode set and result type are checked only after the
 * complete expression has been parsed and encoded. */
global_init:
    {
        begin_constexpr(g_cur_global.init_expr, &g_cur_global.init_len,
                        (int)sizeof(g_cur_global.init_expr));
    } constexpr_expr {
        end_constexpr();
    }
    ;

constexpr_expr:
    func_body { $$ = 0; }
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
            t->has_export_name = 1;
            snprintf(t->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_table_name_count < WAST_MAX_TABLES) snprintf(g_table_names[g_table_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  /* (table opt_id (export "e") limits reftype (ref.null ht)) — exported table with init */
  | LPAREN KW_TABLE opt_id LPAREN KW_EXPORT STRING RPAREN limits table_reftype FOLD_REF_NULL_START reftype RPAREN RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->table_count < WAST_MAX_TABLES) {
            wast_table *t = &mod->tables[mod->table_count++];
            memset(t, 0, sizeof(*t));
            t->limits = $8;
            t->reftype = $9;
            set_table_null_init(t, $11);
            snprintf(t->export_name, WAST_MAX_EXPORT_NAME, "%s", $6);
            t->has_export_name = 1;
            snprintf(t->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_table_name_count < WAST_MAX_TABLES) snprintf(g_table_names[g_table_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  /* (table opt_id (export "e") (import "m" "n") limits reftype) — re-exported import */
  | LPAREN KW_TABLE opt_id LPAREN KW_EXPORT STRING RPAREN LPAREN KW_IMPORT STRING STRING RPAREN limits table_reftype RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->table_count < WAST_MAX_TABLES) {
            wast_table *t = &mod->tables[mod->table_count++];
            memset(t, 0, sizeof(*t));
            t->limits = $13;
            t->reftype = $14;
            snprintf(t->export_name, WAST_MAX_EXPORT_NAME, "%s", $6);
            t->has_export_name = 1;
            snprintf(t->import_module, WAST_MAX_EXPORT_NAME, "%s", $10);
            snprintf(t->import_name,   WAST_MAX_EXPORT_NAME, "%s", $11);
            t->is_import = 1;
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
  /* (table $t 10 funcref (ref.null func)) — table with init expression */
  | LPAREN KW_TABLE opt_id limits table_reftype FOLD_REF_NULL_START reftype RPAREN RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->table_count < WAST_MAX_TABLES) {
            wast_table *t = &mod->tables[mod->table_count++];
            memset(t, 0, sizeof(*t));
            t->limits = $4;
            t->reftype = $5;
            set_table_null_init(t, $7);
            snprintf(t->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_table_name_count < WAST_MAX_TABLES) snprintf(g_table_names[g_table_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  /* (table $t 10 reftype (ref.func $f)) — table with ref.func init expression */
  | LPAREN KW_TABLE opt_id limits table_reftype FOLD_ATOM_START any_idx RPAREN RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->table_count < WAST_MAX_TABLES) {
            wast_table *t = &mod->tables[mod->table_count++];
            memset(t, 0, sizeof(*t));
            t->limits = $4;
            t->reftype = $5;
            set_table_index_init(script, t, (uint32_t)(mod->table_count - 1),
                                 0xd2, IDX_FUNC, $7,
                                 @7.first_line, @7.first_column);
            snprintf(t->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_table_name_count < WAST_MAX_TABLES) snprintf(g_table_names[g_table_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  /* (table opt_id (export "e") limits reftype (ref.func $f)) — exported table with ref.func init */
  | LPAREN KW_TABLE opt_id LPAREN KW_EXPORT STRING RPAREN limits table_reftype FOLD_ATOM_START any_idx RPAREN RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->table_count < WAST_MAX_TABLES) {
            wast_table *t = &mod->tables[mod->table_count++];
            memset(t, 0, sizeof(*t));
            t->limits = $8;
            t->reftype = $9;
            set_table_index_init(script, t, (uint32_t)(mod->table_count - 1),
                                 0xd2, IDX_FUNC, $11,
                                 @11.first_line, @11.first_column);
            snprintf(t->export_name, WAST_MAX_EXPORT_NAME, "%s", $6);
            t->has_export_name = 1;
            snprintf(t->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_table_name_count < WAST_MAX_TABLES) snprintf(g_table_names[g_table_name_count++],WAST_MAX_EXPORT_NAME,"%s",$3);
        }
    }
  /* General constant-expression initializer; global.get is the first core
   * form that is not a reference constructor.  Retain it for post-parse
   * validation rather than deciding validity in the grammar. */
  | LPAREN KW_TABLE opt_id limits table_reftype FOLD_GLOBAL_GET_START any_idx RPAREN RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->table_count < WAST_MAX_TABLES) {
            wast_table *t = &mod->tables[mod->table_count++];
            memset(t, 0, sizeof(*t));
            t->limits = $4;
            t->reftype = $5;
            set_table_index_init(script, t, (uint32_t)(mod->table_count - 1),
                                 0x23, IDX_GLOBAL, $7,
                                 @7.first_line, @7.first_column);
            snprintf(t->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_table_name_count < WAST_MAX_TABLES)
                snprintf(g_table_names[g_table_name_count++], WAST_MAX_EXPORT_NAME,
                         "%s", $3);
        }
    }
  /* General constexpr table initializer needed by the i31 suite. */
  | LPAREN KW_TABLE opt_id limits table_reftype FOLD_REF_I31_START FOLD_GLOBAL_GET_START any_idx RPAREN RPAREN RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->table_count < WAST_MAX_TABLES) {
            wast_table *t = &mod->tables[mod->table_count++];
            memset(t, 0, sizeof(*t));
            t->limits = $4;
            t->reftype = $5;
            set_table_index_init(script, t, (uint32_t)(mod->table_count - 1),
                                 0x23, IDX_GLOBAL, $8,
                                 @8.first_line, @8.first_column);
            emit_init_byte(t->init_expr, &t->init_len,
                           (int)sizeof(t->init_expr), 0xfb);
            emit_init_byte(t->init_expr, &t->init_len,
                           (int)sizeof(t->init_expr), 0x1c);
            snprintf(t->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_table_name_count < WAST_MAX_TABLES)
                snprintf(g_table_names[g_table_name_count++],
                         WAST_MAX_EXPORT_NAME, "%s", $3);
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
            /* The shorthand segment initializes the table declared by this
             * field, not necessarily table 0.  table_count includes imported
             * and previously declared tables, so the new table's index is the
             * post-increment count minus one. */
            g_cur_elem.table_index = mod->table_count - 1;
            g_cur_elem.offset_expr[0] = 0x41;
            g_cur_elem.offset_expr[1] = 0x00;
            g_cur_elem.offset_expr[2] = 0x0B;
            g_cur_elem.offset_len = 3;
            mod->elem[mod->elem_count++] = g_cur_elem;
            if (g_elem_name_count < WAST_MAX_ELEM_SEGS) g_elem_names[g_elem_name_count++][0]='\0';
        }
    }
  | LPAREN KW_TABLE opt_id KW_I64 table_reftype LPAREN KW_ELEM {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.reftype = $5;
    } elem_func_list RPAREN RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->table_count < WAST_MAX_TABLES) {
            wast_table *t = &mod->tables[mod->table_count++];
            memset(t, 0, sizeof(*t));
            t->limits.min = (uint64_t)g_cur_elem.ref_count;
            t->limits.is_64 = 1;
            t->reftype = g_cur_elem.reftype;
            snprintf(t->id, WAST_MAX_EXPORT_NAME, "%s", $3);
            if (g_table_name_count < WAST_MAX_TABLES)
                snprintf(g_table_names[g_table_name_count++],
                         WAST_MAX_EXPORT_NAME, "%s", $3);
        }
        if (mod->elem_count < WAST_MAX_ELEM_SEGS) {
            g_cur_elem.table_index = mod->table_count - 1;
            g_cur_elem.offset_expr[0] = 0x42;
            g_cur_elem.offset_expr[1] = 0x00;
            g_cur_elem.offset_expr[2] = 0x0b;
            g_cur_elem.offset_len = 3;
            mod->elem[mod->elem_count++] = g_cur_elem;
            if (g_elem_name_count < WAST_MAX_ELEM_SEGS)
                g_elem_names[g_elem_name_count++][0] = '\0';
        }
    }
    ;

/* --- data --- */
data_item:
    LPAREN KW_DATA opt_id MODULE_MEMORY_START any_idx RPAREN data_offset data_string_list RPAREN {
        wast_module *mod = &cur_group(script)->module;
        g_cur_data.memory_index = (int)resolve_memory($5);
        snprintf(g_cur_data.name, WAST_MAX_EXPORT_NAME, "%s", $3);
        if (mod->data_count < WAST_MAX_DATA_SEGS) {
            record_data_name($3);
            mod->data[mod->data_count++] = g_cur_data;
        }
    }
  | LPAREN KW_DATA opt_id data_offset data_string_list RPAREN {
        wast_module *mod = &cur_group(script)->module;
        snprintf(g_cur_data.name, WAST_MAX_EXPORT_NAME, "%s", $3);
        if (mod->data_count < WAST_MAX_DATA_SEGS) {
            record_data_name($3);
            mod->data[mod->data_count++] = g_cur_data;
        }
    }
  | LPAREN KW_DATA opt_id {
        memset(&g_cur_data, 0, sizeof(g_cur_data));
        g_cur_data.is_passive = 1;
    } data_string_list RPAREN {
        /* passive data segment */
        wast_module *mod = &cur_group(script)->module;
        snprintf(g_cur_data.name, WAST_MAX_EXPORT_NAME, "%s", $3);
        if (mod->data_count < WAST_MAX_DATA_SEGS) {
            record_data_name($3);
            mod->data[mod->data_count++] = g_cur_data;
        }
    }
    ;

data_offset:
    FOLD_GLOBAL_GET_START INT RPAREN {
        memset(&g_cur_data, 0, sizeof(g_cur_data)); g_cur_data.memory_index = 0;
        emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, 0x23);
        emit_init_leb_s32(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, (int32_t)$2);
        emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, 0x0B);
    }
  |
    FOLD_ATOM_START ID RPAREN {
        memset(&g_cur_data, 0, sizeof(g_cur_data));
        g_cur_data.is_passive = 0; g_cur_data.memory_index = 0;
        if (strcmp($1, "global.get") == 0) {
            emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, 0x23);
            emit_init_leb_s32(g_cur_data.offset_expr, &g_cur_data.offset_len, 32,
                              (int32_t)resolve_global($2));
        }
        emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, 0x0B);
    }
  |
    FOLD_GLOBAL_GET_START ID RPAREN {
        memset(&g_cur_data, 0, sizeof(g_cur_data));
        g_cur_data.is_passive = 0; g_cur_data.memory_index = 0;
        emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, 0x23);
        emit_init_leb_s32(g_cur_data.offset_expr, &g_cur_data.offset_len, 32,
                          (int32_t)resolve_global($2));
        emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, 0x0B);
    }
  |
    FOLD_ATOM_START INT RPAREN {
        memset(&g_cur_data, 0, sizeof(g_cur_data));
        g_cur_data.is_passive = 0; g_cur_data.memory_index = 0;
        begin_constexpr(g_cur_data.offset_expr, &g_cur_data.offset_len,
                        (int)sizeof(g_cur_data.offset_expr));
        emit_const_immediate_atom(script, $1, $2);
        emit_byte(script, 0x0b);
        end_constexpr();
    }
  |
    FOLD_ATOM_START any_int RPAREN {
        memset(&g_cur_data, 0, sizeof(g_cur_data));
        g_cur_data.is_passive = 0; g_cur_data.memory_index = 0;
        begin_constexpr(g_cur_data.offset_expr, &g_cur_data.offset_len,
                        (int)sizeof(g_cur_data.offset_expr));
        emit_const_immediate_atom(script, $1, $2);
        emit_byte(script, 0x0b);
        end_constexpr();
    }
  | FOLD_ATOM_START HEXINT RPAREN {
        memset(&g_cur_data, 0, sizeof(g_cur_data));
        g_cur_data.is_passive = 0; g_cur_data.memory_index = 0;
        begin_constexpr(g_cur_data.offset_expr, &g_cur_data.offset_len,
                        (int)sizeof(g_cur_data.offset_expr));
        emit_const_immediate_atom(script, $1, $2);
        emit_byte(script, 0x0b);
        end_constexpr();
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
  | FOLD_REF_NULL_START reftype RPAREN {
        memset(&g_cur_data, 0, sizeof(g_cur_data));
        g_cur_data.memory_index = 0;
        emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, 0xd0);
        emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32,
                       (uint8_t)$2);
        emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, 0x0b);
    }
  | FOLD_NOP_START RPAREN {
        memset(&g_cur_data, 0, sizeof(g_cur_data));
        g_cur_data.memory_index = 0;
        emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, 0x01);
        emit_init_byte(g_cur_data.offset_expr, &g_cur_data.offset_len, 32, 0x0b);
    }
  | LPAREN KW_OFFSET_KW {
        memset(&g_cur_data, 0, sizeof(g_cur_data));
        g_cur_data.memory_index = 0;
        begin_constexpr(g_cur_data.offset_expr, &g_cur_data.offset_len,
                        (int)sizeof(g_cur_data.offset_expr));
    } func_body RPAREN {
        emit_byte(script, 0x0b);
        end_constexpr();
    }
  /* General folded offset expression — for compound constexprs in assert_invalid */
  | FOLD_ATOM_START {
        memset(&g_cur_data, 0, sizeof(g_cur_data));
        g_cur_data.is_passive = 0; g_cur_data.memory_index = 0;
        begin_constexpr(g_cur_data.offset_expr, &g_cur_data.offset_len,
                        (int)sizeof(g_cur_data.offset_expr));
    } fold_arg_list RPAREN {
        (void)emit_atom_op(script, $1);
        emit_byte(script, 0x0b);
        end_constexpr();
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
  | string_list STRING
    ;

data_string_list:
    /* empty */
  | data_string_list STRING {
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
  | LPAREN KW_ELEM opt_id KW_FUNC {
        /* passive funcref elem segment: (elem func $f ...) */
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.is_passive = 1;
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
    } elem_funcs RPAREN {
        wast_module *mod = &cur_group(script)->module;
        if (mod->elem_count < WAST_MAX_ELEM_SEGS) {
            mod->elem[mod->elem_count++] = g_cur_elem;
            if (g_elem_name_count < WAST_MAX_ELEM_SEGS)
                snprintf(g_elem_names[g_elem_name_count++], WAST_MAX_EXPORT_NAME, "%s", $3);
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
        g_cur_elem.reftype = heap_reftype_to_value_type($5);
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
        g_cur_elem.reftype = heap_reftype_to_value_type($4);
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
    FOLD_ATOM_START any_int RPAREN {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.table_index = 0;
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
        begin_constexpr(g_cur_elem.offset_expr, &g_cur_elem.offset_len,
                        (int)sizeof(g_cur_elem.offset_expr));
        emit_const_immediate_atom(script, $1, $2);
        emit_byte(script, 0x0b);
        end_constexpr();
    }
  | LPAREN KW_TABLE any_idx RPAREN LPAREN KW_OFFSET_KW {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.table_index = (int)meta_index_ref(script,META_ELEM_TABLE,IDX_TABLE,
            (uint32_t)cur_group(script)->module.elem_count,0,$3,@3.first_line,@3.first_column);
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
        begin_constexpr(g_cur_elem.offset_expr, &g_cur_elem.offset_len,
                        (int)sizeof(g_cur_elem.offset_expr));
    } func_body RPAREN {
        emit_byte(script, 0x0b);
        end_constexpr();
    }
  |
    LPAREN KW_TABLE any_idx RPAREN FOLD_ATOM_START any_int RPAREN {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.table_index = (int)meta_index_ref(script,META_ELEM_TABLE,IDX_TABLE,
            (uint32_t)cur_group(script)->module.elem_count,0,$3,@3.first_line,@3.first_column);
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
        begin_constexpr(g_cur_elem.offset_expr, &g_cur_elem.offset_len,
                        (int)sizeof(g_cur_elem.offset_expr));
        emit_const_immediate_atom(script, $5, $6);
        emit_byte(script, 0x0b);
        end_constexpr();
    }
  | LPAREN KW_I32_CONST any_int RPAREN {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.table_index = 0;
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x41);
        emit_init_leb_s32(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, (int32_t)$3);
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x0B);
    }
  | FOLD_REF_NULL_START reftype RPAREN {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.table_index = 0;
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0xd0);
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32,
                       (uint8_t)$2);
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x0b);
    }
  | FOLD_NOP_START RPAREN {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.table_index = 0;
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x01);
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x0b);
        report_validation_error(script, "constant expression required");
    }
  | LPAREN KW_OFFSET_KW {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.table_index = 0;
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
        begin_constexpr(g_cur_elem.offset_expr, &g_cur_elem.offset_len,
                        (int)sizeof(g_cur_elem.offset_expr));
    } func_body RPAREN {
        emit_byte(script, 0x0b);
        end_constexpr();
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
  /* (table $idx) (i32.const N) — abbreviated multi-table elem syntax */
  | LPAREN KW_TABLE any_idx RPAREN LPAREN KW_I32_CONST any_int RPAREN {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.table_index = (int)meta_index_ref(script,META_ELEM_TABLE,IDX_TABLE,
            (uint32_t)cur_group(script)->module.elem_count,0,$3,@3.first_line,@3.first_column);
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x41);
        emit_init_leb_s32(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, (int32_t)$7);
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x0B);
    }
  /* (global.get $name) offset */
  | FOLD_GLOBAL_GET_START any_idx RPAREN {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.table_index = 0;
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x23);
        emit_init_leb_s32(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32,
                          (int32_t)resolve_global($2));
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x0B);
    }
  /* (table $t) (global.get $name) offset */
  | LPAREN KW_TABLE any_idx RPAREN FOLD_GLOBAL_GET_START any_idx RPAREN {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.table_index = (int)meta_index_ref(script,META_ELEM_TABLE,IDX_TABLE,
            (uint32_t)cur_group(script)->module.elem_count,0,$3,@3.first_line,@3.first_column);
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x23);
        emit_init_leb_s32(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32,
                          (int32_t)resolve_global($6));
        emit_init_byte(g_cur_elem.offset_expr, &g_cur_elem.offset_len, 32, 0x0B);
    }
  /* General folded offset expression — for compound constexprs in assert_invalid */
  | FOLD_ATOM_START {
        memset(&g_cur_elem, 0, sizeof(g_cur_elem));
        g_cur_elem.table_index = 0;
        g_cur_elem.reftype = WASM_VALTYPE_FUNCREF;
        begin_constexpr(g_cur_elem.offset_expr, &g_cur_elem.offset_len,
                        (int)sizeof(g_cur_elem.offset_expr));
    } fold_arg_list RPAREN {
        (void)emit_atom_op(script, $1);
        emit_byte(script, 0x0b);
        end_constexpr();
    }
    ;

elem_func_list:
    /* empty */
  | KW_FUNC
  | KW_FUNC elem_funcs
  | elem_funcs
  /* expression-form entries: (ref.func $f) (ref.null func) ... directly after (elem */
  | elem_expr_items
  /* (elem (table $t) offset funcref (ref.func ...)) — reftype + item list */
  | KW_FUNCREF   elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_FUNCREF; }
  | KW_EXTERNREF elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_EXTERNREF; }
  /* (ref $t) / (ref null $t) as element kind */
  | LPAREN KW_REF_TYPE KW_NULL any_idx RPAREN elem_item_list { g_cur_elem.reftype = indexed_ref_type(script,$4,1); }
  | LPAREN KW_REF_TYPE any_idx RPAREN elem_item_list { g_cur_elem.reftype = indexed_ref_type(script,$3,0); }
  | LPAREN KW_REF_TYPE KW_NULL KW_FUNC RPAREN elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_FUNCREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_EXTERN RPAREN elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_EXTERNREF; }
  | LPAREN KW_REF_TYPE KW_FUNC RPAREN elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_FUNCREF_NONNULL; }
  | KW_ANYREF    elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_ANYREF; }
  | KW_EQREF     elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_EQREF; }
  | KW_I31REF    elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_I31REF; }
  | KW_STRUCTREF elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_STRUCTREF; }
  | KW_ARRAYREF  elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_ARRAYREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_ANY RPAREN elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_ANYREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_EQ RPAREN elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_EQREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_I31 RPAREN elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_I31REF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_STRUCT RPAREN elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_STRUCTREF; }
  | LPAREN KW_REF_TYPE KW_NULL KW_ARRAY RPAREN elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_ARRAYREF; }
  | LPAREN KW_REF_TYPE KW_ANY RPAREN elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_ANYREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_EQ RPAREN elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_EQREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_I31 RPAREN elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_I31REF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_STRUCT RPAREN elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_STRUCTREF_NONNULL; }
  | LPAREN KW_REF_TYPE KW_ARRAY RPAREN elem_item_list { g_cur_elem.reftype = WASM_VALTYPE_ARRAYREF_NONNULL; }
    ;

/* Non-empty list of expression-form element entries (no reftype/func prefix) */
elem_expr_items:
    elem_expr_item
  | elem_expr_items elem_expr_item
    ;

elem_expr_item:
    FOLD_ATOM_START any_idx RPAREN {
        append_elem_func_ref(script,$2,@2.first_line,@2.first_column);
    }
  | FOLD_REF_NULL_START reftype RPAREN {
        append_elem_null_ref(heap_reftype_to_value_type($2));
    }
  | FOLD_REF_NULL_START any_idx RPAREN {
        append_elem_null_ref(indexed_ref_type(script, $2, 1));
    }
  | FOLD_REF_I31_START {
        if (g_cur_elem.ref_count < WAST_MAX_ELEM_REFS) {
            int slot = g_cur_elem.ref_count;
            begin_constexpr(g_cur_elem.ref_exprs[slot],
                            &g_cur_elem.ref_expr_lens[slot],
                            WAST_MAX_ELEM_EXPR_BYTES);
        }
    } fold_arg_list_nonempty RPAREN {
        if (g_constexpr_target) {
            emit_byte(script, 0xfb); emit_leb_u32(script, 0x1c);
            emit_byte(script, 0x0b);
            end_constexpr();
            g_cur_elem.ref_count++;
        }
    }
  | FOLD_ARRAY_NEW_START any_idx {
        if (g_cur_elem.ref_count < WAST_MAX_ELEM_REFS) {
            int slot = g_cur_elem.ref_count;
            begin_constexpr(g_cur_elem.ref_exprs[slot],
                            &g_cur_elem.ref_expr_lens[slot],
                            WAST_MAX_ELEM_EXPR_BYTES);
        }
    } fold_arg_list_nonempty RPAREN {
        if (g_constexpr_target) {
            emit_byte(script, 0xfb); emit_leb_u32(script, 0x06);
            emit_index_ref(script, IDX_TYPE, $2,
                           @2.first_line, @2.first_column);
            emit_byte(script, 0x0b);
            end_constexpr();
            g_cur_elem.ref_count++;
        }
    }
  | FOLD_ARRAY_NEW_FIXED_START any_idx any_nat {
        if (g_cur_elem.ref_count < WAST_MAX_ELEM_REFS) {
            int slot = g_cur_elem.ref_count;
            begin_constexpr(g_cur_elem.ref_exprs[slot],
                            &g_cur_elem.ref_expr_lens[slot],
                            WAST_MAX_ELEM_EXPR_BYTES);
        }
    } fold_arg_list_nonempty RPAREN {
        if (g_constexpr_target) {
            emit_byte(script, 0xfb); emit_leb_u32(script, 0x08);
            emit_index_ref(script, IDX_TYPE, $2,
                           @2.first_line, @2.first_column);
            emit_leb_u32(script, (uint32_t)$3);
            emit_byte(script, 0x0b);
            end_constexpr();
            g_cur_elem.ref_count++;
        }
    }
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
  | elem_item_list FOLD_REF_I31_START {
        if (g_cur_elem.ref_count < WAST_MAX_ELEM_REFS) {
            int slot = g_cur_elem.ref_count;
            begin_constexpr(g_cur_elem.ref_exprs[slot],
                            &g_cur_elem.ref_expr_lens[slot],
                            WAST_MAX_ELEM_EXPR_BYTES);
        }
    } fold_arg_list_nonempty RPAREN {
        if (g_constexpr_target) {
            emit_byte(script, 0xfb); emit_leb_u32(script, 0x1c);
            emit_byte(script, 0x0b);
            end_constexpr();
            g_cur_elem.ref_count++;
        }
    }
  | elem_item_list FOLD_ARRAY_NEW_START any_idx {
        if (g_cur_elem.ref_count < WAST_MAX_ELEM_REFS) {
            int slot = g_cur_elem.ref_count;
            begin_constexpr(g_cur_elem.ref_exprs[slot],
                            &g_cur_elem.ref_expr_lens[slot],
                            WAST_MAX_ELEM_EXPR_BYTES);
        }
    } fold_arg_list_nonempty RPAREN {
        if (g_constexpr_target) {
            emit_byte(script, 0xfb); emit_leb_u32(script, 0x06);
            emit_index_ref(script, IDX_TYPE, $3,
                           @3.first_line, @3.first_column);
            emit_byte(script, 0x0b);
            end_constexpr();
            g_cur_elem.ref_count++;
        }
    }
  | elem_item_list FOLD_ARRAY_NEW_FIXED_START any_idx any_nat {
        if (g_cur_elem.ref_count < WAST_MAX_ELEM_REFS) {
            int slot = g_cur_elem.ref_count;
            begin_constexpr(g_cur_elem.ref_exprs[slot],
                            &g_cur_elem.ref_expr_lens[slot],
                            WAST_MAX_ELEM_EXPR_BYTES);
        }
    } fold_arg_list_nonempty RPAREN {
        if (g_constexpr_target) {
            emit_byte(script, 0xfb); emit_leb_u32(script, 0x08);
            emit_index_ref(script, IDX_TYPE, $3,
                           @3.first_line, @3.first_column);
            emit_leb_u32(script, (uint32_t)$4);
            emit_byte(script, 0x0b);
            end_constexpr();
            g_cur_elem.ref_count++;
        }
    }
  | elem_item_list LPAREN KW_ITEM {
        if (g_cur_elem.ref_count < WAST_MAX_ELEM_REFS) {
            int slot = g_cur_elem.ref_count;
            begin_constexpr(g_cur_elem.ref_exprs[slot],
                            &g_cur_elem.ref_expr_lens[slot],
                            WAST_MAX_ELEM_EXPR_BYTES);
        }
    } func_body RPAREN {
        if (g_constexpr_target) {
            emit_byte(script, 0x0b);
            end_constexpr();
            g_cur_elem.ref_count++;
        }
    }
  | elem_item_list LPAREN KW_REF_FUNC any_idx RPAREN {
        append_elem_func_ref(script,$4,@4.first_line,@4.first_column);
    }
  | elem_item_list FOLD_ATOM_START any_idx RPAREN {
        append_elem_func_ref(script,$3,@3.first_line,@3.first_column);
    }
  | elem_item_list FOLD_ATOM_START reftype RPAREN {
        append_elem_null_ref(heap_reftype_to_value_type($3));
    }
  | elem_item_list FOLD_REF_NULL_START reftype RPAREN {
        append_elem_null_ref(heap_reftype_to_value_type($3));
    }
  | elem_item_list FOLD_REF_NULL_START any_idx RPAREN {
        append_elem_null_ref(indexed_ref_type(script, $3, 1));
    }
  | elem_item_list FOLD_GLOBAL_GET_START any_idx RPAREN {
        append_elem_global_ref(script,$3,@3.first_line,@3.first_column);
    }
  | elem_item_list LPAREN KW_REF_NULL reftype RPAREN {
        append_elem_null_ref(heap_reftype_to_value_type($4));
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
            if (e->kind == 4 && e->index < (uint32_t)mod->tag_count) {
                wast_tag *tag = &mod->tags[e->index];
                snprintf(tag->export_name, WAST_MAX_EXPORT_NAME, "%s", $3);
                tag->has_export_name = 1;
            }
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
  | MODULE_MEMORY_START any_idx RPAREN {
        g_export_kind = 2; g_export_index = meta_index_ref(script,META_EXPORT,IDX_MEMORY,
            (uint32_t)cur_group(script)->module.export_count,0,$2,@2.first_line,@2.first_column);
    }
  | MODULE_GLOBAL_START any_idx RPAREN {
        g_export_kind = 3; g_export_index = meta_index_ref(script,META_EXPORT,IDX_GLOBAL,
            (uint32_t)cur_group(script)->module.export_count,0,$2,@2.first_line,@2.first_column);
    }
  | LPAREN KW_TAG any_idx RPAREN {
        g_export_kind = 4;
        g_export_index = resolve_tag($3);
    }
    ;

/* --- start --- */
start_item:
    LPAREN KW_START any_idx RPAREN {
        ensure_group(script);
        cur_group(script)->module.start_func = (int)meta_index_ref(script,META_START,IDX_FUNC,0,0,$3,@3.first_line,@3.first_column);
    }
    ;

tag_item:
    LPAREN KW_TAG {
        memset(&g_cur_tag, 0, sizeof(g_cur_tag));
        g_cur_tag.type_index = -1;
    } opt_id tag_attr_list RPAREN {
        commit_tag(script, $4);
    }
    ;

tag_attr_list:
    /* empty */
  | tag_attr_list LPAREN KW_EXPORT STRING RPAREN {
        ensure_group(script);
        wast_module *mod = &cur_group(script)->module;
        /* Check for duplicate export name (tag exports aren't encoded but
         * must still participate in the uniqueness check). */
        for (int i = 0; i < mod->export_count; i++)
            if (strcmp(mod->exports[i].name, $4) == 0)
                report_validation_error(script, "duplicate export name");
        if (mod->export_count < WAST_MAX_EXPORTS) {
            wast_export *e = &mod->exports[mod->export_count++];
            snprintf(e->name, WAST_MAX_EXPORT_NAME, "%s", $4);
            e->kind = 4; /* tag */
            e->index = (uint32_t)mod->tag_count;
        }
        snprintf(g_cur_tag.export_name, WAST_MAX_EXPORT_NAME, "%s", $4);
        g_cur_tag.has_export_name = 1;
    }
  | tag_attr_list LPAREN KW_TYPE any_idx RPAREN {
        g_cur_tag.type_index = (int)resolve_type($4);
    }
  | tag_attr_list LPAREN KW_PARAM {
        g_cur_tag.has_inline_params = 1;
    } tag_valtype_list RPAREN
  | tag_attr_list LPAREN KW_RESULT tag_result_list RPAREN
  | tag_attr_list LPAREN KW_IMPORT STRING STRING RPAREN {
        snprintf(g_cur_tag.import_module, WAST_MAX_EXPORT_NAME, "%s", $4);
        snprintf(g_cur_tag.import_name, WAST_MAX_EXPORT_NAME, "%s", $5);
        g_cur_tag.is_import = 1;
    }
    ;

tag_valtype_list:
    /* empty */
  | tag_valtype_list valtype {
        if (g_cur_tag.param_count < WAST_MAX_PARAMS)
            g_cur_tag.params[g_cur_tag.param_count++] = $2;
    }
    ;

tag_result_list:
    /* empty */
  | tag_result_list valtype {
        report_validation_error(script, "non-empty tag result type");
    }
    ;

/* -----------------------------------------------------------------------
 * Assertions
 * --------------------------------------------------------------------- */

/* The body following `(module` is either a real module item stream or the
 * quoted-module spelling.  Factoring this choice prevents the two assertion
 * productions from competing after the shared KW_MODULE prefix. */
module_assert_body:
    opt_module_id module_fields { $$ = 1; }
  | KW_QUOTE string_list {
        attach_raw_module(script, WAST_RAW_QUOTE);
        $$ = 0;
    }
  | KW_BINARY string_list {
        attach_raw_module(script, WAST_RAW_BINARY);
        $$ = 0;
    }
    ;

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
  | LPAREN KW_ASSERT_EXCEPTION {
        memset(&g_cur_assert, 0, sizeof(g_cur_assert));
        g_cur_assert.kind = WAST_ASSERT_EXCEPTION;
        g_in_assert = 1;
    }
    action RPAREN {
        ensure_group(script);
        append_assert(script);
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
        wast_group *group = cur_group(script);
        group->has_module_assertion = 1;
        group->module_assert_kind = WAST_ASSERT_INVALID;
    }
    LPAREN KW_MODULE module_assert_body RPAREN STRING RPAREN {
        wast_group *group = cur_group(script);
        if ($6) {
            apply_func_fixups(script);
        }
        group->has_module_assertion = 1;
        group->module_assert_kind = WAST_ASSERT_INVALID;
        snprintf(group->expected_module_error, WAST_MAX_EXPORT_NAME, "%s", $8);
        g_in_assert = 0;
    }
  | LPAREN KW_ASSERT_INVALID { g_in_assert = 1; begin_module(script); }
    MODULE_QUOTE_START string_list RPAREN STRING RPAREN {
        wast_group *group = cur_group(script);
        attach_raw_module(script, WAST_RAW_QUOTE);
        group->has_module_assertion = 1;
        group->module_assert_kind = WAST_ASSERT_INVALID;
        snprintf(group->expected_module_error, WAST_MAX_EXPORT_NAME, "%s", $7);
        g_in_assert = 0;
    }
  | LPAREN KW_ASSERT_INVALID { g_in_assert = 1; begin_module(script); }
    MODULE_BINARY_START string_list RPAREN STRING RPAREN {
        wast_group *group = cur_group(script);
        attach_raw_module(script, WAST_RAW_BINARY);
        group->has_module_assertion = 1;
        group->module_assert_kind = WAST_ASSERT_INVALID;
        snprintf(group->expected_module_error, WAST_MAX_EXPORT_NAME, "%s", $7);
        g_in_assert = 0;
    }
  | LPAREN KW_ASSERT_MALFORMED {
        g_in_assert = 1;
        begin_module(script);
    }
    LPAREN KW_MODULE module_assert_body RPAREN STRING RPAREN {
        wast_group *group = cur_group(script);
        if ($6) {
            apply_func_fixups(script);
        }
        group->has_module_assertion = 1;
        group->module_assert_kind = WAST_ASSERT_MALFORMED;
        snprintf(group->expected_module_error, WAST_MAX_EXPORT_NAME, "%s", $8);
        g_in_assert = 0;
    }
  | LPAREN KW_ASSERT_MALFORMED { g_in_assert = 1; begin_module(script); }
    MODULE_QUOTE_START string_list RPAREN STRING RPAREN {
        wast_group *group = cur_group(script);
        attach_raw_module(script, WAST_RAW_QUOTE);
        group->has_module_assertion = 1;
        group->module_assert_kind = WAST_ASSERT_MALFORMED;
        snprintf(group->expected_module_error, WAST_MAX_EXPORT_NAME, "%s", $7);
        g_in_assert = 0;
    }
  | LPAREN KW_ASSERT_MALFORMED { g_in_assert = 1; begin_module(script); }
    MODULE_BINARY_START string_list RPAREN STRING RPAREN {
        wast_group *group = cur_group(script);
        attach_raw_module(script, WAST_RAW_BINARY);
        group->has_module_assertion = 1;
        group->module_assert_kind = WAST_ASSERT_MALFORMED;
        snprintf(group->expected_module_error, WAST_MAX_EXPORT_NAME, "%s", $7);
        g_in_assert = 0;
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
    FOLD_REF_NULL_START reftype RPAREN {
        memset(&$$, 0, sizeof($$));
        $$.type = heap_reftype_to_value_type($2);
        $$.ref = UINT32_MAX;
    }
  | FOLD_REF_NULL_START RPAREN {
        /* (ref.null) — any null reference pattern */
        memset(&$$, 0, sizeof($$)); $$.type = WASM_VALTYPE_FUNCREF;
        $$.ref = UINT32_MAX; $$.nan_mode[0] = REF_MATCH_NULL;
    }
  | FOLD_REF_I31_START RPAREN {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_I31REF_NONNULL;
        $$.ref = UINT32_MAX;
    }
  | FOLD_ATOM_START KW_NAN RPAREN            { $$ = folded_nan_value($1,0, strcmp($1,"f64.const")==0 ? NAN_MATCH_F64_ARITH : NAN_MATCH_F32_ARITH); }
  | FOLD_ATOM_START KW_NAN_CANONICAL RPAREN  { $$ = folded_nan_value($1,0, strcmp($1,"f64.const")==0 ? NAN_MATCH_F64_CANON : NAN_MATCH_F32_CANON); }
  | FOLD_ATOM_START KW_NAN_ARITHMETIC RPAREN { $$ = folded_nan_value($1,0, strcmp($1,"f64.const")==0 ? NAN_MATCH_F64_ARITH : NAN_MATCH_F32_ARITH); }
  | FOLD_ATOM_START KW_NEG_NAN RPAREN        { $$ = folded_nan_value($1,1, strcmp($1,"f64.const")==0 ? NAN_MATCH_F64_ARITH : NAN_MATCH_F32_ARITH); }
  | FOLD_ATOM_START KW_POS_NAN RPAREN        { $$ = folded_nan_value($1,0, strcmp($1,"f64.const")==0 ? NAN_MATCH_F64_ARITH : NAN_MATCH_F32_ARITH); }
  | FOLD_ATOM_START KW_INF RPAREN            { $$ = folded_special_value($1,2); }
  | FOLD_ATOM_START KW_POS_INF RPAREN        { $$ = folded_special_value($1,2); }
  | FOLD_ATOM_START KW_NEG_INF RPAREN        { $$ = folded_special_value($1,3); }
  | FOLD_ATOM_START ATOM RPAREN {
        /* Handles nan:0xHEX, ±inf, ±nan:0xHEX — preserves exact payload bits */
        memset(&$$, 0, sizeof($$));
        const char *s = $2;
        int is64 = strncmp($1, "f64.", 4) == 0;
        if (is64) {
            uint64_t bits = 0x7ff8000000000000ULL;
            if (strcmp(s, "-inf") == 0) bits = 0xfff0000000000000ULL;
            else if (strcmp(s, "inf") == 0 || strcmp(s, "+inf") == 0) bits = 0x7ff0000000000000ULL;
            else if (strncmp(s, "-nan:0x", 7) == 0) bits = 0xfff0000000000000ULL | (parse_hex_payload(s + 7) & 0x000fffffffffffffULL);
            else if (strncmp(s, "+nan:0x", 7) == 0) bits = 0x7ff0000000000000ULL | (parse_hex_payload(s + 7) & 0x000fffffffffffffULL);
            else if (strncmp(s, "nan:0x", 6) == 0) bits = 0x7ff0000000000000ULL | (parse_hex_payload(s + 6) & 0x000fffffffffffffULL);
            memcpy(&$$.f64, &bits, sizeof(bits)); $$.type = WASM_VALTYPE_F64;
        } else {
            uint32_t bits = 0x7fc00000U;
            if (strcmp(s, "-inf") == 0) bits = 0xff800000U;
            else if (strcmp(s, "inf") == 0 || strcmp(s, "+inf") == 0) bits = 0x7f800000U;
            else if (strncmp(s, "-nan:0x", 7) == 0) bits = 0xff800000U | (uint32_t)(parse_hex_payload(s + 7) & 0x7fffffU);
            else if (strncmp(s, "+nan:0x", 7) == 0) bits = 0x7f800000U | (uint32_t)(parse_hex_payload(s + 7) & 0x7fffffU);
            else if (strncmp(s, "nan:0x", 6) == 0) bits = 0x7f800000U | (uint32_t)(parse_hex_payload(s + 6) & 0x7fffffU);
            memcpy(&$$.f32, &bits, sizeof(bits)); $$.type = WASM_VALTYPE_F32;
        }
    }
  |
    FOLD_ATOM_START FLOAT RPAREN {
        memset(&$$, 0, sizeof($$));
        if (strncmp($1, "f64.", 4) == 0) { $$.type = WASM_VALTYPE_F64; $$.f64 = $2; }
        else { $$.type = WASM_VALTYPE_F32; $$.f32 = (float)$2; }
    }
  |
    FOLD_ATOM_START any_int RPAREN {
        memset(&$$, 0, sizeof($$));
        if (strcmp($1, "i64.const") == 0) { $$.type = WASM_VALTYPE_I64; $$.i64 = (int64_t)$2; }
        else if (strcmp($1, "ref.extern") == 0) { $$.type = WASM_VALTYPE_EXTERNREF; $$.ref = (uint32_t)$2; }
        else if (strcmp($1, "ref.func") == 0) { $$.type = WASM_VALTYPE_FUNCREF; $$.ref = (uint32_t)$2; }
        else if (strcmp($1, "ref.host") == 0) { $$.type = WASM_VALTYPE_EXTERNREF; $$.ref = (uint32_t)$2; }
        else if (strcmp($1, "f32.const") == 0) {
            $$.type = WASM_VALTYPE_F32;
            $$.f32 = (float)(int64_t)$2;
        }
        else if (strcmp($1, "f64.const") == 0) {
            $$.type = WASM_VALTYPE_F64;
            $$.f64 = (double)(int64_t)$2;
        }
        else { $$.type = WASM_VALTYPE_I32; $$.i32 = (int32_t)$2; }
    }
  |
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
  | FOLD_ATOM_START lane_type lane_vals RPAREN {
        if (strcmp($1, "v128.const") != 0)
            report_validation_error(script,
                                    "lane literal is only valid for v128.const");
        $$ = lanes_to_v128($2, &$3, script);
    }
  | LPAREN KW_REF_NULL reftype RPAREN {
        memset(&$$, 0, sizeof($$));
        $$.type = ($3 == 0x70) ? WASM_VALTYPE_FUNCREF : WASM_VALTYPE_EXTERNREF;
        $$.ref  = UINT32_MAX; /* null */
    }
  | LPAREN KW_REF_NULL RPAREN {
        /* (ref.null) — any null reference pattern */
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_FUNCREF;
        $$.ref  = UINT32_MAX;
        $$.nan_mode[0] = REF_MATCH_NULL;
    }
  | LPAREN KW_REF_FUNC any_int RPAREN {
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_FUNCREF;
        $$.ref  = (uint32_t)$3;
    }
  | LPAREN KW_REF_FUNC RPAREN {
        /* (ref.func) pattern: any non-null funcref */
        memset(&$$, 0, sizeof($$));
        $$.type = WASM_VALTYPE_FUNCREF_NONNULL;
        $$.ref  = UINT32_MAX;
    }
  | FOLD_ATOM_START RPAREN {
        /* Any non-null reference pattern used by GC assertions. */
        memset(&$$, 0, sizeof($$));
        if (strcmp($1, "ref.func") == 0) { $$.type = WASM_VALTYPE_FUNCREF_NONNULL; $$.ref = UINT32_MAX; }
        else if (strcmp($1, "ref.extern") == 0) { $$.type = WASM_VALTYPE_EXTERNREF_NONNULL; $$.ref = UINT32_MAX; }
        else if (strcmp($1, "ref.i31") == 0) { $$.type = WASM_VALTYPE_I31REF_NONNULL; $$.ref = UINT32_MAX; }
        else if (strcmp($1, "ref.struct") == 0) { $$.type = WASM_VALTYPE_STRUCTREF_NONNULL; $$.ref = UINT32_MAX; }
        else if (strcmp($1, "ref.array") == 0) { $$.type = WASM_VALTYPE_ARRAYREF_NONNULL; $$.ref = UINT32_MAX; }
        else if (strcmp($1, "ref.eq") == 0) { $$.type = WASM_VALTYPE_EQREF_NONNULL; $$.ref = UINT32_MAX; }
        else if (strcmp($1, "ref.any") == 0) { $$.type = WASM_VALTYPE_ANYREF_NONNULL; $$.ref = UINT32_MAX; }
        else if (strcmp($1, "ref.host") == 0) { $$.type = WASM_VALTYPE_EXTERNREF; $$.ref = UINT32_MAX; }
        else { $$.type = WASM_VALTYPE_I32; $$.i32 = 0; }
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
  | POSINT {
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
        $$.flags[0] = 9;
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
        $$.flags[0] = 8;
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
        $$.flags[0] = 8;
        $$.count    = 1;
    }
  | ATOM {
        memset(&$$, 0, sizeof($$));
        const char *p = $1; int negative = 0;
        if (*p == '-' || *p == '+') { negative = *p == '-'; p++; }
        if (strncmp(p, "nan:0x", 6) == 0) {
            uint64_t payload = 0; p += 6;
            while (*p) {
                if (*p != '_') {
                    int digit = *p >= '0' && *p <= '9' ? *p - '0' :
                        *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 :
                        *p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : -1;
                    if (digit < 0) { payload = 0; break; }
                    payload = (payload << 4) | (uint64_t)digit;
                }
                p++;
            }
            $$.ivals[0] = (int64_t)payload;
            $$.flags[0] = negative ? 6 : 5;
        } else $$.flags[0] = 7;
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
    wasm_value v;
    memset(&v, 0, sizeof(v));
    v.type = WASM_VALTYPE_V128;

    static const int expected_counts[] = {16,8,4,2,4,2};
    if (lane_type < 0 || lane_type > 5 ||
        lanes->count != expected_counts[lane_type])
        report_validation_error(script, "invalid vector lane count");
    if (lane_type <= 3) {
        static const uint32_t bits[] = {8,16,32,64};
        uint32_t width = bits[lane_type];
        for (int i=0;i<lanes->count;i++) {
            if (lanes->flags[i]) {
                report_validation_error(script,"constant out of range"); break;
            }
            if (width < 64) {
                int64_t min = -(INT64_C(1) << (width-1));
                uint64_t max = (UINT64_C(1) << width) - 1;
                if (lanes->ivals[i] < min ||
                    (lanes->ivals[i] >= 0 && (uint64_t)lanes->ivals[i] > max)) {
                    report_validation_error(script,"constant out of range"); break;
                }
            }
        }
    } else {
        for (int i=0;i<lanes->count;i++)
            if ((!lanes->flags[i] && isinf(lanes->vals[i])) ||
                lanes->flags[i] == 7) {
                report_validation_error(script,"constant out of range"); break;
            }
    }

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
                else if (lanes->flags[i] == 5 || lanes->flags[i] == 6) {
                    uint64_t payload=(uint64_t)lanes->ivals[i];
                    if (!payload || payload > UINT32_C(0x7fffff))
                        report_validation_error(script,"constant out of range");
                    bits=(lanes->flags[i]==6?UINT32_C(0xff800000):UINT32_C(0x7f800000))|
                         ((uint32_t)payload&UINT32_C(0x7fffff));
                }
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
                else if (lanes->flags[i] == 5 || lanes->flags[i] == 6) {
                    uint64_t payload=(uint64_t)lanes->ivals[i];
                    if (!payload || payload > UINT64_C(0x000fffffffffffff))
                        report_validation_error(script,"constant out of range");
                    bits=(lanes->flags[i]==6?UINT64_C(0xfff0000000000000):UINT64_C(0x7ff0000000000000))|
                         (payload&UINT64_C(0x000fffffffffffff));
                }
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
