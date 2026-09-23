#include "posix_stubs.h"
#include "../engine/runtime_internal.h"
#include "lib/include/kernel.h"
#include "lib/include/select.h"
#include "lib/include/path.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern void waste_browser_record_transition(const char *event);

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

static exec_status native_posix_memory(const waste_exec_engine *caller,
                                       exec_memory **memory_out,
                                       exec_error *error) {
    if (caller->memory) {
        *memory_out = caller->memory;
        return EXEC_OK;
    }
    error->status = EXEC_ERROR_NOT_FOUND;
    snprintf(error->message, sizeof(error->message),
             "POSIX host call: caller has no memory");
    return error->status;
}

static int native_posix_range(exec_memory *memory, uint32_t offset,
                              uint32_t length, uint8_t **bytes_out) {
    uint64_t byte_size = memory->pages * UINT64_C(65536);
    if ((uint64_t)offset + length > byte_size) return 0;
    if (bytes_out) *bytes_out = memory->data + offset;
    return 1;
}

static exec_status native_posix_result(int32_t value, wasm_value *results,
                                       int *result_count) {
    results[0].type = WASM_VALTYPE_I32;
    results[0].i32 = value;
    *result_count = 1;
    return EXEC_OK;
}

static exec_status native_posix_getpid(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    (void)args; (void)caller; (void)error;
    if (arg_count != 0) return native_posix_result(-POSIX_EINVAL, results, result_count);
    return native_posix_result(native_store_getpid((native_store *)data),
                               results, result_count);
}

static exec_status native_posix_getppid(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error,
                                        const waste_exec_engine *caller) {
    (void)args; (void)caller; (void)error;
    if (arg_count != 0) return native_posix_result(-POSIX_EINVAL, results, result_count);
    return native_posix_result(native_store_getppid((native_store *)data),
                               results, result_count);
}

/* Resolve the caller's guest errno slot through its normal libc export. This
 * keeps errno in the instance's linear memory and avoids a host-global slot. */
static void native_posix_set_errno(native_store *store,
                                   const waste_exec_engine *caller, int value) {
    exec_error ignored = {0};
    uint32_t index;
    wasm_value result;
    int result_count = 0;
    waste_exec_engine *errno_engine = (waste_exec_engine *)caller;
    native_linked_module *env = native_registered_module(store, "env");
    if (env) errno_engine = env->engine;
    if (exec_find_export(errno_engine, "__errno_location", &index, &ignored) != EXEC_OK)
        return;
    if (exec_invoke(errno_engine, index, NULL, 0,
                    &result, &result_count, &ignored) != EXEC_OK ||
        result_count != 1 || result.type != WASM_VALTYPE_I32)
        return;
    exec_memory *memory = caller->memory;
    uint8_t *slot;
    if (memory && native_posix_range(memory, (uint32_t)result.i32, 4, &slot)) {
        slot[0] = (uint8_t)value;
        slot[1] = (uint8_t)(value >> 8);
        slot[2] = (uint8_t)(value >> 16);
        slot[3] = (uint8_t)(value >> 24);
    }
}

static int native_posix_guest_path(const waste_exec_engine *caller,
                                   uint32_t offset, const uint8_t **path,
                                   size_t *length, int *error_number) {
    exec_memory *memory = caller ? caller->memory : NULL;
    if (!memory) { *error_number = POSIX_EFAULT; return 0; }
    uint64_t size = memory->pages * UINT64_C(65536);
    if ((uint64_t)offset >= size) { *error_number = POSIX_EFAULT; return 0; }
    size_t at = 0;
    while ((uint64_t)offset + at < size && at < POSIX_PATH_MAX &&
           memory->data[offset + at]) at++;
    if (at == 0 || at >= POSIX_PATH_MAX || (uint64_t)offset + at >= size) {
        *error_number = at >= POSIX_PATH_MAX ? POSIX_EINVAL : POSIX_EFAULT;
        return 0;
    }
    *path = memory->data + offset;
    *length = at;
    return 1;
}

static int native_posix_copy_guest_string(exec_memory *memory, uint32_t offset,
                                          char **copy_out) {
    const uint8_t *bytes;
    size_t length = 0;
    uint64_t size;
    if (!memory || !copy_out) return POSIX_EFAULT;
    size = memory->pages * UINT64_C(65536);
    if ((uint64_t)offset >= size) return POSIX_EFAULT;
    while ((uint64_t)offset + length < size &&
           length < NATIVE_EXEC_PATH_MAX && memory->data[offset + length])
        length++;
    if ((uint64_t)offset + length >= size) return POSIX_EFAULT;
    if (length == NATIVE_EXEC_PATH_MAX) return POSIX_E2BIG;
    bytes = memory->data + offset;
    *copy_out = (char *)malloc(length + 1);
    if (!*copy_out) return POSIX_ENOMEM;
    memcpy(*copy_out, bytes, length);
    (*copy_out)[length] = '\0';
    return 0;
}

static int native_posix_copy_guest_vector(exec_memory *memory, uint32_t vector,
                                          char **items, uint32_t capacity,
                                          uint32_t *count_out,
                                          int allow_null) {
    uint64_t size;
    uint32_t count = 0;
    if (!count_out || !items) return POSIX_EFAULT;
    *count_out = 0;
    if (vector == 0 && allow_null) return 0;
    if (!memory) return POSIX_EFAULT;
    size = memory->pages * UINT64_C(65536);
    for (;;) {
        uint64_t slot = (uint64_t)vector + (uint64_t)count * 4u;
        uint32_t pointer;
        char *copy = NULL;
        int status;
        if (count >= capacity || slot + 4u > size) return POSIX_E2BIG;
        pointer = (uint32_t)memory->data[slot] |
                  ((uint32_t)memory->data[slot + 1] << 8) |
                  ((uint32_t)memory->data[slot + 2] << 16) |
                  ((uint32_t)memory->data[slot + 3] << 24);
        if (pointer == 0) break;
        status = native_posix_copy_guest_string(memory, pointer, &copy);
        if (status != 0) return status;
        items[count++] = copy;
    }
    *count_out = count;
    return 0;
}

static void native_posix_metadata_stat(const posix_path_metadata *metadata,
                                       posix_guest_stat *stat) {
    memset(stat, 0, sizeof(*stat));
    stat->st_ino = metadata->inode;
    stat->st_mode = metadata->mode;
    stat->st_nlink = metadata->kind == POSIX_NODE_DIRECTORY ? 2 : 1;
    stat->st_uid = metadata->uid;
    stat->st_gid = metadata->gid;
    stat->st_size = metadata->size;
    stat->st_blksize = 4096;
    stat->st_blocks = (metadata->size + 511) / 512;
}

/* ---- POSIX stub implementations ---- */

static exec_status native_posix_open(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *path;
    uint64_t byte_size;
    uint32_t offset;
    size_t length = 0;
    if (arg_count != 3 || native_posix_memory(caller, &memory, error) != EXEC_OK)
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
    if (store && store->kernel)
        return native_posix_result(posix_kernel_open(
            store->kernel, path, length, args[1].i32, args[2].i32),
            results, result_count);
    return native_posix_result(waste_host_posix_open(
        (const char *)path, length, args[1].i32, args[2].i32), results,
        result_count);
}

static exec_status native_posix_close(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    (void)data; (void)caller;
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
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *buffer;
    uint32_t count;
    if (arg_count != 3 || native_posix_memory(caller, &memory, error) != EXEC_OK)
        return error->status;
    count = (uint32_t)args[2].i32;
    if (!native_posix_range(memory, (uint32_t)args[1].i32, count, &buffer)) {
        error->status = EXEC_ERROR_TRAP;
        snprintf(error->message, sizeof(error->message),
                 "POSIX read buffer is outside guest memory");
        return error->status;
    }
    int32_t read_result;
    if (store->kernel_terminal) {
        read_result = posix_kernel_read(store->kernel, args[0].i32,
                                        buffer, (int)count);
        if (read_result == -POSIX_EAGAIN) {
            waste_browser_record_transition("read-eagain");
            error->yield_reason = EXEC_YIELD_READ;
            return EXEC_YIELD;
        }
        if (read_result >= 0)
            waste_browser_record_transition("read-data");
        else
            waste_browser_record_transition("read-error");
    } else if (store->kernel) {
        read_result = posix_kernel_read(store->kernel, args[0].i32,
                                        buffer, (int)count);
    } else {
        /* Noninteractive WAST sandboxes retain the narrow host capability. */
        read_result = waste_host_posix_read(args[0].i32, buffer, count);
        if (read_result == -2) {
            error->yield_reason = EXEC_YIELD_READ;
            return EXEC_YIELD;
        }
    }
    return native_posix_result(read_result, results, result_count);
}

static exec_status native_posix_write(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *buffer;
    uint32_t count;
    if (arg_count != 3 || native_posix_memory(caller, &memory, error) != EXEC_OK)
        return error->status;
    count = (uint32_t)args[2].i32;
    if (!native_posix_range(memory, (uint32_t)args[1].i32, count, &buffer)) {
        error->status = EXEC_ERROR_TRAP;
        snprintf(error->message, sizeof(error->message),
                 "POSIX write buffer is outside guest memory");
        return error->status;
    }
    if (store && store->kernel_terminal &&
        posix_kernel_isatty(store->kernel, args[0].i32)) {
        uint8_t translated[512];
        uint32_t offset = 0;
        while (offset < count) {
            uint32_t chunk = count - offset;
            if (chunk > 256) chunk = 256;
            int translated_count = posix_kernel_terminal_process_output(
                store->kernel, args[0].i32, buffer + offset, (int)chunk,
                translated, (int)sizeof(translated));
            if (translated_count < 0)
                return native_posix_result(translated_count, results, result_count);
            int32_t written = waste_host_posix_write(
                args[0].i32, translated, (uint32_t)translated_count);
            char write_event[32];
            int has_bash_prompt = 0;
            for (int i = 0; i + 4 < translated_count; i++)
                if (translated[i] == 'b' && translated[i + 1] == 'a' &&
                    translated[i + 2] == 's' && translated[i + 3] == 'h' &&
                    translated[i + 4] == '-') {
                    has_bash_prompt = 1;
                    break;
                }
            snprintf(write_event, sizeof(write_event), "term-write-n%d%s",
                     translated_count, has_bash_prompt ? "-bash" : "");
            waste_browser_record_transition(write_event);
            if (written < 0)
                return native_posix_result(written, results, result_count);
            if (written != translated_count)
                return native_posix_result((int32_t)offset, results, result_count);
            offset += chunk;
        }
        return native_posix_result((int32_t)count, results, result_count);
    }
    if (store && store->kernel)
        return native_posix_result(posix_kernel_write(store->kernel, args[0].i32,
                                                      buffer, (int)count),
                                   results, result_count);
    return native_posix_result(waste_host_posix_write(args[0].i32, buffer, count),
                               results, result_count);
}

static exec_status native_posix_i32_zero(void *data, const wasm_value *args,
                                         int arg_count, wasm_value *results,
                                         int *result_count,
                                         exec_error *error,
                                         const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)error; (void)caller;
    return native_posix_result(0, results, result_count);
}

static exec_status native_posix_i32_one(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count,
                                        exec_error *error,
                                        const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)error; (void)caller;
    return native_posix_result(1, results, result_count);
}

static exec_status native_posix_getpgrp(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error,
                                        const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    (void)caller; (void)error;
    if (!store || arg_count != 0)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    return native_posix_result(posix_kernel_getpgid(store->kernel), results,
                               result_count);
}

static exec_status native_posix_setpgid(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error,
                                        const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int target_pid;
    (void)caller; (void)error;
    if (!store || arg_count != 2 || args[0].i32 < 0 || args[1].i32 <= 0)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    target_pid = args[0].i32 == 0 ? native_store_getpid(store) : args[0].i32;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++) {
        if (store->processes[i].used && store->processes[i].pid == target_pid)
            return native_posix_result(posix_kernel_setpgid(
                                           store->processes[i].kernel,
                                           args[1].i32), results, result_count);
    }
    return native_posix_result(-POSIX_EINVAL, results, result_count);
}

static exec_status native_posix_tcgetpgrp(void *data, const wasm_value *args,
                                          int arg_count, wasm_value *results,
                                          int *result_count, exec_error *error,
                                          const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    (void)caller; (void)error;
    if (!store || arg_count != 1)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    return native_posix_result(posix_kernel_terminal_get_foreground_pgid(
                                   store->kernel, args[0].i32),
                               results, result_count);
}

static exec_status native_posix_tcsetpgrp(void *data, const wasm_value *args,
                                          int arg_count, wasm_value *results,
                                          int *result_count, exec_error *error,
                                          const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    (void)caller; (void)error;
    if (!store || arg_count != 2)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    return native_posix_result(posix_kernel_terminal_set_foreground_pgid(
                                   store->kernel, args[0].i32, args[1].i32),
                               results, result_count);
}

/* Stable guest sigaction prefix: handler pointer at offset zero, followed by
 * a fixed-width 128-bit mask. Flags remain outside this ABI prefix. */
static exec_status native_posix_sigaction(void *data, const wasm_value *args,
                                          int arg_count, wasm_value *results,
                                          int *result_count, exec_error *error,
                                          const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    if (!store || !store->kernel || arg_count != 3)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    int signal = args[0].i32;
    uint32_t action_ptr = (uint32_t)args[1].i32;
    uint32_t old_ptr = (uint32_t)args[2].i32;
    uint32_t old_handler = POSIX_SIG_DFL;
    posix_sigset old_mask = {{0, 0, 0, 0}};
    if (posix_kernel_signal_get_handler(store->kernel, signal, &old_handler) < 0)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    if (posix_kernel_signal_get_action_mask(store->kernel, signal, &old_mask) < 0)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    if (old_ptr) {
        exec_memory *memory;
        uint8_t *bytes;
        if (native_posix_memory(caller, &memory, error) != EXEC_OK ||
            !native_posix_range(memory, old_ptr,
                                sizeof(uint32_t) + POSIX_SIGSET_BYTES, &bytes))
            return native_posix_result(-POSIX_EFAULT, results, result_count);
        memcpy(bytes, &old_handler, sizeof(old_handler));
        memcpy(bytes + sizeof(old_handler), &old_mask, POSIX_SIGSET_BYTES);
    }
    if (action_ptr) {
        exec_memory *memory;
        uint8_t *bytes;
        uint32_t handler;
        posix_sigset action_mask;
        if (native_posix_memory(caller, &memory, error) != EXEC_OK ||
            !native_posix_range(memory, action_ptr,
                                sizeof(uint32_t) + POSIX_SIGSET_BYTES, &bytes))
            return native_posix_result(-POSIX_EFAULT, results, result_count);
        memcpy(&handler, bytes, sizeof(handler));
        memcpy(&action_mask, bytes + sizeof(handler), POSIX_SIGSET_BYTES);
        posix_signal_disposition disposition = POSIX_SIGNAL_HANDLER;
        if (handler == POSIX_SIG_DFL) disposition = POSIX_SIGNAL_DEFAULT;
        else if (handler == POSIX_SIG_IGN || handler == UINT32_C(1))
            disposition = POSIX_SIGNAL_IGNORE;
        if (posix_kernel_signal_set_disposition(store->kernel, signal,
                                                disposition) < 0)
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_kernel_signal_set_handler(store->kernel, signal, handler);
        posix_kernel_signal_set_action_mask(store->kernel, signal, &action_mask);
    }
    return native_posix_result(0, results, result_count);
}

static exec_status native_posix_isatty(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    (void)error; (void)caller;
    if (arg_count != 1) return native_posix_result(-POSIX_EINVAL, results, result_count);
    native_store *store = (native_store *)data;
    return native_posix_result(store->kernel &&
        posix_kernel_isatty(store->kernel, args[0].i32) ? 1 : 0,
        results, result_count);
}

static exec_status native_posix_tcgetattr(void *data, const wasm_value *args,
                                          int arg_count, wasm_value *results,
                                          int *result_count, exec_error *error,
                                          const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = NULL;
    uint8_t *bytes = NULL;
    posix_termios termios;
    if (arg_count != 2 || !store->kernel ||
        native_posix_memory(caller, &memory, error) != EXEC_OK ||
        !native_posix_range(memory, (uint32_t)args[1].i32, sizeof(termios), &bytes) ||
        posix_kernel_tcgetattr(store->kernel, args[0].i32, &termios) < 0)
        return native_posix_result(-POSIX_EBADF, results, result_count);
    memcpy(bytes, &termios, sizeof(termios));
    return native_posix_result(0, results, result_count);
}

static exec_status native_posix_tcsetattr(void *data, const wasm_value *args,
                                          int arg_count, wasm_value *results,
                                          int *result_count, exec_error *error,
                                          const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = NULL;
    uint8_t *bytes = NULL;
    posix_termios termios;
    if (arg_count != 3 || !store->kernel ||
        native_posix_memory(caller, &memory, error) != EXEC_OK ||
        !native_posix_range(memory, (uint32_t)args[2].i32, sizeof(termios), &bytes))
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    memcpy(&termios, bytes, sizeof(termios));
    return native_posix_result(posix_kernel_tcsetattr(store->kernel, args[0].i32,
                                                       &termios),
                               results, result_count);
}

static exec_status native_posix_ioctl(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = NULL;
    uint8_t *bytes = NULL;
    if (arg_count != 3 || !store->kernel ||
        native_posix_memory(caller, &memory, error) != EXEC_OK)
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    if (args[1].i32 == POSIX_TIOCGWINSZ) {
        posix_winsize winsize;
        if (!native_posix_range(memory, (uint32_t)args[2].i32, sizeof(winsize), &bytes) ||
            posix_kernel_terminal_get_winsize(store->kernel, args[0].i32, &winsize) < 0)
            return native_posix_result(-POSIX_EBADF, results, result_count);
        memcpy(bytes, &winsize, sizeof(winsize));
        return native_posix_result(0, results, result_count);
    }
    if (args[1].i32 == POSIX_TIOCSWINSZ) {
        posix_winsize winsize;
        if (!native_posix_range(memory, (uint32_t)args[2].i32, sizeof(winsize), &bytes))
            return native_posix_result(-POSIX_EFAULT, results, result_count);
        memcpy(&winsize, bytes, sizeof(winsize));
        return native_posix_result(posix_kernel_terminal_set_winsize(
            store->kernel, args[0].i32, &winsize), results, result_count);
    }
    return native_posix_result(-POSIX_EINVAL, results, result_count);
}

static exec_status native_posix_tcflow(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    (void)error; (void)caller;
    native_store *store = (native_store *)data;
    if (arg_count != 2 || !store->kernel)
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    return native_posix_result(posix_kernel_tcflow(store->kernel,
                                                   args[0].i32, args[1].i32),
                                results, result_count);
}

static exec_status native_posix_i32_sixty_four(
        void *data, const wasm_value *args, int arg_count,
        wasm_value *results, int *result_count, exec_error *error,
        const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)error; (void)caller;
    return native_posix_result(64, results, result_count);
}

static exec_status native_posix_i32_negative(void *data,
                                             const wasm_value *args,
                                             int arg_count,
                                             wasm_value *results,
                                             int *result_count,
                                             exec_error *error,
                                             const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)error; (void)caller;
    return native_posix_result(-1, results, result_count);
}

static exec_status native_posix_i64_zero(void *data, const wasm_value *args,
                                         int arg_count, wasm_value *results,
                                         int *result_count,
                                         exec_error *error,
                                         const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)error; (void)caller;
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
                                             exec_error *error,
                                             const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)error; (void)caller;
    results[0].type = WASM_VALTYPE_I64;
    results[0].i64 = -1;
    *result_count = 1;
    return EXEC_OK;
}

static exec_status native_posix_void(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)results; (void)error;
    (void)caller;
    *result_count = 0;
    return EXEC_OK;
}

static exec_status native_posix_exit(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    (void)data; (void)results; (void)caller;
    if (arg_count != 1) return native_posix_result(-POSIX_EINVAL, results,
                                                   result_count);
    if (error) {
        error->status = EXEC_ERROR_EXIT;
        error->exit_code = args[0].i32 & 0xff;
    }
    *result_count = 0;
    return EXEC_ERROR_EXIT;
}

static exec_status native_posix_raise(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int status;
    posix_signal_disposition disposition;
    (void)caller; (void)error;
    if (!store || arg_count != 1)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    status = posix_kernel_signal_raise(store->kernel, args[0].i32);
    if (status == 0 && posix_kernel_signal_get_disposition(
            store->kernel, args[0].i32, &disposition) == 0 &&
        disposition == POSIX_SIGNAL_DEFAULT && args[0].i32 != POSIX_SIGSTOP) {
        if (error) {
            error->status = EXEC_ERROR_EXIT;
            error->exit_code = 128 + args[0].i32;
        }
        return EXEC_ERROR_EXIT;
    }
    return native_posix_result(status, results, result_count);
}

static exec_status native_posix_kill(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int pid;
    (void)caller; (void)error;
    if (!store || arg_count != 2 || args[0].i32 < 0)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    pid = args[0].i32 == 0 ? native_store_getpid(store) : args[0].i32;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++) {
        if (store->processes[i].used && store->processes[i].pid == pid)
            return native_posix_result(native_store_signal_process(
                                           store, pid, args[1].i32), results,
                                       result_count);
    }
    return native_posix_result(-POSIX_EINVAL, results, result_count);
}

static exec_status native_posix_killpg(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int pgid, delivered = 0;
    (void)caller; (void)error;
    if (!store || arg_count != 2 || args[0].i32 < 0)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    pgid = args[0].i32 == 0 ? posix_kernel_getpgid(store->kernel) : args[0].i32;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++) {
        if (store->processes[i].used &&
            posix_kernel_getpgid(store->processes[i].kernel) == pgid) {
            (void)native_store_signal_process(store,
                                               store->processes[i].pid,
                                               args[1].i32);
            delivered = 1;
        }
    }
    return native_posix_result(delivered ? 0 : -POSIX_EINVAL, results,
                               result_count);
}

static exec_status native_posix_fcntl(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int fd, command;
    (void)error; (void)caller;
    if (!store || arg_count < 2) return native_posix_result(-POSIX_EINVAL,
                                                             results, result_count);
    fd = args[0].i32;
    command = args[1].i32;
    if (command == POSIX_F_GETFD)
        return native_posix_result(posix_kernel_get_cloexec(store->kernel, fd),
                                   results, result_count);
    if (command == POSIX_F_SETFD && arg_count >= 3)
        return native_posix_result(posix_kernel_set_cloexec(
                                       store->kernel, fd, args[2].i32),
                                   results, result_count);
    return native_posix_result(-POSIX_EINVAL, results, result_count);
}

static exec_status native_posix_getcwd(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count,
                                       exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *buffer;
    uint32_t offset;
    uint32_t capacity;
    if (arg_count != 2 ||
        native_posix_memory(caller, &memory, error) != EXEC_OK)
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
    if (!native_posix_range(memory, offset, capacity, &buffer))
        return native_posix_result(0, results, result_count);
    if (!store->kernel || posix_kernel_getcwd(store->kernel, (char *)buffer,
                                               capacity) < 0)
        return native_posix_result(0, results, result_count);
    return native_posix_result((int32_t)offset, results, result_count);
}

static exec_status native_posix_chdir(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    const uint8_t *path;
    size_t length;
    int errno_value = 0;
    if (arg_count != 1 || !native_posix_guest_path(caller, (uint32_t)args[0].i32,
                                                   &path, &length, &errno_value))
        return native_posix_result(-1, results, result_count);
    int result = store->kernel ? posix_kernel_path_set_cwd(
        store->kernel, (const char *)path) : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    (void)error;
    return native_posix_result(result < 0 ? -1 : 0, results, result_count);
}

static exec_status native_posix_mkdir(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    const uint8_t *path; size_t length; int errno_value = 0;
    if (arg_count != 2 || !native_posix_guest_path(caller, (uint32_t)args[0].i32,
                                                   &path, &length, &errno_value))
        return native_posix_result(-1, results, result_count);
    int result = store->kernel ? posix_kernel_path_mkdir(
        store->kernel, path, length, args[1].i32) : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    (void)error;
    return native_posix_result(result < 0 ? -1 : 0, results, result_count);
}

static exec_status native_posix_unlink(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    const uint8_t *path; size_t length; int errno_value = 0;
    if (arg_count != 1 || !native_posix_guest_path(caller, (uint32_t)args[0].i32,
                                                   &path, &length, &errno_value))
        return native_posix_result(-1, results, result_count);
    int result = store->kernel ? posix_kernel_path_unlink(
        store->kernel, path, length, 0) : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    (void)error;
    return native_posix_result(result < 0 ? -1 : 0, results, result_count);
}

static exec_status native_posix_rename(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    const uint8_t *source, *destination; size_t source_length, destination_length;
    int errno_value = 0;
    if (arg_count != 2 || !native_posix_guest_path(caller, (uint32_t)args[0].i32,
                                                   &source, &source_length, &errno_value) ||
        !native_posix_guest_path(caller, (uint32_t)args[1].i32,
                                 &destination, &destination_length, &errno_value))
        return native_posix_result(-1, results, result_count);
    int result = store->kernel ? posix_kernel_path_rename(
        store->kernel, source, source_length, destination, destination_length)
        : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    (void)error;
    return native_posix_result(result < 0 ? -1 : 0, results, result_count);
}

static exec_status native_posix_readdir(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error,
                                        const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *name_buffer;
    uint8_t *metadata_buffer;
    posix_path_metadata metadata;
    if (arg_count != 4 || native_posix_memory(caller, &memory, error) != EXEC_OK ||
        !native_posix_range(memory, (uint32_t)args[1].i32,
                            (uint32_t)args[2].i32, &name_buffer) ||
        !native_posix_range(memory, (uint32_t)args[3].i32,
                            POSIX_PATH_METADATA_BYTES, &metadata_buffer))
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    int result = store->kernel ? posix_kernel_readdir(
        store->kernel, args[0].i32, (char *)name_buffer, (size_t)args[2].i32,
        &metadata) : -POSIX_ENOSYS;
    if (result > 0) posix_path_metadata_encode(metadata_buffer, &metadata);
    if (result < 0) native_posix_set_errno(store, caller, -result);
    return native_posix_result(result, results, result_count);
}

static exec_status native_posix_readlink(void *data, const wasm_value *args,
                                         int arg_count, wasm_value *results,
                                         int *result_count, exec_error *error,
                                         const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    const uint8_t *path; size_t path_length; uint8_t *buffer;
    int errno_value = 0;
    if (arg_count != 3 || native_posix_memory(caller, &memory, error) != EXEC_OK ||
        !native_posix_guest_path(caller, (uint32_t)args[0].i32, &path,
                                 &path_length, &errno_value) ||
        !native_posix_range(memory, (uint32_t)args[1].i32,
                            (uint32_t)args[2].i32, &buffer))
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    int result = store->kernel ? posix_kernel_path_readlink(
        store->kernel, path, path_length, (char *)buffer, (size_t)args[2].i32)
        : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    return native_posix_result(result, results, result_count);
}

static exec_status native_posix_rmdir(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    const uint8_t *path; size_t length; int errno_value = 0;
    if (arg_count != 1 || !native_posix_guest_path(caller, (uint32_t)args[0].i32,
                                                   &path, &length, &errno_value))
        return native_posix_result(-1, results, result_count);
    int result = store->kernel ? posix_kernel_path_unlink(
        store->kernel, path, length, 1) : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    (void)error;
    return native_posix_result(result < 0 ? -1 : 0, results, result_count);
}

static exec_status native_posix_lseek(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int64_t result_value = -1;
    int result = arg_count == 3 && store->kernel ? posix_kernel_lseek(
        store->kernel, args[0].i32, args[1].i64, args[2].i32, &result_value)
        : -POSIX_EINVAL;
    if (result < 0) {
        native_posix_set_errno(store, caller, -result);
        result_value = -1;
    }
    results[0].type = WASM_VALTYPE_I64;
    results[0].i64 = result_value;
    *result_count = 1;
    (void)error;
    return EXEC_OK;
}

static exec_status native_posix_stat_common(void *data, const wasm_value *args,
                                            int arg_count, wasm_value *results,
                                            int *result_count, exec_error *error,
                                            int follow,
                                            const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    const uint8_t *path;
    size_t path_length;
    uint8_t *status;
    posix_path_metadata metadata;
    posix_guest_stat guest_stat;
    int errno_value = POSIX_ENOSYS;
    if (arg_count != 2 || native_posix_memory(caller, &memory, error) != EXEC_OK)
        return native_posix_result(-1, results, result_count);
    if (!native_posix_guest_path(caller, (uint32_t)args[0].i32, &path,
                                 &path_length, &errno_value) ||
        !native_posix_range(memory, (uint32_t)args[1].i32,
                            POSIX_GUEST_STAT_BYTES, &status)) {
        native_posix_set_errno(store, caller, errno_value ? errno_value : POSIX_EFAULT);
        return native_posix_result(-1, results, result_count);
    }
    int result = store->kernel ? posix_kernel_path_stat(
        store->kernel, path, path_length, follow, &metadata) : -POSIX_ENOSYS;
    if (result < 0) {
        native_posix_set_errno(store, caller, -result);
        return native_posix_result(-1, results, result_count);
    }
    native_posix_metadata_stat(&metadata, &guest_stat);
    posix_guest_stat_encode(status, &guest_stat);
    return native_posix_result(0, results, result_count);
}

static exec_status native_posix_stat(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    return native_posix_stat_common(data, args, arg_count, results,
                                    result_count, error, 1, caller);
}

static exec_status native_posix_lstat(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    return native_posix_stat_common(data, args, arg_count, results,
                                    result_count, error, 0, caller);
}

static exec_status native_posix_access(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    const uint8_t *path;
    size_t path_length;
    int errno_value = 0;
    if (arg_count != 2 || !native_posix_guest_path(
            caller, (uint32_t)args[0].i32, &path, &path_length, &errno_value)) {
        native_posix_set_errno(store, caller, errno_value ? errno_value : POSIX_EFAULT);
        return native_posix_result(-1, results, result_count);
    }
    int result = store->kernel ? posix_kernel_path_access(
        store->kernel, path, path_length, args[1].i32, 0) : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    (void)error;
    return native_posix_result(result < 0 ? -1 : 0, results, result_count);
}

static exec_status native_posix_faccessat(void *data, const wasm_value *args,
                                          int arg_count, wasm_value *results,
                                          int *result_count, exec_error *error,
                                          const waste_exec_engine *caller) {
    if (arg_count != 4) return native_posix_result(-1, results, result_count);
    /* The initial namespace has no dirfd-relative escape; AT_FDCWD is the
     * only accepted base and is represented by -100. */
    if (args[0].i32 != -100) {
        native_posix_set_errno((native_store *)data, caller, POSIX_EBADF);
        return native_posix_result(-1, results, result_count);
    }
    wasm_value access_args[2] = { args[1], args[2] };
    return native_posix_access(data, access_args, 2, results, result_count,
                               error, caller);
}

static exec_status native_posix_fstat(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *status;
    posix_guest_stat guest_stat;
    if (arg_count != 2 || native_posix_memory(caller, &memory, error) != EXEC_OK ||
        !native_posix_range(memory, (uint32_t)args[1].i32,
                            POSIX_GUEST_STAT_BYTES, &status)) {
        native_posix_set_errno(store, caller, POSIX_EFAULT);
        return native_posix_result(-1, results, result_count);
    }
    if (!store->kernel || args[0].i32 < 0 || args[0].i32 >= POSIX_KERNEL_FD_MAX ||
        !store->kernel->fds[args[0].i32].ofd) {
        native_posix_set_errno(store, caller, POSIX_EBADF);
        return native_posix_result(-1, results, result_count);
    }
    posix_path_metadata metadata = {
        POSIX_NODE_REGULAR, 0666, 0, 0, 0, (uint64_t)(args[0].i32 + 1)
    };
    native_posix_metadata_stat(&metadata, &guest_stat);
    posix_guest_stat_encode(status, &guest_stat);
    return native_posix_result(0, results, result_count);
}

static exec_status native_posix_fork(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    (void)args; (void)arg_count; (void)caller;
    native_store *store = (native_store *)data;
    native_process_capsule *capsule = native_store_active_capsule(store);
    if (capsule && capsule->pending_result_valid) {
        int result = 0;
        if (native_store_take_process_wake(store, &result) == 0)
            return native_posix_result(result, results, result_count);
    }
    if (store->fork_child_resume) {
        store->fork_child_resume = 0;
        return native_posix_result(0, results, result_count);
    }
    if (store->fork_parent_resume) {
        int pid = store->fork_child_pid;
        store->fork_parent_resume = 0;
        return native_posix_result(pid, results, result_count);
    }
    if (error) {
        memset(error, 0, sizeof(*error));
        error->status = EXEC_YIELD;
        error->yield_reason = EXEC_YIELD_FORK;
    }
    return EXEC_YIELD;
}

static exec_status native_posix_waitpid(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error,
                                        const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = NULL;
    uint8_t *status_bytes = NULL;
    if (arg_count != 3 || native_posix_memory(caller, &memory, error) != EXEC_OK ||
        !native_posix_range(memory, (uint32_t)args[1].i32, 4, &status_bytes)) {
        native_posix_set_errno(store, caller, POSIX_EFAULT);
        return native_posix_result(-1, results, result_count);
    }
    int status = 0;
    int waited = native_store_wait_process(store, args[0].i32, args[2].i32,
                                           &status);
    if (waited < 0) {
        if (waited == -POSIX_ECHILD && args[0].i32 == 0 &&
            store->last_wait_pid > 0) {
            status = store->last_wait_status;
            status_bytes[0] = (uint8_t)status;
            status_bytes[1] = (uint8_t)(status >> 8);
            status_bytes[2] = (uint8_t)(status >> 16);
            status_bytes[3] = (uint8_t)(status >> 24);
            return native_posix_result(store->last_wait_pid, results, result_count);
        }
        native_posix_set_errno(store, caller, -waited);
        return native_posix_result(-1, results, result_count);
    }
    status_bytes[0] = (uint8_t)status;
    status_bytes[1] = (uint8_t)(status >> 8);
    status_bytes[2] = (uint8_t)(status >> 16);
    status_bytes[3] = (uint8_t)(status >> 24);
    return native_posix_result(waited, results, result_count);
}

static exec_status native_posix_execve(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = NULL;
    const uint8_t *path = NULL;
    size_t path_length = 0;
    int path_error = POSIX_EFAULT;
    int status;
    if (store->exec_request.active &&
        store->exec_request.pid == native_store_getpid(store) &&
        store->exec_request.failure_errno != 0) {
        int failure = store->exec_request.failure_errno;
        native_exec_request_destroy(&store->exec_request);
        native_posix_set_errno(store, caller, failure);
        return native_posix_result(-1, results, result_count);
    }
    if (arg_count != 3 || native_posix_memory(caller, &memory, error) != EXEC_OK ||
        !native_posix_guest_path(caller, (uint32_t)args[0].i32, &path,
                                 &path_length, &path_error)) {
        native_posix_set_errno(store, caller, path_error);
        return native_posix_result(-1, results, result_count);
    }
    if (path_length >= NATIVE_EXEC_PATH_MAX) {
        native_posix_set_errno(store, caller, POSIX_E2BIG);
        return native_posix_result(-1, results, result_count);
    }
    if (store->kernel) {
        int access = posix_kernel_path_access(
            store->kernel, path, path_length, POSIX_X_OK, 0);
        if (access != 0 &&
            !(access == -POSIX_ENOENT &&
              native_store_find_executable(store, (const char *)path))) {
            native_posix_set_errno(store, caller, -access);
            return native_posix_result(-1, results, result_count);
        }
    } else if (!native_store_find_executable(store, (const char *)path)) {
        native_posix_set_errno(store, caller, POSIX_ENOENT);
        return native_posix_result(-1, results, result_count);
    }
    if (store->exec_request.active) {
        native_posix_set_errno(store, caller, POSIX_EBUSY);
        return native_posix_result(-1, results, result_count);
    }
    native_exec_request_destroy(&store->exec_request);
    memcpy(store->exec_request.path, path, path_length);
    store->exec_request.path[path_length] = '\0';
    status = native_posix_copy_guest_vector(
        memory, (uint32_t)args[1].i32, store->exec_request.argv,
        NATIVE_EXEC_ARG_MAX, &store->exec_request.argc, 0);
    if (status == 0)
        status = native_posix_copy_guest_vector(
            memory, (uint32_t)args[2].i32, store->exec_request.envp,
            NATIVE_EXEC_ENV_MAX, &store->exec_request.envc, 1);
    if (status != 0) {
        native_exec_request_destroy(&store->exec_request);
        native_posix_set_errno(store, caller, status);
        return native_posix_result(-1, results, result_count);
    }
    store->exec_request.pid = native_store_getpid(store);
    store->exec_request.active = 1;
    if (error) {
        memset(error, 0, sizeof(*error));
        error->status = EXEC_YIELD;
        error->yield_reason = EXEC_YIELD_EXEC;
    }
    return EXEC_YIELD;
}

static exec_status native_posix_path_access_v1(
        void *data, const wasm_value *args, int arg_count,
        wasm_value *results, int *result_count, exec_error *error,
        const waste_exec_engine *caller) {
    (void)error;
    if (arg_count != 4) return native_posix_result(-POSIX_EINVAL, results, result_count);
    exec_memory *memory = caller->memory;
    if (!memory || !native_posix_range(memory, (uint32_t)args[0].i32,
                                       (uint32_t)args[1].i32, NULL))
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    const uint8_t *path = memory->data + (uint32_t)args[0].i32;
    int result = ((native_store *)data)->kernel ? posix_kernel_path_access(
        ((native_store *)data)->kernel, path, (size_t)args[1].i32,
        args[2].i32, args[3].i32) : -POSIX_ENOSYS;
    return native_posix_result(result, results, result_count);
}

static exec_status native_posix_path_stat_v1(
        void *data, const wasm_value *args, int arg_count,
        wasm_value *results, int *result_count, exec_error *error,
        const waste_exec_engine *caller) {
    (void)error;
    if (arg_count != 4) return native_posix_result(-POSIX_EINVAL, results, result_count);
    exec_memory *memory = caller->memory;
    uint8_t *metadata_bytes;
    if (!memory || !native_posix_range(memory, (uint32_t)args[0].i32,
                                       (uint32_t)args[1].i32, NULL) ||
        !native_posix_range(memory, (uint32_t)args[3].i32,
                            POSIX_PATH_METADATA_BYTES, &metadata_bytes))
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    posix_path_metadata metadata;
    int result = ((native_store *)data)->kernel ? posix_kernel_path_stat(
        ((native_store *)data)->kernel,
        memory->data + (uint32_t)args[0].i32, (size_t)args[1].i32,
        args[2].i32, &metadata) : -POSIX_ENOSYS;
    if (result == 0) posix_path_metadata_encode(metadata_bytes, &metadata);
    return native_posix_result(result, results, result_count);
}

/* ---- Kernel select/pselect host imports ---- */

static exec_status native_posix_select(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    if (arg_count != 5 ||
        native_posix_memory(caller, &memory, error) != EXEC_OK)
        return error->status;
    if (!store->kernel)
        return native_posix_result(-POSIX_EINVAL, results, result_count);

    int32_t nfds = args[0].i32;
    uint32_t read_ptr = (uint32_t)args[1].i32;
    uint32_t write_ptr = (uint32_t)args[2].i32;
    uint32_t except_ptr = (uint32_t)args[3].i32;
    uint32_t timeout_ptr = (uint32_t)args[4].i32;
    uint64_t byte_size = memory->pages * UINT64_C(65536);

    /* Validate and decode fd_sets. */
    posix_fd_set rds, wrs, exs;
    posix_fd_set *rp = (void *)0, *wp = (void *)0, *ep = (void *)0;
    if (read_ptr) {
        if ((uint64_t)read_ptr + POSIX_FD_SET_BYTES > byte_size)
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_fd_set_decode(&rds, memory->data + read_ptr);
        rp = &rds;
    }
    if (write_ptr) {
        if ((uint64_t)write_ptr + POSIX_FD_SET_BYTES > byte_size)
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_fd_set_decode(&wrs, memory->data + write_ptr);
        wp = &wrs;
    }
    if (except_ptr) {
        if ((uint64_t)except_ptr + POSIX_FD_SET_BYTES > byte_size)
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_fd_set_decode(&exs, memory->data + except_ptr);
        ep = &exs;
    }

    /* Decode timeout. */
    const posix_timeval *tvp = (void *)0;
    posix_timeval tv;
    if (timeout_ptr) {
        if ((uint64_t)timeout_ptr + POSIX_TIMEVAL_BYTES > byte_size)
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_timeval_decode(&tv, memory->data + timeout_ptr);
        tvp = &tv;
    }

    int32_t ret = posix_kernel_select(store->kernel, nfds, rp, wp, ep, tvp);

    if (ret == -POSIX_EAGAIN) {
        error->yield_reason = EXEC_YIELD_SELECT;
        return EXEC_YIELD;
    }

    /* Encode output sets back to guest memory on success. */
    if (ret >= 0) {
        if (rp) posix_fd_set_encode(memory->data + read_ptr, rp);
        if (wp) posix_fd_set_encode(memory->data + write_ptr, wp);
        if (ep) posix_fd_set_encode(memory->data + except_ptr, ep);
    }

    return native_posix_result(ret, results, result_count);
}

static exec_status native_posix_pselect(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error,
                                        const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    if (arg_count != 6 ||
        native_posix_memory(caller, &memory, error) != EXEC_OK)
        return error->status;
    if (!store->kernel)
        return native_posix_result(-POSIX_EINVAL, results, result_count);

    int32_t nfds = args[0].i32;
    uint32_t read_ptr = (uint32_t)args[1].i32;
    uint32_t write_ptr = (uint32_t)args[2].i32;
    uint32_t except_ptr = (uint32_t)args[3].i32;
    uint32_t timeout_ptr = (uint32_t)args[4].i32;
    uint32_t sigmask_ptr = (uint32_t)args[5].i32;
    uint64_t byte_size = memory->pages * UINT64_C(65536);

    /* Validate and decode fd_sets. */
    posix_fd_set rds, wrs, exs;
    posix_fd_set *rp = (void *)0, *wp = (void *)0, *ep = (void *)0;
    if (read_ptr) {
        if ((uint64_t)read_ptr + POSIX_FD_SET_BYTES > byte_size)
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_fd_set_decode(&rds, memory->data + read_ptr);
        rp = &rds;
    }
    if (write_ptr) {
        if ((uint64_t)write_ptr + POSIX_FD_SET_BYTES > byte_size)
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_fd_set_decode(&wrs, memory->data + write_ptr);
        wp = &wrs;
    }
    if (except_ptr) {
        if ((uint64_t)except_ptr + POSIX_FD_SET_BYTES > byte_size)
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_fd_set_decode(&exs, memory->data + except_ptr);
        ep = &exs;
    }

    /* Decode timeout. */
    const posix_timespec *tsp = (void *)0;
    posix_timespec ts;
    if (timeout_ptr) {
        if ((uint64_t)timeout_ptr + POSIX_TIMESPEC_BYTES > byte_size)
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_timespec_decode(&ts, memory->data + timeout_ptr);
        tsp = &ts;
    }

    /* Decode and validate the optional temporary signal mask. */
    posix_sigset mask;
    const posix_sigset *mask_ptr = (void *)0;
    if (sigmask_ptr) {
        if ((uint64_t)sigmask_ptr + POSIX_SIGSET_BYTES > byte_size)
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_sigset_decode(&mask, memory->data + sigmask_ptr);
        mask_ptr = &mask;
    }

    int32_t ret = posix_kernel_pselect(store->kernel, nfds, rp, wp, ep,
                                       tsp, mask_ptr);

    /* A caught signal is delivered at the engine boundary, rather than via a
     * JavaScript callback.  The sigaction ABI stores a wasm function index in
     * its handler slot; invoke it with the POSIX signal number before the
     * interrupted pselect returns EINTR. */
    if (ret == -POSIX_EINTR) {
        int signal = posix_kernel_signal_last_delivered(store->kernel);
        posix_signal_disposition disposition;
        uint32_t handler = POSIX_SIG_DFL;
        if (signal > 0 &&
            posix_kernel_signal_get_disposition(store->kernel, signal,
                                                &disposition) == 0 &&
            disposition == POSIX_SIGNAL_HANDLER &&
            posix_kernel_signal_get_handler(store->kernel, signal, &handler) == 0 &&
            handler != POSIX_SIG_DFL && handler != POSIX_SIG_IGN) {
            wasm_value handler_arg;
            int handler_results = 0;
            exec_error handler_error;
            posix_sigset saved_mask;
            memset(&handler_error, 0, sizeof(handler_error));
            handler_arg.type = WASM_VALTYPE_I32;
            handler_arg.i32 = signal;
            if (posix_kernel_signal_enter_handler(store->kernel, signal,
                                                  &saved_mask) == 0) {
                (void)exec_invoke((waste_exec_engine *)caller, handler,
                                  &handler_arg, 1, NULL, &handler_results,
                                  &handler_error);
                posix_kernel_signal_leave_handler(store->kernel, &saved_mask);
            }
        }
    }

    if (ret == -POSIX_EAGAIN) {
        error->yield_reason = EXEC_YIELD_SELECT;
        return EXEC_YIELD;
    }

    /* Encode output sets back to guest memory on success. */
    if (ret >= 0) {
        if (rp) posix_fd_set_encode(memory->data + read_ptr, rp);
        if (wp) posix_fd_set_encode(memory->data + write_ptr, wp);
        if (ep) posix_fd_set_encode(memory->data + except_ptr, ep);
    }

    return native_posix_result(ret, results, result_count);
}

/* Return the active image's fixed-width startup block pointer.  This is the
 * only guest-facing way to discover argv/envp storage; the block itself stays
 * in the process image and never crosses the host boundary as a pointer. */
static exec_status native_posix_startup_v1(void *data, const wasm_value *args,
                                           int arg_count, wasm_value *results,
                                           int *result_count, exec_error *error,
                                           const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    native_process_capsule *capsule;
    (void)caller; (void)error;
    if (!store || arg_count != 0)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    capsule = native_store_active_capsule(store);
    if (!capsule || !capsule->image || capsule->image->startup_ptr == 0)
        return native_posix_result(-POSIX_ENOENT, results, result_count);
    return native_posix_result((int32_t)capsule->image->startup_ptr, results,
                               result_count);
}

/* ---- POSIX function dispatch tables ---- */

static exec_host_func native_posix_function(const char *module,
                                             const char *name) {
    if (strcmp(module, "waste_kernel") == 0) {
        if (strcmp(name, "startup_v1") == 0) return native_posix_startup_v1;
        if (strcmp(name, "select_v1") == 0) return native_posix_select;
        if (strcmp(name, "pselect_v1") == 0) return native_posix_pselect;
        if (strcmp(name, POSIX_KERNEL_PATH_ACCESS_V1) == 0)
            return native_posix_path_access_v1;
        if (strcmp(name, POSIX_KERNEL_PATH_STAT_V1) == 0)
            return native_posix_path_stat_v1;
        if (strcmp(name, "isatty_v1") == 0) return native_posix_isatty;
        if (strcmp(name, "tcgetattr_v1") == 0) return native_posix_tcgetattr;
        if (strcmp(name, "tcsetattr_v1") == 0) return native_posix_tcsetattr;
        if (strcmp(name, "tcflow_v1") == 0) return native_posix_tcflow;
        if (strcmp(name, "ioctl_v1") == 0) return native_posix_ioctl;
        return (void *)0;
    }
    if (strcmp(module, "env") != 0) return (void *)0;
    if (strcmp(name, "open") == 0) return native_posix_open;
    if (strcmp(name, "close") == 0) return native_posix_close;
    if (strcmp(name, "read") == 0) return native_posix_read;
    if (strcmp(name, "write") == 0) return native_posix_write;
    if (strcmp(name, "getcwd") == 0) return native_posix_getcwd;
    if (strcmp(name, "chdir") == 0) return native_posix_chdir;
    if (strcmp(name, "readdir_v1") == 0) return native_posix_readdir;
    if (strcmp(name, "getpgrp") == 0) return native_posix_getpgrp;
    if (strcmp(name, "tcgetpgrp") == 0) return native_posix_tcgetpgrp;
    if (strcmp(name, "isatty") == 0) return native_posix_isatty;
    if (strcmp(name, "tcflow") == 0) return native_posix_tcflow;
    if (strcmp(name, "time") == 0) return native_posix_i64_zero;
    if (strcmp(name, "exit") == 0) return native_posix_exit;
    if (strcmp(name, "raise") == 0) return native_posix_raise;
    if (strcmp(name, "kill") == 0) return native_posix_kill;
    if (strcmp(name, "killpg") == 0) return native_posix_killpg;
    if (strcmp(name, "abort") == 0 || strcmp(name, "siglongjmp") == 0)
        return native_posix_void;
    if (strcmp(name, "sigsetjmp") == 0 || strcmp(name, "alarm") == 0 ||
        strcmp(name, "sigemptyset") == 0 || strcmp(name, "sigaddset") == 0 ||
        strcmp(name, "sigdelset") == 0 || strcmp(name, "sigismember") == 0 ||
        strcmp(name, "sigprocmask") == 0 ||
        strcmp(name, "setitimer") == 0 || strcmp(name, "sleep") == 0 ||
        strcmp(name, "gettimeofday") == 0 || strcmp(name, "getrusage") == 0 ||
        strcmp(name, "dup") == 0 ||
        strcmp(name, "dup2") == 0 ||
        strcmp(name, "umask") == 0)
        return native_posix_i32_zero;
    if (strcmp(name, "fcntl") == 0) return native_posix_fcntl;
    if (strcmp(name, "setpgid") == 0) return native_posix_setpgid;
    if (strcmp(name, "tcsetpgrp") == 0) return native_posix_tcsetpgrp;
    if (strcmp(name, "tcgetattr") == 0) return native_posix_tcgetattr;
    if (strcmp(name, "tcsetattr") == 0) return native_posix_tcsetattr;
    if (strcmp(name, "ioctl") == 0) return native_posix_ioctl;
    if (strcmp(name, "sigaction") == 0) return native_posix_sigaction;
    if (strcmp(name, "getpid") == 0) return native_posix_getpid;
    if (strcmp(name, "getppid") == 0) return native_posix_getppid;
    if (strcmp(name, "access") == 0 || strcmp(name, "eaccess") == 0)
        return native_posix_access;
    if (strcmp(name, "faccessat") == 0) return native_posix_faccessat;
    if (strcmp(name, "stat") == 0) return native_posix_stat;
    if (strcmp(name, "lstat") == 0) return native_posix_lstat;
    if (strcmp(name, "fstat") == 0) return native_posix_fstat;
    if (strcmp(name, "mkdir") == 0) return native_posix_mkdir;
    if (strcmp(name, "unlink") == 0) return native_posix_unlink;
    if (strcmp(name, "rename") == 0) return native_posix_rename;
    if (strcmp(name, "rmdir") == 0) return native_posix_rmdir;
    if (strcmp(name, "readlink") == 0) return native_posix_readlink;
    if (strcmp(name, "lseek") == 0) return native_posix_lseek;
    if (strcmp(name, "fork") == 0) return native_posix_fork;
    if (strcmp(name, "pipe") == 0 ||
        strcmp(name, "getgroups") == 0 ||
        strcmp(name, "confstr") == 0 || strcmp(name, "fchmod") == 0 ||
        strcmp(name, "rmdir") == 0)
        return native_posix_i32_negative;
    if (strcmp(name, "waitpid") == 0) return native_posix_waitpid;
    if (strcmp(name, "execve") == 0) return native_posix_execve;
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
