#include "posix_stubs.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* Host imports for POSIX operations delegated to JavaScript */

__attribute__((import_module("waste_host"), import_name("posix_open")))
extern int32_t waste_host_posix_open(const char *path, size_t length,
                                     int32_t flags, int32_t mode);

__attribute__((import_module("waste_host"), import_name("posix_close")))
extern int32_t waste_host_posix_close(int32_t descriptor);

__attribute__((import_module("waste_host"), import_name("posix_read")))
extern int32_t waste_host_posix_read(int32_t descriptor, void *buffer,
                                     uint32_t count);

__attribute__((import_module("waste_host"), import_name("posix_write")))
extern int32_t waste_host_posix_write(int32_t descriptor, const void *buffer,
                                      uint32_t count);

/* ---- POSIX stub helpers ---- */

static exec_status native_posix_memory(native_store *store,
                                       exec_memory **memory_out,
                                       exec_error *error) {
    native_linked_module *runtime = native_registered_module(
        store, "waste-runtime");
    if (!runtime) {
        error->status = EXEC_ERROR_NOT_FOUND;
        snprintf(error->message, sizeof(error->message),
                 "POSIX host call has no waste-runtime module");
        return error->status;
    }
    return exec_find_export_memory(runtime->engine, "memory", memory_out,
                                   error);
}

static int native_posix_range(exec_memory *memory, uint32_t offset,
                              uint32_t length, uint8_t **bytes_out) {
    uint64_t byte_size = memory->pages * UINT64_C(65536);
    if ((uint64_t)offset + length > byte_size) return 0;
    *bytes_out = memory->data + offset;
    return 1;
}

static exec_status native_posix_result(int32_t value, wasm_value *results,
                                       int *result_count) {
    results[0].type = WASM_VALTYPE_I32;
    results[0].i32 = value;
    *result_count = 1;
    return EXEC_OK;
}

/* ---- POSIX stub implementations ---- */

static exec_status native_posix_open(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *path;
    uint64_t byte_size;
    uint32_t offset;
    size_t length = 0;
    if (arg_count != 3 || native_posix_memory(store, &memory, error) != EXEC_OK)
        return error->status;
    offset = (uint32_t)args[0].i32;
    byte_size = memory->pages * UINT64_C(65536);
    if ((uint64_t)offset >= byte_size) {
        error->status = EXEC_ERROR_TRAP;
        snprintf(error->message, sizeof(error->message),
                 "POSIX open path is outside guest memory");
        return error->status;
    }
    path = memory->data + offset;
    while ((uint64_t)offset + length < byte_size && path[length]) length++;
    if ((uint64_t)offset + length == byte_size) {
        error->status = EXEC_ERROR_TRAP;
        snprintf(error->message, sizeof(error->message),
                 "unterminated POSIX open path");
        return error->status;
    }
    return native_posix_result(
        waste_host_posix_open((const char *)path, length, args[1].i32,
                              args[2].i32),
        results, result_count);
}

static exec_status native_posix_close(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error) {
    (void)data;
    if (arg_count != 1) {
        error->status = EXEC_ERROR_FORMAT;
        snprintf(error->message, sizeof(error->message),
                 "POSIX close argument count mismatch");
        return error->status;
    }
    return native_posix_result(waste_host_posix_close(args[0].i32), results,
                               result_count);
}

static exec_status native_posix_read(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *buffer;
    uint32_t count;
    if (arg_count != 3 || native_posix_memory(store, &memory, error) != EXEC_OK)
        return error->status;
    count = (uint32_t)args[2].i32;
    if (!native_posix_range(memory, (uint32_t)args[1].i32, count, &buffer)) {
        error->status = EXEC_ERROR_TRAP;
        snprintf(error->message, sizeof(error->message),
                 "POSIX read buffer is outside guest memory");
        return error->status;
    }
    int32_t read_result = waste_host_posix_read(args[0].i32, buffer, count);
    if (read_result == -2) return EXEC_YIELD;
    return native_posix_result(read_result, results, result_count);
}

static exec_status native_posix_write(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *buffer;
    uint32_t count;
    if (arg_count != 3 || native_posix_memory(store, &memory, error) != EXEC_OK)
        return error->status;
    count = (uint32_t)args[2].i32;
    if (!native_posix_range(memory, (uint32_t)args[1].i32, count, &buffer)) {
        error->status = EXEC_ERROR_TRAP;
        snprintf(error->message, sizeof(error->message),
                 "POSIX write buffer is outside guest memory");
        return error->status;
    }
    return native_posix_result(
        waste_host_posix_write(args[0].i32, buffer, count), results,
        result_count);
}

static exec_status native_posix_i32_zero(void *data, const wasm_value *args,
                                         int arg_count, wasm_value *results,
                                         int *result_count,
                                         exec_error *error) {
    (void)data; (void)args; (void)arg_count; (void)error;
    return native_posix_result(0, results, result_count);
}

static exec_status native_posix_i32_one(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count,
                                        exec_error *error) {
    (void)data; (void)args; (void)arg_count; (void)error;
    return native_posix_result(1, results, result_count);
}

static exec_status native_posix_i32_sixty_four(
        void *data, const wasm_value *args, int arg_count,
        wasm_value *results, int *result_count, exec_error *error) {
    (void)data; (void)args; (void)arg_count; (void)error;
    return native_posix_result(64, results, result_count);
}

static exec_status native_posix_i32_negative(void *data,
                                             const wasm_value *args,
                                             int arg_count,
                                             wasm_value *results,
                                             int *result_count,
                                             exec_error *error) {
    (void)data; (void)args; (void)arg_count; (void)error;
    return native_posix_result(-1, results, result_count);
}

static exec_status native_posix_i64_zero(void *data, const wasm_value *args,
                                         int arg_count, wasm_value *results,
                                         int *result_count,
                                         exec_error *error) {
    (void)data; (void)args; (void)arg_count; (void)error;
    results[0].type = WASM_VALTYPE_I64;
    results[0].i64 = 0;
    *result_count = 1;
    return EXEC_OK;
}

static exec_status native_posix_i64_negative(void *data,
                                             const wasm_value *args,
                                             int arg_count,
                                             wasm_value *results,
                                             int *result_count,
                                             exec_error *error) {
    (void)data; (void)args; (void)arg_count; (void)error;
    results[0].type = WASM_VALTYPE_I64;
    results[0].i64 = -1;
    *result_count = 1;
    return EXEC_OK;
}

static exec_status native_posix_void(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error) {
    (void)data; (void)args; (void)arg_count; (void)results; (void)error;
    *result_count = 0;
    return EXEC_OK;
}

static exec_status native_posix_getcwd(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count,
                                       exec_error *error) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *buffer;
    uint32_t offset;
    uint32_t capacity;
    if (arg_count != 2 ||
        native_posix_memory(store, &memory, error) != EXEC_OK)
        return native_posix_result(0, results, result_count);
    offset = (uint32_t)args[0].i32;
    capacity = (uint32_t)args[1].i32;
    if (offset == 0) {
        native_linked_module *libc = native_registered_module(store, "env");
        uint32_t malloc_index;
        wasm_value malloc_arg;
        wasm_value malloc_result;
        int malloc_result_count = 0;
        if (!libc || exec_find_export(libc->engine, "malloc", &malloc_index,
                                      error) != EXEC_OK)
            return native_posix_result(0, results, result_count);
        malloc_arg.type = WASM_VALTYPE_I32;
        malloc_arg.i32 = 2;
        if (exec_invoke(libc->engine, malloc_index, &malloc_arg, 1,
                        &malloc_result, &malloc_result_count, error) != EXEC_OK ||
            malloc_result_count != 1)
            return native_posix_result(0, results, result_count);
        offset = (uint32_t)malloc_result.i32;
        capacity = 2;
    }
    if (capacity < 2)
        return native_posix_result(0, results, result_count);
    if (!native_posix_range(memory, offset, 2, &buffer))
        return native_posix_result(0, results, result_count);
    buffer[0] = '/';
    buffer[1] = '\0';
    return native_posix_result((int32_t)offset, results, result_count);
}

static exec_status native_posix_stat(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *status;
    uint32_t offset;
    if (arg_count != 2 || native_posix_memory(store, &memory, error) != EXEC_OK)
        return native_posix_result(-1, results, result_count);
    offset = (uint32_t)args[1].i32;
    if (!native_posix_range(memory, offset, 128, &status))
        return native_posix_result(-1, results, result_count);
    memset(status, 0, 128);
    return native_posix_result(0, results, result_count);
}

/* ---- POSIX function dispatch tables ---- */

static exec_host_func native_posix_function(const char *module,
                                             const char *name) {
    if (strcmp(module, "env") != 0) return (void *)0;
    if (strcmp(name, "open") == 0) return native_posix_open;
    if (strcmp(name, "close") == 0) return native_posix_close;
    if (strcmp(name, "read") == 0) return native_posix_read;
    if (strcmp(name, "write") == 0) return native_posix_write;
    if (strcmp(name, "getcwd") == 0) return native_posix_getcwd;
    if (strcmp(name, "getpid") == 0 || strcmp(name, "getpgrp") == 0 ||
        strcmp(name, "tcgetpgrp") == 0 || strcmp(name, "isatty") == 0)
        return native_posix_i32_one;
    if (strcmp(name, "time") == 0) return native_posix_i64_zero;
    if (strcmp(name, "lseek") == 0) return native_posix_i64_negative;
    if (strcmp(name, "abort") == 0 || strcmp(name, "exit") == 0 ||
        strcmp(name, "siglongjmp") == 0)
        return native_posix_void;
    if (strcmp(name, "sigsetjmp") == 0 || strcmp(name, "alarm") == 0 ||
        strcmp(name, "sigemptyset") == 0 || strcmp(name, "sigaddset") == 0 ||
        strcmp(name, "sigdelset") == 0 || strcmp(name, "sigismember") == 0 ||
        strcmp(name, "sigprocmask") == 0 || strcmp(name, "sigaction") == 0 ||
        strcmp(name, "setitimer") == 0 || strcmp(name, "sleep") == 0 ||
        strcmp(name, "setpgid") == 0 || strcmp(name, "tcsetpgrp") == 0 ||
        strcmp(name, "tcgetattr") == 0 || strcmp(name, "tcsetattr") == 0 ||
        strcmp(name, "gettimeofday") == 0 || strcmp(name, "getrusage") == 0 ||
        strcmp(name, "access") == 0 || strcmp(name, "eaccess") == 0 ||
        strcmp(name, "faccessat") == 0 ||
        strcmp(name, "fcntl") == 0 || strcmp(name, "dup") == 0 ||
        strcmp(name, "dup2") == 0 ||
        strcmp(name, "kill") == 0 || strcmp(name, "killpg") == 0 ||
        strcmp(name, "umask") == 0 || strcmp(name, "getppid") == 0)
        return native_posix_i32_zero;
    if (strcmp(name, "stat") == 0 || strcmp(name, "lstat") == 0 ||
        strcmp(name, "fstat") == 0)
        return native_posix_stat;
    if (strcmp(name, "pipe") == 0 || strcmp(name, "fork") == 0 ||
        strcmp(name, "waitpid") == 0 || strcmp(name, "execve") == 0 ||
        strcmp(name, "getgroups") == 0 ||
        strcmp(name, "confstr") == 0 || strcmp(name, "fchmod") == 0 ||
        strcmp(name, "unlink") == 0 || strcmp(name, "rename") == 0 ||
        strcmp(name, "chdir") == 0)
        return native_posix_i32_negative;
    if (strcmp(name, "getdtablesize") == 0)
        return native_posix_i32_sixty_four;
    return (void *)0;
}

static exec_host_control native_posix_control(const char *module,
                                              const char *name) {
    if (strcmp(module, "env") != 0) return EXEC_HOST_CONTROL_NONE;
    if (strcmp(name, "sigsetjmp") == 0)
        return EXEC_HOST_CONTROL_SIGSETJMP;
    if (strcmp(name, "siglongjmp") == 0)
        return EXEC_HOST_CONTROL_SIGLONGJMP;
    if (strcmp(name, "exit") == 0)
        return EXEC_HOST_CONTROL_EXIT;
    return EXEC_HOST_CONTROL_NONE;
}

/* ---- Host resolver wrapping POSIX stubs for the shared linker ---- */

int browser_host_resolver(const char *module, const char *name,
                           void *context, native_host_binding *out) {
    exec_host_func func = native_posix_function(module, name);
    if (!func) return 0;
    out->function = func;
    out->host_data = context;
    out->control = native_posix_control(module, name);
    return 1;
}
