/* Test: dylink.0 parser for PIC shared library modules.
 * Reads c-engine-shared-lib-trivial.so.wasm and verifies the dylink parser
 * correctly extracts memory_size, memory_alignment, table_size, table_alignment.
 *
 * Build (from repo root):
 *   cc -O0 -g -std=c11 -Wall -Wextra -I src/engine \
 *      tests/c-engine-shared-lib-dylink.c src/engine/wasm/reader.c \
 *      src/engine/wasm/leb.c -lm -o build/engine/shared-lib-dylink
 *   ./build/engine/shared-lib-dylink tests/c-engine-shared-lib-trivial.so.wasm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "wasm/reader.h"

/* Minimal reproduction of native_dylink_info from store.h — avoids pulling
 * in the full store header and its freestanding-libc dependencies. */
typedef struct {
    uint32_t memory_size;
    uint32_t memory_alignment;
    uint32_t table_size;
    uint32_t table_alignment;
} native_dylink_info;

/* Pull in native_parse_dylink directly — it only depends on wasm_reader and
 * string.h, so we can compile it without the rest of store.c. */
int native_parse_dylink(const uint8_t *bytes, size_t size,
                        native_dylink_info *info) {
    if (!bytes || !info || size < 8) return -1;
    memset(info, 0, sizeof(*info));
    if (bytes[0] != 0x00 || bytes[1] != 0x61 ||
        bytes[2] != 0x73 || bytes[3] != 0x6d) return -1;
    wasm_reader reader;
    wasm_reader_init(&reader, bytes + 8, size - 8);
    while (wasm_reader_remaining(&reader) > 0) {
        uint8_t section_id;
        uint32_t section_size;
        if (!wasm_reader_read_u8(&reader, &section_id) ||
            !wasm_reader_read_u32(&reader, &section_size))
            return -1;
        wasm_reader section;
        if (!wasm_reader_read_subreader(&reader, section_size, &section))
            return -1;
        if (section_id != 0) continue;
        uint32_t name_length;
        const uint8_t *name_bytes;
        if (!wasm_reader_read_u32(&section, &name_length) ||
            !wasm_reader_read_bytes(&section, name_length, &name_bytes))
            continue;
        if (name_length == 8 &&
            memcmp(name_bytes, "dylink.0", 8) == 0) {
            uint8_t subsection_type;
            uint32_t subsection_size;
            if (!wasm_reader_read_u8(&section, &subsection_type) ||
                !wasm_reader_read_u32(&section, &subsection_size))
                return -1;
            if (subsection_type != 1) return -1;
            wasm_reader sub;
            if (!wasm_reader_read_subreader(&section, subsection_size, &sub))
                return -1;
            if (!wasm_reader_read_u32(&sub, &info->memory_size) ||
                !wasm_reader_read_u32(&sub, &info->memory_alignment) ||
                !wasm_reader_read_u32(&sub, &info->table_size) ||
                !wasm_reader_read_u32(&sub, &info->table_alignment))
                return -1;
            return 0;
        }
    }
    return -1;
}

static int checks;
static int failures;

static void check(int condition, const char *label) {
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", label);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <pic-shared-lib.wasm>\n", argv[0]);
        return 2;
    }

    /* Read binary from disk. */
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *bytes = malloc((size_t)len);
    if (!bytes) { fclose(f); return 2; }
    if (fread(bytes, 1, (size_t)len, f) != (size_t)len) {
        free(bytes); fclose(f); return 2;
    }
    fclose(f);

    /* Parse dylink.0 section. */
    native_dylink_info info;
    memset(&info, 0xff, sizeof(info));
    int result = native_parse_dylink(bytes, (size_t)len, &info);
    check(result == 0, "native_parse_dylink succeeds on PIC module");
    check(info.memory_size == 4, "dylink memory_size == 4 (counter variable)");
    check(info.memory_alignment == 2, "dylink memory_alignment == 2 (log2 of 4)");
    check(info.table_size == 0, "dylink table_size == 0 (no indirect calls)");
    check(info.table_alignment == 0, "dylink table_alignment == 0");

    /* Verify parser rejects non-PIC modules. */
    uint8_t minimal[] = {
        0x00, 0x61, 0x73, 0x6d, /* magic */
        0x01, 0x00, 0x00, 0x00, /* version 1 */
    };
    native_dylink_info bad;
    int bad_result = native_parse_dylink(minimal, sizeof(minimal), &bad);
    check(bad_result != 0, "native_parse_dylink rejects non-PIC module");

    free(bytes);

    fprintf(stderr, "%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
