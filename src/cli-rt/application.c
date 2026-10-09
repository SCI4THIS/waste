#define _POSIX_C_SOURCE 200809L
#include "application.h"
#include "native_input.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int environment_set(const char **environment, unsigned *count, const char *value) {
    const char *equals = strchr(value, '=');
    if (!equals || equals == value) return 0;
    size_t key = (size_t)(equals - value);
    for (unsigned i = 0; i < *count; i++) {
        if (!strncmp(environment[i], value, key) && environment[i][key] == '=') {
            environment[i] = value;
            return 1;
        }
    }
    if (*count + 1 >= NATIVE_EXEC_ENV_MAX) return 0;
    environment[(*count)++] = value;
    return 1;
}

int native_application_main(int argc, char **argv, native_exec_format format) {
    const char *name = format == NATIVE_EXEC_FORMAT_WAT ? "wat" : "wasm";
    const char *root = NULL, *entry = NULL, *report = NULL;
    struct { const char *guest, *host; unsigned mode; } staged[NATIVE_STAGED_FILE_MAX];
    unsigned staged_count = 0;
    const char *defaults[] = {"HOME=/root", "USER=root", "LOGNAME=root",
        "PWD=/root", "PATH=/bin:/usr/bin", "TERM=xterm", "PS1=# "};
    const char *environment[NATIVE_EXEC_ENV_MAX];
    unsigned env_count = sizeof(defaults) / sizeof(defaults[0]);
    memcpy(environment, defaults, sizeof(defaults));
    native_runtime_options options = {.columns = NATIVE_TERMINAL_COLUMNS,
        .rows = NATIVE_TERMINAL_ROWS, .control_fd = -1, .raw_output = 1, .host_stdio = 1};
    int file_index = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--")) {
            if (++i < argc) file_index = i;
            break;
        }
        if (argv[i][0] != '-' || !strcmp(argv[i], "-")) { file_index = i; break; }
        if (!strcmp(argv[i], "--help")) {
            printf("usage: %s [OPTIONS] FILE [GUEST_ARGUMENT...]\n"
                "  --vfs-root DIRECTORY  override the root discovered from the runtime location\n"
                "  --entry EXPORT        invoke a () -> () export (default _start)\n"
                "  --env NAME=VALUE      replace/add a bounded guest environment value\n"
                "  --timeout-ms N        opt-in execution/wait deadline\n"
                "  --result-file JSON    opt-in harness evidence (new file only)\n"
                "  --trace-process       print process transitions to stderr\n"
                "  --trace-waits         print explicit input/select waits to stderr\n"
                "  --stage-file GUEST MODE HOST  copy an explicit file into this guest session\n"
                "  --                    end runtime options\n", name);
            return 0;
        }
        if (!strcmp(argv[i], "--trace-process")) { options.trace_process = 1; continue; }
        if (!strcmp(argv[i], "--trace-waits")) { options.trace_waits = 1; continue; }
        if (!strcmp(argv[i], "--stage-file")) {
            if (i + 3 >= argc || staged_count == NATIVE_STAGED_FILE_MAX) goto usage;
            const char *guest = argv[++i], *mode = argv[++i], *host = argv[++i];
            char *end;
            errno = 0;
            unsigned long bits = strtoul(mode, &end, 8);
            if (errno || !*mode || *end || bits > 0777 || guest[0] != '/' ||
                strlen(guest) >= POSIX_PATH_NODE_NAME_MAX) goto usage;
            staged[staged_count].guest = guest;
            staged[staged_count].host = host;
            staged[staged_count++].mode = (unsigned)bits;
            continue;
        }
        if (i + 1 >= argc) goto usage;
        const char *option = argv[i++], *value = argv[i];
        if (!strcmp(option, "--vfs-root")) root = value;
        else if (!strcmp(option, "--entry")) {
            if (!value[0] || strlen(value) >= WAST_MAX_EXPORT_NAME) goto usage;
            entry = value;
        } else if (!strcmp(option, "--result-file")) report = value;
        else if (!strcmp(option, "--env")) {
            if (!environment_set(environment, &env_count, value)) goto usage;
        } else if (!strcmp(option, "--timeout-ms")) {
            char *end;
            errno = 0;
            unsigned long timeout = strtoul(value, &end, 10);
            if (errno || !value[0] || *end || !timeout || timeout > EXECUTION_MAX_TIMEOUT_MS) goto usage;
            options.timeout_ms = (unsigned)timeout;
        } else goto usage;
    }
    if (!file_index || argc - file_index >= NATIVE_EXEC_ARG_MAX) goto usage;
    native_application_input input;
    char error[256] = {0};
    if (native_application_input_open(&input, root, argv[file_index], format, error, sizeof(error))) {
        fprintf(stderr, "%s: %s\n", name, error);
        return 126;
    }
    native_runtime *runtime = native_runtime_create(input.root, &options);
    if (!runtime) {
        native_application_input_destroy(&input);
        fprintf(stderr, "%s: cannot allocate native runtime\n", name);
        return 126;
    }
    if (input.staged)
        (void)native_runtime_stage_input(runtime, input.guest, input.bytes, input.size, input.mode);
    for (unsigned i = 0; i < staged_count && !native_runtime_failed(runtime); i++)
        (void)native_runtime_stage_file(runtime, staged[i].guest, staged[i].mode, staged[i].host);
    const char *guest_args[NATIVE_EXEC_ARG_MAX];
    unsigned guest_argc = 0;
    guest_args[guest_argc++] = input.guest;
    for (int i = file_index + 1; i < argc; i++) guest_args[guest_argc++] = argv[i];
    native_process_start_options startup = {.entry = entry, .format = format, .readable_input = 1};
    native_runtime_start(runtime, input.guest, guest_args, guest_argc, environment, env_count, &startup);
    int failed = native_runtime_failed(runtime);
    int status = native_runtime_finish(runtime, report);
    native_application_input_destroy(&input);
    return failed && status == 1 ? 126 : status;
usage:
    fprintf(stderr, "usage: %s [OPTIONS] FILE [GUEST_ARGUMENT...]; use --help\n", name);
    return 2;
}
