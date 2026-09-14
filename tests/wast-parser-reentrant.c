#include "script/wast_runner.h"
#include "text/wat_context.h"
#include "text/wat_builder.h"
#include "script/wast_stream.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
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

static int reject_multi_module_wat(void) {
    static const char source[] = "(module)\n(module)";
    uint8_t *wasm = NULL;
    size_t wasm_length = 0;
    char error[256] = {0};
    int result = waste_wat_compile(source, sizeof(source) - 1, &wasm,
                                   &wasm_length, error, sizeof(error));
    return result != 0 && wasm == NULL && wasm_length == 0 && error[0];
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
    else if (!reject_multi_module_wat()) failed = "strict WAT";
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
