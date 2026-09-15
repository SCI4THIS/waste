#include "decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WASM_DECODE_MAX_IMPORTS = 8192 };

static wasm_decode_status decode_fail(wasm_decode_error *error,
                                      wasm_decode_status status,
                                      size_t offset, const char *message) {
    if (error) {
        error->status = status;
        error->offset = offset;
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return status;
}

static int valid_utf8(const uint8_t *bytes, size_t length) {
    size_t i = 0;
    while (i < length) {
        uint8_t first = bytes[i++];
        if (first <= 0x7f) continue;
        if (first >= 0xc2 && first <= 0xdf) {
            if (i >= length || bytes[i] < 0x80 || bytes[i] > 0xbf) return 0;
            i++;
        } else if (first >= 0xe0 && first <= 0xef) {
            uint8_t second, third;
            if (i + 1 >= length) return 0;
            second = bytes[i];
            third = bytes[i + 1];
            if (third < 0x80 || third > 0xbf ||
                (first == 0xe0 && (second < 0xa0 || second > 0xbf)) ||
                (first == 0xed && (second < 0x80 || second > 0x9f)) ||
                (first != 0xe0 && first != 0xed &&
                 (second < 0x80 || second > 0xbf)))
                return 0;
            i += 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            uint8_t second, third, fourth;
            if (i + 2 >= length) return 0;
            second = bytes[i];
            third = bytes[i + 1];
            fourth = bytes[i + 2];
            if (third < 0x80 || third > 0xbf ||
                fourth < 0x80 || fourth > 0xbf ||
                (first == 0xf0 && (second < 0x90 || second > 0xbf)) ||
                (first == 0xf4 && (second < 0x80 || second > 0x8f)) ||
                (first != 0xf0 && first != 0xf4 &&
                 (second < 0x80 || second > 0xbf)))
                return 0;
            i += 3;
        } else {
            return 0;
        }
    }
    return 1;
}

int wasm_decode_byte_valtype(uint8_t byte, wasm_valtype *out) {
    switch (byte) {
        case 0x7f: *out = WASM_VALTYPE_I32; return 1;
        case 0x7e: *out = WASM_VALTYPE_I64; return 1;
        case 0x7d: *out = WASM_VALTYPE_F32; return 1;
        case 0x7c: *out = WASM_VALTYPE_F64; return 1;
        case 0x7b: *out = WASM_VALTYPE_V128; return 1;
        case 0x70: *out = WASM_VALTYPE_FUNCREF; return 1;
        case 0x6f: *out = WASM_VALTYPE_EXTERNREF; return 1;
        case 0x6e: *out = WASM_VALTYPE_ANYREF; return 1;
        case 0x6d: *out = WASM_VALTYPE_EQREF; return 1;
        case 0x6c: *out = WASM_VALTYPE_I31REF; return 1;
        case 0x6b: *out = WASM_VALTYPE_STRUCTREF; return 1;
        case 0x6a: *out = WASM_VALTYPE_ARRAYREF; return 1;
        case 0x71: *out = WASM_VALTYPE_NULLREF; return 1;
        case 0x73: *out = WASM_VALTYPE_NULLFUNCREF; return 1;
        case 0x74: *out = WASM_VALTYPE_NULLEXNREF; return 1;
        case 0x72: *out = WASM_VALTYPE_NULLEXTERNREF; return 1;
        case 0x69: *out = WASM_VALTYPE_EXNREF; return 1;
        default: return 0;
    }
}

int wasm_decode_valtype(wasm_reader *reader, wasm_valtype *out) {
    uint8_t byte;
    int32_t heap;
    if (!wasm_reader_read_u8(reader, &byte)) return 0;
    if (wasm_decode_byte_valtype(byte, out)) return 1;
    if (byte != 0x63 && byte != 0x64) return 0;
    if (!wasm_reader_read_i32(reader, &heap)) return 0;
    if (heap == -16)
        *out = byte == 0x63 ? WASM_VALTYPE_FUNCREF :
                              WASM_VALTYPE_FUNCREF_NONNULL;
    else if (heap == -17)
        *out = byte == 0x63 ? WASM_VALTYPE_EXTERNREF :
                              WASM_VALTYPE_EXTERNREF_NONNULL;
    else if (heap == -18)
        *out = byte == 0x63 ? WASM_VALTYPE_ANYREF :
                              WASM_VALTYPE_ANYREF_NONNULL;
    else if (heap == -19)
        *out = byte == 0x63 ? WASM_VALTYPE_EQREF :
                              WASM_VALTYPE_EQREF_NONNULL;
    else if (heap == -20)
        *out = byte == 0x63 ? WASM_VALTYPE_I31REF :
                              WASM_VALTYPE_I31REF_NONNULL;
    else if (heap == -21)
        *out = byte == 0x63 ? WASM_VALTYPE_STRUCTREF :
                              WASM_VALTYPE_STRUCTREF_NONNULL;
    else if (heap == -22)
        *out = byte == 0x63 ? WASM_VALTYPE_ARRAYREF :
                              WASM_VALTYPE_ARRAYREF_NONNULL;
    else if (heap == -23)
        *out = byte == 0x63 ? WASM_VALTYPE_EXNREF :
                              WASM_VALTYPE_EXNREF_NONNULL;
    else if (byte == 0x63 && heap == -15)
        *out = WASM_VALTYPE_NULLREF;
    else if (byte == 0x63 && heap == -14)
        *out = WASM_VALTYPE_NULLEXTERNREF;
    else if (byte == 0x63 && heap == -13)
        *out = WASM_VALTYPE_NULLFUNCREF;
    else if (byte == 0x63 && heap == -12)
        *out = WASM_VALTYPE_NULLEXNREF;
    else if (heap >= 0 && heap < WAST_MAX_TYPES)
        *out = (wasm_valtype)((byte == 0x63 ?
            WASM_VALTYPE_TYPE_REF_NULL_BASE : WASM_VALTYPE_TYPE_REF_BASE) +
            (uint32_t)heap);
    else
        return 0;
    return 1;
}

static int decode_name(wasm_reader *reader,
                       char out[WAST_MAX_EXPORT_NAME]) {
    uint32_t length;
    const uint8_t *bytes;
    if (!wasm_reader_read_u32(reader, &length) ||
        length >= WAST_MAX_EXPORT_NAME ||
        !wasm_reader_read_bytes(reader, length, &bytes) ||
        !valid_utf8(bytes, length))
        return 0;
    memcpy(out, bytes, length);
    out[length] = '\0';
    return 1;
}

static int decode_limits(wasm_reader *reader, wasm_import_limits *limits) {
    if (!wasm_reader_read_u32(reader, &limits->flags) ||
        !wasm_reader_read_u64(reader, &limits->minimum))
        return 0;
    limits->maximum = 0;
    return !(limits->flags & 1u) ||
           wasm_reader_read_u64(reader, &limits->maximum);
}

static int decode_import(wasm_reader *reader, wasm_import *import_) {
    uint8_t kind;
    if (!decode_name(reader, import_->module) ||
        !decode_name(reader, import_->name) ||
        !wasm_reader_read_u8(reader, &kind) || kind > WASM_IMPORT_TAG)
        return 0;
    import_->kind = (wasm_import_kind)kind;
    switch (import_->kind) {
        case WASM_IMPORT_FUNCTION:
            return wasm_reader_read_u32(
                reader, &import_->descriptor.function.type_index);
        case WASM_IMPORT_TABLE:
            return wasm_decode_valtype(
                       reader, &import_->descriptor.table.element_type) &&
                   decode_limits(reader, &import_->descriptor.table.limits);
        case WASM_IMPORT_MEMORY:
            return decode_limits(reader, &import_->descriptor.memory.limits);
        case WASM_IMPORT_GLOBAL:
            return wasm_decode_valtype(
                       reader, &import_->descriptor.global.value_type) &&
                   wasm_reader_read_u8(
                       reader, &import_->descriptor.global.mutable_);
        case WASM_IMPORT_TAG:
            return wasm_reader_read_u32(
                       reader, &import_->descriptor.tag.attribute) &&
                   wasm_reader_read_u32(
                       reader, &import_->descriptor.tag.type_index);
    }
    return 0;
}

static wasm_decode_status decode_import_section(wasm_reader *section,
                                                wasm_module *module,
                                                wasm_decode_error *error) {
    uint32_t count;
    if (!wasm_reader_read_u32(section, &count) ||
        count > WASM_DECODE_MAX_IMPORTS)
        return decode_fail(error, WASM_DECODE_FORMAT,
                           wasm_reader_offset(section),
                           "invalid import count");
    if (count) {
        module->imports = calloc(count, sizeof(*module->imports));
        if (!module->imports)
            return decode_fail(error, WASM_DECODE_OUT_OF_MEMORY,
                               wasm_reader_offset(section),
                               "out of memory decoding imports");
    }
    module->import_count = count;
    for (uint32_t i = 0; i < count; i++) {
        if (!decode_import(section, &module->imports[i]))
            return decode_fail(error, WASM_DECODE_FORMAT,
                               wasm_reader_offset(section),
                               "invalid import declaration");
    }
    if (section->cursor != section->end)
        return decode_fail(error, WASM_DECODE_FORMAT,
                           wasm_reader_offset(section),
                           "trailing bytes in import section");
    return WASM_DECODE_OK;
}

static wasm_decode_status decode_module_imports(const uint8_t *bytes,
                                                size_t size,
                                                wasm_module *module,
                                                wasm_decode_error *error) {
    static const uint8_t header[8] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00
    };
    wasm_reader reader;
    wasm_decode_status status = WASM_DECODE_OK;

    if (!module)
        return decode_fail(error, WASM_DECODE_FORMAT, 0,
                           "null decoded module");
    wasm_module_init(module);
    if (!bytes || size < sizeof(header) ||
        memcmp(bytes, header, sizeof(header)) != 0)
        return decode_fail(error, WASM_DECODE_FORMAT, 0,
                           "invalid Wasm header");

    module->source = bytes;
    module->source_size = size;
    wasm_reader_init(&reader, bytes, size);
    reader.cursor += sizeof(header);
    while (reader.cursor < reader.end) {
        uint8_t section_id;
        uint32_t section_size;
        wasm_reader section;
        if (!wasm_reader_read_u8(&reader, &section_id) ||
            !wasm_reader_read_u32(&reader, &section_size) ||
            !wasm_reader_read_subreader(&reader, section_size, &section)) {
            status = decode_fail(error, WASM_DECODE_FORMAT,
                                 wasm_reader_offset(&reader),
                                 "truncated section");
            break;
        }
        if (section_id == 2) {
            status = decode_import_section(&section, module, error);
            break;
        }
        if (section_id != 0 && section_id > 2) break;
    }

    if (status != WASM_DECODE_OK) wasm_module_dispose(module);
    else if (error) memset(error, 0, sizeof(*error));
    return status;
}

wasm_decode_status wasm_decode_module_imports(const uint8_t *bytes, size_t size,
                                               wasm_module *module,
                                               wasm_decode_error *error) {
    return decode_module_imports(bytes, size, module, error);
}

static wasm_decode_status decode_section_directory(wasm_module *module,
                                                   wasm_decode_error *error) {
    wasm_reader reader;
    uint32_t capacity = 0;
    wasm_reader_init(&reader, module->source, module->source_size);
    reader.cursor += 8;
    while (reader.cursor < reader.end) {
        uint8_t id;
        uint32_t payload_size;
        wasm_reader payload;
        wasm_section *sections;
        if (!wasm_reader_read_u8(&reader, &id) ||
            !wasm_reader_read_u32(&reader, &payload_size) ||
            !wasm_reader_read_subreader(&reader, payload_size, &payload))
            return decode_fail(error, WASM_DECODE_FORMAT,
                               wasm_reader_offset(&reader),
                               "truncated section");
        if (module->section_count == capacity) {
            uint32_t next = capacity ? capacity * 2u : 16u;
            if (next < capacity)
                return decode_fail(error, WASM_DECODE_OUT_OF_MEMORY,
                                   wasm_reader_offset(&reader),
                                   "out of memory decoding sections");
#if SIZE_MAX <= UINT32_MAX
            if (next > SIZE_MAX / sizeof(*sections))
                return decode_fail(error, WASM_DECODE_OUT_OF_MEMORY,
                                   wasm_reader_offset(&reader),
                                   "out of memory decoding sections");
#endif
            sections = realloc(module->sections,
                               (size_t)next * sizeof(*sections));
            if (!sections)
                return decode_fail(error, WASM_DECODE_OUT_OF_MEMORY,
                                   wasm_reader_offset(&reader),
                                   "out of memory decoding sections");
            module->sections = sections;
            capacity = next;
        }
        module->sections[module->section_count++] = (wasm_section){
            wasm_reader_offset(&payload), payload_size, id
        };
    }
    module->fully_decoded = 1;
    return WASM_DECODE_OK;
}

wasm_decode_status wasm_decode_module(const uint8_t *bytes, size_t size,
                                      wasm_module *module,
                                      wasm_decode_error *error) {
    uint8_t *owned;
    wasm_decode_status status;
    if (!module)
        return decode_fail(error, WASM_DECODE_FORMAT, 0,
                           "null decoded module");
    wasm_module_init(module);
    if (!bytes || size < 8)
        return decode_fail(error, WASM_DECODE_FORMAT, 0,
                           "invalid Wasm header");
    owned = malloc(size);
    if (!owned)
        return decode_fail(error, WASM_DECODE_OUT_OF_MEMORY, 0,
                           "out of memory copying module");
    memcpy(owned, bytes, size);
    status = decode_module_imports(owned, size, module, error);
    if (status != WASM_DECODE_OK) {
        free(owned);
        return status;
    }
    module->owned_source = owned;
    status = decode_section_directory(module, error);
    if (status != WASM_DECODE_OK) wasm_module_dispose(module);
    return status;
}
