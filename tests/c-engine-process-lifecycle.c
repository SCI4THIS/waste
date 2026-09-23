#include "store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int failures;
static int handler_context_destroyed;

static exec_status handler_step_probe(
        const uint8_t *source, size_t source_size, size_t offset,
        unsigned line, size_t *next_offset, unsigned *next_line,
        void *context) {
    int *steps = (int *)context;
    if (!source || source_size != 5 || offset > source_size || line == 0)
        return EXEC_ERROR_FORMAT;
    if ((*steps)++ == 0) {
        *next_offset = offset;
        *next_line = line;
        return EXEC_YIELD;
    }
    if (*steps == 3) {
        *next_offset = 1;
        *next_line = 1;
        return EXEC_OK;
    }
    *next_offset = 3;
    *next_line = 2;
    return EXEC_OK;
}

static exec_status handler_step_trap(
        const uint8_t *source, size_t source_size, size_t offset,
        unsigned line, size_t *next_offset, unsigned *next_line,
        void *context) {
    (void)source; (void)source_size; (void)offset; (void)line; (void)context;
    *next_offset = 0;
    *next_line = 1;
    return EXEC_ERROR_TRAP;
}

static exec_status handler_step_yield_advance(
        const uint8_t *source, size_t source_size, size_t offset,
        unsigned line, size_t *next_offset, unsigned *next_line,
        void *context) {
    (void)context;
    if (!source || source_size != 5 || offset != 0 || line != 1)
        return EXEC_ERROR_FORMAT;
    *next_offset = 2;
    *next_line = 2;
    return EXEC_YIELD;
}

static void destroy_handler_context(void *context) {
    handler_context_destroyed++;
    free(context);
}

static void check(int condition, const char *message) {
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

int main(void) {
    native_store store;
    memset(&store, 0, sizeof(store));
    store.kernel = posix_kernel_create(0);
    if (!store.kernel) return 2;
    store.processes[0].used = 1;
    store.processes[0].pid = 1;
    store.processes[0].kernel = store.kernel;
    store.process_count = 1;
    store.active_pid = 1;
    store.next_pid = 2;

    int fds[2];
    check(posix_kernel_pipe(store.kernel, fds) == 0, "parent pipe creates");
    uint8_t *handler_source = (uint8_t *)malloc(5);
    if (handler_source) memcpy(handler_source, "(mod)", 5);
    check(native_process_capsule_install_handler(
              native_store_active_capsule(&store), NATIVE_PROCESS_HANDLER_WAST,
              handler_source, 5, NULL, NULL) == 0,
          "parent can own a WAST process handler");
    check(native_process_handler_default_exit_code(EXEC_OK, 9) == 0 &&
          native_process_handler_default_exit_code(EXEC_ERROR_FORMAT, 9) == 126 &&
          native_process_handler_default_exit_code(EXEC_ERROR_UNSUPPORTED, 9) == 126 &&
          native_process_handler_default_exit_code(EXEC_ERROR_TRAP, 9) == 127 &&
          native_process_handler_default_exit_code(EXEC_ERROR_EXIT, 9) == 9,
          "handler result mapping covers shell and explicit exit statuses");
    int child_pid = 0;
    check(native_store_fork_process(&store, &child_pid) == 0 && child_pid == 2,
          "fork allocates child pid");
    check(&store.processes[0].capsule != &store.processes[1].capsule,
          "fork creates distinct parent and child capsules");
    check(native_store_getpid(&store) == 1 && native_store_getppid(&store) == 0,
          "parent pid and ppid are stable");
    check(native_store_active_capsule(&store) != NULL &&
          native_store_active_capsule(&store)->state == NATIVE_PROCESS_RUNNABLE,
          "parent execution capsule is selected");
    check(native_process_capsule_select_entry(
              native_store_active_capsule(&store), NULL, 0, NULL, 0) ==
              -POSIX_EINVAL,
          "entry selection rejects a missing engine");
    {
        native_process_capsule probe;
        waste_exec_engine fake_engine;
        memset(&fake_engine, 0, sizeof(fake_engine));
        fake_engine.func_count = 1;
        native_process_capsule_init(&probe);
        check(native_process_capsule_select_entry(
                  &probe, &fake_engine, 1, NULL, 0) == -POSIX_EINVAL &&
              probe.engine == NULL,
              "entry selection rejects an out-of-range function atomically");
        check(native_process_capsule_select_entry(
                  &probe, &fake_engine, 0, NULL, 0) == 0 &&
              probe.engine == &fake_engine && probe.root_func_idx == 0,
              "entry selection accepts a valid function index");
        native_process_capsule wide_probe;
        waste_exec_engine wide_engine;
        memset(&wide_engine, 0, sizeof(wide_engine));
        wide_engine.import_func_count = UINT32_MAX;
        wide_engine.func_count = 1;
        native_process_capsule_init(&wide_probe);
        check(native_process_capsule_select_entry(
                  &wide_probe, &wide_engine, 0, NULL, 0) == 0,
              "entry validation uses widened function-count arithmetic");
        native_process_capsule size_probe;
        uint8_t *oversized_source = (uint8_t *)malloc(1);
        native_process_capsule_init(&size_probe);
        check(native_process_capsule_install_handler(
                  &size_probe, NATIVE_PROCESS_HANDLER_WAST,
                  oversized_source, NATIVE_EXEC_BYTES_MAX + 1u,
                  NULL, NULL) == -POSIX_EINVAL &&
              size_probe.handler.kind == NATIVE_PROCESS_HANDLER_NONE,
              "handler installer rejects oversized source atomically");
        free(oversized_source);
        uint8_t *unknown_source = (uint8_t *)malloc(1);
        check(native_process_capsule_install_handler(
                  &size_probe, (native_process_handler_kind)99,
                  unknown_source, 1, NULL, NULL) == -POSIX_EINVAL &&
              size_probe.handler.kind == NATIVE_PROCESS_HANDLER_NONE,
              "handler installer rejects unknown handler kinds");
        free(unknown_source);
        {
            native_process_capsule blocked_source;
            native_process_capsule blocked_child;
            uint8_t *blocked_bytes = (uint8_t *)malloc(5);
            if (blocked_bytes) memcpy(blocked_bytes, "(mod)", 5);
            native_process_capsule_init(&blocked_source);
            native_process_capsule_init(&blocked_child);
            check(native_process_capsule_install_handler(
                      &blocked_source, NATIVE_PROCESS_HANDLER_WAST,
                      blocked_bytes, 5, NULL, NULL) == 0 &&
                  native_process_capsule_suspend_handler(
                      &blocked_source, NATIVE_PROCESS_BROWSER_BLOCKED) == 0,
                  "context-free blocked handler can be cloned");
            blocked_source.handler.wait_reason = EXEC_YIELD_SELECT;
            check(native_process_capsule_clone(
                      &blocked_child, &blocked_source) == 1 &&
                  blocked_child.state == NATIVE_PROCESS_RUNNABLE &&
                  blocked_child.handler.wait_reason == EXEC_YIELD_NONE &&
                  blocked_source.state == NATIVE_PROCESS_BROWSER_BLOCKED &&
                  blocked_source.handler.wait_reason == EXEC_YIELD_SELECT,
                  "forked handler child resets without changing parent wait state");
            native_process_capsule_destroy(&blocked_source);
            native_process_capsule_destroy(&blocked_child);
        }
    }
    {
        native_process_capsule probe;
        uint8_t *source = (uint8_t *)malloc(5);
        int attached_steps = 0;
        if (source) memcpy(source, "(mod)", 5);
        native_process_capsule_init(&probe);
        check(native_process_capsule_install_handler(
                  &probe, NATIVE_PROCESS_HANDLER_WAST, source, 5,
                  NULL, NULL) == 0 &&
              native_process_capsule_attach_handler_context(
                  &probe, &attached_steps, NULL) == 0 &&
              native_process_capsule_run_attached_handler_step(
                  &probe, handler_step_probe) == EXEC_YIELD &&
              attached_steps == 1 &&
              probe.handler.stream_offset == 0 &&
              probe.handler.stream_line == 1 &&
              probe.handler.status == EXEC_YIELD,
              "attached handler step uses capsule-owned context");
    check(native_process_capsule_run_handler_step(
                  &probe, handler_step_yield_advance, NULL) == EXEC_YIELD &&
              probe.handler.stream_offset == 2 &&
              probe.handler.stream_line == 2 &&
              probe.handler.status == EXEC_YIELD,
              "explicit handler step context remains available");
        native_process_capsule_clear_handler(&probe);
    }
    {
        native_store driver_store;
        native_process_capsule *driver_capsule;
        int driver_steps = 0;
        uint8_t *source = (uint8_t *)malloc(5);
        memset(&driver_store, 0, sizeof(driver_store));
        driver_store.processes[0].used = 1;
        driver_store.processes[0].pid = 1;
        driver_store.processes[0].capsule.state = NATIVE_PROCESS_RUNNABLE;
        driver_store.process_count = 1;
        driver_store.active_pid = 1;
        if (source) memcpy(source, "(mod)", 5);
        driver_capsule = native_store_active_capsule(&driver_store);
        check(native_process_capsule_install_handler(
                  driver_capsule, NATIVE_PROCESS_HANDLER_WAST, source, 5,
                  &driver_steps, NULL) == 0 &&
              native_store_run_process_handler_step(
                  &driver_store, handler_step_probe) == EXEC_YIELD &&
              driver_steps == 1 && driver_capsule->handler.stream_offset == 0,
              "store dispatches a handler step through the active process");
        {
            const uint8_t *cursor_source = NULL;
            size_t cursor_size = 0;
            size_t cursor_offset = 0;
            unsigned cursor_line = 0;
            check(native_store_process_handler_cursor(
                      &driver_store, &cursor_source, &cursor_size,
                      &cursor_offset, &cursor_line) == 0 &&
                  cursor_source == source && cursor_size == 5 &&
                  cursor_offset == 0 && cursor_line == 1 &&
                  native_store_advance_process_handler(
                      &driver_store, 2, 2) == 0 &&
                  native_store_process_handler_cursor(
                      &driver_store, NULL, &cursor_size, &cursor_offset,
                      &cursor_line) == -POSIX_EINVAL,
                  "store cursor bridge enforces complete output arguments");
            {
                void *stored_context = NULL;
                check(native_store_process_handler_context(
                          &driver_store, &stored_context) == 0 &&
                      stored_context == &driver_steps &&
                      native_store_process_handler_context(
                          &driver_store, NULL) == -POSIX_EINVAL,
                      "store context bridge returns the owned driver context");
            }
            {
                exec_status handler_status = EXEC_ERROR_TRAP;
                int handler_exit_code = -1;
                check(native_store_process_handler_result(
                          &driver_store, &handler_status,
                          &handler_exit_code) == 0 &&
                      handler_status == EXEC_YIELD && handler_exit_code == 0 &&
                      native_store_process_handler_result(
                          &driver_store, &handler_status, NULL) == -POSIX_EINVAL,
                      "store result bridge exposes yielded handler status");
            }
            check(native_store_suspend_process_handler(
                      &driver_store, NATIVE_PROCESS_BROWSER_BLOCKED) == 0 &&
                  native_store_resume_process_handler(&driver_store) == 0 &&
                  driver_capsule->handler.stream_offset == 2 &&
                  driver_capsule->handler.stream_line == 2,
                  "store wait bridge resumes without changing the cursor");
            check(native_store_suspend_process_handler(
                      &driver_store, NATIVE_PROCESS_WAIT_BLOCKED) == 0 &&
                  native_store_resume_process_handler(&driver_store) == 0 &&
                  driver_capsule->state == NATIVE_PROCESS_RUNNABLE,
                  "store readiness bridge covers both blocked states");
            {
                exec_yield_reason reason = EXEC_YIELD_NONE;
                check(native_store_suspend_process_handler_for_yield(
                          &driver_store, EXEC_YIELD_READ) == 0 &&
                      native_store_process_handler_wait_reason(
                          &driver_store, &reason) == 0 &&
                      reason == EXEC_YIELD_READ &&
                      native_store_resume_process_handler(&driver_store) == 0 &&
                      native_store_suspend_process_handler_for_yield(
                          &driver_store, EXEC_YIELD_SELECT) == 0 &&
                      native_store_process_handler_wait_reason(
                          &driver_store, &reason) == 0 &&
                      reason == EXEC_YIELD_SELECT &&
                      native_store_resume_process_handler(&driver_store) == 0 &&
                      native_store_suspend_process_handler_for_yield(
                          &driver_store, EXEC_YIELD_FORK) == -POSIX_EINVAL &&
                      driver_capsule->state == NATIVE_PROCESS_RUNNABLE,
                      "yield bridge accepts external waits and rejects transitions");
            }
            check(native_store_process_handler_cursor(
                      &driver_store, &cursor_source, &cursor_size,
                      &cursor_offset, &cursor_line) == 0 &&
                  cursor_offset == 2 && cursor_line == 2,
                  "store cursor bridge preserves advanced source position");
        }
        native_process_capsule_clear_handler(driver_capsule);
    }
    native_exec_request request;
    memset(&request, 0, sizeof(request));
    request.active = 1;
    request.pid = 1;
    request.pid = 99;
    check(native_store_prepare_process_exec(&store, &request) == -POSIX_EINVAL &&
          native_store_active_capsule(&store)->pending_transition ==
              NATIVE_PROCESS_TRANSITION_NONE,
          "exec preparation rejects a request for another pid atomically");
    request.pid = 1;
    check(native_store_prepare_process_exec(&store, &request) == 0 &&
          native_store_active_capsule(&store)->pending_transition ==
              NATIVE_PROCESS_TRANSITION_EXEC,
          "exec transition is prepared on parent capsule");
    check(native_store_prepare_process_exec(&store, &request) == -POSIX_EBUSY &&
          native_store_active_capsule(&store)->pending_transition ==
              NATIVE_PROCESS_TRANSITION_EXEC,
          "duplicate exec preparation preserves the active transition");
    native_store_abort_process_exec(&store);
    check(native_store_active_capsule(&store)->pending_transition ==
              NATIVE_PROCESS_TRANSITION_NONE,
          "exec transition abort clears pending state");
    check(native_store_set_active_process(&store, child_pid) == 0,
          "child can be selected");
    check(native_store_getpid(&store) == child_pid && native_store_getppid(&store) == 1,
          "child observes pid and parent pid");
    check(native_store_active_capsule(&store) != NULL &&
          native_store_active_capsule(&store)->state == NATIVE_PROCESS_RUNNABLE,
          "child execution capsule is selected");
    check(native_store_active_capsule(&store)->handler.kind ==
              NATIVE_PROCESS_HANDLER_WAST &&
          native_store_active_capsule(&store)->handler.source_size == 5 &&
          memcmp(native_store_active_capsule(&store)->handler.source,
                 "(mod)", 5) == 0,
          "fork clones WAST handler source and cursor state");
    check(native_process_capsule_set_handler_exit_code(
              native_store_active_capsule(&store), 9) == 0 &&
          native_store_active_capsule(&store)->handler.exit_code == 9 &&
          store.processes[0].capsule.handler.exit_code == 0,
          "child handler exit code is isolated from parent");
    check(native_process_capsule_set_handler_exit_code(
              native_store_active_capsule(&store), 256) == -POSIX_EINVAL &&
          native_store_active_capsule(&store)->handler.exit_code == 9,
          "handler rejects an exit code outside the shell byte range");
    check(native_process_capsule_set_handler_exit_code(
              native_store_active_capsule(&store), -1) == -POSIX_EINVAL &&
          native_store_active_capsule(&store)->handler.exit_code == 9,
          "handler rejects a negative exit code");
    check(native_process_handler_default_exit_code(EXEC_OK, 9) == 0 &&
              native_process_handler_default_exit_code(
                  EXEC_ERROR_FORMAT, 9) == 126 &&
              native_process_handler_default_exit_code(
                  EXEC_ERROR_EXIT, 9) == 9 &&
              native_process_handler_default_exit_code(
                  EXEC_ERROR_TRAP, 9) == 127,
          "handler completion maps success, format, exit, and trap statuses");
    store.processes[0].capsule.pending_result = 44;
    store.processes[0].capsule.pending_result_valid = 1;
    check(native_store_complete_process_handler_and_wake_default(
              &store, EXEC_ERROR_FORMAT) == -POSIX_EBUSY &&
              native_store_active_capsule(&store)->handler.kind ==
                  NATIVE_PROCESS_HANDLER_WAST &&
              !store.processes[1].zombie &&
              handler_context_destroyed == 0,
          "handler wake refuses to overwrite a queued parent result");
    store.processes[0].capsule.pending_result_valid = 0;
    store.processes[0].capsule.pending_transition =
        NATIVE_PROCESS_TRANSITION_EXEC;
    check(native_store_complete_process_handler_and_wake_default(
              &store, EXEC_ERROR_FORMAT) == -POSIX_EBUSY &&
              native_store_active_capsule(&store)->handler.kind ==
                  NATIVE_PROCESS_HANDLER_WAST &&
              !store.processes[1].zombie &&
              handler_context_destroyed == 0 &&
              store.processes[0].capsule.pending_transition ==
                  NATIVE_PROCESS_TRANSITION_EXEC,
          "handler wake refuses to overwrite a parent transition");
    store.processes[0].capsule.pending_transition =
        NATIVE_PROCESS_TRANSITION_NONE;
    store.processes[0].zombie = 1;
    check(native_store_complete_process_handler_and_wake_default(
              &store, EXEC_ERROR_FORMAT) == -POSIX_EBUSY &&
              native_store_active_capsule(&store)->handler.kind ==
                  NATIVE_PROCESS_HANDLER_WAST &&
              !store.processes[1].zombie &&
              handler_context_destroyed == 0,
          "handler wake refuses a disappeared parent atomically");
    store.processes[0].zombie = 0;
    {
        const uint8_t *handler_source = NULL;
        size_t handler_size = 0;
        size_t handler_offset = 0;
        unsigned handler_line = 0;
        check(native_process_capsule_handler_cursor(
                  native_store_active_capsule(&store), &handler_source,
                  &handler_size, &handler_offset, &handler_line) == 0 &&
              handler_source && handler_size == 5 && handler_offset == 0 &&
              handler_line == 1,
              "handler cursor exposes owned source position");
        check(native_process_capsule_advance_handler(
                  native_store_active_capsule(&store), 3, 2) == 0 &&
              native_store_active_capsule(&store)->handler.stream_offset == 3 &&
              native_store_active_capsule(&store)->handler.stream_line == 2,
              "handler cursor advances monotonically");
        check(store.processes[0].capsule.handler.stream_offset == 0 &&
              store.processes[0].capsule.handler.stream_line == 1,
              "child cursor advance leaves parent cursor unchanged");
        check(native_process_capsule_advance_handler(
                  native_store_active_capsule(&store), 2, 2) == -POSIX_EINVAL &&
              native_store_active_capsule(&store)->handler.stream_offset == 3,
              "handler cursor rejects backward movement");
        check(native_process_capsule_advance_handler(
                  native_store_active_capsule(&store), 6, 3) == -POSIX_EINVAL &&
              native_store_active_capsule(&store)->handler.stream_offset == 3,
              "handler cursor rejects movement beyond source");
        check(native_process_capsule_suspend_handler(
                  native_store_active_capsule(&store),
                  NATIVE_PROCESS_BROWSER_BLOCKED) == 0 &&
              native_store_active_capsule(&store)->state ==
                  NATIVE_PROCESS_BROWSER_BLOCKED &&
              native_store_active_capsule(&store)->handler.stream_offset == 3,
              "handler yield preserves cursor at browser wait");
        check(native_store_complete_process_handler(
                  &store, EXEC_OK, 0) == -POSIX_EBUSY &&
              native_store_active_capsule(&store)->state ==
                  NATIVE_PROCESS_BROWSER_BLOCKED &&
              native_store_active_capsule(&store)->handler.kind ==
                  NATIVE_PROCESS_HANDLER_WAST,
              "blocked handler cannot complete before resume");
        check(native_process_capsule_resume_handler(
                  native_store_active_capsule(&store)) == 0 &&
              native_store_active_capsule(&store)->state ==
                  NATIVE_PROCESS_RUNNABLE &&
              native_store_active_capsule(&store)->handler.stream_offset == 3,
              "handler resumes with the same cursor");
        check(native_process_capsule_suspend_handler(
                  native_store_active_capsule(&store),
                  NATIVE_PROCESS_WAIT_BLOCKED) == 0 &&
              native_store_active_capsule(&store)->state ==
                  NATIVE_PROCESS_WAIT_BLOCKED,
              "handler enters wait-blocked state");
        check(native_process_capsule_resume_handler(
                  native_store_active_capsule(&store)) == 0 &&
              native_store_active_capsule(&store)->state ==
                  NATIVE_PROCESS_RUNNABLE &&
              native_store_active_capsule(&store)->handler.stream_offset == 3,
              "handler resumes after wait-blocked state");
        {
            int steps = 0;
            check(native_process_capsule_suspend_handler(
                      native_store_active_capsule(&store),
                      NATIVE_PROCESS_BROWSER_BLOCKED) == 0 &&
                  native_process_capsule_run_handler_step(
                      native_store_active_capsule(&store),
                      handler_step_probe, &steps) == EXEC_ERROR_FORMAT &&
                  steps == 0 &&
                  native_store_active_capsule(&store)->handler.stream_offset == 3,
                  "blocked handler cannot dispatch a command step");
            check(native_process_capsule_suspend_handler(
                      native_store_active_capsule(&store),
                      NATIVE_PROCESS_WAIT_BLOCKED) == -POSIX_EINVAL &&
                  native_store_active_capsule(&store)->state ==
                      NATIVE_PROCESS_BROWSER_BLOCKED,
                  "blocked handler rejects a second suspend transition");
            check(native_process_capsule_resume_handler(
                      native_store_active_capsule(&store)) == 0,
                  "handler resumes before command-step dispatch");
            check(native_process_capsule_resume_handler(
                      native_store_active_capsule(&store)) == -POSIX_EINVAL &&
                  native_store_active_capsule(&store)->state ==
                      NATIVE_PROCESS_RUNNABLE,
                  "runnable handler rejects a duplicate resume");
            check(native_process_capsule_run_handler_step(
                      native_store_active_capsule(&store),
                      handler_step_probe, &steps) == EXEC_YIELD &&
                  native_store_active_capsule(&store)->handler.stream_offset == 3 &&
                  native_store_active_capsule(&store)->handler.status == EXEC_YIELD,
                  "handler step can yield without losing cursor");
            check(native_process_capsule_run_handler_step(
                      native_store_active_capsule(&store),
                      handler_step_probe, &steps) == EXEC_OK && steps == 2 &&
                  native_store_active_capsule(&store)->handler.stream_offset == 3 &&
                  native_store_active_capsule(&store)->handler.stream_line == 2 &&
                  native_store_active_capsule(&store)->handler.status == EXEC_OK,
                  "handler step commits cursor after command completion");
            check(native_process_capsule_run_handler_step(
                      native_store_active_capsule(&store),
                      handler_step_probe, &steps) == EXEC_ERROR_FORMAT &&
                  native_store_active_capsule(&store)->handler.stream_offset == 3 &&
                  native_store_active_capsule(&store)->handler.stream_line == 2 &&
                  native_store_active_capsule(&store)->handler.status ==
                      EXEC_ERROR_FORMAT,
                  "invalid handler step advance leaves cursor unchanged");
            check(native_process_capsule_run_handler_step(
                      native_store_active_capsule(&store),
                      handler_step_trap, NULL) == EXEC_ERROR_TRAP &&
                  native_store_active_capsule(&store)->handler.stream_offset == 3 &&
                  native_store_active_capsule(&store)->handler.status ==
                      EXEC_ERROR_TRAP,
                  "failed handler step records trap without cursor commit");
            check(store.processes[0].capsule.handler.status == EXEC_OK &&
                  store.processes[0].capsule.handler.exit_code == 0,
                  "child handler result remains isolated from parent");
        }
    }
    int *handler_context = (int *)malloc(sizeof(*handler_context));
    if (handler_context) *handler_context = 7;
    check(native_process_capsule_attach_handler_context(
              native_store_active_capsule(&store), handler_context,
              destroy_handler_context) == 0 &&
              native_store_active_capsule(&store)->handler.context == handler_context,
          "child attaches runtime handler context ownership");
    {
        native_process_capsule clone;
        native_process_capsule_init(&clone);
        check(native_process_capsule_clone(
                  &clone, native_store_active_capsule(&store)) == 0 &&
                  native_store_active_capsule(&store)->handler.context ==
                      handler_context && handler_context_destroyed == 0,
              "capsules reject cloning an attached handler context");
        native_process_capsule_destroy(&clone);
    }
    check(posix_kernel_write(store.kernel, fds[1], "x", 1) == 1,
          "child inherited shared open-file description");
    native_store_active_capsule(&store)->pending_result = 77;
    native_store_active_capsule(&store)->pending_result_valid = 1;
    check(native_store_complete_process_handler(&store, EXEC_OK, 0) ==
              -POSIX_EINVAL &&
          native_store_active_capsule(&store)->handler.kind ==
              NATIVE_PROCESS_HANDLER_WAST &&
          native_store_active_capsule(&store)->pending_result == 77,
          "handler completion does not overwrite a queued result");
    native_store_active_capsule(&store)->pending_result_valid = 0;
    check(native_store_complete_process_handler(&store, EXEC_ERROR_EXIT, 256) ==
              -POSIX_EINVAL &&
          native_store_active_capsule(&store)->handler.kind ==
              NATIVE_PROCESS_HANDLER_WAST &&
          !native_store_active_capsule(&store)->pending_result_valid,
          "handler completion rejects an out-of-range exit code atomically");
    native_store_active_capsule(&store)->handler.status = EXEC_ERROR_EXIT;
    native_store_active_capsule(&store)->handler.exit_code = 9;
    check(native_store_complete_process_handler_result_and_wake(&store) == 0 &&
              native_store_getpid(&store) == 1 &&
              store.processes[1].capsule.handler.kind ==
                  NATIVE_PROCESS_HANDLER_NONE &&
              store.processes[1].capsule.state == NATIVE_PROCESS_EXITED &&
              store.processes[1].capsule.pending_transition ==
                  NATIVE_PROCESS_TRANSITION_EXIT &&
              native_store_active_capsule(&store)->pending_result_valid &&
              native_store_active_capsule(&store)->pending_result == child_pid &&
              native_store_active_capsule(&store)->pending_transition ==
                  NATIVE_PROCESS_TRANSITION_WAKE &&
              store.processes[1].capsule.pending_error == EXEC_ERROR_EXIT &&
              store.processes[1].exit_status == (9 << 8),
          "handler completion publishes explicit exit before waking parent");
    {
        int helper_wake = 0;
        check(native_store_take_process_wake(&store, &helper_wake) == 0 &&
                  helper_wake == child_pid &&
                  native_store_active_capsule(&store)->pending_transition ==
                      NATIVE_PROCESS_TRANSITION_NONE,
              "handler completion wake is consumed exactly once");
        check(native_store_complete_process_handler_and_wake_default(
                  &store, EXEC_OK) == -POSIX_EINVAL &&
                  native_store_getpid(&store) == 1 &&
                  !native_store_active_capsule(&store)->pending_result_valid,
              "completed handler cannot be completed or woken twice");
    }
    check(store.processes[1].zombie &&
              store.processes[1].exit_status == (9 << 8),
          "completed child remains a reapable zombie");
    check(native_store_set_active_process(&store, 1) == 0,
          "parent is selected before wake validation");
    store.processes[0].capsule.state = NATIVE_PROCESS_BROWSER_BLOCKED;
    {
        int wake_result = 0;
        check(native_store_take_process_wake(&store, &wake_result) ==
                  -POSIX_EINVAL &&
              native_store_active_capsule(&store)->pending_transition ==
                  NATIVE_PROCESS_TRANSITION_NONE,
              "parent rejects wake consumption before child wake");
    }
    check(native_store_wake_process(&store, 1, child_pid) == 0 &&
          native_store_getpid(&store) == 1 &&
          native_store_active_capsule(&store)->state ==
              NATIVE_PROCESS_RUNNABLE &&
          native_store_active_capsule(&store)->pending_result_valid &&
          native_store_active_capsule(&store)->pending_result == child_pid &&
          native_store_active_capsule(&store)->pending_transition ==
              NATIVE_PROCESS_TRANSITION_WAKE,
          "child exit wakes parent with child pid before reap");
    check(native_store_prepare_process_exec(&store, &request) == -POSIX_EBUSY &&
          native_store_active_capsule(&store)->pending_transition ==
              NATIVE_PROCESS_TRANSITION_WAKE,
          "exec preparation cannot overwrite a queued wake");
    check(native_store_wake_process(&store, child_pid, 1) == -POSIX_ECHILD,
          "zombie child rejects a wake transition");
    check(native_store_wake_process(&store, 1, child_pid) == -POSIX_EBUSY,
          "parent rejects a duplicate pending wake");
    {
        int wake_result = 0;
        check(native_store_take_process_wake(&store, NULL) == -POSIX_EINVAL,
              "wake result rejects a missing destination");
        check(native_store_take_process_wake(&store, &wake_result) == 0 &&
              wake_result == child_pid &&
              !native_store_active_capsule(&store)->pending_result_valid &&
              native_store_active_capsule(&store)->pending_transition ==
                  NATIVE_PROCESS_TRANSITION_NONE,
              "parent consumes wake result before reap");
        check(native_store_take_process_wake(&store, &wake_result) ==
                  -POSIX_EINVAL &&
              native_store_active_capsule(&store)->pending_transition ==
                  NATIVE_PROCESS_TRANSITION_NONE,
              "wake result cannot be consumed twice");
        native_store_active_capsule(&store)->pending_result = 77;
        native_store_active_capsule(&store)->pending_result_valid = 1;
        native_store_active_capsule(&store)->pending_transition =
            NATIVE_PROCESS_TRANSITION_EXEC;
        check(native_store_take_process_wake(&store, &wake_result) == 0 &&
              wake_result == 77 &&
              !native_store_active_capsule(&store)->pending_result_valid &&
              native_store_active_capsule(&store)->pending_transition ==
                  NATIVE_PROCESS_TRANSITION_EXEC,
              "wake consumption preserves an unrelated transition label");
        native_store_active_capsule(&store)->pending_transition =
            NATIVE_PROCESS_TRANSITION_NONE;
    }

    int status = 0;
    check(native_store_wait_process(&store, child_pid, 0, &status) == child_pid,
          "parent reaps requested child");
    check(handler_context_destroyed == 1,
          "reaping child destroys attached handler context exactly once");
    check(status == (9 << 8), "wait status uses POSIX exit encoding");
    check(native_store_wait_process(&store, child_pid, 0, &status) == -POSIX_ECHILD,
          "child can only be reaped once");
    char byte = 0;
    check(posix_kernel_read(store.kernel, fds[0], &byte, 1) == 1 && byte == 'x',
          "parent descriptors remain usable after reaping");
    check(native_store_wait_process(&store, -1, 0, &status) == -POSIX_ECHILD,
          "wait without children reports ECHILD");
    check(native_store_wait_process(&store, 0, 0, &status) == -POSIX_ECHILD,
          "empty process-group wait reports ECHILD");

    int group_child = 0;
    check(native_store_fork_process(&store, &group_child) == 0,
          "fork for group signal status");
    check(posix_kernel_setpgid(store.processes[1].kernel, 7) == 7,
          "assign child signal group");
    check(native_store_signal_process(&store, group_child, 15) == 0,
          "group member receives signal");
    check(native_store_set_active_process(&store, 1) == 0,
          "restore parent after group signal");
    check(native_store_wait_process(&store, group_child, 0, &status) == group_child &&
          status == ((128 + 15) << 8),
          "group signal status is reapable");

    posix_kernel_destroy(store.kernel);
    printf("C-engine process lifecycle: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
