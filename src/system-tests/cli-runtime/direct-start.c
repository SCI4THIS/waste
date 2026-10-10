/* Private ownership/host-boundary probe, compiled natively and for Wasm.
 * Guest behavior uses the installed Bash/echo/libc and the adjacent fixtures. */
#include "process_driver.h"
#include "guest_posix.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef __wasm__
#define EXPORT(name) __attribute__((export_name(name)))
#else
#define EXPORT(name)
#endif
static posix_kernel *files;
static char failure[512], output[8192];
static size_t output_size;
static unsigned checks;

static int check(int valid, const char *message) {
    checks++;
    if (!valid && !failure[0]) snprintf(failure, sizeof(failure), "%s", message);
    return valid;
}
static int32_t write_output(void *context, int32_t fd, const void *bytes, uint32_t count) {
    (void)context;
    if (fd != 1 && fd != 2) return -POSIX_EBADF;
    if (count >= sizeof(output) - output_size) return -POSIX_ENOSPC;
    memcpy(output + output_size, bytes, count);
    output_size += count;
    output[output_size] = 0;
    return (int32_t)count;
}
static const guest_posix_platform platform = {.write = write_output};

EXPORT("probe_init") int probe_init(void) {
    files = posix_kernel_create(1);
    if (!files) return 0;
    const char *directories[] = {"/usr", "/usr/bin", "/usr/lib", "/root", "/tmp"};
    posix_path_metadata directory = {POSIX_NODE_DIRECTORY, 0755, 0, 0, 0, 1, 0, 0};
    for (unsigned i = 0; i < sizeof(directories) / sizeof(directories[0]); i++)
        (void)posix_kernel_path_add(files, directories[i], &directory);
    const uint8_t truncated[] = {0, 97, 115, 109, 1, 0, 0, 0, 1, 255};
    posix_path_metadata invalid = {POSIX_NODE_REGULAR, 0755, 0, 0, sizeof(truncated), 2, 0, 0};
    return !posix_kernel_path_set_cwd(files, "/root") &&
        !posix_kernel_path_add_data(files, "/tmp/truncated.wasm", &invalid, truncated, sizeof(truncated));
}
EXPORT("probe_stage") int probe_stage(const char *path, const uint8_t *bytes, uint32_t size) {
    posix_path_metadata metadata = {POSIX_NODE_REGULAR, 0755, 0, 0, size, 2, 0, 0};
    return !posix_kernel_path_add_data(files, path, &metadata, bytes, size);
}
static void setup(native_store *store, native_process_driver *driver) {
    native_store_init(store);
    posix_kernel_destroy(store->kernel);
    store->kernel = store->processes[0].kernel = posix_kernel_clone(files);
    store->kernel_terminal = 1;
    store->guest_platform = &platform;
    store->host_resolver = guest_posix_host_resolver;
    store->host_context = store;
    native_process_driver_init(driver);
    output_size = 0;
    output[0] = 0;
}
static void cleanup(native_store *store, native_process_driver *driver) {
    native_process_driver_destroy(driver);
    native_store_free(store);
}
static exec_status start(native_store *store, native_process_driver *driver,
    const char *path, const char *const *args, uint32_t count, exec_error *error) {
    const char *environment[] = {"HOME=/root", "PATH=/usr/bin:/bin", "TERM=xterm"};
    return native_process_driver_start(driver, store, path, args, count,
        environment, sizeof(environment) / sizeof(environment[0]), error);
}
static exec_status run(native_store *store, native_process_driver *driver, exec_error *error) {
    int count = 0;
    return native_process_driver_invoke(driver, store, driver->selection.engine,
        driver->selection.func_idx, NULL, 0, NULL, &count, error);
}
static void failures(void) {
    const char *paths[] = {"/tmp/malformed.wat", "/tmp/missing-entry.wat",
        "/tmp/missing-library.wat", "/tmp/incompatible-import.wat",
        "/tmp/trapping-start.wat", "/tmp/trapping-runtime.wat", "/tmp/failed-runtime.wat",
        "/tmp/incompatible-entry.wat", "/tmp/incompatible-runtime.wat", "/tmp/truncated.wasm",
        "/tmp/yielding-start.wat", "/tmp/not-present"};
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]) && !failure[0]; i++) {
        native_store *store = calloc(1, sizeof(*store));
        native_process_driver *driver = calloc(1, sizeof(*driver));
        exec_error error = {0};
        if (!check(store && driver, "allocate probe context")) { free(store); free(driver); return; }
        setup(store, driver);
        posix_kernel *original = store->kernel;
        int fd = posix_kernel_dup(store->kernel, 1);
        check(fd >= 3 && !posix_kernel_set_cloexec(store->kernel, fd, 1), "prepare close-on-exec descriptor");
        const char *args[] = {paths[i]};
        check(start(store, driver, paths[i], args, 1, &error) != EXEC_OK, paths[i]);
        check(error.message[0] != 0, "startup failure has a diagnostic");
        native_process_capsule *capsule = native_store_active_capsule(store);
        check(store->kernel == original && !capsule->engine && !capsule->image &&
            !store->module_count && !store->orphan_count && !store->call_blocks &&
            !driver->active_engine && !driver->image_active &&
            capsule->pending_transition == NATIVE_PROCESS_TRANSITION_NONE,
            "failed startup restores the empty store");
        check(posix_kernel_get_cloexec(store->kernel, fd) == 1,
              "failed startup preserves close-on-exec descriptor");
        args[0] = "/tmp/resources.wat";
        memset(&error, 0, sizeof(error));
        if (check(start(store, driver, args[0], args, 1, &error) == EXEC_OK,
                  error.message[0] ? error.message : "retry startup")) {
            check(posix_kernel_get_cloexec(store->kernel, fd) < 0, "successful startup closes descriptor");
            native_linked_module *provider = native_registered_module(store, "waste-runtime");
            check(provider && provider->engine->memory == driver->active_engine->memory &&
                provider->engine->tables[0] == driver->active_engine->tables[0],
                "initial executable aliases C-owned resources");
            check(run(store, driver, &error) == EXEC_ERROR_EXIT && !error.exit_code,
                  "retry image runs initialized exactly once");
        }
        cleanup(store, driver);
        free(store);
        free(driver);
    }
}
static void application(const char *const *args, uint32_t count, int exit_code,
                        const char *expected, unsigned forks, unsigned execs) {
    native_store *store = calloc(1, sizeof(*store));
    native_process_driver *driver = calloc(1, sizeof(*driver));
    exec_error error = {0};
    if (!check(store && driver, "allocate probe context")) { free(store); free(driver); return; }
    setup(store, driver);
    if (check(start(store, driver, args[0], args, count, &error) == EXEC_OK,
              error.message[0] ? error.message : "application startup")) {
        check(native_store_find_library(store, "libc") != NULL, "installed production libc loaded");
        check(native_process_driver_start(driver, store, args[0], args, count, NULL, 0,
              &error) != EXEC_OK, "cannot start an already started process");
        memset(&error, 0, sizeof(error));
        exec_status status = run(store, driver, &error);
        check(status == EXEC_ERROR_EXIT && error.exit_code == exit_code,
              error.message[0] ? error.message : "application exit status");
        check(strcmp(output, expected) == 0, output[0] ? output : "application produced no output");
        check(driver->forks == forks && driver->execs == execs, "real fork/exec counts without bootstrap");
    }
    cleanup(store, driver);
    free(store);
    free(driver);
}
static void input_options(void) {
    native_store *store = calloc(1, sizeof(*store));
    native_process_driver *driver = calloc(1, sizeof(*driver));
    if (!check(store && driver, "allocate option probe")) { free(store); free(driver); return; }
    setup(store, driver);
    const char *path = "/tmp/resources.wat";
    const char *args[] = {path};
    exec_error error = {0};
    check(!posix_kernel_path_chmod(store->kernel, (const uint8_t *)path, strlen(path), 0644),
          "prepare readable non-executable input");
    native_process_start_options options = {.format = NATIVE_EXEC_FORMAT_WASM, .readable_input = 1};
    check(native_process_driver_start_with_options(driver, store, path, args, 1, NULL, 0,
        &options, &error) != EXEC_OK, "binary frontend rejects WAT");
    options.format = NATIVE_EXEC_FORMAT_WAT;
    options.entry = "missing";
    check(native_process_driver_start_with_options(driver, store, path, args, 1, NULL, 0,
        &options, &error) != EXEC_OK, "missing requested export recovers");
    options.entry = "probe";
    options.readable_input = 0;
    check(native_process_driver_start_with_options(driver, store, path, args, 1, NULL, 0,
        &options, &error) != EXEC_OK, "ordinary exec still requires execute permission");
    options.readable_input = 1;
    exec_status status = native_process_driver_start_with_options(driver, store, path, args, 1, NULL, 0,
        &options, &error);
    if (check(status == EXEC_OK, error.message[0] ? error.message : "explicit interpreter reads without execute permission"))
        check(run(store, driver, &error) == EXEC_ERROR_EXIT && !error.exit_code,
              "explicit entry and startup initialize once");
    cleanup(store, driver);
    setup(store, driver);
    args[0] = "/tmp/memoryless.wat";
    memset(&error, 0, sizeof(error));
    if (check(start(store, driver, args[0], args, 1, &error) == EXEC_OK, "memoryless process startup"))
        check(run(store, driver, &error) == EXEC_ERROR_EXIT && !error.exit_code,
              "memoryless entry completes");
    cleanup(store, driver);
    free(store);
    free(driver);
}
static void standalone_context(void) {
    native_store *store = calloc(1, sizeof(*store));
    native_process_driver *driver = calloc(1, sizeof(*driver));
    exec_error error = {0};
    if (!check(store && driver, "allocate standalone context probe")) { free(store); free(driver); return; }
    setup(store, driver);
    check(!posix_kernel_close(store->kernel, 0) &&
          !posix_kernel_close(store->kernel, 1) &&
          !posix_kernel_close(store->kernel, 2), "standalone context begins without descriptors");
    store->kernel_terminal = 0;
    posix_kernel *original = store->kernel;
    const char *path = "/usr/lib/libc.so.wasm";
    check(!posix_kernel_path_unlink(store->kernel, (const uint8_t *)path, strlen(path), 0), "remove context library");
    check(native_process_driver_prepare_runtime(driver, store, 4, 1, &error) != EXEC_OK,
          "missing context library fails");
    native_process_capsule *capsule = native_store_active_capsule(store);
    check(store->kernel == original && !capsule->engine && !capsule->image &&
          !capsule->loaded_library_count && !store->module_count &&
          !store->kernel_terminal && !store->kernel->fds[0].ofd &&
          !store->kernel->fds[1].ofd && !store->kernel->fds[2].ofd,
          "failed context restores empty store and descriptors");
    uint8_t *bytes = NULL;
    size_t length = 0;
    posix_path_metadata metadata;
    check(!posix_kernel_path_read_snapshot(files, (const uint8_t *)path, strlen(path),
        NATIVE_EXEC_BYTES_MAX, &bytes, &length, &metadata) &&
        !posix_kernel_path_add_data(store->kernel, path, &metadata, bytes, length), "restore installed context library");
    free(bytes);
    memset(&error, 0, sizeof(error));
    if (check(native_process_driver_prepare_runtime(driver, store, 4, 1, &error) == EXEC_OK,
              error.message[0] ? error.message : "retry production context")) {
        native_linked_module *runtime = native_registered_module(store, "waste-runtime");
        native_loaded_library *libc = native_store_find_library(store, "libc");
        check(runtime && libc && runtime->engine->memory == libc->engine->memory &&
              runtime->engine->tables[0] == libc->engine->tables[0], "context DSO shares C-owned resources");
        posix_ofd *terminal = store->kernel->fds[0].ofd;
        check(store->kernel_terminal && terminal && terminal == store->kernel->fds[1].ofd &&
              terminal == store->kernel->fds[2].ofd &&
              posix_kernel_attach_terminal(store->kernel) == -POSIX_EBUSY &&
              store->kernel->fds[0].ofd == terminal,
              "context attaches shared terminal and rejects replacement");
        check(!driver->forks && !driver->execs && !driver->image_active, "context starts no application");
        check(native_process_driver_prepare_runtime(driver, store, 4, 1, &error) != EXEC_OK,
              "context cannot replace existing providers");
    }
    cleanup(store, driver);
    free(store);
    free(driver);
}
static exec_status signal_invoke(native_store *store, native_process_driver *driver,
    const char *name, int first, int second, int argc, int *value, exec_error *error) {
    uint32_t index;
    wasm_value arguments[2] = {{.type = WASM_VALTYPE_I32, .i32 = first},
                              {.type = WASM_VALTYPE_I32, .i32 = second}};
    wasm_value result;
    int count = 0;
    memset(error, 0, sizeof(*error));
    waste_exec_engine *engine = driver->active_engine;
    (void)store;
    exec_status status = exec_find_export(engine, name, &index, error);
    if (status == EXEC_OK)
        status = exec_invoke(engine, index, arguments, argc, &result, &count, error);
    if (status == EXEC_OK && count == 1 && value) *value = result.i32;
    return status;
}
static void read_signals(void) {
    native_store *store = calloc(1, sizeof(*store));
    native_process_driver *driver = calloc(1, sizeof(*driver));
    if (!check(store && driver, "allocate read signal probe")) { free(store); free(driver); return; }
    const char *path = "/tmp/read-signals.wat", *args[] = {path};
    for (int mode = 0; mode < 14 && !failure[0]; mode++) {
        setup(store, driver);
        exec_error error = {0};
        int value = 0;
        if (!check(start(store, driver, path, args, 1, &error) == EXEC_OK,
                error.message[0] ? error.message : "start read signal fixture")) {
            cleanup(store, driver); break;
        }
        int slot = mode < 6 ? 2 : mode == 6 ? 3 : mode == 7 ? 4 : mode == 8 ? 5 :
                   mode == 9 ? 6 : mode == 10 ? 7 : mode == 11 ? INT32_MAX : mode == 12 ? 8 : 1;
        int flags = mode == 1 || mode == 4 ? POSIX_SA_RESTART : 0;
        check(signal_invoke(store, driver, "action", slot, flags, 2, &value, &error) == EXEC_OK && !value,
              "install table-slot signal handler");
        check(signal_invoke(store, driver, "old-flags", 0, 0, 0, &value, &error) == EXEC_OK && value == flags,
              "sigaction reports restart flags");
        if (mode == 5) {
            check(signal_invoke(store, driver, "alias-action", 0, 0, 0, &value, &error) == EXEC_OK && !value &&
                  store->kernel->signal_handlers[28] == 2, "aliased sigaction decodes before output");
            check(signal_invoke(store, driver, "bad-output", 0, 0, 0, &value, &error) == EXEC_OK && value == -POSIX_EFAULT &&
                  store->kernel->signal_handlers[28] == 2, "invalid sigaction output preserves action");
        }
        const char *operation = mode == 4 ? "select" : mode == 9 ? "jump" : "read";
        check(signal_invoke(store, driver, operation, 0, 0, 0, &value, &error) == EXEC_YIELD &&
              posix_kernel_wait_active(store->kernel), "read signal probe suspends");
        posix_sigset mask = {{0}};
        if (mode == 2) { mask.words[0] = UINT32_C(1) << 27; posix_kernel_set_signal_mask(store->kernel, &mask); }
        check(!posix_kernel_signal_raise(store->kernel, 28), "queue resize signal during wait");
        if (mode == 13) {
            check(posix_kernel_wait_poll(store->kernel) == POSIX_WAIT_BLOCKED &&
                  !posix_kernel_signal_pending(store->kernel, 28), "ignored signal does not interrupt wait");
            const uint8_t byte = 'I';
            check(!posix_kernel_terminal_enqueue(store->kernel, 0, &byte, 1) &&
                  signal_invoke(store, driver, "read", 0, 0, 0, &value, &error) == EXEC_OK && value == 1,
                  "ignored signal leaves read continuation usable");
            cleanup(store, driver);
            continue;
        }
        if (mode == 2) {
            check(posix_kernel_wait_poll(store->kernel) == POSIX_WAIT_BLOCKED &&
                  posix_kernel_signal_pending(store->kernel, 28), "masked signal does not wake read");
            mask.words[0] = 0; posix_kernel_set_signal_mask(store->kernel, &mask);
        }
        check(posix_kernel_wait_poll(store->kernel) == POSIX_WAIT_SIGNAL, "unmasked signal wakes read");
        if (mode == 3) {
            const uint8_t byte = 'Q';
            check(!posix_kernel_terminal_enqueue(store->kernel, 0, &byte, 1), "input races pending signal");
        }
        exec_status status = signal_invoke(store, driver, operation, 0, 0, 0, &value, &error);
        if (mode < 6) {
            check(mode == 1 ? status == EXEC_YIELD : status == EXEC_OK && value == (mode == 3 ? 1 : mode == 4 ? -1 : -POSIX_EINTR),
                  "READ preserves data or delivers EINTR/restart");
            if (mode == 3) {
                check(posix_kernel_signal_pending(store->kernel, 28), "successful read retains pending signal");
                status = signal_invoke(store, driver, "read", 0, 0, 0, &value, &error);
                check(status == EXEC_OK && value == -POSIX_EINTR, "next blocking read delivers retained signal");
            }
            if (mode == 1) {
                const uint8_t byte = 'R';
                check(posix_kernel_wait_active(store->kernel) &&
                      !posix_kernel_signal_pending(store->kernel, 28), "restart re-registers wait without stale signal");
                check(!posix_kernel_terminal_enqueue(store->kernel, 0, &byte, 1) &&
                      signal_invoke(store, driver, "read", 0, 0, 0, &value, &error) == EXEC_OK && value == 1,
                      "restarted read completes on later input");
            }
            check(signal_invoke(store, driver, "calls", 0, 0, 0, &value, &error) == EXEC_OK && value == 1,
                  "handler runs exactly once across resume");
            check(signal_invoke(store, driver, "caught", 0, 0, 0, &value, &error) == EXEC_OK && value == 28,
                  "handler receives correct signal");
            check(signal_invoke(store, driver, "masked", 0, 0, 0, &value, &error) == EXEC_OK &&
                  value == (int)((UINT32_C(1) << 27) | 512), "handler applies signal and action masks");
            check(signal_invoke(store, driver, "byte", 0, 0, 0, &value, &error) == EXEC_OK &&
                  value == (mode == 1 ? 'R' : 165), "interrupted read preserves output buffer");
        } else {
            check(mode == 7 ? status == EXEC_ERROR_EXIT && error.exit_code == 23 :
                  mode == 8 ? status == EXEC_ERROR_UNSUPPORTED :
                  mode == 9 ? status == EXEC_OK && value == 7 : status == EXEC_ERROR_TRAP,
                  "handler traps, exits, blocked calls, jumps and signatures propagate");
            check(!posix_kernel_wait_active(store->kernel), "failed handler leaves no dangling read wait");
            check(signal_invoke(store, driver, "action", 1, 0, 2, &value, &error) == EXEC_OK && !value,
                  "subsequent invocation does not resume rejected handler");
        }
        posix_kernel_get_signal_mask(store->kernel, &mask);
        check(!mask.words[0] && !store->execution_control.synchronous_callbacks,
              "handler restores mask and synchronous scope");
        cleanup(store, driver);
    }
    free(store); free(driver);
}
static void bash_resize(void) {
    native_store *store = calloc(1, sizeof(*store));
    native_process_driver *driver = calloc(1, sizeof(*driver));
    if (!check(store && driver, "allocate Bash resize probe")) { free(store); free(driver); return; }
    setup(store, driver);
    const char *args[] = {"/usr/bin/bash", "--norc", "-i"};
    exec_error error = {0};
    if (check(start(store, driver, args[0], args, 3, &error) == EXEC_OK,
              "start interactive Bash resize probe")) {
        check(run(store, driver, &error) == EXEC_YIELD &&
              (error.yield_reason == EXEC_YIELD_READ || error.yield_reason == EXEC_YIELD_SELECT),
              "interactive Bash blocks on input");
        posix_winsize dimensions = {41, 121, 0, 0};
        check(!posix_kernel_terminal_set_winsize(store->kernel, 0, &dimensions) &&
              posix_kernel_wait_poll(store->kernel) == POSIX_WAIT_SIGNAL, "resize wakes Bash input wait");
        check(run(store, driver, &error) == EXEC_YIELD &&
              (error.yield_reason == EXEC_YIELD_READ || error.yield_reason == EXEC_YIELD_SELECT) &&
              !posix_kernel_signal_pending(store->kernel, 28), "Bash handler resumes input wait without stale signal");
        const uint8_t command[] = "printf 'RESIZE_%s_%s\\n' \"$LINES\" \"$COLUMNS\"; exit 0\n";
        check(!posix_kernel_terminal_enqueue(store->kernel, 0, command, sizeof(command) - 1),
              "send command after resize handler");
        check(run(store, driver, &error) == EXEC_ERROR_EXIT && !error.exit_code,
              "resized Bash remains usable");
        const char expected[] = "RESIZE_41_121\n";
        int found = 0;
        for (size_t i = 0; i + sizeof(expected) - 1 <= output_size; i++)
            if (!memcmp(output + i, expected, sizeof(expected) - 1)) found = 1;
        check(found, "Bash updates LINES/COLUMNS after input-wait signal");
    }
    cleanup(store, driver);
    free(store); free(driver);
}
static void stream_descriptors(void) {
    posix_kernel *kernel = posix_kernel_create(0);
    if (!check(kernel != NULL, "allocate inherited-stream kernel")) return;
    check(!posix_kernel_attach_stream(kernel, 5, 0, 0, -1, 0010600), "attach raw input capability");
    check(!posix_kernel_isatty(kernel, 5), "raw input is not a terminal");
    check(posix_kernel_attach_stream(kernel, 5, 0, 0, -1, 0010600) == -POSIX_EBUSY,
          "stream attachment rejects occupied descriptor");
    char bytes[4] = {0};
    check(posix_kernel_read(kernel, 5, bytes, 3) == -POSIX_EAGAIN && kernel->wait.active,
          "empty inherited input registers a wait");
    check(!posix_kernel_terminal_enqueue(kernel, 5, (const uint8_t *)"abc", 3), "enqueue inherited input");
    check(posix_kernel_query_readiness(kernel, 5) & POSIX_POLL_IN, "inherited input readiness");
    check(posix_kernel_dup2(kernel, 5, 6) == 6 && kernel->fds[5].ofd == kernel->fds[6].ofd,
          "input alias shares capability and queue");
    check(!posix_kernel_close(kernel, 5), "close original inherited descriptor");
    check(posix_kernel_read(kernel, 6, bytes, 2) == 2 && !memcmp(bytes, "ab", 2), "alias reads shared input");
    check(!posix_kernel_terminal_signal_eof(kernel, 6), "inherited input EOF");
    check(posix_kernel_read(kernel, 6, bytes, 2) == 1 && bytes[0] == 'c', "queued bytes precede EOF");
    check(posix_kernel_read(kernel, 6, bytes, 2) == 0, "inherited input returns EOF");
    check(posix_kernel_write(kernel, 6, "x", 1) == -POSIX_EBADF, "input capability rejects writes");
    check(!posix_kernel_attach_stream(kernel, 7, 0, -1, 2, 0010600), "attach inherited output");
    check(posix_kernel_read(kernel, 7, bytes, 1) == -POSIX_EBADF, "output capability rejects reads");
    posix_kernel *clone = posix_kernel_clone(kernel);
    check(clone && clone->fds[7].ofd == kernel->fds[7].ofd &&
          clone->fds[7].ofd->output_handle == 2, "fork preserves inherited capability identity");
    posix_kernel_destroy(clone);
    posix_kernel_destroy(kernel);
}
static void process_signal_policy(void) {
    native_store *store = calloc(1, sizeof(*store));
    native_process_driver *driver = calloc(1, sizeof(*driver));
    if (!check(store && driver, "allocate process signal probe")) { free(store); free(driver); return; }
    setup(store, driver);
    const char *args[] = {"/usr/bin/bash", "-c", "trap 'echo STALE_HANDLER' TERM; exec /usr/bin/cat"};
    exec_error error = {0};
    if (check(start(store, driver, args[0], args, 3, &error) == EXEC_OK,
              "start caught-action exec probe")) {
        check(run(store, driver, &error) == EXEC_YIELD && error.yield_reason == EXEC_YIELD_READ,
              "replacement cat waits for input");
        posix_signal_disposition disposition;
        check(!posix_kernel_signal_get_disposition(store->kernel, 15, &disposition) &&
              disposition == POSIX_SIGNAL_DEFAULT, "exec resets caught disposition");
        posix_sigset mask = {{UINT32_C(1) << 14, 0, 0, 0}};
        posix_kernel_set_signal_mask(store->kernel, &mask);
        check(!native_store_signal_process(store, 1, 15) &&
              posix_kernel_signal_pending(store->kernel, 15), "blocked termination stays pending");
        check(run(store, driver, &error) == EXEC_YIELD, "blocked termination preserves running process");
        memset(&mask, 0, sizeof(mask));
        posix_kernel_set_signal_mask(store->kernel, &mask);
        check(run(store, driver, &error) == EXEC_ERROR_EXIT && error.exit_code == 143 && error.signal == 15,
              "unblocked default action terminates replacement image");
        check(store->processes[0].exit_status == 15 && !store->kernel->fds[0].ofd,
              "signal status and descriptor cleanup belong to process");
    }
    cleanup(store, driver);
    free(store); free(driver);
}
EXPORT("probe_run") unsigned probe_run(void) {
    failures();
    if (!failure[0]) input_options();
    if (!failure[0]) standalone_context();
    if (!failure[0]) stream_descriptors();
    if (!failure[0]) read_signals();
    if (!failure[0]) bash_resize();
    if (!failure[0]) process_signal_policy();
    if (!failure[0]) {
        const char *args[] = {"/usr/bin/bash", "-c", "kill -WINCH $$; kill -CHLD $$; echo NOTIFIED"};
        application(args, 3, 0, "NOTIFIED\n", 0, 0);
    }
    if (!failure[0]) {
        const char *args[] = {"/usr/bin/bash", "-c", "trap '' INT; exec /usr/bin/bash -c 'kill -INT $$; echo IGNORED'"};
        application(args, 3, 0, "IGNORED\n", 0, 1);
    }
    if (!failure[0]) {
        const char *echo[] = {"/usr/bin/echo", "two words", "argument"};
        application(echo, 3, 0, "two words argument\n", 0, 0);
    }
    if (!failure[0]) {
        const char *bash[] = {"/usr/bin/bash", "--norc", "-c",
            "printf '%s|%s|%s\\n' \"$0\" \"$1\" \"$HOME\"; exit 7", "with space", "two words"};
        application(bash, 6, 7, "with space|two words|/root\n", 0, 0);
    }
    if (!failure[0]) {
        const char *bash[] = {"/usr/bin/bash", "--norc", "-c",
            "/usr/bin/echo child; exec /usr/bin/echo replaced"};
        application(bash, 4, 0, "child\nreplaced\n", 1, 2);
    }
    return failure[0] ? 0 : checks;
}
EXPORT("probe_error") const char *probe_error(void) { return failure; }
EXPORT("probe_destroy") void probe_destroy(void) { posix_kernel_destroy(files); files = NULL; }

#ifndef __wasm__
static int stage_file(const char *host, const char *guest) {
    FILE *file = fopen(host, "rb");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return 0; }
    long length = ftell(file);
    uint8_t *bytes = length >= 0 && length <= NATIVE_EXEC_BYTES_MAX ? malloc((size_t)length + 1) : NULL;
    int ok = bytes && !fseek(file, 0, SEEK_SET) &&
        fread(bytes, 1, (size_t)length, file) == (size_t)length &&
        probe_stage(guest, bytes, (uint32_t)length);
    free(bytes);
    fclose(file);
    return ok;
}
int main(void) {
    if (!probe_init()) return 1;
    const char *fixtures[] = {"resources", "malformed", "missing-entry", "missing-library",
        "incompatible-import", "trapping-start", "trapping-runtime", "failed-runtime",
        "incompatible-entry", "incompatible-runtime", "yielding-start", "memoryless", "read-signals"};
    for (unsigned i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i++) {
        char host[256], guest[256];
        snprintf(host, sizeof(host), "src/system-tests/cli-runtime/%s.wat", fixtures[i]);
        snprintf(guest, sizeof(guest), "/tmp/%s.wat", fixtures[i]);
        if (!stage_file(host, guest)) return 1;
    }
    if (!stage_file("src/vfs/usr/bin/bash", "/usr/bin/bash") ||
        !stage_file("src/vfs/usr/bin/cat", "/usr/bin/cat") ||
        !stage_file("src/vfs/usr/bin/echo", "/usr/bin/echo") ||
        !stage_file("src/vfs/usr/lib/libc.so.wasm", "/usr/lib/libc.so.wasm")) return 1;
    unsigned passed = probe_run();
    printf("direct startup: %u checks, %s\n", checks, passed ? "PASS" : failure);
    probe_destroy();
    return passed ? 0 : 1;
}
#endif
