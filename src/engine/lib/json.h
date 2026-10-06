#ifndef WASTE_JSON_H
#define WASTE_JSON_H
#include "../../config.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Bounded JSON views. No platform capabilities, retained source pointers or
 * independent WAST parser: execution inputs come from the manifest's paths. */
typedef struct { const char *start, *end; char type; } json_view;
typedef struct { const char *at, *end; } json_reader;

static inline void whitespace(json_reader *r) {
    while (r->at < r->end && (*r->at == ' ' || *r->at == '\t' ||
           *r->at == '\n' || *r->at == '\r')) r->at++;
}

static inline int hex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static inline int string_end(json_reader *r) {
    while (r->at < r->end) {
        unsigned char ch = (unsigned char)*r->at++;
        if (ch == '"') return 1;
        if (ch < 0x20) return 0;
        if (ch >= 0x80) {
            unsigned n = ch >= 0xc2 && ch <= 0xdf ? 1 :
                         ch >= 0xe0 && ch <= 0xef ? 2 :
                         ch >= 0xf0 && ch <= 0xf4 ? 3 : 0;
            if (!n || (size_t)(r->end - r->at) < n) return 0;
            unsigned char next = (unsigned char)*r->at;
            if ((ch == 0xe0 && next < 0xa0) || (ch == 0xed && next >= 0xa0) ||
                (ch == 0xf0 && next < 0x90) || (ch == 0xf4 && next >= 0x90)) return 0;
            for (unsigned i = 0; i < n; i++)
                if (((unsigned char)*r->at++ & 0xc0) != 0x80) return 0;
            continue;
        }
        if (ch != '\\') continue;
        if (r->at == r->end) return 0;
        ch = (unsigned char)*r->at++;
        if (ch == 'u') {
            if (r->end - r->at < 4) return 0;
            for (int i = 0; i < 4; i++) if (hex(*r->at++) < 0) return 0;
        } else if (ch != '"' && ch != '\\' && ch != '/' && ch != 'b' &&
                   ch != 'f' && ch != 'n' && ch != 'r' && ch != 't') return 0;
    }
    return 0;
}

static inline int digit(json_reader *r) {
    return r->at < r->end && *r->at >= '0' && *r->at <= '9';
}

static inline int value(json_reader *r, json_view *out, unsigned depth) {
    whitespace(r);
    if (r->at == r->end || depth > JSON_MAX_DEPTH) return 0;
    out->start = r->at;
    out->type = *r->at++;
    if (out->type == '"') {
        if (!string_end(r)) return 0;
    } else if (out->type == '{' || out->type == '[') {
        char close = out->type == '{' ? '}' : ']';
        whitespace(r);
        if (r->at == r->end) return 0;
        if (*r->at != close) for (;;) {
            json_view child;
            if (out->type == '{') {
                if (!value(r, &child, depth + 1) || child.type != '"') return 0;
                whitespace(r);
                if (r->at == r->end || *r->at++ != ':') return 0;
            }
            if (!value(r, &child, depth + 1)) return 0;
            whitespace(r);
            if (r->at == r->end) return 0;
            if (*r->at == close) break;
            if (*r->at++ != ',') return 0;
        }
        r->at++;
    } else {
        r->at = out->start;
        const char *literal = *r->at == 't' ? "true" : *r->at == 'f' ? "false" :
                              *r->at == 'n' ? "null" : NULL;
        if (literal) {
            size_t n = strlen(literal);
            if ((size_t)(r->end - r->at) < n || memcmp(r->at, literal, n)) return 0;
            r->at += n;
        } else {
            if (*r->at == '-') r->at++;
            if (!digit(r)) return 0;
            if (*r->at == '0') r->at++;
            else while (digit(r)) r->at++;
            if (r->at < r->end && *r->at == '.') {
                r->at++;
                if (!digit(r)) return 0;
                while (digit(r)) r->at++;
            }
            if (r->at < r->end && (*r->at == 'e' || *r->at == 'E')) {
                r->at++;
                if (r->at < r->end && (*r->at == '+' || *r->at == '-')) r->at++;
                if (!digit(r)) return 0;
                while (digit(r)) r->at++;
            }
        }
    }
    out->end = r->at;
    return 1;
}

static inline int text(json_view v, char *out, size_t capacity) {
    if (v.type != '"' || !capacity) return 0;
    size_t used = 0;
    const char *p = v.start + 1, *end = v.end - 1;
    while (p < end) {
        uint32_t ch = (unsigned char)*p++;
        int unicode = 0;
        if (ch == '\\') {
            ch = (unsigned char)*p++;
            if (ch == 'u') {
                unicode = 1;
                ch = 0;
                for (int i = 0; i < 4; i++) ch = ch * 16u + (unsigned)hex(*p++);
                if (ch >= 0xd800 && ch <= 0xdbff) {
                    if (end - p < 6 || p[0] != '\\' || p[1] != 'u') return 0;
                    p += 2;
                    uint32_t low = 0;
                    for (int i = 0; i < 4; i++) low = low * 16u + (unsigned)hex(*p++);
                    if (low < 0xdc00 || low > 0xdfff) return 0;
                    ch = 0x10000u + (ch - 0xd800u) * 1024u + low - 0xdc00u;
                } else if (ch >= 0xdc00 && ch <= 0xdfff) return 0;
            } else if (ch == 'b') ch = '\b';
            else if (ch == 'f') ch = '\f';
            else if (ch == 'n') ch = '\n';
            else if (ch == 'r') ch = '\r';
            else if (ch == 't') ch = '\t';
        }
        if (!ch) return 0;
        unsigned n = !unicode || ch < 128 ? 1 : ch < 2048 ? 2 : ch < 65536 ? 3 : 4;
        if (n >= capacity - used) return 0;
        if (n == 1) out[used++] = (char)ch;
        else {
            out[used++] = (char)((n == 2 ? 0xc0 : n == 3 ? 0xe0 : 0xf0) |
                                 (ch >> (6u * (n - 1u))));
            for (unsigned i = n - 1; i > 0; i--)
                out[used++] = (char)(0x80u | ((ch >> (6u * (i - 1u))) & 63u));
        }
    }
    out[used] = 0;
    return 1;
}

/* Missing/duplicate members and type mismatches are structured decode errors. */
static inline int member(json_view object, const char *name, json_view *out) {
    if (object.type != '{') return 0;
    json_reader r = {object.start + 1, object.end - 1};
    int found = 0;
    whitespace(&r);
    while (r.at < r.end) {
        json_view key, child;
        char decoded[128];
        if (!value(&r, &key, 0)) return 0;
        whitespace(&r);
        if (r.at == r.end || *r.at++ != ':' || !value(&r, &child, 0)) return 0;
        if (text(key, decoded, sizeof(decoded)) && !strcmp(decoded, name)) {
            if (found) return 0;
            *out = child;
            found = 1;
        }
        whitespace(&r);
        if (r.at < r.end && *r.at++ != ',') return 0;
        whitespace(&r);
    }
    return found;
}

static inline int field(json_view v, const char *key, char *out, size_t n) {
    json_view child;
    return member(v, key, &child) && text(child, out, n);
}

static inline int literal(json_view v, const char *expected) {
    return v.type != '"' && (size_t)(v.end - v.start) == strlen(expected) &&
           !memcmp(v.start, expected, strlen(expected));
}

static inline int boolean(json_view v, const char *key, int *out) {
    json_view child;
    if (!member(v, key, &child)) return 0;
    if (literal(child, "true")) *out = 1;
    else if (literal(child, "false")) *out = 0;
    else return 0;
    return 1;
}

#endif
