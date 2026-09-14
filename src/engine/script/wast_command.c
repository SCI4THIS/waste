#include "script/wast_command.h"

#include <string.h>

static int token_delimiter(char character) {
    return character == ' ' || character == '\t' || character == '\r' ||
           character == '\n' || character == '(' || character == ')';
}

wast_stream_command_kind wast_command_classify(const char *source,
                                                size_t length) {
    size_t at = 0;
    while (at < length && (source[at] == ' ' || source[at] == '\t' ||
           source[at] == '\r' || source[at] == '\n')) at++;
    if (at >= length || source[at++] != '(')
        return WAST_STREAM_UNKNOWN;
    while (at < length && (source[at] == ' ' || source[at] == '\t' ||
           source[at] == '\r' || source[at] == '\n')) at++;
    size_t start = at;
    while (at < length && !token_delimiter(source[at])) at++;
    size_t word_length = at - start;
    if (word_length == 6 && memcmp(source + start, "module", 6) == 0)
        return WAST_STREAM_MODULE;
    if (word_length == 6 && memcmp(source + start, "invoke", 6) == 0)
        return WAST_STREAM_INVOKE;
    if (word_length == 8 && memcmp(source + start, "register", 8) == 0)
        return WAST_STREAM_REGISTER;
    if (word_length >= 7 && memcmp(source + start, "assert_", 7) == 0)
        return WAST_STREAM_ASSERTION;
    return WAST_STREAM_UNKNOWN;
}
