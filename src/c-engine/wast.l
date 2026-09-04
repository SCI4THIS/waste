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

/* Strip underscores from a numeric literal into a static buffer */
static const char *strip_underscores(const char *s) {
    static char buf[128];
    size_t j = 0;
    for (size_t i = 0; s[i] && j + 1 < sizeof(buf); i++)
        if (s[i] != '_') buf[j++] = s[i];
    buf[j] = '\0';
    return buf;
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
ATOM_CHAR   [a-zA-Z0-9_.+\-*/\\^~=<>!?@#$%&|:`]
NUM         {DIGIT}(_*{DIGIT})*
HEXNUM      {HEXDIGIT}(_*{HEXDIGIT})*

%%

"(;"([^;]|";"[^)])*";)"    { }
";;"[^\n]*                  { }
{WS}                        { }

"("   { return LPAREN; }
")"   { return RPAREN; }

\"([^\"\\]|\\.)*\"  {
    size_t len = (size_t)(yyleng - 2);
    if (len >= sizeof(yylval->str_val)) len = sizeof(yylval->str_val) - 1;
    memcpy(yylval->str_val, yytext + 1, len);
    yylval->str_val[len] = '\0';
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
offset=0x{HEXDIGIT}+  { yylval->u32_val = (uint32_t)strtoull(yytext+7,NULL,16); return OFFSET_IMM; }
offset={DIGIT}+        { yylval->u32_val = (uint32_t)strtoul(yytext+7,NULL,10);  return OFFSET_IMM; }
align={DIGIT}+         { yylval->u32_val = (uint32_t)strtoul(yytext+6,NULL,10);  return ALIGN_IMM; }

    /* Dotted instruction keywords — must be before type keywords and ATOM */
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
call_indirect    { return KW_CALL_INDIRECT; }
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

i8x16   { return KW_I8X16; }
i16x8   { return KW_I16X8; }
i32x4   { return KW_I32X4; }
i64x2   { return KW_I64X2; }
f32x4   { return KW_F32X4; }
f64x2   { return KW_F64X2; }

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
start        { return KW_START; }
local        { return KW_LOCAL; }
mut          { return KW_MUT; }
param        { return KW_PARAM; }
result       { return KW_RESULT; }
declare      { return KW_DECLARE; }
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

-?{NUM}  {
    const char *s = strip_underscores(yytext);
    yylval->i64_val = strtoll(s, NULL, 10);
    return INT;
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

-?0x{HEXNUM}*("."{HEXNUM}*)?([pP][+-]?{NUM})?  {
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
