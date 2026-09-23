#include "store.h"
#include "runtime_internal.h"
#include "wasm/encode.h"
#include "wast/runner.h"
#include "source.h"
#include "wasm/decode.h"
#include "instantiate.h"
#include "lib/include/kernel.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void native_exec_request_init(native_exec_request *request) {
    if (request) memset(request, 0, sizeof(*request));
}

void native_exec_request_destroy(native_exec_request *request) {
    if (!request) return;
    for (uint32_t i = 0; i < request->argc; i++) free(request->argv[i]);
    for (uint32_t i = 0; i < request->envc; i++) free(request->envp[i]);
    free(request->handler_bytes);
    memset(request, 0, sizeof(*request));
}

int native_exec_request_take_handler(native_exec_request *request,
                                     native_exec_handler_kind kind,
                                     uint8_t **bytes_out, size_t *size_out) {
    if (!request || !bytes_out || !size_out || request->handler_kind != kind ||
        request->handler_size == 0 ||
        request->handler_size > NATIVE_EXEC_BYTES_MAX)
        return -POSIX_EINVAL;
    *bytes_out = request->handler_bytes;
    *size_out = request->handler_size;
    request->handler_bytes = NULL;
    request->handler_size = 0;
    request->handler_kind = NATIVE_EXEC_HANDLER_NONE;
    return 0;
}

int native_store_commit_process_handler_with_context(
        native_store *store, native_exec_request *request,
        native_process_handler_kind kind, void *context,
        native_process_handler_context_destroy destroy_context) {
    native_process_capsule *capsule = native_store_active_capsule(store);
    native_process_image *old_image;
    uint8_t *source = NULL;
    size_t source_size = 0;
    if (!store || !capsule || !request || !request->active ||
        capsule->pending_transition != NATIVE_PROCESS_TRANSITION_EXEC ||
        kind != NATIVE_PROCESS_HANDLER_WAST ||
        request->handler_kind != NATIVE_EXEC_HANDLER_WAST ||
        !request->handler_bytes || request->handler_size == 0 ||
        request->handler_size > NATIVE_EXEC_BYTES_MAX ||
        capsule->handler.kind != NATIVE_PROCESS_HANDLER_NONE ||
        capsule->pending_result_valid)
        return -POSIX_EINVAL;
    if (native_exec_request_take_handler(
            request, NATIVE_EXEC_HANDLER_WAST, &source, &source_size) != 0)
        return -POSIX_EINVAL;
    if (native_process_capsule_install_handler(
            capsule, kind, source, source_size, context, destroy_context) != 0) {
        free(source);
        return -POSIX_EBUSY;
    }
    posix_kernel_close_on_exec(store->kernel);
    old_image = capsule->image;
    native_process_image_release(old_image);
    capsule->image = NULL;
    capsule->engine = NULL;
    capsule->root_func_idx = 0;
    capsule->root_arg_count = 0;
    memset(capsule->root_args, 0, sizeof(capsule->root_args));
    capsule->generation++;
    capsule->state = NATIVE_PROCESS_RUNNABLE;
    capsule->pending_transition = NATIVE_PROCESS_TRANSITION_NONE;
    return 0;
}

int native_store_commit_process_handler(native_store *store,
                                        native_exec_request *request,
                                        native_process_handler_kind kind) {
    return native_store_commit_process_handler_with_context(
        store, request, kind, NULL, NULL);
}

void native_process_image_init(native_process_image *image) {
    if (image) memset(image, 0, sizeof(*image));
}

static void native_process_image_dispose_startup(native_process_image *image) {
    if (!image) return;
    for (uint32_t i = 0; i < image->argc; i++) free(image->argv[i]);
    for (uint32_t i = 0; i < image->envc; i++) free(image->envp[i]);
    image->argc = 0;
    image->envc = 0;
}

static char *native_process_image_strdup(const char *value) {
    size_t length;
    char *copy;
    if (!value) return NULL;
    length = strlen(value) + 1;
    copy = (char *)malloc(length);
    if (copy) memcpy(copy, value, length);
    return copy;
}

static void native_store_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static char *native_store_strdup(const char *value) {
    size_t length;
    char *copy;
    if (!value) return NULL;
    length = strlen(value) + 1u;
    copy = (char *)malloc(length);
    if (copy) memcpy(copy, value, length);
    return copy;
}

static int native_path_has_suffix(const char *path, const char *suffix) {
    size_t path_length;
    size_t suffix_length;
    if (!path || !suffix) return 0;
    path_length = strlen(path);
    suffix_length = strlen(suffix);
    return path_length >= suffix_length &&
           strcmp(path + path_length - suffix_length, suffix) == 0;
}

/* Materialize the optional __waste_startup(i32) block at the top of the
 * process image's memory.  The block is deliberately self-describing through
 * fixed offsets so a libc shim can consume it without host pointers. */
static exec_status native_process_image_startup_block(
        native_process_image *image, const native_exec_request *request,
        native_store *store, exec_error *error) {
    uint32_t hook;
    uint32_t type_index;
    exec_memory *memory = image->engine ? image->engine->memory : NULL;
    size_t vector_bytes, string_bytes, total;
    uint32_t base, argv_ptr, envp_ptr, cursor;
    exec_func_type *type;
    wasm_value argument;
    int result_count = 0;
    exec_error hook_error;
    int has_hook = exec_find_export(image->engine, "__waste_startup", &hook,
                                    error) == EXEC_OK;
    if (!memory || memory->pages == 0)
        return exec_fail(error, EXEC_ERROR_FORMAT,
                         "startup hook requires linear memory");
    if (has_hook && (exec_get_func_type_index(image->engine, hook, &type_index, error) != EXEC_OK ||
        type_index >= image->engine->type_count))
        return EXEC_ERROR_FORMAT;
    if (has_hook) {
        type = &image->engine->types[type_index];
        if (type->param_count != 1 || type->params[0] != WASM_VALTYPE_I32 ||
            type->result_count != 0)
            return exec_fail(error, EXEC_ERROR_FORMAT,
                             "__waste_startup must have type (i32) -> ()");
    }
    vector_bytes = ((size_t)request->argc + 1u +
                    (size_t)request->envc + 1u) * sizeof(uint32_t);
    string_bytes = strlen(image->cwd) + 1u;
    for (uint32_t i = 0; i < request->argc; i++) string_bytes += strlen(request->argv[i]) + 1u;
    for (uint32_t i = 0; i < request->envc; i++) string_bytes += strlen(request->envp[i]) + 1u;
    total = 44u + vector_bytes + string_bytes;
    if (total > memory->pages * (size_t)EXEC_PAGE_SIZE || total > UINT32_MAX)
        return exec_fail(error, EXEC_ERROR_TRAP, "startup block exceeds memory");
    base = (uint32_t)(memory->pages * (size_t)EXEC_PAGE_SIZE - total);
    argv_ptr = base + 44u;
    envp_ptr = argv_ptr + (request->argc + 1u) * 4u;
    cursor = envp_ptr + (request->envc + 1u) * 4u;
    native_store_u32(memory->data + base + 0, request->argc);
    native_store_u32(memory->data + base + 4, argv_ptr);
    native_store_u32(memory->data + base + 8, request->envc);
    native_store_u32(memory->data + base + 12, envp_ptr);
    native_store_u32(memory->data + base + 16, (uint32_t)image->pid);
    native_store_u32(memory->data + base + 20, cursor);
    for (uint32_t i = 0; i < request->argc; i++) {
        native_store_u32(memory->data + argv_ptr + i * 4u, cursor);
        size_t length = strlen(request->argv[i]) + 1u;
        memcpy(memory->data + cursor, request->argv[i], length);
        cursor += (uint32_t)length;
    }
    native_store_u32(memory->data + argv_ptr + request->argc * 4u, 0);
    for (uint32_t i = 0; i < request->envc; i++) {
        native_store_u32(memory->data + envp_ptr + i * 4u, cursor);
        size_t length = strlen(request->envp[i]) + 1u;
        memcpy(memory->data + cursor, request->envp[i], length);
        cursor += (uint32_t)length;
    }
    native_store_u32(memory->data + envp_ptr + request->envc * 4u, 0);
    memcpy(memory->data + cursor, image->cwd, strlen(image->cwd) + 1u);
    image->startup_ptr = base;
    image->startup_size = (uint32_t)total;
    if (has_hook) {
        argument.type = WASM_VALTYPE_I32;
        argument.i32 = (int32_t)base;
        memset(&hook_error, 0, sizeof(hook_error));
        if (exec_invoke(image->engine, hook, &argument, 1, NULL, &result_count,
                        &hook_error) != EXEC_OK)
            return exec_fail(error, EXEC_ERROR_TRAP, "startup hook failed");
    }
    (void)store;
    return EXEC_OK;
}

void native_process_image_retain(native_process_image *image) {
    if (image) image->references++;
}

void native_process_image_pin(native_process_image *image) {
    if (image) image->checkpoint_pins++;
}

void native_process_image_unpin(native_process_image *image) {
    if (!image || image->checkpoint_pins == 0) return;
    image->checkpoint_pins--;
    if (image->checkpoint_pins == 0 && image->references == 0) {
        native_process_image_dispose_startup(image);
        exec_free(image->engine);
        free(image);
    }
}

void native_process_image_release(native_process_image *image) {
    if (!image || image->references == 0) return;
    image->references--;
    if (image->references == 0 && image->checkpoint_pins == 0) {
        native_process_image_dispose_startup(image);
        exec_free(image->engine);
        free(image);
    }
}

static int native_executable_path_valid(const char *path) {
    size_t length;
    if (!path || path[0] != '/') return 0;
    length = strlen(path);
    if (length == 0 || length >= NATIVE_EXEC_PATH_MAX) return 0;
    if (strcmp(path, "/") == 0) return 0;
    for (size_t i = 1; path[i]; i++)
        if (path[i] == '/' && path[i - 1] == '/') return 0;
    return 1;
}

static int native_executable_import_allowed(const wasm_import *import) {
    if (!import) return 0;
    /* The first external-image ABI deliberately has one tiny escape hatch for
     * the probe. Production libc/kernel images use versioned waste_kernel
     * imports and are expanded only when their ABI entries are implemented. */
    if (strcmp(import->module, "env") == 0 &&
        (strcmp(import->name, "write") == 0 ||
         strcmp(import->name, "exit") == 0 ||
         strcmp(import->name, "fcntl") == 0 ||
         strcmp(import->name, "memory") == 0))
        return 1;
    if (strcmp(import->module, "waste_kernel") == 0) return 1;
    return 0;
}

static int native_executable_has_asyncify_name(const char *name) {
    const char *needle = "asyncify";
    if (!name) return 0;
    for (size_t i = 0; name[i]; i++) {
        size_t j = 0;
        while (needle[j] && name[i + j] == needle[j]) j++;
        if (!needle[j]) return 1;
    }
    return 0;
}

const native_executable *native_store_find_executable(
        const native_store *store, const char *path) {
    if (!store || !native_executable_path_valid(path)) return NULL;
    for (uint32_t i = 0; i < store->executable_count; i++)
        if (strcmp(store->executables[i].path, path) == 0)
            return &store->executables[i];
    return NULL;
}

int native_store_bind_executable_paths(native_store *store) {
    if (!store || !store->kernel) return -POSIX_EINVAL;
    for (uint32_t i = 0; i < store->executable_count; i++) {
        const native_executable *executable = &store->executables[i];
        posix_path_metadata metadata = {
            POSIX_NODE_REGULAR, executable->mode, 0, 0,
            (int64_t)executable->size, (uint64_t)(3u + i)
        };
        int status = posix_kernel_path_add_data(
            store->kernel, executable->path, &metadata, executable->bytes,
            executable->size);
        if (status != 0) return status;
    }
    return 0;
}

int native_store_bind_interpreter_paths(native_store *store) {
    if (!store || !store->kernel) return -POSIX_EINVAL;
    const posix_path_metadata metadata = {
        POSIX_NODE_REGULAR, 0755u, 0, 0, 0, 0
    };
    int status = posix_kernel_path_add_data(
        store->kernel, "/bin/wat", &metadata, NULL, 0);
    if (status != 0) return status;
    return posix_kernel_path_add_data(
        store->kernel, "/bin/wast", &metadata, NULL, 0);
}

int native_store_register_executable(native_store *store, const char *path,
                                     const uint8_t *bytes, size_t size,
                                     uint32_t mode, uint32_t abi_version,
                                     const char *entry) {
    native_executable *next;
    int bind_status;
    size_t path_length, entry_length;
    wasm_module decoded;
    wasm_decode_error decode_error;
    if (!store || !native_executable_path_valid(path) || !bytes || size == 0 ||
        size > NATIVE_EXEC_BYTES_MAX || !(mode & 0111u) || !entry ||
        strcmp(entry, "_start") != 0 || abi_version != 1)
        return -POSIX_EINVAL;
    wasm_module_init(&decoded);
    if (wasm_decode_module(bytes, size, &decoded, &decode_error) != WASM_DECODE_OK) {
        wasm_module_dispose(&decoded);
        return -POSIX_ENOEXEC;
    }
    for (uint32_t i = 0; i < decoded.section_count; i++) {
        if (decoded.sections[i].id == 8) {
            wasm_module_dispose(&decoded);
            return -POSIX_EINVAL;
        }
    }
    for (uint32_t i = 0; i < decoded.import_count; i++) {
        if (native_executable_has_asyncify_name(decoded.imports[i].module) ||
            native_executable_has_asyncify_name(decoded.imports[i].name) ||
            !native_executable_import_allowed(&decoded.imports[i])) {
            wasm_module_dispose(&decoded);
            return -POSIX_ENOSYS;
        }
    }
    wasm_module_dispose(&decoded);
    if (native_store_find_executable(store, path)) return -POSIX_EEXIST;
    path_length = strlen(path);
    entry_length = strlen(entry);
    if (entry_length >= WAST_MAX_EXPORT_NAME) return -POSIX_EINVAL;
    if (store->executable_count == store->executable_capacity) {
        uint32_t capacity = store->executable_capacity ?
                            store->executable_capacity * 2u : 8u;
        next = realloc(store->executables,
                       (size_t)capacity * sizeof(*next));
        if (!next) return -POSIX_ENOMEM;
        store->executables = next;
        store->executable_capacity = capacity;
    }
    native_executable *executable =
        &store->executables[store->executable_count];
    memset(executable, 0, sizeof(*executable));
    executable->bytes = malloc(size);
    if (!executable->bytes) return -POSIX_ENOMEM;
    memcpy(executable->bytes, bytes, size);
    memcpy(executable->path, path, path_length + 1);
    memcpy(executable->entry, entry, entry_length + 1);
    executable->size = size;
    executable->mode = mode;
    executable->abi_version = abi_version;
    executable->validated = 1;
    store->executable_count++;
    bind_status = store->kernel ? native_store_bind_executable_paths(store) : 0;
    if (bind_status != 0) {
        store->executable_count--;
        free(executable->bytes);
        memset(executable, 0, sizeof(*executable));
        return bind_status;
    }
    return 0;
}

exec_status native_store_instantiate_executable(
        native_store *store, native_exec_request *request,
        native_process_image **image_out, exec_error *error) {
    const native_executable *executable;
    native_executable vfs_executable;
    posix_path_metadata vfs_metadata;
    uint8_t *vfs_bytes = NULL;
    size_t vfs_size = 0;
    int vfs_status = -POSIX_ENOENT;
    native_process_image *image;
    waste_exec_engine *engine = NULL;
    uint32_t entry_func = 0;
    exec_status status;
    const uint8_t *image_bytes = NULL;
    size_t image_size = 0;
    uint8_t *compiled_bytes = NULL;
    waste_source_view source_view;
    waste_source_result source_result;
    char text_error[256] = {0};
    native_exec_request effective_request;
    int effective_request_owned = 0;
    uint32_t effective_owned_count = 0;
    const char *load_path = request ? request->path : NULL;
    int wat_handler = request && strcmp(request->path, "/bin/wat") == 0;
    int wast_handler = request && strcmp(request->path, "/bin/wast") == 0;
    if (!store || !request || !image_out || !request->active) {
        if (error) {
            error->status = EXEC_ERROR_FORMAT;
            snprintf(error->message, sizeof(error->message),
                     "invalid executable request");
        }
        return EXEC_ERROR_FORMAT;
    }
    *image_out = NULL;
    if (wat_handler || wast_handler) {
        if (request->argc < 2 || !request->argv[1] ||
            strlen(request->argv[1]) >= NATIVE_EXEC_PATH_MAX)
            return exec_fail(error, EXEC_ERROR_FORMAT,
                             "/bin/wat requires a source path");
        load_path = request->argv[1];
    }
    memset(&vfs_executable, 0, sizeof(vfs_executable));
    if (store->kernel) {
        vfs_status = posix_kernel_path_snapshot(
            store->kernel, (const uint8_t *)load_path,
            strlen(load_path), NATIVE_EXEC_BYTES_MAX, &vfs_bytes,
            &vfs_size, &vfs_metadata);
    }
    if (vfs_status == 0) {
        snprintf(vfs_executable.path, sizeof(vfs_executable.path), "%s",
                 load_path);
        vfs_executable.bytes = vfs_bytes;
        vfs_executable.size = vfs_size;
        vfs_executable.mode = vfs_metadata.mode;
        vfs_executable.abi_version = 1;
        snprintf(vfs_executable.entry, sizeof(vfs_executable.entry), "%s",
                 "_start");
        vfs_executable.validated = 1;
        executable = &vfs_executable;
    } else if (vfs_status == -POSIX_ENOENT) {
        /* Compatibility path for focused lifecycle fixtures that register an
         * image without constructing a VFS namespace. Production browser
         * images are always loaded from the VFS snapshot above. */
        executable = (wat_handler || wast_handler) ? NULL :
            native_store_find_executable(store, request->path);
    } else if (vfs_status == -POSIX_EISDIR ||
               vfs_status == -POSIX_EACCES ||
               vfs_status == -POSIX_ELOOP ||
               vfs_status == -POSIX_ENOTDIR) {
        return exec_fail(error, EXEC_ERROR_NOT_FOUND,
                         "VFS path is not an executable image");
    } else {
        return exec_fail(error, EXEC_ERROR_FORMAT,
                         "VFS executable snapshot failed");
    }
    if (!executable || !executable->validated)
        return exec_fail(error, EXEC_ERROR_NOT_FOUND,
                         "executable is not present in the VFS");

    image_bytes = executable->bytes;
    image_size = executable->size;
    source_result = waste_source_view_init((const char *)image_bytes,
                                           image_size, &source_view);
    int is_wat = wat_handler || native_path_has_suffix(executable->path, ".wat");
    int is_wast = wast_handler || native_path_has_suffix(executable->path, ".wast");
    if (source_result == WASTE_SOURCE_OK && !wat_handler && !wast_handler) {
        is_wat = strcmp(source_view.interpreter, "/bin/wat") == 0;
        is_wast = strcmp(source_view.interpreter, "/bin/wast") == 0;
    }
    if (source_result == WASTE_SOURCE_INVALID_SHEBANG ||
        source_result == WASTE_SOURCE_SHEBANG_TOO_LONG) {
        free(vfs_bytes);
        return exec_fail(error, EXEC_ERROR_FORMAT,
                         source_result == WASTE_SOURCE_SHEBANG_TOO_LONG ?
                         "shebang line exceeds loader limit" :
                         "invalid shebang line");
    }
    if (source_result == WASTE_SOURCE_OK &&
        strcmp(source_view.interpreter, "/bin/wat") == 0) {
        if (request->interpreter_depth >= NATIVE_EXEC_INTERPRETER_MAX) {
            free(vfs_bytes);
            return exec_fail(error, EXEC_ERROR_FORMAT,
                             "interpreter recursion limit exceeded");
        }
        if (store->kernel && posix_kernel_path_access(
                store->kernel, (const uint8_t *)"/bin/wat", 8,
                POSIX_X_OK, 0) != 0) {
            free(vfs_bytes);
            return exec_fail(error, EXEC_ERROR_NOT_FOUND,
                             "WAT interpreter is not executable");
        }
    }
    if (is_wast && (image_size < 4 || memcmp(image_bytes, "\0asm", 4) != 0)) {
        wast_script script;
        int parse_status = wast_parse_bytes((const char *)image_bytes,
                                             image_size, &script);
        if (parse_status != 0) {
            snprintf(text_error, sizeof(text_error), "%s",
                     script.error[0] ? script.error : "WAST parse failed");
            wast_script_free(&script);
            free(vfs_bytes);
            return exec_fail(error, EXEC_ERROR_FORMAT, text_error);
        }
        wast_script_free(&script);
        free(request->handler_bytes);
        request->handler_bytes = vfs_bytes;
        request->handler_size = vfs_size;
        request->handler_kind = NATIVE_EXEC_HANDLER_WAST;
        vfs_bytes = NULL;
        return exec_fail(error, EXEC_ERROR_UNSUPPORTED,
                         "WAST process handler is not installed");
    }
    if (is_wat && (image_size < 4 || memcmp(image_bytes, "\0asm", 4) != 0)) {
        if (waste_wat_compile((const char *)image_bytes, image_size,
                              &compiled_bytes, &image_size, text_error,
                              sizeof(text_error)) != 0) {
            free(vfs_bytes);
            return exec_fail(error, EXEC_ERROR_FORMAT,
                             text_error[0] ? text_error : "WAT compilation failed");
        }
        image_bytes = compiled_bytes;
    } else if (image_size < 4 || memcmp(image_bytes, "\0asm", 4) != 0) {
        free(vfs_bytes);
        return exec_fail(error, EXEC_ERROR_FORMAT,
                         "executable is neither Wasm nor WAT");
    }
    status = native_load_module(store, NULL, image_bytes, image_size,
                                &engine, error);
    free(compiled_bytes);
    free(vfs_bytes);
    vfs_bytes = NULL;
    if (status != EXEC_OK) return status;

    native_exec_request_init(&effective_request);
    effective_request = *request;
    if (source_result == WASTE_SOURCE_OK &&
        strcmp(source_view.interpreter, "/bin/wat") == 0) {
        uint32_t argument_count = request->argc;
        uint32_t shebang_argument = source_view.argument[0] ? 1u : 0u;
        if (argument_count + 1u + shebang_argument >= NATIVE_EXEC_ARG_MAX) {
            exec_free(engine);
            return exec_fail(error, EXEC_ERROR_FORMAT,
                             "WAT interpreter argument vector is too large");
        }
        if (wat_handler) {
            effective_request.argv[0] = native_store_strdup(load_path);
            if (!effective_request.argv[0]) {
                exec_free(engine);
                return exec_fail(error, EXEC_ERROR_TRAP,
                                 "WAT interpreter argv allocation failed");
            }
            for (uint32_t i = 2; i < argument_count; i++)
                effective_request.argv[i - 1u] = request->argv[i];
            effective_request.argc = argument_count > 1u ? argument_count - 1u : 1u;
            effective_owned_count = 1;
        } else {
            effective_request.argv[0] = native_store_strdup("/bin/wat");
            if (shebang_argument)
                effective_request.argv[1] =
                    native_store_strdup(source_view.argument);
            effective_request.argv[1u + shebang_argument] =
                native_store_strdup(request->path);
            if (!effective_request.argv[0] ||
                !effective_request.argv[1u + shebang_argument] ||
                (shebang_argument && !effective_request.argv[1])) {
                free(effective_request.argv[0]);
                free(effective_request.argv[1]);
                if (shebang_argument) free(effective_request.argv[2]);
                exec_free(engine);
                return exec_fail(error, EXEC_ERROR_TRAP,
                                 "WAT interpreter argv allocation failed");
            }
            for (uint32_t i = 1; i < argument_count; i++)
                effective_request.argv[i + 1u + shebang_argument] =
                    request->argv[i];
            effective_request.argc = argument_count ?
                argument_count + 1u + shebang_argument :
                2u + shebang_argument;
            effective_owned_count = 2u + shebang_argument;
        }
        effective_request.interpreter_depth = request->interpreter_depth + 1u;
        effective_request_owned = 1;
    }
    const native_exec_request *startup_request =
        effective_request_owned ? &effective_request : request;
    status = exec_find_export(engine, executable->entry, &entry_func, error);
    if (status != EXEC_OK) {
        if (effective_request_owned) {
            for (uint32_t i = 0; i < effective_owned_count; i++)
                free(effective_request.argv[i]);
        }
        exec_free(engine);
        return status;
    }
    image = (native_process_image *)calloc(1, sizeof(*image));
    if (!image) {
        if (effective_request_owned) {
            for (uint32_t i = 0; i < effective_owned_count; i++)
                free(effective_request.argv[i]);
        }
        exec_free(engine);
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "executable image allocation failed");
    }
    image->engine = engine;
    image->entry_func = entry_func;
    image->references = 1;
    snprintf(image->path, sizeof(image->path), "%s", executable->path);
    image->pid = startup_request->pid;
    if (store->kernel) {
        posix_path_metadata cwd_metadata;
        if (posix_kernel_path_stat(store->kernel,
                                   (const uint8_t *)store->kernel->cwd,
                                   strlen(store->kernel->cwd), 1,
                                   &cwd_metadata) != 0 ||
            cwd_metadata.kind != POSIX_NODE_DIRECTORY)
            (void)posix_kernel_path_set_cwd(store->kernel, "/");
        snprintf(image->cwd, sizeof(image->cwd), "%s", store->kernel->cwd);
    }
    for (uint32_t i = 0; i < startup_request->argc; i++) {
        image->argv[i] = native_process_image_strdup(startup_request->argv[i]);
        if (!image->argv[i]) {
            if (effective_request_owned) {
                for (uint32_t j = 0; j < effective_owned_count; j++)
                    free(effective_request.argv[j]);
            }
            native_process_image_dispose_startup(image);
            exec_free(image->engine);
            free(image);
            return exec_fail(error, EXEC_ERROR_TRAP,
                             "executable argv allocation failed");
        }
        image->argc++;
    }
    for (uint32_t i = 0; i < startup_request->envc; i++) {
        image->envp[i] = native_process_image_strdup(startup_request->envp[i]);
        if (!image->envp[i]) {
            if (effective_request_owned) {
                for (uint32_t j = 0; j < effective_owned_count; j++)
                    free(effective_request.argv[j]);
            }
            native_process_image_dispose_startup(image);
            exec_free(image->engine);
            free(image);
            return exec_fail(error, EXEC_ERROR_TRAP,
                             "executable environment allocation failed");
        }
        image->envc++;
    }
    status = native_process_image_startup_block(image, startup_request, store, error);
    if (effective_request_owned) {
        for (uint32_t i = 0; i < effective_owned_count; i++)
            free(effective_request.argv[i]);
    }
    if (status != EXEC_OK) {
        native_process_image_dispose_startup(image);
        exec_free(image->engine);
        free(image);
        return status;
    }
    *image_out = image;
    return EXEC_OK;
}

/* ---- cross-module call trampoline ---- */

static exec_status native_linked_call(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    (void)caller;
    native_linked_func *function = (native_linked_func *)data;
    return exec_invoke(function->engine, function->func_idx, args, arg_count,
                       results, result_count, error);
}

/* ---- spectest helpers ---- */

static exec_status native_spectest_noop(void *data, const wasm_value *args,
                                         int arg_count, wasm_value *results,
                                         int *result_count, exec_error *error,
                                         const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)results; (void)error;
    (void)caller;
    *result_count = 0;
    return EXEC_OK;
}

static int native_spectest_has_function(const char *name) {
    return strcmp(name, "print") == 0 ||
           strcmp(name, "print_i32") == 0 ||
           strcmp(name, "print_i64") == 0 ||
           strcmp(name, "print_f32") == 0 ||
           strcmp(name, "print_f64") == 0 ||
           strcmp(name, "print_i32_f32") == 0 ||
           strcmp(name, "print_f64_f64") == 0;
}

static exec_global *native_spectest_global(native_store *store,
                                            const char *name) {
    if (strcmp(name, "global_i32") == 0) return &store->spectest_i32;
    if (strcmp(name, "global_i64") == 0) return &store->spectest_i64;
    if (strcmp(name, "global_f32") == 0) return &store->spectest_f32;
    if (strcmp(name, "global_f64") == 0) return &store->spectest_f64;
    return NULL;
}

/* ---- store management ---- */

void native_store_init(native_store *store) {
    memset(store, 0, sizeof(*store));
    native_exec_request_init(&store->exec_request);
    store->spectest_memory.pages = 1;
    store->spectest_memory.max_pages = 2;
    store->spectest_memory.has_max = 1;
    store->spectest_memory.data = calloc(EXEC_PAGE_SIZE, 1);
    store->spectest_table.size = 10;
    store->spectest_table.max_size = 20;
    store->spectest_table.has_max = 1;
    store->spectest_table.element_type = WASM_VALTYPE_FUNCREF;
    store->spectest_table.elements = calloc(10, sizeof(exec_table_element));
    store->spectest_i32.value.type = WASM_VALTYPE_I32;
    store->spectest_i32.value.i32 = 666;
    store->spectest_i64.value.type = WASM_VALTYPE_I64;
    store->spectest_i64.value.i64 = 666;
    store->spectest_f32.value.type = WASM_VALTYPE_F32;
    store->spectest_f32.value.f32 = 666.6f;
    store->spectest_f64.value.type = WASM_VALTYPE_F64;
    store->spectest_f64.value.f64 = 666.6;
    store->kernel = posix_kernel_create(0); /* noninteractive by default */
    if (store->kernel) {
        (void)native_store_bind_interpreter_paths(store);
        store->processes[0].used = 1;
        store->processes[0].pid = 1;
        store->processes[0].ppid = 0;
        store->processes[0].kernel = store->kernel;
        native_process_capsule_init(&store->processes[0].capsule);
        store->process_count = 1;
        store->active_pid = 1;
        store->next_pid = 2;
    }
}

void native_store_free(native_store *store) {
    native_exec_request_destroy(&store->exec_request);
    for (uint32_t i = 0; i < store->executable_count; i++)
        free(store->executables[i].bytes);
    free(store->executables);
    for (int i = store->module_count; i > 0; i--)
        exec_free(store->modules[i - 1].engine);
    for (int i = store->orphan_count; i > 0; i--)
        exec_free(store->orphan_engines[i - 1]);
    native_call_block *block = store->call_blocks;
    while (block) {
        native_call_block *next = block->next;
        free(block->calls);
        free(block);
        block = next;
    }
    free(store->modules);
    free(store->orphan_engines);
    free(store->spectest_memory.data);
    free(store->spectest_table.elements);
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++)
        if (store->processes[i].used) {
            native_process_capsule_destroy(&store->processes[i].capsule);
            posix_kernel_destroy(store->processes[i].kernel);
            store->processes[i].kernel = NULL;
        }
    store->kernel = NULL;
    memset(store, 0, sizeof(*store));
}

void native_store_enable_terminal(native_store *store) {
    if (!store) return;
    if (store->kernel) posix_kernel_destroy(store->kernel);
    store->kernel = posix_kernel_create(1);
    store->kernel_terminal = store->kernel != NULL;
    if (store->kernel) {
        posix_termios termios;
        if (posix_kernel_tcgetattr(store->kernel, 0, &termios) == 0) {
            termios.iflag |= POSIX_TERMIOS_IFLAG_ICRNL;
            termios.oflag |= POSIX_TERMIOS_OFLAG_OPOST |
                             POSIX_TERMIOS_OFLAG_ONLCR;
            termios.lflag |= POSIX_TERMIOS_LFLAG_ISIG |
                             POSIX_TERMIOS_LFLAG_ICANON |
                             POSIX_TERMIOS_LFLAG_ECHO |
                             POSIX_TERMIOS_LFLAG_IEXTEN;
            (void)posix_kernel_tcsetattr(store->kernel, 0, &termios);
        }
        (void)native_store_bind_executable_paths(store);
        (void)native_store_bind_interpreter_paths(store);
    }
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++)
        if (store->processes[i].used && store->processes[i].pid == store->active_pid)
            store->processes[i].kernel = store->kernel;
}

int native_store_keep_orphan(native_store *store,
                              waste_exec_engine *engine) {
    if (store->orphan_count == store->orphan_capacity) {
        int capacity = store->orphan_capacity ?
                       store->orphan_capacity * 2 : 16;
        waste_exec_engine **engines = realloc(
            store->orphan_engines, (size_t)capacity * sizeof(*engines));
        if (!engines) return 0;
        store->orphan_engines = engines;
        store->orphan_capacity = capacity;
    }
    store->orphan_engines[store->orphan_count++] = engine;
    return 1;
}

native_linked_module *native_registered_module(native_store *store,
                                                const char *name) {
    for (int i = store->module_count; i > 0; i--)
        if (strcmp(store->modules[i - 1].registered, name) == 0)
            return &store->modules[i - 1];
    return NULL;
}

waste_exec_engine *native_selected_engine(native_store *store,
                                           const char *id) {
    if (!id || !id[0])
        return store->module_count ?
               store->modules[store->module_count - 1].engine : NULL;
    for (int i = store->module_count; i > 0; i--)
        if (strcmp(store->modules[i - 1].id, id) == 0)
            return store->modules[i - 1].engine;
    return NULL;
}

native_linked_module *native_selected_module(native_store *store,
                                              const char *id) {
    if (!id || !id[0])
        return store->module_count ?
               &store->modules[store->module_count - 1] : NULL;
    for (int i = store->module_count; i > 0; i--)
        if (strcmp(store->modules[i - 1].id, id) == 0)
            return &store->modules[i - 1];
    return NULL;
}

int native_store_add(native_store *store, waste_exec_engine *engine,
                     const wast_module *identity,
                     const wast_module *metadata) {
    if (store->module_count == store->module_capacity) {
        int next_capacity = store->module_capacity ?
                            store->module_capacity * 2 : 16;
        native_linked_module *next = realloc(
            store->modules, (size_t)next_capacity * sizeof(*next));
        if (!next) return 0;
        store->modules = next;
        store->module_capacity = next_capacity;
    }
    native_linked_module *linked = &store->modules[store->module_count++];
    memset(linked, 0, sizeof(*linked));
    linked->engine = engine;
    linked->module = metadata;
    snprintf(linked->id, sizeof(linked->id), "%s", identity->id);
    snprintf(linked->registered, sizeof(linked->registered), "%s",
             identity->register_name);
    return 1;
}

const wast_module *native_find_definition(const wast_script *script,
                                           int before_group,
                                           const char *id) {
    for (int i = before_group - 1; i >= 0; i--) {
        const wast_module *module = &script->groups[i].module;
        if (module->is_definition && strcmp(module->id, id) == 0)
            return module;
    }
    return NULL;
}

/* ---- module loading ---- */

exec_status native_load_module(native_store *store,
                                const wast_module *module,
                                const uint8_t *bytes, size_t size,
                                waste_exec_engine **engine_out,
                                exec_error *error) {
    wasm_module decoded;
    wasm_decode_error decode_error;
    size_t function_count = 0, global_count = 0;
    size_t memory_count = 0, table_count = 0, tag_count = 0;
    const wasm_import *decoded_imports;
    uint32_t decoded_import_count;
    exec_status load_status = EXEC_OK;

    (void)module;
    if (!store || !engine_out) {
        if (error) {
            error->status = EXEC_ERROR_FORMAT;
            snprintf(error->message, sizeof(error->message),
                     "invalid linker input");
        }
        return EXEC_ERROR_FORMAT;
    }
    *engine_out = NULL;
    if (wasm_decode_module(bytes, size, &decoded, &decode_error) !=
        WASM_DECODE_OK) {
        if (error) {
            error->status = EXEC_ERROR_FORMAT;
            snprintf(error->message, sizeof(error->message), "%s at byte %zu",
                     decode_error.message, decode_error.offset);
        }
        return EXEC_ERROR_FORMAT;
    }
    decoded_imports = decoded.imports;
    decoded_import_count = decoded.import_count;
    for (uint32_t i = 0; i < decoded_import_count; i++) {
        function_count += decoded_imports[i].kind == WASM_IMPORT_FUNCTION;
        table_count += decoded_imports[i].kind == WASM_IMPORT_TABLE;
        memory_count += decoded_imports[i].kind == WASM_IMPORT_MEMORY;
        global_count += decoded_imports[i].kind == WASM_IMPORT_GLOBAL;
        tag_count += decoded_imports[i].kind == WASM_IMPORT_TAG;
    }

    exec_host_import *functions = calloc(function_count ? function_count : 1,
                                          sizeof(*functions));
    exec_global_import *globals = calloc(global_count ? global_count : 1,
                                          sizeof(*globals));
    exec_memory_import *memories = calloc(memory_count ? memory_count : 1,
                                           sizeof(*memories));
    exec_table_import *tables = calloc(table_count ? table_count : 1,
                                        sizeof(*tables));
    exec_tag_import *tags = calloc(tag_count ? tag_count : 1, sizeof(*tags));
    native_call_block *call_block = calloc(1, sizeof(*call_block));
    if (call_block && function_count)
        call_block->calls = calloc(function_count, sizeof(*call_block->calls));
    if (!call_block ||
        (function_count && (!functions || !call_block->calls)) ||
        (global_count && !globals) || (memory_count && !memories) ||
        (table_count && !tables) || (tag_count && !tags)) {
        free(functions); free(globals); free(memories); free(tables); free(tags);
        if (call_block) { free(call_block->calls); free(call_block); }
        wasm_module_dispose(&decoded);
        if (error) {
            error->status = EXEC_ERROR_FORMAT;
            snprintf(error->message, sizeof(error->message),
                     "out of memory linking module");
        }
        return EXEC_ERROR_FORMAT;
    }

    size_t nf = 0, ng = 0, nm = 0, nt = 0, ntag = 0;

    /* Resolve the declarations produced by the binary decoder for both WAT
     * output and literal binary modules. */
    for (uint32_t i = 0; i < decoded_import_count; i++) {
        const wasm_import *request = &decoded_imports[i];
        native_linked_module *provider = native_registered_module(
            store, request->module);
        if (request->kind == WASM_IMPORT_FUNCTION) {
            functions[nf].module = request->module;
            functions[nf].name = request->name;
            if (!provider && strcmp(request->module, "spectest") == 0 &&
                native_spectest_has_function(request->name)) {
                functions[nf].function = native_spectest_noop;
            } else if (!provider && store->host_resolver) {
                native_host_binding binding;
                if (store->host_resolver(request->module, request->name,
                                          store->host_context, &binding)) {
                    functions[nf].function = binding.function;
                    functions[nf].host_data = binding.host_data;
                    functions[nf].control = binding.control;
                }
            } else if (provider) {
                uint32_t index = 0, type_index = 0;
                exec_status status = exec_find_export(
                    provider->engine, request->name, &index, error);
                if (status != EXEC_OK && store->host_resolver) {
                    native_host_binding binding;
                    if (store->host_resolver(request->module, request->name,
                                              store->host_context, &binding)) {
                        functions[nf].function = binding.function;
                        functions[nf].host_data = binding.host_data;
                        functions[nf].control = binding.control;
                        if (error) memset(error, 0, sizeof(*error));
                        nf++;
                        continue;
                    }
                }
                if (status != EXEC_OK) {
                    if (error) {
                        error->status = EXEC_ERROR_NOT_FOUND;
                        snprintf(error->message, sizeof(error->message),
                                 "unresolved function import %.96s.%.96s",
                                 request->module, request->name);
                    }
                    goto fail;
                }
                status = exec_get_func_type_index(
                    provider->engine, index, &type_index, error);
                if (status != EXEC_OK) goto fail;
                call_block->calls[nf].engine = provider->engine;
                call_block->calls[nf].func_idx = index;
                functions[nf].function = native_linked_call;
                functions[nf].host_data = &call_block->calls[nf];
                functions[nf].type_owner = provider->engine;
                functions[nf].type_index = type_index;
                functions[nf].has_wasm_type = 1;
            }
            if (!functions[nf].function) {
                if (error) {
                    error->status = EXEC_ERROR_NOT_FOUND;
                    snprintf(error->message, sizeof(error->message),
                             "unresolved function import %.96s.%.96s",
                             request->module, request->name);
                }
                goto fail;
            }
            nf++;
        } else if (request->kind == WASM_IMPORT_TABLE) {
            exec_table *value = provider ? NULL :
                (strcmp(request->module, "spectest") == 0 &&
                 strcmp(request->name, "table") == 0 ?
                 &store->spectest_table : NULL);
            if (provider && exec_find_export_table(
                    provider->engine, request->name, &value, error) != EXEC_OK)
                goto fail;
            tables[nt++] = (exec_table_import){request->module,
                                               request->name, value};
        } else if (request->kind == WASM_IMPORT_MEMORY) {
            exec_memory *value = provider ? NULL :
                (strcmp(request->module, "spectest") == 0 &&
                 strcmp(request->name, "memory") == 0 ?
                 &store->spectest_memory : NULL);
            if (provider && exec_find_export_memory(
                    provider->engine, request->name, &value, error) != EXEC_OK)
                goto fail;
            memories[nm++] = (exec_memory_import){request->module,
                                                  request->name, value};
        } else if (request->kind == WASM_IMPORT_GLOBAL) {
            exec_global *value = provider ? NULL :
                (strcmp(request->module, "spectest") == 0 ?
                 native_spectest_global(store, request->name) : NULL);
            if (provider && exec_find_export_global(
                    provider->engine, request->name, &value, error) != EXEC_OK)
                goto fail;
            globals[ng++] = (exec_global_import){request->module,
                                                  request->name, value};
        } else if (request->kind == WASM_IMPORT_TAG) {
            exec_tag *value = NULL;
            if (provider && exec_find_export_tag(
                    provider->engine, request->name, &value, error) != EXEC_OK)
                goto fail;
            tags[ntag++] = (exec_tag_import){request->module,
                                              request->name, value};
        }
    }

    {
        exec_imports imports = {functions, nf, globals, ng,
                                memories, nm, tables, nt, tags, ntag};
        load_status = wasm_instantiate_module(
            &decoded, &imports, engine_out, error);
        if (load_status != EXEC_OK && !*engine_out) goto fail;
    }
    free(functions); free(globals); free(memories); free(tables); free(tags);
    wasm_module_dispose(&decoded);
    if (function_count) {
        call_block->next = store->call_blocks;
        store->call_blocks = call_block;
    } else {
        free(call_block->calls);
        free(call_block);
    }
    return load_status;

fail:
    free(functions); free(globals); free(memories); free(tables); free(tags);
    wasm_module_dispose(&decoded);
    free(call_block->calls); free(call_block);
    return error ? error->status : EXEC_ERROR_FORMAT;
}

/* ---- module encoding ---- */

uint8_t *encode_group_module(const wast_group *group, size_t *size_out,
                              char *error) {
    if (group->raw_module.kind == WAST_RAW_BINARY) {
        size_t allocation = group->raw_module.length ?
                            group->raw_module.length : 1;
        uint8_t *copy = malloc(allocation);
        if (!copy) {
            snprintf(error, 256, "out of memory copying binary module");
            return NULL;
        }
        if (group->raw_module.length)
            memcpy(copy, group->raw_module.bytes, group->raw_module.length);
        *size_out = group->raw_module.length;
        return copy;
    }
    if (group->raw_module.kind == WAST_RAW_QUOTE) {
        size_t length = group->raw_module.length;
        uint8_t *wasm = NULL;
        if (waste_wat_compile((const char *)group->raw_module.bytes, length,
                              &wasm, size_out,
                              error, 256) != 0)
            wasm = NULL;
        return wasm;
    }
    return wast_encode_module(&group->module, size_out, error);
}
