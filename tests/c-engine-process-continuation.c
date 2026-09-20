/* c-engine-process-continuation.c — Stage 1 process-control contract probe.
 *
 * This tests the import ABI and observable child/parent status before
 * evaluator continuation and address-space checkpoints exist. The harness
 * selects each process phase; later stages drive both phases from one
 * suspended fork continuation. */

#include "engine_internal.h"
#include "runtime_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int tests;

static void profile_event(const char *event, int pid, int generation) {
    if (getenv("WASTE_PROFILE"))
        fprintf(stderr, "WASTE_PROFILE process event=%s pid=%d generation=%d\n",
                event, pid, generation);
}

#define CHECK(condition, ...) do { \
    tests++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fputc('\n', stderr); \
        failures++; \
    } \
} while (0)

enum probe_phase { PROBE_CHILD, PROBE_PARENT };

typedef struct {
    enum probe_phase phase;
    int child_fork_calls;
    int parent_fork_calls;
    int wait_calls;
    int reap_pid;
    int reap_status;
} process_probe;

static exec_status result_i32(int32_t value, wasm_value *results,
                              int *result_count) {
    results[0].type = WASM_VALTYPE_I32;
    results[0].i32 = value;
    *result_count = 1;
    return EXEC_OK;
}

static exec_status probe_fork(void *data, const wasm_value *args,
                              int arg_count, wasm_value *results,
                              int *result_count, exec_error *error,
                              const waste_exec_engine *caller) {
    (void)args;
    (void)caller;
    process_probe *probe = data;
    if (arg_count != 0) return exec_fail(error, EXEC_ERROR_TRAP,
                                         "fork argument mismatch");
    if (probe->phase == PROBE_CHILD) {
        probe->child_fork_calls++;
        return result_i32(0, results, result_count);
    }
    probe->parent_fork_calls++;
    return result_i32(42, results, result_count);
}

static int probe_range(const waste_exec_engine *caller, uint32_t offset,
                       uint32_t length, uint8_t **out) {
    if (!caller || !caller->memory) return 0;
    uint64_t size = caller->memory->pages * UINT64_C(65536);
    if ((uint64_t)offset + length > size) return 0;
    if (out) *out = caller->memory->data + offset;
    return 1;
}

static exec_status probe_waitpid(void *data, const wasm_value *args,
                                 int arg_count, wasm_value *results,
                                 int *result_count, exec_error *error,
                                 const waste_exec_engine *caller) {
    process_probe *probe = data;
    uint8_t *status_bytes;
    if (arg_count != 3 || !probe_range(caller, (uint32_t)args[1].i32, 4,
                                       &status_bytes))
        return exec_fail(error, EXEC_ERROR_TRAP, "waitpid argument mismatch");
    if (probe->phase != PROBE_PARENT || args[0].i32 != 42 || args[2].i32 != 0)
        return exec_fail(error, EXEC_ERROR_TRAP, "invalid waitpid process");
    probe->wait_calls++;
    probe->reap_pid = args[0].i32;
    probe->reap_status = 127 << 8;
    status_bytes[0] = (uint8_t)probe->reap_status;
    status_bytes[1] = (uint8_t)(probe->reap_status >> 8);
    status_bytes[2] = 0;
    status_bytes[3] = 0;
    return result_i32(args[0].i32, results, result_count);
}

static exec_status unexpected_exit(void *data, const wasm_value *args,
                                   int arg_count, wasm_value *results,
                                   int *result_count, exec_error *error,
                                   const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)results;
    (void)result_count; (void)caller;
    return exec_fail(error, EXEC_ERROR_TRAP, "exit host callback was called");
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

static exec_status invoke_run(waste_exec_engine *engine, exec_error *error,
                              int32_t *value, int *count) {
    uint32_t function;
    wasm_value result;
    exec_status status = exec_find_export(engine, "run", &function, error);
    if (status != EXEC_OK) return status;
    status = exec_invoke(engine, function, NULL, 0, &result, count, error);
    if (status == EXEC_OK && *count == 1) *value = result.i32;
    return status;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "../../tests/c-engine-process-continuation.wasm";
    size_t size;
    uint8_t *bytes = read_file(path, &size);
    process_probe probe = {0};
    exec_host_import imports[] = {
        {"env", "fork", probe_fork, &probe, NULL, 0, 0,
         EXEC_HOST_CONTROL_NONE},
        {"env", "exit", unexpected_exit, &probe, NULL, 0, 0,
         EXEC_HOST_CONTROL_EXIT},
        {"env", "waitpid", probe_waitpid, &probe, NULL, 0, 0,
         EXEC_HOST_CONTROL_NONE},
    };
    exec_imports binding = {.functions = imports, .function_count = 3};
    waste_exec_engine *engine = NULL;
    exec_error error;
    int count = 0;
    int32_t value = 0;

    CHECK(bytes != NULL, "read fixture %s", path);
    if (!bytes) return 1;
    memset(&error, 0, sizeof(error));
    CHECK(exec_load_with_imports(bytes, size, &binding, &engine, &error) == EXEC_OK,
          "load fixture: %s", error.message);
    free(bytes);
    if (!engine) return 1;

    probe.phase = PROBE_CHILD;
    profile_event("fork-begin", 42, 1);
    profile_event("child-resume", 42, 1);
    memset(&error, 0, sizeof(error));
    exec_status status = invoke_run(engine, &error, &value, &count);
    profile_event("child-exit", 42, 1);
    CHECK(status == EXEC_ERROR_EXIT, "child status = %d", status);
    CHECK(error.exit_code == 127, "child exit = %d", error.exit_code);
    CHECK(probe.child_fork_calls == 1 && probe.parent_fork_calls == 0,
          "child fork counts = %d/%d", probe.child_fork_calls,
          probe.parent_fork_calls);
    CHECK(probe.wait_calls == 0, "child wait count = %d", probe.wait_calls);
    CHECK(engine->memory && engine->memory->data[0] == 0xfe &&
          engine->memory->data[1] == 0xca,
          "child marker was not written");

    memset(engine->memory->data, 0, 16);
    probe.phase = PROBE_PARENT;
    profile_event("parent-restore", 1, 1);
    profile_event("parent-resume", 1, 1);
    memset(&error, 0, sizeof(error));
    status = invoke_run(engine, &error, &value, &count);
    profile_event("wait-reap", 42, 1);
    CHECK(status == EXEC_OK && count == 1, "parent invoke status/count = %d/%d",
          status, count);
    CHECK(value == (127 << 8), "parent wait status = %d", value);
    CHECK(probe.parent_fork_calls == 1 && probe.wait_calls == 1 &&
          probe.reap_pid == 42 && probe.reap_status == (127 << 8),
          "parent lifecycle = fork %d wait %d pid %d status %d",
          probe.parent_fork_calls, probe.wait_calls, probe.reap_pid,
          probe.reap_status);
    CHECK(engine->memory->data[0] == 0xef && engine->memory->data[1] == 0xbe,
          "parent marker was not written");

    exec_free(engine);
    printf("C-engine process contract: %d checks, %d failures\n", tests,
           failures);
    return failures ? 1 : 0;
}
