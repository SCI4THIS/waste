#include "wasm/decode.h"
#include "wasm/writer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) do {                                                \
    if (!(condition)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                       \
                __FILE__, __LINE__, #condition);                             \
        failures++;                                                          \
    }                                                                        \
} while (0)

static int write_name(wasm_writer *writer, const char *name) {
    size_t length = strlen(name);
    return length <= UINT32_MAX &&
           wasm_writer_write_u32(writer, (uint32_t)length) &&
           wasm_writer_write_bytes(writer, name, length);
}

static uint8_t *build_import_module(size_t *size_out) {
    static const uint8_t header[8] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00
    };
    wasm_writer section;
    wasm_writer module;
    uint8_t *bytes;

    wasm_writer_init(&section);
    wasm_writer_init(&module);
    wasm_writer_write_u32(&section, 5);

    write_name(&section, "host");
    write_name(&section, "function");
    wasm_writer_write_u8(&section, WASM_IMPORT_FUNCTION);
    wasm_writer_write_u32(&section, 3);

    write_name(&section, "host");
    write_name(&section, "table");
    wasm_writer_write_u8(&section, WASM_IMPORT_TABLE);
    wasm_writer_write_u8(&section, 0x70);
    wasm_writer_write_u32(&section, 1);
    wasm_writer_write_u64(&section, 2);
    wasm_writer_write_u64(&section, 4);

    write_name(&section, "host");
    write_name(&section, "memory");
    wasm_writer_write_u8(&section, WASM_IMPORT_MEMORY);
    wasm_writer_write_u32(&section, 5);
    wasm_writer_write_u64(&section, 1);
    wasm_writer_write_u64(&section, 2);

    write_name(&section, "host");
    write_name(&section, "global");
    wasm_writer_write_u8(&section, WASM_IMPORT_GLOBAL);
    wasm_writer_write_u8(&section, 0x7e);
    wasm_writer_write_u8(&section, 1);

    write_name(&section, "host");
    write_name(&section, "tag");
    wasm_writer_write_u8(&section, WASM_IMPORT_TAG);
    wasm_writer_write_u32(&section, 0);
    wasm_writer_write_u32(&section, 7);

    wasm_writer_write_bytes(&module, header, sizeof(header));
    wasm_writer_write_u8(&module, 2);
    wasm_writer_write_u32(&module, (uint32_t)section.len);
    wasm_writer_write_bytes(&module, section.data, section.len);
    wasm_writer_dispose(&section);
    bytes = wasm_writer_take(&module, size_out);
    CHECK(bytes != NULL);
    return bytes;
}

static void test_all_import_descriptors(void) {
    size_t size = 0;
    uint8_t *bytes = build_import_module(&size);
    wasm_module module;
    wasm_decode_error error;

    if (!bytes) return;
    CHECK(wasm_decode_module_imports(bytes, size, &module, &error) ==
          WASM_DECODE_OK);
    CHECK(module.source == bytes);
    CHECK(module.source_size == size);
    CHECK(module.import_count == 5);
    if (module.import_count == 5) {
        CHECK(strcmp(module.imports[0].module, "host") == 0);
        CHECK(strcmp(module.imports[0].name, "function") == 0);
        CHECK(module.imports[0].kind == WASM_IMPORT_FUNCTION);
        CHECK(module.imports[0].descriptor.function.type_index == 3);

        CHECK(module.imports[1].kind == WASM_IMPORT_TABLE);
        CHECK(module.imports[1].descriptor.table.element_type ==
              WASM_VALTYPE_FUNCREF);
        CHECK(module.imports[1].descriptor.table.limits.flags == 1);
        CHECK(module.imports[1].descriptor.table.limits.minimum == 2);
        CHECK(module.imports[1].descriptor.table.limits.maximum == 4);

        CHECK(module.imports[2].kind == WASM_IMPORT_MEMORY);
        CHECK(module.imports[2].descriptor.memory.limits.flags == 5);
        CHECK(module.imports[2].descriptor.memory.limits.minimum == 1);
        CHECK(module.imports[2].descriptor.memory.limits.maximum == 2);

        CHECK(module.imports[3].kind == WASM_IMPORT_GLOBAL);
        CHECK(module.imports[3].descriptor.global.value_type ==
              WASM_VALTYPE_I64);
        CHECK(module.imports[3].descriptor.global.mutable_ == 1);

        CHECK(module.imports[4].kind == WASM_IMPORT_TAG);
        CHECK(module.imports[4].descriptor.tag.attribute == 0);
        CHECK(module.imports[4].descriptor.tag.type_index == 7);
    }
    wasm_module_dispose(&module);
    CHECK(module.imports == NULL);
    CHECK(module.import_count == 0);
    free(bytes);
}

static void test_no_import_section(void) {
    static const uint8_t header[8] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00
    };
    wasm_module module;
    wasm_decode_error error;
    CHECK(wasm_decode_module_imports(header, sizeof(header), &module, &error) ==
          WASM_DECODE_OK);
    CHECK(module.import_count == 0);
    wasm_module_dispose(&module);
}

static void test_owned_module_source(void) {
    size_t size = 0;
    uint8_t *bytes = build_import_module(&size);
    wasm_module module;
    wasm_decode_error error;
    if (!bytes) return;
    CHECK(wasm_decode_module(bytes, size, &module, &error) == WASM_DECODE_OK);
    CHECK(module.owned_source != NULL);
    CHECK(module.source == module.owned_source);
    CHECK(module.source != bytes);
    CHECK(module.import_count == 5);
    CHECK(module.fully_decoded == 1);
    CHECK(module.section_count == 1);
    if (module.section_count == 1) {
        CHECK(module.sections[0].id == 2);
        CHECK(module.sections[0].payload_offset > 8);
    }
    memset(bytes, 0, size);
    free(bytes);
    CHECK(module.source[0] == 0x00);
    CHECK(module.source[1] == 0x61);
    CHECK(strcmp(module.imports[0].name, "function") == 0);
    wasm_module_dispose(&module);
    CHECK(module.source == NULL);
    CHECK(module.owned_source == NULL);
}

static void test_malformed_imports(void) {
    static const uint8_t invalid_utf8[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x02, 0x07, 0x01, 0x01, 0xff, 0x01, 0x66, 0x00, 0x00
    };
    static const uint8_t trailing[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x02, 0x02, 0x00, 0x00
    };
    size_t size = 0;
    uint8_t *truncated = build_import_module(&size);
    wasm_module module;
    wasm_decode_error error;

    CHECK(wasm_decode_module_imports(invalid_utf8, sizeof(invalid_utf8),
                                     &module, &error) == WASM_DECODE_FORMAT);
    CHECK(module.imports == NULL);
    CHECK(error.offset > 8);
    CHECK(wasm_decode_module_imports(trailing, sizeof(trailing),
                                     &module, &error) == WASM_DECODE_FORMAT);
    CHECK(module.imports == NULL);
    if (truncated) {
        CHECK(wasm_decode_module_imports(truncated, size - 1, &module,
                                         &error) == WASM_DECODE_FORMAT);
        CHECK(module.imports == NULL);
        free(truncated);
    }
}

int main(void) {
    test_all_import_descriptors();
    test_no_import_section();
    test_owned_module_source();
    test_malformed_imports();
    if (failures) {
        fprintf(stderr, "%d wasm import decode checks failed\n", failures);
        return 1;
    }
    puts("wasm import decode checks passed");
    return 0;
}
