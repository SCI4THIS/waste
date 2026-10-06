/* Native batch execution of the installed /tests distribution. Host process
 * isolation, clocks, signals and report files belong to this adapter. */
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "test_suite.h"
#include "native_wast.h"
#include "native_vfs.h"
#include "lib/include/kernel.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define REPORT_MAX (16u * 1024u * 1024u)
#define FILTER_MAX 128u

typedef struct {
    const char *items[FILTER_MAX];
    unsigned count;
} filters;

typedef struct {
    native_wast_counts counts;
    char error[256];
} child_receipt;

typedef struct {
    const waste_suite_test *test;
    const char *status;
    int expected_failure, finished, exit_code;
    pid_t pid;
    int receipt_fd;
    uint64_t started, budget, elapsed;
    FILE *report, *diagnostic;
    child_receipt receipt;
} test_record;

static volatile sig_atomic_t interrupted;
static void interrupt_handler(int signal_number) { interrupted = signal_number; }

static uint64_t now_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) {
        perror("monotonic clock");
        exit(2);
    }
    return (uint64_t)value.tv_sec * 1000000000u + (uint64_t)value.tv_nsec;
}

static uint8_t *read_host(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    long size = -1;
    if (file && !fseek(file, 0, SEEK_END)) size = ftell(file);
    uint8_t *bytes = size >= 0 && (unsigned long)size <= WASTE_SUITE_MAX_BYTES ?
                     malloc((size_t)size + 1u) : NULL;
    int ok = bytes && !fseek(file, 0, SEEK_SET) &&
             fread(bytes, 1, (size_t)size, file) == (size_t)size && !ferror(file);
    if (file) fclose(file);
    if (!ok) { free(bytes); return NULL; }
    bytes[size] = 0;
    *length = (size_t)size;
    return bytes;
}

static uint8_t *read_guest(posix_kernel *kernel, const char *path, size_t *length) {
    int fd = posix_kernel_open(kernel, (const uint8_t *)path, strlen(path), 0, 0);
    uint64_t size = 0;
    uint8_t *bytes = NULL;
    if (fd >= 0 && !posix_kernel_file_size(kernel, fd, &size) &&
        size <= WASTE_SUITE_MAX_BYTES) {
        bytes = malloc((size_t)size + 1u);
        if (bytes && posix_kernel_read(kernel, fd, bytes, (int)size) != (int)size) {
            free(bytes);
            bytes = NULL;
        }
    }
    if (fd >= 0) posix_kernel_close(kernel, fd);
    if (bytes) { bytes[size] = 0; *length = (size_t)size; }
    return bytes;
}

static int mount_store(native_store *store, const waste_vfs *vfs) {
    native_store_init(store);
    native_wast_bind(store);
    return store->kernel && !native_store_bind_interpreter_paths(store) &&
           !waste_vfs_mount(store->kernel, vfs) &&
           !posix_kernel_path_set_cwd(store->kernel, "/root");
}

static int add_filter(filters *set, const char *value) {
    if (!*value || set->count == FILTER_MAX) return 0;
    set->items[set->count++] = value;
    return 1;
}

static int matches(const filters *set, const waste_suite_test *test, int group) {
    const char *name = test->identity;
    for (const char *p = name; *p; p++) if (*p == '/') name = p + 1;
    for (unsigned i = 0; i < set->count; i++) {
        const char *v = set->items[i];
        if (group ? !strcmp(v, test->group) :
            (!strcmp(v, test->identity) || !strcmp(v, test->path) ||
             !strcmp(v, test->file) || !strcmp(v, name))) return 1;
    }
    return 0;
}

static unsigned positive_ms(const char *value) {
    unsigned n = 0;
    if (!*value) return 0;
    for (; *value; value++) {
        if (*value < '0' || *value > '9' || n > 360000u) return 0;
        n = n * 10u + (unsigned)(*value - '0');
    }
    return n <= 3600000u ? n : 0;
}

static unsigned group_budget(const char *group) {
    if (!strcmp(group, "core")) return 15000;
    if (!strcmp(group, "core/simd") || !strcmp(group, "core/bulk-memory") ||
        !strcmp(group, "core/memory64")) return 10000;
    return 5000;
}

static int expected_load(const char *path, const waste_suite *suite, unsigned char *expected) {
    size_t length;
    uint8_t *bytes = read_host(path, &length);
    if (!bytes || memchr(bytes, 0, length)) { free(bytes); return 0; }
    char *p = (char *)bytes;
    while (*p) {
        char *start = p;
        while (*p && *p != '\n') p++;
        char *next = *p ? p + 1 : p;
        *p = 0;
        for (char *ch = start; *ch; ch++) if (*ch == '#') { *ch = 0; break; }
        while (*start == ' ' || *start == '\t' || *start == '\r') start++;
        char *end = start + strlen(start);
        while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *--end = 0;
        if (*start) {
            unsigned i;
            for (i = 0; i < suite->count; i++) if (!strcmp(start, suite->tests[i].identity)) break;
            if (i == suite->count || expected[i]) {
                fprintf(stderr, "unknown or duplicate expected-failure identity: %s\n", start);
                free(bytes);
                return 0;
            }
            expected[i] = 1;
        }
        p = next;
    }
    free(bytes);
    return 1;
}

static int stage_assets(native_store *store, const waste_suite_test *test) {
    for (unsigned i = 0; i < test->asset_count; i++) {
        const waste_suite_asset *asset = &test->assets[i];
        size_t length;
        uint8_t *bytes = read_guest(store->kernel, asset->path, &length);
        if (!bytes) return 0;
        int fd = posix_kernel_open(store->kernel, (const uint8_t *)asset->mount_path,
            strlen(asset->mount_path), POSIX_O_WRONLY | POSIX_O_CREAT | POSIX_O_TRUNC,
            (int)asset->mode);
        int ok = fd >= 0 && posix_kernel_write(store->kernel, fd, bytes, (int)length) == (int)length;
        if (fd >= 0) posix_kernel_close(store->kernel, fd);
        free(bytes);
        if (!ok) return 0;
    }
    return 1;
}

static int start_test(test_record *record, const waste_vfs *vfs) {
    record->report = tmpfile();
    record->diagnostic = tmpfile();
    int pipe_fd[2];
    if (!record->report || !record->diagnostic || pipe(pipe_fd)) return 0;
    fflush(NULL);
    record->started = now_ns();
    record->pid = fork();
    if (record->pid < 0) { close(pipe_fd[0]); close(pipe_fd[1]); return 0; }
    if (!record->pid) {
        close(pipe_fd[0]);
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        child_receipt receipt = {0};
        int code = 2;
        struct rlimit limit = {REPORT_MAX, REPORT_MAX};
        if (dup2(fileno(record->report), STDOUT_FILENO) < 0 ||
            dup2(fileno(record->diagnostic), STDERR_FILENO) < 0 ||
            setrlimit(RLIMIT_FSIZE, &limit)) _exit(2);
        native_store store;
        if (!mount_store(&store, vfs)) {
            snprintf(receipt.error, sizeof(receipt.error), "cannot mount isolated test filesystem");
        } else if (!stage_assets(&store, record->test)) {
            snprintf(receipt.error, sizeof(receipt.error), "cannot stage mounted companion files");
        } else {
            size_t source_length;
            uint8_t *source = read_guest(store.kernel, record->test->path, &source_length);
            if (!source) snprintf(receipt.error, sizeof(receipt.error),
                                   "cannot read mounted WAST: %.200s", record->test->identity);
            else {
                code = native_wast_run(&store, record->test->file, (const char *)source,
                                       source_length, &receipt.counts);
                free(source);
            }
        }
        native_store_free(&store);
        if (fflush(stdout) || fflush(stderr)) code = 2;
        ssize_t written;
        do { written = write(pipe_fd[1], &receipt, sizeof(receipt)); } while (written < 0 && errno == EINTR);
        close(pipe_fd[1]);
        _exit(written == (ssize_t)sizeof(receipt) ? code : 2);
    }
    close(pipe_fd[1]);
    record->receipt_fd = pipe_fd[0];
    return 1;
}

static void finish_test(test_record *record, int status, const char *forced) {
    record->elapsed = now_ns() - record->started;
    record->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) :
                        WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 2;
    size_t got = 0;
    while (got < sizeof(record->receipt)) {
        ssize_t n = read(record->receipt_fd, (char *)&record->receipt + got,
                         sizeof(record->receipt) - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(record->receipt_fd);
    record->pid = 0;
    record->finished = 1;
    int completed = got == sizeof(record->receipt) && record->receipt.counts.completed &&
                    WIFEXITED(status) && record->exit_code <= 1;
    if (forced) record->status = forced;
    else if (!completed) {
        record->status = "FAIL";
        if (!record->receipt.error[0])
            snprintf(record->receipt.error, sizeof(record->receipt.error),
                     "test process did not produce a complete result (exit %d)", record->exit_code);
    } else {
        int passed = record->exit_code == 0 && record->receipt.counts.passed == record->receipt.counts.total;
        record->status = record->expected_failure ? (passed ? "XPASS" : "XFAIL") : (passed ? "PASS" : "FAIL");
    }
    // Partial reports after interruption/crash are not valid JSON.
    if (!completed || forced) { fclose(record->report); record->report = NULL; }
}

static void json_key_string(FILE *out, const char *key, const char *value) {
    fprintf(out, ",\"%s\":", key);
    native_wast_json_string(out, value);
}

static void report_record(FILE *out, const test_record *record) {
    fputs("{\"identity\":", out);
    native_wast_json_string(out, record->test->identity);
    json_key_string(out, "status", record->status);
    fprintf(out, ",\"expectedFailure\":%s", record->expected_failure ? "true" : "false");
    json_key_string(out, "group", record->test->group);
    json_key_string(out, "file", record->test->file);
    json_key_string(out, "mode", record->test->mode);
    fprintf(out, ",\"elapsedMs\":%.3f", (double)record->elapsed / 1000000.0);
    if (!strcmp(record->status, "SKIP")) json_key_string(out, "skipReason", record->test->skip_reason);
    if (record->report) {
        fprintf(out, ",\"passed\":%u,\"total\":%u", record->receipt.counts.passed, record->receipt.counts.total);
        if (record->receipt.counts.passed != record->receipt.counts.total)
            fprintf(out, ",\"failureCount\":%u", record->receipt.counts.total - record->receipt.counts.passed);
        fputs(",\"nativeReport\":", out);
        rewind(record->report);
        char buffer[4096];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), record->report))) fwrite(buffer, 1, n, out);
    }
    if (record->exit_code) fprintf(out, ",\"exitCode\":%d", record->exit_code);
    if (record->receipt.error[0]) json_key_string(out, "error", record->receipt.error);
    if (record->diagnostic) {
        char preview[4097];
        rewind(record->diagnostic);
        size_t n = fread(preview, 1, sizeof(preview) - 1, record->diagnostic);
        preview[n] = 0;
        if (n) json_key_string(out, "stderrPreview", preview);
    }
    fputc('}', out);
}

static void usage(void) {
    puts("usage: waste-test --vfs-root=DIRECTORY [OPTIONS] [IDENTITY ...]\n"
         "  --manifest=/tests/manifest.json   mounted format-1 manifest\n"
         "  --list                           enumerate selected tests, including skips\n"
         "  --group=NAME --exclude=FILE --exclude-group=NAME (repeatable)\n"
         "  --expected-failures=PATH          tracked host-side XFAIL list\n"
         "  --results=PATH --json             assertion JSON file / stdout summary\n"
         "  --jobs=N                         isolated native children (1..64, default 1)\n"
         "  --timeout-ms=N                    global deadline override (1..3600000 ms)\n"
         "  --timeout-group=NAME:N            repeatable per-group override\n"
         "Default deadlines: core 15s; SIMD/bulk-memory/memory64 10s; other groups 5s.\n"
         "PASS/FAIL/XFAIL/XPASS/TIMEOUT/CANCELLED/SKIP; unexpected outcomes exit 1.\n"
         "Sources and companions are read exclusively from the installed tree.");
}

static int same_file(const char *a, const char *b) {
    struct stat x, y;
    return a && b && (!strcmp(a, b) || (!stat(a, &x) && !stat(b, &y) &&
                                      x.st_dev == y.st_dev && x.st_ino == y.st_ino));
}

/* Host report destinations must not replace installed inputs, even through
 * a hard-link alias. Nonexistent paths inside the tree are excluded too. */
static int report_outside_tree(const char *root, const char *destination, const waste_vfs *vfs) {
    if (!destination) return 1;
    char resolved_root[4096], parent[4096], resolved_parent[4096];
    if (!realpath(root, resolved_root) || strlen(destination) >= sizeof(parent)) return 0;
    strcpy(parent, destination);
    char *slash = strrchr(parent, '/');
    if (slash) { if (slash == parent) slash[1] = 0; else *slash = 0; }
    else strcpy(parent, ".");
    if (!realpath(parent, resolved_parent)) return 0;
    size_t length = strlen(resolved_root);
    if (!strncmp(resolved_parent, resolved_root, length) &&
        (!resolved_parent[length] || resolved_parent[length] == '/')) return 0;
    char input[8192];
    snprintf(input, sizeof(input), "%s/.inventory.json", root);
    if (same_file(destination, input)) return 0;
    for (uint32_t i = 0; i < vfs->count; i++) {
        snprintf(input, sizeof(input), "%s%s", root, vfs->entries[i].path);
        if (same_file(destination, input)) return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *root_path = NULL, *manifest_path = "/tests/manifest.json";
    const char *results_path = NULL, *expected_path = NULL;
    unsigned jobs = 1, global_timeout = 0;
    int list = 0, json = 0;
    filters groups = {0}, files = {0}, excluded = {0}, excluded_groups = {0}, budgets = {0};
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--help")) { usage(); return 0; }
        if (!strcmp(arg, "--list")) list = 1;
        else if (!strcmp(arg, "--json")) json = 1;
        else if (!strncmp(arg, "--vfs-root=", 11)) root_path = arg + 11;
        else if (!strcmp(arg, "--vfs-root") && i + 1 < argc) root_path = argv[++i];
        else if (!strncmp(arg, "--manifest=", 11)) manifest_path = arg + 11;
        else if (!strncmp(arg, "--results=", 10)) results_path = arg + 10;
        else if (!strncmp(arg, "--expected-failures=", 20)) expected_path = arg + 20;
        else if (!strncmp(arg, "--jobs=", 7)) {
            jobs = positive_ms(arg + 7);
            if (!jobs || jobs > 64) goto bad_options;
        } else if (!strncmp(arg, "--timeout-ms=", 13)) {
            global_timeout = positive_ms(arg + 13);
            if (!global_timeout) goto bad_options;
        } else if (!strncmp(arg, "--group=", 8)) { if (!add_filter(&groups, arg + 8)) goto bad_options; }
        else if (!strncmp(arg, "--exclude=", 10)) { if (!add_filter(&excluded, arg + 10)) goto bad_options; }
        else if (!strncmp(arg, "--exclude-group=", 16)) { if (!add_filter(&excluded_groups, arg + 16)) goto bad_options; }
        else if (!strncmp(arg, "--timeout-group=", 16)) {
            const char *spec = arg + 16, *colon = strrchr(spec, ':');
            if (!colon || colon == spec || !positive_ms(colon + 1) || !add_filter(&budgets, spec)) goto bad_options;
        } else if (arg[0] == '-') goto bad_options;
        else if (!add_filter(&files, arg)) goto bad_options;
    }
    if (!root_path || !*root_path || strncmp(manifest_path, "/tests/", 7) ||
        (results_path && (!*results_path || same_file(results_path, root_path) ||
                          same_file(results_path, expected_path)))) goto bad_options;

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = interrupt_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) return 2;

    int code = 2;
    size_t manifest_length = 0;
    waste_vfs vfs = {0};
    uint8_t *manifest = NULL;
    char error[256];
    native_store catalogue;
    memset(&catalogue, 0, sizeof(catalogue));
    waste_suite suite = {0};
    test_record *records = NULL;
    FILE *results = NULL;
    unsigned record_count = 0;
    if (native_vfs_load(root_path, &vfs, error, sizeof(error)) ||
        !mount_store(&catalogue, &vfs) ||
        !(manifest = read_guest(catalogue.kernel, manifest_path, &manifest_length))) {
        fprintf(stderr, "cannot load tree or mounted manifest: %s (%s)\n", root_path, manifest_path);
        goto cleanup;
    }
    if (waste_suite_decode((const char *)manifest, manifest_length, &suite, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        goto cleanup;
    }
    if (!report_outside_tree(root_path, results_path, &vfs)) {
        fprintf(stderr, "result file would replace installed VFS inputs\n");
        goto cleanup;
    }
    unsigned char expected[WASTE_SUITE_MAX_TESTS] = {0};
    if (expected_path && !expected_load(expected_path, &suite, expected)) {
        fprintf(stderr, "cannot load expected-failures list: %s\n", expected_path);
        goto cleanup;
    }
    records = calloc(suite.count, sizeof(*records));
    if (!records) goto cleanup;
    for (unsigned i = 0; i < suite.count; i++) {
        const waste_suite_test *test = &suite.tests[i];
        if ((groups.count || files.count) && !matches(&groups, test, 1) && !matches(&files, test, 0)) continue;
        if (matches(&excluded, test, 0) || matches(&excluded_groups, test, 1)) continue;
        test_record *record = &records[record_count++];
        record->test = test;
        record->expected_failure = expected[i] || test->expect_failure;
        unsigned ms = global_timeout ? global_timeout : group_budget(test->group);
        for (unsigned j = 0; j < budgets.count; j++) {
            const char *spec = budgets.items[j], *colon = strrchr(spec, ':');
            if ((size_t)(colon - spec) == strlen(test->group) && !memcmp(spec, test->group, strlen(test->group)))
                ms = positive_ms(colon + 1);
        }
        record->budget = (uint64_t)ms * 1000000u;
        if (test->skip_reason[0]) { record->status = "SKIP"; record->finished = 1; }
    }
    if (!record_count) { fprintf(stderr, "no requested native tests found\n"); goto cleanup; }
    if (list) {
        unsigned xfails = 0, skips = 0;
        for (unsigned i = 0; i < record_count; i++) {
            test_record *r = &records[i];
            printf("%s %s", r->status ? r->status : r->expected_failure ? "XFAIL?" : "PASS?", r->test->identity);
            if (r->status) { printf(": %s", r->test->skip_reason); skips++; }
            if (r->expected_failure) xfails++;
            putchar('\n');
        }
        if (json) printf("{\"count\":%u,\"expectedFailures\":%u,\"skip\":%u}\n", record_count, xfails, skips);
        if (results_path) {
            results = fopen(results_path, "wb");
            if (!results) { perror(results_path); goto cleanup; }
            fprintf(results, "{\"count\":%u,\"expectedFailures\":%u,\"skip\":%u,\"exitCode\":0,\"tests\":[",
                    record_count, xfails, skips);
            for (unsigned i = 0; i < record_count; i++) {
                if (i) fputc(',', results);
                fputs("{\"identity\":", results);
                native_wast_json_string(results, records[i].test->identity);
                json_key_string(results, "group", records[i].test->group);
                json_key_string(results, "skipReason", records[i].test->skip_reason);
                fprintf(results, ",\"expectedFailure\":%s}", records[i].expected_failure ? "true" : "false");
            }
            fputs("]}\n", results);
            if (fflush(results) || ferror(results)) goto cleanup;
        }
        code = 0;
        goto cleanup;
    }
    if (results_path && !(results = fopen(results_path, "wb"))) { perror(results_path); goto cleanup; }
    unsigned next = 0, active = 0, flushed = 0;
    while (next < record_count || active) {
        while (next < record_count && active < jobs && !interrupted) {
            test_record *record = &records[next++];
            if (record->finished) continue;
            if (!start_test(record, &vfs)) {
                record->status = "FAIL";
                record->finished = 1;
                snprintf(record->receipt.error, sizeof(record->receipt.error), "cannot start isolated test process");
                if (record->report) { fclose(record->report); record->report = NULL; }
            } else active++;
        }
        for (unsigned i = 0; i < next; i++) {
            test_record *record = &records[i];
            if (!record->pid) continue;
            int status;
            pid_t done = waitpid(record->pid, &status, WNOHANG);
            const char *forced = NULL;
            if (done == 0 && (interrupted || now_ns() - record->started >= record->budget)) {
                forced = interrupted ? "CANCELLED" : "TIMEOUT";
                kill(record->pid, SIGKILL);
                do { done = waitpid(record->pid, &status, 0); } while (done < 0 && errno == EINTR);
            }
            if (done > 0) { finish_test(record, status, forced); active--; }
            else if (done < 0 && errno != EINTR) {
                kill(record->pid, SIGKILL);
                while (waitpid(record->pid, NULL, 0) < 0 && errno == EINTR) {}
                snprintf(record->receipt.error, sizeof(record->receipt.error), "cannot reap isolated test process");
                close(record->receipt_fd);
                record->pid = 0;
                record->status = "FAIL";
                record->finished = 1;
                fclose(record->report);
                record->report = NULL;
                active--;
            }
        }
        if (interrupted) while (next < record_count) {
            test_record *record = &records[next++];
            record->status = "CANCELLED";
            record->finished = 1;
        }
        while (flushed < record_count && records[flushed].finished) {
            const test_record *r = &records[flushed++];
            printf("%s %s\n", r->status, r->test->identity);
            if (!strcmp(r->status, "FAIL") || !strcmp(r->status, "TIMEOUT") || !strcmp(r->status, "CANCELLED")) {
                report_record(stderr, r);
                fputc('\n', stderr);
            }
        }
        if (active) { struct timespec delay = {0, 5000000}; nanosleep(&delay, NULL); }
    }
    unsigned pass = 0, fail = 0, xfail = 0, xpass = 0, skip = 0;
    for (unsigned i = 0; i < record_count; i++) {
        const char *status = records[i].status;
        if (!strcmp(status, "PASS")) pass++;
        else if (!strcmp(status, "XFAIL")) xfail++;
        else if (!strcmp(status, "XPASS")) xpass++;
        else if (!strcmp(status, "SKIP")) skip++;
        else fail++;
    }
    for (unsigned output = 0; output < 2; output++) {
        FILE *out = output ? results : json ? stdout : NULL;
        if (!out) continue;
        fputs(output ? "{\"summary\":" : "", out);
        fprintf(out, "{\"pass\":%u,\"fail\":%u,\"xfail\":%u,\"xpass\":%u,\"skip\":%u,\"failures\":[",
                pass, fail, xfail, xpass, skip);
        int first = 1;
        for (unsigned i = 0; i < record_count; i++) {
            const char *status = records[i].status;
            if (strcmp(status, "FAIL") && strcmp(status, "TIMEOUT") && strcmp(status, "CANCELLED")) continue;
            if (!first) fputc(',', out);
            native_wast_json_string(out, records[i].test->identity);
            first = 0;
        }
        fputs("],\"unexpectedPasses\":[", out);
        first = 1;
        for (unsigned i = 0; i < record_count; i++) if (!strcmp(records[i].status, "XPASS")) {
            if (!first) fputc(',', out);
            native_wast_json_string(out, records[i].test->identity);
            first = 0;
        }
        fputs("]}", out);
        if (output) {
            fputs(",\"tests\":[", out);
            for (unsigned i = 0; i < record_count; i++) {
                if (i) fputc(',', out);
                report_record(out, &records[i]);
            }
            fprintf(out, "],\"exitCode\":%d}", interrupted ? 128 + interrupted : fail || xpass ? 1 : 0);
        }
        fputc('\n', out);
    }
    if (!json) printf("Native suite: %u PASS, %u FAIL, %u XFAIL, %u XPASS, %u SKIP\n", pass, fail, xfail, xpass, skip);
    code = interrupted ? 128 + interrupted : fail || xpass ? 1 : 0;
    if (results && (fflush(results) || ferror(results))) { perror("writing results"); code = 2; }
cleanup:
    if (results && fclose(results)) code = 2;
    if (records) for (unsigned i = 0; i < record_count; i++) {
        if (records[i].report) fclose(records[i].report);
        if (records[i].diagnostic) fclose(records[i].diagnostic);
    }
    free(records);
    waste_suite_free(&suite);
    native_store_free(&catalogue);
    free(manifest);
    waste_vfs_free(&vfs);
    return code;
bad_options:
    fprintf(stderr, "invalid suite options; use --help\n");
    return 2;
}
