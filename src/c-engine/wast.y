%define api.pure full
%locations
%define parse.error verbose
%expect 2

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

/* Parser state for building functions */
static wast_func  g_cur_func;
static int        g_in_func = 0;
static int        g_cur_group = 0;  /* index into script->groups */

static wast_group *cur_group(wast_script *script) {
    if (g_cur_group < 0 || g_cur_group >= WAST_MAX_GROUPS) return &script->groups[0];
    return &script->groups[g_cur_group];
}

static void ensure_group(wast_script *script) {
    if (script->group_count == 0) {
        script->group_count = 1;
        g_cur_group = 0;
    }
}

static void start_new_group(wast_script *script) {
    if (script->group_count < WAST_MAX_GROUPS) {
        g_cur_group = script->group_count++;
        memset(&script->groups[g_cur_group], 0, sizeof(script->groups[g_cur_group]));
    }
}

static void append_instr(wast_script *script, wasm_instr instr) {
    if (!g_in_func) return;
    if (g_cur_func.instr_count >= WAST_MAX_INSTRS) {
        if (script->error[0] == '\0')
            snprintf(script->error, sizeof(script->error), "too many instructions");
        return;
    }
    g_cur_func.instrs[g_cur_func.instr_count++] = instr;
}

static void commit_func(wast_script *script) {
    if (!g_in_func) return;
    ensure_group(script);
    wast_module *mod = &cur_group(script)->module;
    if (mod->func_count < WAST_MAX_FUNCS) {
        mod->funcs[mod->func_count++] = g_cur_func;
    } else {
        if (script->error[0] == '\0')
            snprintf(script->error, sizeof(script->error), "too many functions");
    }
    g_in_func = 0;
}

static wast_assertion g_cur_assert;
static int            g_in_assert = 0;
static char           g_invoke_name[WAST_MAX_EXPORT_NAME];
}

%union {
    int64_t      i64_val;
    double       f64_val;
    uint32_t     u32_val;
    char         str_val[WAST_MAX_EXPORT_NAME];
    wasm_valtype valtype_val;
    wasm_value   value_val;
    lane_list    lane_list_val;
    int          int_val;
}

%token LPAREN RPAREN
%token <str_val>  STRING ATOM
%token <u32_val>  SIMD_OP
%token <i64_val>  INT HEXINT
%token <f64_val>  FLOAT

%token KW_V128_CONST
%token KW_LOCAL_GET
%token KW_MODULE KW_FUNC KW_EXPORT KW_PARAM KW_RESULT
%token KW_ASSERT_RETURN KW_INVOKE KW_EITHER
%token KW_V128
%token KW_I32 KW_I64 KW_F32 KW_F64
%token KW_I8X16 KW_I16X8 KW_I32X4 KW_I64X2 KW_F32X4 KW_F64X2
%token KW_NAN_CANONICAL KW_NAN_ARITHMETIC KW_NEG_NAN KW_NEG_INF
%token KW_POS_NAN KW_POS_INF KW_NAN KW_INF

%type <valtype_val>   valtype
%type <value_val>     const_val result_const
%type <lane_list_val> lane_vals lane_val
%type <int_val>       lane_type

%%

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
  | LPAREN ATOM error RPAREN
  ;

module_cmd:
    LPAREN KW_MODULE {
        start_new_group(script);
    }
    func_list RPAREN
    ;

func_list:
    /* empty */
  | func_list func_item
    ;

func_item:
    LPAREN KW_FUNC {
        memset(&g_cur_func, 0, sizeof(g_cur_func));
        g_in_func = 1;
    }
    func_attrs RPAREN {
        wasm_instr end_instr;
        memset(&end_instr, 0, sizeof(end_instr));
        end_instr.opcode = 0x0B;
        append_instr(script, end_instr);
        commit_func(script);
    }
    ;

func_attrs:
    export_decl param_list result_decl body
    ;

export_decl:
    LPAREN KW_EXPORT STRING RPAREN {
        if (g_in_func) {
            size_t n = strlen($3);
            if (n >= WAST_MAX_EXPORT_NAME) n = WAST_MAX_EXPORT_NAME - 1;
            memcpy(g_cur_func.export_name, $3, n);
            g_cur_func.export_name[n] = '\0';
        }
    }
  | /* empty */
    ;

param_list:
    /* empty */
  | param_list LPAREN KW_PARAM valtype_list RPAREN
    ;

valtype_list:
    /* empty */
  | valtype_list valtype {
        if (g_in_func && g_cur_func.param_count < WAST_MAX_PARAMS)
            g_cur_func.params[g_cur_func.param_count++] = $2;
    }
    ;

result_decl:
    LPAREN KW_RESULT valtype RPAREN {
        if (g_in_func) {
            g_cur_func.result = $3;
            g_cur_func.has_result = 1;
        }
    }
  | /* empty */
    ;

body:
    fold_instr
  | /* empty */
    ;

fold_instr:
    LPAREN KW_LOCAL_GET any_int RPAREN {
        wasm_instr instr;
        memset(&instr, 0, sizeof(instr));
        instr.opcode  = 0x20;
        instr.u32_imm = (uint32_t)$<i64_val>3;
        append_instr(script, instr);
    }
  | LPAREN KW_V128_CONST lane_type lane_vals RPAREN {
        wasm_instr instr;
        wasm_value v = lanes_to_v128($3, &$4, script);
        memset(&instr, 0, sizeof(instr));
        instr.opcode   = 0xFD;
        instr.simd_op  = 12;
        instr.v128_imm = v.v128;
        append_instr(script, instr);
    }
  | LPAREN SIMD_OP fold_arg_list RPAREN {
        wasm_instr instr;
        memset(&instr, 0, sizeof(instr));
        instr.opcode  = 0xFD;
        instr.simd_op = $2;
        append_instr(script, instr);
    }
    ;

any_int:
    INT     { $<i64_val>$ = $1; }
  | HEXINT  { $<i64_val>$ = $1; }
    ;

fold_arg_list:
    /* empty */
  | fold_arg_list fold_instr
    ;

assert_cmd:
    LPAREN KW_ASSERT_RETURN {
        memset(&g_cur_assert, 0, sizeof(g_cur_assert));
        g_in_assert = 1;
    }
    action result_spec RPAREN {
        ensure_group(script);
        wast_group *grp = cur_group(script);
        if (grp->assertion_count < WAST_MAX_ASSERTIONS) {
            size_t n = strlen(g_invoke_name);
            if (n >= WAST_MAX_EXPORT_NAME) n = WAST_MAX_EXPORT_NAME - 1;
            memcpy(g_cur_assert.func_name, g_invoke_name, n);
            g_cur_assert.func_name[n] = '\0';
            grp->assertions[grp->assertion_count++] = g_cur_assert;
        }
        g_in_assert = 0;
    }
    ;

action:
    LPAREN KW_INVOKE STRING {
        strncpy(g_invoke_name, $3, WAST_MAX_EXPORT_NAME - 1);
        g_invoke_name[WAST_MAX_EXPORT_NAME - 1] = '\0';
        memset(&g_cur_assert, 0, sizeof(g_cur_assert));
    }
    const_list RPAREN
    ;

const_list:
    /* empty */
  | const_list const_val {
        if (g_in_assert && g_cur_assert.arg_count < WAST_MAX_ARGS)
            g_cur_assert.args[g_cur_assert.arg_count++] = $2;
    }
    ;

result_spec:
    result_const {
        g_cur_assert.result_count = 1;
        g_cur_assert.alt_count    = 1;
        g_cur_assert.alternatives[0][0] = $1;
    }
  | LPAREN KW_EITHER either_alts RPAREN
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

const_val:
    LPAREN KW_V128_CONST lane_type lane_vals RPAREN {
        $$ = lanes_to_v128($3, &$4, script);
    }
    ;

valtype:
    KW_I32  { $$ = WASM_VALTYPE_I32; }
  | KW_I64  { $$ = WASM_VALTYPE_I64; }
  | KW_F32  { $$ = WASM_VALTYPE_F32; }
  | KW_F64  { $$ = WASM_VALTYPE_F64; }
  | KW_V128 { $$ = WASM_VALTYPE_V128; }
    ;

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
    ;

%%

void yyerror(YYLTYPE *loc, wast_script *script, void *scanner, const char *msg) {
    (void)loc;
    (void)scanner;
    if (script->error[0] == '\0')
        snprintf(script->error, sizeof(script->error), "parse error: %s", msg);
}

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
                v.v128.bytes[i*4+1] = (uint8_t)((x >> 8) & 0xFF);
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
                if (lanes->flags[i] == 1) {
                    bits = 0x7FC00000u; nm = NAN_MATCH_F32_CANON;
                } else if (lanes->flags[i] == 2) {
                    bits = 0x7FC00000u; nm = NAN_MATCH_F32_ARITH;
                } else if (lanes->flags[i] == 3) {
                    bits = 0xFFC00000u; nm = NAN_MATCH_F32_ARITH;
                } else if (lanes->flags[i] == 4) {
                    bits = 0x7FC00000u; nm = NAN_MATCH_F32_ARITH;
                } else {
                    float f = (float)lanes->vals[i];
                    memcpy(&bits, &f, 4);
                }
                v.v128.bytes[i*4]   = (uint8_t)(bits & 0xFF);
                v.v128.bytes[i*4+1] = (uint8_t)((bits >> 8) & 0xFF);
                v.v128.bytes[i*4+2] = (uint8_t)((bits >> 16) & 0xFF);
                v.v128.bytes[i*4+3] = (uint8_t)(bits >> 24);
                /* Mark only the first byte of this lane with the mode */
                v.nan_mode[i*4] = nm;
            }
            break;
        }
        case 5: {
            for (int i = 0; i < 2 && i < lanes->count; i++) {
                uint64_t bits;
                uint8_t nm = NAN_MATCH_EXACT;
                if (lanes->flags[i] == 1) {
                    bits = 0x7FF8000000000000ULL; nm = NAN_MATCH_F64_CANON;
                } else if (lanes->flags[i] == 2) {
                    bits = 0x7FF8000000000000ULL; nm = NAN_MATCH_F64_ARITH;
                } else if (lanes->flags[i] == 3) {
                    bits = 0xFFF8000000000000ULL; nm = NAN_MATCH_F64_ARITH;
                } else if (lanes->flags[i] == 4) {
                    bits = 0x7FF8000000000000ULL; nm = NAN_MATCH_F64_ARITH;
                } else {
                    double d = lanes->vals[i];
                    memcpy(&bits, &d, 8);
                }
                for (int b = 0; b < 8; b++)
                    v.v128.bytes[i*8+b] = (uint8_t)(bits >> (b*8));
                /* Mark only the first byte of this lane with the mode */
                v.nan_mode[i*8] = nm;
            }
            break;
        }
        default:
            break;
    }
    return v;
}
