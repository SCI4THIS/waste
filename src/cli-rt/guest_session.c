/* Private session harness: deterministic controls and compatibility reports. */
#define _POSIX_C_SOURCE 200809L
#include "native_runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int session_number(const char *text, unsigned maximum, unsigned *value) {
    char *end;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno || !*text || *end || !parsed || parsed > maximum) return 0;
    *value = (unsigned)parsed;
    return 1;
}

static int session_number64(const char *text, uint64_t maximum, uint64_t *value) {
    char *end;
    errno = 0;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno || !*text || *end || parsed > maximum) return 0;
    *value = (uint64_t)parsed;
    return 1;
}

int main(int argc, char **argv) {
    const char *root_path = NULL, *script_path = NULL, *result_path = NULL, *exec_path = NULL;
    const char *guest_args[NATIVE_EXEC_ARG_MAX];
    unsigned guest_arg_count = 0;
    unsigned timeout = 30000, cancel_after = 0, columns = NATIVE_TERMINAL_COLUMNS, rows = NATIVE_TERMINAL_ROWS;
    int trace_waits = 0, trace_process = 0, control_fd = -1;
    int clock_realtime_fixed = 0, clock_monotonic_fixed = 0;
    uint64_t clock_realtime_ns = 0, clock_monotonic_ns = 0;
    struct { const char *guest, *host; unsigned mode; } staged[NATIVE_STAGED_FILE_MAX];
    unsigned staged_count = 0;
    /* Scripted host-IO replies accumulated before the session starts.  They
     * are copied into the session's queue after native_store_init runs. */
    struct { native_host_io_kind kind; int cancel; const char *host_file; }
        pending_io[NATIVE_HOST_IO_REPLY_MAX];
    unsigned pending_io_count = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            puts("usage: private/guest-session --vfs-root DIRECTORY [--script HOST.wast] [--result-file JSON]\n"
                 "       [--timeout-ms N] [--columns N] [--rows N] [--trace-waits]\n"
                 "       [--cancel-after-ms N] [--control-fd N] [--trace-process]\n"
                 "       [--clock-realtime-ns N] [--clock-monotonic-ns N]\n"
                 "       [--host-upload-reply HOST_FILE | --host-upload-cancel]...\n"
                 "       [--host-download-complete | --host-download-cancel]...\n"
                 "       [--stage-file GUEST_PATH OCTAL_MODE HOST_FILE]...\n"
                 "       [--exec GUEST_PATH -- ARG...] (direct-start migration check)\n"
                 "Default script: mounted /usr/share/waste/launch.wast. Guest output: stdout.\n"
                 "Input is consumed only after a guest READ/SELECT yield. Limits cover interpreted execution and waits.\n"
                 "Timeout exits 124; scheduled cancellation exits 125. Neither is a guest trap/exit.\n"
                 "Control-fd >= 3 accepts 16-byte WSC1 resize/signal/clock records at READ/SELECT waits.\n"
                 "Clock overrides freeze the kernel-visible monotonic/realtime clocks; host deadlines keep real time.\n"
                 "Host-upload/download replies are consumed in FIFO order at the guest's host-io yields.\n"
                 "Result-file must not already exist. Bounded child-first fork/exec is supported.\n"
                 "Use wast for ordinary/full-conformance WAST.");
            return 0;
        }
        if (!strcmp(argv[i], "--trace-waits")) { trace_waits = 1; continue; }
        if (!strcmp(argv[i], "--")) {
            if (!exec_path) goto invalid;
            while (++i < argc) {
                if (guest_arg_count + 1 >= NATIVE_EXEC_ARG_MAX) goto invalid;
                guest_args[guest_arg_count++] = argv[i];
            }
            break;
        }
        if (!strcmp(argv[i], "--trace-process")) { trace_process = 1; continue; }
        if (!strcmp(argv[i], "--host-upload-cancel")) {
            if (pending_io_count == NATIVE_HOST_IO_REPLY_MAX) goto invalid;
            pending_io[pending_io_count].kind = NATIVE_HOST_IO_UPLOAD;
            pending_io[pending_io_count].cancel = 1;
            pending_io[pending_io_count++].host_file = NULL;
            continue;
        }
        if (!strcmp(argv[i], "--host-download-complete")) {
            if (pending_io_count == NATIVE_HOST_IO_REPLY_MAX) goto invalid;
            pending_io[pending_io_count].kind = NATIVE_HOST_IO_DOWNLOAD;
            pending_io[pending_io_count].cancel = 0;
            pending_io[pending_io_count++].host_file = NULL;
            continue;
        }
        if (!strcmp(argv[i], "--host-download-cancel")) {
            if (pending_io_count == NATIVE_HOST_IO_REPLY_MAX) goto invalid;
            pending_io[pending_io_count].kind = NATIVE_HOST_IO_DOWNLOAD;
            pending_io[pending_io_count].cancel = 1;
            pending_io[pending_io_count++].host_file = NULL;
            continue;
        }
        if (!strcmp(argv[i], "--stage-file")) {
            if (i + 3 >= argc || staged_count == NATIVE_STAGED_FILE_MAX) goto invalid;
            const char *guest = argv[++i], *mode = argv[++i], *host = argv[++i];
            char *end;
            errno = 0;
            unsigned long bits = strtoul(mode, &end, 8);
            if (errno || !*mode || *end || bits > 0777 || guest[0] != '/' ||
                strlen(guest) >= POSIX_PATH_NODE_NAME_MAX) goto invalid;
            staged[staged_count].guest = guest;
            staged[staged_count].host = host;
            staged[staged_count++].mode = (unsigned)bits;
            continue;
        }
        if (i + 1 >= argc) goto invalid;
        const char *option = argv[i++], *value = argv[i];
        if (!strcmp(option, "--vfs-root")) root_path = value;
        else if (!strcmp(option, "--script")) script_path = value;
        else if (!strcmp(option, "--exec")) exec_path = value;
        else if (!strcmp(option, "--result-file")) result_path = value;
        else if (!strcmp(option, "--timeout-ms")) {
            if (!session_number(value, EXECUTION_MAX_TIMEOUT_MS, &timeout)) goto invalid;
        } else if (!strcmp(option, "--cancel-after-ms")) {
            if (!session_number(value, EXECUTION_MAX_TIMEOUT_MS, &cancel_after)) goto invalid;
        } else if (!strcmp(option, "--columns")) {
            if (!session_number(value, 65535, &columns)) goto invalid;
        } else if (!strcmp(option, "--rows")) {
            if (!session_number(value, 65535, &rows)) goto invalid;
        } else if (!strcmp(option, "--control-fd")) {
            unsigned descriptor;
            if (!session_number(value, INT_MAX, &descriptor) || descriptor < 3) goto invalid;
            control_fd = (int)descriptor;
            int flags = fcntl(control_fd, F_GETFL);
            if (flags < 0 || (flags & O_ACCMODE) == O_WRONLY) goto invalid;
        } else if (!strcmp(option, "--clock-realtime-ns")) {
            if (!session_number64(value, UINT64_MAX, &clock_realtime_ns)) goto invalid;
            clock_realtime_fixed = 1;
        } else if (!strcmp(option, "--clock-monotonic-ns")) {
            if (!session_number64(value, UINT64_MAX, &clock_monotonic_ns)) goto invalid;
            clock_monotonic_fixed = 1;
        } else if (!strcmp(option, "--host-upload-reply")) {
            if (pending_io_count == NATIVE_HOST_IO_REPLY_MAX) goto invalid;
            pending_io[pending_io_count].kind = NATIVE_HOST_IO_UPLOAD;
            pending_io[pending_io_count].cancel = 0;
            pending_io[pending_io_count++].host_file = value;
        } else goto invalid;
    }
    if (!root_path || (exec_path && script_path) || (result_path &&
        (!strcmp(result_path, root_path) || (script_path && !strcmp(result_path, script_path))))) goto invalid;
    native_runtime_options options = {.timeout_ms = timeout, .cancel_after_ms = cancel_after,
        .columns = columns, .rows = rows, .control_fd = control_fd,
        .trace_waits = trace_waits, .trace_process = trace_process,
        .merge_output = 1, .report_default = 1,
        .clock_realtime_fixed = clock_realtime_fixed, .clock_realtime_ns = clock_realtime_ns,
        .clock_monotonic_fixed = clock_monotonic_fixed, .clock_monotonic_ns = clock_monotonic_ns};
    native_runtime *session = native_runtime_create(root_path, &options);
    if (!session) { fputs("cannot allocate native runtime\n", stderr); return 1; }
    for (unsigned i = 0; i < pending_io_count; i++) {
        if (native_runtime_queue_reply(session, pending_io[i].kind, pending_io[i].cancel, pending_io[i].host_file)) {
            (void)native_runtime_finish(session, NULL);
            goto invalid;
        }
    }
    for (unsigned i = 0; i < staged_count; i++)
        (void)native_runtime_stage_file(session, staged[i].guest, staged[i].mode, staged[i].host);
    if (exec_path) {
        const char *environment[] = {"HOME=/root", "USER=root", "LOGNAME=root", "PWD=/root",
            "PATH=/bin:/usr/bin", "TERM=xterm", "PS1=# "};
        memmove(guest_args + 1, guest_args, guest_arg_count * sizeof(*guest_args));
        guest_args[0] = exec_path;
        native_runtime_start(session, exec_path, guest_args, guest_arg_count + 1,
            environment, sizeof(environment) / sizeof(environment[0]), NULL);
    } else native_runtime_script(session, script_path);
    return native_runtime_finish(session, result_path);

invalid:
    fputs("invalid guest-session arguments; use --help\n", stderr);
    return 2;
}
