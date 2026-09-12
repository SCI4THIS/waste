#include "wast_stream.h"
#include "wast_runner.h"

#include <string.h>

static void advance(wast_stream *s, char c) {
    if (c == '\n') { s->line++; s->column = 1; }
    else s->column++;
}

void wast_stream_init(wast_stream *stream, const char *source, size_t length) {
    memset(stream, 0, sizeof(*stream));
    stream->source = source;
    stream->length = length;
    stream->line = 1;
    stream->column = 1;
}

static void skip_space(wast_stream *s) {
    while (s->offset < s->length) {
        const char *p = s->source;
        size_t i = s->offset;
        if (p[i] == ';' && i + 1 < s->length && p[i + 1] == ';') {
            advance(s, p[i]); advance(s, p[i + 1]); s->offset += 2;
            while (s->offset < s->length && p[s->offset] != '\n') {
                advance(s, p[s->offset++]);
            }
            continue;
        }
        if (p[i] == '(' && i + 1 < s->length && p[i + 1] == ';') {
            int depth = 1;
            advance(s, p[i]); advance(s, p[i + 1]); s->offset += 2;
            while (s->offset < s->length && depth) {
                i = s->offset;
                if (i + 1 < s->length && p[i] == '(' && p[i + 1] == ';') {
                    depth++; advance(s, p[i]); advance(s, p[i + 1]); s->offset += 2;
                } else if (i + 1 < s->length && p[i] == ';' && p[i + 1] == ')') {
                    depth--; advance(s, p[i]); advance(s, p[i + 1]); s->offset += 2;
                } else advance(s, p[s->offset++]);
            }
            continue;
        }
        if (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n') {
            advance(s, p[s->offset++]); continue;
        }
        break;
    }
}

static wast_stream_command_kind classify(const char *p, size_t n) {
    size_t i = 0;
    while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n')) i++;
    if (i >= n || p[i++] != '(') return WAST_STREAM_UNKNOWN;
    while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n')) i++;
    size_t start = i;
    while (i < n && p[i] != ' ' && p[i] != '\t' && p[i] != '\r' && p[i] != '\n' && p[i] != '(' && p[i] != ')') i++;
    size_t len = i - start;
    if (len == 6 && memcmp(p + start, "module", 6) == 0) return WAST_STREAM_MODULE;
    if (len == 6 && memcmp(p + start, "invoke", 6) == 0) return WAST_STREAM_INVOKE;
    if (len == 8 && memcmp(p + start, "register", 8) == 0) return WAST_STREAM_REGISTER;
    if (len >= 7 && memcmp(p + start, "assert_", 7) == 0) return WAST_STREAM_ASSERTION;
    return WAST_STREAM_UNKNOWN;
}

static int starts_inline_module(const char *p, size_t n) {
    static const char *fields[] = {
        "func", "memory", "global", "table", "data", "elem",
        "type", "import", "export", "start", "tag", "rec", NULL
    };
    size_t i = 0;
    if (i >= n || p[i++] != '(') return 0;
    while (i < n && (p[i] == ' ' || p[i] == '\t' ||
                     p[i] == '\r' || p[i] == '\n')) i++;
    for (int field = 0; fields[field]; field++) {
        size_t length = strlen(fields[field]);
        if (i + length <= n && memcmp(p + i, fields[field], length) == 0 &&
            (i + length == n || p[i + length] == ' ' ||
             p[i + length] == '\t' || p[i + length] == '\r' ||
             p[i + length] == '\n' || p[i + length] == '(' ||
             p[i + length] == ')'))
            return 1;
    }
    return 0;
}

int wast_stream_next(wast_stream *s, wast_stream_callback callback, void *opaque) {
    if (!s || !callback) return -1;
    skip_space(s);
    if (s->offset >= s->length) return 0;
    size_t start = s->offset;
    unsigned line = s->line;
    /* Inline-module sugar is one WAT module whose fields happen to occupy the
     * top level.  It must remain one execution unit instead of being split
     * into a sequence of unrelated WAST commands. */
    if (starts_inline_module(s->source + start, s->length - start)) {
        wast_script parsed;
        memset(&parsed, 0, sizeof(parsed));
        (void)wast_parse_bytes(s->source + start, s->length - start, &parsed);
        s->offset = s->length;
        (void)callback(WAST_STREAM_MODULE, s->source + start,
                       s->length - start, start, line, &parsed, opaque);
        wast_script_free(&parsed);
        return 1;
    }
    int depth = 0;
    int in_string = 0;
    while (s->offset < s->length) {
        size_t i = s->offset;
        char c = s->source[i];
        if (in_string) {
            if (c == '\\' && i + 1 < s->length) {
                advance(s, c); advance(s, s->source[i + 1]); s->offset += 2; continue;
            }
            if (c == '"') in_string = 0;
            advance(s, c); s->offset++; continue;
        }
        if (c == ';' && i + 1 < s->length && s->source[i + 1] == ';') {
            advance(s, c); advance(s, s->source[i + 1]); s->offset += 2;
            while (s->offset < s->length && s->source[s->offset] != '\n')
                advance(s, s->source[s->offset++]);
            continue;
        }
        if (c == '(' && i + 1 < s->length && s->source[i + 1] == ';') {
            int comment_depth = 1;
            advance(s, c); advance(s, s->source[i + 1]); s->offset += 2;
            while (s->offset < s->length && comment_depth) {
                i = s->offset;
                if (i + 1 < s->length && s->source[i] == '(' && s->source[i + 1] == ';') {
                    comment_depth++; advance(s, s->source[i]); advance(s, s->source[i + 1]); s->offset += 2;
                } else if (i + 1 < s->length && s->source[i] == ';' && s->source[i + 1] == ')') {
                    comment_depth--; advance(s, s->source[i]); advance(s, s->source[i + 1]); s->offset += 2;
                } else advance(s, s->source[s->offset++]);
            }
            continue;
        }
        if (c == '"') { in_string = 1; advance(s, c); s->offset++; continue; }
        if (c == '(') { depth++; advance(s, c); s->offset++; continue; }
        if (c == ')') {
            if (depth == 0) break;
            depth--; advance(s, c); s->offset++;
            if (depth == 0) break;
            continue;
        }
        advance(s, c); s->offset++;
    }
    if (depth != 0 || in_string) {
        strncpy(s->error, depth != 0 ? "unterminated WAST command" : "unterminated string", sizeof(s->error) - 1);
        s->error[sizeof(s->error) - 1] = '\0';
        return -1;
    }
    wast_script parsed;
    memset(&parsed, 0, sizeof(parsed));
    (void)wast_parse_bytes(s->source + start, s->offset - start, &parsed);
    (void)callback(classify(s->source + start, s->offset - start),
                   s->source + start, s->offset - start, start, line,
                   &parsed, opaque);
    wast_script_free(&parsed);
    return 1;
}

int wast_stream_run(const char *source, size_t length,
                    wast_stream_callback callback, void *opaque) {
    if (!source || !callback) return -1;
    wast_stream stream;
    wast_stream_init(&stream, source, length);
    int count = 0;
    for (;;) {
        int rc = wast_stream_next(&stream, callback, opaque);
        if (rc <= 0) return rc < 0 ? -1 : count;
        count++;
    }
}
