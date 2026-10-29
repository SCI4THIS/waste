/* Minimal host using only the public session API. All host file access is here. */
#define _POSIX_C_SOURCE 200809L
#include "include/waste.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t monotonic_ns(void *unused) {
    (void)unused;
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) return 0;
    return (uint64_t)value.tv_sec * 1000000000u + (uint64_t)value.tv_nsec;
}
static waste_status stage(waste_session *session, const char *guest, const char *host,
                           uint32_t mode, waste_error *error) {
    FILE *file = fopen(host, "rb");
    if (!file) { perror(host); return WASTE_ERROR_NOT_FOUND; }
    if (fseek(file, 0, SEEK_END)) { fclose(file); return WASTE_ERROR_MALFORMED; }
    long length = ftell(file);
    if (length < 0 || length > 32 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file); return WASTE_ERROR_MALFORMED;
    }
    uint8_t *bytes = malloc(length ? (size_t)length : 1);
    if (!bytes) { fclose(file); return WASTE_ERROR_RESOURCE; }
    size_t count = fread(bytes, 1, (size_t)length, file);
    int closed = fclose(file);
    waste_status status = count == (size_t)length && !closed ?
        waste_session_put_file(session, guest, bytes, (uint32_t)length, mode, error) : WASTE_ERROR_MALFORMED;
    free(bytes); /* The session copied the bytes. */
    return status;
}
int main(int argc, char **argv) {
    const char *collect = NULL;
    if (argc > 2 && !strcmp(argv[1], "--collect")) { collect = argv[2]; argc -= 2; argv += 2; }
    if (argc < 4) {
        fputs("usage: embedding-example [--collect GUEST_OUTPUT] LIBC_FILE COMMAND_FILE /usr/bin/COMMAND [ARG...]\n", stderr);
        return 2;
    }
    waste_session_options options;
    waste_session_options_init(&options);
    options.monotonic_ns = monotonic_ns;
    options.timeout_ms = 10000;
    waste_error error = {0};
    waste_session *session = NULL;
    waste_status status = waste_session_create(&options, &session, &error);
    if (status == WASTE_OK) status = stage(session, "/lib/libc.so.wasm", argv[1], 0644, &error);
    if (status == WASTE_OK) status = stage(session, argv[3], argv[2], 0755, &error);
    uint8_t buffer[4096];
    while (status == WASTE_OK) {
        size_t length = fread(buffer, 1, sizeof(buffer), stdin);
        if (ferror(stdin)) { status = WASTE_ERROR_RESOURCE; break; }
        status = waste_session_input(session, buffer, (uint32_t)length, feof(stdin) ? 1u : 0u, &error);
        if (feof(stdin)) break;
    }
    if (status == WASTE_OK) status = waste_session_start(session, argv[3],
        (const char *const *)(argv + 4), (uint32_t)(argc - 4), NULL, 0, &error);
    waste_session_result result = {0};
    while (status == WASTE_OK) {
        status = waste_session_inspect(session, &result);
        if (status != WASTE_OK || result.state >= WASTE_SESSION_EXITED) break;
        if (result.state == WASTE_SESSION_WAITING && result.wait == WASTE_SESSION_WAIT_TIMER) {
            uint64_t delay = result.wait_ns < 16000000u ? result.wait_ns : 16000000u;
            struct timespec interval = {0, (long)delay};
            (void)nanosleep(&interval, NULL);
        }
        status = waste_session_step(session, &error);
    }
    /* Streams and files remain available before the one explicit deletion. */
    for (uint32_t fd = 1; session && fd <= 2; fd++) {
        uint32_t offset = 0, copied;
        while (waste_session_copy_output(session, fd, offset, buffer, sizeof(buffer), &copied, &error) == WASTE_OK && copied) {
            if (fwrite(buffer, 1, copied, fd == 1 ? stdout : stderr) != copied) { status = WASTE_ERROR_RESOURCE; break; }
            offset += copied;
        }
    }
    if (collect && status == WASTE_OK) {
        uint64_t offset = 0;
        uint32_t copied;
        do {
            status = waste_session_read_file(session, collect, offset, buffer, sizeof(buffer), &copied, &error);
            if (status != WASTE_OK || !copied) break;
            if (fwrite(buffer, 1, copied, stdout) != copied) { status = WASTE_ERROR_RESOURCE; break; }
            offset += copied;
        } while (copied);
    }
    int exit_code = status == WASTE_OK && result.state == WASTE_SESSION_EXITED ? result.exit_code : 1;
    if (status != WASTE_OK || result.state != WASTE_SESSION_EXITED)
        fprintf(stderr, "embedding: %s\n", result.message[0] ? result.message : error.message);
    waste_session_delete(&session);
    return exit_code;
}
