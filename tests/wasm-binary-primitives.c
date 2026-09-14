#include "wasm/wasm_leb.h"
#include "wasm/wasm_reader.h"
#include "wasm/wasm_writer.h"

#include <inttypes.h>
#include <limits.h>
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

static void test_u32_roundtrip(void) {
    static const uint32_t values[] = {
        0, 1, 127, 128, 624485, UINT32_MAX
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        uint8_t bytes[5];
        const uint8_t *cursor = bytes;
        size_t length = wasm_leb_encode_u32(bytes, values[i]);
        uint32_t decoded = 0;
        CHECK(length >= 1 && length <= sizeof(bytes));
        CHECK(wasm_leb_read_u32(&cursor, bytes + length, &decoded) ==
              WASM_LEB_OK);
        CHECK(cursor == bytes + length);
        CHECK(decoded == values[i]);
    }
}

static void test_u64_roundtrip(void) {
    static const uint64_t values[] = {
        0, 1, 127, 128, UINT32_MAX, UINT64_MAX
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        uint8_t bytes[10];
        const uint8_t *cursor = bytes;
        size_t length = wasm_leb_encode_u64(bytes, values[i]);
        uint64_t decoded = 0;
        CHECK(length >= 1 && length <= sizeof(bytes));
        CHECK(wasm_leb_read_u64(&cursor, bytes + length, &decoded) ==
              WASM_LEB_OK);
        CHECK(cursor == bytes + length);
        CHECK(decoded == values[i]);
    }
}

static void test_i32_roundtrip(void) {
    static const int32_t values[] = {
        INT32_MIN, -624485, -65, -64, -1, 0, 63, 64, 624485, INT32_MAX
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        uint8_t bytes[5];
        const uint8_t *cursor = bytes;
        size_t length = wasm_leb_encode_i32(bytes, values[i]);
        int32_t decoded = 0;
        CHECK(length >= 1 && length <= sizeof(bytes));
        CHECK(wasm_leb_read_i32(&cursor, bytes + length, &decoded) ==
              WASM_LEB_OK);
        CHECK(cursor == bytes + length);
        CHECK(decoded == values[i]);
    }
}

static void test_i64_roundtrip(void) {
    static const int64_t values[] = {
        INT64_MIN, -INT64_C(624485), -65, -64, -1,
        0, 63, 64, INT64_C(624485), INT64_MAX
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        uint8_t bytes[10];
        const uint8_t *cursor = bytes;
        size_t length = wasm_leb_encode_i64(bytes, values[i]);
        int64_t decoded = 0;
        CHECK(length >= 1 && length <= sizeof(bytes));
        CHECK(wasm_leb_read_i64(&cursor, bytes + length, &decoded) ==
              WASM_LEB_OK);
        CHECK(cursor == bytes + length);
        CHECK(decoded == values[i]);
    }
}

static void test_s33_roundtrip(void) {
    static const int64_t values[] = {
        -INT64_C(4294967296), -1, 0, INT64_C(4294967295)
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        uint8_t bytes[5];
        const uint8_t *cursor = bytes;
        size_t length = wasm_leb_encode_s33(bytes, values[i]);
        int64_t decoded = 0;
        CHECK(length >= 1 && length <= sizeof(bytes));
        CHECK(wasm_leb_read_s33(&cursor, bytes + length, &decoded) ==
              WASM_LEB_OK);
        CHECK(cursor == bytes + length);
        CHECK(decoded == values[i]);
    }
    {
        uint8_t bytes[5];
        CHECK(wasm_leb_encode_s33(bytes, -INT64_C(4294967297)) == 0);
        CHECK(wasm_leb_encode_s33(bytes, INT64_C(4294967296)) == 0);
    }
}

static void test_known_canonical_encodings(void) {
    static const uint8_t u624485[] = {0xe5, 0x8e, 0x26};
    static const uint8_t i_minus_624485[] = {0x9b, 0xf1, 0x59};
    uint8_t bytes[10];
    size_t length;

    length = wasm_leb_encode_u32(bytes, 624485);
    CHECK(length == sizeof(u624485));
    CHECK(memcmp(bytes, u624485, sizeof(u624485)) == 0);

    length = wasm_leb_encode_i32(bytes, -624485);
    CHECK(length == sizeof(i_minus_624485));
    CHECK(memcmp(bytes, i_minus_624485, sizeof(i_minus_624485)) == 0);

    length = wasm_leb_encode_s33(bytes, -16);
    CHECK(length == 1 && bytes[0] == 0x70);
}

static void test_noncanonical_and_malformed(void) {
    static const uint8_t overlong_zero[] = {0x80, 0x00};
    static const uint8_t overlong_minus_one[] = {0xff, 0x7f};
    static const uint8_t truncated[] = {0x80};
    static const uint8_t u32_overflow[] = {0x80, 0x80, 0x80, 0x80, 0x10};
    static const uint8_t u32_too_long[] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x00};
    static const uint8_t i32_positive_overflow[] = {0xff, 0xff, 0xff, 0xff, 0x0f};
    static const uint8_t u64_overflow[] = {
        0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x02
    };
    static const uint8_t i64_positive_overflow[] = {
        0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0x01
    };
    static const uint8_t s33_positive_overflow[] = {
        0x80, 0x80, 0x80, 0x80, 0x10
    };
    static const uint8_t s33_negative_overflow[] = {
        0xff, 0xff, 0xff, 0xff, 0x6f
    };
    const uint8_t *cursor;
    uint32_t u32 = 1;
    uint64_t u64 = 1;
    int32_t i32 = 0;
    int64_t i64 = 0;

    cursor = overlong_zero;
    CHECK(wasm_leb_read_u32(&cursor, overlong_zero + 2, &u32) == WASM_LEB_OK);
    CHECK(u32 == 0 && cursor == overlong_zero + 2);

    cursor = overlong_minus_one;
    CHECK(wasm_leb_read_i32(&cursor, overlong_minus_one + 2, &i32) ==
          WASM_LEB_OK);
    CHECK(i32 == -1 && cursor == overlong_minus_one + 2);

    cursor = truncated;
    CHECK(wasm_leb_read_u32(&cursor, truncated + 1, &u32) ==
          WASM_LEB_TRUNCATED);
    CHECK(cursor == truncated);

    cursor = u32_overflow;
    CHECK(wasm_leb_read_u32(&cursor, u32_overflow + 5, &u32) ==
          WASM_LEB_INVALID);
    CHECK(cursor == u32_overflow);

    cursor = u32_too_long;
    CHECK(wasm_leb_read_u32(&cursor, u32_too_long + 6, &u32) ==
          WASM_LEB_INVALID);
    CHECK(cursor == u32_too_long);

    cursor = i32_positive_overflow;
    CHECK(wasm_leb_read_i32(&cursor, i32_positive_overflow + 5, &i32) ==
          WASM_LEB_INVALID);
    CHECK(cursor == i32_positive_overflow);

    cursor = truncated;
    CHECK(wasm_leb_read_u64(&cursor, truncated + 1, &u64) ==
          WASM_LEB_TRUNCATED);
    CHECK(cursor == truncated);

    cursor = u64_overflow;
    CHECK(wasm_leb_read_u64(&cursor, u64_overflow + 10, &u64) ==
          WASM_LEB_INVALID);
    CHECK(cursor == u64_overflow);

    cursor = i64_positive_overflow;
    CHECK(wasm_leb_read_i64(&cursor, i64_positive_overflow + 10, &i64) ==
          WASM_LEB_INVALID);
    CHECK(cursor == i64_positive_overflow);

    cursor = s33_positive_overflow;
    CHECK(wasm_leb_read_s33(&cursor, s33_positive_overflow + 5, &i64) ==
          WASM_LEB_INVALID);
    CHECK(cursor == s33_positive_overflow);

    cursor = s33_negative_overflow;
    CHECK(wasm_leb_read_s33(&cursor, s33_negative_overflow + 5, &i64) ==
          WASM_LEB_INVALID);
    CHECK(cursor == s33_negative_overflow);
}

static void test_reader_boundaries(void) {
    static const uint8_t bytes[] = {0x01, 0x02, 0x03, 0x04};
    static const uint8_t truncated_leb[] = {0x80};
    wasm_reader reader;
    wasm_reader child;
    const uint8_t *slice = NULL;
    uint8_t byte = 0;

    wasm_reader_init(&reader, bytes, sizeof(bytes));
    CHECK(wasm_reader_offset(&reader) == 0);
    CHECK(wasm_reader_remaining(&reader) == sizeof(bytes));
    CHECK(wasm_reader_read_u8(&reader, &byte) && byte == 1);
    CHECK(wasm_reader_read_subreader(&reader, 2, &child));
    CHECK(wasm_reader_offset(&child) == 1);
    CHECK(wasm_reader_remaining(&child) == 2);
    CHECK(wasm_reader_read_bytes(&child, 2, &slice));
    CHECK(slice[0] == 2 && slice[1] == 3);
    CHECK(!wasm_reader_read_u8(&child, &byte));
    CHECK(wasm_reader_offset(&reader) == 3);
    CHECK(!wasm_reader_read_bytes(&reader, 2, &slice));
    CHECK(wasm_reader_offset(&reader) == 3);
    CHECK(wasm_reader_read_u8(&reader, &byte) && byte == 4);

    wasm_reader_init(&reader, truncated_leb, sizeof(truncated_leb));
    {
        uint32_t value = 0;
        CHECK(!wasm_reader_read_u32(&reader, &value));
        CHECK(reader.leb_status == WASM_LEB_TRUNCATED);
        CHECK(wasm_reader_offset(&reader) == 0);
    }
}

static void test_writer(void) {
    wasm_writer writer;
    wasm_reader reader;
    uint8_t *bytes;
    size_t size = 0;
    uint32_t u32 = 0;
    uint64_t u64 = 0;
    int32_t i32 = 0;
    int64_t i64 = 0;
    int64_t s33 = 0;

    wasm_writer_init(&writer);
    CHECK(wasm_writer_write_u8(&writer, 0xaa));
    CHECK(wasm_writer_write_u32(&writer, UINT32_MAX));
    CHECK(wasm_writer_write_u64(&writer, UINT64_MAX));
    CHECK(wasm_writer_write_i32(&writer, INT32_MIN));
    CHECK(wasm_writer_write_i64(&writer, INT64_MIN));
    CHECK(wasm_writer_write_s33(&writer, -INT64_C(4294967296)));
    bytes = wasm_writer_take(&writer, &size);
    CHECK(bytes != NULL && size > 1);
    CHECK(writer.data == NULL && writer.len == 0 && writer.cap == 0);

    wasm_reader_init(&reader, bytes, size);
    {
        uint8_t marker = 0;
        CHECK(wasm_reader_read_u8(&reader, &marker) && marker == 0xaa);
    }
    CHECK(wasm_reader_read_u32(&reader, &u32) && u32 == UINT32_MAX);
    CHECK(wasm_reader_read_u64(&reader, &u64) && u64 == UINT64_MAX);
    CHECK(wasm_reader_read_i32(&reader, &i32) && i32 == INT32_MIN);
    CHECK(wasm_reader_read_i64(&reader, &i64) && i64 == INT64_MIN);
    CHECK(wasm_reader_read_s33(&reader, &s33) &&
          s33 == -INT64_C(4294967296));
    CHECK(wasm_reader_remaining(&reader) == 0);
    free(bytes);
    wasm_writer_dispose(&writer);
}

int main(void) {
    test_u32_roundtrip();
    test_u64_roundtrip();
    test_i32_roundtrip();
    test_i64_roundtrip();
    test_s33_roundtrip();
    test_known_canonical_encodings();
    test_noncanonical_and_malformed();
    test_reader_boundaries();
    test_writer();
    if (failures) {
        fprintf(stderr, "%d binary primitive checks failed\n", failures);
        return 1;
    }
    puts("wasm binary primitive checks passed");
    return 0;
}
