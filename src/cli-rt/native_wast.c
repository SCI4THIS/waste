/* Native WAST execution and import adapters shared by CLI and batch entry points. */
#include "native_wast.h"
#include "wast/runner.h"
#include "wast/stream.h"
#include "wast/setup.h"
#include "wasm/decode.h"
#include "runtime_internal.h"
#include "guest_posix.h"
#include "process_driver.h"
#include "lib/include/kernel.h"
#include "lib/include/select.h"
#include "lib/include/syscall.h"
#include <stdlib.h>
#include <string.h>

static exec_status cli_result(int32_t value, wasm_value *results,
                              int *result_count) {
    results[0].type = WASM_VALTYPE_I32;
    results[0].i32 = value;
    *result_count = 1;
    return EXEC_OK;
}

static exec_status cli_select_host(void *data, const wasm_value *args,
                                   int arg_count, wasm_value *results,
                                   int *result_count, exec_error *error,
                                   const waste_exec_engine *caller) {
    native_store *store = data;
    if (arg_count != 5 || !caller->memory) return EXEC_ERROR_NOT_FOUND;
    exec_memory *memory = caller->memory;
    posix_fd_set rds, wrs, exs;
    posix_fd_set *rp = NULL, *wp = NULL, *ep = NULL;
    uint8_t bytes[POSIX_FD_SET_BYTES];
    uint32_t pointers[3] = {(uint32_t)args[1].i32,
                            (uint32_t)args[2].i32,
                            (uint32_t)args[3].i32};
    posix_fd_set *sets[3] = {&rds, &wrs, &exs};
    for (int i = 0; i < 3; i++) {
        if (!pointers[i]) continue;
        if (exec_memory_read(memory, pointers[i], bytes, sizeof(bytes), error) != EXEC_OK)
            return cli_result(-POSIX_EINVAL, results, result_count);
        posix_fd_set_decode(sets[i], bytes);
        if (i == 0) rp = sets[i];
        if (i == 1) wp = sets[i];
        if (i == 2) ep = sets[i];
    }
    posix_timeval tv;
    const posix_timeval *tvp = NULL;
    uint32_t timeout = (uint32_t)args[4].i32;
    if (timeout) {
        if (exec_memory_read(memory, timeout, bytes, POSIX_TIMEVAL_BYTES, error) != EXEC_OK)
            return cli_result(-POSIX_EINVAL, results, result_count);
        posix_timeval_decode(&tv, bytes);
        tvp = &tv;
    }
    int32_t value = posix_kernel_select(store->kernel, args[0].i32,
                                        rp, wp, ep, tvp);
    if (value == -POSIX_EAGAIN) return EXEC_YIELD;
    if (value >= 0) {
        if (rp) { posix_fd_set_encode(bytes, rp); exec_memory_write(memory, pointers[0], bytes, POSIX_FD_SET_BYTES, error); }
        if (wp) { posix_fd_set_encode(bytes, wp); exec_memory_write(memory, pointers[1], bytes, POSIX_FD_SET_BYTES, error); }
        if (ep) { posix_fd_set_encode(bytes, ep); exec_memory_write(memory, pointers[2], bytes, POSIX_FD_SET_BYTES, error); }
    }
    (void)error;
    return cli_result(value, results, result_count);
}

static exec_status cli_pselect_host(void *data, const wasm_value *args,
                                    int arg_count, wasm_value *results,
                                    int *result_count, exec_error *error,
                                    const waste_exec_engine *caller) {
    native_store *store = data;
    if (arg_count != 6 || !caller->memory) return EXEC_ERROR_NOT_FOUND;
    exec_memory *memory = caller->memory;
    posix_fd_set rds, wrs, exs;
    posix_fd_set *rp = NULL, *wp = NULL, *ep = NULL;
    uint8_t bytes[POSIX_FD_SET_BYTES];
    uint32_t pointers[3] = {(uint32_t)args[1].i32,
                            (uint32_t)args[2].i32,
                            (uint32_t)args[3].i32};
    posix_fd_set *sets[3] = {&rds, &wrs, &exs};
    for (int i = 0; i < 3; i++) {
        if (!pointers[i]) continue;
        if (exec_memory_read(memory, pointers[i], bytes, sizeof(bytes), error) != EXEC_OK)
            return cli_result(-POSIX_EINVAL, results, result_count);
        posix_fd_set_decode(sets[i], bytes);
        if (i == 0) rp = sets[i];
        if (i == 1) wp = sets[i];
        if (i == 2) ep = sets[i];
    }
    posix_timespec ts;
    const posix_timespec *tsp = NULL;
    uint32_t timeout = (uint32_t)args[4].i32;
    if (timeout) {
        if (exec_memory_read(memory, timeout, bytes, POSIX_TIMESPEC_BYTES, error) != EXEC_OK)
            return cli_result(-POSIX_EINVAL, results, result_count);
        posix_timespec_decode(&ts, bytes);
        tsp = &ts;
    }
    posix_sigset mask;
    const posix_sigset *maskp = NULL;
    uint32_t mask_ptr = (uint32_t)args[5].i32;
    if (mask_ptr) {
        if (exec_memory_read(memory, mask_ptr, bytes, POSIX_SIGSET_BYTES, error) != EXEC_OK)
            return cli_result(-POSIX_EINVAL, results, result_count);
        posix_sigset_decode(&mask, bytes);
        maskp = &mask;
    }
    int32_t value = posix_kernel_pselect(store->kernel, args[0].i32,
                                         rp, wp, ep, tsp, maskp);
    if (value == -POSIX_EAGAIN) return EXEC_YIELD;
    if (value >= 0) {
        if (rp) { posix_fd_set_encode(bytes, rp); exec_memory_write(memory, pointers[0], bytes, POSIX_FD_SET_BYTES, error); }
        if (wp) { posix_fd_set_encode(bytes, wp); exec_memory_write(memory, pointers[1], bytes, POSIX_FD_SET_BYTES, error); }
        if (ep) { posix_fd_set_encode(bytes, ep); exec_memory_write(memory, pointers[2], bytes, POSIX_FD_SET_BYTES, error); }
    }
    (void)error;
    return cli_result(value, results, result_count);
}

static exec_status cli_path_access_host(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error,
                                        const waste_exec_engine *caller) {
    native_store *store = data;
    if (arg_count != 4 || !caller->memory) return cli_result(-POSIX_EFAULT, results, result_count);
    uint32_t offset = (uint32_t)args[0].i32;
    uint32_t length = (uint32_t)args[1].i32;
    uint8_t *path = length ? malloc(length) : NULL;
    if ((length && !path) || exec_memory_read(caller->memory, offset, path,
                                               length, error) != EXEC_OK) {
        free(path);
        return cli_result(-POSIX_EFAULT, results, result_count);
    }
    int result = posix_kernel_path_access(store->kernel, path, length,
                                          args[2].i32, args[3].i32);
    free(path);
    (void)error;
    return cli_result(result, results, result_count);
}

static exec_status cli_path_stat_host(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = data;
    uint8_t output[POSIX_PATH_METADATA_BYTES];
    if (arg_count != 4 || !caller->memory) return cli_result(-POSIX_EFAULT, results, result_count);
    uint32_t offset = (uint32_t)args[0].i32;
    uint32_t length = (uint32_t)args[1].i32;
    uint32_t output_offset = (uint32_t)args[3].i32;
    uint8_t *path = length ? malloc(length) : NULL;
    if ((length && !path) || exec_memory_read(caller->memory, offset, path,
                                               length, error) != EXEC_OK ||
        exec_memory_read(caller->memory, output_offset, output,
                         sizeof(output), error) != EXEC_OK) {
        free(path);
        return cli_result(-POSIX_EFAULT, results, result_count);
    }
    posix_path_metadata metadata;
    int result = posix_kernel_path_stat(store->kernel,
        path, length, args[2].i32, &metadata);
    if (result == 0) {
        posix_path_metadata_encode(output, &metadata);
        if (exec_memory_write(caller->memory, output_offset, output,
                              sizeof(output), error) != EXEC_OK)
            result = -POSIX_EFAULT;
    }
    free(path);
    (void)error;
    return cli_result(result, results, result_count);
}

/* Native guest-POSIX platform: deliberately refuses host filesystem I/O so
 * the native runner stays sandbox-safe.  Imports still resolve through
 * guest_posix_host_resolver (clearing link-time "unresolved function import"
 * errors on libc-test fixtures); the engine's kernel/VFS answers whatever it
 * can, and the few calls that fall through to the platform surface -ENOSYS
 * rather than touching real files. */
static int32_t native_platform_open(void *data, const char *path, size_t length,
                                    int32_t flags, int32_t mode) {
    (void)data; (void)path; (void)length; (void)flags; (void)mode;
    return -POSIX_ENOSYS;
}
static int32_t native_platform_close(void *data, int32_t fd) {
    (void)data; (void)fd; return -POSIX_ENOSYS;
}
static int32_t native_platform_read(void *data, int32_t fd, void *bytes,
                                    uint32_t length) {
    (void)data; (void)fd; (void)bytes; (void)length; return -POSIX_ENOSYS;
}
static int32_t native_platform_write(void *data, int32_t fd, const void *bytes,
                                     uint32_t length) {
    (void)data; (void)fd; (void)bytes; (void)length; return -POSIX_ENOSYS;
}
static void native_platform_trace(void *data, const char *event) {
    (void)data; (void)event;
}
static const guest_posix_platform native_platform = {
    native_platform_open, native_platform_close,
    native_platform_read, native_platform_write,
    native_platform_trace,
};

static int cli_host_resolver(const char *module, const char *name,
                             void *context, native_host_binding *out) {
    if (strcmp(module, "waste_kernel") == 0) {
        exec_host_func fn = NULL;
        if (strcmp(name, "select_v1") == 0) fn = cli_select_host;
        else if (strcmp(name, "pselect_v1") == 0) fn = cli_pselect_host;
        else if (strcmp(name, POSIX_KERNEL_PATH_ACCESS_V1) == 0) fn = cli_path_access_host;
        else if (strcmp(name, POSIX_KERNEL_PATH_STAT_V1) == 0) fn = cli_path_stat_host;
        if (fn) {
            out->function = fn;
            out->host_data = context;
            out->control = EXEC_HOST_CONTROL_NONE;
            return 1;
        }
        /* Fall through: the shared resolver handles the remainder of the
         * waste_kernel ABI (startup_v1, open_v1, realtime_v1, …). */
    }
    /* Delegate env.* and the rest of the waste_kernel ABI to the engine's
     * POSIX stub table, mirroring browser_host_resolver.  The native
     * platform refuses host I/O; the engine kernel still owns descriptor,
     * path, and process semantics. */
    native_store *store = context;
    store->guest_platform = &native_platform;
    store->guest_platform_data = NULL;
    return guest_posix_host_resolver(module, name, context, out);
}

/* Emit a JSON string with escaping */
void native_wast_json_string(FILE *output, const char *s) {
    fputc('"', output);
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"') fputs("\\\"", output);
        else if (ch == '\\') fputs("\\\\", output);
        else if (ch == '\n') fputs("\\n", output);
        else if (ch == '\r') fputs("\\r", output);
        else if (ch == '\t') fputs("\\t", output);
        else if (ch < 0x20) fprintf(output, "\\u%04x", (unsigned)ch);
        else fputc((int)ch, output);
    }
    fputc('"', output);
}

static int native_monotonic_ns(uint64_t *now) {
    struct __kernel_timespec value;
    if (sys_clock_gettime(1 /* CLOCK_MONOTONIC */, &value)) return -1;
    *now = (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
    return 0;
}

static uint64_t native_kernel_clock(void *opaque) {
    (void)opaque;
    uint64_t now = 0;
    (void)native_monotonic_ns(&now);
    return now;
}

void native_wast_bind(native_store *store) {
    store->host_resolver = cli_host_resolver;
    store->host_context = store;
    posix_kernel_set_clock(store->kernel, native_kernel_clock, NULL);
}
/* Parsed module metadata is borrowed by store bindings until execution ends.
 * Assertions/register commands do not need retention; the shared scanner owns
 * their temporary parses. Anonymous replacement releases its parse immediately. */
#define NATIVE_RETAINED_MAX 4096u
typedef struct {
    native_store *store;
    native_process_driver driver;
    wast_script **retained;
    unsigned retained_count, retained_capacity, line;
    wast_script *current_anonymous;
    native_wast_counts *counts;
    wast_setup_report setup;
    char parse_error[256];
    int stopped, completed;
    const native_wast_options *options;
    char first_failure[256];
} native_wast_context;

static void native_result(native_wast_context *context, int passed,
                           const char *name, const char *message) {
    unsigned index = context->counts->total++;
    if (passed) context->counts->passed++;
    else if (!context->first_failure[0])
        snprintf(context->first_failure, sizeof(context->first_failure), "%.63s: %.189s",
                 name, message && message[0] ? message : "failed");
    FILE *out = context->options->output;
    if (!context->options->json) {
        if (context->options->verbose) {
            fputc(passed ? '.' : 'F', out);
            fflush(out);
        }
        return;
    }
    if (index) fprintf(out, ",\n");
    fprintf(out, "{\"index\":%u,\"line\":%u,\"func\":", index, context->line);
    native_wast_json_string(out, name);
    fprintf(out, ",\"pass\":%s,\"error\":", passed ? "true" : "false");
    if (passed || !message || !message[0]) fprintf(out, "null");
    else native_wast_json_string(out, message);
    fprintf(out, "}");
}

static void native_parse_failure(native_wast_context *context, const char *message) {
    if (!context->parse_error[0])
        snprintf(context->parse_error, sizeof(context->parse_error), "%s", message);
    native_result(context, 0, "(parse)", message);
}

static exec_status native_assertion_invoke(void *opaque,
    waste_exec_engine *engine, uint32_t function, const wasm_value *args,
    int count, wasm_value *results, int *result_count, exec_error *error) {
    native_wast_context *context = opaque;
    if (context->options->invoke)
        return context->options->invoke(context->options->invoke_context, engine, function,
            args, count, results, result_count, error);
    for (;;) {
        exec_status status = native_process_driver_invoke(&context->driver, context->store, engine,
            function, args, count, results, result_count, error);
        if (status != EXEC_YIELD || error->yield_reason != EXEC_YIELD_SELECT ||
            !context->store->kernel) return status;
        posix_wait_record *wait = &context->store->kernel->wait;
        if (!wait->active || !wait->has_deadline) return status;
        /* Only finite kernel SELECT waits can progress without terminal input.
         * Keep the assertion's arguments/results live across ordinary returns;
         * the engine owns evaluator and process continuation state. */
        for (;;) {
            status = exec_execution_check(&context->store->execution_control, error);
            if (status != EXEC_OK) return status;
            uint64_t now;
            if (native_monotonic_ns(&now))
                return exec_fail(error, EXEC_ERROR_UNSUPPORTED, "native monotonic clock failed");
            if (now >= wait->deadline_ns) break;
            uint64_t remaining = wait->deadline_ns - now;
            if (remaining > UINT64_C(5000000)) remaining = UINT64_C(5000000);
            struct __kernel_timespec delay = {0, (int64_t)remaining};
            long slept = sys_nanosleep(&delay);
            if (slept && slept != -4 /* EINTR */)
                return exec_fail(error, EXEC_ERROR_UNSUPPORTED, "native timed wait failed");
        }
        memset(error, 0, sizeof(*error));
    }
}

static void native_assertions(native_wast_context *context,
                               const wast_script *script, const wast_group *group) {
    for (int i = 0; i < group->assertion_count && !context->stopped; i++) {
        const wast_assertion *assertion = &script->assertions[group->assertion_start + i];
        waste_exec_engine *engine = native_selected_engine(context->store, assertion->module_id);
        exec_error error = {0};
        exec_status status = engine ? wast_run_assertion_with_invoke(engine,
            assertion, &error, native_assertion_invoke, context) :
            exec_fail(&error, EXEC_ERROR_NOT_FOUND, "unknown module id");
        if (status == EXEC_YIELD && !error.message[0])
            snprintf(error.message, sizeof(error.message),
                     "native assertion needs session input/resume (wait %d)", error.yield_reason);
        native_result(context, status == EXEC_OK, assertion->func_name, error.message);
        if (status == EXEC_ERROR_INTERRUPTED) context->stopped = 1;
    }
}

static wast_script *native_retain(native_wast_context *context, wast_script *parsed) {
    if (context->retained_count == NATIVE_RETAINED_MAX) return NULL;
    if (parsed->group_count && parsed->group_capacity != parsed->group_count) {
        wast_group *groups = realloc(parsed->groups,
            (size_t)parsed->group_count * sizeof(*groups));
        if (!groups) return NULL;
        parsed->groups = groups;
        parsed->group_capacity = parsed->group_count;
    }
    if (context->retained_count == context->retained_capacity) {
        unsigned capacity = context->retained_capacity ? context->retained_capacity * 2u : 16u;
        wast_script **retained = realloc(context->retained, (size_t)capacity * sizeof(*retained));
        if (!retained) return NULL;
        context->retained = retained;
        context->retained_capacity = capacity;
    }
    wast_script *holder = malloc(sizeof(*holder));
    if (!holder) return NULL;
    *holder = *parsed;
    memset(parsed, 0, sizeof(*parsed));
    context->retained[context->retained_count++] = holder;
    return holder;
}

static void native_forget(native_wast_context *context, wast_script *script) {
    for (unsigned i = 0; i < context->retained_count; i++) {
        if (context->retained[i] != script) continue;
        memmove(context->retained + i, context->retained + i + 1,
                (size_t)(context->retained_count - i - 1) * sizeof(*context->retained));
        context->retained_count--;
        wast_script_free(script);
        free(script);
        return;
    }
}

static void native_release_anonymous(native_wast_context *context) {
    wast_script *script = context->current_anonymous;
    if (!script || !context->store->module_count) return;
    native_linked_module *current = &context->store->modules[context->store->module_count - 1];
    if (current->id[0] || current->registered[0]) {
        context->current_anonymous = NULL;
        return;
    }
    int preserve = current->engine == context->driver.parent_engine;
    native_process_capsule *capsule = native_store_active_capsule(context->store);
    preserve |= capsule && capsule->engine == current->engine;
    for (int i = 0; current->module && i < current->module->table_count; i++)
        preserve |= current->module->tables[i].is_import != 0;
    if (preserve) {
        if (!native_store_keep_orphan(context->store, current->engine)) {
            native_result(context, 0, "(module)", "cannot preserve anonymous module aliases");
            context->stopped = 1;
            return;
        }
    } else exec_free(current->engine);
    memset(current, 0, sizeof(*current));
    context->store->module_count--;
    context->current_anonymous = NULL;
    native_forget(context, script);
}

static const wast_module *native_definition(native_wast_context *context, const char *id) {
    for (unsigned i = context->retained_count; i > 0; i--) {
        const wast_script *script = context->retained[i - 1];
        for (int j = script->group_count; j > 0; j--) {
            const wast_module *module = &script->groups[j - 1].module;
            if (module->is_definition && !strcmp(module->id, id)) return module;
        }
    }
    return NULL;
}

static void native_module(native_wast_context *context, wast_group *group) {
    const wast_module *module = &group->module;
    if (module->is_definition) return;
    if (module->instance_of[0]) {
        module = native_definition(context, module->instance_of);
        if (!module) {
            wast_setup_record(&context->setup, context->line,
                EXEC_ERROR_NOT_FOUND, WAST_SETUP_DEFINITION, "unknown module definition");
            return;
        }
    }
    if (group->has_module_assertion && group->has_validation_error) {
        int passed = group->module_assert_kind == WAST_ASSERT_INVALID ||
                     group->module_assert_kind == WAST_ASSERT_MALFORMED;
        native_result(context, passed, "(module)", group->validation_error);
        return;
    }
    wast_group encoded = *group;
    encoded.module = *module;
    size_t size = 0;
    char message[256] = {0};
    uint8_t *binary = encode_group_module(&encoded, &size, message);
    if (!binary) {
        if (group->has_module_assertion)
            native_result(context, group->module_assert_kind != WAST_ASSERT_TRAP,
                          "(module)", message);
        else wast_setup_record(&context->setup, context->line,
            EXEC_ERROR_FORMAT, WAST_SETUP_ENCODE, message);
        return;
    }
    waste_exec_engine *engine = NULL;
    exec_error error = {0};
    exec_status status = native_load_module(context->store, module, binary, size, &engine, &error);
    free(binary);
    if (status == EXEC_ERROR_INTERRUPTED || status == EXEC_YIELD) {
        if (!group->has_module_assertion)
            wast_setup_record(&context->setup, context->line, status,
                WAST_SETUP_LOAD, error.message);
        native_result(context, 0, "(module)", status == EXEC_YIELD ?
                      "yielding module starts need a session driver" : error.message);
        context->stopped = 1;
    } else if (group->has_module_assertion) {
        int passed = group->module_assert_kind == WAST_ASSERT_TRAP ?
                     status == EXEC_ERROR_TRAP : status != EXEC_OK;
        native_result(context, passed, "(module)",
                      status == EXEC_OK ? "module unexpectedly instantiated" : error.message);
    } else if (status != EXEC_OK) {
        wast_setup_record(&context->setup, context->line, status,
                          WAST_SETUP_LOAD, error.message);
    } else if (!native_store_add(context->store, engine, &group->module, module)) {
        wast_setup_record(&context->setup, context->line, EXEC_ERROR_TRAP,
                          WAST_SETUP_RETAIN, "cannot retain module instance");
        native_result(context, 0, "(module)", "cannot retain module instance");
        context->stopped = 1;
    } else {
        wast_setup_record(&context->setup, context->line, EXEC_OK,
                          WAST_SETUP_LOAD, NULL);
        return; /* store owns the engine; parsed metadata remains retained */
    }
    /* Failed starts may have installed funcrefs in an imported table. Keep the
     * instance until store teardown instead of leaving dangling provider data. */
    if (engine && !native_store_keep_orphan(context->store, engine)) {
        exec_free(engine);
        native_result(context, 0, "(module)", "cannot retain module assertion engine");
        context->stopped = 1;
    }
}

static int native_command(wast_stream_command_kind kind, const char *bytes,
    size_t length, size_t offset, unsigned line, wast_script *parsed, void *opaque) {
    native_wast_context *context = opaque;
    (void)bytes; (void)length; (void)offset;
    context->line = line;
    if (parsed->error[0]) {
        native_parse_failure(context, parsed->error);
        return 0; /* recover at the scanner's next balanced command */
    }
    if (kind == WAST_STREAM_REGISTER) {
        if (parsed->group_count != 1) {
            native_result(context, 0, "(register)", "invalid register command");
            return 0;
        }
        const wast_module *registration = &parsed->groups[0].module;
        native_linked_module *target = native_selected_module(context->store, registration->register_target);
        if (target) snprintf(target->registered, sizeof(target->registered), "%s",
                             registration->register_name);
        else native_result(context, 0, "(register)", "unknown module id");
        return 0;
    }
    if ((kind == WAST_STREAM_ASSERTION || kind == WAST_STREAM_INVOKE) &&
        parsed->group_count == 1 && !parsed->groups[0].has_module_assertion) {
        native_assertions(context, parsed, &parsed->groups[0]);
        return 0;
    }
    if (!parsed->group_count) return 0;
    if (kind == WAST_STREAM_MODULE && !parsed->groups[0].has_module_assertion &&
        !parsed->groups[0].module.is_definition)
        native_release_anonymous(context);
    if (context->stopped) return 0;
    /* Module assertions never become the current instance. Their temporary
     * parses can be released by the scanner after encoding/instantiation. */
    if (parsed->groups[0].has_module_assertion) {
        native_module(context, &parsed->groups[0]);
        return 0;
    }
    wast_script *script = native_retain(context, parsed);
    if (!script) {
        native_result(context, 0, "(module)", "parsed module retention limit/allocation failed");
        context->stopped = 1;
        return 0;
    }
    int before = context->store->module_count;
    for (int i = 0; i < script->group_count && !context->stopped; i++)
        native_module(context, &script->groups[i]);
    if (context->store->module_count == before && !script->groups[0].module.is_definition)
        native_forget(context, script);
    else if (script->group_count == 1 && !script->groups[0].module.is_definition &&
        !script->groups[0].module.id[0] && !script->groups[0].module.register_name[0])
        context->current_anonymous = script;
    return 0;
}

/* Inspect parsed commands, not comments or string searches. A script owning
 * either production registration, or asserting linkage against it, retains
 * a language namespace. Only ordinary modules request automatic context. */
typedef struct { int required, language; uint64_t pages, table_entries; } native_context_request;
static int production_name(const char *name) {
    return !strcmp(name, "libc") || !strcmp(name, "waste-runtime");
}
static int context_command(wast_stream_command_kind kind, const char *bytes,
    size_t length, size_t offset, unsigned line, wast_script *parsed, void *opaque) {
    (void)bytes; (void)length; (void)offset; (void)line;
    native_context_request *request = opaque;
    for (int g = 0; g < parsed->group_count; g++) {
        wast_group *group = &parsed->groups[g];
        const wast_module *module = &group->module;
        if (production_name(module->register_name)) request->language = 1;
        if (kind == WAST_STREAM_REGISTER) continue;
        int required = 0;
        for (int i = 0; i < module->func_count; i++)
            required |= production_name(module->funcs[i].import_module);
        for (int i = 0; i < module->global_count; i++)
            required |= production_name(module->globals[i].import_module);
        for (int i = 0; i < module->tag_count; i++)
            required |= production_name(module->tags[i].import_module);
        for (int i = 0; i < module->table_count; i++) {
            required |= production_name(module->tables[i].import_module);
            if (production_name(module->tables[i].import_module) &&
                module->tables[i].limits.min > request->table_entries)
                request->table_entries = module->tables[i].limits.min;
        }
        for (int i = 0; i < module->memory_count; i++) {
            required |= production_name(module->memories[i].import_module);
            if (production_name(module->memories[i].import_module) &&
                module->memories[i].limits.min > request->pages)
                request->pages = module->memories[i].limits.min;
        }
        if (group->raw_module.kind != WAST_RAW_NONE) {
            size_t size = 0;
            char error[256] = {0};
            uint8_t *bytes = encode_group_module(group, &size, error);
            wasm_module decoded;
            wasm_decode_error decode_error;
            wasm_module_init(&decoded);
            if (bytes && wasm_decode_module_imports(bytes, size, &decoded, &decode_error) == WASM_DECODE_OK) {
                for (uint32_t i = 0; i < decoded.import_count; i++) {
                    const wasm_import *import = &decoded.imports[i];
                    if (!production_name(import->module)) continue;
                    required = 1;
                    if (import->kind == WASM_IMPORT_MEMORY &&
                        import->descriptor.memory.limits.minimum > request->pages)
                        request->pages = import->descriptor.memory.limits.minimum;
                    if (import->kind == WASM_IMPORT_TABLE &&
                        import->descriptor.table.limits.minimum > request->table_entries)
                        request->table_entries = import->descriptor.table.limits.minimum;
                }
            }
            wasm_module_dispose(&decoded);
            free(bytes);
        }
        if (group->has_module_assertion && required) request->language = 1;
        else if (required) request->required = 1;
    }
    return 0;
}

int native_wast_run_with_options(native_store *store, const char *filename,
    const char *source, size_t length, native_wast_counts *counts,
    const native_wast_options *options) {
    memset(counts, 0, sizeof(*counts));
    native_wast_context context = {0};
    context.store = store;
    context.counts = counts;
    context.line = 1;
    context.options = options;
    native_process_driver_init(&context.driver);
    FILE *out = options->output;
    if (options->json) {
        fprintf(out, "{\"file\":"); native_wast_json_string(out, filename);
        fprintf(out, ",\"assertions\":[\n");
    }
    if (options->mode != NATIVE_WAST_LANGUAGE) {
        native_context_request request = {.pages = PROCESS_INITIAL_MEMORY_PAGES};
        wast_stream scan;
        wast_stream_init(&scan, source, length);
        while (wast_stream_next(&scan, context_command, &request) > 0) {}
        wast_stream_destroy(&scan);
        if (options->mode == NATIVE_WAST_RUNTIME && request.language) {
            wast_setup_record(&context.setup, 1, EXEC_ERROR_FORMAT, WAST_SETUP_LOAD,
                "runtime context conflicts with script-owned providers or linkage assertions");
            context.stopped = 1;
        } else if (options->mode == NATIVE_WAST_RUNTIME || (request.required && !request.language)) {
            exec_error error = {0};
            exec_status status = request.pages <= UINT32_MAX && request.table_entries <= UINT32_MAX ?
                native_process_driver_prepare_runtime(options->driver ? options->driver : &context.driver,
                    store, (uint32_t)request.pages, (uint32_t)request.table_entries, &error) : EXEC_ERROR_FORMAT;
            if (status != EXEC_OK) {
                wast_setup_record(&context.setup, 1, status, WAST_SETUP_LOAD,
                    error.message[0] ? error.message : "runtime context memory exceeds limit");
                context.stopped = 1;
            } else if (options->context_ready) options->context_ready(options->invoke_context);
        }
    }
    wast_stream stream;
    wast_stream_init(&stream, source, length);
    while (!context.stopped) {
        int status = wast_stream_next(&stream, native_command, &context);
        if (status < 0) {
            context.line = stream.line;
            native_parse_failure(&context, stream.error);
        }
        if (status <= 0) {
            context.completed = status == 0 && !context.stopped;
            break;
        }
    }
    wast_stream_destroy(&stream);
    if (options->json) {
        fprintf(out, "\n],\"passed\":%u,\"total\":%u", counts->passed, counts->total);
        if (context.parse_error[0]) { fprintf(out, ",\"error\":"); native_wast_json_string(out, context.parse_error); }
        fprintf(out, ",\"completed\":%s,\"setup\":{\"total\":%u,\"passed\":%u,\"complete\":%s,\"failures\":[",
               context.completed ? "true" : "false", context.setup.total,
               context.setup.passed, context.setup.incomplete ? "false" : "true");
        const char *phases[] = {"", "encode", "load", "definition", "retain"};
        for (unsigned i = 0; i < context.setup.count; i++) {
            const wast_setup_failure *failure = &context.setup.failures[i];
            fprintf(out, "%s{\"line\":%u,\"status\":%d,\"phase\":", i ? "," : "", failure->line, failure->status);
            native_wast_json_string(out, phases[failure->phase]);
            fprintf(out, ",\"error\":"); native_wast_json_string(out, failure->error); fprintf(out, "}");
        }
        fprintf(out, "]}}\n");
    } else {
        if (options->verbose) {
            fprintf(out, "\nWAST: %u PASS, %u FAIL, %u total\n", counts->passed,
                    counts->total - counts->passed, counts->total);
            if (context.setup.passed != context.setup.total)
                fprintf(out, "Setup: %u PASS, %u FAIL, %u total\n", context.setup.passed,
                    context.setup.total - context.setup.passed, context.setup.total);
        }
        if (!context.first_failure[0] && context.setup.count)
            snprintf(context.first_failure, sizeof(context.first_failure), "line %u: %.210s",
                context.setup.failures[0].line, context.setup.failures[0].error);
        if (context.first_failure[0]) fprintf(stderr, "%s: %s\n", filename, context.first_failure);
    }
    native_process_driver_destroy(&context.driver);
    for (unsigned i = 0; i < context.retained_count; i++) {
        wast_script_free(context.retained[i]);
        free(context.retained[i]);
    }
    free(context.retained);
    /* Receipt completeness describes serialized evidence, not script EOF.
     * A truncated script still has a complete JSON failure report. */
    counts->completed = !context.setup.incomplete;
    int passed = counts->passed == counts->total &&
                 context.setup.passed == context.setup.total && context.completed && counts->completed && !ferror(out);
    wast_setup_reset(&context.setup);
    return passed ? 0 : 1;
}

int native_wast_run(native_store *store, const char *filename,
    const char *source, size_t length, native_wast_counts *counts) {
    native_wast_options options = {.json = 1, .output = stdout, .mode = NATIVE_WAST_LANGUAGE};
    return native_wast_run_with_options(store, filename, source, length, counts, &options);
}
