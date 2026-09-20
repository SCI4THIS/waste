/* c-engine-continuation.c — Stage 2 evaluator continuation probe. */

#include "engine_internal.h"
#include "runtime_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int tests;

#define CHECK(condition, ...) do { \
    tests++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fputc('\n', stderr); \
        failures++; \
    } \
} while (0)

typedef struct {
    int calls;
    int successful_calls;
} pause_state;

typedef struct {
    waste_exec_engine *engine;
    uint32_t function;
} trampoline_state;

static exec_status pause_host(void *data, const wasm_value *args,
                              int arg_count, wasm_value *results,
                              int *result_count, exec_error *error,
                              const waste_exec_engine *caller) {
    (void)args;
    (void)caller;
    pause_state *state = data;
    if (arg_count != 0) return exec_fail(error, EXEC_ERROR_TRAP,
                                         "pause argument mismatch");
    state->calls++;
    if (state->calls == 1) {
        error->yield_reason = EXEC_YIELD_FORK;
        return EXEC_YIELD;
    }
    state->successful_calls++;
    results[0].type = WASM_VALTYPE_I32;
    results[0].i32 = 41;
    *result_count = 1;
    return EXEC_OK;
}

static exec_status unexpected_control(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)results;
    (void)result_count; (void)caller;
    return exec_fail(error, EXEC_ERROR_TRAP,
                     "non-local control callback was called");
}

static exec_status trampoline_host(void *data, const wasm_value *args,
                                   int arg_count, wasm_value *results,
                                   int *result_count, exec_error *error,
                                   const waste_exec_engine *caller) {
    (void)caller;
    trampoline_state *trampoline = data;
    return exec_invoke(trampoline->engine, trampoline->function, args,
                       arg_count, results, result_count, error);
}

static uint8_t *read_file(const char *path, size_t *size_out) {
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file); return NULL;
    }
    bytes = (uint8_t *)malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes); fclose(file); return NULL;
    }
    fclose(file);
    *size_out = (size_t)length;
    return bytes;
}

static exec_status invoke_export(waste_exec_engine *engine, const char *name,
                                 int32_t *value, exec_error *error) {
    uint32_t function;
    wasm_value result;
    int count = 0;
    exec_status status = exec_find_export(engine, name, &function, error);
    if (status != EXEC_OK) return status;
    status = exec_invoke(engine, function, NULL, 0, &result, &count, error);
    if (status == EXEC_OK && count == 1) *value = result.i32;
    return status;
}

static void run_replay_case(waste_exec_engine *engine, pause_state *pause,
                            const char *name, int32_t expected) {
    exec_continuation *saved = (exec_continuation *)calloc(1, sizeof(*saved));
    exec_error error;
    int32_t value = 0;
    memset(&error, 0, sizeof(error));
    CHECK(saved != NULL, "%s continuation allocation", name);
    if (!saved) return;
    exec_continuation_init(saved);

    exec_status status = invoke_export(engine, name, &value, &error);
    CHECK(status == EXEC_YIELD && error.yield_reason == EXEC_YIELD_FORK,
          "%s initial status/reason = %d/%d", name, status,
          error.yield_reason);
    CHECK(pause->calls == 1, "%s initial host calls = %d", name, pause->calls);
    CHECK(exec_continuation_capture(engine, saved, &error) == EXEC_OK,
          "%s continuation capture: %s", name, error.message);

    memset(&error, 0, sizeof(error));
    status = invoke_export(engine, name, &value, &error);
    CHECK(status == EXEC_OK && value == expected,
          "%s live resume = status %d value %d", name, status, value);
    CHECK(pause->successful_calls == 1,
          "%s live successful host calls = %d", name, pause->successful_calls);

    CHECK(exec_continuation_restore(saved, &error) == EXEC_OK,
          "%s continuation restore: %s", name, error.message);
    memset(&error, 0, sizeof(error));
    status = invoke_export(engine, name, &value, &error);
    CHECK(status == EXEC_OK && value == expected,
          "%s replay resume = status %d value %d", name, status, value);
    CHECK(pause->successful_calls == 2,
          "%s replay successful host calls = %d", name, pause->successful_calls);
    exec_continuation_destroy(saved);
    free(saved);
}

static void run_cross_instance_case(waste_exec_engine *provider,
                                    pause_state *pause, const char *path) {
    size_t size;
    uint8_t *bytes = read_file(path, &size);
    exec_error error;
    uint32_t provider_function = 0, provider_type = 0;
    trampoline_state trampoline = {provider, 0};
    exec_host_import import;
    exec_imports imports;
    waste_exec_engine *consumer = NULL;
    exec_continuation *provider_saved =
        (exec_continuation *)calloc(1, sizeof(*provider_saved));
    exec_continuation *consumer_saved =
        (exec_continuation *)calloc(1, sizeof(*consumer_saved));
    int32_t value = 0;
    memset(&error, 0, sizeof(error));
    CHECK(provider_saved && consumer_saved, "cross-instance continuation allocation");
    if (!provider_saved || !consumer_saved) {
        free(provider_saved); free(consumer_saved); free(bytes); return;
    }
    exec_continuation_init(provider_saved);
    exec_continuation_init(consumer_saved);

    CHECK(bytes != NULL, "read cross-instance fixture %s", path);
    if (!bytes) return;
    CHECK(exec_find_export(provider, "run", &provider_function, &error) == EXEC_OK,
          "find provider export: %s", error.message);
    CHECK(exec_get_func_type_index(provider, provider_function, &provider_type,
                                   &error) == EXEC_OK,
          "find provider type: %s", error.message);
    trampoline.function = provider_function;
    import = (exec_host_import){"provider", "run", trampoline_host,
                                &trampoline, provider, provider_type, 1,
                                EXEC_HOST_CONTROL_NONE};
    imports.functions = &import;
    imports.function_count = 1;
    CHECK(exec_load_with_imports(bytes, size, &imports, &consumer, &error) == EXEC_OK,
          "load consumer: %s", error.message);
    free(bytes);
    if (!consumer) return;

    pause->calls = 0;
    pause->successful_calls = 0;
    memset(&error, 0, sizeof(error));
    exec_status status = invoke_export(consumer, "run_consumer", &value,
                                       &error);
    CHECK(status == EXEC_YIELD && error.yield_reason == EXEC_YIELD_FORK,
          "cross-instance initial status/reason = %d/%d", status,
          error.yield_reason);
    CHECK(exec_continuation_capture(provider, provider_saved, &error) == EXEC_OK,
          "capture provider continuation: %s", error.message);
    CHECK(exec_continuation_capture(consumer, consumer_saved, &error) == EXEC_OK,
          "capture consumer continuation: %s", error.message);

    memset(&error, 0, sizeof(error));
    status = invoke_export(consumer, "run_consumer", &value, &error);
    CHECK(status == EXEC_OK && value == 45,
          "cross-instance live resume = status %d value %d", status, value);
    CHECK(pause->successful_calls == 1,
          "cross-instance live host calls = %d", pause->successful_calls);

    CHECK(exec_continuation_restore(provider_saved, &error) == EXEC_OK,
          "restore provider continuation: %s", error.message);
    CHECK(exec_continuation_restore(consumer_saved, &error) == EXEC_OK,
          "restore consumer continuation: %s", error.message);
    memset(&error, 0, sizeof(error));
    status = invoke_export(consumer, "run_consumer", &value, &error);
    CHECK(status == EXEC_OK && value == 45,
          "cross-instance replay resume = status %d value %d", status, value);
    CHECK(pause->successful_calls == 2,
          "cross-instance replay host calls = %d", pause->successful_calls);

    exec_continuation_destroy(provider_saved);
    exec_continuation_destroy(consumer_saved);
    free(provider_saved);
    free(consumer_saved);
    exec_free(consumer);
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "../../tests/c-engine-continuation.wasm";
    size_t size;
    uint8_t *bytes = read_file(path, &size);
    pause_state pause = {0};
    exec_host_import host = {
        "host", "pause", pause_host, &pause, NULL, 0, 0,
        EXEC_HOST_CONTROL_NONE
    };
    exec_host_import controls[] = {
        host,
        {"env", "sigsetjmp", unexpected_control, &pause, NULL, 0, 0,
         EXEC_HOST_CONTROL_SIGSETJMP},
        {"env", "siglongjmp", unexpected_control, &pause, NULL, 0, 0,
         EXEC_HOST_CONTROL_SIGLONGJMP},
    };
    exec_imports imports = {.functions = controls, .function_count = 3};
    waste_exec_engine *engine = NULL;
    exec_error error;

    CHECK(bytes != NULL, "read fixture %s", path);
    if (!bytes) return 1;
    memset(&error, 0, sizeof(error));
    CHECK(exec_load_with_imports(bytes, size, &imports, &engine, &error) == EXEC_OK,
          "load fixture: %s", error.message);
    free(bytes);
    if (!engine) return 1;

    run_replay_case(engine, &pause, "run", 42);
    pause.calls = 0;
    pause.successful_calls = 0;
    run_replay_case(engine, &pause, "run_indirect", 43);
    pause.calls = 0;
    pause.successful_calls = 0;
    run_replay_case(engine, &pause, "run_jump", 7);
    run_cross_instance_case(engine, &pause,
                            argc > 2 ? argv[2] : "../../tests/c-engine-continuation-consumer.wasm");

    exec_free(engine);
    printf("C-engine continuation: %d checks, %d failures\n", tests, failures);
    return failures ? 1 : 0;
}
