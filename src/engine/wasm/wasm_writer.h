#ifndef WASTE_WASM_WRITER_H
#define WASTE_WASM_WRITER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    int failed;
} wasm_writer;

void wasm_writer_init(wasm_writer *writer);
void wasm_writer_dispose(wasm_writer *writer);
uint8_t *wasm_writer_take(wasm_writer *writer, size_t *size_out);
int wasm_writer_write_u8(wasm_writer *writer, uint8_t value);
int wasm_writer_write_bytes(wasm_writer *writer, const void *bytes,
                            size_t size);
int wasm_writer_write_u32(wasm_writer *writer, uint32_t value);
int wasm_writer_write_u64(wasm_writer *writer, uint64_t value);
int wasm_writer_write_i32(wasm_writer *writer, int32_t value);
int wasm_writer_write_i64(wasm_writer *writer, int64_t value);
int wasm_writer_write_s33(wasm_writer *writer, int64_t value);

#endif
