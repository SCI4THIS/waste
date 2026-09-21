/* Stage 2 fork/exec fixture driver.  It exercises one captured parent
 * continuation through both failed-exec and successful-exec child exits. */
#include "engine_internal.h"
#include "runtime_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { fprintf(stderr, "FAIL: "); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

enum phase { PHASE_FORK, PHASE_CHILD, PHASE_PARENT };
typedef struct {
    enum phase phase;
    int exec_retry;
    int successful_exec;
    int child_exit;
    int writes;
    int waits;
} probe_state;

static exec_status i32_result(int32_t value, wasm_value *results, int *count) {
    results[0] = i32_value((uint32_t)value); *count = 1; return EXEC_OK;
}

static exec_status host_fork(void *data, const wasm_value *a, int n,
                             wasm_value *r, int *rc, exec_error *e,
                             const waste_exec_engine *caller) {
    (void)a; (void)caller;
    probe_state *s = data;
    if (n) return exec_fail(e, EXEC_ERROR_TRAP, "fork arity");
    if (s->phase == PHASE_FORK) {
        if (e) { memset(e, 0, sizeof(*e)); e->status = EXEC_YIELD;
                 e->yield_reason = EXEC_YIELD_FORK; }
        return EXEC_YIELD;
    }
    return i32_result(s->phase == PHASE_CHILD ? 0 : 42, r, rc);
}

static exec_status host_exec(void *data, const wasm_value *a, int n,
                             wasm_value *r, int *rc, exec_error *e,
                             const waste_exec_engine *caller) {
    (void)a; (void)n; (void)r; (void)rc; (void)caller;
    probe_state *s = data;
    if (!s->exec_retry) {
        if (e) { memset(e, 0, sizeof(*e)); e->status = EXEC_YIELD;
                 e->yield_reason = EXEC_YIELD_EXEC; }
        return EXEC_YIELD;
    }
    return i32_result(-1, r, rc);
}

static exec_status host_write(void *data, const wasm_value *a, int n,
                              wasm_value *r, int *rc, exec_error *e,
                              const waste_exec_engine *caller) {
    probe_state *s = data; uint8_t *bytes;
    if (n != 3 || !caller || !caller->memory || a[1].i32 < 0 ||
        (uint64_t)(uint32_t)a[1].i32 + (uint32_t)a[2].i32 >
            caller->memory->pages * UINT64_C(65536))
        return exec_fail(e, EXEC_ERROR_TRAP, "write arguments");
    bytes = caller->memory->data + (uint32_t)a[1].i32;
    s->writes++;
    if (getenv("WASTE_PROFILE"))
        fprintf(stderr, "WASTE_PROFILE exec fixture write=%.*s\n",
                a[2].i32, (const char *)bytes);
    return i32_result(a[2].i32, r, rc);
}

static exec_status host_wait(void *data, const wasm_value *a, int n,
                             wasm_value *r, int *rc, exec_error *e,
                             const waste_exec_engine *caller) {
    (void)caller;
    probe_state *s = data;
    if (n != 3 || a[0].i32 != 42 || a[2].i32 != 0)
        return exec_fail(e, EXEC_ERROR_TRAP, "wait arguments");
    s->waits++;
    return i32_result(42, r, rc);
}

static exec_status host_exit(void *data, const wasm_value *a, int n,
                             wasm_value *r, int *rc, exec_error *e,
                             const waste_exec_engine *caller) {
    (void)data; (void)r; (void)rc; (void)caller;
    if (n != 1) return exec_fail(e, EXEC_ERROR_TRAP, "exit arity");
    if (e) { memset(e, 0, sizeof(*e)); e->status = EXEC_ERROR_EXIT;
             e->exit_code = a[0].i32; }
    return EXEC_ERROR_EXIT;
}

static uint8_t *read_all(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb"); long n; uint8_t *p;
    if (!f || fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET)) { if (f) fclose(f); return NULL; }
    p = malloc((size_t)n); if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p); fclose(f); return NULL;
    }
    fclose(f); *size = (size_t)n; return p;
}

static int run_case(const uint8_t *bytes, size_t size, int successful) {
    probe_state state = {PHASE_FORK, 0, successful, 0, 0, 0};
    exec_host_import imports[] = {
        {"env", "fork", host_fork, &state, NULL, 0, 0, EXEC_HOST_CONTROL_NONE},
        {"env", "execve", host_exec, &state, NULL, 0, 0, EXEC_HOST_CONTROL_NONE},
        {"env", "waitpid", host_wait, &state, NULL, 0, 0, EXEC_HOST_CONTROL_NONE},
        {"env", "write", host_write, &state, NULL, 0, 0, EXEC_HOST_CONTROL_NONE},
        {"env", "exit", host_exit, &state, NULL, 0, 0, EXEC_HOST_CONTROL_EXIT},
    };
    exec_imports binding = {.functions = imports, .function_count = 5};
    waste_exec_engine *engine = NULL; exec_error error; wasm_value result;
    int count = 0; uint32_t fn = 0; exec_continuation parent;
    exec_continuation_init(&parent);
    memset(&error, 0, sizeof(error));
    CHECK(exec_load_with_imports(bytes, size, &binding, &engine, &error) == EXEC_OK,
          "fixture load: %s", error.message);
    if (!engine) return 0;
    CHECK(exec_find_export(engine, "run", &fn, &error) == EXEC_OK,
          "fixture export: %s", error.message);
    state.phase = PHASE_FORK;
    exec_status st = exec_invoke(engine, fn, NULL, 0, &result, &count, &error);
    CHECK(st == EXEC_YIELD && error.yield_reason == EXEC_YIELD_FORK,
          "fork yield status=%d reason=%d", st, error.yield_reason);
    CHECK(exec_continuation_capture(engine, &parent, &error) == EXEC_OK,
          "parent capture: %s", error.message);
    exec_error rejected;
    memset(&rejected, 0, sizeof(rejected));
    CHECK(exec_continuation_resume(&parent, NULL, 1, EXEC_YIELD_FORK,
                                   &rejected) == EXEC_ERROR_TRAP,
          "wrong-engine resume was accepted");
    if (getenv("WASTE_PROFILE"))
        fprintf(stderr, "WASTE_PROFILE exec fixture capture depth=%u\n",
                parent.active_call_depth);
    state.phase = PHASE_CHILD;
    st = exec_invoke(engine, fn, NULL, 0, &result, &count, &error);
    CHECK(st == EXEC_YIELD && error.yield_reason == EXEC_YIELD_EXEC,
          "exec yield status=%d reason=%d", st, error.yield_reason);
    if (!successful) {
        state.exec_retry = 1;
        st = exec_invoke(engine, fn, NULL, 0, &result, &count, &error);
        CHECK(st == EXEC_ERROR_EXIT && error.exit_code == 127,
              "failed exec child status=%d code=%d", st, error.exit_code);
    } else {
        state.child_exit = 0;
    }
    CHECK(exec_continuation_resume(&parent, engine, 1, EXEC_YIELD_FORK,
                                   &error) == EXEC_OK,
          "parent resume: %s", error.message);
    state.phase = PHASE_PARENT;
    if (getenv("WASTE_PROFILE"))
        fprintf(stderr, "WASTE_PROFILE exec fixture resume phase=parent depth=%u\n",
                parent.active_call_depth);
    st = exec_invoke(engine, fn, NULL, 0, &result, &count, &error);
    CHECK(st == EXEC_OK && result.i32 == 0,
          "parent status=%d result=%d", st, result.i32);
    CHECK(state.waits == 1 && state.writes >= 2,
          "parent lifecycle waits=%d writes=%d", state.waits, state.writes);
    memset(&rejected, 0, sizeof(rejected));
    CHECK(exec_continuation_resume(&parent, engine, 1, EXEC_YIELD_FORK,
                                   &rejected) == EXEC_ERROR_TRAP,
          "double continuation resume was accepted");
    exec_continuation_destroy(&parent); exec_free(engine); return 1;
}

int main(int argc, char **argv) {
    size_t size; const char *path = argc > 1 ? argv[1] :
        "../../tests/c-engine-exec-lifecycle.wasm";
    uint8_t *bytes = read_all(path, &size);
    CHECK(bytes != NULL, "read fixture %s", path);
    if (bytes) { run_case(bytes, size, 0); run_case(bytes, size, 1); free(bytes); }
    printf("C-engine exec lifecycle fixtures: %d checks, %d failures\n",
           checks, failures);
    return failures ? 1 : 0;
}
