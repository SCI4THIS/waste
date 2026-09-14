%option reentrant bison-bridge bison-locations noyywrap nodefault 8bit
%option nounput noinput
%option extra-type="wat_context *"
%x SKIP PAREN COMMAND COMMAND_STRING

%{
#include "text/wat_types.h"
#include "text/wat_context.h"
#include "text/wat_builder.h"
#include "text/wat_literal.h"
#include "wast.tab.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define YY_DECL int yylex(YYSTYPE *yylval_param, YYLTYPE *yylloc_param,     \
                         wat_context *context, yyscan_t yyscanner)

#define RETURN_PAREN_TOKEN(token) do {                                      \
    yylloc->first_line = context->paren_line;                               \
    yylloc->first_column = context->paren_column;                           \
    if ((token) != LPAREN) context->lex.fold_depth++;                       \
    BEGIN(INITIAL);                                                         \
    return (token);                                                         \
} while (0)

static void safe_strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i + 1 < n && src[i] != '\0'; i++) dest[i] = src[i];
    dest[i] = '\0';
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

static void expect_numeric(wat_context *context, int kind, int bits,
                           int count) {
    context->numeric_kind = kind;
    context->numeric_bits = bits;
    context->numeric_remaining = count;
}

static void consume_numeric(wat_context *context) {
    if (context->numeric_remaining > 0) context->numeric_remaining--;
    if (context->numeric_remaining == 0) {
        context->numeric_kind = 0;
        context->numeric_bits = 0;
    }
}

static int numeric_error(wat_context *context, const char *message) {
    if (context->lexer_error[0] == '\0')
        snprintf(context->lexer_error, sizeof(context->lexer_error), "%s",
                 message);
    context->numeric_kind = 0;
    context->numeric_remaining = 0;
    return LEX_ERROR;
}

static int integer_token(YYSTYPE *yylval, wat_context *context,
                         const char *source, size_t length, int base,
                         int positive) {
    if (context->numeric_kind == 2) {
        if (!wast_literal_float_is_valid(context, source, length,
                                         context->numeric_bits))
            return numeric_error(context, "invalid floating-point literal");
        yylval->f64_val = wast_literal_parse_float(
            context, source, context->numeric_bits);
        consume_numeric(context);
        return FLOAT;
    }
    if (context->numeric_kind == 1 &&
        !wast_literal_integer_is_valid(source, length,
                                       context->numeric_bits))
        return numeric_error(context, "invalid integer literal");
    const char *clean = wast_literal_strip_underscores(context, source);
    if (positive && clean[0] == '+') clean++;
    if (!positive && clean[0] == '-')
        yylval->i64_val = -(int64_t)strtoull(clean + 1, NULL, base);
    else
        yylval->i64_val = (int64_t)strtoull(clean, NULL, base);
    if (context->numeric_kind == 1) consume_numeric(context);
    return positive ? POSINT : base == 16 ? HEXINT : INT;
}

static int float_token(YYSTYPE *yylval, wat_context *context,
                       const char *source, size_t length) {
    int bits = context->numeric_kind == 2 ? context->numeric_bits : 64;
    if (!wast_literal_float_is_valid(context, source, length, bits))
        return numeric_error(context, "invalid floating-point literal");
    yylval->f64_val = wast_literal_parse_float(context, source, bits);
    if (context->numeric_kind == 2) consume_numeric(context);
    return FLOAT;
}

#define YY_USER_ACTION do {                                                   \
    yylloc->first_line = yyextra->lex.line;                                   \
    yylloc->first_column = yyextra->lex.column;                               \
    yyextra->lex.byte_offset += (size_t)yyleng;                               \
    for (int loc_i = 0; loc_i < yyleng; loc_i++) {                            \
        if (yytext[loc_i] == '\n') {                                         \
            yyextra->lex.line++; yyextra->lex.column = 1;                     \
        } else yyextra->lex.column++;                                         \
    }                                                                         \
    yylloc->last_line = yyextra->lex.line;                                    \
    yylloc->last_column = yyextra->lex.column;                                \
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

<COMMAND>{WS} { }
<COMMAND>";;"[^\x0a\x0d]* { }
<COMMAND>"(;" {
    context->skip_annotation_depth = 0;
    context->skip_comment_depth = 1;
    context->skip_in_string = 0;
    context->skip_line_comment = 0;
    context->skip_return_paren = 2;
    BEGIN(SKIP);
}
<COMMAND>"(@" {
    if (context->command_scan_inline && !context->command_scan_started) {
        context->command_scan_started = 1;
        context->command_start_offset = context->lex.byte_offset -
                                        (size_t)yyleng;
        context->command_start_line = (unsigned)yylloc->first_line;
    }
    context->skip_annotation_depth = 1;
    context->skip_comment_depth = 0;
    context->skip_in_string = 0;
    context->skip_line_comment = 0;
    context->skip_return_paren = 2;
    BEGIN(SKIP);
}
<COMMAND>\" {
    if (!context->command_scan_started) {
        context->command_scan_started = 1;
        context->command_start_offset = context->lex.byte_offset - 1;
        context->command_start_line = (unsigned)yylloc->first_line;
    }
    BEGIN(COMMAND_STRING);
}
<COMMAND>"(" {
    if (!context->command_scan_started) {
        context->command_scan_started = 1;
        context->command_start_offset = context->lex.byte_offset - 1;
        context->command_start_line = (unsigned)yylloc->first_line;
    }
    context->command_scan_depth++;
}
<COMMAND>")" {
    if (!context->command_scan_started) {
        context->command_scan_started = 1;
        context->command_start_offset = context->lex.byte_offset - 1;
        context->command_start_line = (unsigned)yylloc->first_line;
    }
    if (context->command_scan_depth > 0) context->command_scan_depth--;
    if (!context->command_scan_inline && context->command_scan_depth == 0) {
        context->command_end_offset = context->lex.byte_offset;
        return WAST_COMMAND_BOUNDARY;
    }
}
<COMMAND>. {
    if (!context->command_scan_started) {
        context->command_scan_started = 1;
        context->command_start_offset = context->lex.byte_offset -
                                        (size_t)yyleng;
        context->command_start_line = (unsigned)yylloc->first_line;
    }
}
<COMMAND><<EOF>> {
    if (context->command_scan_finished) return 0;
    context->command_scan_finished = 1;
    if (!context->command_scan_started) return 0;
    if (!context->command_scan_inline && context->command_scan_depth != 0) {
        snprintf(context->lexer_error, sizeof(context->lexer_error), "%s",
                 "unterminated WAST command");
        return LEX_ERROR;
    }
    context->command_end_offset = context->lex.byte_offset;
    return WAST_COMMAND_BOUNDARY;
}

<COMMAND_STRING>\\. { }
<COMMAND_STRING>\\\n { }
<COMMAND_STRING>\" { BEGIN(COMMAND); }
<COMMAND_STRING>\n { }
<COMMAND_STRING>. { }
<COMMAND_STRING><<EOF>> {
    snprintf(context->lexer_error, sizeof(context->lexer_error), "%s",
             "unterminated string");
    return LEX_ERROR;
}

"(;"                        {
    context->skip_annotation_depth = 0;
    context->skip_comment_depth = 1;
    context->skip_in_string = 0;
    context->skip_line_comment = 0;
    context->skip_return_paren = 0;
    BEGIN(SKIP);
}
"(@"                        {
    if (context->inline_module && !context->inline_token_emitted) {
        context->inline_token_emitted = 1;
        context->lex.byte_offset -= (size_t)yyleng;
        context->lex.line = yylloc->first_line;
        context->lex.column = yylloc->first_column;
        yylloc->last_line = yylloc->first_line;
        yylloc->last_column = yylloc->first_column;
        yyless(0);
        return INLINE_MODULE_START;
    }
    context->skip_annotation_depth = 1;
    context->skip_comment_depth = 0;
    context->skip_in_string = 0;
    context->skip_line_comment = 0;
    context->skip_return_paren = 0;
    BEGIN(SKIP);
}
";;"[^\x0a\x0d]*           { }
{WS}                        { }

<SKIP>"(;" {
    if (!context->skip_in_string && !context->skip_line_comment)
        context->skip_comment_depth++;
}
<SKIP>";)" {
    if (!context->skip_in_string && !context->skip_line_comment) {
        if (context->skip_comment_depth > 0) {
            context->skip_comment_depth--;
            if (context->skip_comment_depth == 0 &&
                context->skip_annotation_depth == 0)
                BEGIN(context->skip_return_paren == 1 ? PAREN :
                      context->skip_return_paren == 2 ? COMMAND : INITIAL);
        } else if (context->skip_annotation_depth > 0) {
            context->skip_annotation_depth--;
            if (context->skip_annotation_depth == 0)
                BEGIN(context->skip_return_paren == 1 ? PAREN :
                      context->skip_return_paren == 2 ? COMMAND : INITIAL);
        }
    }
}
<SKIP>";;" {
    if (!context->skip_in_string && context->skip_comment_depth == 0)
        context->skip_line_comment = 1;
}
<SKIP>\n {
    context->skip_line_comment = 0;
}
<SKIP>\\. { }
<SKIP>\" {
    if (!context->skip_line_comment && context->skip_comment_depth == 0)
        context->skip_in_string = !context->skip_in_string;
}
<SKIP>"(" {
    if (!context->skip_in_string && !context->skip_line_comment &&
        context->skip_comment_depth == 0 &&
        context->skip_annotation_depth > 0)
        context->skip_annotation_depth++;
}
<SKIP>")" {
    if (!context->skip_in_string && !context->skip_line_comment &&
        context->skip_comment_depth == 0 &&
        context->skip_annotation_depth > 0) {
        context->skip_annotation_depth--;
        if (context->skip_annotation_depth == 0)
            BEGIN(context->skip_return_paren == 1 ? PAREN :
                  context->skip_return_paren == 2 ? COMMAND : INITIAL);
    }
}
<SKIP>. { }
<SKIP><<EOF>> {
    yylloc->first_line = context->lex.line;
    yylloc->first_column = context->lex.column;
    yylloc->last_line = context->lex.line;
    yylloc->last_column = context->lex.column;
    snprintf(context->lexer_error, sizeof(context->lexer_error), "%s",
             context->skip_comment_depth ? "unterminated block comment" :
                                           "unterminated annotation");
    BEGIN(INITIAL);
    return LEX_ERROR;
}

<PAREN>{WS} { }
<PAREN>"(;" {
    context->skip_annotation_depth = 0;
    context->skip_comment_depth = 1;
    context->skip_in_string = 0;
    context->skip_line_comment = 0;
    context->skip_return_paren = 1;
    BEGIN(SKIP);
}
<PAREN>"(@" {
    context->skip_annotation_depth = 1;
    context->skip_comment_depth = 0;
    context->skip_in_string = 0;
    context->skip_line_comment = 0;
    context->skip_return_paren = 1;
    BEGIN(SKIP);
}
<PAREN>block { RETURN_PAREN_TOKEN(FOLD_BLOCK_START); }
<PAREN>loop { RETURN_PAREN_TOKEN(FOLD_LOOP_START); }
<PAREN>if { RETURN_PAREN_TOKEN(FOLD_IF_START); }
<PAREN>local\.get { RETURN_PAREN_TOKEN(FOLD_LOCAL_GET_START); }
<PAREN>local\.set { RETURN_PAREN_TOKEN(FOLD_LOCAL_SET_START); }
<PAREN>local\.tee { RETURN_PAREN_TOKEN(FOLD_LOCAL_TEE_START); }
<PAREN>global\.get { RETURN_PAREN_TOKEN(FOLD_GLOBAL_GET_START); }
<PAREN>global\.set { RETURN_PAREN_TOKEN(FOLD_GLOBAL_SET_START); }
<PAREN>ref\.null { RETURN_PAREN_TOKEN(FOLD_REF_NULL_START); }
<PAREN>ref\.i31 { RETURN_PAREN_TOKEN(FOLD_REF_I31_START); }
<PAREN>array\.new_fixed { RETURN_PAREN_TOKEN(FOLD_ARRAY_NEW_FIXED_START); }
<PAREN>array\.new { RETURN_PAREN_TOKEN(FOLD_ARRAY_NEW_START); }
<PAREN>global { RETURN_PAREN_TOKEN(MODULE_GLOBAL_START); }
<PAREN>mut { RETURN_PAREN_TOKEN(MODULE_GLOBAL_MUT_START); }
<PAREN>memory { RETURN_PAREN_TOKEN(MODULE_MEMORY_START); }
<PAREN>then { RETURN_PAREN_TOKEN(FOLD_THEN_START); }
<PAREN>else { RETURN_PAREN_TOKEN(FOLD_ELSE_START); }
<PAREN>drop { RETURN_PAREN_TOKEN(FOLD_DROP_START); }
<PAREN>nop { RETURN_PAREN_TOKEN(FOLD_NOP_START); }
<PAREN>unreachable { RETURN_PAREN_TOKEN(FOLD_UNREACHABLE_START); }
<PAREN>return { RETURN_PAREN_TOKEN(FOLD_RETURN_START); }
<PAREN>br_table { RETURN_PAREN_TOKEN(FOLD_BR_TABLE_START); }
<PAREN>br_on_non_null { RETURN_PAREN_TOKEN(FOLD_BR_ON_NON_NULL_START); }
<PAREN>br_on_null { RETURN_PAREN_TOKEN(FOLD_BR_ON_NULL_START); }
<PAREN>br_if { RETURN_PAREN_TOKEN(FOLD_BR_IF_START); }
<PAREN>br { RETURN_PAREN_TOKEN(FOLD_BR_START); }
<PAREN>return_call_indirect { RETURN_PAREN_TOKEN(FOLD_RETURN_CALL_INDIRECT_START); }
<PAREN>return_call_ref { RETURN_PAREN_TOKEN(FOLD_RETURN_CALL_REF_START); }
<PAREN>return_call { RETURN_PAREN_TOKEN(FOLD_RETURN_CALL_START); }
<PAREN>call_ref { RETURN_PAREN_TOKEN(FOLD_CALL_REF_START); }
<PAREN>call_indirect { RETURN_PAREN_TOKEN(FOLD_CALL_INDIRECT_START); }
<PAREN>call { RETURN_PAREN_TOKEN(FOLD_CALL_START); }
<PAREN>try_table { RETURN_PAREN_TOKEN(FOLD_TRY_TABLE_START); }
<PAREN>catch_all_ref { RETURN_PAREN_TOKEN(FOLD_CATCH_ALL_REF_START); }
<PAREN>catch_all { RETURN_PAREN_TOKEN(FOLD_CATCH_ALL_START); }
<PAREN>catch_ref { RETURN_PAREN_TOKEN(FOLD_CATCH_REF_START); }
<PAREN>catch { RETURN_PAREN_TOKEN(FOLD_CATCH_START); }
<PAREN>throw_ref { RETURN_PAREN_TOKEN(FOLD_THROW_REF_START); }
<PAREN>throw { RETURN_PAREN_TOKEN(FOLD_THROW_START); }
<PAREN>select { RETURN_PAREN_TOKEN(FOLD_SELECT_START); }
<PAREN>[a-zA-Z][a-zA-Z0-9_]*\.[a-zA-Z0-9_.]+ {
    size_t n = (size_t)yyleng;
    if (n >= sizeof(yylval->str_val)) n = sizeof(yylval->str_val) - 1;
    memcpy(yylval->str_val, yytext, n);
    yylval->str_val[n] = '\0';
    if (strcmp(yylval->str_val, "i32.const") == 0)
        expect_numeric(context, 1, 32, 1);
    else if (strcmp(yylval->str_val, "i64.const") == 0)
        expect_numeric(context, 1, 64, 1);
    else if (strcmp(yylval->str_val, "f32.const") == 0)
        expect_numeric(context, 2, 32, 1);
    else if (strcmp(yylval->str_val, "f64.const") == 0)
        expect_numeric(context, 2, 64, 1);
    else if (strcmp(yylval->str_val, "v128.const") == 0)
        context->v128_const_pending = 1;
    wast_simd_info simd;
    if (wast_simd_lookup(yylval->str_val, &simd)) {
        if (simd.immediate == WAST_SIMD_IMM_MEMARG)
            RETURN_PAREN_TOKEN(FOLD_SIMD_MEM_START);
        if (simd.immediate == WAST_SIMD_IMM_MEMARG_LANE)
            RETURN_PAREN_TOKEN(FOLD_SIMD_MEM_LANE_START);
        if (simd.immediate == WAST_SIMD_IMM_LANE)
            RETURN_PAREN_TOKEN(FOLD_SIMD_LANE_START);
        if (simd.immediate == WAST_SIMD_IMM_SHUFFLE)
            RETURN_PAREN_TOKEN(FOLD_SIMD_SHUFFLE_START);
    }
    RETURN_PAREN_TOKEN(FOLD_ATOM_START);
}
<PAREN>. {
    context->lex.byte_offset -= (size_t)yyleng;
    context->lex.line = yylloc->first_line;
    context->lex.column = yylloc->first_column;
    yyless(0);
    RETURN_PAREN_TOKEN(LPAREN);
}
<PAREN><<EOF>> { RETURN_PAREN_TOKEN(LPAREN); }

    /* Folded control starts are classified as one boundary token.  This avoids
     * the LR parser choosing a function-header attribute after seeing `(` before
     * it can inspect the following keyword. */
\([ \t\r\n]*block { yyextra->lex.fold_depth++; return FOLD_BLOCK_START; }
\([ \t\r\n]*loop  { yyextra->lex.fold_depth++; return FOLD_LOOP_START; }
\([ \t\r\n]*if    { yyextra->lex.fold_depth++; return FOLD_IF_START; }
\([ \t\r\n]*local\.get { yyextra->lex.fold_depth++; return FOLD_LOCAL_GET_START; }
\([ \t\r\n]*local\.set { yyextra->lex.fold_depth++; return FOLD_LOCAL_SET_START; }
\([ \t\r\n]*local\.tee { yyextra->lex.fold_depth++; return FOLD_LOCAL_TEE_START; }
\([ \t\r\n]*global\.get { yyextra->lex.fold_depth++; return FOLD_GLOBAL_GET_START; }
\([ \t\r\n]*global\.set { yyextra->lex.fold_depth++; return FOLD_GLOBAL_SET_START; }
\([ \t\r\n]*ref\.null { yyextra->lex.fold_depth++; return FOLD_REF_NULL_START; }
\([ \t\r\n]*ref\.i31 { yyextra->lex.fold_depth++; return FOLD_REF_I31_START; }
\([ \t\r\n]*array\.new_fixed { yyextra->lex.fold_depth++; return FOLD_ARRAY_NEW_FIXED_START; }
\([ \t\r\n]*array\.new { yyextra->lex.fold_depth++; return FOLD_ARRAY_NEW_START; }
\([ \t\r\n]*global { yyextra->lex.fold_depth++; return MODULE_GLOBAL_START; }
\([ \t\r\n]*mut { return MODULE_GLOBAL_MUT_START; }
\([ \t\r\n]*module[ \t\r\n]+binary { yyextra->lex.fold_depth++; return MODULE_BINARY_START; }
\([ \t\r\n]*module[ \t\r\n]+quote { yyextra->lex.fold_depth++; return MODULE_QUOTE_START; }
\([ \t\r\n]*module[ \t\r\n]+\$[a-zA-Z0-9_.+\-]+[ \t\r\n]+binary { yyextra->lex.fold_depth++; return MODULE_ID_BINARY_START; }
\([ \t\r\n]*module[ \t\r\n]+\$[a-zA-Z0-9_.+\-]+[ \t\r\n]+quote { yyextra->lex.fold_depth++; return MODULE_ID_QUOTE_START; }
\([ \t\r\n]*memory { yyextra->lex.fold_depth++; return MODULE_MEMORY_START; }
\([ \t\r\n]*then { yyextra->lex.fold_depth++; return FOLD_THEN_START; }
\([ \t\r\n]*else { yyextra->lex.fold_depth++; return FOLD_ELSE_START; }
\([ \t\r\n]*drop { yyextra->lex.fold_depth++; return FOLD_DROP_START; }
\([ \t\r\n]*nop { yyextra->lex.fold_depth++; return FOLD_NOP_START; }
\([ \t\r\n]*unreachable { yyextra->lex.fold_depth++; return FOLD_UNREACHABLE_START; }
\([ \t\r\n]*return { yyextra->lex.fold_depth++; return FOLD_RETURN_START; }
\([ \t\r\n]*br_table { yyextra->lex.fold_depth++; return FOLD_BR_TABLE_START; }
\([ \t\r\n]*br_on_non_null { yyextra->lex.fold_depth++; return FOLD_BR_ON_NON_NULL_START; }
\([ \t\r\n]*br_on_null { yyextra->lex.fold_depth++; return FOLD_BR_ON_NULL_START; }
\([ \t\r\n]*br_if { yyextra->lex.fold_depth++; return FOLD_BR_IF_START; }
\([ \t\r\n]*br { yyextra->lex.fold_depth++; return FOLD_BR_START; }
\([ \t\r\n]*return_call_indirect { yyextra->lex.fold_depth++; return FOLD_RETURN_CALL_INDIRECT_START; }
\([ \t\r\n]*return_call_ref { yyextra->lex.fold_depth++; return FOLD_RETURN_CALL_REF_START; }
\([ \t\r\n]*return_call { yyextra->lex.fold_depth++; return FOLD_RETURN_CALL_START; }
\([ \t\r\n]*call_ref { yyextra->lex.fold_depth++; return FOLD_CALL_REF_START; }
\([ \t\r\n]*call_indirect { yyextra->lex.fold_depth++; return FOLD_CALL_INDIRECT_START; }
\([ \t\r\n]*call { yyextra->lex.fold_depth++; return FOLD_CALL_START; }
\([ \t\r\n]*try_table { yyextra->lex.fold_depth++; return FOLD_TRY_TABLE_START; }
\([ \t\r\n]*catch_all_ref { yyextra->lex.fold_depth++; return FOLD_CATCH_ALL_REF_START; }
\([ \t\r\n]*catch_all { yyextra->lex.fold_depth++; return FOLD_CATCH_ALL_START; }
\([ \t\r\n]*catch_ref { yyextra->lex.fold_depth++; return FOLD_CATCH_REF_START; }
\([ \t\r\n]*catch { yyextra->lex.fold_depth++; return FOLD_CATCH_START; }
\([ \t\r\n]*throw_ref { yyextra->lex.fold_depth++; return FOLD_THROW_REF_START; }
\([ \t\r\n]*throw { yyextra->lex.fold_depth++; return FOLD_THROW_START; }
\([ \t\r\n]*select { yyextra->lex.fold_depth++; return FOLD_SELECT_START; }
\([ \t\r\n]*br_on_cast(_fail)? {
    const char *p = yytext + 1;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    size_t n = strlen(p);
    if (n >= sizeof(yylval->str_val)) n = sizeof(yylval->str_val) - 1;
    memcpy(yylval->str_val, p, n);
    yylval->str_val[n] = '\0';
    yyextra->lex.fold_depth++;
    return FOLD_ATOM_START;
}
\([ \t\r\n]*{MEMOP} {
    const char *p = yytext + 1;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    size_t n = strlen(p);
    if (n >= sizeof(yylval->str_val)) n = sizeof(yylval->str_val) - 1;
    memcpy(yylval->str_val, p, n);
    yylval->str_val[n] = '\0';
    yyextra->lex.fold_depth++;
    return FOLD_MEMOP_START;
}
\([ \t\r\n]*[a-zA-Z][a-zA-Z0-9_]*\.[a-zA-Z0-9_.]+ {
    const char *p = yytext + 1;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    size_t n = strlen(p); if (n >= sizeof(yylval->str_val)) n = sizeof(yylval->str_val)-1;
    memcpy(yylval->str_val, p, n); yylval->str_val[n] = '\0';
    yyextra->lex.fold_depth++;
    if (strcmp(yylval->str_val, "i32.const") == 0)
        expect_numeric(context, 1, 32, 1);
    else if (strcmp(yylval->str_val, "i64.const") == 0)
        expect_numeric(context, 1, 64, 1);
    else if (strcmp(yylval->str_val, "f32.const") == 0)
        expect_numeric(context, 2, 32, 1);
    else if (strcmp(yylval->str_val, "f64.const") == 0)
        expect_numeric(context, 2, 64, 1);
    else if (strcmp(yylval->str_val, "v128.const") == 0)
        context->v128_const_pending = 1;
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

"("   {
    if (context->inline_module && !context->inline_token_emitted) {
        context->inline_token_emitted = 1;
        context->lex.byte_offset -= (size_t)yyleng;
        context->lex.line = yylloc->first_line;
        context->lex.column = yylloc->first_column;
        yylloc->last_line = yylloc->first_line;
        yylloc->last_column = yylloc->first_column;
        yyless(0);
        return INLINE_MODULE_START;
    }
    context->paren_line = yylloc->first_line;
    context->paren_column = yylloc->first_column;
    BEGIN(PAREN);
}
")"   { if (yyextra->lex.fold_depth > 0) yyextra->lex.fold_depth--; return RPAREN; }

\"([^\"\\]|\\.)*\"  {
    size_t len = (size_t)(yyleng - 2);
    yylval->string_val = wast_builder_retain_string(context, yytext + 1, len);
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
        const char *s = wast_literal_strip_underscores(context, yytext+9);
        uint64_t v = strtoull(s,NULL,16);
        yylval->u64_val = v;
        return OFFSET_IMM;
    }
offset={NUM}        {
        const char *s = wast_literal_strip_underscores(context, yytext+7);
        uint64_t v = strtoull(s,NULL,10);
        yylval->u64_val = v;
        return OFFSET_IMM;
    }
align=0x{HEXNUM}  {
        const char *s = wast_literal_strip_underscores(context, yytext+8);
        uint64_t v = strtoull(s,NULL,16);
        yylval->u32_val = (v <= 0xFFFFFFFFu) ? (uint32_t)v : 0xFFFFFFFFu;
        if (v > 0xFFFFFFFFu) {
            wat_context *st = yyget_extra(yyscanner);
            if (st) st->lex.offset_overflow = 1;
        }
        return ALIGN_IMM;
    }
align={NUM}         { yylval->u32_val = (uint32_t)strtoul(yytext+6,NULL,10);  return ALIGN_IMM; }

    /* Dotted instruction keywords — must be before type keywords and ATOM */
struct\.get_s    { return KW_STRUCT_GET_S; }
struct\.get_u    { return KW_STRUCT_GET_U; }
struct\.get      { return KW_STRUCT_GET; }
struct\.set      { return KW_STRUCT_SET; }
v128\.const      { context->v128_const_pending = 1; return KW_V128_CONST; }
local\.get       { return KW_LOCAL_GET; }
local\.set       { return KW_LOCAL_SET; }
local\.tee       { return KW_LOCAL_TEE; }
global\.get      { return KW_GLOBAL_GET; }
global\.set      { return KW_GLOBAL_SET; }
i32\.const       { expect_numeric(context, 1, 32, 1); return KW_I32_CONST; }
i64\.const       { expect_numeric(context, 1, 64, 1); return KW_I64_CONST; }
f32\.const       { expect_numeric(context, 2, 32, 1); return KW_F32_CONST; }
f64\.const       { expect_numeric(context, 2, 64, 1); return KW_F64_CONST; }
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
assert_malformed_custom { return KW_ASSERT_MALFORMED_CUSTOM; }
assert_invalid_custom { return KW_ASSERT_INVALID_CUSTOM; }
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

i8x16   { if (context->v128_const_pending) { expect_numeric(context,1,8,16); context->v128_const_pending=0; } return KW_I8X16; }
i16x8   { if (context->v128_const_pending) { expect_numeric(context,1,16,8); context->v128_const_pending=0; } return KW_I16X8; }
i32x4   { if (context->v128_const_pending) { expect_numeric(context,1,32,4); context->v128_const_pending=0; } return KW_I32X4; }
i64x2   { if (context->v128_const_pending) { expect_numeric(context,1,64,2); context->v128_const_pending=0; } return KW_I64X2; }
f32x4   { if (context->v128_const_pending) { expect_numeric(context,2,32,4); context->v128_const_pending=0; } return KW_F32X4; }
f64x2   { if (context->v128_const_pending) { expect_numeric(context,2,64,2); context->v128_const_pending=0; } return KW_F64X2; }

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
nan:canonical   {
    if (context->strict_wat_mode && context->numeric_kind == 2)
        return numeric_error(context,
                             "script NaN pattern is not a WAT literal");
    if (context->numeric_kind == 2) consume_numeric(context);
    return KW_NAN_CANONICAL;
}
nan:arithmetic  {
    if (context->strict_wat_mode && context->numeric_kind == 2)
        return numeric_error(context,
                             "script NaN pattern is not a WAT literal");
    if (context->numeric_kind == 2) consume_numeric(context);
    return KW_NAN_ARITHMETIC;
}
-nan            { if (context->numeric_kind == 2) consume_numeric(context); return KW_NEG_NAN; }
-inf            { if (context->numeric_kind == 2) consume_numeric(context); return KW_NEG_INF; }
\+nan           { if (context->numeric_kind == 2) consume_numeric(context); return KW_POS_NAN; }
\+inf           { if (context->numeric_kind == 2) consume_numeric(context); return KW_POS_INF; }
nan             { if (context->numeric_kind == 2) consume_numeric(context); return KW_NAN; }
inf             { if (context->numeric_kind == 2) consume_numeric(context); return KW_INF; }

    /* Numeric literals — underscores allowed as separators */
-?0x{HEXNUM}  {
    return integer_token(yylval, context, yytext, (size_t)yyleng, 16, 0);
}

\+0x{HEXNUM}  {
    return integer_token(yylval, context, yytext, (size_t)yyleng, 16, 1);
}

-?{NUM}  {
    return integer_token(yylval, context, yytext, (size_t)yyleng, 10, 0);
}

\+{NUM}  {
    return integer_token(yylval, context, yytext, (size_t)yyleng, 10, 1);
}

    /* Hex float literals (with dot or exponent) — must beat ATOM rule */
[+-]?0x{HEXNUM}*("."{HEXNUM}*)?[pP][+-]?{NUM}  {
    return float_token(yylval, context, yytext, (size_t)yyleng);
}

[+-]?0x{HEXNUM}+"."{HEXNUM}*  {
    return float_token(yylval, context, yytext, (size_t)yyleng);
}

    /* Decimal float with exponent (no dot required) */
[+-]?{NUM}+[eE][+-]?{NUM}+  {
    return float_token(yylval, context, yytext, (size_t)yyleng);
}

[+-]?{NUM}+"."{NUM}*([eE][+-]?{NUM})?  {
    return float_token(yylval, context, yytext, (size_t)yyleng);
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
    if (context->numeric_kind != 0) {
        if (context->numeric_kind == 2 &&
            wast_literal_float_is_valid(context, yytext, (size_t)yyleng,
                                        context->numeric_bits)) {
            consume_numeric(context);
        } else {
            return numeric_error(context, context->numeric_kind == 2 ?
                                 "invalid floating-point literal" :
                                 "invalid integer literal");
        }
    }
    safe_strncpy(yylval->str_val, yytext, sizeof(yylval->str_val));
    return ATOM;
}

.   { }

%%

void wast_lexer_begin_command_scan(yyscan_t scanner) {
    struct yyguts_t *yyg = (struct yyguts_t *)scanner;
    BEGIN(COMMAND);
}
