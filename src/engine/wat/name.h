#ifndef WASTE_TEXT_WAT_NAME_H
#define WASTE_TEXT_WAT_NAME_H

#include <stddef.h>
#include <stdint.h>

/* Internal names are C strings. Escape the two bytes used by this encoding so
 * arbitrary WebAssembly name bytes, including NUL, remain injective. */
static inline int wast_name_from_bytes(const uint8_t *bytes, size_t length,
                                       char *out, size_t capacity) {
    size_t written = 0;
    if (!out || capacity == 0) return 0;
    for (size_t i = 0; i < length; i++) {
        uint8_t byte = bytes[i];
        if (byte == 0 || byte == 1) {
            if (capacity - written < 3) return 0;
            out[written++] = 1;
            out[written++] = byte == 0 ? '0' : '1';
        } else {
            if (written >= capacity - 1) return 0;
            out[written++] = (char)byte;
        }
    }
    out[written] = '\0';
    return 1;
}

static inline int wast_name_to_bytes(const char *name, uint8_t *out,
                                     size_t capacity, size_t *length) {
    size_t written = 0;
    if (!name || !out || !length) return 0;
    for (size_t i = 0; name[i]; i++) {
        uint8_t byte = (uint8_t)name[i];
        if (byte == 1) {
            char tag = name[++i];
            if (tag != '0' && tag != '1') return 0;
            byte = tag == '0' ? 0 : 1;
        }
        if (written >= capacity) return 0;
        out[written++] = byte;
    }
    *length = written;
    return 1;
}

#endif
