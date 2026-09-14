#include "wasm_leb.h"

#include <limits.h>

static wasm_leb_status read_unsigned(const uint8_t **cursor,
                                     const uint8_t *end, unsigned bits,
                                     uint64_t *out) {
    const uint8_t *at;
    uint64_t value = 0;
    unsigned max_bytes = (bits + 6u) / 7u;

    if (!cursor || !*cursor || !end || !out) return WASM_LEB_INVALID;
    at = *cursor;
    for (unsigned i = 0; i < max_bytes; i++) {
        uint8_t byte;
        unsigned shift = i * 7u;
        if (at >= end) return WASM_LEB_TRUNCATED;
        byte = *at++;
        if (i + 1u == max_bytes) {
            unsigned used = bits - shift;
            uint8_t high_mask = (uint8_t)(0x7fu &
                ~((UINT32_C(1) << used) - 1u));
            if ((byte & 0x80u) || (byte & high_mask))
                return WASM_LEB_INVALID;
        }
        value |= (uint64_t)(byte & 0x7fu) << shift;
        if (!(byte & 0x80u)) {
            *cursor = at;
            *out = value;
            return WASM_LEB_OK;
        }
    }
    return WASM_LEB_INVALID;
}

static wasm_leb_status read_signed(const uint8_t **cursor,
                                   const uint8_t *end, unsigned bits,
                                   int64_t *out) {
    const uint8_t *at;
    uint64_t value = 0;
    unsigned max_bytes = (bits + 6u) / 7u;

    if (!cursor || !*cursor || !end || !out) return WASM_LEB_INVALID;
    at = *cursor;
    for (unsigned i = 0; i < max_bytes; i++) {
        uint8_t byte;
        unsigned shift = i * 7u;
        if (at >= end) return WASM_LEB_TRUNCATED;
        byte = *at++;
        if (i + 1u == max_bytes) {
            unsigned used = bits - shift;
            uint8_t low_mask = (uint8_t)((UINT32_C(1) << used) - 1u);
            uint8_t high_mask = (uint8_t)(0x7fu & ~low_mask);
            uint8_t sign_bit = (uint8_t)(UINT32_C(1) << (used - 1u));
            uint8_t expected = (byte & sign_bit) ? high_mask : 0;
            if ((byte & 0x80u) || (byte & high_mask) != expected)
                return WASM_LEB_INVALID;
        }
        value |= (uint64_t)(byte & 0x7fu) << shift;
        if (!(byte & 0x80u)) {
            unsigned encoded_bits = shift + 7u;
            if (encoded_bits < 64u && (byte & 0x40u))
                value |= UINT64_MAX << encoded_bits;
            if (bits < 64u) {
                uint64_t mask = (UINT64_C(1) << bits) - 1u;
                value &= mask;
                if (value & (UINT64_C(1) << (bits - 1u)))
                    value |= ~mask;
            }
            *cursor = at;
            *out = (int64_t)value;
            return WASM_LEB_OK;
        }
    }
    return WASM_LEB_INVALID;
}

wasm_leb_status wasm_leb_read_u32(const uint8_t **cursor,
                                  const uint8_t *end, uint32_t *out) {
    uint64_t value;
    wasm_leb_status status;
    if (!out) return WASM_LEB_INVALID;
    status = read_unsigned(cursor, end, 32, &value);
    if (status == WASM_LEB_OK) *out = (uint32_t)value;
    return status;
}

wasm_leb_status wasm_leb_read_u64(const uint8_t **cursor,
                                  const uint8_t *end, uint64_t *out) {
    return read_unsigned(cursor, end, 64, out);
}

wasm_leb_status wasm_leb_read_i32(const uint8_t **cursor,
                                  const uint8_t *end, int32_t *out) {
    int64_t value;
    wasm_leb_status status;
    if (!out) return WASM_LEB_INVALID;
    status = read_signed(cursor, end, 32, &value);
    if (status == WASM_LEB_OK) *out = (int32_t)value;
    return status;
}

wasm_leb_status wasm_leb_read_i64(const uint8_t **cursor,
                                  const uint8_t *end, int64_t *out) {
    return read_signed(cursor, end, 64, out);
}

wasm_leb_status wasm_leb_read_s33(const uint8_t **cursor,
                                  const uint8_t *end, int64_t *out) {
    return read_signed(cursor, end, 33, out);
}

size_t wasm_leb_encode_u32(uint8_t out[5], uint32_t value) {
    size_t length = 0;
    do {
        uint8_t byte = (uint8_t)(value & 0x7fu);
        value >>= 7;
        if (value) byte |= 0x80u;
        out[length++] = byte;
    } while (value);
    return length;
}

size_t wasm_leb_encode_u64(uint8_t out[10], uint64_t value) {
    size_t length = 0;
    do {
        uint8_t byte = (uint8_t)(value & 0x7fu);
        value >>= 7;
        if (value) byte |= 0x80u;
        out[length++] = byte;
    } while (value);
    return length;
}

static int64_t shift_signed_7(int64_t value) {
    if (value >= 0) return value / 128;
    return -1 - (int64_t)(~(uint64_t)value >> 7);
}

static size_t encode_signed(uint8_t *out, size_t capacity, int64_t value) {
    size_t length = 0;
    int more = 1;
    while (more) {
        uint8_t byte;
        int64_t next;
        if (length >= capacity) return 0;
        byte = (uint8_t)((uint64_t)value & 0x7fu);
        next = shift_signed_7(value);
        more = !((next == 0 && !(byte & 0x40u)) ||
                 (next == -1 && (byte & 0x40u)));
        if (more) byte |= 0x80u;
        out[length++] = byte;
        value = next;
    }
    return length;
}

size_t wasm_leb_encode_i32(uint8_t out[5], int32_t value) {
    return encode_signed(out, 5, value);
}

size_t wasm_leb_encode_i64(uint8_t out[10], int64_t value) {
    return encode_signed(out, 10, value);
}

size_t wasm_leb_encode_s33(uint8_t out[5], int64_t value) {
    if (value < -INT64_C(4294967296) || value > INT64_C(4294967295))
        return 0;
    return encode_signed(out, 5, value);
}
