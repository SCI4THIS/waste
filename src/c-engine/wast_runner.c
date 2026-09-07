#include "wast_runner.h"
#include "wast_types.h"
#include "waste_exec.h"
#include "wast_encode.h"

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

static int raw_number_digit(uint8_t c, int base) {
    int digit = raw_hex_digit(c);
    return digit >= 0 && digit < base ? digit : -1;
}

/* Consume one WebAssembly digit sequence.  Unlike libc's number readers,
 * the text format permits one underscore only between adjacent digits. */
static int raw_digit_sequence(const uint8_t *token, size_t length,
                              size_t *at, int base, int required) {
    int count = 0;
    while (*at < length && raw_number_digit(token[*at], base) >= 0) {
        (*at)++;
        count++;
        if (*at < length && token[*at] == '_') {
            if (*at + 1 >= length ||
                raw_number_digit(token[*at + 1], base) < 0)
                return 0;
            (*at)++;
        }
    }
    return !required || count > 0;
}

static int raw_integer_literal_valid(const uint8_t *token, size_t length,
                                     int bits) {
    size_t at = 0;
    int negative = 0;
    if (at < length && (token[at] == '+' || token[at] == '-'))
        negative = token[at++] == '-';
    int base = 10;
    if (at + 1 < length && token[at] == '0' &&
        (token[at + 1] == 'x' || token[at + 1] == 'X')) {
        base = 16;
        at += 2;
    }
    size_t digits = at;
    if (!raw_digit_sequence(token, length, &at, base, 1) || at != length)
        return 0;

    uint64_t limit = bits == 32 ?
        (negative ? UINT64_C(0x80000000) : UINT64_C(0xffffffff)) :
        (negative ? UINT64_C(0x8000000000000000) : UINT64_MAX);
    uint64_t value = 0;
    for (at = digits; at < length; at++) {
        if (token[at] == '_') continue;
        unsigned digit = (unsigned)raw_number_digit(token[at], base);
        if (value > (limit - digit) / (unsigned)base) return 0;
        value = value * (unsigned)base + digit;
    }
    return 1;
}

static int raw_integer_syntax_valid(const uint8_t *token, size_t length,
                                    int *hexadecimal) {
    size_t at = 0;
    if (at < length && (token[at] == '+' || token[at] == '-')) at++;
    int base = 10;
    if (at + 1 < length && token[at] == '0' &&
        (token[at + 1] == 'x' || token[at + 1] == 'X')) {
        base = 16;
        at += 2;
    }
    if (hexadecimal) *hexadecimal = base == 16;
    return raw_digit_sequence(token, length, &at, base, 1) && at == length;
}

static int raw_nan_literal_valid(const uint8_t *token, size_t length,
                                 int bits) {
    size_t at = 0;
    if (at < length && (token[at] == '+' || token[at] == '-')) at++;
    static const char prefix[] = "nan:0x";
    if (length - at < sizeof(prefix) - 1 ||
        memcmp(token + at, prefix, sizeof(prefix) - 1) != 0)
        return 0;
    at += sizeof(prefix) - 1;
    size_t digits = at;
    if (!raw_digit_sequence(token, length, &at, 16, 1) || at != length)
        return 0;
    uint64_t limit = bits == 32 ? UINT64_C(0x7fffff) :
                                  UINT64_C(0xfffffffffffff);
    uint64_t value = 0;
    for (at = digits; at < length; at++) {
        if (token[at] == '_') continue;
        unsigned digit = (unsigned)raw_number_digit(token[at], 16);
        if (value > (limit - digit) / 16u) return 0;
        value = value * 16u + digit;
    }
    return value != 0;
}

static int raw_float_syntax_valid(const uint8_t *token, size_t length) {
    size_t at = 0;
    if (at < length && (token[at] == '+' || token[at] == '-')) at++;
    int base = 10;
    char exponent = 'e';
    if (at + 1 < length && token[at] == '0' &&
        (token[at + 1] == 'x' || token[at + 1] == 'X')) {
        base = 16;
        exponent = 'p';
        at += 2;
    }
    if (!raw_digit_sequence(token, length, &at, base, 1)) return 0;
    if (at < length && token[at] == '.') {
        at++;
        if (!raw_digit_sequence(token, length, &at, base, 0)) return 0;
    }
    if (at < length && (token[at] | 0x20u) == (uint8_t)exponent) {
        at++;
        if (at < length && (token[at] == '+' || token[at] == '-')) at++;
        if (!raw_digit_sequence(token, length, &at, 10, 1)) return 0;
    }
    return at == length;
}

static int raw_float_literal_valid(const uint8_t *token, size_t length,
                                   int bits) {
    if ((length == 3 && memcmp(token, "inf", 3) == 0) ||
        (length == 4 && (token[0] == '+' || token[0] == '-') &&
         memcmp(token + 1, "inf", 3) == 0) ||
        (length == 3 && memcmp(token, "nan", 3) == 0) ||
        (length == 4 && (token[0] == '+' || token[0] == '-') &&
         memcmp(token + 1, "nan", 3) == 0))
        return 1;
    if ((length >= 6 && memcmp(token, "nan:0x", 6) == 0) ||
        (length >= 7 && (token[0] == '+' || token[0] == '-') &&
         memcmp(token + 1, "nan:0x", 6) == 0))
        return raw_nan_literal_valid(token, length, bits);
    if (!raw_float_syntax_valid(token, length)) return 0;

    char *clean = malloc(length + 1);
    if (!clean) return 0;
    size_t out = 0;
    for (size_t i = 0; i < length; i++)
        if (token[i] != '_') clean[out++] = (char)token[i];
    clean[out] = '\0';
    char *end = NULL;
    double value = strtod(clean, &end);
    int valid = end && *end == '\0' && !isinf(value);
    if (valid && bits == 32) {
        float narrowed = (float)value;
        valid = !isinf(narrowed);
    }
    free(clean);
    return valid;
}

static int raw_token_delimiter(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '(' || c == ')';
}

/* The grammar intentionally carries broad atom tokens.  Validate literal
 * spelling and range in this C preprocessing pass, where the surrounding
 * const operator supplies the required i32/i64/f32/f64 context. */
static int raw_const_literals_are_valid(const uint8_t *source,
                                        size_t length) {
    static const struct { const char *name; int bits; int floating; } ops[] = {
        {"i32.const", 32, 0}, {"i64.const", 64, 0},
        {"f32.const", 32, 1}, {"f64.const", 64, 1}
    };
    int block_depth = 0;
    int line_comment = 0;
    for (size_t i = 0; i < length; i++) {
        if (line_comment) {
            if (source[i] == '\n') line_comment = 0;
            continue;
        }
        if (block_depth) {
            if (source[i] == '(' && i + 1 < length && source[i + 1] == ';') {
                block_depth++; i++;
            } else if (source[i] == ';' && i + 1 < length &&
                       source[i + 1] == ')') {
                block_depth--; i++;
            }
            continue;
        }
        if (source[i] == ';' && i + 1 < length && source[i + 1] == ';') {
            line_comment = 1; i++; continue;
        }
        if (source[i] == '(' && i + 1 < length && source[i + 1] == ';') {
            block_depth = 1; i++; continue;
        }
        if (source[i] == '"') {
            for (i++; i < length && source[i] != '"'; i++)
                if (source[i] == '\\' && i + 1 < length) i++;
            continue;
        }
        for (size_t op = 0; op < sizeof(ops) / sizeof(ops[0]); op++) {
            size_t name_length = strlen(ops[op].name);
            if (i + name_length > length ||
                memcmp(source + i, ops[op].name, name_length) != 0 ||
                (i > 0 && !raw_token_delimiter(source[i - 1])) ||
                (i + name_length < length &&
                 !raw_token_delimiter(source[i + name_length])))
                continue;
            size_t begin = i + name_length;
            while (begin < length && (source[begin] == ' ' ||
                   source[begin] == '\t' || source[begin] == '\r' ||
                   source[begin] == '\n')) begin++;
            size_t end = begin;
            while (end < length && !raw_token_delimiter(source[end])) end++;
            if (begin == end) break; /* the parser diagnoses a missing literal */
            int valid = ops[op].floating ?
                raw_float_literal_valid(source + begin, end - begin,
                                        ops[op].bits) :
                raw_integer_literal_valid(source + begin, end - begin,
                                          ops[op].bits);
            if (!valid) return 0;
            i = end - 1;
            break;
        }
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
    if (!raw_valid_utf8(source, length) ||
        !raw_const_literals_are_valid(source, length)) return 0;
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
            /* An annotation identifier immediately follows the '@'.  An
             * empty id must not be normalized away by the annotation-strip
             * pass. */
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

static size_t raw_skip_space(const char *source, size_t length, size_t at) {
    while (at < length && (source[at] == ' ' || source[at] == '\t' ||
           source[at] == '\r' || source[at] == '\n')) at++;
    return at;
}

static int raw_word(const char *source, size_t length, size_t *at,
                    const char *word) {
    size_t n = strlen(word);
    if (*at + n > length || memcmp(source + *at, word, n) != 0) return 0;
    if (*at + n < length && source[*at + n] != ' ' &&
        source[*at + n] != '\t' && source[*at + n] != '\r' &&
        source[*at + n] != '\n' && source[*at + n] != ')') return 0;
    *at += n;
    return 1;
}

static int raw_queue_module(wast_script *script, wast_raw_module_kind kind,
                            uint8_t *bytes, size_t length) {
    int count = script->raw_module_count;
    wast_raw_module *next = realloc(
        script->raw_modules, (size_t)(count + 1) * sizeof(*next));
    if (!next) { free(bytes); return 0; }
    script->raw_modules = next;
    script->raw_modules[count] = (wast_raw_module){kind, bytes, length};
    script->raw_module_count++;
    return 1;
}

/* Retain binary and quoted module payloads before the ordinary preprocessing
 * pass removes comments and before STRING's C representation loses embedded
 * NUL bytes.  The generated grammar consumes these payloads in source order. */
static int collect_raw_modules(const char *source, size_t length,
                               wast_script *script) {
    for (size_t i = 0; i < length; i++) {
        if (source[i] == '"') {
            for (i++; i < length; i++) {
                if (source[i] == '\\' && i + 1 < length) i++;
                else if (source[i] == '"') break;
            }
            continue;
        }
        if (source[i] == ';' && i + 1 < length && source[i + 1] == ';') {
            while (i < length && source[i] != '\n') i++;
            continue;
        }
        if (source[i] != '(') continue;
        if (i + 1 < length && source[i + 1] == ';') {
            int depth = 1;
            i += 2;
            while (i + 1 < length && depth) {
                if (source[i] == '(' && source[i + 1] == ';') { depth++; i += 2; }
                else if (source[i] == ';' && source[i + 1] == ')') { depth--; i += 2; }
                else i++;
            }
            if (i) i--;
            continue;
        }

        size_t at = raw_skip_space(source, length, i + 1);
        if (!raw_word(source, length, &at, "module")) continue;
        at = raw_skip_space(source, length, at);
        if (at < length && source[at] == '$') {
            while (at < length && source[at] != ' ' && source[at] != '\t' &&
                   source[at] != '\r' && source[at] != '\n' &&
                   source[at] != ')') at++;
            at = raw_skip_space(source, length, at);
        }
        wast_raw_module_kind kind = WAST_RAW_NONE;
        size_t after_kind = at;
        if (raw_word(source, length, &after_kind, "binary"))
            kind = WAST_RAW_BINARY;
        else if (raw_word(source, length, &after_kind, "quote"))
            kind = WAST_RAW_QUOTE;
        if (kind == WAST_RAW_NONE) continue;

        uint8_t *bytes = NULL;
        size_t payload_length = 0, capacity = 0;
        int depth = 1;
        size_t cursor = after_kind;
        while (cursor < length && depth) {
            if (source[cursor] == ';' && cursor + 1 < length &&
                source[cursor + 1] == ';') {
                while (cursor < length && source[cursor] != '\n' && source[cursor] != '\r') cursor++;
            } else if (source[cursor] == '(' && cursor + 1 < length &&
                       source[cursor + 1] == ';') {
                int comment_depth = 1;
                cursor += 2;
                while (cursor + 1 < length && comment_depth) {
                    if (source[cursor] == '(' && source[cursor + 1] == ';') {
                        comment_depth++; cursor += 2;
                    } else if (source[cursor] == ';' && source[cursor + 1] == ')') {
                        comment_depth--; cursor += 2;
                    } else cursor++;
                }
            } else if (source[cursor] == '"') {
                size_t begin = ++cursor;
                while (cursor < length && source[cursor] != '"') {
                    if (source[cursor] == '\\' && cursor + 1 < length) cursor += 2;
                    else cursor++;
                }
                if (!raw_decode_string(source, begin, cursor, &bytes,
                                       &payload_length, &capacity)) {
                    free(bytes);
                    return 0;
                }
                if (cursor < length) cursor++;
            } else if (source[cursor] == '(') {
                depth++; cursor++;
            } else if (source[cursor] == ')') {
                depth--; cursor++;
            } else cursor++;
        }
        if (!raw_queue_module(script, kind, bytes, payload_length)) return 0;
    }
    return 1;
}

/* Remove comments and annotation forms before handing text to the generated
 * parser.  The official suite uses nested block comments and nested
 * annotations; Flex's regular-expression rules cannot represent either
 * construct safely.  Newlines are retained so diagnostics keep their source
 * line numbers, while quoted strings are copied byte-for-byte. */
static size_t strip_nonsemantic_forms(char *text, size_t length) {
    size_t r = 0, w = 0;
    while (r < length) {
        if (text[r] == '"') {
            size_t start = r++;
            while (r < length) {
                if (text[r] == '\\' && r + 1 < length) r += 2;
                else if (text[r++] == '"') break;
            }
            while (start < r) text[w++] = text[start++];
            continue;
        }
        if (r + 1 < length && text[r] == ';' && text[r + 1] == ';') {
            r += 2;
            while (r < length && text[r] != '\n' && text[r] != '\r') r++;
            continue;
        }
        if (r + 1 < length && text[r] == '(' &&
            (text[r + 1] == ';' || text[r + 1] == '@')) {
            int depth = 1;
            int annotation = text[r + 1] == '@';
            r += 2;
            while (r < length && depth) {
                if (annotation && r + 1 < length && text[r] == ';' && text[r + 1] == ';') {
                    r += 2;
                    while (r < length && text[r] != '\n' && text[r] != '\r') r++;
                }
                else if (annotation && r + 1 < length && text[r] == '(' && text[r + 1] == ';') {
                    r += 2;
                    while (r + 1 < length && !(text[r] == ';' && text[r + 1] == ')')) r++;
                    if (r + 1 < length) r += 2;
                }
                else if (annotation && text[r] == '"') {
                    r++;
                    while (r < length) {
                        if (text[r] == '\\' && r + 1 < length) r += 2;
                        else if (text[r++] == '"') break;
                    }
                }
                else if (annotation && text[r] == '(') { depth++; r++; }
                else if (annotation && text[r] == ')') { depth--; r++; }
                else if (!annotation && r + 1 < length && text[r] == '(' && text[r + 1] == ';') { depth++; r += 2; }
                else if (!annotation && r + 1 < length && text[r] == ';' && text[r + 1] == ')') { depth--; r += 2; }
                else { if (text[r] == '\n') text[w++] = '\n'; r++; }
            }
            continue;
        }
        text[w++] = text[r++];
    }
    text[w] = '\0';
    text[w + 1] = '\0';
    return w;
}

/* Flex must classify the same integer token for i32.const and f32.const, but
 * only the latter may need more precision than int64_t can retain.  Rewrite
 * integer-spelled float operands into equivalent float spellings before
 * lexing (`123` -> `123.0`, `0x123` -> `0x123p0`).  This keeps the lexer
 * context-free while libc's correctly-rounded strtod sees the original full
 * significand. */
static char *normalize_float_integer_literals(const char *source,
                                              size_t length,
                                              size_t *output_length) {
    if (length > (SIZE_MAX - 64) / 8) return NULL;
    char *output = malloc(length * 8 + 64);
    if (!output) return NULL;
    size_t read = 0, written = 0;
    while (read < length) {
        if (source[read] == '"') {
            output[written++] = source[read++];
            while (read < length) {
                char c = source[read++];
                output[written++] = c;
                if (c == '\\' && read < length)
                    output[written++] = source[read++];
                else if (c == '"')
                    break;
            }
            continue;
        }
        const char *name = NULL;
        int is_f32 = 0;
        if (read + 9 <= length && memcmp(source + read, "f32.const", 9) == 0)
            name = "f32.const", is_f32 = 1;
        else if (read + 9 <= length &&
                 memcmp(source + read, "f64.const", 9) == 0)
            name = "f64.const";
        if (name && (read == 0 || raw_token_delimiter((uint8_t)source[read - 1])) &&
            (read + 9 == length ||
             raw_token_delimiter((uint8_t)source[read + 9]))) {
            size_t begin = read + 9;
            while (begin < length &&
                   (source[begin] == ' ' || source[begin] == '\t' ||
                    source[begin] == '\r' || source[begin] == '\n'))
                begin++;
            size_t end = begin;
            while (end < length &&
                   !raw_token_delimiter((uint8_t)source[end])) end++;
            int hexadecimal = 0;
            int integer_syntax = begin < end && raw_integer_syntax_valid(
                (const uint8_t *)source + begin, end - begin, &hexadecimal);
            int float_syntax = begin < end && raw_float_syntax_valid(
                (const uint8_t *)source + begin, end - begin);
            if (integer_syntax || float_syntax) {
                size_t token_length = end - begin;
                char *clean = malloc(token_length + 1);
                if (!clean) { free(output); return NULL; }
                size_t clean_length = 0;
                for (size_t i = begin; i < end; i++)
                    if (source[i] != '_') clean[clean_length++] = source[i];
                clean[clean_length] = '\0';
                char canonical[64];
                int canonical_length;
                if (is_f32) {
                    float value = strtof(clean, NULL);
                    canonical_length = snprintf(canonical, sizeof(canonical),
                                                "%a", (double)value);
                } else {
                    double value = strtod(clean, NULL);
                    canonical_length = snprintf(canonical, sizeof(canonical),
                                                "%a", value);
                }
                free(clean);
                if (canonical_length <= 0 ||
                    (size_t)canonical_length >= sizeof(canonical)) {
                    free(output);
                    return NULL;
                }
                size_t prefix_length = begin - read;
                memcpy(output + written, source + read, prefix_length);
                written += prefix_length;
                memcpy(output + written, canonical,
                       (size_t)canonical_length);
                written += (size_t)canonical_length;
                read = end;
                continue;
            }
        }
        output[written++] = source[read++];
    }
    output[written] = '\0';
    output[written + 1] = '\0';
    *output_length = written;
    return output;
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

extern int           yyparse(wast_script *script, void *scanner);
extern int           yydebug;
extern int           yylex_init_extra(wast_lex_state *state, yyscan_t *scanner);
extern int           yylex_destroy(yyscan_t scanner);
extern YY_BUFFER_STATE yy_scan_bytes(const char *bytes, int len, yyscan_t scanner);
extern void          yy_switch_to_buffer(YY_BUFFER_STATE buf, yyscan_t scanner);
extern void          yy_delete_buffer(YY_BUFFER_STATE buf, yyscan_t scanner);

/* Check whether source text starts with a bare module field rather than
   a top-level command.  If so, it is an "inline module" and we need to
   wrap the entire content in (module ...) before parsing. */
static int is_inline_module(const char *src, size_t len) {
    size_t i = 0;
    while (i < len && (src[i] == ' ' || src[i] == '\t' ||
                       src[i] == '\r' || src[i] == '\n')) i++;
    if (i >= len || src[i] != '(') return 0;
    i++;
    while (i < len && (src[i] == ' ' || src[i] == '\t' ||
                       src[i] == '\r' || src[i] == '\n')) i++;
    /* Module field keywords that are NOT top-level commands */
    static const char *field_kws[] = {
        "func", "memory", "global", "table", "data", "elem",
        "type", "import", "export", "start", "tag", "rec", NULL
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

    if (!collect_raw_modules(bytes, length, script)) {
        snprintf(script->error, sizeof(script->error),
                 "out of memory retaining quoted module payload");
        return -1;
    }

#ifndef WASTE_FREESTANDING
    yydebug = getenv("WAST_YYDEBUG") != NULL;
#else
    yydebug = 0;
#endif

    /* Detect inline module sugar: bare module fields without (module ...) wrapper */
    int inline_mod = is_inline_module(bytes, length);
    size_t alloc_len = length + (inline_mod ? 10 : 0) + 2; /* "(module " + ")" + 2 NULs */

    char *source = (char *)malloc(alloc_len);
    if (!source) { snprintf(script->error, sizeof(script->error), "out of memory"); return -1; }
    if (inline_mod) {
        memcpy(source, "(module ", 8);
        memcpy(source + 8, bytes, length);
        source[8 + length] = ')';
        length = length + 9;
    } else {
        memcpy(source, bytes, length);
    }
    size_t nread = strip_nonsemantic_forms(source, length);
    source[nread] = '\0'; source[nread + 1] = '\0';
    size_t normalized_length = 0;
    char *normalized = normalize_float_integer_literals(
        source, nread, &normalized_length);
    if (!normalized) {
        free(source);
        snprintf(script->error, sizeof(script->error), "out of memory");
        return -1;
    }
    free(source);
    source = normalized;
    nread = normalized_length;

    yyscan_t scanner;
    wast_lex_state lex_state = {.line = 1, .column = 1};
    yylex_init_extra(&lex_state, &scanner);
    YY_BUFFER_STATE buf = yy_scan_bytes(source, (int)nread, scanner);
    yy_switch_to_buffer(buf, scanner);
    int rc = yyparse(script, scanner);
    yy_delete_buffer(buf, scanner);
    yylex_destroy(scanner);
    for (size_t i = 0; i < lex_state.string_count; i++)
        free(lex_state.strings[i]);
    free(lex_state.strings);
    free(source);
    if (rc != 0 && script->error[0] == '\0') snprintf(script->error, sizeof(script->error), "parse failed");
    return (rc != 0 || script->error[0] != '\0') ? -1 : 0;
}

int wast_parse_bytes(const char *bytes, size_t length, wast_script *script) {
    return wast_parse_bytes_mode(bytes, length, script, 0);
}

#ifndef WASTE_FREESTANDING
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
    nread = strip_nonsemantic_forms(source, nread);
    /* Flex's yy_scan_bytes needs 2 null bytes at the end of the buffer */
    source[nread]   = '\0';
    source[nread+1] = '\0';

    int rc = wast_parse_bytes(source, nread, script);
    free(source);
    return rc;
}
#endif

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
    for (int i = 0; i < script->raw_module_count; i++)
        free(script->raw_modules[i].bytes);
    free(script->raw_modules);
    script->groups = NULL;
    script->assertions = NULL;
    script->group_count = 0;
    script->group_capacity = 0;
    script->assertion_count = 0;
    script->assertion_capacity = 0;
    script->raw_modules = NULL;
    script->raw_module_count = 0;
    script->raw_module_cursor = 0;
}

/* Compare two v128 values with NaN mode awareness.
 * nan_mode[i] in expected is:
 *   NAN_MATCH_EXACT      (0): compare byte i exactly
 *   NAN_MATCH_F32_CANON  (1): bytes i..i+3 are an f32 lane; match any canonical NaN
 *   NAN_MATCH_F32_ARITH  (2): bytes i..i+3 are an f32 lane; match any NaN
 *   NAN_MATCH_F64_CANON  (3): bytes i..i+7 are an f64 lane; match any canonical NaN
 *   NAN_MATCH_F64_ARITH  (4): bytes i..i+7 are an f64 lane; match any NaN
 * Only the first byte of each lane carries the mode; remaining bytes are EXACT.
 */
static int v128_matches(const wasm_value *actual, const wasm_value *expected) {
    int i = 0;
    while (i < 16) {
        uint8_t mode = expected->nan_mode[i];
        if (mode == NAN_MATCH_EXACT) {
            if (actual->v128.bytes[i] != expected->v128.bytes[i]) return 0;
            i++;
        } else if (mode == NAN_MATCH_F32_CANON || mode == NAN_MATCH_F32_ARITH) {
            uint32_t ab;
            memcpy(&ab, &actual->v128.bytes[i], 4);
            int is_nan = ((ab & 0x7F800000u) == 0x7F800000u) && (ab & 0x007FFFFFu);
            if (!is_nan) return 0;
            if (mode == NAN_MATCH_F32_CANON) {
                if ((ab & 0x007FFFFFu) != 0x00400000u) return 0;
            }
            i += 4;
        } else if (mode == NAN_MATCH_F64_CANON || mode == NAN_MATCH_F64_ARITH) {
            uint64_t ab;
            memcpy(&ab, &actual->v128.bytes[i], 8);
            int is_nan = ((ab & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) &&
                         (ab & 0x000FFFFFFFFFFFFFULL);
            if (!is_nan) return 0;
            if (mode == NAN_MATCH_F64_CANON) {
                if ((ab & 0x000FFFFFFFFFFFFFULL) != 0x0008000000000000ULL) return 0;
            }
            i += 8;
        } else {
            /* Unknown mode: exact */
            if (actual->v128.bytes[i] != expected->v128.bytes[i]) return 0;
            i++;
        }
    }
    return 1;
}

static int value_matches(const wasm_value *actual, const wasm_value *expected) {
    if (expected->nan_mode[0] == REF_MATCH_NULL)
        return actual->ref == UINT32_MAX &&
               ((unsigned)actual->type >= (unsigned)WASM_VALTYPE_FUNCREF ||
                WASM_VALTYPE_IS_TYPE_REF(actual->type));
    if (actual->ref == UINT32_MAX && expected->ref == UINT32_MAX) {
        if (actual->type == WASM_VALTYPE_NULLREF &&
            (expected->type == WASM_VALTYPE_ANYREF ||
             expected->type == WASM_VALTYPE_EQREF ||
             expected->type == WASM_VALTYPE_I31REF ||
             expected->type == WASM_VALTYPE_STRUCTREF ||
             expected->type == WASM_VALTYPE_ARRAYREF)) return 1;
        if (actual->type == WASM_VALTYPE_NULLFUNCREF &&
            (expected->type == WASM_VALTYPE_FUNCREF ||
             WASM_VALTYPE_IS_TYPE_REF(expected->type))) return 1;
        if (actual->type == WASM_VALTYPE_NULLEXNREF &&
            expected->type == WASM_VALTYPE_EXNREF) return 1;
        if (actual->type == WASM_VALTYPE_NULLEXTERNREF &&
            expected->type == WASM_VALTYPE_EXTERNREF) return 1;
    }
    /* (ref.func) pattern: any non-null funcref */
    if (expected->type == WASM_VALTYPE_FUNCREF_NONNULL && expected->ref == UINT32_MAX)
        return (actual->type == WASM_VALTYPE_FUNCREF ||
                actual->type == WASM_VALTYPE_FUNCREF_NONNULL ||
                WASM_VALTYPE_IS_TYPE_REF(actual->type))
               && actual->ref != UINT32_MAX;
    /* (ref.extern) pattern: any non-null externref */
    if (expected->type == WASM_VALTYPE_EXTERNREF_NONNULL && expected->ref == UINT32_MAX)
        return (actual->type == WASM_VALTYPE_EXTERNREF || actual->type == WASM_VALTYPE_EXTERNREF_NONNULL)
               && actual->ref != UINT32_MAX;
    /* A typed function reference is a subtype of funcref.  Assertions use
     * the source-level expected type, while execution retains the more precise
     * indexed type needed by call_ref. */
    if (expected->type == WASM_VALTYPE_FUNCREF &&
        WASM_VALTYPE_IS_TYPE_REF(actual->type))
        return actual->ref == expected->ref;
    if (actual->type != expected->type) {
        if (actual->type == WASM_VALTYPE_V128 && expected->type == WASM_VALTYPE_V128)
            return v128_matches(actual, expected);
        return 0;
    }
    switch (actual->type) {
        case WASM_VALTYPE_V128:
            return v128_matches(actual, expected);
        case WASM_VALTYPE_I32:
            return actual->i32 == expected->i32;
        case WASM_VALTYPE_I64:
            return actual->i64 == expected->i64;
        case WASM_VALTYPE_F32: {
            uint32_t ab, eb;
            memcpy(&ab, &actual->f32, 4);
            memcpy(&eb, &expected->f32, 4);
            uint8_t mode = expected->nan_mode[0];
            if (mode == NAN_MATCH_F32_CANON || mode == NAN_MATCH_F32_ARITH) {
                int is_nan = ((ab & 0x7F800000u) == 0x7F800000u) && (ab & 0x007FFFFFu);
                if (!is_nan) return 0;
                if (mode == NAN_MATCH_F32_CANON)
                    return (ab & 0x007FFFFFu) == 0x00400000u;
                return 1; /* arithmetic: any NaN (quiet bit presence not required by Wasm) */
            }
            return ab == eb;
        }
        case WASM_VALTYPE_F64: {
            uint64_t ab, eb;
            memcpy(&ab, &actual->f64, 8);
            memcpy(&eb, &expected->f64, 8);
            uint8_t mode = expected->nan_mode[0];
            if (mode == NAN_MATCH_F64_CANON || mode == NAN_MATCH_F64_ARITH) {
                int is_nan = ((ab & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) &&
                             (ab & 0x000FFFFFFFFFFFFFULL);
                if (!is_nan) return 0;
                if (mode == NAN_MATCH_F64_CANON)
                    return (ab & 0x000FFFFFFFFFFFFFULL) == 0x0008000000000000ULL;
                return 1;
            }
            return ab == eb;
        }
        case WASM_VALTYPE_FUNCREF:
        case WASM_VALTYPE_EXTERNREF:
        case WASM_VALTYPE_FUNCREF_NONNULL:
        case WASM_VALTYPE_EXTERNREF_NONNULL:
        case WASM_VALTYPE_ANYREF:
        case WASM_VALTYPE_EQREF:
        case WASM_VALTYPE_I31REF:
        case WASM_VALTYPE_STRUCTREF:
        case WASM_VALTYPE_ARRAYREF:
        case WASM_VALTYPE_ANYREF_NONNULL:
        case WASM_VALTYPE_EQREF_NONNULL:
        case WASM_VALTYPE_I31REF_NONNULL:
        case WASM_VALTYPE_STRUCTREF_NONNULL:
        case WASM_VALTYPE_ARRAYREF_NONNULL:
        case WASM_VALTYPE_EXNREF:
        case WASM_VALTYPE_EXNREF_NONNULL:
        case WASM_VALTYPE_NULLREF:
        case WASM_VALTYPE_NULLFUNCREF:
        case WASM_VALTYPE_NULLEXNREF:
        case WASM_VALTYPE_NULLEXTERNREF:
            return actual->ref == expected->ref;
    }
    return 0;
}

int wast_v128_matches_any(const wasm_value *actual,
                           const wasm_value alternatives[][WAST_MAX_RESULTS],
                           int alt_count, int result_count) {
    for (int i = 0; i < alt_count; i++) {
        int matches = 1;
        for (int result = 0; result < result_count; result++) {
            if (!value_matches(&actual[result], &alternatives[i][result])) {
                matches = 0;
                break;
            }
        }
        if (matches) return 1;
    }
    return 0;
}

exec_status wast_run_assertion(waste_exec_engine *engine,
                               const wast_assertion *assertion,
                               exec_error *error) {
    if (assertion->action_kind == WAST_ACTION_GET) {
        exec_global *global = NULL;
        exec_status status = exec_find_export_global(
            engine, assertion->func_name, &global, error);
        if (status != EXEC_OK) return status;
        if (assertion->alt_count == 0 ||
            wast_v128_matches_any(&global->value, assertion->alternatives,
                                  assertion->alt_count, 1))
            return EXEC_OK;
        if (error) {
            error->status = EXEC_ERROR_TRAP;
            snprintf(error->message, sizeof(error->message),
                     "global result mismatch for %s", assertion->func_name);
        }
        return EXEC_ERROR_TRAP;
    }
    uint32_t func_idx;
    exec_status st = exec_find_export(engine, assertion->func_name, &func_idx, error);
    if (st != EXEC_OK) return st;

    wasm_value results[WAST_MAX_RESULTS];
    int result_count = 0;
    st = exec_invoke(engine, func_idx,
                     assertion->args, assertion->arg_count,
                     results, &result_count, error);
    if (assertion->kind == WAST_ASSERT_TRAP ||
        assertion->kind == WAST_ASSERT_EXHAUSTION) {
        if (st == EXEC_ERROR_TRAP) {
            if (error) memset(error, 0, sizeof(*error));
            return EXEC_OK;
        }
        if (st == EXEC_OK && error) {
            error->status = EXEC_ERROR_TRAP;
            snprintf(error->message, sizeof(error->message),
                     "expected trap from %s", assertion->func_name);
        }
        return st == EXEC_OK ? EXEC_ERROR_TRAP : st;
    }
    if (st != EXEC_OK) return st;

    if (assertion->alt_count == 0) {
        return EXEC_OK;
    }

    if (result_count != assertion->result_count) {
        if (error) snprintf(error->message, sizeof(error->message), "result count mismatch");
        return EXEC_ERROR_TRAP;
    }

    if (wast_v128_matches_any(results, assertion->alternatives,
                               assertion->alt_count, assertion->result_count)) {
        return EXEC_OK;
    }

    if (error) {
        if (assertion->result_count == 1 &&
            results[0].type == WASM_VALTYPE_I32 &&
            assertion->alternatives[0][0].type == WASM_VALTYPE_I32)
            snprintf(error->message, sizeof(error->message),
                     "result mismatch for %s (actual %d, expected %d)",
                     assertion->func_name, results[0].i32,
                     assertion->alternatives[0][0].i32);
        else if (assertion->result_count == 1 &&
                 results[0].type == WASM_VALTYPE_F64 &&
                 assertion->alternatives[0][0].type == WASM_VALTYPE_F64) {
            uint64_t actual_bits, expected_bits;
            memcpy(&actual_bits, &results[0].f64, sizeof(actual_bits));
            memcpy(&expected_bits, &assertion->alternatives[0][0].f64,
                   sizeof(expected_bits));
            snprintf(error->message, sizeof(error->message),
                     "result mismatch for %s (actual 0x%016llx, expected 0x%016llx)",
                     assertion->func_name,
                     (unsigned long long)actual_bits,
                     (unsigned long long)expected_bits);
        }
        else
            snprintf(error->message, sizeof(error->message),
                     "result mismatch for %s", assertion->func_name);
    }
    return EXEC_ERROR_TRAP;
}
