#include "script/wast_stream.h"
#include "text/wat_context.h"
#include "text/wat_builder.h"
#include "script/wast_runner.h"
#include "wast.tab.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef void *yyscan_t;
typedef struct yy_buffer_state *YY_BUFFER_STATE;

extern int yylex(YYSTYPE *value, YYLTYPE *location, wat_context *context,
                 yyscan_t scanner);
extern int yylex_init_extra(wat_context *context, yyscan_t *scanner);
extern int yylex_destroy(yyscan_t scanner);
extern YY_BUFFER_STATE yy_scan_bytes(const char *bytes, int length,
                                     yyscan_t scanner);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer, yyscan_t scanner);
extern void wast_lexer_begin_command_scan(yyscan_t scanner);

void wast_stream_init(wast_stream *stream, const char *source, size_t length) {
    if (!stream) return;
    memset(stream, 0, sizeof(*stream));
    stream->source = source;
    stream->length = length;
    stream->line = 1;
    stream->column = 1;
}

void wast_stream_destroy(wast_stream *stream) {
    if (!stream) return;
    yyscan_t scanner = (yyscan_t)stream->scanner;
    if (scanner && stream->buffer)
        yy_delete_buffer((YY_BUFFER_STATE)stream->buffer, scanner);
    if (scanner) yylex_destroy(scanner);
    if (stream->context)
        wast_builder_context_destroy((wat_context *)stream->context);
    stream->scanner = NULL;
    stream->buffer = NULL;
    stream->context = NULL;
    stream->initialized = 0;
}

static int initialize_scanner(wast_stream *stream) {
    if (stream->initialized) return 1;
    if (!stream->source || stream->length > (size_t)INT_MAX) {
        snprintf(stream->error, sizeof(stream->error), "%s",
                 !stream->source ? "missing WAST source" :
                                   "WAST source exceeds scanner limit");
        return 0;
    }
    wat_context *context = wast_builder_context_create();
    if (!context) {
        snprintf(stream->error, sizeof(stream->error), "%s",
                 "out of memory creating command scanner");
        return 0;
    }
    context->command_scan = 1;
    context->command_scan_inline =
        wast_source_is_inline_module(stream->source, stream->length);
    yyscan_t scanner = NULL;
    if (yylex_init_extra(context, &scanner) != 0) {
        wast_builder_context_destroy(context);
        snprintf(stream->error, sizeof(stream->error), "%s",
                 "could not initialize command scanner");
        return 0;
    }
    YY_BUFFER_STATE buffer = yy_scan_bytes(
        stream->source, (int)stream->length, scanner);
    if (!buffer) {
        yylex_destroy(scanner);
        wast_builder_context_destroy(context);
        snprintf(stream->error, sizeof(stream->error), "%s",
                 "could not allocate command scanner buffer");
        return 0;
    }
    stream->context = context;
    stream->scanner = scanner;
    stream->buffer = buffer;
    stream->initialized = 1;
    wast_lexer_begin_command_scan(scanner);
    return 1;
}

int wast_stream_next(wast_stream *stream, wast_stream_callback callback,
                     void *opaque) {
    if (!stream || !callback) return -1;
    if (stream->finished) return 0;
    if (!initialize_scanner(stream)) return -1;

    wat_context *context = (wat_context *)stream->context;
    YYSTYPE value;
    YYLTYPE location;
    memset(&value, 0, sizeof(value));
    memset(&location, 0, sizeof(location));
    int token = yylex(&value, &location, context,
                      (yyscan_t)stream->scanner);
    stream->line = (unsigned)context->lex.line;
    stream->column = (unsigned)context->lex.column;
    if (token == 0) {
        stream->offset = stream->length;
        stream->finished = 1;
        wast_stream_destroy(stream);
        return 0;
    }
    if (token != WAST_COMMAND_BOUNDARY) {
        snprintf(stream->error, sizeof(stream->error), "%s",
                 context->lexer_error[0] ? context->lexer_error :
                                           "command scanner failed");
        stream->finished = 1;
        wast_stream_destroy(stream);
        return -1;
    }

    size_t start = context->command_start_offset;
    size_t end = context->command_end_offset;
    unsigned line = context->command_start_line;
    if (start > end || end > stream->length) {
        snprintf(stream->error, sizeof(stream->error), "%s",
                 "invalid command boundary");
        wast_stream_destroy(stream);
        return -1;
    }
    context->command_scan_started = 0;
    context->command_scan_depth = 0;
    stream->offset = end;

    wast_script parsed;
    memset(&parsed, 0, sizeof(parsed));
    (void)wast_parse_bytes(stream->source + start, end - start, &parsed);
    int callback_result = callback(
        context->command_scan_inline ? WAST_STREAM_MODULE :
        wast_command_classify(stream->source + start, end - start),
        stream->source + start, end - start, start, line, &parsed, opaque);
    wast_script_free(&parsed);
    return callback_result < 0 ? -1 : 1;
}

int wast_stream_run(const char *source, size_t length,
                    wast_stream_callback callback, void *opaque) {
    if (!source || !callback) return -1;
    wast_stream stream;
    wast_stream_init(&stream, source, length);
    int count = 0;
    for (;;) {
        int result = wast_stream_next(&stream, callback, opaque);
        if (result <= 0) {
            wast_stream_destroy(&stream);
            return result < 0 ? -1 : count;
        }
        count++;
    }
}
