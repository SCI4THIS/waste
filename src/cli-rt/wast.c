/* File-based WAST frontend; conformance semantics stay in native_wast.c. */
#define _POSIX_C_SOURCE 200809L
#include "native_input.h"
#include "native_vfs.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int native_wast_legacy_main(int, char **);
static char *read_source(const char *path, size_t *length) {
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    struct stat before, after;
    if (fd < 0) return NULL;
    if (fstat(fd, &before) || !S_ISREG(before.st_mode) || before.st_size < 0 ||
        (uint64_t)before.st_size > NATIVE_SESSION_SOURCE_MAX_BYTES) { close(fd); return NULL; }
    *length = (size_t)before.st_size;
    char *source = malloc(*length + 1);
    size_t at = 0;
    while (source && at < *length) {
        ssize_t got = read(fd, source + at, *length - at);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        at += (size_t)got;
    }
    char tail;
    ssize_t extra;
    do { extra = read(fd, &tail, 1); } while (extra < 0 && errno == EINTR);
    int failed = !source || at != *length || extra != 0 || fstat(fd, &after) ||
        before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec;
    close(fd);
    if (failed) { free(source); return NULL; }
    source[*length] = 0;
    return source;
}
static int suite(const char *root, int argc, char **argv) {
    char helper[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", helper, sizeof(helper) - 1);
    if (n <= 0 || (size_t)n >= sizeof(helper) - 1) return 2;
    helper[n] = 0;
    char *base = strrchr(helper, '/');
    if (!base) return 2;
    int sanitize = strstr(base, "-sanitize") != NULL;
    size_t room = sizeof(helper) - (size_t)(base + 1 - helper);
    snprintf(base + 1, room, "private/test-suite%s", sanitize ? "-sanitize" : "");
    char **args = calloc((size_t)argc + 4, sizeof(*args));
    if (!args) return 2;
    args[0] = helper; args[1] = "--vfs-root"; args[2] = (char *)root;
    for (int i = 0; i < argc; i++) args[i + 3] = argv[i];
    execv(helper, args);
    free(args);
    perror("wast: cannot start private batch helper");
    return 2;
}
int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "--parse-only") || !strcmp(argv[1], "--count") ||
        !strcmp(argv[1], "--browser-spec") || !strcmp(argv[1], "--server")))
        return native_wast_legacy_main(argc, argv);
    const char *override = NULL;
    native_runtime_options runtime_options = {.columns = NATIVE_TERMINAL_COLUMNS,
        .rows = NATIVE_TERMINAL_ROWS, .control_fd = -1, .raw_output = 1, .empty_descriptors = 1, .disable_test_suite = 1};
    native_wast_options options = {.output = stdout, .mode = NATIVE_WAST_AUTO};
    int first = 1;
    for (; first < argc; first++) {
        const char *option = argv[first];
        if (!strcmp(option, "--")) { first++; break; }
        if (option[0] != '-') break;
        if (!strcmp(option, "--help")) {
            puts("usage: wast [OPTIONS] FILE...\n"
                 "  --verbose            assertion progress, totals and first failure\n"
                 "  --json               assertion/setup JSON per input file\n"
                 "  --context MODE       auto (default), language or runtime\n"
                 "  --vfs-root DIRECTORY override root discovered from the executable location\n"
                 "  --timeout-ms N       bound each file's execution and external waits\n"
                 "  --suite [OPTIONS]    installed corpus batch selection/reporting\n"
                 "  --parse-only FILE... | --count FILE | --browser-spec FILE | --server\n"
                 "  --                   end options for filenames beginning with '-'\n"
                 "Each file owns an isolated store/kernel. JSON and verbose are alternatives.\n"
                 "Runtime auto-context never overrides script providers or linkage assertions.\n"
                 "Use wast --suite --help for batch options.");
            return 0;
        }
        if (!strcmp(option, "--verbose")) { options.verbose = 1; continue; }
        if (!strcmp(option, "--json")) { options.json = 1; continue; }
        if (!strcmp(option, "--suite")) {
            char root[PATH_MAX];
            if (options.json || options.verbose || options.mode != NATIVE_WAST_AUTO || runtime_options.timeout_ms)
                goto usage;
            if (native_input_resolve_root(override, root, sizeof(root))) goto root_error;
            return suite(root, argc - first - 1, argv + first + 1);
        }
        if (first + 1 >= argc) goto usage;
        const char *value = argv[++first];
        if (!strcmp(option, "--vfs-root")) override = value;
        else if (!strcmp(option, "--context")) {
            if (!strcmp(value, "auto")) options.mode = NATIVE_WAST_AUTO;
            else if (!strcmp(value, "language")) options.mode = NATIVE_WAST_LANGUAGE;
            else if (!strcmp(value, "runtime")) options.mode = NATIVE_WAST_RUNTIME;
            else goto usage;
        } else if (!strcmp(option, "--timeout-ms")) {
            char *end;
            errno = 0;
            unsigned long timeout = strtoul(value, &end, 10);
            if (errno || !value[0] || *end || !timeout || timeout > EXECUTION_MAX_TIMEOUT_MS) goto usage;
            runtime_options.timeout_ms = (unsigned)timeout;
        } else goto usage;
    }
    if (first == argc || (options.json && options.verbose)) goto usage;
    char root[PATH_MAX];
    if (native_input_resolve_root(override, root, sizeof(root))) goto root_error;
    waste_vfs vfs = {0};
    char error[256] = {0};
    if (native_vfs_load(root, &vfs, error, sizeof(error))) {
        fprintf(stderr, "wast: %s\n", error); return 2;
    }
    runtime_options.diagnostic_output = options.json;
    int result = 0;
    for (int i = first; i < argc; i++) {
        size_t length = 0;
        char *source = read_source(argv[i], &length);
        if (!source) {
            fprintf(stderr, "wast: cannot read bounded regular WAST input: %s\n", argv[i]);
            if (options.json) {
                fputs("{\"file\":", stdout); native_wast_json_string(stdout, argv[i]);
                puts(",\"assertions\":[],\"passed\":0,\"total\":0,\"completed\":false,\"error\":\"cannot read WAST input\"}");
            }
            result = 1; continue;
        }
        native_runtime *runtime = native_runtime_create_vfs(root, &vfs, &runtime_options);
        native_wast_counts counts;
        if (options.verbose && argc - first > 1) printf("%s: ", argv[i]);
        int status = native_runtime_wast(runtime, argv[i], source, length, &counts, &options);
        int finished = native_runtime_finish(runtime, NULL);
        free(source);
        if (finished == 124 || finished == 125) result = finished;
        else if ((status || finished) && result == 0) result = 1;
    }
    waste_vfs_free(&vfs);
    return fflush(stdout) || ferror(stdout) ? 1 : result;
root_error:
    fputs("wast: cannot resolve VFS root; use --vfs-root DIRECTORY\n", stderr);
    return 2;
usage:
    fputs("usage: wast [OPTIONS] FILE...; use --help\n", stderr);
    return 2;
}
