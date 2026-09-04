%option reentrant bison-bridge bison-locations noyywrap nodefault 8bit
%option nounput noinput

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
%}

DIGIT       [0-9]
HEXDIGIT    [0-9a-fA-F]
WS          [ \t\r\n]+
ATOM_CHAR   [a-zA-Z0-9_.+\-*/\\^~=<>!?@#$%&|:`]

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

v128\.const     { return KW_V128_CONST; }
local\.get      { return KW_LOCAL_GET; }
module          { return KW_MODULE; }
func            { return KW_FUNC; }
export          { return KW_EXPORT; }
param           { return KW_PARAM; }
result          { return KW_RESULT; }
assert_return   { return KW_ASSERT_RETURN; }
invoke          { return KW_INVOKE; }
either          { return KW_EITHER; }

v128    { return KW_V128; }
i32     { return KW_I32; }
i64     { return KW_I64; }
f32     { return KW_F32; }
f64     { return KW_F64; }

i8x16   { return KW_I8X16; }
i16x8   { return KW_I16X8; }
i32x4   { return KW_I32X4; }
i64x2   { return KW_I64X2; }
f32x4   { return KW_F32X4; }
f64x2   { return KW_F64X2; }

nan:canonical   { return KW_NAN_CANONICAL; }
nan:arithmetic  { return KW_NAN_ARITHMETIC; }
-nan            { return KW_NEG_NAN; }
-inf            { return KW_NEG_INF; }
\+nan           { return KW_POS_NAN; }
\+inf           { return KW_POS_INF; }
nan             { return KW_NAN; }
inf             { return KW_INF; }

-?0x{HEXDIGIT}+  {
    if (yytext[0] == '-')
        yylval->i64_val = -(int64_t)strtoull(yytext + 1, NULL, 16);
    else
        yylval->i64_val = (int64_t)strtoull(yytext, NULL, 16);
    return HEXINT;
}

-?{DIGIT}+  {
    yylval->i64_val = strtoll(yytext, NULL, 10);
    return INT;
}

-?0x{HEXDIGIT}*("."{HEXDIGIT}*)?([pP][+-]?{DIGIT}+)?  {
    yylval->f64_val = strtod(yytext, NULL);
    return FLOAT;
}

-?{DIGIT}+"."{DIGIT}*([eE][+-]?{DIGIT}+)?  {
    yylval->f64_val = strtod(yytext, NULL);
    return FLOAT;
}

\+0\.0  {
    yylval->f64_val = 0.0;
    return FLOAT;
}

{ATOM_CHAR}+  {
    safe_strncpy(yylval->str_val, yytext, sizeof(yylval->str_val));
    return ATOM;
}

.   { }

%%
