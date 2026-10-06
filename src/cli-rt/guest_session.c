/* Native guest session: mounted inputs and shared guest ABI/process driver. */
#define _POSIX_C_SOURCE 200809L
#include "guest_posix.h"
#include "process_driver.h"
#include "native_vfs.h"
#include "wast/stream.h"
#include "wast/runner.h"
#include "wast/handler.h"
#include "runtime_internal.h"
#include "lib/include/kernel.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#define SESSION_SOURCE_MAX (64u * 1024u * 1024u)
#define SESSION_COMMAND_MAX 1024
#define SESSION_HOST_IO_REPLY_MAX 16

/* Scripted upload/download reply queued by --host-upload-reply /
 * --host-download-reply.  The native adapter has no real file dialog, so each
 * host-IO yield is matched to the next queued reply of the same kind.  Upload
 * replies either supply bytes (bytes != NULL) or cancel; download replies
 * either complete or cancel. */
typedef struct {
    native_host_io_kind kind;
    int cancel;
    uint8_t *bytes;
    size_t length;
} session_host_io_reply;

typedef struct {
    int suite_root_fd;
    native_store store;
    native_process_driver driver;
    wast_process_handler handler;
    wast_script *retained[SESSION_COMMAND_MAX];
    int retained_count;
    unsigned passed, total, waits, select_waits, input_bytes;
    unsigned resize_events, signal_events, clock_events;
    unsigned upload_events, download_events;
    int control_fd;
    uint8_t control_record[16];
    size_t control_length;
    uint64_t deadline, cancel_deadline;
    int stopped, timed_out, cancelled, exited, exit_code, input_eof, trace_waits;
    char error[256];
    unsigned handler_failures;
    char handler_error[256];
    /* Scripted deterministic clocks for host-independent guest tests.  The
     * host-side deadlines (session->deadline, cancel_deadline, poll timeout)
     * never use these overrides: only the kernel-visible guest clocks do. */
    int clock_realtime_fixed, clock_monotonic_fixed;
    uint64_t clock_realtime_ns, clock_monotonic_ns;
    /* FIFO of scripted host-IO replies consumed on each EXEC_YIELD_HOST_IO. */
    session_host_io_reply host_io_replies[SESSION_HOST_IO_REPLY_MAX];
    int host_io_reply_count;
    int host_io_reply_head;
} cli_guest_session;

static uint64_t host_now(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) return 0;
    return (uint64_t)value.tv_sec * 1000000000u + (uint64_t)value.tv_nsec;
}

static uint64_t session_now(void *opaque) {
    cli_guest_session *session = opaque;
    if (session && session->clock_monotonic_fixed) return session->clock_monotonic_ns;
    return host_now();
}

static uint64_t session_realtime(void *opaque) {
    cli_guest_session *session = opaque;
    struct timespec value;
    if (session && session->clock_realtime_fixed) return session->clock_realtime_ns;
    if (clock_gettime(CLOCK_REALTIME, &value)) return 0;
    return (uint64_t)value.tv_sec * 1000000000u + (uint64_t)value.tv_nsec;
}

static volatile sig_atomic_t session_interrupted;
static void session_interrupt(int value) { session_interrupted = value; }
static uint8_t *session_read_file(const char *, size_t, size_t *);

static exec_stop_reason session_control(void *opaque) {
    cli_guest_session *session = opaque;
    uint64_t now = host_now();
    if (session_interrupted) { session->cancelled = 1; return EXEC_STOP_CANCELLED; }
    if (session->cancel_deadline && now >= session->cancel_deadline &&
        session->cancel_deadline <= session->deadline) {
        session->cancelled = 1;
        return EXEC_STOP_CANCELLED;
    }
    if (now >= session->deadline) {
        session->timed_out = 1;
        return EXEC_STOP_TIMEOUT;
    }
    return EXEC_STOP_NONE;
}

static int32_t session_write(void *opaque, int32_t descriptor,
                              const void *bytes, uint32_t length) {
    (void)opaque;
    if (descriptor != 1 && descriptor != 2) return -POSIX_EBADF;
    /* One transcript stream, like the browser worker. Host stdin/stdout are
     * the only capabilities; guest paths never become host filesystem calls. */
    uint32_t at = 0;
    while (at < length) {
        ssize_t written = write(STDOUT_FILENO, (const uint8_t *)bytes + at,
                                (size_t)(length - at));
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return -POSIX_EIO;
        at += (uint32_t)written;
    }
    return (int32_t)length;
}

static const guest_posix_platform session_platform = {
    NULL, NULL, NULL, session_write, NULL
};

static uint32_t session_control_word(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

/* A borrowed, explicit host capability, never a guest descriptor. WSC1
 * records are fixed-size and little-endian; partial records survive polls.
 * Return 1 only after applying an event, 0 for a fragment/clean EOF, -1 on
 * malformed input. No allocation or host signal delivery is involved. */
static int session_control_read(cli_guest_session *session, exec_error *error) {
    ssize_t count = read(session->control_fd,
        session->control_record + session->control_length,
        sizeof(session->control_record) - session->control_length);
    if (count < 0 && (errno == EINTR || errno == EAGAIN)) return 0;
    if (count < 0) goto invalid;
    if (!count) {
        session->control_fd = -1;
        if (session->control_length) goto invalid;
        return 0;
    }
    session->control_length += (size_t)count;
    if (session->control_length != sizeof(session->control_record)) return 0;
    const uint8_t *record = session->control_record;
    uint32_t operation = session_control_word(record + 4);
    uint32_t first = session_control_word(record + 8);
    uint32_t second = session_control_word(record + 12);
    session->control_length = 0;
    if (memcmp(record, "WSC1", 4)) goto invalid;
    if (operation == 1 && first && first <= UINT16_MAX &&
        second && second <= UINT16_MAX) {
        posix_winsize dimensions = {(uint16_t)second, (uint16_t)first, 0, 0};
        if (posix_kernel_terminal_set_winsize(session->store.kernel, 0,
                                             &dimensions)) goto invalid;
        session->resize_events++;
    } else if (operation == 2 && first && first <= POSIX_SIGNAL_MAX) {
        /* second == 0 queues to the active process via the kernel path; a
         * positive PID routes to the named process via the shared store so
         * backgrounded processes and process-group members can be signaled
         * without stealing the active continuation.  Any store/kernel error
         * stays visible, rather than being hidden as a passed event. */
        int routed = second
            ? native_store_signal_process(&session->store, (int)second, (int)first)
            : posix_kernel_signal_raise(session->store.kernel, (int)first);
        if (routed) goto invalid;
        session->signal_events++;
    } else if (operation == 3 && first && first <= POSIX_SIGNAL_MAX && second) {
        /* Process-group fan-out: deliver the signal to every live member of
         * the named pgid via the shared store.  Zero members is a protocol
         * error (callers should keep groups non-empty), so a negative or
         * zero result is reported as an invalid record rather than silently
         * dropped. */
        int delivered = native_store_signal_process_group(&session->store,
                                                           (int)second,
                                                           (int)first);
        if (delivered <= 0) goto invalid;
        session->signal_events++;
    } else if (operation == 4 && session->clock_monotonic_fixed &&
               session->clock_monotonic_ns) {
        /* Absolute u64 nanoseconds, low word first. Only a preconfigured
         * frozen guest clock can advance; policy deadlines keep host time. */
        uint64_t value = ((uint64_t)second << 32) | first;
        if (value < session->clock_monotonic_ns) goto invalid;
        session->clock_monotonic_ns = value;
        session->clock_events++;
    } else goto invalid;
    return 1;
invalid:
    snprintf(error->message, sizeof(error->message), "invalid native session control record");
    error->status = EXEC_ERROR_UNSUPPORTED;
    return -1;
}

static int session_input(cli_guest_session *session, exec_error *error) {
    int selecting = error->yield_reason == EXEC_YIELD_SELECT;
    if (session->input_eof && !selecting) {
        snprintf(error->message, sizeof(error->message), "guest yielded after terminal EOF");
        return 0;
    }
    for (;;) {
        uint64_t host = host_now();
        if (exec_execution_check(&session->store.execution_control, error) != EXEC_OK) {
            return 0;
        }
        posix_wait_record *wait = &session->store.kernel->wait;
        uint64_t deadline = session->deadline;
        if (session->cancel_deadline && session->cancel_deadline < deadline)
            deadline = session->cancel_deadline;
        if (selecting && wait->active && wait->has_deadline) {
            /* The guest's SELECT deadline uses the kernel-visible clock, which
             * may be a scripted deterministic value independent of host time.
             * Return immediately when already past it; otherwise bound the
             * host poll by the remaining real-time difference. */
            uint64_t guest = session_now(session);
            if (guest >= wait->deadline_ns) return 1;
            uint64_t guest_remaining = wait->deadline_ns - guest;
            if (host + guest_remaining < deadline)
                deadline = host + guest_remaining;
        }
        uint64_t remaining = (deadline > host ? deadline - host : 0u) + 999999u;
        remaining /= 1000000u;
        int timeout = remaining > INT_MAX ? INT_MAX : (int)remaining;
        struct pollfd descriptors[2] = {
            {session->input_eof ? -1 : STDIN_FILENO, POLLIN, 0},
            {session->control_fd, POLLIN, 0}
        };
        int ready = poll(descriptors, 2, timeout);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0 || (descriptors[0].revents | descriptors[1].revents) &
                         (POLLERR | POLLNVAL)) {
            snprintf(error->message, sizeof(error->message), "host terminal input failed");
            return 0;
        }
        if (!ready) continue;
        if (descriptors[1].revents & (POLLIN | POLLHUP)) {
            int applied = session_control_read(session, error);
            if (applied) return applied > 0;
        }
        if (!(descriptors[0].revents & (POLLIN | POLLHUP))) continue;
        uint8_t bytes[1024];
        ssize_t count = read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            snprintf(error->message, sizeof(error->message), "cannot read host terminal input");
            return 0;
        }
        if (count == 0) {
            session->input_eof = 1;
            if (posix_kernel_terminal_signal_eof(session->store.kernel, 0) == 0) return 1;
        } else if (posix_kernel_terminal_enqueue(session->store.kernel, 0,
                                                  bytes, (int)count) == 0) {
            session->input_bytes += (unsigned)count;
            return 1;
        }
        snprintf(error->message, sizeof(error->message), "cannot enqueue guest terminal input");
        return 0;
    }
}

/* Apply the next queued host-IO reply to the store's pending yield.  Returns 1
 * if a matching reply was consumed, 0 if none remained.  Mismatched kinds are
 * reported as failures: the test contract and CLI invocation must agree. */
#include "guest_suite.h"

static int session_host_io(cli_guest_session *session, exec_error *error) {
    native_host_io_state *host_io = &session->store.host_io;
    if (host_io->kind == NATIVE_HOST_IO_TEST_SUITE) {
        if (session_test_suite(session, error)) return 1;
        const char *message = "waste-test: native batch unavailable or report exceeds 16 MiB\n";
        return session_suite_reply(host_io, 2, (const uint8_t *)message, strlen(message), NULL, 0);
    }
    if (host_io->kind == NATIVE_HOST_IO_UPLOAD) session->upload_events++;
    else if (host_io->kind == NATIVE_HOST_IO_DOWNLOAD) session->download_events++;
    else {
        snprintf(error->message, sizeof(error->message), "host-io yield without pending request");
        return 0;
    }
    if (session->host_io_reply_count == 0) {
        snprintf(error->message, sizeof(error->message),
                 "host-io yield with no scripted --host-%s-reply queued",
                 host_io->kind == NATIVE_HOST_IO_UPLOAD ? "upload" : "download");
        return 0;
    }
    session_host_io_reply *reply = &session->host_io_replies[session->host_io_reply_head];
    if (reply->kind != (native_host_io_kind)host_io->kind) {
        snprintf(error->message, sizeof(error->message),
                 "scripted host-io reply kind mismatch (expected %s, got %s)",
                 reply->kind == NATIVE_HOST_IO_UPLOAD ? "upload" : "download",
                 host_io->kind == NATIVE_HOST_IO_UPLOAD ? "upload" : "download");
        return 0;
    }
    if (reply->cancel) {
        host_io->result = -1;
    } else if (reply->kind == NATIVE_HOST_IO_UPLOAD) {
        free(host_io->data);
        host_io->data = reply->bytes;
        host_io->data_len = reply->length;
        reply->bytes = NULL;
        reply->length = 0;
        host_io->result = 1;
    } else {
        host_io->result = 1;
    }
    free(reply->bytes);
    reply->bytes = NULL;
    reply->length = 0;
    session->host_io_reply_head = (session->host_io_reply_head + 1) % SESSION_HOST_IO_REPLY_MAX;
    session->host_io_reply_count--;
    return 1;
}

static void session_handler_result(void *opaque, int passed,
                                    const char *name, const char *message) {
    cli_guest_session *session = opaque;
    (void)name;
    session->total++;
    session->passed += passed != 0;
    if (!passed) {
        session->handler_failures++;
        snprintf(session->handler_error, sizeof(session->handler_error), "%s", message ? message : "failure");
    }
    /* A failed child assertion exits that handler with status 1; it must not
     * stop the parent shell or be misreported as a root session failure. */
}

static exec_status session_handler_step(const uint8_t *source, size_t size,
    size_t offset, unsigned line, size_t *next_offset, unsigned *next_line, void *opaque) {
    wast_process_handler *handler = opaque;
    exec_status status = wast_process_handler_step(source, size, offset, line,
                                                    next_offset, next_line, handler);
    cli_guest_session *session = handler->result_data;
    session->driver.handler_wait_reason = handler->wait_reason;
    return status;
}

static exec_status session_invoke(void *opaque, waste_exec_engine *engine,
                                  uint32_t function, const wasm_value *args,
                                  int argc, wasm_value *results, int *result_count,
                                  exec_error *error) {
    cli_guest_session *session = opaque;
    for (;;) {
        exec_status status = native_process_driver_invoke(&session->driver,
            &session->store, engine, function, args, argc, results, result_count, error);
        if (status == EXEC_ERROR_EXIT) {
            session->exited = 1;
            session->exit_code = error->exit_code;
            session->stopped = 1;
        }
        if (status != EXEC_YIELD) return status;
        if (error->yield_reason != EXEC_YIELD_READ &&
            error->yield_reason != EXEC_YIELD_SELECT &&
            error->yield_reason != EXEC_YIELD_HOST_IO) {
            snprintf(error->message, sizeof(error->message),
                     "native session transition %d not implemented", (int)error->yield_reason);
            error->status = EXEC_ERROR_TRAP;
            return error->status;
        }
        int host_io_yield = error->yield_reason == EXEC_YIELD_HOST_IO;
        if (error->yield_reason == EXEC_YIELD_READ) session->waits++;
        else if (error->yield_reason == EXEC_YIELD_SELECT) session->select_waits++;
        if (session->trace_waits && !host_io_yield) {
            fprintf(stderr, "{\"wait\":%u,\"pid\":%d,\"kind\":%d}\n",
                    session->waits + session->select_waits,
                    native_store_getpid(&session->store), (int)error->yield_reason);
            fflush(stderr);
        }
        if (host_io_yield) {
            if (!session_host_io(session, error)) {
                error->status = EXEC_ERROR_TRAP;
                return error->status;
            }
        } else if (!session_input(session, error)) {
            if (error->status != EXEC_ERROR_INTERRUPTED &&
                error->status != EXEC_ERROR_UNSUPPORTED)
                error->status = EXEC_ERROR_TRAP;
            return error->status;
        }
        native_driver_selection *selected = &session->driver.selection;
        if (native_store_set_active_process(&session->store, selected->pid) != 0)
            return exec_fail(error, EXEC_ERROR_TRAP, "native session process disappeared");
        exec_yield_reason handler_reason = EXEC_YIELD_NONE;
        if (native_store_process_handler_wait_reason(&session->store, &handler_reason) == 0 &&
            handler_reason != EXEC_YIELD_NONE &&
            native_store_resume_process_handler(&session->store) != 0)
            return exec_fail(error, EXEC_ERROR_TRAP, "cannot resume native WAST handler");
        engine = selected->engine;
        function = selected->func_idx;
        args = selected->args;
        argc = selected->arg_count;
        /* Resume the same engine/entry with the original arguments. The
         * evaluator owns saved PCs, operands, locals and pending imports. */
    }
}

static void session_failure(cli_guest_session *session, const char *message) {
    session->stopped = 1;
    snprintf(session->error, sizeof(session->error), "%s", message);
}

static int session_command(wast_stream_command_kind kind, const char *bytes,
                            size_t length, size_t offset, unsigned line,
                            wast_script *parsed, void *opaque) {
    cli_guest_session *session = opaque;
    (void)bytes; (void)length; (void)offset; (void)line;
    exec_error stop_error = {0};
    if (exec_execution_check(&session->store.execution_control, &stop_error) != EXEC_OK) {
        session_failure(session, stop_error.message);
        return 0;
    }
    if (parsed->error[0]) {
        session_failure(session, parsed->error);
        return 0;
    }
    if (kind == WAST_STREAM_REGISTER && parsed->group_count == 1) {
        wast_module *module = &parsed->groups[0].module;
        native_linked_module *selected = native_selected_module(&session->store,
                                                                  module->register_target);
        if (!selected) session_failure(session, "unknown register target");
        else snprintf(selected->registered, sizeof(selected->registered), "%s", module->register_name);
        return 0;
    }
    if (kind == WAST_STREAM_MODULE && parsed->group_count == 1 &&
        !parsed->groups[0].has_module_assertion &&
        !parsed->groups[0].module.is_definition &&
        !parsed->groups[0].module.instance_of[0]) {
        if (session->retained_count == SESSION_COMMAND_MAX) {
            session_failure(session, "native session retained-command bound exceeded");
            return 0;
        }
        wast_script *owned = malloc(sizeof(*owned));
        if (!owned) {
            session_failure(session, "cannot retain guest module");
            return 0;
        }
        *owned = *parsed;
        memset(parsed, 0, sizeof(*parsed));
        session->retained[session->retained_count++] = owned;
        wast_module *module = &owned->groups[0].module;
        size_t size = 0;
        char encoding_error[256] = {0};
        uint8_t *binary = encode_group_module(&owned->groups[0], &size, encoding_error);
        if (!binary) {
            session_failure(session, encoding_error);
            return 0;
        }
        waste_exec_engine *engine = NULL;
        exec_error error = {0};
        exec_status status = native_load_module(&session->store, module, binary, size,
                                                &engine, &error);
        free(binary);
        if (status != EXEC_OK) {
            exec_free(engine);
            session_failure(session, error.message);
        } else if (!native_store_add(&session->store, engine, module, module)) {
            exec_free(engine);
            session_failure(session, "cannot retain guest instance");
        }
        return 0;
    }
    if ((kind == WAST_STREAM_ASSERTION || kind == WAST_STREAM_INVOKE) &&
        parsed->group_count == 1 && !parsed->groups[0].has_module_assertion) {
        for (int i = 0; i < parsed->assertion_count && !session->stopped; i++) {
            const wast_assertion *assertion = &parsed->assertions[i];
            waste_exec_engine *engine = native_selected_engine(&session->store, assertion->module_id);
            exec_error error = {0};
            exec_status status = engine ? wast_run_assertion_with_invoke(
                engine, assertion, &error, session_invoke, session) : EXEC_ERROR_NOT_FOUND;
            session->total++;
            if (status == EXEC_OK) session->passed++;
            else session_failure(session, error.message[0] ? error.message : "unknown guest module");
        }
        return 0;
    }
    session_failure(session, "unsupported native session command; use waste-cli for conformance WAST");
    return 0;
}

static uint8_t *session_read_file(const char *path, size_t limit, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    long size = -1;
    if (!fseek(file, 0, SEEK_END)) size = ftell(file);
    uint8_t *bytes = size >= 0 && (unsigned long)size <= limit ? malloc((size_t)size + 1) : NULL;
    if (bytes && (!fseek(file, 0, SEEK_SET)) && fread(bytes, 1, (size_t)size, file) == (size_t)size) {
        bytes[size] = 0;
        *length = (size_t)size;
    } else {
        free(bytes);
        bytes = NULL;
    }
    fclose(file);
    return bytes;
}

static void session_json_string(FILE *file, const char *value) {
    fputc('"', file);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p == '\\' || *p == '"') fprintf(file, "\\%c", *p);
        else if (*p < 32 || *p >= 127) fprintf(file, "\\u%04x", *p);
        else fputc(*p, file);
    }
    fputc('"', file);
}

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

/* Append one scripted host-IO reply.  Called during CLI parsing so the ordered
 * sequence matches the guest's upload/download yield order. */
static int session_queue_host_io_reply(cli_guest_session *session,
                                         native_host_io_kind kind,
                                         int cancel, const char *host_file) {
    if (session->host_io_reply_count == SESSION_HOST_IO_REPLY_MAX) return -1;
    int tail = (session->host_io_reply_head + session->host_io_reply_count) %
               SESSION_HOST_IO_REPLY_MAX;
    session_host_io_reply *reply = &session->host_io_replies[tail];
    memset(reply, 0, sizeof(*reply));
    reply->kind = kind;
    reply->cancel = cancel;
    if (host_file) {
        size_t length = 0;
        uint8_t *bytes = session_read_file(host_file, NATIVE_EXEC_BYTES_MAX, &length);
        if (!bytes) return -1;
        reply->bytes = bytes;
        reply->length = length;
    }
    session->host_io_reply_count++;
    return 0;
}

int main(int argc, char **argv) {
    const char *root_path = NULL, *script_path = NULL, *result_path = NULL;
    unsigned timeout = 30000, cancel_after = 0, columns = 80, rows = 24;
    int trace_waits = 0, control_fd = -1;
    int clock_realtime_fixed = 0, clock_monotonic_fixed = 0;
    uint64_t clock_realtime_ns = 0, clock_monotonic_ns = 0;
    struct { const char *guest, *host; unsigned mode; } staged[64];
    unsigned staged_count = 0;
    /* Scripted host-IO replies accumulated before the session starts.  They
     * are copied into the session's queue after native_store_init runs. */
    struct { native_host_io_kind kind; int cancel; const char *host_file; }
        pending_io[SESSION_HOST_IO_REPLY_MAX];
    unsigned pending_io_count = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            puts("usage: waste-session --vfs-root DIRECTORY [--script HOST.wast] [--result-file JSON]\n"
                 "       [--timeout-ms N] [--columns N] [--rows N] [--trace-waits]\n"
                 "       [--cancel-after-ms N] [--control-fd N]\n"
                 "       [--clock-realtime-ns N] [--clock-monotonic-ns N]\n"
                 "       [--host-upload-reply HOST_FILE | --host-upload-cancel]...\n"
                 "       [--host-download-complete | --host-download-cancel]...\n"
                 "       [--stage-file GUEST_PATH OCTAL_MODE HOST_FILE]...\n"
                 "Default script: mounted /usr/share/waste/launch.wast. Guest output: stdout.\n"
                 "Input is consumed only after a guest READ/SELECT yield. Limits cover interpreted execution and waits.\n"
                 "Timeout exits 124; scheduled cancellation exits 125. Neither is a guest trap/exit.\n"
                 "Control-fd >= 3 accepts 16-byte WSC1 resize/signal/clock records at READ/SELECT waits.\n"
                 "Clock overrides freeze the kernel-visible monotonic/realtime clocks; host deadlines keep real time.\n"
                 "Host-upload/download replies are consumed in FIFO order at the guest's host-io yields.\n"
                 "Result-file must not already exist. Bounded child-first fork/exec is supported.\n"
                 "Use waste-cli for ordinary/full-conformance WAST.");
            return 0;
        }
        if (!strcmp(argv[i], "--trace-waits")) { trace_waits = 1; continue; }
        if (!strcmp(argv[i], "--host-upload-cancel")) {
            if (pending_io_count == SESSION_HOST_IO_REPLY_MAX) goto invalid;
            pending_io[pending_io_count].kind = NATIVE_HOST_IO_UPLOAD;
            pending_io[pending_io_count].cancel = 1;
            pending_io[pending_io_count++].host_file = NULL;
            continue;
        }
        if (!strcmp(argv[i], "--host-download-complete")) {
            if (pending_io_count == SESSION_HOST_IO_REPLY_MAX) goto invalid;
            pending_io[pending_io_count].kind = NATIVE_HOST_IO_DOWNLOAD;
            pending_io[pending_io_count].cancel = 0;
            pending_io[pending_io_count++].host_file = NULL;
            continue;
        }
        if (!strcmp(argv[i], "--host-download-cancel")) {
            if (pending_io_count == SESSION_HOST_IO_REPLY_MAX) goto invalid;
            pending_io[pending_io_count].kind = NATIVE_HOST_IO_DOWNLOAD;
            pending_io[pending_io_count].cancel = 1;
            pending_io[pending_io_count++].host_file = NULL;
            continue;
        }
        if (!strcmp(argv[i], "--stage-file")) {
            if (i + 3 >= argc || staged_count == 64) goto invalid;
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
        else if (!strcmp(option, "--result-file")) result_path = value;
        else if (!strcmp(option, "--timeout-ms")) {
            if (!session_number(value, 3600000, &timeout)) goto invalid;
        } else if (!strcmp(option, "--cancel-after-ms")) {
            if (!session_number(value, 3600000, &cancel_after)) goto invalid;
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
            if (pending_io_count == SESSION_HOST_IO_REPLY_MAX) goto invalid;
            pending_io[pending_io_count].kind = NATIVE_HOST_IO_UPLOAD;
            pending_io[pending_io_count].cancel = 0;
            pending_io[pending_io_count++].host_file = value;
        } else goto invalid;
    }
    if (!root_path || (result_path &&
        (!strcmp(result_path, root_path) || (script_path && !strcmp(result_path, script_path))))) goto invalid;
    cli_guest_session session = {0};
    session.suite_root_fd = -1;
    session.trace_waits = trace_waits;
    session.control_fd = control_fd;
    session.clock_realtime_fixed = clock_realtime_fixed;
    session.clock_realtime_ns = clock_realtime_ns;
    session.clock_monotonic_fixed = clock_monotonic_fixed;
    session.clock_monotonic_ns = clock_monotonic_ns;
    for (unsigned i = 0; i < pending_io_count; i++) {
        if (session_queue_host_io_reply(&session, pending_io[i].kind,
                                         pending_io[i].cancel,
                                         pending_io[i].host_file)) goto invalid;
    }
    native_store_init(&session.store);
    session.store.test_suite_enabled = 1;
    struct sigaction interrupt_action = {0};
    interrupt_action.sa_handler = session_interrupt;
    sigemptyset(&interrupt_action.sa_mask);
    if (sigaction(SIGINT, &interrupt_action, NULL) ||
        sigaction(SIGTERM, &interrupt_action, NULL)) return 2;
    native_process_driver_init(&session.driver);
    wast_process_handler_init(&session.handler, &session.store, session_handler_result, &session);
    session.driver.handler_step = session_handler_step;
    session.driver.handler_reset = wast_process_handler_reset;
    session.driver.handler_context = &session.handler;
    native_store_enable_terminal(&session.store);
    session.store.guest_platform = &session_platform;
    session.store.guest_platform_data = &session;
    session.store.host_resolver = guest_posix_host_resolver;
    session.store.host_context = &session.store;
    session.deadline = host_now() + (uint64_t)timeout * 1000000u;
    if (cancel_after)
        session.cancel_deadline = session.deadline - (uint64_t)timeout * 1000000u +
                                  (uint64_t)cancel_after * 1000000u;
    session.store.execution_control.poll = session_control;
    session.store.execution_control.context = &session;
    size_t source_length = 0;
    uint8_t *source = NULL;
    waste_vfs vfs = {0};
    char vfs_error[256];
    session.suite_root_fd = native_vfs_open_root(root_path);
    if (!session.store.kernel ||
        native_vfs_load_at(session.suite_root_fd, &vfs, vfs_error, sizeof(vfs_error)) ||
        waste_vfs_mount(session.store.kernel, &vfs))
        session_failure(&session, "cannot mount native session VFS tree");
    waste_vfs_free(&vfs);
    size_t staged_bytes = 0;
    for (unsigned i = 0; i < staged_count && !session.stopped; i++) {
        size_t length = 0;
        uint8_t *bytes = session_read_file(staged[i].host, NATIVE_EXEC_BYTES_MAX, &length);
        posix_path_metadata metadata = {POSIX_NODE_REGULAR, staged[i].mode,
            0, 0, (int64_t)length, 1000u + i, 0, 0};
        if (!bytes || length > SESSION_SOURCE_MAX - staged_bytes ||
            posix_kernel_path_add_data(session.store.kernel, staged[i].guest,
                                        &metadata, bytes, length))
            session_failure(&session, "cannot stage explicit native session file");
        staged_bytes += length;
        free(bytes);
    }
    if (!session.stopped) {
        posix_kernel_set_clock(session.store.kernel, session_now, &session);
        posix_kernel_set_realtime_clock(session.store.kernel, session_realtime, &session);
        posix_winsize dimensions = {(uint16_t)rows, (uint16_t)columns, 0, 0};
        if (posix_kernel_path_set_cwd(session.store.kernel, "/root") ||
            posix_kernel_terminal_set_winsize(session.store.kernel, 0, &dimensions))
            session_failure(&session, "cannot configure native session terminal");
        if (script_path) source = session_read_file(script_path, SESSION_SOURCE_MAX, &source_length);
        else {
            posix_path_metadata metadata;
            const char *path = "/usr/share/waste/launch.wast";
            if (posix_kernel_path_read_snapshot(session.store.kernel, (const uint8_t *)path,
                    strlen(path), SESSION_SOURCE_MAX, &source, &source_length, &metadata)) source = NULL;
        }
        if (!source) session_failure(&session, "cannot load native session script");
    }
    wast_stream stream;
    wast_stream_init(&stream, (const char *)source, source_length);
    while (!session.stopped) {
        int status = wast_stream_next(&stream, session_command, &session);
        if (status == 0) break;
        if (status < 0) {
            session_failure(&session, stream.error);
            break;
        }
    }
    wast_stream_destroy(&stream);
    free(source);
    if (!session.error[0] && !session.total) session_failure(&session, "native session ran no invocations");
    /* Never truncate an existing file (including a hardlink/symlink to an
     * input). Evidence consumers should choose a fresh result path. */
    FILE *result = result_path ? fopen(result_path, "wx") : stderr;
    if (result) {
        fprintf(result, "{\"runtime\":\"native-guest-session\",\"passed\":%u,\"total\":%u,"
                        "\"forks\":%u,\"execs\":%u,\"childExits\":%u,"
                        "\"readWaits\":%u,\"selectWaits\":%u,\"inputBytes\":%u,\"resizeEvents\":%u,\"signalEvents\":%u,"
                        "\"uploadEvents\":%u,\"downloadEvents\":%u,\"clockEvents\":%u,"
                        "\"timedOut\":%s,\"cancelled\":%s,\"exited\":%s,"
                        "\"exitStatus\":%d,\"error\":", session.passed, session.total,
                        session.driver.forks, session.driver.execs, session.driver.child_exits,
                        session.waits, session.select_waits, session.input_bytes,
                        session.resize_events, session.signal_events,
                        session.upload_events, session.download_events, session.clock_events,
                        session.timed_out ? "true" : "false",
                        session.cancelled ? "true" : "false",
                        session.exited ? "true" : "false", session.exit_code);
        session_json_string(result, session.error);
        fprintf(result, ",\"handlerFailures\":%u,\"handlerError\":", session.handler_failures);
        session_json_string(result, session.handler_error);
        fputs("}\n", result);
        if (result_path && fclose(result)) session_failure(&session, "cannot write native session results");
    } else session_failure(&session, "cannot open native session results");
    native_process_driver_destroy(&session.driver);
    wast_process_handler_reset(&session.handler);
    native_store_free(&session.store);
    wast_process_handler_destroy(&session.handler);
    for (int i = 0; i < session.retained_count; i++) {
        wast_script_free(session.retained[i]);
        free(session.retained[i]);
    }
    for (int i = 0; i < SESSION_HOST_IO_REPLY_MAX; i++)
        free(session.host_io_replies[i].bytes);
    if (session.suite_root_fd >= 0) close(session.suite_root_fd);
    if (session.error[0]) fprintf(stderr, "%s\n", session.error);
    return session.timed_out ? 124 : session.cancelled ? 125 : session.error[0] ? 1 : session.exit_code;
invalid:
    fputs("invalid waste-session arguments; use --help\n", stderr);
    return 2;
}
