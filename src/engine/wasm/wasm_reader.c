#include "wasm_reader.h"

void wasm_reader_init(wasm_reader *reader, const uint8_t *bytes, size_t size) {
    if (!reader) return;
    reader->start = bytes;
    reader->cursor = bytes;
    reader->end = bytes ? bytes + size : bytes;
    reader->leb_status = WASM_LEB_OK;
}

size_t wasm_reader_offset(const wasm_reader *reader) {
    if (!reader || !reader->start || !reader->cursor) return 0;
    return (size_t)(reader->cursor - reader->start);
}

size_t wasm_reader_remaining(const wasm_reader *reader) {
    if (!reader || !reader->cursor || !reader->end ||
        reader->cursor > reader->end)
        return 0;
    return (size_t)(reader->end - reader->cursor);
}

int wasm_reader_read_u8(wasm_reader *reader, uint8_t *out) {
    if (!reader || !out || !reader->cursor || !reader->end ||
        reader->cursor >= reader->end)
        return 0;
    *out = *reader->cursor++;
    return 1;
}

int wasm_reader_read_bytes(wasm_reader *reader, size_t size,
                           const uint8_t **out) {
    if (!reader || !out || !reader->cursor || !reader->end ||
        size > wasm_reader_remaining(reader))
        return 0;
    *out = reader->cursor;
    reader->cursor += size;
    return 1;
}

int wasm_reader_read_subreader(wasm_reader *reader, size_t size,
                               wasm_reader *out) {
    const uint8_t *bytes;
    if (!out || !wasm_reader_read_bytes(reader, size, &bytes)) return 0;
    out->start = reader->start;
    out->cursor = bytes;
    out->end = bytes + size;
    out->leb_status = WASM_LEB_OK;
    return 1;
}

int wasm_reader_read_u32(wasm_reader *reader, uint32_t *out) {
    if (!reader) return 0;
    reader->leb_status = wasm_leb_read_u32(&reader->cursor, reader->end, out);
    return reader->leb_status == WASM_LEB_OK;
}

int wasm_reader_read_u64(wasm_reader *reader, uint64_t *out) {
    if (!reader) return 0;
    reader->leb_status = wasm_leb_read_u64(&reader->cursor, reader->end, out);
    return reader->leb_status == WASM_LEB_OK;
}

int wasm_reader_read_i32(wasm_reader *reader, int32_t *out) {
    if (!reader) return 0;
    reader->leb_status = wasm_leb_read_i32(&reader->cursor, reader->end, out);
    return reader->leb_status == WASM_LEB_OK;
}

int wasm_reader_read_i64(wasm_reader *reader, int64_t *out) {
    if (!reader) return 0;
    reader->leb_status = wasm_leb_read_i64(&reader->cursor, reader->end, out);
    return reader->leb_status == WASM_LEB_OK;
}

int wasm_reader_read_s33(wasm_reader *reader, int64_t *out) {
    if (!reader) return 0;
    reader->leb_status = wasm_leb_read_s33(&reader->cursor, reader->end, out);
    return reader->leb_status == WASM_LEB_OK;
}
