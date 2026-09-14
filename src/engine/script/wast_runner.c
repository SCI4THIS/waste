#include "script/wast_runner.h"
#include "text/wat_context.h"
#include "text/wat_builder.h"
#include "text/wat_types.h"
#include "runtime/engine_internal.h"
#include "wasm/wasm_encode.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>

static int wast_parse_bytes_mode(const char *bytes, size_t length,
                                 wast_script *script, int strict_wat_mode);

static int raw_hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (unsigned char)(c | 0x20u);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static int raw_append(uint8_t **bytes, size_t *length, size_t *capacity,
                      uint8_t value) {
    if (*length == *capacity) {
        size_t next_capacity = *capacity ? *capacity * 2 : 64;
        if (next_capacity < *capacity) return 0;
        uint8_t *next = realloc(*bytes, next_capacity);
        if (!next) return 0;
        *bytes = next;
        *capacity = next_capacity;
    }
    (*bytes)[(*length)++] = value;
    return 1;
}

static int raw_append_utf8(uint8_t **bytes, size_t *length, size_t *capacity,
                           uint32_t codepoint) {
    if (codepoint <= 0x7fu)
        return raw_append(bytes, length, capacity, (uint8_t)codepoint);
    if (codepoint <= 0x7ffu)
        return raw_append(bytes, length, capacity,
                          (uint8_t)(0xc0u | (codepoint >> 6))) &&
               raw_append(bytes, length, capacity,
                          (uint8_t)(0x80u | (codepoint & 0x3fu)));
    if (codepoint <= 0xffffu)
        return raw_append(bytes, length, capacity,
                          (uint8_t)(0xe0u | (codepoint >> 12))) &&
               raw_append(bytes, length, capacity,
                          (uint8_t)(0x80u | ((codepoint >> 6) & 0x3fu))) &&
               raw_append(bytes, length, capacity,
                          (uint8_t)(0x80u | (codepoint & 0x3fu)));
    return raw_append(bytes, length, capacity,
                      (uint8_t)(0xf0u | (codepoint >> 18))) &&
           raw_append(bytes, length, capacity,
                      (uint8_t)(0x80u | ((codepoint >> 12) & 0x3fu))) &&
           raw_append(bytes, length, capacity,
                      (uint8_t)(0x80u | ((codepoint >> 6) & 0x3fu))) &&
           raw_append(bytes, length, capacity,
                      (uint8_t)(0x80u | (codepoint & 0x3fu)));
}

static int raw_valid_utf8(const uint8_t *bytes, size_t length) {
    size_t i = 0;
    while (i < length) {
        uint8_t first = bytes[i++];
        if (first <= 0x7f) continue;
        if (first >= 0xc2 && first <= 0xdf) {
            if (i >= length || bytes[i] < 0x80 || bytes[i] > 0xbf) return 0;
            i++;
        } else if (first >= 0xe0 && first <= 0xef) {
            if (i + 1 >= length) return 0;
            uint8_t second = bytes[i], third = bytes[i + 1];
            if (third < 0x80 || third > 0xbf ||
                (first == 0xe0 && (second < 0xa0 || second > 0xbf)) ||
                (first == 0xed && (second < 0x80 || second > 0x9f)) ||
                (first != 0xe0 && first != 0xed &&
                 (second < 0x80 || second > 0xbf))) return 0;
            i += 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            if (i + 2 >= length) return 0;
            uint8_t second = bytes[i], third = bytes[i + 1], fourth = bytes[i + 2];
            if (third < 0x80 || third > 0xbf || fourth < 0x80 || fourth > 0xbf ||
                (first == 0xf0 && (second < 0x90 || second > 0xbf)) ||
                (first == 0xf4 && (second < 0x80 || second > 0x8f)) ||
                (first != 0xf0 && first != 0xf4 &&
                 (second < 0x80 || second > 0xbf))) return 0;
            i += 3;
        } else return 0;
    }
    return 1;
}

static int raw_decode_string(const char *source, size_t begin, size_t end,
                             uint8_t **bytes, size_t *length,
                             size_t *capacity) {
    for (size_t i = begin; i < end; i++) {
        unsigned char c = (unsigned char)source[i];
        if (c != '\\') {
            if (!raw_append(bytes, length, capacity, c)) return 0;
            continue;
        }
        if (++i >= end) return 1;
        c = (unsigned char)source[i];
        int high = raw_hex_digit(c);
        int low = i + 1 < end ?
                  raw_hex_digit((unsigned char)source[i + 1]) : -1;
        if (high >= 0 && low >= 0) {
            if (!raw_append(bytes, length, capacity,
                            (uint8_t)((high << 4) | low))) return 0;
            i++;
        } else if (c == 'u' && i + 1 < end && source[i + 1] == '{') {
            uint32_t codepoint = 0;
            i += 2;
            while (i < end && source[i] != '}') {
                int digit = raw_hex_digit((unsigned char)source[i++]);
                if (digit >= 0 && codepoint <= 0x10ffffu / 16u)
                    codepoint = codepoint * 16u + (uint32_t)digit;
            }
            if (!raw_append_utf8(bytes, length, capacity, codepoint)) return 0;
        } else {
            if (c == 't') c = '\t';
            else if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            if (!raw_append(bytes, length, capacity, c)) return 0;
        }
    }
    return 1;
}

static int raw_quote_text_is_lexically_valid(const uint8_t *source,
                                             size_t length) {
    if (!raw_valid_utf8(source, length)) return 0;
    int paren_depth = 0;
    int block_comment_depth = 0;
    int line_comment = 0;
    for (size_t i = 0; i < length; i++) {
        uint8_t c = source[i];
        if ((c < 0x20 && c != '\t' && c != '\n' && c != '\r') || c == 0x7f)
            return 0;
        if (line_comment) {
            if (c == '\n') line_comment = 0;
            continue;
        }
        if (block_comment_depth) {
            if (c == '(' && i + 1 < length && source[i + 1] == ';') {
                block_comment_depth++;
                i++;
            } else if (c == ';' && i + 1 < length &&
                       source[i + 1] == ')') {
                block_comment_depth--;
                i++;
            }
            continue;
        }
        if (c == ';' && i + 1 < length && source[i + 1] == ';') {
            line_comment = 1;
            i++;
            continue;
        }
        if (c == '(' && i + 1 < length && source[i + 1] == ';') {
            block_comment_depth = 1;
            i++;
            continue;
        }
        if (c == '(') {
            paren_depth++;
            /* An annotation identifier immediately follows the '@'.  Keep an
             * empty id visible to the annotation validator and scanner. */
            if (i + 1 < length && source[i + 1] == '@') {
                size_t id = i + 2;
                if (id >= length || source[id] == ' ' || source[id] == '\t' ||
                    source[id] == '\r' || source[id] == '\n' ||
                    source[id] == '(' || source[id] == ')' ||
                    source[id] == '"')
                    return 0;
            } else {
                size_t next = i + 1;
                while (next < length &&
                       (source[next] == ' ' || source[next] == '\t' ||
                        source[next] == '\r' || source[next] == '\n'))
                    next++;
                /* Annotation syntax is deliberately `(@id`, not `( @id`. */
                if (next > i + 1 && next < length && source[next] == '@')
                    return 0;
            }
            continue;
        }
        if (c == ')') {
            if (--paren_depth < 0) return 0;
            if (i + 1 < length && source[i + 1] == '@') return 0;
            continue;
        }
        if (c == '$' && i + 1 < length && source[i + 1] == '(')
            return 0;
        /* Empty identifier: '$' followed by whitespace, ')', or end-of-input */
        if (c == '$') {
            if (i + 1 >= length) return 0;
            uint8_t next = source[i + 1];
            if (next == ' ' || next == '\t' || next == '\r' || next == '\n' ||
                next == ')')
                return 0;
            /* Quoted identifier $"..." — check for empty or invalid content.
             * In decoded quote text, escape sequences are already resolved to
             * literal bytes.  Reject identifiers that contain literal control
             * characters (tab, newline, CR, etc.) or are empty. */
            if (next == '"') {
                size_t q = i + 2;
                int has_invalid = 0, content_len = 0;
                while (q < length && source[q] != '"') {
                    uint8_t ch = source[q];
                    if (ch < 0x20 || ch == 0x7f) has_invalid = 1;
                    if (ch == '\\' && q + 1 < length)
                        q += 2;
                    else
                        q++;
                    content_len++;
                }
                if (content_len == 0 || has_invalid) return 0;
            }
        }
        if (c >= 0x80)
            return 0; /* non-ASCII is only legal inside strings/comments */
        if (c != '"') continue;
        if (i > 0) {
            uint8_t previous = source[i - 1];
            if (previous != ' ' && previous != '\t' && previous != '\r' &&
                previous != '\n' && previous != '(' && previous != ')' &&
                previous != '$')
                return 0;
        }
        size_t begin = ++i;
        while (i < length && source[i] != '"') {
            if (source[i] == '\\' && i + 1 < length) i += 2;
            else i++;
        }
        if (i >= length) return 0;
        uint8_t *decoded = NULL;
        size_t decoded_length = 0, capacity = 0;
        int ok = raw_decode_string((const char *)source, begin, i,
                                   &decoded, &decoded_length, &capacity) &&
                 raw_valid_utf8(decoded, decoded_length);
        free(decoded);
        if (!ok) return 0;
        if (i + 1 < length) {
            uint8_t next = source[i + 1];
            if (next != ' ' && next != '\t' && next != '\r' && next != '\n' &&
                next != '(' && next != ')')
                return 0;
        }
    }
    return paren_depth == 0 && block_comment_depth == 0;
}

 typedef struct {
    char keyword[40];
    uint8_t has_child;
    uint8_t has_name;
} annotation_context;

static size_t annotation_skip_space(const char *source, size_t length,
                                    size_t at) {
    while (at < length && (source[at] == ' ' || source[at] == '\t' ||
           source[at] == '\r' || source[at] == '\n')) at++;
    return at;
}

static int annotation_token(const char *source, size_t length, size_t *at,
                            char *token, size_t token_size) {
    size_t start = annotation_skip_space(source, length, *at);
    size_t end = start;
    while (end < length && source[end] != ' ' && source[end] != '\t' &&
           source[end] != '\r' && source[end] != '\n' &&
           source[end] != '(' && source[end] != ')' && source[end] != '"')
        end++;
    if (end == start) return 0;
    size_t count = end - start;
    if (count >= token_size) count = token_size - 1;
    memcpy(token, source + start, count);
    token[count] = '\0';
    *at = end;
    return 1;
}

static int annotation_string(const char *source, size_t length, size_t *at,
                             int require_utf8, uint8_t *first,
                             size_t *decoded_size) {
    size_t cursor = annotation_skip_space(source, length, *at);
    if (cursor >= length || source[cursor] != '"') return 0;
    size_t begin = ++cursor;
    while (cursor < length && source[cursor] != '"') {
        if (source[cursor] == '\\' && cursor + 1 < length) cursor += 2;
        else cursor++;
    }
    if (cursor >= length) return -1;
    uint8_t *decoded = NULL;
    size_t size = 0, capacity = 0;
    int ok = raw_decode_string(source, begin, cursor, &decoded, &size,
                               &capacity);
    if (ok && require_utf8) ok = raw_valid_utf8(decoded, size);
    if (first) *first = size ? decoded[0] : 0;
    if (decoded_size) *decoded_size = size;
    free(decoded);
    *at = cursor + 1;
    return ok ? 1 : -1;
}

/* Return the offset immediately after the balanced annotation/command that
 * starts at an opening parenthesis.  Strings and nested comments are opaque. */
static size_t annotation_form_end(const char *source, size_t length,
                                  size_t start) {
    int depth = 0;
    for (size_t at = start; at < length; at++) {
        if (source[at] == '"') {
            for (at++; at < length; at++) {
                if (source[at] == '\\' && at + 1 < length) at++;
                else if (source[at] == '"') break;
            }
            continue;
        }
        if (source[at] == ';' && at + 1 < length && source[at + 1] == ';') {
            while (at < length && source[at] != '\n' && source[at] != '\r')
                at++;
            continue;
        }
        if (source[at] == '(' && at + 1 < length && source[at + 1] == ';') {
            int comments = 1;
            at += 2;
            while (at + 1 < length && comments) {
                if (source[at] == '(' && source[at + 1] == ';') {
                    comments++; at += 2;
                } else if (source[at] == ';' && source[at + 1] == ')') {
                    comments--; at += 2;
                } else at++;
            }
            if (at) at--;
            continue;
        }
        if (source[at] == '(') depth++;
        else if (source[at] == ')' && --depth == 0) return at + 1;
    }
    return length;
}

static size_t annotation_block_comment_end(const char *source, size_t length,
                                           size_t start) {
    int depth = 1;
    size_t at = start + 2;
    while (at + 1 < length && depth) {
        if (source[at] == '(' && source[at + 1] == ';') {
            depth++;
            at += 2;
        } else if (source[at] == ';' && source[at + 1] == ')') {
            depth--;
            at += 2;
        } else {
            at++;
        }
    }
    return at;
}

static int annotation_section_kind(const char *kind) {
    static const char *const kinds[] = {
        "custom", "type", "import", "func", "table", "memory", "tag",
        "global", "export", "start", "elem", "data_count", "code", "data"
    };
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++)
        if (strcmp(kind, kinds[i]) == 0) return 1;
    return 0;
}

static int validate_custom_annotation(const char *source, size_t start,
                                      size_t end, char *error,
                                      size_t error_size) {
    size_t at = start + 2;
    char id[64];
    if (!annotation_token(source, end, &at, id, sizeof(id))) return 1;
    if (strcmp(id, "custom") != 0) return 1;

    int string_status = annotation_string(source, end, &at, 1, NULL, NULL);
    if (string_status <= 0) {
        snprintf(error, error_size, "%s", string_status < 0 ?
                 "@custom annotation: malformed UTF-8 encoding" :
                 "@custom annotation: missing section name");
        return 0;
    }
    at = annotation_skip_space(source, end, at);
    if (at < end && source[at] == '(') {
        size_t placement_end = annotation_form_end(source, end, at);
        size_t field = at + 1;
        char direction[24], kind[24];
        if (!annotation_token(source, placement_end, &field, direction,
                              sizeof(direction)) ||
            (strcmp(direction, "before") != 0 &&
             strcmp(direction, "after") != 0)) {
            snprintf(error, error_size, "%s",
                     "@custom annotation: malformed placement");
            return 0;
        }
        if (!annotation_token(source, placement_end, &field, kind,
                              sizeof(kind)) || !annotation_section_kind(kind)) {
            snprintf(error, error_size, "%s",
                     "@custom annotation: malformed section kind");
            return 0;
        }
        field = annotation_skip_space(source, placement_end, field);
        if (field >= placement_end || source[field] != ')') {
            snprintf(error, error_size, "%s",
                     "@custom annotation: malformed placement");
            return 0;
        }
        at = placement_end;
    }
    for (;;) {
        at = annotation_skip_space(source, end, at);
        if (at >= end || source[at] == ')') return 1;
        if (annotation_string(source, end, &at, 0, NULL, NULL) != 1) {
            snprintf(error, error_size, "%s",
                     "@custom annotation: unexpected token");
            return 0;
        }
    }
}

static int annotation_next_is_branch_hint(const char *source, size_t length,
                                          size_t at, int *duplicate) {
    at = annotation_skip_space(source, length, at);
    *duplicate = 0;
    if (at < length && source[at] == '(') {
        size_t word = annotation_skip_space(source, length, at + 1);
        if (word < length && source[word] == '@') {
            static const char hint[] = "@metadata.code.branch_hint";
            if (word + sizeof(hint) - 1 <= length &&
                memcmp(source + word, hint, sizeof(hint) - 1) == 0)
                *duplicate = 1;
            return 0;
        }
        char token[16];
        return annotation_token(source, length, &word, token,
                                sizeof(token)) && strcmp(token, "if") == 0;
    }
    char token[16];
    return annotation_token(source, length, &at, token, sizeof(token)) &&
           strcmp(token, "if") == 0;
}

static int validate_custom_annotations(const char *source, size_t length,
                                       int inline_module, char *error,
                                       size_t error_size) {
    annotation_context contexts[256];
    int depth = inline_module ? 1 : 0;
    if (inline_module) {
        memset(&contexts[0], 0, sizeof(contexts[0]));
        snprintf(contexts[0].keyword, sizeof(contexts[0].keyword), "%s",
                 "module");
    }
    for (size_t at = 0; at < length; at++) {
        if (source[at] == '"') {
            for (at++; at < length; at++) {
                if (source[at] == '\\' && at + 1 < length) at++;
                else if (source[at] == '"') break;
            }
            continue;
        }
        if (source[at] == ';' && at + 1 < length && source[at + 1] == ';') {
            while (at < length && source[at] != '\n' && source[at] != '\r')
                at++;
            continue;
        }
        if (source[at] == '(' && at + 1 < length && source[at + 1] == ';') {
            size_t comment_end = annotation_block_comment_end(source, length,
                                                               at);
            at = comment_end ? comment_end - 1 : at;
            continue;
        }
        if (source[at] == '(' && at + 1 < length && source[at + 1] == '@') {
            size_t end = annotation_form_end(source, length, at);
            size_t id_at = at + 2;
            char id[64];
            if (!annotation_token(source, end, &id_at, id, sizeof(id))) {
                /* Generic annotations may use quoted identifiers.  Their
                 * lexical validity is checked by the raw WAT gate; only the
                 * three registered handlers below have semantic rules here. */
                at = end ? end - 1 : at;
                continue;
            }
            annotation_context *parent = depth ? &contexts[depth - 1] : NULL;
            if (strcmp(id, "custom") == 0) {
                if (!parent || strcmp(parent->keyword, "module") != 0) {
                    snprintf(error, error_size, "%s",
                             "misplaced @custom annotation");
                    return 0;
                }
                if (!validate_custom_annotation(source, at, end, error,
                                                error_size)) return 0;
                parent->has_child = 1;
            } else if (strcmp(id, "name") == 0) {
                if (!parent || (strcmp(parent->keyword, "module") != 0 &&
                    strcmp(parent->keyword, "func") != 0 &&
                    strcmp(parent->keyword, "tag") != 0) ||
                    parent->has_child) {
                    snprintf(error, error_size, "%s",
                             "misplaced @name annotation");
                    return 0;
                }
                if (parent->has_name) {
                    snprintf(error, error_size, "%s",
                             "@name annotation: multiple names");
                    return 0;
                }
                size_t value = id_at;
                size_t close;
                if (annotation_string(source, end, &value, 1, NULL, NULL) != 1 ||
                    (close = annotation_skip_space(source, end, value)) >= end ||
                    source[close] != ')') {
                    snprintf(error, error_size, "%s",
                             "@name annotation: string expected");
                    return 0;
                }
                parent->has_name = 1;
            } else if (strcmp(id, "metadata.code.branch_hint") == 0) {
                int in_function = 0;
                for (int i = depth - 1; i >= 0; i--)
                    if (strcmp(contexts[i].keyword, "func") == 0) {
                        in_function = 1;
                        break;
                    }
                if (!in_function) {
                    snprintf(error, error_size, "%s",
                             "@metadata.code.branch_hint annotation: not in a function");
                    return 0;
                }
                size_t value = id_at, decoded_size = 0;
                uint8_t hint = 0;
                size_t close;
                if (annotation_string(source, end, &value, 0, &hint,
                                      &decoded_size) != 1 ||
                    decoded_size != 1 || hint > 1 ||
                    (close = annotation_skip_space(source, end, value)) >= end ||
                    source[close] != ')') {
                    snprintf(error, error_size, "%s",
                             "@metadata.code.branch_hint annotation: invalid hint value");
                    return 0;
                }
                int duplicate = 0;
                if (!annotation_next_is_branch_hint(source, length, end,
                                                    &duplicate)) {
                    snprintf(error, error_size, "%s", duplicate ?
                             "@metadata.code.branch_hint annotation: duplicate annotation" :
                             "@metadata.code.branch_hint annotation: invalid target");
                    return 0;
                }
            }
            at = end ? end - 1 : at;
            continue;
        }
        if (source[at] == '(') {
            size_t keyword_at = annotation_skip_space(source, length, at + 1);
            char keyword[40] = {0};
            annotation_token(source, length, &keyword_at, keyword,
                             sizeof(keyword));
            if (strcmp(keyword, "assert_malformed_custom") == 0 ||
                strcmp(keyword, "assert_invalid_custom") == 0) {
                size_t end = annotation_form_end(source, length, at);
                at = end ? end - 1 : at;
                continue;
            }
            if (depth && contexts[depth - 1].keyword[0])
                contexts[depth - 1].has_child = 1;
            if (depth >= (int)(sizeof(contexts) / sizeof(contexts[0]))) {
                snprintf(error, error_size, "%s",
                         "annotation nesting limit exceeded");
                return 0;
            }
            memset(&contexts[depth], 0, sizeof(contexts[depth]));
            snprintf(contexts[depth].keyword,
                     sizeof(contexts[depth].keyword), "%s", keyword);
            depth++;
        } else if (source[at] == ')' && depth) {
            depth--;
        }
    }
    return 1;
}

static int queue_custom_assertion_errors(const char *source, size_t length,
                                         wast_script *script) {
    for (size_t at = 0; at < length; at++) {
        if (source[at] == '"') {
            for (at++; at < length; at++) {
                if (source[at] == '\\' && at + 1 < length) at++;
                else if (source[at] == '"') break;
            }
            continue;
        }
        if (source[at] != '(') continue;
        size_t keyword_at = annotation_skip_space(source, length, at + 1);
        char keyword[40];
        if (!annotation_token(source, length, &keyword_at, keyword,
                              sizeof(keyword)) ||
            (strcmp(keyword, "assert_malformed_custom") != 0 &&
             strcmp(keyword, "assert_invalid_custom") != 0))
            continue;
        size_t command_end = annotation_form_end(source, length, at);
        size_t module = keyword_at;
        while (module < command_end) {
            if (source[module] == '"') {
                for (module++; module < command_end; module++) {
                    if (source[module] == '\\' && module + 1 < command_end)
                        module++;
                    else if (source[module] == '"') break;
                }
            } else if (source[module] == '(') {
                size_t word_at = annotation_skip_space(source, command_end,
                                                       module + 1);
                char word[16];
                if (annotation_token(source, command_end, &word_at, word,
                                     sizeof(word)) &&
                    strcmp(word, "module") == 0)
                    break;
            }
            module++;
        }
        uint8_t invalid = 0;
        if (module < command_end) {
            size_t module_end = annotation_form_end(source, command_end,
                                                    module);
            char ignored[256] = {0};
            invalid = !validate_custom_annotations(
                source + module, module_end - module, 0, ignored,
                sizeof(ignored));
        }
        uint8_t *next = realloc(
            script->custom_assertion_errors,
            (size_t)(script->custom_assertion_count + 1));
        if (!next) return 0;
        script->custom_assertion_errors = next;
        script->custom_assertion_errors[script->custom_assertion_count++] =
            invalid;
        at = command_end ? command_end - 1 : at;
    }
    return 1;
}

int waste_wat_compile(const char *bytes, size_t length,
                      uint8_t **wasm_out, size_t *wasm_length,
                      char *error, size_t error_length) {
    wast_script script;
    char local_error[256] = {0};
    if (wasm_out) *wasm_out = NULL;
    if (wasm_length) *wasm_length = 0;
    if (error && error_length) error[0] = '\0';
    if (!bytes || !wasm_out || !wasm_length)
        return -1;
    if (!raw_quote_text_is_lexically_valid((const uint8_t *)bytes, length)) {
        if (error && error_length)
            snprintf(error, error_length, "malformed UTF-8 encoding");
        return -1;
    }
    if (wast_parse_bytes_mode(bytes, length, &script, 1) != 0) {
        snprintf(local_error, sizeof(local_error), "%s",
                 script.error[0] ? script.error : "WAT parse failed");
        wast_script_free(&script);
        if (error && error_length) snprintf(error, error_length, "%s", local_error);
        return -1;
    }
    if (script.group_count != 1 || script.assertion_count != 0) {
        snprintf(local_error, sizeof(local_error),
                 "WAT input must contain exactly one module");
        wast_script_free(&script);
        if (error && error_length) snprintf(error, error_length, "%s", local_error);
        return -1;
    }
    uint8_t *wasm = wast_encode_module(&script.groups[0].module,
                                       wasm_length, local_error);
    wast_script_free(&script);
    if (!wasm) {
        if (error && error_length) snprintf(error, error_length, "%s", local_error);
        *wasm_length = 0;
        return -1;
    }
    *wasm_out = wasm;
    return 0;
}

/* Flex/Bison generated API */
typedef void *yyscan_t;
typedef struct yy_buffer_state *YY_BUFFER_STATE;

extern int           yyparse(wast_script *script, wat_context *context,
                             void *scanner);
extern int           yylex_init_extra(wat_context *context,
                                      yyscan_t *scanner);
extern int           yylex_destroy(yyscan_t scanner);
extern YY_BUFFER_STATE yy_scan_bytes(const char *bytes, int len, yyscan_t scanner);
extern void          yy_switch_to_buffer(YY_BUFFER_STATE buf, yyscan_t scanner);
extern void          yy_delete_buffer(YY_BUFFER_STATE buf, yyscan_t scanner);

/* Check whether source text starts with a bare module field rather than a
   top-level command.  The result selects the grammar's dedicated inline
   module-fields entry token; the source itself is never wrapped or rewritten. */
int wast_source_is_inline_module(const char *src, size_t len) {
    size_t i = 0;
    for (;;) {
        while (i < len && (src[i] == ' ' || src[i] == '\t' ||
                           src[i] == '\r' || src[i] == '\n')) i++;
        if (i + 1 < len && src[i] == ';' && src[i + 1] == ';') {
            i += 2;
            while (i < len && src[i] != '\n' && src[i] != '\r') i++;
            continue;
        }
        if (i + 1 < len && src[i] == '(' && src[i + 1] == ';') {
            size_t end = annotation_block_comment_end(src, len, i);
            if (!end) return 0;
            i = end;
            continue;
        }
        break;
    }
    if (i >= len || src[i] != '(') return 0;
    i++;
    while (i < len && (src[i] == ' ' || src[i] == '\t' ||
                       src[i] == '\r' || src[i] == '\n')) i++;
    if (i < len && src[i] == '@') return 1;
    /* Module field keywords that are NOT top-level commands */
    static const char *field_kws[] = {
        "func", "memory", "global", "table", "data", "elem",
        "type", "import", "export", "start", "tag", "rec", "@custom",
        NULL
    };
    for (int k = 0; field_kws[k]; k++) {
        size_t klen = strlen(field_kws[k]);
        if (i + klen <= len && memcmp(src + i, field_kws[k], klen) == 0) {
            /* Ensure the keyword is followed by a delimiter */
            if (i + klen >= len || src[i + klen] == ' ' || src[i + klen] == '\t' ||
                src[i + klen] == '\r' || src[i + klen] == '\n' ||
                src[i + klen] == ')' || src[i + klen] == '(')
                return 1;
        }
    }
    return 0;
}

static int wast_parse_bytes_mode(const char *bytes, size_t length,
                                 wast_script *script, int strict_wat_mode) {
    memset(script, 0, sizeof(*script));
    script->strict_wat_mode = strict_wat_mode;

    if (!queue_custom_assertion_errors(bytes, length, script)) {
        snprintf(script->error, sizeof(script->error),
                 "out of memory retaining custom assertion metadata");
        return -1;
    }

    int inline_mod = wast_source_is_inline_module(bytes, length);
    char annotation_error[256] = {0};
    if (!validate_custom_annotations(bytes, length, inline_mod,
                                     annotation_error,
                                     sizeof(annotation_error))) {
        snprintf(script->error, sizeof(script->error), "%s",
                 annotation_error);
        return -1;
    }

    wat_context *context = wast_builder_context_create();
    if (!context) {
        snprintf(script->error, sizeof(script->error), "out of memory");
        return -1;
    }
    context->inline_module = inline_mod;
    context->strict_wat_mode = strict_wat_mode;
    script->parse_context = context;
    yyscan_t scanner = NULL;
    if (yylex_init_extra(context, &scanner) != 0) {
        script->parse_context = NULL;
        wast_builder_context_destroy(context);
        snprintf(script->error, sizeof(script->error),
                 "could not initialize lexer");
        return -1;
    }
    YY_BUFFER_STATE buf = yy_scan_bytes(bytes, (int)length, scanner);
    if (!buf) {
        yylex_destroy(scanner);
        script->parse_context = NULL;
        wast_builder_context_destroy(context);
        snprintf(script->error, sizeof(script->error),
                 "could not allocate lexer buffer");
        return -1;
    }
    yy_switch_to_buffer(buf, scanner);
    int rc = yyparse(script, context, scanner);
    yy_delete_buffer(buf, scanner);
    yylex_destroy(scanner);
    script->parse_context = NULL;
    wast_builder_context_destroy(context);
    if (rc != 0 && script->error[0] == '\0') snprintf(script->error, sizeof(script->error), "parse failed");
    return (rc != 0 || script->error[0] != '\0') ? -1 : 0;
}

int wast_parse_bytes(const char *bytes, size_t length, wast_script *script) {
    return wast_parse_bytes_mode(bytes, length, script, 0);
}

int wast_parse_file(const char *path, wast_script *script) {
    memset(script, 0, sizeof(*script));

    /* Read entire file into memory */
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(script->error, sizeof(script->error), "cannot open file: %s", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize < 0) {
        fclose(f);
        snprintf(script->error, sizeof(script->error), "cannot stat file: %s", path);
        return -1;
    }
    char *source = (char *)malloc((size_t)fsize + 2);
    if (!source) {
        fclose(f);
        snprintf(script->error, sizeof(script->error), "out of memory");
        return -1;
    }
    size_t nread = fread(source, 1, (size_t)fsize, f);
    fclose(f);
    /* Flex's yy_scan_bytes needs 2 null bytes at the end of the buffer */
    source[nread]   = '\0';
    source[nread+1] = '\0';

    int rc = wast_parse_bytes(source, nread, script);
    free(source);
    return rc;
}

void wast_script_free(wast_script *script) {
    if (!script) return;
    for (int g = 0; g < script->group_count; g++) {
        wast_module *module = &script->groups[g].module;
        free(script->groups[g].raw_module.bytes);
        for (int d = 0; d < module->data_count; d++) {
            uint8_t *bytes = module->data[d].bytes;
            int seen = 0;
            for (int prior_d = 0; prior_d < d && !seen; prior_d++)
                if (module->data[prior_d].bytes == bytes) seen = 1;
            for (int prior = 0; prior < g && !seen; prior++) {
                wast_module *pm = &script->groups[prior].module;
                for (int i = 0; i < pm->data_count; i++)
                    if (pm->data[i].bytes == bytes) { seen = 1; break; }
            }
            if (!seen) free(bytes);
        }
        int funcs_seen = 0;
        for (int prior = 0; prior < g; prior++)
            if (script->groups[prior].module.funcs == module->funcs) { funcs_seen = 1; break; }
        if (!funcs_seen) free(module->funcs);
    }
    free(script->groups);
    free(script->assertions);
    free(script->custom_assertion_errors);
    script->groups = NULL;
    script->assertions = NULL;
    script->group_count = 0;
    script->group_capacity = 0;
    script->assertion_count = 0;
    script->assertion_capacity = 0;
    script->custom_assertion_errors = NULL;
    script->custom_assertion_count = 0;
    script->custom_assertion_cursor = 0;
}
