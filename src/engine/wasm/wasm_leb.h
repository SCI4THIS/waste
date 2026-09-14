#ifndef WASTE_WASM_LEB_H
#define WASTE_WASM_LEB_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    WASM_LEB_OK = 0,
    WASM_LEB_TRUNCATED,
    WASM_LEB_INVALID
} wasm_leb_status;

/* Readers commit cursor advancement only after a complete valid value. */
wasm_leb_status wasm_leb_read_u32(const uint8_t **cursor,
                                  const uint8_t *end, uint32_t *out);
wasm_leb_status wasm_leb_read_u64(const uint8_t **cursor,
                                  const uint8_t *end, uint64_t *out);
wasm_leb_status wasm_leb_read_i32(const uint8_t **cursor,
                                  const uint8_t *end, int32_t *out);
wasm_leb_status wasm_leb_read_i64(const uint8_t **cursor,
                                  const uint8_t *end, int64_t *out);
wasm_leb_status wasm_leb_read_s33(const uint8_t **cursor,
                                  const uint8_t *end, int64_t *out);

/* Writers always produce the shortest valid representation. */
size_t wasm_leb_encode_u32(uint8_t out[5], uint32_t value);
size_t wasm_leb_encode_u64(uint8_t out[10], uint64_t value);
size_t wasm_leb_encode_i32(uint8_t out[5], int32_t value);
size_t wasm_leb_encode_i64(uint8_t out[10], int64_t value);
size_t wasm_leb_encode_s33(uint8_t out[5], int64_t value);

#endif
