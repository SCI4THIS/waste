#include "engine_internal.h"
#include "runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { fprintf(stderr, "FAIL: "); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

typedef struct { int calls; } wait_state;

static exec_status read_host(void *data, const wasm_value *args, int argc,
                             wasm_value *results, int *result_count,
                             exec_error *error,
                             const waste_exec_engine *caller) {
    (void)args; (void)caller;
    wait_state *state = data;
    if (argc != 3) return exec_fail(error, EXEC_ERROR_TRAP, "read arity");
    state->calls++;
    if (state->calls == 1 || state->calls == 3) {
        memset(error, 0, sizeof(*error));
        error->status = EXEC_YIELD;
        error->yield_reason = EXEC_YIELD_READ;
        return EXEC_YIELD;
    }
    results[0] = i32_value(1);
    *result_count = 1;
    return EXEC_OK;
}

int main(int argc, char **argv) {
    FILE *file = fopen(argc > 1 ? argv[1] : "../../tests/c-engine-two-wait.wasm", "rb");
    long length; uint8_t *bytes; size_t size;
    wait_state state = {0};
    exec_host_import import = {"env", "read", read_host, &state, NULL, 0, 0,
                               EXEC_HOST_CONTROL_NONE};
    exec_imports imports = {.functions = &import, .function_count = 1};
    waste_exec_engine *engine = NULL; exec_error error; uint32_t function;
    wasm_value result; int count = 0;
    CHECK(file != NULL, "open two-wait fixture");
    if (!file) return 1;
    fseek(file, 0, SEEK_END); length = ftell(file); fseek(file, 0, SEEK_SET);
    bytes = malloc((size_t)length); size = (size_t)length;
    CHECK(bytes != NULL && fread(bytes, 1, size, file) == size,
          "read two-wait fixture"); fclose(file);
    CHECK(exec_load_with_imports(bytes, size, &imports, &engine, &error) == EXEC_OK,
          "load two-wait fixture: %s", error.message);
    free(bytes);
    if (!engine) return 1;
    CHECK(exec_find_export(engine, "run", &function, &error) == EXEC_OK,
          "find two-wait entry");
    exec_continuation continuation;
    exec_continuation_init(&continuation);
    exec_status status = exec_invoke(engine, function, NULL, 0, &result,
                                     &count, &error);
    CHECK(status == EXEC_YIELD && error.yield_reason == EXEC_YIELD_READ,
          "first read yield status=%d reason=%d", status, error.yield_reason);
    CHECK(exec_continuation_capture(engine, &continuation, &error) == EXEC_OK,
          "capture first read");
    CHECK(exec_continuation_resume(&continuation, engine, 2, EXEC_YIELD_READ,
                                   &error) == EXEC_OK, "resume first read");
    status = exec_invoke(engine, function, NULL, 0, &result, &count, &error);
    CHECK(status == EXEC_YIELD && error.yield_reason == EXEC_YIELD_READ,
          "second read yield status=%d reason=%d", status, error.yield_reason);
    exec_continuation_init(&continuation);
    CHECK(exec_continuation_capture(engine, &continuation, &error) == EXEC_OK,
          "capture second read");
    CHECK(exec_continuation_resume(&continuation, engine, 2, EXEC_YIELD_READ,
                                   &error) == EXEC_OK, "resume second read");
    status = exec_invoke(engine, function, NULL, 0, &result, &count, &error);
    CHECK(status == EXEC_OK && count == 1 && result.i32 == 3 && state.calls == 4,
          "two-read image completion status=%d result=%d calls=%d",
          status, count ? result.i32 : -1, state.calls);
    exec_continuation_destroy(&continuation); exec_free(engine);
    printf("C-engine replacement two-wait fixture: %d checks, %d failures\n",
           checks, failures);
    return failures ? 1 : 0;
}
