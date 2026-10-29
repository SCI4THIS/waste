#include "../include/waste.h"
#include "../process_driver.h"
#include "../guest_posix.h"
#include "../wasm/decode.h"
#include "adapter.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    uint8_t *bytes;
    uint32_t size, capacity;
} session_buffer;

struct waste_session {
    native_store store;
    native_process_driver driver;
    waste_session_options options;
    waste_session_result result;
    waste_resource_budget resources;
    session_buffer output[2], input;
    uint32_t input_offset, input_total;
    int eof, cancelled, limit, allocation_failed;
    uint64_t started_ns, last_ns;
};

static waste_status report(waste_error *error, waste_status status, const char *message) {
    if (error) {
        memset(error, 0, sizeof(*error));
        error->status = status;
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return status;
}

static int terminal(const waste_session *session) {
    return session->result.state >= WASTE_SESSION_EXITED;
}

static const char *resource_message(const waste_session *session) {
    switch (session->resources.exhausted) {
        case WASTE_RESOURCE_MEMORY: return "guest resident memory page limit exceeded";
        case WASTE_RESOURCE_FILESYSTEM: return "guest filesystem byte limit exceeded";
        case WASTE_RESOURCE_PROCESSES: return "guest process slot limit exceeded";
        case WASTE_RESOURCE_INSTRUCTIONS: return "guest instruction limit exceeded";
        default: return "session output limit exceeded";
    }
}

static posix_kernel *root_kernel(waste_session *session) {
    return session->store.processes[0].kernel;
}

static uint64_t monotonic(void *opaque) {
    waste_session *session = opaque;
    uint64_t now = session->options.monotonic_ns(session->options.host_context);
    if (now < session->last_ns) now = session->last_ns;
    session->last_ns = now;
    return now;
}

static uint64_t realtime(void *opaque) {
    waste_session *session = opaque;
    return session->options.realtime_ns ?
        session->options.realtime_ns(session->options.host_context) : 0;
}

static exec_stop_reason poll_execution(void *opaque) {
    waste_session *session = opaque;
    if (session->cancelled || session->limit || session->allocation_failed)
        return EXEC_STOP_CANCELLED;
    uint64_t now = monotonic(session);
    if (session->options.timeout_ms && now - session->started_ns >=
            (uint64_t)session->options.timeout_ms * 1000000)
        return EXEC_STOP_TIMEOUT;
    return EXEC_STOP_NONE;
}

static int reserve(session_buffer *buffer, uint32_t needed, uint32_t maximum) {
    if (needed <= buffer->capacity) return 1;
    uint32_t capacity = buffer->capacity ? buffer->capacity : 4096;
    while (capacity < needed && capacity <= maximum / 2) capacity *= 2;
    if (capacity < needed || capacity > maximum) capacity = needed;
    uint8_t *bytes = realloc(buffer->bytes, capacity);
    if (!bytes) return 0;
    buffer->bytes = bytes;
    buffer->capacity = capacity;
    return 1;
}

static int32_t capture(void *opaque, int32_t fd, const void *bytes, uint32_t size) {
    waste_session *session = opaque;
    posix_kernel *kernel = session->store.kernel;
    if (fd < 0 || fd >= POSIX_KERNEL_FD_MAX || !kernel->fds[fd].ofd)
        return -POSIX_EBADF;
    int handle = kernel->fds[fd].ofd->output_handle;
    if (handle != 1 && handle != 2) return -POSIX_EBADF;
    uint32_t total = session->output[0].size + session->output[1].size;
    if (size > session->options.max_output_bytes - total) {
        session->limit = 1;
        return -POSIX_ENOSPC;
    }
    session_buffer *buffer = &session->output[handle - 1];
    if (!reserve(buffer, buffer->size + size, session->options.max_output_bytes)) {
        session->allocation_failed = 1;
        return -POSIX_ENOMEM;
    }
    if (size) memcpy(buffer->bytes + buffer->size, bytes, size);
    buffer->size += size;
    return (int32_t)size;
}

static int32_t random_bytes(void *opaque, void *bytes, uint32_t length) {
    waste_session *session = opaque;
    return session->options.random_bytes ? session->options.random_bytes(
        session->options.host_context, bytes, length) : -POSIX_ENOSYS;
}

static const guest_posix_platform session_platform = {
    .write = capture, .random = random_bytes
};

static int bounded_string(const char *text, size_t maximum, size_t *length) {
    if (!text) return 0;
    for (size_t i = 0; i < maximum; i++) if (!text[i]) {
        *length = i;
        return valid_utf8((const uint8_t *)text, i);
    }
    return 0;
}

static int canonical(const char *path) {
    if (!path || path[0] != '/') return 0;
    size_t size;
    if (!bounded_string(path, POSIX_PATH_NODE_NAME_MAX, &size) ||
        (size > 1 && path[size - 1] == '/')) return 0;
    const char *at = path + 1;
    while (*at) {
        const char *end = strchr(at, '/');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        if (!length || (length == 1 && at[0] == '.') ||
            (length == 2 && at[0] == '.' && at[1] == '.')) return 0;
        for (size_t i = 0; i < length; i++)
            if ((unsigned char)at[i] < 32 || at[i] == '\\') return 0;
        at += length;
        if (*at) at++;
    }
    return 1;
}

static waste_status check_path(waste_session *session, const char *path,
                               int populate, waste_error *error) {
    if (!session || !canonical(path))
        return report(error, WASTE_ERROR_MALFORMED, "invalid session path");
    if (populate ? session->result.state != WASTE_SESSION_CONFIGURING :
        session->result.state != WASTE_SESSION_CONFIGURING && !terminal(session))
        return report(error, WASTE_ERROR_UNSUPPORTED, "operation is invalid in this session state");
    return WASTE_OK;
}

static waste_status kernel_error(waste_error *error, int status, const char *message) {
    return report(error, !status ? WASTE_OK : status == -POSIX_ENOENT ? WASTE_ERROR_NOT_FOUND :
                  status == -POSIX_ENOMEM ? WASTE_ERROR_RESOURCE : WASTE_ERROR_MALFORMED, message);
}

static void complete(waste_session *session, uint32_t state, const char *message) {
    session->result.state = state;
    session->result.wait = WASTE_SESSION_WAIT_NONE;
    session->result.wait_ns = UINT64_MAX;
    snprintf(session->result.message, sizeof(session->result.message), "%s", message);
    /* Keep kernel contents available, but no process may run after completion. */
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++)
        if (session->store.processes[i].used)
            session->store.processes[i].capsule.state = NATIVE_PROCESS_EXITED;
}

void waste_session_options_init(waste_session_options *options) {
    if (!options) return;
    memset(options, 0, sizeof(*options));
    options->record_size = sizeof(*options);
    options->abi_major = WASTE_SESSION_ABI_MAJOR;
    options->abi_minor = WASTE_SESSION_ABI_MINOR;
    options->max_input_bytes = 16 * 1024 * 1024;
    options->max_output_bytes = 16 * 1024 * 1024;
    options->quantum_ms = EXECUTION_DEFAULT_PUMP_QUANTUM_MS;
    options->max_memory_pages = 2048;
    options->max_processes = NATIVE_PROCESS_MAX;
    options->max_filesystem_bytes = VFS_MAX_BYTES;
}

exec_status waste_session_adapter_create(waste_session **out, exec_error *error) {
    if (!out) return exec_fail(error, EXEC_ERROR_FORMAT, "missing session output");
    *out = NULL;
    waste_session *session = calloc(1, sizeof(*session));
    if (!session) return exec_fail(error, EXEC_ERROR_RESOURCE, "cannot allocate command session");
    session->result.record_size = sizeof(session->result);
    session->result.exit_code = -1;
    session->result.wait_ns = UINT64_MAX;
    native_store_init(&session->store);
    native_process_driver_init(&session->driver);
    if (!session->store.kernel) {
        waste_session_delete(&session);
        return exec_fail(error, EXEC_ERROR_RESOURCE, "cannot initialize command session");
    }
    *out = session;
    return EXEC_OK;
}

native_store *waste_session_adapter_store(waste_session *session) {
    return session ? &session->store : NULL;
}
native_process_driver *waste_session_adapter_driver(waste_session *session) {
    return session ? &session->driver : NULL;
}
exec_status waste_session_adapter_start(waste_session *session, const char *path,
    const char *const *argv, uint32_t argc, const char *const *envp, uint32_t envc,
    const native_process_start_options *options, exec_error *error) {
    return native_process_driver_start_with_options(&session->driver, &session->store,
        path, argv, argc, envp, envc, options, error);
}
exec_status waste_session_adapter_invoke(waste_session *session, waste_exec_engine *engine,
    uint32_t function, const wasm_value *args, int argc, wasm_value *results,
    int *result_count, exec_error *error) {
    return native_process_driver_invoke(&session->driver, &session->store,
        engine, function, args, argc, results, result_count, error);
}

waste_status waste_session_create(const waste_session_options *options,
    waste_session **out, waste_error *error) {
    if (!out) return report(error, WASTE_ERROR_MALFORMED, "missing session output");
    *out = NULL;
    if (!options || options->record_size != sizeof(*options) ||
        options->abi_major != WASTE_SESSION_ABI_MAJOR ||
        options->abi_minor > WASTE_SESSION_ABI_MINOR ||
        (options->required_features & ~WASTE_SESSION_FEATURES))
        return report(error, WASTE_ERROR_UNSUPPORTED, "incompatible session ABI or features");
    if (!options->monotonic_ns || !options->max_input_bytes ||
        options->max_input_bytes > NATIVE_SESSION_SOURCE_MAX_BYTES ||
        !options->max_output_bytes || options->max_output_bytes > NATIVE_SESSION_SOURCE_MAX_BYTES ||
        !options->quantum_ms || options->quantum_ms > EXECUTION_MAX_PUMP_QUANTUM_MS ||
        options->timeout_ms > EXECUTION_MAX_TIMEOUT_MS ||
        !options->max_memory_pages || options->max_memory_pages > 32768 ||
        !options->max_processes || options->max_processes > NATIVE_PROCESS_MAX ||
        !options->max_filesystem_bytes || options->max_filesystem_bytes > VFS_MAX_BYTES)
        return report(error, WASTE_ERROR_MALFORMED, "invalid session limits or monotonic clock");
    waste_session *session = NULL;
    exec_error initialization = {0};
    if (waste_session_adapter_create(&session, &initialization) != EXEC_OK)
        return report(error, WASTE_ERROR_RESOURCE, initialization.message);
    session->options = *options;
    session->resources.memory_limit = options->max_memory_pages;
    session->resources.file_limit = options->max_filesystem_bytes;
    session->resources.process_limit = options->max_processes;
    session->resources.process_peak = 1;
    session->resources.instruction_limit = options->max_instructions;
    session->store.execution_control.resources = &session->resources;
    session->store.spectest_memory.resources = &session->resources;
    for (uint64_t i = 0; i < session->store.spectest_memory.pages; i++) {
        exec_memory_page *page = session->store.spectest_memory.page_data ?
            session->store.spectest_memory.page_data[i] : NULL;
        if (!page) continue;
        if (!waste_resource_reserve(&session->resources, WASTE_RESOURCE_MEMORY, 1)) goto failed;
        page->resources = &session->resources;
    }
    session->store.guest_platform = &session_platform;
    session->store.guest_platform_data = session;
    session->store.host_resolver = guest_posix_host_resolver;
    session->store.host_context = &session->store;
    session->store.kernel_terminal = 1;
    posix_kernel *kernel = root_kernel(session);
    kernel->resources = &session->resources;
    const char *directories[] = {"/usr", "/usr/bin", "/usr/lib", "/lib", "/root", "/tmp", "/work"};
    posix_path_metadata metadata = {POSIX_NODE_DIRECTORY, 0755, 0, 0, 0, 0, 0, 0};
    for (unsigned i = 0; i < sizeof(directories) / sizeof(directories[0]); i++) {
        metadata.inode = posix_kernel_allocate_inode(kernel);
        if (posix_kernel_path_add(kernel, directories[i], &metadata)) goto failed;
    }
    if (posix_kernel_path_set_cwd(kernel, "/work") ||
        /* Batch streams are non-seekable pipe capabilities, not regular files. */
        posix_kernel_attach_stream(kernel, 0, 0, 0, -1, 0010444) ||
        posix_kernel_attach_stream(kernel, 1, 0, -1, 1, 0010222) ||
        posix_kernel_attach_stream(kernel, 2, 0, -1, 2, 0010222)) goto failed;
    posix_kernel_set_clock(kernel, monotonic, session);
    posix_kernel_set_realtime_clock(kernel, realtime, session);
    session->store.execution_control.poll = poll_execution;
    session->store.execution_control.context = session;
    session->store.execution_control.pump_quantum_ns = (uint64_t)options->quantum_ms * 1000000;
    session->store.execution_control.pump_clock_now = monotonic;
    session->store.execution_control.pump_clock_context = session;
    *out = session;
    return report(error, WASTE_OK, "");
failed:
    waste_session_delete(&session);
    return report(error, WASTE_ERROR_RESOURCE, "cannot initialize command session");
}

void waste_session_delete(waste_session **address) {
    if (!address || !*address) return;
    waste_session *session = *address;
    *address = NULL;
    native_process_driver_destroy(&session->driver);
    native_store_free(&session->store);
    free(session->input.bytes);
    free(session->output[0].bytes);
    free(session->output[1].bytes);
    free(session);
}

static waste_status populate(waste_session *session, const char *path,
    const uint8_t *bytes, uint32_t length, uint32_t mode, int directory, waste_error *error) {
    waste_status status = check_path(session, path, 1, error);
    if (status != WASTE_OK) return status;
    if (mode > 0777 || (length && !bytes) || length > VFS_MAX_BYTES)
        return report(error, WASTE_ERROR_MALFORMED, "invalid session file input");
    char parent[POSIX_PATH_NODE_NAME_MAX];
    snprintf(parent, sizeof(parent), "%s", path);
    char *slash = strrchr(parent, '/');
    if (slash == parent) slash[1] = 0; else *slash = 0;
    posix_kernel *kernel = root_kernel(session);
    posix_path_metadata metadata;
    int result = posix_kernel_path_stat(kernel, (const uint8_t *)parent, strlen(parent), 1, &metadata);
    if (result || metadata.kind != POSIX_NODE_DIRECTORY)
        return report(error, WASTE_ERROR_NOT_FOUND, "session file parent directory does not exist");
    metadata = (posix_path_metadata){directory ? POSIX_NODE_DIRECTORY : POSIX_NODE_REGULAR,
        mode, 0, 0, length, posix_kernel_allocate_inode(kernel), 0, 0};
    result = directory ? posix_kernel_path_add(kernel, path, &metadata) :
        posix_kernel_path_add_data(kernel, path, &metadata, bytes, length);
    if (session->resources.exhausted) {
        session->resources.exhausted = WASTE_RESOURCE_NONE;
        return report(error, WASTE_ERROR_LIMIT, "session filesystem byte limit exceeded");
    }
    return kernel_error(error, result, result ? "cannot populate session file" : "");
}

waste_status waste_session_put_file(waste_session *session, const char *path,
    const uint8_t *bytes, uint32_t length, uint32_t mode, waste_error *error) {
    return populate(session, path, bytes, length, mode, 0, error);
}
waste_status waste_session_mkdir(waste_session *session, const char *path,
    uint32_t mode, waste_error *error) {
    return populate(session, path, NULL, 0, mode, 1, error);
}
waste_status waste_session_set_cwd(waste_session *session, const char *path, waste_error *error) {
    waste_status status = check_path(session, path, 1, error);
    if (status != WASTE_OK) return status;
    int result = posix_kernel_path_set_cwd(root_kernel(session), path);
    return kernel_error(error, result, result ? "invalid working directory" : "");
}

/* Streams are shared OFDs across dup/fork. Locate by capability identity so
 * closing fd 0 does not lose a surviving input alias. Never use host fd numbers. */
static void pump_input(waste_session *session) {
    for (int p = 0; p < NATIVE_PROCESS_MAX; p++) {
        posix_kernel *kernel = session->store.processes[p].kernel;
        if (!session->store.processes[p].used || !kernel) continue;
        for (int fd = 0; fd < POSIX_KERNEL_FD_MAX; fd++) {
            posix_ofd *ofd = kernel->fds[fd].ofd;
            if (!ofd || ofd->kind != POSIX_OFD_STREAM || ofd->input_handle != 0) continue;
            uint32_t room = (uint32_t)(ofd->terminal.input_capacity - ofd->terminal.input_length);
            uint32_t length = session->input.size - session->input_offset;
            if (length > room) length = room;
            if (length && !posix_kernel_terminal_enqueue(kernel, fd,
                    session->input.bytes + session->input_offset, (int)length))
                session->input_offset += length;
            if (session->eof && session->input_offset == session->input.size)
                (void)posix_kernel_terminal_signal_eof(kernel, fd);
            return;
        }
    }
}

waste_status waste_session_input(waste_session *session, const uint8_t *bytes,
    uint32_t length, uint32_t eof, waste_error *error) {
    if (!session || terminal(session) || session->eof || eof > 1 || (length && !bytes))
        return report(error, WASTE_ERROR_MALFORMED, "invalid input state or bytes");
    if (length > session->options.max_input_bytes - session->input_total)
        return report(error, WASTE_ERROR_LIMIT, "session input limit exceeded");
    uint32_t pending = session->input.size - session->input_offset;
    if (session->input_offset && pending)
        memmove(session->input.bytes, session->input.bytes + session->input_offset, pending);
    session->input.size = pending;
    session->input_offset = 0;
    if (!reserve(&session->input, pending + length, session->options.max_input_bytes))
        return report(error, WASTE_ERROR_RESOURCE, "cannot allocate session input");
    if (length) memcpy(session->input.bytes + pending, bytes, length);
    session->input.size += length;
    session->input_total += length;
    session->eof = (int)eof;
    pump_input(session);
    return report(error, WASTE_OK, "");
}

waste_status waste_session_start(waste_session *session, const char *path,
    const char *const *args, uint32_t argc, const char *const *environment,
    uint32_t envc, waste_error *error) {
    if (!session || session->result.state != WASTE_SESSION_CONFIGURING ||
        !canonical(path) || argc >= PROCESS_EXEC_MAX_ARGS || envc > PROCESS_EXEC_MAX_ENV ||
        (argc && !args) || (envc && !environment))
        return report(error, WASTE_ERROR_MALFORMED, "invalid command startup");
    const char *argv[PROCESS_EXEC_MAX_ARGS];
    argv[0] = path;
    size_t vector_bytes = strlen(path) + 1;
    for (uint32_t i = 0; i < argc; i++) {
        size_t size;
        if (!bounded_string(args[i], PROCESS_EXEC_MAX_BYTES - vector_bytes, &size))
            return report(error, WASTE_ERROR_MALFORMED, "invalid or oversized command argument");
        vector_bytes += size + 1;
        argv[i + 1] = args[i];
    }
    for (uint32_t i = 0; i < envc; i++) {
        size_t size;
        if (!bounded_string(environment[i], PROCESS_EXEC_MAX_BYTES - vector_bytes, &size) ||
            !strchr(environment[i], '=') || environment[i][0] == '=')
            return report(error, WASTE_ERROR_MALFORMED, "invalid environment entry");
        vector_bytes += size + 1;
    }
    char pwd[POSIX_PATH_NODE_NAME_MAX + 4];
    snprintf(pwd, sizeof(pwd), "PWD=%s", root_kernel(session)->cwd);
    const char *defaults[] = {"HOME=/work", "PATH=/usr/bin:/bin", "LC_ALL=C", pwd};
    if (!environment) { environment = defaults; envc = 4; }
    session->started_ns = monotonic(session);
    session->store.execution_control.last_pump_ns = session->started_ns;
    pump_input(session);
    exec_error execution = {0};
    native_process_start_options options = {.format = NATIVE_EXEC_FORMAT_WASM};
    exec_status status = waste_session_adapter_start(session,
        path, argv, argc + 1, environment, envc, &options, &execution);
    if (status != EXEC_OK) {
        complete(session, session->limit || session->resources.exhausted ? WASTE_SESSION_LIMIT :
            session->cancelled ? WASTE_SESSION_CANCELLED :
            session->store.execution_control.stopped == EXEC_STOP_TIMEOUT ?
                WASTE_SESSION_TIMED_OUT : WASTE_SESSION_FAILED,
            session->resources.exhausted ? resource_message(session) : execution.message);
        return report(error, session->resources.exhausted ? WASTE_ERROR_LIMIT :
            status == EXEC_ERROR_RESOURCE ? WASTE_ERROR_RESOURCE : WASTE_ERROR_MALFORMED,
            session->result.message);
    }
    session->result.state = WASTE_SESSION_RUNNABLE;
    return report(error, WASTE_OK, "");
}

waste_status waste_session_step(waste_session *session, waste_error *error) {
    if (!session || session->result.state == WASTE_SESSION_CONFIGURING || terminal(session))
        return report(error, WASTE_ERROR_UNSUPPORTED, "session cannot execute in this state");
    pump_input(session);
    exec_error execution = {0};
    native_driver_selection *selection = &session->driver.selection;
    if (selection->pid > 0 && native_store_set_active_process(&session->store, selection->pid))
        return report(error, WASTE_ERROR_TRAP, "cannot select session process");
    int count = 0;
    exec_status status = waste_session_adapter_invoke(session,
        selection->engine, selection->func_idx, selection->args, selection->arg_count,
        NULL, &count, &execution);
    session->result.elapsed_ns = monotonic(session) - session->started_ns;
    if (session->limit || session->resources.exhausted)
        complete(session, WASTE_SESSION_LIMIT, resource_message(session));
    else if (session->allocation_failed) complete(session, WASTE_SESSION_FAILED, "session allocation failed");
    else if (session->cancelled) complete(session, WASTE_SESSION_CANCELLED, "execution cancelled");
    else if (session->store.execution_control.stopped == EXEC_STOP_TIMEOUT)
        complete(session, WASTE_SESSION_TIMED_OUT, "execution deadline exceeded");
    else if (status == EXEC_YIELD) {
        uint64_t deadline = UINT64_MAX;
        int timed = native_process_driver_wait_timeout(&session->store, &deadline);
        session->result.state = WASTE_SESSION_WAITING;
        session->result.wait = execution.yield_reason == EXEC_YIELD_PUMP ? WASTE_SESSION_WAIT_PUMP :
            timed ? WASTE_SESSION_WAIT_TIMER : WASTE_SESSION_WAIT_INPUT;
        session->result.wait_ns = deadline;
        if (session->options.timeout_ms) {
            uint64_t budget = (uint64_t)session->options.timeout_ms * 1000000;
            uint64_t remaining = session->result.elapsed_ns < budget ?
                budget - session->result.elapsed_ns : 0;
            if (remaining < session->result.wait_ns) session->result.wait_ns = remaining;
        }
        if (execution.yield_reason == EXEC_YIELD_HOST_IO) {
            complete(session, WASTE_SESSION_FAILED, "host operation is unavailable in command sessions");
        }
    } else if (status == EXEC_ERROR_RESOURCE ||
               (status == EXEC_ERROR_EXIT && session->driver.completion_status == EXEC_ERROR_RESOURCE)) {
        complete(session, WASTE_SESSION_FAILED, execution.message);
    } else if (status == EXEC_ERROR_EXIT && session->driver.completion_status != EXEC_OK &&
               session->driver.completion_status != EXEC_ERROR_EXIT) {
        complete(session, WASTE_SESSION_TRAPPED, execution.message);
    } else if (status == EXEC_OK || status == EXEC_ERROR_EXIT) {
        complete(session, execution.signal ? WASTE_SESSION_SIGNALED : WASTE_SESSION_EXITED, "");
        session->result.signal = (uint32_t)execution.signal;
        if (!execution.signal) session->result.exit_code = status == EXEC_OK ? 0 : execution.exit_code;
    } else complete(session, WASTE_SESSION_TRAPPED, execution.message);
    return report(error, WASTE_OK, "");
}

void waste_session_cancel(waste_session *session) {
    if (!session || terminal(session)) return;
    session->cancelled = 1;
    if (session->result.state == WASTE_SESSION_CONFIGURING)
        complete(session, WASTE_SESSION_CANCELLED, "execution cancelled");
}

waste_status waste_session_inspect(const waste_session *session, waste_session_result *result) {
    if (!session || !result) return WASTE_ERROR_MALFORMED;
    *result = session->result;
    result->stdout_bytes = session->output[0].size;
    result->stderr_bytes = session->output[1].size;
    result->memory_pages_peak = (uint32_t)session->resources.memory_peak;
    result->filesystem_bytes_peak = (uint32_t)session->resources.file_peak;
    result->processes_peak = session->resources.process_peak;
    result->instructions = session->resources.instructions;
    return WASTE_OK;
}

waste_status waste_session_copy_output(const waste_session *session, uint32_t fd,
    uint32_t offset, uint8_t *bytes, uint32_t capacity, uint32_t *copied, waste_error *error) {
    if (copied) *copied = 0;
    if (!session || !copied || (capacity && !bytes) || (fd != 1 && fd != 2))
        return report(error, WASTE_ERROR_MALFORMED, "invalid output buffer or descriptor");
    const session_buffer *buffer = &session->output[fd - 1];
    if (offset > buffer->size) return report(error, WASTE_ERROR_MALFORMED, "invalid output offset");
    uint32_t length = buffer->size - offset;
    if (length > capacity) length = capacity;
    if (length) memcpy(bytes, buffer->bytes + offset, length);
    *copied = length;
    return report(error, WASTE_OK, "");
}

static void file_info(waste_session_file_info *info, const posix_path_metadata *metadata) {
    *info = (waste_session_file_info){metadata->kind, metadata->mode, (uint64_t)metadata->size,
                                     metadata->mtime_sec, metadata->mtime_nsec};
}
waste_status waste_session_stat(waste_session *session, const char *path,
    waste_session_file_info *info, waste_error *error) {
    waste_status status = check_path(session, path, 0, error);
    if (status != WASTE_OK) return status;
    if (!info) return report(error, WASTE_ERROR_MALFORMED, "missing file metadata output");
    posix_path_metadata metadata;
    int result = posix_kernel_path_stat(root_kernel(session), (const uint8_t *)path, strlen(path), 1, &metadata);
    if (!result) file_info(info, &metadata);
    return kernel_error(error, result, result ? "cannot stat session path" : "");
}
waste_status waste_session_read_file(waste_session *session, const char *path, uint64_t offset,
    uint8_t *bytes, uint32_t capacity, uint32_t *copied, waste_error *error) {
    if (copied) *copied = 0;
    waste_session_file_info info = {0};
    waste_status status = waste_session_stat(session, path, &info, error);
    if (status != WASTE_OK) return status;
    if (!copied || (capacity && !bytes) || info.kind != POSIX_NODE_REGULAR || offset > info.size)
        return report(error, WASTE_ERROR_MALFORMED, "invalid session file range");
    posix_kernel *kernel = root_kernel(session);
    int fd = posix_kernel_open(kernel, (const uint8_t *)path, strlen(path), 0, 0);
    if (fd < 0) return kernel_error(error, fd, "cannot open session file");
    uint64_t remaining = info.size - offset;
    uint32_t length = remaining < capacity ? (uint32_t)remaining : capacity;
    int result = posix_kernel_file_read_at(kernel, fd, offset, bytes, length);
    (void)posix_kernel_close(kernel, fd);
    if (result < 0) return kernel_error(error, result, "cannot copy session file");
    *copied = (uint32_t)result;
    return report(error, WASTE_OK, "");
}
waste_status waste_session_list(waste_session *session, const char *path, uint32_t index,
    char *name, uint32_t capacity, waste_session_file_info *info, waste_error *error) {
    waste_session_file_info directory = {0};
    waste_status status = waste_session_stat(session, path, &directory, error);
    if (status != WASTE_OK) return status;
    if (!name || !capacity || !info || directory.kind != POSIX_NODE_DIRECTORY)
        return report(error, WASTE_ERROR_MALFORMED, "invalid directory output");
    posix_kernel *kernel = root_kernel(session);
    size_t prefix = strlen(path);
    for (int i = 0; i < kernel->path_node_count; i++) {
        const char *candidate = kernel->path_nodes[i].path;
        if (strncmp(candidate, path, prefix)) continue;
        const char *child = candidate + prefix;
        if (prefix > 1) { if (*child != '/') continue; child++; }
        if (!child[0] || strchr(child, '/')) continue;
        if (index--) continue;
        if (strlen(child) >= capacity)
            return report(error, WASTE_ERROR_MALFORMED, "directory name output is too small");
        strcpy(name, child);
        return waste_session_stat(session, candidate, info, error);
    }
    return report(error, WASTE_ERROR_NOT_FOUND, "end of directory");
}
