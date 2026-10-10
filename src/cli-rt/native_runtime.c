/* Native guest session: mounted inputs and shared guest ABI/process driver. */
#define _POSIX_C_SOURCE 200809L
#include "native_runtime.h"
#include "guest_posix.h"
#include "process_driver.h"
#include "native_vfs.h"
#include "native_terminal.h"
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

struct cli_guest_session {
    native_terminal terminal;
    struct sigaction previous_signals[2];
    int signal_count, host_stdio, host_signal;
    int suite_root_fd;
    native_store store;
    native_process_driver driver;
    wast_process_handler handler;
    wast_script *retained[NATIVE_SESSION_MAX_COMMANDS];
    int retained_count;
    unsigned passed, total, waits, select_waits, input_bytes;
    unsigned resize_events, signal_events, clock_events;
    unsigned upload_events, download_events;
    int merge_output, report_default, diagnostic_output, raw_output;
    unsigned columns, rows;
    int control_fd;
    uint8_t control_record[16];
    size_t control_length;
    size_t staged_bytes;
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
    session_host_io_reply host_io_replies[NATIVE_HOST_IO_REPLY_MAX];
    int host_io_reply_count;
    int host_io_reply_head;
};
typedef struct cli_guest_session cli_guest_session;

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

static uint64_t session_pump_now(void *opaque) {
    (void)opaque;
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
    if (session->host_stdio) {
        if (native_terminal_resized()) {
            if (native_terminal_resize(&session->terminal, session->store.kernel) < 0)
                snprintf(session->error, sizeof(session->error), "cannot update host terminal size");
            else session->resize_events++;
        }
        int signal;
        while ((signal = native_terminal_take_signal()) != 0) {
            native_store_signal_foreground(&session->store, signal);
            session->signal_events++;
        }
    }
    if (session_interrupted) { session->cancelled = 1; return EXEC_STOP_CANCELLED; }
    if (session->cancel_deadline && now >= session->cancel_deadline &&
        (!session->deadline || session->cancel_deadline <= session->deadline)) {
        session->cancelled = 1;
        return EXEC_STOP_CANCELLED;
    }
    if (session->deadline && now >= session->deadline) {
        session->timed_out = 1;
        return EXEC_STOP_TIMEOUT;
    }
    return EXEC_STOP_NONE;
}

static int32_t session_write(void *opaque, int32_t descriptor,
                              const void *bytes, uint32_t length) {
    cli_guest_session *session = opaque;
    if (session->host_stdio) {
        if (descriptor < 0 || descriptor >= POSIX_KERNEL_FD_MAX) return -POSIX_EBADF;
        const posix_ofd *ofd = session->store.kernel->fds[descriptor].ofd;
        if (!ofd || (ofd->kind != POSIX_OFD_TERMINAL && ofd->kind != POSIX_OFD_STREAM) ||
            ofd->output_handle < 0 || ofd->output_handle >= 3) return -POSIX_EBADF;
        int host = session->terminal.handles[ofd->output_handle];
        uint32_t at = 0;
        while (at < length) {
            if (native_terminal_signal()) return -POSIX_EINTR;
            ssize_t n = write(host, (const uint8_t *)bytes + at, length - at);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return errno == EPIPE ? -POSIX_EPIPE : -POSIX_EIO;
            at += (uint32_t)n;
        }
        return (int32_t)length;
    }
    if (descriptor != 1 && descriptor != 2) return -POSIX_EBADF;
    /* One transcript stream, like the browser worker. Host stdin/stdout are
     * the only capabilities; guest paths never become host filesystem calls. */
    uint32_t at = 0;
    while (at < length) {
        ssize_t written = write(session->diagnostic_output ? STDERR_FILENO : session->merge_output ? STDOUT_FILENO : descriptor, (const uint8_t *)bytes + at,
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
    int pumping = error->yield_reason == EXEC_YIELD_PUMP;
    posix_wait_record *initial_wait = &session->store.kernel->wait;
    if (!session->store.kernel_terminal &&
        !pumping && (!selecting || !initial_wait->active || !initial_wait->has_deadline)) {
        (void)exec_fail(error, EXEC_ERROR_UNSUPPORTED,
                       "external input waits require a runtime terminal context");
        return 0;
    }
    if (session->input_eof && !selecting && !pumping && !session->host_stdio) {
        snprintf(error->message, sizeof(error->message), "guest yielded after terminal EOF");
        return 0;
    }
    for (;;) {
        uint64_t host = host_now();
        exec_status checked = exec_execution_check(&session->store.execution_control, error);
        if (checked != EXEC_OK) return checked == EXEC_ERROR_EXIT;
        posix_wait_record *wait = &session->store.kernel->wait;
        if (wait->active) {
            int state = posix_kernel_wait_poll(session->store.kernel);
            if (state == POSIX_WAIT_SIGNAL ||
                (selecting && state != POSIX_WAIT_BLOCKED)) return 1;
        }
        uint64_t deadline = session->deadline ? session->deadline : UINT64_MAX;
        if (session->cancel_deadline && session->cancel_deadline < deadline)
            deadline = session->cancel_deadline;
        uint64_t scheduled_remaining;
        if (native_process_driver_wait_timeout(&session->store, &scheduled_remaining)) {
            if (!scheduled_remaining) return 1;
            if (deadline > host && scheduled_remaining < deadline - host)
                deadline = host + scheduled_remaining;
        }
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
        int timeout = deadline == UINT64_MAX ? -1 : remaining > INT_MAX ? INT_MAX : (int)remaining;
        if (pumping) timeout = 0;
        struct pollfd descriptors[4] = {
            {session->input_eof || !session->store.kernel_terminal ? -1 : STDIN_FILENO, POLLIN, 0},
            {session->control_fd, POLLIN, 0}, {-1, POLLIN, 0}, {-1, POLLIN, 0}
        };
        int guest_fds[3] = {-1, -1, -1};
        if (session->host_stdio) {
            descriptors[0].fd = -1;
            for (int fd = 0; fd < POSIX_KERNEL_FD_MAX; fd++) {
                posix_ofd *ofd = session->store.kernel->fds[fd].ofd;
                if (!ofd || (ofd->kind != POSIX_OFD_TERMINAL && ofd->kind != POSIX_OFD_STREAM) ||
                    (pumping && ofd->kind != POSIX_OFD_TERMINAL) ||
                    ofd->input_handle < 0 || ofd->input_handle >= 3 || ofd->terminal.eof ||
                    (wait->active && !posix_fd_isset(fd, &wait->readfds))) continue;
                int handle = ofd->input_handle;
                int index = handle ? handle + 1 : 0;
                if (guest_fds[handle] >= 0) continue;
                descriptors[index].fd = session->terminal.handles[handle];
                guest_fds[handle] = fd;
            }
        }
        int ready = poll(descriptors, session->host_stdio ? 4 : 2, timeout);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0 || (descriptors[0].revents | descriptors[1].revents) &
                         (POLLERR | POLLNVAL)) {
            snprintf(error->message, sizeof(error->message), "host terminal input failed");
            return 0;
        }
        if (!ready) { if (pumping) return 1; continue; }
        if (descriptors[1].revents & (POLLIN | POLLHUP)) {
            int applied = session_control_read(session, error);
            if (applied) return applied > 0;
        }
        int input_index = 0, guest_fd = 0;
        if (session->host_stdio) {
            input_index = -1;
            for (int handle = 0; handle < 3; handle++) {
                int index = handle ? handle + 1 : 0;
                if (guest_fds[handle] < 0) continue;
                if (descriptors[index].revents & (POLLERR | POLLNVAL)) {
                    snprintf(error->message, sizeof(error->message), "inherited input stream failed");
                    return 0;
                }
                if (descriptors[index].revents & (POLLIN | POLLHUP)) {
                    input_index = index; guest_fd = guest_fds[handle]; break;
                }
            }
            if (input_index < 0) continue;
        } else if (!(descriptors[0].revents & (POLLIN | POLLHUP))) continue;
        uint8_t bytes[1024];
        size_t capacity = sizeof(bytes);
        if (session->host_stdio) {
            const posix_ofd *ofd = session->store.kernel->fds[guest_fd].ofd;
            size_t available = (size_t)(ofd->terminal.input_capacity - ofd->terminal.input_length);
            if (capacity > available) capacity = available;
            if (!capacity) return 1;
        }
        ssize_t count = read(descriptors[input_index].fd, bytes, capacity);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (count < 0) {
            snprintf(error->message, sizeof(error->message), "cannot read host terminal input");
            return 0;
        }
        if (count == 0) {
            if (!session->host_stdio) session->input_eof = 1;
            if (posix_kernel_terminal_signal_eof(session->store.kernel, guest_fd) == 0) return 1;
        } else if (guest_posix_enqueue_input(&session->store, guest_fd,
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
    session->host_io_reply_head = (session->host_io_reply_head + 1) % NATIVE_HOST_IO_REPLY_MAX;
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
            session->host_signal = error->signal;
            session->stopped = 1;
        }
        if (status != EXEC_YIELD) return status;
        if (error->yield_reason != EXEC_YIELD_READ &&
            error->yield_reason != EXEC_YIELD_SELECT &&
            error->yield_reason != EXEC_YIELD_PUMP &&
            error->yield_reason != EXEC_YIELD_HOST_IO) {
            snprintf(error->message, sizeof(error->message),
                     "native session transition %d not implemented", (int)error->yield_reason);
            error->status = EXEC_ERROR_TRAP;
            return error->status;
        }
        int host_io_yield = error->yield_reason == EXEC_YIELD_HOST_IO;
        if (error->yield_reason == EXEC_YIELD_READ) session->waits++;
        else if (error->yield_reason == EXEC_YIELD_SELECT) session->select_waits++;
        if (session->trace_waits && !host_io_yield && error->yield_reason != EXEC_YIELD_PUMP) {
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
        if (session->retained_count == NATIVE_SESSION_MAX_COMMANDS) {
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
    session_failure(session, "unsupported native session command; use wast for conformance WAST");
    return 0;
}

static uint8_t *session_read_file(const char *path, size_t limit, size_t *length) {
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    struct stat before, after;
    if (fd < 0) return NULL;
    if (fstat(fd, &before) || !S_ISREG(before.st_mode) || before.st_size < 0 ||
        (uint64_t)before.st_size > limit) { close(fd); return NULL; }
    size_t size = (size_t)before.st_size, at = 0;
    uint8_t *bytes = malloc(size + 1);
    while (bytes && at < size) {
        ssize_t got = read(fd, bytes + at, size - at);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        at += (size_t)got;
    }
    uint8_t tail;
    ssize_t extra;
    do { extra = read(fd, &tail, 1); } while (extra < 0 && errno == EINTR);
    int failed = !bytes || at != size || extra != 0 || fstat(fd, &after) ||
        before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec;
    close(fd);
    if (failed) { free(bytes); return NULL; }
    bytes[size] = 0;
    *length = size;
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

/* Append one scripted host-IO reply.  Called during CLI parsing so the ordered
 * sequence matches the guest's upload/download yield order. */
static int session_queue_host_io_reply(cli_guest_session *session,
                                         native_host_io_kind kind,
                                         int cancel, const char *host_file) {
    if (session->host_io_reply_count == NATIVE_HOST_IO_REPLY_MAX) return -1;
    int tail = (session->host_io_reply_head + session->host_io_reply_count) %
               NATIVE_HOST_IO_REPLY_MAX;
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

static void session_process_trace(void *context, const char *event) {
    (void)context;
    fputs("{\"process\":", stderr);
    session_json_string(stderr, event);
    fputs("}\n", stderr);
    fflush(stderr);
}


int native_runtime_finish(native_runtime *session, const char *result_path) {
    if (!session) return 1;
    if (native_terminal_close(&session->terminal))
        session_failure(session, "cannot restore host terminal");
    const int signals[] = {SIGINT, SIGTERM};
    for (int i = session->signal_count - 1; i >= 0; i--)
        if (sigaction(signals[i], &session->previous_signals[i], NULL))
            session_failure(session, "cannot restore native signal adapter");
    /* Never truncate an existing file (including a hardlink/symlink to an
     * input). Evidence consumers should choose a fresh result path. */
    FILE *result = result_path ? fopen(result_path, "wx") : session->report_default ? stderr : NULL;
    if (result) {
        fprintf(result, "{\"runtime\":\"native-guest-session\",\"passed\":%u,\"total\":%u,"
                        "\"forks\":%u,\"execs\":%u,\"childExits\":%u,"
                        "\"readWaits\":%u,\"selectWaits\":%u,\"inputBytes\":%u,\"resizeEvents\":%u,\"signalEvents\":%u,"
                        "\"uploadEvents\":%u,\"downloadEvents\":%u,\"clockEvents\":%u,"
                        "\"timedOut\":%s,\"cancelled\":%s,\"exited\":%s,"
                        "\"exitStatus\":%d,\"signal\":%d,\"error\":", session->passed, session->total,
                        session->driver.forks, session->driver.execs, session->driver.child_exits,
                        session->waits, session->select_waits, session->input_bytes,
                        session->resize_events, session->signal_events,
                        session->upload_events, session->download_events, session->clock_events,
                        session->timed_out ? "true" : "false",
                        session->cancelled ? "true" : "false",
                        session->exited ? "true" : "false", session->exit_code, session->host_signal);
        session_json_string(result, session->error);
        fprintf(result, ",\"handlerFailures\":%u,\"handlerError\":", session->handler_failures);
        session_json_string(result, session->handler_error);
        fputs("}\n", result);
        if (result_path && fclose(result)) session_failure(session, "cannot write native session results");
    } else if (result_path || session->report_default) session_failure(session, "cannot open native session results");
    native_process_driver_destroy(&session->driver);
    wast_process_handler_reset(&session->handler);
    native_store_free(&session->store);
    wast_process_handler_destroy(&session->handler);
    for (int i = 0; i < session->retained_count; i++) {
        wast_script_free(session->retained[i]);
        free(session->retained[i]);
    }
    for (int i = 0; i < NATIVE_HOST_IO_REPLY_MAX; i++)
        free(session->host_io_replies[i].bytes);
    if (session->suite_root_fd >= 0) close(session->suite_root_fd);
    if (session->error[0]) fprintf(stderr, "%s\n", session->error);
    int status = session->host_signal ? 128 + session->host_signal : session->timed_out ? 124 :
        session->cancelled ? 125 : session->error[0] ? 1 : session->exit_code;
    free(session);
    return status;
}

int native_runtime_failed(const native_runtime *session) {
    return !session || session->error[0] != 0;
}

native_runtime *native_runtime_create_vfs(const char *root, const waste_vfs *mounted, const native_runtime_options *options) {
    cli_guest_session *session = calloc(1, sizeof(*session));
    if (!session) return NULL;
    session->suite_root_fd = -1;
    session->host_stdio = options->host_stdio;
    session->control_fd = options->control_fd;
    session->trace_waits = options->trace_waits;
    session->merge_output = options->merge_output;
    session->report_default = options->report_default;
    session->diagnostic_output = options->diagnostic_output;
    session->clock_realtime_fixed = options->clock_realtime_fixed;
    session->clock_realtime_ns = options->clock_realtime_ns;
    session->clock_monotonic_fixed = options->clock_monotonic_fixed;
    session->clock_monotonic_ns = options->clock_monotonic_ns;
    native_store_init(&session->store);
    session->store.test_suite_enabled = !options->disable_test_suite;
    native_process_driver_init(&session->driver);
    wast_process_handler_init(&session->handler, &session->store, session_handler_result, session);
    session->driver.handler_step = session_handler_step;
    session->driver.handler_reset = wast_process_handler_reset;
    session->driver.handler_create = wast_process_handler_create;
    session->driver.handler_destroy = wast_process_handler_destroy_owned;
    session->driver.handler_context = &session->handler;
    if (options->trace_process) session->driver.trace = session_process_trace;
    session->raw_output = options->raw_output;
    session->columns = options->columns;
    session->rows = options->rows;
    if (session->host_stdio) {
        if (native_terminal_open(&session->terminal, session->store.kernel))
            session_failure(session, "cannot configure inherited stdio/host terminal");
        session->store.kernel_terminal = 1;
    } else if (!options->empty_descriptors) native_store_enable_terminal(&session->store);
    if (options->raw_output && !session->host_stdio && session->store.kernel) {
        posix_termios termios;
        if (!posix_kernel_tcgetattr(session->store.kernel, 0, &termios)) {
            termios.oflag = 0;
            (void)posix_kernel_tcsetattr(session->store.kernel, 0, &termios);
        }
    }
    session->store.guest_platform = &session_platform;
    session->store.guest_platform_data = session;
    session->store.host_resolver = guest_posix_host_resolver;
    session->store.host_context = &session->store;
    session_interrupted = 0;
    struct sigaction action = {0};
    action.sa_handler = session_interrupt;
    sigemptyset(&action.sa_mask);
    if (!session->host_stdio) {
        const int signals[] = {SIGINT, SIGTERM};
        for (int i = 0; i < 2; i++) {
            if (sigaction(signals[i], &action, &session->previous_signals[i])) {
                session_failure(session, "cannot configure native signal adapter"); break;
            }
            session->signal_count++;
        }
    }
    uint64_t now = host_now();
    if (options->timeout_ms) session->deadline = now + (uint64_t)options->timeout_ms * 1000000u;
    if (options->cancel_after_ms) session->cancel_deadline = now + (uint64_t)options->cancel_after_ms * 1000000u;
    session->store.execution_control.poll = session_control;
    session->store.execution_control.context = session;
    session->store.execution_control.pump_quantum_ns = UINT64_C(16000000);
    session->store.execution_control.pump_clock_now = session_pump_now;
    session->store.execution_control.pump_clock_context = session;
    waste_vfs vfs = {0};
    char error[256] = {0};
    session->suite_root_fd = native_vfs_open_root(root);
    if (!session->store.kernel || session->suite_root_fd < 0 ||
        (!mounted && native_vfs_load_at(session->suite_root_fd, &vfs, error, sizeof(error))) ||
        waste_vfs_mount(session->store.kernel, mounted ? mounted : &vfs))
        session_failure(session, error[0] ? error : "cannot mount native VFS tree");
    waste_vfs_free(&vfs);
    if (!session->stopped) {
        posix_kernel_set_clock(session->store.kernel, session_now, session);
        posix_kernel_set_realtime_clock(session->store.kernel, session_realtime, session);
        posix_winsize dimensions = {(uint16_t)options->rows, (uint16_t)options->columns, 0, 0};
        if (posix_kernel_path_set_cwd(session->store.kernel, "/root") ||
            (!options->empty_descriptors && !session->host_stdio &&
             posix_kernel_terminal_set_winsize(session->store.kernel, 0, &dimensions)))
            session_failure(session, "cannot configure native session terminal");
    }
    return session;
}

int native_runtime_stage_file(native_runtime *session, const char *guest, unsigned mode, const char *host) {
    if (!session || session->stopped) return -1;
    size_t length = 0;
    uint8_t *bytes = session_read_file(host, NATIVE_EXEC_BYTES_MAX, &length);
    posix_path_metadata metadata = {POSIX_NODE_REGULAR, mode, 0, 0, (int64_t)length, 1000, 0, 0};
    int failed = !bytes || length > NATIVE_SESSION_SOURCE_MAX_BYTES - session->staged_bytes ||
        posix_kernel_path_add_data(session->store.kernel, guest, &metadata, bytes, length);
    if (!failed) session->staged_bytes += length;
    free(bytes);
    if (failed) session_failure(session, "cannot stage explicit native input");
    return failed ? -1 : 0;
}

int native_runtime_queue_reply(native_runtime *session, native_host_io_kind kind, int cancel, const char *host) {
    return session_queue_host_io_reply(session, kind, cancel, host);
}

int native_runtime_stage_input(native_runtime *session, const char *guest, const uint8_t *bytes, size_t length, unsigned mode) {
    if (!session || session->stopped) return -1;
    const char *directory = "/tmp/waste-cli-input";
    int present = posix_kernel_path_access(session->store.kernel, (const uint8_t *)directory,
                                         strlen(directory), POSIX_F_OK, 0);
    posix_path_metadata metadata = {POSIX_NODE_DIRECTORY, 0700, 0, 0, 0, 1000, 0, 0};
    if (present != -POSIX_ENOENT ||
        posix_kernel_path_add(session->store.kernel, directory, &metadata)) {
        session_failure(session, "temporary guest input namespace already exists or is unavailable");
        return -1;
    }
    metadata.kind = POSIX_NODE_REGULAR;
    metadata.mode = mode;
    metadata.size = (int64_t)length;
    int status = posix_kernel_path_add_data(session->store.kernel, guest, &metadata, bytes, length);
    if (status) session_failure(session, "cannot stage explicit native input");
    return status;
}

void native_runtime_script(native_runtime *session, const char *host_script) {
    if (!session || session->stopped) return;
    size_t length = 0;
    uint8_t *source = NULL;
    if (host_script) source = session_read_file(host_script, NATIVE_SESSION_SOURCE_MAX_BYTES, &length);
    else {
        posix_path_metadata metadata;
        const char *path = "/usr/share/waste/launch.wast";
        if (posix_kernel_path_read_snapshot(session->store.kernel, (const uint8_t *)path,
                strlen(path), NATIVE_SESSION_SOURCE_MAX_BYTES, &source, &length, &metadata)) source = NULL;
    }
    if (!source) { session_failure(session, "cannot load native session script"); return; }
    wast_stream stream;
    wast_stream_init(&stream, (const char *)source, length);
    while (!session->stopped) {
        int status = wast_stream_next(&stream, session_command, session);
        if (!status) break;
        if (status < 0) { session_failure(session, stream.error); break; }
    }
    wast_stream_destroy(&stream);
    free(source);
    if (!session->error[0] && !session->total) session_failure(session, "native session ran no invocations");
}

void native_runtime_start(native_runtime *session, const char *guest,
    const char *const *argv, uint32_t argc, const char *const *envp, uint32_t envc,
    const native_process_start_options *options) {
    if (!session || session->stopped) return;
    exec_error error = {0};
    exec_status status = native_process_driver_start_with_options(&session->driver,
        &session->store, guest, argv, argc, envp, envc, options, &error);
    if (status == EXEC_OK) {
        native_driver_selection *entry = &session->driver.selection;
        int count = 0;
        status = session_invoke(session, entry->engine, entry->func_idx, NULL, 0, NULL, &count, &error);
    }
    if (status != EXEC_OK && status != EXEC_ERROR_EXIT)
        session_failure(session, error.message[0] ? error.message : "native process startup failed");
}

native_runtime *native_runtime_create(const char *root, const native_runtime_options *options) {
    return native_runtime_create_vfs(root, NULL, options);
}
native_store *native_runtime_store(native_runtime *runtime) { return runtime ? &runtime->store : NULL; }
static void session_wast_context_ready(void *opaque) {
    native_runtime *runtime = opaque;
    if (runtime->raw_output) {
        posix_termios termios;
        if (!posix_kernel_tcgetattr(runtime->store.kernel, 0, &termios)) {
            termios.oflag = 0;
            (void)posix_kernel_tcsetattr(runtime->store.kernel, 0, &termios);
        }
    }
    posix_winsize dimensions = {(uint16_t)runtime->rows, (uint16_t)runtime->columns, 0, 0};
    (void)posix_kernel_terminal_set_winsize(runtime->store.kernel, 0, &dimensions);
}
int native_runtime_wast(native_runtime *runtime, const char *filename, const char *source,
    size_t length, native_wast_counts *counts, const native_wast_options *requested) {
    if (!runtime || runtime->stopped) { memset(counts, 0, sizeof(*counts)); return 1; }
    native_wast_options options = *requested;
    options.invoke = session_invoke;
    options.invoke_context = runtime;
    options.context_ready = session_wast_context_ready;
    options.driver = &runtime->driver;
    return native_wast_run_with_options(&runtime->store, filename, source, length, counts, &options);
}
