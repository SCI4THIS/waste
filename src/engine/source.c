#include "source.h"

#include <string.h>

static int is_space(char c) {
    return c == ' ' || c == '\t';
}

static int copy_token(const char *source, size_t begin, size_t end,
                      char *destination, size_t capacity) {
    size_t length = end - begin;
    if (!length || length >= capacity) return 0;
    memcpy(destination, source + begin, length);
    destination[length] = '\0';
    return 1;
}

waste_source_result waste_source_view_init(const char *source, size_t length,
                                           waste_source_view *view) {
    if (!view) return WASTE_SOURCE_INVALID_SHEBANG;
    memset(view, 0, sizeof(*view));
    view->body = source;
    view->body_length = length;
    if (!source || length < 2 || source[0] != '#' || source[1] != '!')
        return WASTE_SOURCE_NO_SHEBANG;

    size_t line_end = 2;
    while (line_end < length && source[line_end] != '\n') line_end++;
    size_t content_end = line_end;
    if (content_end > 2 && source[content_end - 1] == '\r') content_end--;
    if (line_end - 2 >= 4096) return WASTE_SOURCE_SHEBANG_TOO_LONG;

    size_t cursor = 2;
    while (cursor < content_end && is_space(source[cursor])) cursor++;
    size_t interpreter_begin = cursor;
    while (cursor < content_end && !is_space(source[cursor])) cursor++;
    if (!copy_token(source, interpreter_begin, cursor, view->interpreter,
                    sizeof(view->interpreter)))
        return WASTE_SOURCE_INVALID_SHEBANG;
    if (view->interpreter[0] != '/') return WASTE_SOURCE_INVALID_SHEBANG;

    while (cursor < content_end && is_space(source[cursor])) cursor++;
    if (cursor < content_end) {
        size_t argument_begin = cursor;
        while (cursor < content_end && !is_space(source[cursor])) cursor++;
        if (!copy_token(source, argument_begin, cursor, view->argument,
                        sizeof(view->argument)))
            return WASTE_SOURCE_INVALID_SHEBANG;
        while (cursor < content_end && is_space(source[cursor])) cursor++;
        if (cursor != content_end) return WASTE_SOURCE_INVALID_SHEBANG;
    }

    view->has_shebang = 1;
    view->source_offset = line_end < length ? line_end + 1 : line_end;
    view->line_offset = 1;
    view->body = source + view->source_offset;
    view->body_length = length - view->source_offset;
    return WASTE_SOURCE_OK;
}
