/* Public host API checks: ownership, direct execution, streams and teardown.
 * This consumer deliberately includes no private engine header or WAST runner. */
#define _POSIX_C_SOURCE 200809L
#include "include/waste.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned checks;
#define CHECK(test) do { checks++; if (!(test)) { \
    fprintf(stderr, "embedding:%d: %s (%s)\n", __LINE__, #test, error.message); exit(1); \
} } while (0)
static waste_error error;
static const char *svg_report = "build/engine/logs/embedding-native.svg";
static uint64_t clock_now(void *context) {
    (void)context;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) abort();
    return (uint64_t)now.tv_sec * 1000000000 + (uint64_t)now.tv_nsec;
}

static void stage(waste_session *session, const char *guest, const char *host) {
    FILE *file = fopen(host, "rb");
    CHECK(file != NULL);
    CHECK(!fseek(file, 0, SEEK_END));
    long size = ftell(file);
    CHECK(size >= 0 && (unsigned long)size <= UINT32_MAX);
    CHECK(!fseek(file, 0, SEEK_SET));
    uint8_t *bytes = malloc((size_t)size + 1);
    CHECK(bytes != NULL);
    CHECK(fread(bytes, 1, (size_t)size, file) == (size_t)size);
    CHECK(!fclose(file));
    CHECK(waste_session_put_file(session, guest, bytes, (uint32_t)size, 0755, &error) == WASTE_OK);
    /* No input buffer may be borrowed by the engine after put_file returns. */
    memset(bytes, 0xa5, (size_t)size);
    free(bytes);
}
static waste_session *create(uint32_t output_limit) {
    waste_session_options options;
    waste_session_options_init(&options);
    options.monotonic_ns = clock_now;
    options.quantum_ms = 1;
    options.timeout_ms = 10000;
    if (output_limit) options.max_output_bytes = output_limit;
    waste_session *session = NULL;
    CHECK(waste_session_create(&options, &session, &error) == WASTE_OK);
    stage(session, "/lib/libc.so.wasm", "src/vfs/lib/libc.so.wasm");
    return session;
}
static waste_session_result inspect(waste_session *session) {
    waste_session_result result;
    CHECK(waste_session_inspect(session, &result) == WASTE_OK);
    return result;
}
static waste_session_result run(waste_session *session) {
    waste_session_result result = inspect(session);
    unsigned steps = 0;
    while (result.state < WASTE_SESSION_EXITED) {
        CHECK(steps++ < 100000);
        CHECK(waste_session_step(session, &error) == WASTE_OK);
        result = inspect(session);
    }
    if (result.state != WASTE_SESSION_EXITED)
        fprintf(stderr, "session state=%u: %s\n", result.state, result.message);
    return result;
}
static void output(waste_session *session, uint32_t fd, const void *expected, uint32_t length) {
    uint8_t bytes[16384];
    uint32_t copied = UINT32_MAX;
    CHECK(length < sizeof(bytes));
    CHECK(waste_session_copy_output(session, fd, 0, bytes, sizeof(bytes), &copied, &error) == WASTE_OK);
    CHECK(copied == length && !memcmp(bytes, expected, length));
}

static void contracts(void) {
    waste_session_options options;
    waste_session_options_init(&options);
    options.monotonic_ns = clock_now;
    waste_session *session = NULL;
    options.required_features = UINT32_MAX;
    CHECK(waste_session_create(&options, &session, &error) == WASTE_ERROR_UNSUPPORTED && !session);
    options.required_features = 0;
    options.abi_major++;
    CHECK(waste_session_create(&options, &session, &error) == WASTE_ERROR_UNSUPPORTED && !session);
    options.abi_major--;
    CHECK(waste_session_create(&options, &session, &error) == WASTE_OK);
    CHECK(waste_session_put_file(session, "/missing/input", NULL, 0, 0644, &error) == WASTE_ERROR_NOT_FOUND);
    CHECK(waste_session_put_file(session, "/work/../bad", NULL, 0, 0644, &error) == WASTE_ERROR_MALFORMED);
    CHECK(waste_session_start(session, "/usr/bin/missing", NULL, 0, NULL, 0, &error) != WASTE_OK);
    CHECK(inspect(session).state == WASTE_SESSION_FAILED);
    CHECK(waste_session_step(session, &error) != WASTE_OK);
    waste_session_delete(&session);
    waste_session_delete(&session);
    CHECK(session == NULL);
}
static void awk(void) {
    waste_session *session = create(0);
    stage(session, "/usr/bin/awk", "src/vfs/usr/bin/awk");
    char script[] = "BEGIN { printf \"a%cB\", 0; print \"error\" > \"/dev/stderr\"; print \"saved\" > \"/work/result\"; exit 7 }";
    const char *args[] = {script};
    CHECK(waste_session_input(session, NULL, 0, 1, &error) == WASTE_OK);
    CHECK(waste_session_start(session, "/usr/bin/awk", args, 1, NULL, 0, &error) == WASTE_OK);
    memset(script, 'X', sizeof(script) - 1);
    waste_session_result result = run(session);
    CHECK(result.state == WASTE_SESSION_EXITED && result.exit_code == 7);
    const uint8_t expected[] = {'a', 0, 'B'};
    output(session, 1, expected, sizeof(expected));
    output(session, 2, "error\n", 6);
    uint8_t bytes[20]; uint32_t copied;
    CHECK(waste_session_read_file(session, "/work/result", 1, bytes, sizeof(bytes), &copied, &error) == WASTE_OK);
    CHECK(copied == 5 && !memcmp(bytes, "aved\n", 5));
    waste_session_file_info info;
    char name[256];
    CHECK(waste_session_list(session, "/work", 0, name, sizeof(name), &info, &error) == WASTE_OK);
    CHECK(!strcmp(name, "result") && info.size == 6);
    CHECK(waste_session_list(session, "/work", 1, name, sizeof(name), &info, &error) == WASTE_ERROR_NOT_FOUND);
    CHECK(waste_session_put_file(session, "/work/late", NULL, 0, 0644, &error) != WASTE_OK);
    CHECK(waste_session_step(session, &error) != WASTE_OK);
    waste_session_delete(&session);
    session = create(0);
    CHECK(waste_session_stat(session, "/work/result", &info, &error) == WASTE_ERROR_NOT_FOUND);
    waste_session_delete(&session);
}
static void streaming(void) {
    waste_session *session = create(0);
    stage(session, "/usr/bin/awk", "src/vfs/usr/bin/awk");
    const char *args[] = {"{print $1}"};
    CHECK(waste_session_start(session, "/usr/bin/awk", args, 1, NULL, 0, &error) == WASTE_OK);
    waste_session_result result;
    do {
        CHECK(waste_session_step(session, &error) == WASTE_OK);
        result = inspect(session);
    } while (result.state == WASTE_SESSION_WAITING && result.wait == WASTE_SESSION_WAIT_PUMP);
    CHECK(result.state == WASTE_SESSION_WAITING && result.wait == WASTE_SESSION_WAIT_INPUT);
    uint8_t input[10000];
    for (unsigned i = 0; i < sizeof(input); i++) input[i] = i % 2 ? '\n' : 'x';
    CHECK(waste_session_input(session, input, sizeof(input), 1, &error) == WASTE_OK);
    result = run(session);
    CHECK(result.state == WASTE_SESSION_EXITED && result.exit_code == 0);
    output(session, 1, input, sizeof(input));
    CHECK(waste_session_input(session, NULL, 0, 0, &error) != WASTE_OK);
    waste_session_delete(&session);
}
static void command(const char *name, const char *script, const char *expected) {
    waste_session *session = create(0);
    char guest[128], host[128];
    snprintf(guest, sizeof(guest), "/usr/bin/%s", name);
    snprintf(host, sizeof(host), "src/vfs/usr/bin/%s", name);
    stage(session, guest, host);
    CHECK(waste_session_input(session, (const uint8_t *)script, (uint32_t)strlen(script), 1, &error) == WASTE_OK);
    CHECK(waste_session_start(session, guest, NULL, 0, NULL, 0, &error) == WASTE_OK);
    waste_session_result result = run(session);
    CHECK(result.state == WASTE_SESSION_EXITED && !result.exit_code);
    output(session, 1, expected, (uint32_t)strlen(expected));
    waste_session_delete(&session);
}
static void dot(void) {
    waste_session *session = create(0);
    stage(session, "/usr/bin/dot", "src/vfs/usr/bin/dot");
    const char *args[] = {"-Tsvg", "-o", "/work/graph.svg"};
    const char input[] = "digraph { a -> b }";
    CHECK(waste_session_input(session, (const uint8_t *)input, sizeof(input) - 1, 1, &error) == WASTE_OK);
    CHECK(waste_session_start(session, "/usr/bin/dot", args, 3, NULL, 0, &error) == WASTE_OK);
    waste_session_result result = run(session);
    CHECK(result.state == WASTE_SESSION_EXITED && result.exit_code == 0);
    uint8_t svg[8192]; uint32_t copied;
    CHECK(waste_session_read_file(session, "/work/graph.svg", 0, svg, sizeof(svg) - 1, &copied, &error) == WASTE_OK);
    svg[copied] = 0;
    CHECK(strstr((const char *)svg, "<svg") != NULL);
    CHECK(strstr((const char *)svg, "a&#45;&gt;b") != NULL);
    waste_session_file_info info;
    CHECK(waste_session_stat(session, "/work/graph.svg", &info, &error) == WASTE_OK && info.size == copied);
    FILE *report = fopen(svg_report, "wb");
    CHECK(report != NULL);
    CHECK(fwrite(svg, 1, copied, report) == copied);
    CHECK(!fclose(report));
    waste_session_delete(&session);
}
static void trap(void) {
    /* Binary module exporting _start: unreachable. No text parser is used. */
    const uint8_t module[] = {0,97,115,109,1,0,0,0, 1,4,1,96,0,0,
        3,2,1,0, 7,10,1,6,95,115,116,97,114,116,0,0, 10,5,1,3,0,0,11};
    waste_session *session = create(0);
    CHECK(waste_session_put_file(session, "/work/trap.wasm", module, sizeof(module), 0755, &error) == WASTE_OK);
    CHECK(waste_session_start(session, "/work/trap.wasm", NULL, 0, NULL, 0, &error) == WASTE_OK);
    CHECK(run(session).state == WASTE_SESSION_TRAPPED);
    uint32_t copied;
    CHECK(waste_session_copy_output(session, 1, UINT32_MAX, NULL, 0, &copied, &error) == WASTE_ERROR_MALFORMED);
    waste_session_delete(&session);
}
static void cancellation(void) {
    waste_session *session = create(0);
    stage(session, "/usr/bin/awk", "src/vfs/usr/bin/awk");
    const char *args[] = {"BEGIN { while (1) {} }"};
    CHECK(waste_session_start(session, "/usr/bin/awk", args, 1, NULL, 0, &error) == WASTE_OK);
    CHECK(waste_session_step(session, &error) == WASTE_OK);
    CHECK(inspect(session).state == WASTE_SESSION_WAITING);
    waste_session_cancel(session);
    CHECK(run(session).state == WASTE_SESSION_CANCELLED);
    waste_session_cancel(session);
    waste_session_delete(&session);
    session = create(2);
    stage(session, "/usr/bin/awk", "src/vfs/usr/bin/awk");
    const char *prints[] = {"BEGIN { print \"overflow\" }"};
    CHECK(waste_session_input(session, NULL, 0, 1, &error) == WASTE_OK);
    CHECK(waste_session_start(session, "/usr/bin/awk", prints, 1, NULL, 0, &error) == WASTE_OK);
    CHECK(run(session).state == WASTE_SESSION_LIMIT);
    CHECK(inspect(session).stdout_bytes <= 2);
    waste_session_delete(&session);
}

static void quotas(void) {
    waste_session_options options;
    waste_session_options_init(&options);
    options.monotonic_ns = clock_now;
    options.timeout_ms = 10000;
    options.quantum_ms = 1;
    waste_session *session = NULL;
    options.max_filesystem_bytes = 1;
    CHECK(waste_session_create(&options, &session, &error) == WASTE_OK);
    CHECK(waste_session_put_file(session, "/work/large", (const uint8_t *)"ab", 2, 0644, &error) == WASTE_ERROR_LIMIT);
    CHECK(waste_session_put_file(session, "/work/small", (const uint8_t *)"a", 1, 0644, &error) == WASTE_OK);
    CHECK(inspect(session).filesystem_bytes_peak == 1);
    waste_session_delete(&session);

    options.max_filesystem_bytes = 128 * 1024 * 1024;
    options.max_memory_pages = 1;
    CHECK(waste_session_create(&options, &session, &error) == WASTE_OK);
    stage(session, "/lib/libc.so.wasm", "src/vfs/lib/libc.so.wasm");
    stage(session, "/usr/bin/awk", "src/vfs/usr/bin/awk");
    const char *quiet[] = {"BEGIN { exit 0 }"};
    (void)waste_session_start(session, "/usr/bin/awk", quiet, 1, NULL, 0, &error);
    waste_session_result result = inspect(session);
    if (result.state < WASTE_SESSION_EXITED) result = run(session);
    CHECK(result.state == WASTE_SESSION_LIMIT && strstr(result.message, "memory"));
    CHECK(result.memory_pages_peak <= 1);
    waste_session_delete(&session);

    options.max_memory_pages = 2048;
    options.max_instructions = 1000000;
    CHECK(waste_session_create(&options, &session, &error) == WASTE_OK);
    stage(session, "/lib/libc.so.wasm", "src/vfs/lib/libc.so.wasm");
    stage(session, "/usr/bin/awk", "src/vfs/usr/bin/awk");
    const char *loop[] = {"BEGIN { while (1) {} }"};
    (void)waste_session_start(session, "/usr/bin/awk", loop, 1, NULL, 0, &error);
    result = inspect(session);
    if (result.state < WASTE_SESSION_EXITED) result = run(session);
    CHECK(result.state == WASTE_SESSION_LIMIT && strstr(result.message, "instruction"));
    CHECK(result.instructions == options.max_instructions);
    uint32_t baseline = result.filesystem_bytes_peak;
    waste_session_delete(&session);

    options.max_instructions = 0;
    options.max_filesystem_bytes = baseline + 4096;
    CHECK(waste_session_create(&options, &session, &error) == WASTE_OK);
    stage(session, "/lib/libc.so.wasm", "src/vfs/lib/libc.so.wasm");
    stage(session, "/usr/bin/awk", "src/vfs/usr/bin/awk");
    const char *file[] = {"BEGIN { printf \"%10000s\", \"x\" > \"/work/quota\"; close(\"/work/quota\") }"};
    CHECK(waste_session_start(session, "/usr/bin/awk", file, 1, NULL, 0, &error) == WASTE_OK);
    result = run(session);
    CHECK(result.state == WASTE_SESSION_LIMIT && strstr(result.message, "filesystem"));
    CHECK(result.filesystem_bytes_peak <= options.max_filesystem_bytes);
    waste_session_file_info info;
    CHECK(waste_session_stat(session, "/work/quota", &info, &error) == WASTE_OK && info.size <= 4096);
    waste_session_delete(&session);

    options.max_filesystem_bytes = 128 * 1024 * 1024;
    options.max_processes = 1;
    CHECK(waste_session_create(&options, &session, &error) == WASTE_OK);
    stage(session, "/lib/libc.so.wasm", "src/vfs/lib/libc.so.wasm");
    stage(session, "/usr/bin/awk", "src/vfs/usr/bin/awk");
    stage(session, "/usr/bin/bash", "src/vfs/usr/bin/bash");
    const char *forks[] = {"BEGIN { system(\"printf child\") }"};
    CHECK(waste_session_start(session, "/usr/bin/awk", forks, 1, NULL, 0, &error) == WASTE_OK);
    result = run(session);
    CHECK(result.state == WASTE_SESSION_LIMIT && strstr(result.message, "process"));
    CHECK(result.processes_peak == 1);
    waste_session_delete(&session);
}
static void process_outcomes(void) {
    waste_session *session = create(0);
    stage(session, "/usr/bin/bash", "src/vfs/usr/bin/bash");
    const char *terminate[] = {"--norc", "-c", "kill -TERM $$"};
    CHECK(waste_session_input(session, NULL, 0, 1, &error) == WASTE_OK);
    CHECK(waste_session_start(session, "/usr/bin/bash", terminate, 3, NULL, 0, &error) == WASTE_OK);
    waste_session_result result = run(session);
    CHECK(result.state == WASTE_SESSION_SIGNALED && result.signal == 15 && result.exit_code == -1);
    waste_session_delete(&session);

    session = create(0);
    stage(session, "/usr/bin/bash", "src/vfs/usr/bin/bash");
    const char *child[] = {"--norc", "-c", "while :; do :; done & wait"};
    CHECK(waste_session_input(session, NULL, 0, 1, &error) == WASTE_OK);
    CHECK(waste_session_start(session, "/usr/bin/bash", child, 3, NULL, 0, &error) == WASTE_OK);
    unsigned steps = 0;
    do {
        CHECK(steps++ < 100000);
        CHECK(waste_session_step(session, &error) == WASTE_OK);
        result = inspect(session);
        CHECK(result.state < WASTE_SESSION_EXITED);
    } while (result.processes_peak < 2);
    waste_session_cancel(session);
    result = run(session);
    CHECK(result.state == WASTE_SESSION_CANCELLED && result.processes_peak >= 2);
    waste_session_delete(&session);
}

int main(int argc, char **argv) {
    CHECK(argc <= 2);
    if (argc == 2) svg_report = argv[1];
    contracts();
    awk();
    streaming();
    command("m4", "define(`name', `WASTE')name\n", "WASTE\n");
    dot();
    trap();
    cancellation();
    process_outcomes();
    quotas();
    printf("embedding C API: %u checks passed\n", checks);
    return 0;
}
