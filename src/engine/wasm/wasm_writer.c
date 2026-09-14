#include "wasm_writer.h"
#include "wasm_leb.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int reserve(wasm_writer *writer, size_t additional) {
    size_t needed;
    size_t capacity;
    uint8_t *data;
    if (!writer || writer->failed) return 0;
    if (additional > SIZE_MAX - writer->len) {
        writer->failed = 1;
        return 0;
    }
    needed = writer->len + additional;
    if (needed <= writer->cap) return 1;
    capacity = writer->cap ? writer->cap : 256;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    data = (uint8_t *)realloc(writer->data, capacity);
    if (!data) {
        writer->failed = 1;
        return 0;
    }
    writer->data = data;
    writer->cap = capacity;
    return 1;
}

void wasm_writer_init(wasm_writer *writer) {
    if (writer) memset(writer, 0, sizeof(*writer));
}

void wasm_writer_dispose(wasm_writer *writer) {
    if (!writer) return;
    free(writer->data);
    memset(writer, 0, sizeof(*writer));
}

uint8_t *wasm_writer_take(wasm_writer *writer, size_t *size_out) {
    uint8_t *data;
    if (!writer || writer->failed) return NULL;
    data = writer->data;
    if (size_out) *size_out = writer->len;
    writer->data = NULL;
    writer->len = 0;
    writer->cap = 0;
    return data;
}

int wasm_writer_write_u8(wasm_writer *writer, uint8_t value) {
    if (!reserve(writer, 1)) return 0;
    writer->data[writer->len++] = value;
    return 1;
}

int wasm_writer_write_bytes(wasm_writer *writer, const void *bytes,
                            size_t size) {
    if (size && !bytes) {
        if (writer) writer->failed = 1;
        return 0;
    }
    if (!reserve(writer, size)) return 0;
    if (size) memcpy(writer->data + writer->len, bytes, size);
    writer->len += size;
    return 1;
}

int wasm_writer_write_u32(wasm_writer *writer, uint32_t value) {
    uint8_t bytes[5];
    size_t length = wasm_leb_encode_u32(bytes, value);
    return wasm_writer_write_bytes(writer, bytes, length);
}

int wasm_writer_write_u64(wasm_writer *writer, uint64_t value) {
    uint8_t bytes[10];
    size_t length = wasm_leb_encode_u64(bytes, value);
    return wasm_writer_write_bytes(writer, bytes, length);
}

int wasm_writer_write_i32(wasm_writer *writer, int32_t value) {
    uint8_t bytes[5];
    size_t length = wasm_leb_encode_i32(bytes, value);
    return length && wasm_writer_write_bytes(writer, bytes, length);
}

int wasm_writer_write_i64(wasm_writer *writer, int64_t value) {
    uint8_t bytes[10];
    size_t length = wasm_leb_encode_i64(bytes, value);
    return length && wasm_writer_write_bytes(writer, bytes, length);
}

int wasm_writer_write_s33(wasm_writer *writer, int64_t value) {
    uint8_t bytes[5];
    size_t length = wasm_leb_encode_s33(bytes, value);
    if (!length) {
        if (writer) writer->failed = 1;
        return 0;
    }
    return wasm_writer_write_bytes(writer, bytes, length);
}
