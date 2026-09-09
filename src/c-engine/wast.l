%option reentrant bison-bridge bison-locations noyywrap nodefault 8bit
%option nounput noinput
%option extra-type="wast_lex_state *"

%{
#include "wast_types.h"
#include "wast.tab.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void safe_strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i + 1 < n && src[i] != '\0'; i++) dest[i] = src[i];
    dest[i] = '\0';
}

static char *retain_string(wast_lex_state *state, const char *source,
                           size_t length) {
    char *value = (char *)malloc(length + 1);
    if (!value) return NULL;
    memcpy(value, source, length);
    value[length] = '\0';
    if (state->string_count == state->string_capacity) {
        size_t capacity = state->string_capacity ?
                          state->string_capacity * 2 : 64;
        char **strings = (char **)realloc(
            state->strings, capacity * sizeof(*strings));
        if (!strings) { free(value); return NULL; }
        state->strings = strings;
        state->string_capacity = capacity;
    }
    state->strings[state->string_count++] = value;
    return value;
}

/* Strip underscores from a numeric literal into a static buffer */
static const char *strip_underscores(const char *s) {
    /* The core literal tests include exact decimal f64 boundaries with more
     * than 300 digits.  Keep this bounded, but do not truncate a conforming
     * literal before strtod performs the required rounding. */
    static char buf[4096];
    size_t j = 0;
    for (size_t i = 0; s[i] && j + 1 < sizeof(buf); i++)
        if (s[i] != '_') buf[j++] = s[i];
    buf[j] = '\0';
    return buf;
}

static int hex_value(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (unsigned char)(c | 0x20);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static void append_utf8(char *dest, size_t cap, size_t *out, uint32_t cp) {
    unsigned char bytes[4];
    size_t count;
    if (cp <= 0x7f) { bytes[0] = (unsigned char)cp; count = 1; }
    else if (cp <= 0x7ff) {
        bytes[0] = (unsigned char)(0xc0 | (cp >> 6));
        bytes[1] = (unsigned char)(0x80 | (cp & 0x3f)); count = 2;
    } else if (cp <= 0xffff) {
        bytes[0] = (unsigned char)(0xe0 | (cp >> 12));
        bytes[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        bytes[2] = (unsigned char)(0x80 | (cp & 0x3f)); count = 3;
    } else {
        bytes[0] = (unsigned char)(0xf0 | (cp >> 18));
        bytes[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
        bytes[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        bytes[3] = (unsigned char)(0x80 | (cp & 0x3f)); count = 4;
    }
    for (size_t i = 0; i < count && *out + 1 < cap; i++)
        dest[(*out)++] = (char)bytes[i];
}

static void decode_quoted_id(char *dest, size_t cap,
                             const char *src, size_t length) {
    size_t out = 0;
    if (cap > 1) dest[out++] = '$';
    for (size_t i = 0; i < length && out + 1 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c != '\\' || i + 1 >= length) { dest[out++] = (char)c; continue; }
        c = (unsigned char)src[++i];
        if (c == 'u' && i + 1 < length && src[i + 1] == '{') {
            uint32_t cp = 0; i += 2;
            while (i < length && src[i] != '}') {
                int digit = hex_value((unsigned char)src[i++]);
                if (digit >= 0) cp = cp * 16u + (uint32_t)digit;
            }
            append_utf8(dest, cap, &out, cp);
        } else if (i + 1 < length && hex_value(c) >= 0 &&
                   hex_value((unsigned char)src[i + 1]) >= 0) {
            dest[out++] = (char)((hex_value(c) << 4) |
                                 hex_value((unsigned char)src[++i]));
        } else {
            if (c == 't') c = '\t';
            else if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            dest[out++] = (char)c;
        }
    }
    dest[out] = '\0';
}

#define YY_USER_ACTION do {                                                   \
    yylloc->first_line = yyextra->line;                                       \
    yylloc->first_column = yyextra->column;                                   \
    for (int loc_i = 0; loc_i < yyleng; loc_i++) {                            \
        if (yytext[loc_i] == '\n') { yyextra->line++; yyextra->column = 1; }   \
        else yyextra->column++;                                               \
    }                                                                         \
    yylloc->last_line = yyextra->line;                                        \
    yylloc->last_column = yyextra->column;                                    \
} while (0);
%}

DIGIT       [0-9]
HEXDIGIT    [0-9a-fA-F]
WS          [ \t\r\n]+
ATOM_CHAR   [a-zA-Z0-9_.+\-*/\\^~=<>!?@#$%&|:`']
NUM         {DIGIT}(_?{DIGIT})*
HEXNUM      {HEXDIGIT}(_?{HEXDIGIT})*
MEMOP       (i32\.load|i64\.load|f32\.load|f64\.load|i32\.load8_s|i32\.load8_u|i32\.load16_s|i32\.load16_u|i64\.load8_s|i64\.load8_u|i64\.load16_s|i64\.load16_u|i64\.load32_s|i64\.load32_u|i32\.store|i64\.store|f32\.store|f64\.store|i32\.store8|i32\.store16|i64\.store8|i64\.store16|i64\.store32)

%%

"(;"([^;]|";"[^)])*";)"    { }
";;"[^\x0a\x0d]*           { }
{WS}                        { }

    /* WAST annotations are syntactic wrappers around the following token.
     * They carry no execution semantics for this engine, so discard a
     * complete wrapper before returning ordinary WAT tokens.  The rule is
     * deliberately limited to a single, balanced annotation; malformed
     * annotation spellings remain visible to the parser for diagnostics. */
\([ \t\r\n]*@[^()\r\n]*\) { }

    /* Folded control starts are classified as one boundary token.  This avoids
     * the LR parser choosing a function-header attribute after seeing `(` before
     * it can inspect the following keyword. */
\([ \t\r\n]*block { yyextra->fold_depth++; return FOLD_BLOCK_START; }
\([ \t\r\n]*loop  { yyextra->fold_depth++; return FOLD_LOOP_START; }
\([ \t\r\n]*if    { yyextra->fold_depth++; return FOLD_IF_START; }
\([ \t\r\n]*local\.get { yyextra->fold_depth++; return FOLD_LOCAL_GET_START; }
\([ \t\r\n]*local\.set { yyextra->fold_depth++; return FOLD_LOCAL_SET_START; }
\([ \t\r\n]*local\.tee { yyextra->fold_depth++; return FOLD_LOCAL_TEE_START; }
\([ \t\r\n]*global\.get { yyextra->fold_depth++; return FOLD_GLOBAL_GET_START; }
\([ \t\r\n]*global\.set { yyextra->fold_depth++; return FOLD_GLOBAL_SET_START; }
\([ \t\r\n]*ref\.null { yyextra->fold_depth++; return FOLD_REF_NULL_START; }
\([ \t\r\n]*ref\.i31 { yyextra->fold_depth++; return FOLD_REF_I31_START; }
\([ \t\r\n]*array\.new_fixed { yyextra->fold_depth++; return FOLD_ARRAY_NEW_FIXED_START; }
\([ \t\r\n]*array\.new { yyextra->fold_depth++; return FOLD_ARRAY_NEW_START; }
\([ \t\r\n]*global { yyextra->fold_depth++; return MODULE_GLOBAL_START; }
\([ \t\r\n]*mut { return MODULE_GLOBAL_MUT_START; }
\([ \t\r\n]*module[ \t\r\n]+binary { yyextra->fold_depth++; return MODULE_BINARY_START; }
\([ \t\r\n]*module[ \t\r\n]+quote { yyextra->fold_depth++; return MODULE_QUOTE_START; }
\([ \t\r\n]*module[ \t\r\n]+\$[a-zA-Z0-9_.+\-]+[ \t\r\n]+binary { yyextra->fold_depth++; return MODULE_ID_BINARY_START; }
\([ \t\r\n]*module[ \t\r\n]+\$[a-zA-Z0-9_.+\-]+[ \t\r\n]+quote { yyextra->fold_depth++; return MODULE_ID_QUOTE_START; }
\([ \t\r\n]*memory { yyextra->fold_depth++; return MODULE_MEMORY_START; }
\([ \t\r\n]*then { yyextra->fold_depth++; return FOLD_THEN_START; }
\([ \t\r\n]*else { yyextra->fold_depth++; return FOLD_ELSE_START; }
\([ \t\r\n]*drop { yyextra->fold_depth++; return FOLD_DROP_START; }
\([ \t\r\n]*nop { yyextra->fold_depth++; return FOLD_NOP_START; }
\([ \t\r\n]*unreachable { yyextra->fold_depth++; return FOLD_UNREACHABLE_START; }
\([ \t\r\n]*return { yyextra->fold_depth++; return FOLD_RETURN_START; }
\([ \t\r\n]*br_table { yyextra->fold_depth++; return FOLD_BR_TABLE_START; }
\([ \t\r\n]*br_on_non_null { yyextra->fold_depth++; return FOLD_BR_ON_NON_NULL_START; }
\([ \t\r\n]*br_on_null { yyextra->fold_depth++; return FOLD_BR_ON_NULL_START; }
\([ \t\r\n]*br_if { yyextra->fold_depth++; return FOLD_BR_IF_START; }
\([ \t\r\n]*br { yyextra->fold_depth++; return FOLD_BR_START; }
\([ \t\r\n]*return_call_indirect { yyextra->fold_depth++; return FOLD_RETURN_CALL_INDIRECT_START; }
\([ \t\r\n]*return_call_ref { yyextra->fold_depth++; return FOLD_RETURN_CALL_REF_START; }
\([ \t\r\n]*return_call { yyextra->fold_depth++; return FOLD_RETURN_CALL_START; }
\([ \t\r\n]*call_ref { yyextra->fold_depth++; return FOLD_CALL_REF_START; }
\([ \t\r\n]*call_indirect { yyextra->fold_depth++; return FOLD_CALL_INDIRECT_START; }
\([ \t\r\n]*call { yyextra->fold_depth++; return FOLD_CALL_START; }
\([ \t\r\n]*try_table { yyextra->fold_depth++; return FOLD_TRY_TABLE_START; }
\([ \t\r\n]*catch_all_ref { yyextra->fold_depth++; return FOLD_CATCH_ALL_REF_START; }
\([ \t\r\n]*catch_all { yyextra->fold_depth++; return FOLD_CATCH_ALL_START; }
\([ \t\r\n]*catch_ref { yyextra->fold_depth++; return FOLD_CATCH_REF_START; }
\([ \t\r\n]*catch { yyextra->fold_depth++; return FOLD_CATCH_START; }
\([ \t\r\n]*throw_ref { yyextra->fold_depth++; return FOLD_THROW_REF_START; }
\([ \t\r\n]*throw { yyextra->fold_depth++; return FOLD_THROW_START; }
\([ \t\r\n]*select { yyextra->fold_depth++; return FOLD_SELECT_START; }
\([ \t\r\n]*br_on_cast(_fail)? {
    const char *p = yytext + 1;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    size_t n = strlen(p);
    if (n >= sizeof(yylval->str_val)) n = sizeof(yylval->str_val) - 1;
    memcpy(yylval->str_val, p, n);
    yylval->str_val[n] = '\0';
    yyextra->fold_depth++;
    return FOLD_ATOM_START;
}
\([ \t\r\n]*{MEMOP} {
    const char *p = yytext + 1;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    size_t n = strlen(p);
    if (n >= sizeof(yylval->str_val)) n = sizeof(yylval->str_val) - 1;
    memcpy(yylval->str_val, p, n);
    yylval->str_val[n] = '\0';
    yyextra->fold_depth++;
    return FOLD_MEMOP_START;
}
\([ \t\r\n]*[a-zA-Z][a-zA-Z0-9_]*\.[a-zA-Z0-9_.]+ {
    const char *p = yytext + 1;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    size_t n = strlen(p); if (n >= sizeof(yylval->str_val)) n = sizeof(yylval->str_val)-1;
    memcpy(yylval->str_val, p, n); yylval->str_val[n] = '\0';
    yyextra->fold_depth++;
    wast_simd_info simd;
    if (wast_simd_lookup(yylval->str_val, &simd)) {
        if (simd.immediate == WAST_SIMD_IMM_MEMARG)
            return FOLD_SIMD_MEM_START;
        if (simd.immediate == WAST_SIMD_IMM_MEMARG_LANE)
            return FOLD_SIMD_MEM_LANE_START;
        if (simd.immediate == WAST_SIMD_IMM_LANE)
            return FOLD_SIMD_LANE_START;
        if (simd.immediate == WAST_SIMD_IMM_SHUFFLE)
            return FOLD_SIMD_SHUFFLE_START;
    }
    return FOLD_ATOM_START;
}

"("   { return LPAREN; }
")"   { if (yyextra->fold_depth > 0) yyextra->fold_depth--; return RPAREN; }

\"([^\"\\]|\\.)*\"  {
    size_t len = (size_t)(yyleng - 2);
    yylval->string_val = retain_string(yyextra, yytext + 1, len);
    if (!yylval->string_val) yylval->string_val = "";
    return STRING;
}

    /* SIMD relaxed ops — longest matches, must be before shorter patterns */
i8x16\.relaxed_swizzle                { yylval->u32_val = 256; return SIMD_OP; }
i32x4\.relaxed_trunc_f32x4_s          { yylval->u32_val = 257; return SIMD_OP; }
i32x4\.relaxed_trunc_f32x4_u          { yylval->u32_val = 258; return SIMD_OP; }
i32x4\.relaxed_trunc_f64x2_s_zero     { yylval->u32_val = 259; return SIMD_OP; }
i32x4\.relaxed_trunc_f64x2_u_zero     { yylval->u32_val = 260; return SIMD_OP; }
f32x4\.relaxed_madd                    { yylval->u32_val = 261; return SIMD_OP; }
f32x4\.relaxed_nmadd                   { yylval->u32_val = 262; return SIMD_OP; }
f64x2\.relaxed_madd                    { yylval->u32_val = 263; return SIMD_OP; }
f64x2\.relaxed_nmadd                   { yylval->u32_val = 264; return SIMD_OP; }
i8x16\.relaxed_laneselect              { yylval->u32_val = 265; return SIMD_OP; }
i16x8\.relaxed_laneselect              { yylval->u32_val = 266; return SIMD_OP; }
i32x4\.relaxed_laneselect              { yylval->u32_val = 267; return SIMD_OP; }
i64x2\.relaxed_laneselect              { yylval->u32_val = 268; return SIMD_OP; }
f32x4\.relaxed_min                     { yylval->u32_val = 269; return SIMD_OP; }
f32x4\.relaxed_max                     { yylval->u32_val = 270; return SIMD_OP; }
f64x2\.relaxed_min                     { yylval->u32_val = 271; return SIMD_OP; }
f64x2\.relaxed_max                     { yylval->u32_val = 272; return SIMD_OP; }
i16x8\.relaxed_q15mulr_s               { yylval->u32_val = 273; return SIMD_OP; }
i16x8\.relaxed_dot_i8x16_i7x16_s      { yylval->u32_val = 274; return SIMD_OP; }
i32x4\.relaxed_dot_i8x16_i7x16_add_s  { yylval->u32_val = 275; return SIMD_OP; }

i8x16\.eq   { yylval->u32_val = 35;  return SIMD_OP; }
i16x8\.eq   { yylval->u32_val = 37;  return SIMD_OP; }
i32x4\.eq   { yylval->u32_val = 39;  return SIMD_OP; }
i64x2\.eq   { yylval->u32_val = 214; return SIMD_OP; }
f32x4\.eq   { yylval->u32_val = 65;  return SIMD_OP; }
f64x2\.eq   { yylval->u32_val = 71;  return SIMD_OP; }

    /* offset= and align= immediates — must be before ATOM catch-all */
offset=0x{HEXNUM}  {
        const char *s = strip_underscores(yytext+9);
        uint64_t v = strtoull(s,NULL,16);
        yylval->u64_val = v;
        return OFFSET_IMM;
    }
offset={NUM}        {
        const char *s = strip_underscores(yytext+7);
        uint64_t v = strtoull(s,NULL,10);
        yylval->u64_val = v;
        return OFFSET_IMM;
    }
align=0x{HEXNUM}  {
        const char *s = strip_underscores(yytext+8);
        uint64_t v = strtoull(s,NULL,16);
        yylval->u32_val = (v <= 0xFFFFFFFFu) ? (uint32_t)v : 0xFFFFFFFFu;
        if (v > 0xFFFFFFFFu) {
            wast_lex_state *st = yyget_extra(yyscanner);
            if (st) st->offset_overflow = 1;
        }
        return ALIGN_IMM;
    }
align={NUM}         { yylval->u32_val = (uint32_t)strtoul(yytext+6,NULL,10);  return ALIGN_IMM; }

    /* Dotted instruction keywords — must be before type keywords and ATOM */
struct\.get_s    { return KW_STRUCT_GET_S; }
struct\.get_u    { return KW_STRUCT_GET_U; }
struct\.get      { return KW_STRUCT_GET; }
struct\.set      { return KW_STRUCT_SET; }
v128\.const      { return KW_V128_CONST; }
local\.get       { return KW_LOCAL_GET; }
local\.set       { return KW_LOCAL_SET; }
local\.tee       { return KW_LOCAL_TEE; }
global\.get      { return KW_GLOBAL_GET; }
global\.set      { return KW_GLOBAL_SET; }
i32\.const       { return KW_I32_CONST; }
i64\.const       { return KW_I64_CONST; }
f32\.const       { return KW_F32_CONST; }
f64\.const       { return KW_F64_CONST; }
call_indirect        { return KW_CALL_INDIRECT; }
call_ref             { return KW_CALL_REF; }
return_call_indirect { return KW_RETURN_CALL_INDIRECT; }
return_call_ref      { return KW_RETURN_CALL_REF; }
return_call          { return KW_RETURN_CALL; }
br_on_non_null       { return KW_BR_ON_NON_NULL; }
br_on_null           { return KW_BR_ON_NULL; }
ref\.as_non_null { return KW_REF_AS_NON_NULL; }
ref\.func        { return KW_REF_FUNC; }
ref\.null        { return KW_REF_NULL; }
ref\.is_null     { return KW_REF_IS_NULL; }
ref\.extern      { return KW_REF_EXTERN; }
ref              { return KW_REF_TYPE; }
null             { return KW_NULL; }
extern           { return KW_EXTERN; }
memory\.init     { return KW_MEMORY_INIT; }
memory\.copy     { return KW_MEMORY_COPY; }
memory\.fill     { return KW_MEMORY_FILL; }
memory\.size     { return KW_MEMORY_SIZE; }
memory\.grow     { return KW_MEMORY_GROW; }
table\.init      { return KW_TABLE_INIT; }
table\.copy      { return KW_TABLE_COPY; }
table\.fill      { return KW_TABLE_FILL; }
table\.get       { return KW_TABLE_GET; }
table\.set       { return KW_TABLE_SET; }
table\.grow      { return KW_TABLE_GROW; }
table\.size      { return KW_TABLE_SIZE; }

    /* Multi-word assertion keywords — before shorter single-word keywords */
assert_return      { return KW_ASSERT_RETURN; }
assert_trap        { return KW_ASSERT_TRAP; }
assert_exception   { return KW_ASSERT_EXCEPTION; }
assert_exhaustion  { return KW_ASSERT_EXHAUSTION; }
assert_invalid     { return KW_ASSERT_INVALID; }
assert_malformed   { return KW_ASSERT_MALFORMED; }
assert_unlinkable  { return KW_ASSERT_UNLINKABLE; }

    /* Type keywords — before ATOM */
v128        { return KW_V128; }
i32         { return KW_I32; }
i64         { return KW_I64; }
f32         { return KW_F32; }
f64         { return KW_F64; }
funcref     { return KW_FUNCREF; }
externref   { return KW_EXTERNREF; }
anyref      { return KW_ANYREF; }
eqref       { return KW_EQREF; }
i31ref      { return KW_I31REF; }
structref   { return KW_STRUCTREF; }
arrayref    { return KW_ARRAYREF; }
exnref      { return KW_EXNREF; }
nullref     { return KW_NULLREF; }
nullfuncref { return KW_NULLFUNCREF; }
nullexnref  { return KW_NULLEXNREF; }
nullexternref { return KW_NULLEXTERNREF; }

i8x16   { return KW_I8X16; }
i16x8   { return KW_I16X8; }
i32x4   { return KW_I32X4; }
i64x2   { return KW_I64X2; }
f32x4   { return KW_F32X4; }
f64x2   { return KW_F64X2; }

    /* Script-level keywords — before ATOM */
definition   { return KW_DEFINITION; }
instance     { return KW_INSTANCE; }

    /* Module / structural keywords — before ATOM */
module       { return KW_MODULE; }
func         { return KW_FUNC; }
import       { return KW_IMPORT; }
export       { return KW_EXPORT; }
table        { return KW_TABLE; }
memory       { return KW_MEMORY; }
global       { return KW_GLOBAL; }
data         { return KW_DATA; }
elem         { return KW_ELEM; }
type         { return KW_TYPE; }
struct       { return KW_STRUCT; }
array        { return KW_ARRAY; }
field        { return KW_FIELD; }
i8           { return KW_I8; }
i16          { return KW_I16; }
any          { return KW_ANY; }
eq           { return KW_EQ; }
exn          { return KW_EXN; }
none         { return KW_NONE; }
nofunc       { return KW_NOFUNC; }
noexn        { return KW_NOEXN; }
noextern     { return KW_NOEXTERN; }
i31          { return KW_I31; }
start        { return KW_START; }
tag          { return KW_TAG; }
local        { return KW_LOCAL; }
mut          { return KW_MUT; }
param        { return KW_PARAM; }
result       { return KW_RESULT; }
declare      { return KW_DECLARE; }
rec          { return KW_REC; }
sub          { return KW_SUB; }
final        { return KW_FINAL; }
item         { return KW_ITEM; }
offset       { return KW_OFFSET_KW; }
register     { return KW_REGISTER; }

    /* Control flow keywords */
block        { return KW_BLOCK; }
loop         { return KW_LOOP; }
if           { return KW_IF; }
else         { return KW_ELSE; }
then         { return KW_THEN; }
end          { return KW_END; }
return       { return KW_RETURN; }

    /* Instructions */
call         { return KW_CALL; }
br           { return KW_BR; }
br_if        { return KW_BR_IF; }
br_table     { return KW_BR_TABLE; }
select       { return KW_SELECT; }
drop         { return KW_DROP; }
unreachable  { return KW_UNREACHABLE; }
nop          { return KW_NOP; }

    /* Invoke / assertion helpers */
invoke   { return KW_INVOKE; }
quote    { return KW_QUOTE; }
binary   { return KW_BINARY; }
either   { return KW_EITHER; }
get      { return KW_GET; }

    /* NaN / inf specials — before numeric literal rules */
nan:canonical   { return KW_NAN_CANONICAL; }
nan:arithmetic  { return KW_NAN_ARITHMETIC; }
-nan            { return KW_NEG_NAN; }
-inf            { return KW_NEG_INF; }
\+nan           { return KW_POS_NAN; }
\+inf           { return KW_POS_INF; }
nan             { return KW_NAN; }
inf             { return KW_INF; }

    /* Numeric literals — underscores allowed as separators */
-?0x{HEXNUM}  {
    const char *s = strip_underscores(yytext);
    if (s[0] == '-')
        yylval->i64_val = -(int64_t)strtoull(s + 1, NULL, 16);
    else
        yylval->i64_val = (int64_t)strtoull(s, NULL, 16);
    return HEXINT;
}

\+0x{HEXNUM}  {
    /* +0xHEX positive hex integer literal */
    const char *s = strip_underscores(yytext + 1); /* skip + */
    yylval->i64_val = (int64_t)strtoull(s, NULL, 16);
    return POSINT;
}

-?{NUM}  {
    const char *s = strip_underscores(yytext);
    if (s[0] == '-')
        yylval->i64_val = -(int64_t)strtoull(s + 1, NULL, 10);
    else
        yylval->i64_val = (int64_t)strtoull(s, NULL, 10);
    return INT;
}

\+{NUM}  {
    /* +N positive decimal integer literal */
    const char *s = strip_underscores(yytext + 1); /* skip + */
    yylval->i64_val = (int64_t)strtoull(s, NULL, 10);
    return POSINT;
}

    /* Hex float literals (with dot or exponent) — must beat ATOM rule */
[+-]?0x{HEXNUM}*("."{HEXNUM}*)?[pP][+-]?{NUM}  {
    const char *s = strip_underscores(yytext);
    yylval->f64_val = strtod(s, NULL);
    return FLOAT;
}

[+-]?0x{HEXNUM}+"."{HEXNUM}*  {
    const char *s = strip_underscores(yytext);
    yylval->f64_val = strtod(s, NULL);
    return FLOAT;
}

    /* Decimal float with exponent (no dot required) */
[+-]?{NUM}+[eE][+-]?{NUM}+  {
    const char *s = strip_underscores(yytext);
    yylval->f64_val = strtod(s, NULL);
    return FLOAT;
}

[+-]?{NUM}+"."{NUM}*([eE][+-]?{NUM})?  {
    const char *s = strip_underscores(yytext);
    yylval->f64_val = strtod(s, NULL);
    return FLOAT;
}

    /* Generic dotted instruction operators not covered by a dedicated rule. */
[a-zA-Z][a-zA-Z0-9_]*\.[a-zA-Z0-9_.]+ {
    size_t n = (size_t)yyleng;
    if (n >= sizeof(yylval->str_val)) n = sizeof(yylval->str_val) - 1;
    memcpy(yylval->str_val, yytext, n);
    yylval->str_val[n] = '\0';
    wast_simd_info simd;
    if (wast_simd_lookup(yylval->str_val, &simd) &&
        simd.immediate == WAST_SIMD_IMM_SHUFFLE)
        return SIMD_SHUFFLE_OP;
    return OP;
}

    /* Quoted identifier $"..." — must be before plain $id
     * Store as $content (strip quotes) so $"fh" matches $fh. */
\$\"([^\"\\]|\\.)*\"  {
    size_t inner_len = (size_t)(yyleng - 3); /* skip $" and trailing " */
    decode_quoted_id(yylval->str_val, sizeof(yylval->str_val),
                     yytext + 2, inner_len);
    return ID;
}

    /* Dollar-prefixed identifier — must be before ATOM catch-all */
\${ATOM_CHAR}*  {
    safe_strncpy(yylval->str_val, yytext, sizeof(yylval->str_val));
    return ID;
}

    /* Generic atom — must be last */
{ATOM_CHAR}+  {
    safe_strncpy(yylval->str_val, yytext, sizeof(yylval->str_val));
    return ATOM;
}

.   { }

%%
