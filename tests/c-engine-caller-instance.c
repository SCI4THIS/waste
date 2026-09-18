/* c-engine-caller-instance.c — Verify that host imports receive the correct
 * caller engine so they can access the calling instance's memory directly.
 *
 * Scenarios:
 *   1. Direct call: module A calls host → caller has A's memory
 *   2. Direct call: module B calls host → caller has B's memory
 *   3. Nested call: B calls A's export, which calls host → caller has A's memory
 *   4. Two instances of the same decoded module have independent memories
 *
 * Built with ASan/UBSan, linked against system libc. */

#include "engine_internal.h"
#include "runtime_internal.h"
#include "instantiate.h"
#include "wasm/decode.h"
#include "include/waste.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int tests = 0;

#define CHECK(cond, ...) do { \
    tests++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        failures++; \
    } \
} while (0)

/* ---- Host function: reads an i32 from the caller's memory ---- */

static exec_status host_read_caller_mem(
        void *data, const wasm_value *args, int arg_count,
        wasm_value *results, int *result_count, exec_error *error,
        const waste_exec_engine *caller) {
    (void)data;
    if (arg_count != 2)
        return exec_fail(error, EXEC_ERROR_TRAP, "expected 2 args");
    uint32_t offset = (uint32_t)args[0].i32;
    uint32_t length = (uint32_t)args[1].i32;
    if (!caller->memory)
        return exec_fail(error, EXEC_ERROR_TRAP, "caller has no memory");
    uint64_t byte_size = caller->memory->pages * UINT64_C(65536);
    if ((uint64_t)offset + length > byte_size)
        return exec_fail(error, EXEC_ERROR_TRAP, "out of bounds");
    uint32_t value = 0;
    const uint8_t *mem = caller->memory->data + offset;
    for (uint32_t i = 0; i < length && i < 4; i++)
        value |= (uint32_t)mem[i] << (i * 8);
    results[0].type = WASM_VALTYPE_I32;
    results[0].i32 = (int32_t)value;
    *result_count = 1;
    return EXEC_OK;
}

/* ---- Cross-module call trampoline ---- */

typedef struct {
    waste_exec_engine *engine;
    uint32_t func_idx;
} trampoline_data;

static exec_status trampoline_call(
        void *data, const wasm_value *args, int arg_count,
        wasm_value *results, int *result_count, exec_error *error,
        const waste_exec_engine *caller) {
    (void)caller;
    trampoline_data *td = (trampoline_data *)data;
    return exec_invoke(td->engine, td->func_idx, args, arg_count,
                       results, result_count, error);
}

/* ---- Helpers ---- */

static uint8_t *read_file(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    uint8_t *buf = malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *size_out = (size_t)len;
    return buf;
}

static int invoke0(waste_exec_engine *engine, const char *name, int32_t *out) {
    uint32_t func; int count = 0; exec_error error = {0};
    exec_status st = exec_find_export(engine, name, &func, &error);
    if (st != EXEC_OK) return 0;
    wasm_value result;
    st = exec_invoke(engine, func, NULL, 0, &result, &count, &error);
    if (st != EXEC_OK || count != 1) return 0;
    *out = result.i32;
    return 1;
}

static int invoke1(waste_exec_engine *engine, const char *name,
                   int32_t arg, int32_t *out) {
    uint32_t func; int count = 0; exec_error error = {0};
    wasm_value a = {.type = WASM_VALTYPE_I32, .i32 = arg};
    wasm_value result;
    exec_status st = exec_find_export(engine, name, &func, &error);
    if (st != EXEC_OK) return 0;
    st = exec_invoke(engine, func, &a, 1, &result, &count, &error);
    if (st != EXEC_OK || count != 1) return 0;
    *out = result.i32;
    return 1;
}

/* ---- Test 1-4: direct calls and nested calls ---- */

static void test_direct_and_nested(const char *path_a, const char *path_b) {
    size_t size_a, size_b;
    uint8_t *bytes_a = read_file(path_a, &size_a);
    uint8_t *bytes_b = read_file(path_b, &size_b);
    if (!bytes_a || !bytes_b) {
        CHECK(0, "read test modules"); free(bytes_a); free(bytes_b); return;
    }
    exec_error error = {0};

    /* Load module A with host import */
    exec_host_import host = {
        "host", "read_caller_mem", host_read_caller_mem,
        NULL, NULL, 0, 0, EXEC_HOST_CONTROL_NONE
    };
    exec_imports imports_a = {.functions = &host, .function_count = 1};
    waste_exec_engine *eng_a = NULL;
    exec_status st = exec_load_with_imports(bytes_a, size_a, &imports_a,
                                             &eng_a, &error);
    free(bytes_a);
    CHECK(st == EXEC_OK, "load module A: %s", error.message);
    if (st != EXEC_OK) { free(bytes_b); return; }

    /* Test 1: A.read_initial → host reads A's memory at offset 0 */
    int32_t val = 0;
    CHECK(invoke0(eng_a, "read_initial", &val), "A.read_initial invoke");
    CHECK(val == (int32_t)0xefbeadde,
          "A.read_initial = 0x%08x (expected 0xefbeadde)", (unsigned)val);

    /* Test 2: A.store_and_read → stores 0x42424242 at offset 16, reads back */
    CHECK(invoke1(eng_a, "store_and_read", 0x42424242, &val),
          "A.store_and_read invoke");
    CHECK(val == 0x42424242,
          "A.store_and_read = 0x%08x (expected 0x42424242)", (unsigned)val);

    /* Set up trampoline for B's import of A.store_and_read */
    uint32_t a_func_idx = 0, a_type_idx = 0;
    exec_find_export(eng_a, "store_and_read", &a_func_idx, &error);
    exec_get_func_type_index(eng_a, a_func_idx, &a_type_idx, &error);

    trampoline_data td = {eng_a, a_func_idx};
    exec_host_import bindings_b[2] = {
        {"host", "read_caller_mem", host_read_caller_mem,
         NULL, NULL, 0, 0, EXEC_HOST_CONTROL_NONE},
        {"modA", "store_and_read", trampoline_call, &td,
         eng_a, a_type_idx, 1, EXEC_HOST_CONTROL_NONE}
    };
    exec_imports imports_b = {.functions = bindings_b, .function_count = 2};
    waste_exec_engine *eng_b = NULL;
    st = exec_load_with_imports(bytes_b, size_b, &imports_b, &eng_b, &error);
    free(bytes_b);
    CHECK(st == EXEC_OK, "load module B: %s", error.message);
    if (st != EXEC_OK) { exec_free(eng_a); return; }

    /* Test 3: B.read_own → host reads B's memory at offset 0 */
    CHECK(invoke0(eng_b, "read_own", &val), "B.read_own invoke");
    CHECK(val == (int32_t)0xbebafeca,
          "B.read_own = 0x%08x (expected 0xbebafeca)", (unsigned)val);

    /* Verify A and B have different memories */
    CHECK(eng_a->memory != eng_b->memory,
          "A and B should have different memory objects");
    CHECK(eng_a->memory->data != eng_b->memory->data,
          "A and B should have different memory data");

    /* Test 4: B.nested_through_a → B calls A.store_and_read,
     * A calls host_read_caller_mem → host sees A's memory */
    CHECK(invoke1(eng_b, "nested_through_a", 0x99887766, &val),
          "B.nested_through_a invoke");
    CHECK(val == (int32_t)0x99887766,
          "nested_through_a = 0x%08x (expected 0x99887766)", (unsigned)val);

    /* Verify A's memory was modified (at offset 16), B's was not */
    uint32_t a_stored = 0, b_stored = 0;
    memcpy(&a_stored, eng_a->memory->data + 16, 4);
    memcpy(&b_stored, eng_b->memory->data + 16, 4);
    CHECK(a_stored == UINT32_C(0x99887766),
          "A memory[16] = 0x%08x (expected 0x99887766)", a_stored);
    CHECK(b_stored == 0,
          "B memory[16] = 0x%08x (expected 0x00000000)", b_stored);

    exec_free(eng_b);
    exec_free(eng_a);
}

/* ---- Test 5-6: two instances of the same decoded module ---- */

static void test_two_instances(const char *path_a) {
    size_t size;
    uint8_t *bytes = read_file(path_a, &size);
    if (!bytes) { CHECK(0, "read module for dual-instance"); return; }

    wasm_module module;
    wasm_decode_error decode_err = {0};
    exec_error error = {0};
    if (wasm_decode_module(bytes, size, &module, &decode_err) != WASM_DECODE_OK) {
        CHECK(0, "decode: %s", decode_err.message);
        free(bytes); return;
    }
    free(bytes);

    exec_host_import host = {
        "host", "read_caller_mem", host_read_caller_mem,
        NULL, NULL, 0, 0, EXEC_HOST_CONTROL_NONE
    };
    exec_imports imports = {.functions = &host, .function_count = 1};

    waste_exec_engine *inst1 = NULL, *inst2 = NULL;
    CHECK(wasm_instantiate_module(&module, &imports, &inst1, &error) == EXEC_OK,
          "instantiate 1: %s", error.message);
    CHECK(wasm_instantiate_module(&module, &imports, &inst2, &error) == EXEC_OK,
          "instantiate 2: %s", error.message);
    wasm_module_dispose(&module);

    if (!inst1 || !inst2) { exec_free(inst1); exec_free(inst2); return; }

    /* Both see their own initial data */
    int32_t v1 = 0, v2 = 0;
    CHECK(invoke0(inst1, "read_initial", &v1), "inst1.read_initial");
    CHECK(invoke0(inst2, "read_initial", &v2), "inst2.read_initial");
    CHECK(v1 == (int32_t)0xefbeadde, "inst1 initial = 0x%08x", (unsigned)v1);
    CHECK(v2 == (int32_t)0xefbeadde, "inst2 initial = 0x%08x", (unsigned)v2);

    /* Store different values */
    CHECK(invoke1(inst1, "store_and_read", 0x11111111, &v1),
          "inst1.store_and_read");
    CHECK(invoke1(inst2, "store_and_read", 0x22222222, &v2),
          "inst2.store_and_read");
    CHECK(v1 == 0x11111111, "inst1 stored = 0x%08x", (unsigned)v1);
    CHECK(v2 == 0x22222222, "inst2 stored = 0x%08x", (unsigned)v2);

    /* Re-verify isolation: inst1's store didn't affect inst2 */
    CHECK(inst1->memory != inst2->memory,
          "instances have different memory objects");
    CHECK(inst1->memory->data != inst2->memory->data,
          "instances have different memory data");
    uint32_t m1 = 0, m2 = 0;
    memcpy(&m1, inst1->memory->data + 16, 4);
    memcpy(&m2, inst2->memory->data + 16, 4);
    CHECK(m1 == 0x11111111, "inst1 memory[16] = 0x%08x", m1);
    CHECK(m2 == 0x22222222, "inst2 memory[16] = 0x%08x", m2);

    exec_free(inst1);
    exec_free(inst2);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s caller-a.wasm caller-b.wasm\n", argv[0]);
        return 2;
    }

    test_direct_and_nested(argv[1], argv[2]);
    test_two_instances(argv[1]);

    if (failures) {
        fprintf(stderr, "%d/%d tests FAILED\n", failures, tests);
        return 1;
    }
    printf("caller-instance: %d tests passed\n", tests);
    return 0;
}
