#ifndef WASTE_WASM_READER_H
#define WASTE_WASM_READER_H

#include "leb.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *start;
    const uint8_t *cursor;
    const uint8_t *end;
    wasm_leb_status leb_status;
} wasm_reader;

void wasm_reader_init(wasm_reader *reader, const uint8_t *bytes, size_t size);
size_t wasm_reader_offset(const wasm_reader *reader);
size_t wasm_reader_remaining(const wasm_reader *reader);
int wasm_reader_read_u8(wasm_reader *reader, uint8_t *out);
int wasm_reader_read_bytes(wasm_reader *reader, size_t size,
                           const uint8_t **out);
int wasm_reader_read_subreader(wasm_reader *reader, size_t size,
                               wasm_reader *out);
int wasm_reader_read_u32(wasm_reader *reader, uint32_t *out);
int wasm_reader_read_u64(wasm_reader *reader, uint64_t *out);
int wasm_reader_read_i32(wasm_reader *reader, int32_t *out);
int wasm_reader_read_i64(wasm_reader *reader, int64_t *out);
int wasm_reader_read_s33(wasm_reader *reader, int64_t *out);

#endif
