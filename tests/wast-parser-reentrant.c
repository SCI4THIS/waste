#include "wast/runner.h"
#include "wat/context.h"
#include "wat/builder.h"
#include "wast/stream.h"
#include "source.h"
#include "store.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { THREAD_COUNT = 4, ITERATION_COUNT = 20 };

typedef struct {
    int index;
    atomic_int *start;
    int passed;
} parser_thread;

static const char *const valid_sources[] = {
    "(; outer (; nested ;) comment ;) "
    "(module $a (@ignored) (func $f (export \"value-a\") (result i32) "
    "i32.const 11)) "
    "(assert_return (invoke $a \"value-a\") (i32.const 11))",
    "(module $b (type $t (func (result i64))) "
    "(func $g (type $t) (export \"value-b\") i64.const 22)) "
    "(assert_return (invoke $b \"value-b\") (i64.const 22))",
};

static int parse_inline_fields(void) {
    static const char source[] =
        ";; leading comment\n"
        "(; nested (; comment ;) ;)\n"
        "(@ignored)\n"
        "(func (export \"value\") (result f32) f32.const 16777217)\n"
        "(memory 1)\n"
        "(data (i32.const 0) \"A\\00B\")";
    wast_script script;
    int result = wast_parse_bytes(source, sizeof(source) - 1, &script);
    int passed = result == 0 && script.error[0] == '\0' &&
                 script.command_count == 1 && script.group_count == 1 &&
                 script.groups[0].module.data_count == 1 &&
                 script.groups[0].module.data[0].len == 3 &&
                 script.groups[0].module.data[0].bytes[0] == 'A' &&
                 script.groups[0].module.data[0].bytes[1] == 0 &&
                 script.groups[0].module.data[0].bytes[2] == 'B' &&
                 script.parse_context == NULL;
    wast_script_free(&script);
    return passed;
}

static int parse_raw_binary(void) {
    static const char source[] =
        "(module binary \"\\00asm\\01\\00\\00\\00\")";
    static const uint8_t expected[] = {0, 'a', 's', 'm', 1, 0, 0, 0};
    wast_script script;
    int result = wast_parse_bytes(source, sizeof(source) - 1, &script);
    int passed = result == 0 && script.error[0] == '\0' &&
                 script.group_count == 1 &&
                 script.groups[0].raw_module.kind == WAST_RAW_BINARY &&
                 script.groups[0].raw_module.length == sizeof(expected) &&
                 memcmp(script.groups[0].raw_module.bytes, expected,
                        sizeof(expected)) == 0 &&
                 script.parse_context == NULL;
    wast_script_free(&script);
    return passed;
}

static int parse_annotation_only_inline(void) {
    static const char source[] = "(@custom \"section\")";
    wast_script script;
    int result = wast_parse_bytes(source, sizeof(source) - 1, &script);
    int passed = result == 0 && script.error[0] == '\0' &&
                 script.command_count == 1 && script.group_count == 1 &&
                 script.groups[0].module.func_count == 0 &&
                 script.parse_context == NULL;
    wast_script_free(&script);
    return passed;
}

typedef struct {
    int count;
    int parse_errors;
    int assertions;
    size_t last_offset;
    int ordered;
} stream_probe;

static int probe_stream_command(wast_stream_command_kind kind,
                                const char *bytes, size_t length,
                                size_t offset, unsigned line,
                                wast_script *parsed, void *opaque) {
    stream_probe *probe = (stream_probe *)opaque;
    static const wast_stream_command_kind expected_kinds[] = {
        WAST_STREAM_MODULE, WAST_STREAM_ASSERTION, WAST_STREAM_ASSERTION
    };
    static const unsigned expected_lines[] = {1, 2, 4};
    (void)bytes;
    if (probe->count >= 3 || kind != expected_kinds[probe->count] ||
        line != expected_lines[probe->count] || length == 0 ||
        (probe->count > 0 && offset <= probe->last_offset))
        probe->ordered = 0;
    probe->last_offset = offset;
    if (parsed->error[0]) probe->parse_errors++;
    probe->assertions += parsed->assertion_count;
    probe->count++;
    return 0;
}

static int parse_stream_recovery(void) {
    static const char source[] =
        "(module)\n"
        "(assert_return)\n"
        ";; recovery must preserve this boundary\n"
        "(assert_return (invoke \"later ) text\") (i32.const 1))";
    stream_probe probe = {0, 0, 0, 0, 1};
    int commands = wast_stream_run(source, sizeof(source) - 1,
                                   probe_stream_command, &probe);
    int passed = commands == 3 && probe.count == 3 &&
                 probe.parse_errors == 1 && probe.assertions == 1 &&
                 probe.ordered;
    if (!passed)
        fprintf(stderr,
                "stream stats commands=%d count=%d errors=%d assertions=%d ordered=%d\n",
                commands, probe.count, probe.parse_errors, probe.assertions,
                probe.ordered);
    return passed;
}

typedef struct {
    int count;
    int ordered;
} shebang_stream_probe;

static int shebang_stream_callback(wast_stream_command_kind kind,
                                    const char *bytes, size_t length,
                                    size_t offset, unsigned line,
                                    wast_script *parsed, void *opaque) {
    shebang_stream_probe *probe = (shebang_stream_probe *)opaque;
    (void)kind;
    (void)bytes;
    (void)parsed;
    if (length == 0 || line != (unsigned)(probe->count + 2) || offset < 12)
        probe->ordered = 0;
    probe->count++;
    return 0;
}

static int shebang_stream_offsets(void) {
    static const char source[] =
        "#!/bin/wast\n"
        "(module)\n"
        "(assert_return)\n";
    wast_stream stream;
    shebang_stream_probe probe = {0, 1};
    wast_stream_init(&stream, source, sizeof(source) - 1);
    for (;;) {
        int status = wast_stream_next(&stream, shebang_stream_callback, &probe);
        if (status <= 0) {
            wast_stream_destroy(&stream);
            return status == 0 && probe.count == 2 && probe.ordered;
        }
    }
}

static int shebang_stream_position(void) {
    static const char source[] =
        "#!/bin/wast\n(module)\n(assert_return)\n";
    wast_stream stream;
    size_t offset = 0;
    unsigned line = 0;
    wast_stream_init(&stream, source, sizeof(source) - 1);
    if (wast_stream_position(&stream, &offset, &line) != 0 ||
        offset != 12 || line != 2) {
        wast_stream_destroy(&stream);
        return 0;
    }
    if (wast_stream_next(&stream, shebang_stream_callback, &(shebang_stream_probe){0, 1}) != 1)
        return 0;
    int passed = wast_stream_position(&stream, &offset, &line) == 0 &&
                 offset >= 12 && line >= 2;
    wast_stream_destroy(&stream);
    return passed;
}

static int handler_initial_cursor(void) {
    static const char *const sources[] = {
        "#!/bin/wast\n(module)\n",
        "#!/bin/wast\r\n(module)"
    };
    static const size_t expected_offsets[] = {12, 13};
    for (size_t variant = 0; variant < 2; variant++) {
        const char *source = sources[variant];
        size_t source_size = strlen(source);
        native_process_capsule capsule;
        wast_stream stream;
        const uint8_t *owned_source = NULL;
        uint8_t *copy = malloc(source_size);
        size_t offset = 0;
        unsigned line = 0;
        size_t cursor_offset = 0;
        unsigned cursor_line = 0;
        if (!copy) return 0;
        memcpy(copy, source, source_size);
        native_process_capsule_init(&capsule);
        wast_stream_init(&stream, source, source_size);
        if (wast_stream_position(&stream, &offset, &line) != 0 ||
            offset != expected_offsets[variant] || line != 2 ||
            native_process_capsule_install_handler(
                &capsule, NATIVE_PROCESS_HANDLER_WAST, copy,
                source_size, NULL, NULL) != 0 ||
            native_process_capsule_advance_handler(
                &capsule, offset, line) != 0 ||
            native_process_capsule_handler_cursor(
                &capsule, &owned_source, NULL, &cursor_offset,
                &cursor_line) != -POSIX_EINVAL ||
            native_process_capsule_handler_cursor(
                &capsule, &owned_source, &(size_t){0}, &cursor_offset,
                &cursor_line) != 0 || owned_source != copy ||
            cursor_offset != expected_offsets[variant] || cursor_line != 2) {
            wast_stream_destroy(&stream);
            native_process_capsule_destroy(&capsule);
            return 0;
        }
        {
            shebang_stream_probe probe = {0, 1};
            size_t next_offset = 0;
            unsigned next_line = 0;
            if (wast_stream_next(&stream, shebang_stream_callback, &probe) != 1 ||
                wast_stream_position(&stream, &next_offset, &next_line) != 0 ||
                next_offset <= offset || next_line < 2 ||
                native_process_capsule_advance_handler(
                    &capsule, next_offset, next_line) != 0 ||
                native_process_capsule_handler_cursor(
                    &capsule, &owned_source, &(size_t){0}, &cursor_offset,
                    &cursor_line) != 0 || cursor_offset != next_offset ||
                cursor_line != next_line) {
                wast_stream_destroy(&stream);
                native_process_capsule_destroy(&capsule);
                return 0;
            }
        }
        wast_stream_destroy(&stream);
        native_process_capsule_destroy(&capsule);
    }
    return 1;
}

typedef struct {
    wast_stream stream;
    int commands;
} handler_stream_driver;

static int handler_stream_command(wast_stream_command_kind kind,
                                  const char *bytes, size_t length,
                                  size_t offset, unsigned line,
                                  wast_script *parsed, void *opaque) {
    handler_stream_driver *driver = (handler_stream_driver *)opaque;
    (void)kind;
    (void)bytes;
    (void)length;
    (void)offset;
    (void)line;
    (void)parsed;
    driver->commands++;
    return 0;
}

static exec_status handler_stream_step(
        const uint8_t *source, size_t source_size, size_t offset,
        unsigned line, size_t *next_offset, unsigned *next_line,
        void *opaque) {
    handler_stream_driver *driver = (handler_stream_driver *)opaque;
    int status;
    (void)source;
    (void)source_size;
    (void)offset;
    (void)line;
    status = wast_stream_next(&driver->stream, handler_stream_command, driver);
    if (status < 0 || wast_stream_position(&driver->stream,
                                            next_offset, next_line) != 0)
        return EXEC_ERROR_FORMAT;
    return status == 0 ? EXEC_OK : EXEC_YIELD;
}

static int store_handler_stream_resume(void) {
    static const char source[] =
        "#!/bin/wast\n(module)\n(assert_return)\n";
    native_store store;
    native_process_capsule *capsule;
    handler_stream_driver driver;
    uint8_t *copy = malloc(sizeof(source) - 1);
    const uint8_t *owned_source = NULL;
    size_t owned_size = 0;
    size_t offset = 0;
    unsigned line = 0;
    size_t cursor_offset = 0;
    unsigned cursor_line = 0;
    int handler_installed = 0;
    int passed = 0;
    memset(&store, 0, sizeof(store));
    memset(&driver, 0, sizeof(driver));
    if (!copy) return 0;
    memcpy(copy, source, sizeof(source) - 1);
    wast_stream_init(&driver.stream, source, sizeof(source) - 1);
    if (wast_stream_position(&driver.stream, &offset, &line) != 0)
        goto done;
    store.processes[0].used = 1;
    store.processes[0].pid = 1;
    store.processes[0].capsule.state = NATIVE_PROCESS_RUNNABLE;
    store.process_count = 1;
    store.active_pid = 1;
    capsule = native_store_active_capsule(&store);
    if (!capsule || native_process_capsule_install_handler(
            capsule, NATIVE_PROCESS_HANDLER_WAST, copy, sizeof(source) - 1,
            &driver, NULL) != 0)
        goto done;
    copy = NULL;
    handler_installed = 1;
    if (native_store_advance_process_handler(&store, offset, line) != 0)
        goto cleanup_handler;
    if (native_store_run_process_handler_step(&store, handler_stream_step) !=
            EXEC_YIELD || driver.commands != 1 ||
        native_store_process_handler_cursor(
            &store, &owned_source, &owned_size, &cursor_offset,
            &cursor_line) != 0 || owned_source == NULL || owned_size !=
            sizeof(source) - 1 || cursor_offset <= offset || cursor_line < 2)
        goto cleanup_handler;
    if (native_store_run_process_handler_step(&store, handler_stream_step) !=
            EXEC_YIELD || driver.commands != 2)
        goto cleanup_handler;
    if (native_store_run_process_handler_step(&store, handler_stream_step) !=
            EXEC_OK || driver.commands != 2)
        goto cleanup_handler;
    if (native_store_process_handler_cursor(
            &store, &owned_source, &owned_size, &cursor_offset,
            &cursor_line) != 0 || cursor_offset != sizeof(source) - 1 ||
        cursor_line < 4)
        goto cleanup_handler;
    passed = 1;
cleanup_handler:
    if (handler_installed) native_process_capsule_clear_handler(capsule);
done:
    free(copy);
    wast_stream_destroy(&driver.stream);
    return passed;
}

static int store_handler_stream_completion(void) {
    static const char source[] =
        "#!/bin/wast\n(module)\n(assert_return)\n";
    native_store store;
    native_process_capsule *child_capsule;
    handler_stream_driver driver;
    uint8_t *copy = malloc(sizeof(source) - 1);
    size_t offset = 0;
    unsigned line = 0;
    int child_pid = 0;
    int wait_status = -1;
    int passed = 0;
    memset(&store, 0, sizeof(store));
    memset(&driver, 0, sizeof(driver));
    store.kernel = posix_kernel_create(0);
    if (!copy || !store.kernel) goto done;
    memcpy(copy, source, sizeof(source) - 1);
    store.processes[0].used = 1;
    store.processes[0].pid = 1;
    store.processes[0].kernel = store.kernel;
    native_process_capsule_init(&store.processes[0].capsule);
    store.process_count = 1;
    store.active_pid = 1;
    store.next_pid = 2;
    if (native_store_fork_process(&store, &child_pid) != 0 ||
        native_store_set_active_process(&store, child_pid) != 0)
        goto done;
    store.processes[0].capsule.state = NATIVE_PROCESS_BROWSER_BLOCKED;
    child_capsule = native_store_active_capsule(&store);
    wast_stream_init(&driver.stream, source, sizeof(source) - 1);
    if (wast_stream_position(&driver.stream, &offset, &line) != 0 ||
        native_process_capsule_install_handler(
            child_capsule, NATIVE_PROCESS_HANDLER_WAST, copy,
            sizeof(source) - 1, &driver, NULL) != 0 ||
        native_store_advance_process_handler(&store, offset, line) != 0)
        goto done;
    copy = NULL;
    if (native_store_run_process_handler_step(
            &store, handler_stream_step) != EXEC_YIELD ||
        native_store_suspend_process_handler_for_yield(
            &store, EXEC_YIELD_READ) != 0 ||
        native_store_resume_process_handler(&store) != 0 ||
        native_store_run_process_handler_step(
            &store, handler_stream_step) != EXEC_YIELD ||
        native_store_suspend_process_handler_for_yield(
            &store, EXEC_YIELD_SELECT) != 0 ||
        native_store_resume_process_handler(&store) != 0 ||
        native_store_run_process_handler_step(
            &store, handler_stream_step) != EXEC_OK ||
        native_store_complete_process_handler_result_and_wake(&store) != 0)
        goto done;
    if (!store.processes[1].zombie || !store.processes[0].capsule.pending_result_valid ||
        native_store_set_active_process(&store, 1) != 0 ||
        native_store_take_process_wake(&store, &wait_status) != 0 ||
        wait_status != child_pid ||
        native_store_wait_process(&store, child_pid, 0, &wait_status) != child_pid ||
        wait_status != 0)
        goto done;
    passed = 1;
done:
    free(copy);
    wast_stream_destroy(&driver.stream);
    if (store.kernel) {
        for (int i = 0; i < NATIVE_PROCESS_MAX; i++)
            if (store.processes[i].used)
                native_process_capsule_destroy(&store.processes[i].capsule);
        posix_kernel_destroy(store.kernel);
    }
    return passed;
}

static int vfs_snapshot_stream(void) {
    static const uint8_t source[] =
        "#!/bin/wast\n(module)\n(assert_return)\n";
    posix_kernel *kernel = posix_kernel_create(0);
    posix_path_metadata metadata;
    uint8_t *snapshot = NULL;
    size_t snapshot_size = 0;
    handler_stream_driver driver;
    int status;
    int passed = 0;
    memset(&metadata, 0, sizeof(metadata));
    memset(&driver, 0, sizeof(driver));
    if (!kernel) return 0;
    metadata.kind = POSIX_NODE_REGULAR;
    metadata.mode = 0755u;
    metadata.size = (int64_t)(sizeof(source) - 1);
    if (posix_kernel_path_add_data(
            kernel, "/bin/stream.wast", &metadata, source,
            sizeof(source) - 1) != 0 ||
        posix_kernel_path_snapshot(
            kernel, (const uint8_t *)"/bin/stream.wast", 16,
            NATIVE_EXEC_BYTES_MAX, &snapshot, &snapshot_size, &metadata) != 0)
        goto done;
    wast_stream_init(&driver.stream, (const char *)snapshot, snapshot_size);
    for (;;) {
        status = wast_stream_next(&driver.stream, handler_stream_command,
                                  &driver);
        if (status <= 0) break;
    }
    passed = status == 0 && driver.commands == 2 &&
             snapshot_size == sizeof(source) - 1;
    wast_stream_destroy(&driver.stream);
done:
    free(snapshot);
    posix_kernel_destroy(kernel);
    return passed;
}

static int store_handler_stream_failure(void) {
    static const char source[] = "#!/bin/wast\n(module";
    native_store store;
    native_process_capsule *capsule;
    handler_stream_driver driver;
    uint8_t *copy = malloc(sizeof(source) - 1);
    exec_status handler_status = EXEC_OK;
    int child_pid = 0;
    int result = -1;
    int passed = 0;
    memset(&store, 0, sizeof(store));
    memset(&driver, 0, sizeof(driver));
    store.kernel = posix_kernel_create(0);
    if (!copy || !store.kernel) goto done;
    memcpy(copy, source, sizeof(source) - 1);
    store.processes[0].used = 1;
    store.processes[0].pid = 1;
    store.processes[0].kernel = store.kernel;
    native_process_capsule_init(&store.processes[0].capsule);
    store.process_count = 1;
    store.active_pid = 1;
    store.next_pid = 2;
    if (native_store_fork_process(&store, &child_pid) != 0 ||
        native_store_set_active_process(&store, child_pid) != 0)
        goto done;
    store.processes[0].capsule.state = NATIVE_PROCESS_BROWSER_BLOCKED;
    capsule = native_store_active_capsule(&store);
    wast_stream_init(&driver.stream, source, sizeof(source) - 1);
    if (!capsule || native_process_capsule_install_handler(
            capsule, NATIVE_PROCESS_HANDLER_WAST, copy,
            sizeof(source) - 1, &driver, NULL) != 0)
        goto done;
    copy = NULL;
    if (native_store_run_process_handler_step(
            &store, handler_stream_step) != EXEC_ERROR_FORMAT ||
        native_store_process_handler_result(
            &store, &handler_status, &result) != 0 ||
        handler_status != EXEC_ERROR_FORMAT || result != 0 ||
        native_store_complete_process_handler_result_and_wake(&store) != 0 ||
        native_store_set_active_process(&store, 1) != 0 ||
        native_store_take_process_wake(&store, &result) != 0 ||
        result != child_pid ||
        native_store_wait_process(&store, child_pid, 0, &result) != child_pid ||
        result != (126 << 8))
        goto done;
    passed = 1;
done:
    free(copy);
    wast_stream_destroy(&driver.stream);
    if (store.kernel) {
        for (int i = 0; i < NATIVE_PROCESS_MAX; i++)
            if (store.processes[i].used)
                native_process_capsule_destroy(&store.processes[i].capsule);
        posix_kernel_destroy(store.kernel);
    }
    return passed;
}

static int reject_multi_module_wat(void) {
    static const char source[] = "(module)\n(module)";
    uint8_t *wasm = NULL;
    size_t wasm_length = 0;
    char error[256] = {0};
    int result = waste_wat_compile(source, sizeof(source) - 1, &wasm,
                                   &wasm_length, error, sizeof(error));
    return result != 0 && wasm == NULL && wasm_length == 0 && error[0];
}

static int parse_shebang_sources(void) {
    static const char wat[] = "#!/bin/wat --strict\r\n(module)";
    static const char wast[] = "#!/bin/wast\n(module)\n";
    static const char invalid[] = "  #!/bin/wat\n(module)";
    waste_source_view view;
    uint8_t *wasm = NULL;
    size_t wasm_length = 0;
    char error[256] = {0};
    wast_script script;
    int result = waste_source_view_init(wat, sizeof(wat) - 1, &view);
    int passed = result == WASTE_SOURCE_OK && view.has_shebang &&
                 strcmp(view.interpreter, "/bin/wat") == 0 &&
                 strcmp(view.argument, "--strict") == 0 &&
                 waste_wat_compile(wat, sizeof(wat) - 1, &wasm, &wasm_length,
                                   error, sizeof(error)) == 0 &&
                 wasm != NULL && wasm_length >= 8;
    free(wasm);
    result = wast_parse_bytes(wast, sizeof(wast) - 1, &script);
    passed = passed && result == 0 && script.group_count == 1 &&
             script.error[0] == '\0';
    wast_script_free(&script);
    result = waste_source_view_init(invalid, sizeof(invalid) - 1, &view);
    passed = passed && result == WASTE_SOURCE_NO_SHEBANG;
    return passed;
}

static int parse_valid(const char *source) {
    wast_script script;
    int result = wast_parse_bytes(source, strlen(source), &script);
    int passed = result == 0 && script.error[0] == '\0' &&
                 script.group_count == 1 && script.assertion_count == 1 &&
                 script.command_count == 2 && script.parse_context == NULL;
    wast_script_free(&script);
    return passed;
}

static int parse_invalid(void) {
    static const char source[] =
        "(module\n"
        "  (func (export \"broken\") (result i32)\n"
        "    i32.const))";
    wast_script script;
    int result = wast_parse_bytes(source, sizeof(source) - 1, &script);
    int passed = result != 0 && script.error[0] != '\0' &&
                 strstr(script.error, " at 3:") != NULL &&
                 script.parse_context == NULL;
    wast_script_free(&script);
    return passed;
}

static int parse_unterminated_comment(void) {
    static const char source[] = "(module)\n(; unterminated";
    wast_script script;
    int result = wast_parse_bytes(source, sizeof(source) - 1, &script);
    int passed = result != 0 &&
                 strstr(script.error, " at 2:") != NULL &&
                 strstr(script.error, "unterminated block comment") != NULL &&
                 script.parse_context == NULL;
    wast_script_free(&script);
    return passed;
}

static void *run_parser_thread(void *opaque) {
    parser_thread *thread = (parser_thread *)opaque;
    while (!atomic_load_explicit(thread->start, memory_order_acquire)) {
    }
    thread->passed = 1;
    const char *failed = NULL;
    if (!parse_inline_fields()) failed = "inline fields";
    else if (!parse_raw_binary()) failed = "raw binary";
    else if (!parse_annotation_only_inline()) failed = "annotation-only inline";
    else if (!parse_stream_recovery()) failed = "stream recovery";
    else if (!shebang_stream_offsets()) failed = "shebang stream offsets";
    else if (!shebang_stream_position()) failed = "shebang stream position";
    else if (!handler_initial_cursor()) failed = "handler initial cursor";
    else if (!store_handler_stream_resume()) failed = "store handler stream resume";
    else if (!store_handler_stream_completion()) failed = "store handler completion";
    else if (!vfs_snapshot_stream()) failed = "VFS stream snapshot";
    else if (!store_handler_stream_failure()) failed = "store handler failure";
    else if (!reject_multi_module_wat()) failed = "strict WAT";
    else if (!parse_shebang_sources()) failed = "shebang sources";
    else if (!parse_unterminated_comment()) failed = "unterminated comment";
    if (failed) {
        fprintf(stderr, "thread %d failed %s\n", thread->index, failed);
        thread->passed = 0;
        return NULL;
    }
    for (int iteration = 0; iteration < ITERATION_COUNT; iteration++) {
        const char *source =
            valid_sources[(thread->index + iteration) % 2];
        if (!parse_valid(source) || !parse_invalid()) {
            thread->passed = 0;
            break;
        }
    }
    return NULL;
}

int main(void) {
    pthread_t threads[THREAD_COUNT];
    parser_thread states[THREAD_COUNT];
    atomic_int start = 0;

    for (int i = 0; i < THREAD_COUNT; i++) {
        states[i].index = i;
        states[i].start = &start;
        states[i].passed = 0;
        if (pthread_create(&threads[i], NULL, run_parser_thread,
                           &states[i]) != 0) {
            fprintf(stderr, "could not create parser thread %d\n", i);
            return 1;
        }
    }
    atomic_store_explicit(&start, 1, memory_order_release);
    int passed = 1;
    for (int i = 0; i < THREAD_COUNT; i++) {
        if (pthread_join(threads[i], NULL) != 0 || !states[i].passed)
            passed = 0;
    }
    if (!passed) {
        fprintf(stderr, "concurrent parser isolation failed\n");
        return 1;
    }
    printf("concurrent parser isolation passed (context=%zu bytes)\n",
           sizeof(wat_context));
    return 0;
}
