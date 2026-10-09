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
    capsule->handler.verbose = request->handler_verbose;
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

static exec_status native_store_u32(exec_memory *memory, uint32_t offset,
                                    uint32_t value, exec_error *error) {
    uint8_t bytes[4] = {
        (uint8_t)value, (uint8_t)(value >> 8),
        (uint8_t)(value >> 16), (uint8_t)(value >> 24)
    };
    return exec_memory_write(memory, offset, bytes, sizeof(bytes), error);
}

static exec_status native_store_bytes(exec_memory *memory, uint32_t offset,
                                      const void *bytes, size_t length,
                                      exec_error *error) {
    return exec_memory_write(memory, offset, bytes, length, error);
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
 * process image's visible linear memory. The process capsule records the
 * occupied top pages as a startup region, leaving the lower module region
 * available for the future brk/stack layout without host pointers. */
exec_status native_process_image_startup_block(
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
    /* A freestanding () -> () entry may have no linear memory. Retain its
     * host-owned argv/env metadata, but materialize no inaccessible ABI block. */
    if (!memory && !has_hook) return EXEC_OK;
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
    if (total > memory->linear_pages * (size_t)EXEC_PAGE_SIZE ||
        total > UINT32_MAX)
        return exec_fail(error, EXEC_ERROR_TRAP, "startup block exceeds memory");
    base = (uint32_t)(memory->linear_pages * (size_t)EXEC_PAGE_SIZE - total);
    argv_ptr = base + 44u;
    envp_ptr = argv_ptr + (request->argc + 1u) * 4u;
    cursor = envp_ptr + (request->envc + 1u) * 4u;
    if (native_store_u32(memory, base + 0, request->argc, error) != EXEC_OK ||
        native_store_u32(memory, base + 4, argv_ptr, error) != EXEC_OK ||
        native_store_u32(memory, base + 8, request->envc, error) != EXEC_OK ||
        native_store_u32(memory, base + 12, envp_ptr, error) != EXEC_OK ||
        native_store_u32(memory, base + 16, (uint32_t)image->pid, error) != EXEC_OK)
        return error->status;
    for (uint32_t i = 0; i < request->argc; i++) {
        if (native_store_u32(memory, argv_ptr + i * 4u, cursor, error) != EXEC_OK)
            return error->status;
        size_t length = strlen(request->argv[i]) + 1u;
        if (native_store_bytes(memory, cursor, request->argv[i], length,
                               error) != EXEC_OK)
            return error->status;
        cursor += (uint32_t)length;
    }
    if (native_store_u32(memory, argv_ptr + request->argc * 4u, 0, error) != EXEC_OK)
        return error->status;
    for (uint32_t i = 0; i < request->envc; i++) {
        if (native_store_u32(memory, envp_ptr + i * 4u, cursor, error) != EXEC_OK)
            return error->status;
        size_t length = strlen(request->envp[i]) + 1u;
        if (native_store_bytes(memory, cursor, request->envp[i], length,
                               error) != EXEC_OK)
            return error->status;
        cursor += (uint32_t)length;
    }
    if (native_store_u32(memory, envp_ptr + request->envc * 4u, 0, error) != EXEC_OK ||
        native_store_u32(memory, base + 20, cursor, error) != EXEC_OK ||
        native_store_bytes(memory, cursor, image->cwd,
                           strlen(image->cwd) + 1u, error) != EXEC_OK)
        return error->status;
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

static int native_executable_runtime_module(const char *name) {
    return name &&
        (strcmp(name, "env") == 0 ||
         strcmp(name, "waste_kernel") == 0 ||
         strcmp(name, "waste-runtime") == 0 ||
         strcmp(name, "spectest") == 0 ||
         strcmp(name, "GOT.mem") == 0 ||
         strcmp(name, "GOT.func") == 0);
}

static exec_status native_store_grow_table(exec_table *table,
                                           uint64_t minimum,
                                           exec_error *error);

/* Load named shared-library dependencies before instantiating an executable.
 * wasm-ld records these as ordinary import-module names (for example
 * "libncurses").  Reserve the executable's statically linked table range
 * before allocating dependency slots, then register each provider before
 * native_load_module resolves the executable's function imports. */
static exec_status native_store_load_executable_dependencies(
        native_store *store, const uint8_t *bytes, size_t size,
        exec_error *error) {
    wasm_module decoded;
    wasm_decode_error decode_error;
    wasm_module_init(&decoded);
    if (wasm_decode_module(bytes, size, &decoded, &decode_error) !=
        WASM_DECODE_OK) {
        wasm_module_dispose(&decoded);
        return exec_fail(error, EXEC_ERROR_FORMAT,
                         "cannot decode executable dependencies");
    }
    {
        native_process_capsule *capsule =
            native_store_active_capsule(store);
        exec_table *process_table = capsule && capsule->engine &&
            capsule->engine->table_count ? capsule->engine->tables[0] : NULL;
        for (uint32_t i = 0; process_table && i < decoded.import_count; i++) {
            const wasm_import *request = &decoded.imports[i];
            if (request->kind == WASM_IMPORT_TABLE &&
                strcmp(request->module, "env") == 0 &&
                strcmp(request->name, "__indirect_function_table") == 0 &&
                process_table->size <
                    request->descriptor.table.limits.minimum) {
                exec_status grow_status = native_store_grow_table(
                    process_table,
                    request->descriptor.table.limits.minimum, error);
                if (grow_status != EXEC_OK) {
                    wasm_module_dispose(&decoded);
                    return grow_status;
                }
            }
        }
    }
    for (uint32_t i = 0; i < decoded.import_count; i++) {
        const char *module = decoded.imports[i].module;
        char path[POSIX_PATH_NODE_NAME_MAX];
        if (native_executable_runtime_module(module) ||
            native_store_find_library(store, module))
            continue;
        if (native_store_resolve_library(store, module, path,
                                         sizeof(path)) != 0) {
            if (error) {
                error->status = EXEC_ERROR_NOT_FOUND;
                snprintf(error->message, sizeof(error->message),
                         "shared library %.160s is not present in the VFS",
                         module);
            }
            wasm_module_dispose(&decoded);
            return EXEC_ERROR_NOT_FOUND;
        }
        if (native_store_load_library(store, path, error) != 0) {
            if (error && !error->message[0]) {
                error->status = EXEC_ERROR_NOT_FOUND;
                snprintf(error->message, sizeof(error->message),
                         "could not load shared library %.160s", module);
            }
            wasm_module_dispose(&decoded);
            return error && error->status ? error->status :
                EXEC_ERROR_NOT_FOUND;
        }
    }
    wasm_module_dispose(&decoded);
    return EXEC_OK;
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
            (int64_t)executable->size, (uint64_t)(3u + i), 0, 0
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
        POSIX_NODE_REGULAR, 0755u, 0, 0, 0, 0, 0, 0
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
    int wat_handler = request && request->format == NATIVE_EXEC_FORMAT_AUTO &&
        strcmp(request->path, "/bin/wat") == 0;
    int wast_handler = request && request->format == NATIVE_EXEC_FORMAT_AUTO &&
        strcmp(request->path, "/bin/wast") == 0;
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
        uint32_t source_arg = 1;
        if (wast_handler && source_arg < request->argc &&
            !strcmp(request->argv[source_arg], "--verbose")) {
            request->handler_verbose = 1;
            source_arg++;
        }
        if (wast_handler && source_arg < request->argc &&
            !strcmp(request->argv[source_arg], "--")) source_arg++;
        if (source_arg >= request->argc || !request->argv[source_arg] ||
            strlen(request->argv[source_arg]) >= NATIVE_EXEC_PATH_MAX ||
            (wast_handler && request->argc != source_arg + 1u))
            return exec_fail(error, EXEC_ERROR_FORMAT,
                             "usage: /bin/wast [--verbose] [--] FILE (or /bin/wat FILE)");
        load_path = request->argv[source_arg];
    }
    memset(&vfs_executable, 0, sizeof(vfs_executable));
    if (store->kernel) {
        /* An explicit interpreter reads its input; only direct exec requires
         * the input file itself to have executable permission. Installed WAST
         * corpus snapshots deliberately have mode 0644. */
        vfs_status = (wat_handler || wast_handler || request->readable_input ?
            posix_kernel_path_read_snapshot : posix_kernel_path_snapshot)(
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
    if (request->format != NATIVE_EXEC_FORMAT_AUTO) {
        is_wat = request->format == NATIVE_EXEC_FORMAT_WAT;
        is_wast = 0;
        if ((is_wat && image_size >= 4 && !memcmp(image_bytes, "\0asm", 4)) ||
            (!is_wat && (image_size < 4 || memcmp(image_bytes, "\0asm", 4)))) {
            free(vfs_bytes);
            return exec_fail(error, EXEC_ERROR_FORMAT,
                             is_wat ? "expected WAT input" : "expected Wasm binary input");
        }
    }
    if (source_result == WASTE_SOURCE_INVALID_SHEBANG ||
        source_result == WASTE_SOURCE_SHEBANG_TOO_LONG) {
        free(vfs_bytes);
        return exec_fail(error, EXEC_ERROR_FORMAT,
                         source_result == WASTE_SOURCE_SHEBANG_TOO_LONG ?
                         "shebang line exceeds loader limit" :
                         "invalid shebang line");
    }
    if (request->format == NATIVE_EXEC_FORMAT_AUTO && source_result == WASTE_SOURCE_OK &&
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
    status = native_store_load_executable_dependencies(
        store, image_bytes, image_size, error);
    if (status == EXEC_OK)
        status = native_load_module(store, NULL, image_bytes, image_size,
                                &engine, error);
    free(compiled_bytes);
    free(vfs_bytes);
    vfs_bytes = NULL;
    if (status != EXEC_OK) {
        exec_free(engine);
        return status;
    }

    native_exec_request_init(&effective_request);
    effective_request = *request;
    if (request->format == NATIVE_EXEC_FORMAT_AUTO && source_result == WASTE_SOURCE_OK &&
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
    status = exec_find_export(engine, request->entry[0] ? request->entry : executable->entry,
                              &entry_func, error);
    if (status == EXEC_OK) {
        uint32_t type_index;
        status = exec_get_func_type_index(engine, entry_func, &type_index, error);
        if (status == EXEC_OK && (type_index >= engine->type_count ||
            engine->types[type_index].param_count || engine->types[type_index].result_count))
            status = exec_fail(error, EXEC_ERROR_FORMAT, "process entry must have type () -> ()");
    }
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
    native_linked_func *function = (native_linked_func *)data;
    /* Fork clones share immutable import bindings, not provider state. Calling
     * the canonical engine directly lets child libc mutate the parent's heap. */
    waste_exec_engine *provider = exec_clone_resolve(caller, function->engine);
    exec_status status = exec_invoke(
        provider, function->func_idx, args, arg_count,
        results, result_count, error);
    if (status != EXEC_OK && status != EXEC_YIELD &&
        status != EXEC_ERROR_EXIT && error) {
        char detail[sizeof(error->message)];
        snprintf(detail, sizeof(detail), "%.110s%s%.48s.%.80s",
                 error->message[0] ? error->message : "linked call failed",
                 " via ",
                 function->module, function->name);
        snprintf(error->message, sizeof(error->message), "%s", detail);
    }
    return status;
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
    store->execution_control.signal_poll = native_store_default_signal;
    store->execution_control.signal_context = store;
    native_exec_request_init(&store->exec_request);
    store->spectest_memory.pages = 1;
    /* Imported memories use the same engine-owned linear-memory bound as
     * module-owned memories.  Keep the initial page visible to bounds checks
     * and memory.grow; leaving this at zero makes the shared spectest memory
     * appear unmapped to the executor. */
    store->spectest_memory.linear_pages = 1;
    store->spectest_memory.max_pages = 2;
    store->spectest_memory.has_max = 1;
    store->spectest_memory.page_data = calloc(1,
                                               sizeof(*store->spectest_memory.page_data));
    store->spectest_memory.page_protection = malloc(1);
    store->spectest_memory.mappings = calloc(1,
                                               sizeof(*store->spectest_memory.mappings));
    if (store->spectest_memory.page_protection)
        store->spectest_memory.page_protection[0] = EXEC_MEMORY_PROT_READ |
                                                    EXEC_MEMORY_PROT_WRITE;
    if (store->spectest_memory.mappings) {
        store->spectest_memory.mappings[0].page_count = 1;
        store->spectest_memory.mappings[0].protection =
            EXEC_MEMORY_PROT_READ | EXEC_MEMORY_PROT_WRITE;
        store->spectest_memory.mapping_count = 1;
        store->spectest_memory.mapping_capacity = 1;
    }
    if (store->spectest_memory.page_data) {
        uint8_t zero = 0;
        exec_error memory_error = {0};
        (void)exec_memory_write(&store->spectest_memory, 0, &zero, 1,
                                &memory_error);
    }
    store->spectest_table.size = 10;
    store->spectest_table.max_size = 20;
    store->spectest_table.has_max = 1;
    store->spectest_table.element_type = WASM_VALTYPE_FUNCREF;
    store->spectest_table.elements = calloc(10, sizeof(exec_table_element));
    store->spectest_table64.size = 10;
    store->spectest_table64.max_size = 20;
    store->spectest_table64.has_max = 1;
    store->spectest_table64.is_64 = 1;
    store->spectest_table64.element_type = WASM_VALTYPE_FUNCREF;
    store->spectest_table64.elements = calloc(10, sizeof(exec_table_element));
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
        store->shm_namespace = posix_shm_namespace_create();
        if (!store->shm_namespace || posix_kernel_set_shm_namespace(
                store->kernel, store->shm_namespace) != 0) {
            posix_shm_namespace_release(store->shm_namespace);
            store->shm_namespace = NULL;
            posix_kernel_destroy(store->kernel);
            store->kernel = NULL;
        }
    }
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
    for (uint32_t i = 0; i < store->shared_file_page_count; i++) {
        exec_memory_page_release(store->shared_file_pages[i].page);
        posix_kernel_file_release(store->shared_file_pages[i].file_object);
    }
    free(store->shared_file_pages);
    for (int i = store->module_count; i > 0; i--)
        exec_free(store->modules[i - 1].engine);
    for (int i = store->orphan_count; i > 0; i--)
        exec_free(store->orphan_engines[i - 1]);
    native_call_block *block = store->call_blocks;
    while (block) {
        native_call_block *next = block->next;
        free(block->calls);
        free(block->got_globals);
        free(block);
        block = next;
    }
    free(store->modules);
    free(store->orphan_engines);
    exec_memory_release(&store->spectest_memory);
    free(store->spectest_table.elements);
    free(store->spectest_table64.elements);
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++)
        if (store->processes[i].used) {
            native_process_capsule_destroy(&store->processes[i].capsule);
            posix_kernel_destroy(store->processes[i].kernel);
            store->processes[i].kernel = NULL;
        }
    posix_shm_namespace_release(store->shm_namespace);
    store->shm_namespace = NULL;
    store->kernel = NULL;
    free(store->host_io.data);
    memset(store, 0, sizeof(*store));
}

void native_store_enable_terminal(native_store *store) {
    if (!store) return;
    if (store->kernel) posix_kernel_destroy(store->kernel);
    store->kernel = posix_kernel_create(1);
    store->kernel_terminal = store->kernel != NULL;
    if (store->kernel) {
        if (store->shm_namespace)
            (void)posix_kernel_set_shm_namespace(store->kernel,
                                                 store->shm_namespace);
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
    if (identity) {
        snprintf(linked->id, sizeof(linked->id), "%s", identity->id);
        snprintf(linked->registered, sizeof(linked->registered), "%s",
                 identity->register_name);
    }
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

static uint32_t native_store_engine_memory_base(native_store *store,
                                                 waste_exec_engine *engine) {
    native_process_capsule *capsule = native_store_active_capsule(store);
    if (!capsule || !engine) return 0;
    for (uint32_t i = 0; i < capsule->loaded_library_count; i++)
        if (capsule->loaded_libraries[i].engine == engine)
            return capsule->loaded_libraries[i].memory_base;
    return 0;
}

static int native_store_find_global_symbol(native_store *store,
                                            waste_exec_engine *self,
                                            const char *name,
                                            exec_global **global_out,
                                            uint32_t *base_out) {
    exec_error ignored = {0};
    if (exec_find_export_global(self, name, global_out, &ignored) == EXEC_OK) {
        *base_out = store->library_load_ctx.active ?
            (uint32_t)store->library_load_ctx.memory_base_global.value.i32 :
            native_store_engine_memory_base(store, self);
        return 1;
    }
    for (int i = 0; i < store->module_count; i++) {
        waste_exec_engine *engine = native_store_process_engine(
            store, store->modules[i].engine);
        memset(&ignored, 0, sizeof(ignored));
        if (engine && engine != self &&
            exec_find_export_global(engine, name, global_out, &ignored) ==
                EXEC_OK) {
            *base_out = native_store_engine_memory_base(store, engine);
            return 1;
        }
    }
    return 0;
}

static int native_store_find_function_symbol(native_store *store,
                                              waste_exec_engine *self,
                                              const char *name,
                                              waste_exec_engine **owner_out,
                                              uint32_t *index_out) {
    exec_error ignored = {0};
    if (exec_find_export(self, name, index_out, &ignored) == EXEC_OK) {
        *owner_out = self;
        return 1;
    }
    for (int i = 0; i < store->module_count; i++) {
        waste_exec_engine *engine = native_store_process_engine(
            store, store->modules[i].engine);
        memset(&ignored, 0, sizeof(ignored));
        if (engine && engine != self &&
            exec_find_export(engine, name, index_out, &ignored) == EXEC_OK) {
            *owner_out = engine;
            return 1;
        }
    }
    return 0;
}

static int native_store_function_address(native_store *store,
                                         waste_exec_engine *owner,
                                         uint32_t function_index,
                                         uint32_t *address_out) {
    native_process_capsule *capsule = native_store_active_capsule(store);
    exec_table *table = capsule && capsule->engine &&
        capsule->engine->table_count ? capsule->engine->tables[0] : NULL;
    if (!table) return 0;
    for (uint64_t i = 1; i < table->size; i++) {
        if (table->elements[i].owner == owner &&
            table->elements[i].func_idx == function_index) {
            *address_out = (uint32_t)i;
            return 1;
        }
    }
    uint64_t slot = table->size;
    if (slot >= UINT32_MAX) return 0;
    exec_table_element *elements = realloc(
        table->elements, (size_t)(slot + 1) * sizeof(*elements));
    if (!elements) return 0;
    table->elements = elements;
    memset(&table->elements[slot], 0, sizeof(table->elements[slot]));
    table->elements[slot].owner = owner;
    table->elements[slot].func_idx = function_index;
    table->elements[slot].type = WASM_VALTYPE_FUNCREF;
    table->elements[slot].dynamic_type = WASM_VALTYPE_FUNCREF;
    table->size = slot + 1;
    if (!table->has_max || table->max_size < table->size)
        table->max_size = table->size;
    *address_out = (uint32_t)slot;
    return 1;
}

static exec_status native_store_grow_table(exec_table *table,
                                           uint64_t minimum,
                                           exec_error *error) {
    exec_table_element *elements;
    if (!table || minimum > UINT32_MAX)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "function table size is invalid");
    if (table->size >= minimum) return EXEC_OK;
    elements = realloc(table->elements,
                       (size_t)minimum * sizeof(*elements));
    if (!elements)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "cannot grow the function table");
    memset(&elements[table->size], 0,
           (size_t)(minimum - table->size) * sizeof(*elements));
    table->elements = elements;
    table->size = minimum;
    if (!table->has_max || table->max_size < minimum)
        table->max_size = minimum;
    return EXEC_OK;
}

static int native_store_find_imported_function_symbol(
        const wasm_module *decoded, const char *name, uint32_t *index_out) {
    uint32_t function_index = 0;
    if (!decoded || !name || !index_out) return 0;
    for (uint32_t i = 0; i < decoded->import_count; i++) {
        const wasm_import *request = &decoded->imports[i];
        if (request->kind != WASM_IMPORT_FUNCTION) continue;
        if (strcmp(request->name, name) == 0) {
            *index_out = function_index;
            return 1;
        }
        function_index++;
    }
    return 0;
}

static exec_status native_store_patch_got(native_store *store,
                                          const wasm_module *decoded,
                                          exec_global_import *globals,
                                          waste_exec_engine *engine,
                                          exec_error *error) {
    uint32_t global_slot = 0;
    for (uint32_t i = 0; i < decoded->import_count; i++) {
        const wasm_import *request = &decoded->imports[i];
        if (request->kind != WASM_IMPORT_GLOBAL) continue;
        exec_global *got = globals[global_slot++].global;
        if (strcmp(request->module, "GOT.mem") == 0) {
            exec_global *symbol = NULL;
            uint32_t memory_base = 0;
            if (!got || !native_store_find_global_symbol(
                    store, engine, request->name, &symbol, &memory_base)) {
                if (error) {
                    error->status = EXEC_ERROR_NOT_FOUND;
                    snprintf(error->message, sizeof(error->message),
                             "unresolved GOT.mem symbol %.160s",
                             request->name);
                }
                return EXEC_ERROR_NOT_FOUND;
            }
            got->value.type = WASM_VALTYPE_I32;
            got->value.i32 = (int32_t)(memory_base +
                                       (uint32_t)symbol->value.i32);
        } else if (strcmp(request->module, "GOT.func") == 0) {
            waste_exec_engine *owner = NULL;
            uint32_t function_index = 0, address = 0;
            if (!got) {
                if (error) {
                    error->status = EXEC_ERROR_NOT_FOUND;
                    snprintf(error->message, sizeof(error->message),
                             "unresolved GOT.func symbol %.160s",
                             request->name);
                }
                return EXEC_ERROR_NOT_FOUND;
            }
            if (!native_store_find_function_symbol(
                    store, engine, request->name, &owner, &function_index)) {
                /* A PIC executable can take the address of a function that
                 * it also imports normally (for example exit).  Its own
                 * imported function index is a valid shared-table target and
                 * preserves the already-resolved host/module binding. */
                if (!native_store_find_imported_function_symbol(
                        decoded, request->name, &function_index)) {
                    if (error) {
                        error->status = EXEC_ERROR_NOT_FOUND;
                        snprintf(error->message, sizeof(error->message),
                                 "unresolved GOT.func symbol %.160s",
                                 request->name);
                    }
                    return EXEC_ERROR_NOT_FOUND;
                }
                owner = engine;
            }
            if (!native_store_function_address(
                    store, owner, function_index, &address)) {
                if (error) {
                    error->status = EXEC_ERROR_NOT_FOUND;
                    snprintf(error->message, sizeof(error->message),
                             "unresolved GOT.func symbol %.160s",
                             request->name);
                }
                return EXEC_ERROR_NOT_FOUND;
            }
            got->value.type = WASM_VALTYPE_I32;
            got->value.i32 = (int32_t)address;
        }
    }
    return EXEC_OK;
}

exec_status native_load_module(native_store *store,
                                const wast_module *module,
                                const uint8_t *bytes, size_t size,
                                waste_exec_engine **engine_out,
                                exec_error *error) {
    wasm_module decoded;
    wasm_decode_error decode_error;
    size_t function_count = 0, global_count = 0, got_count = 0;
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
        got_count += decoded_imports[i].kind == WASM_IMPORT_GLOBAL &&
            (!strcmp(decoded_imports[i].module, "GOT.func") ||
             !strcmp(decoded_imports[i].module, "GOT.mem") ||
             (store->library_load_ctx.active &&
              !strcmp(decoded_imports[i].module, "env") &&
              (!strcmp(decoded_imports[i].name, "__memory_base") ||
               !strcmp(decoded_imports[i].name, "__table_base"))));
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
    if (call_block && got_count)
        call_block->got_globals = calloc(got_count, sizeof(*call_block->got_globals));
    if (!call_block ||
        (function_count && (!functions || !call_block->calls)) ||
        (got_count && !call_block->got_globals) ||
        (global_count && !globals) || (memory_count && !memories) ||
        (table_count && !tables) || (tag_count && !tags)) {
        free(functions); free(globals); free(memories); free(tables); free(tags);
        if (call_block) {
            free(call_block->calls); free(call_block->got_globals); free(call_block);
        }
        wasm_module_dispose(&decoded);
        if (error) {
            error->status = EXEC_ERROR_FORMAT;
            snprintf(error->message, sizeof(error->message),
                     "out of memory linking module");
        }
        return EXEC_ERROR_FORMAT;
    }

    size_t nf = 0, ng = 0, nm = 0, nt = 0, ntag = 0, ngot = 0;

    /* Resolve the declarations produced by the binary decoder for both WAT
     * output and literal binary modules. */
    for (uint32_t i = 0; i < decoded_import_count; i++) {
        const wasm_import *request = &decoded_imports[i];
        native_linked_module process_provider = {0};
        native_linked_module *provider = native_registered_module(
            store, request->module);
        native_loaded_library *loaded_provider =
            native_store_find_library(store, request->module);
        /* An explicit WAST (register ...) binding is the newest store
         * registration and must win over a same-named process DSO. The DSO
         * list is only a fallback for process providers missing a linked
         * module entry; otherwise Bash's libc would shadow a test's provider. */
        if (!provider && loaded_provider) {
            process_provider.engine = loaded_provider->engine;
            provider = &process_provider;
        }
        waste_exec_engine *provider_engine = provider ?
            native_store_process_engine(store, provider->engine) : NULL;
        if (request->kind == WASM_IMPORT_FUNCTION) {
            native_host_binding host_binding = {0};
            int host_resolved = store->host_resolver &&
                store->host_resolver(request->module, request->name,
                                     store->host_context, &host_binding);
            functions[nf].module = request->module;
            functions[nf].name = request->name;
            if (!provider && strcmp(request->module, "spectest") == 0 &&
                native_spectest_has_function(request->name)) {
                functions[nf].function = native_spectest_noop;
            } else if (host_resolved &&
                       (!provider || host_binding.prefer_over_module)) {
                functions[nf].function = host_binding.function;
                functions[nf].host_data = host_binding.host_data;
                functions[nf].control = host_binding.control;
            } else if (provider) {
                uint32_t index = 0, type_index = 0;
                exec_status status = exec_find_export(
                    provider_engine, request->name, &index, error);
                if (status != EXEC_OK && host_resolved) {
                    functions[nf].function = host_binding.function;
                    functions[nf].host_data = host_binding.host_data;
                    functions[nf].control = host_binding.control;
                    if (error) memset(error, 0, sizeof(*error));
                    nf++;
                    continue;
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
                    provider_engine, index, &type_index, error);
                if (status != EXEC_OK) goto fail;
                call_block->calls[nf].engine = provider_engine;
                call_block->calls[nf].func_idx = index;
                snprintf(call_block->calls[nf].module,
                         sizeof(call_block->calls[nf].module), "%s",
                         request->module);
                snprintf(call_block->calls[nf].name,
                         sizeof(call_block->calls[nf].name), "%s",
                         request->name);
                functions[nf].function = native_linked_call;
                functions[nf].host_data = &call_block->calls[nf];
                functions[nf].type_owner = provider_engine;
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
            exec_table *value = NULL;
            if (!provider && strcmp(request->module, "spectest") == 0) {
                if (strcmp(request->name, "table") == 0)
                    value = &store->spectest_table;
                else if (strcmp(request->name, "table64") == 0)
                    value = &store->spectest_table64;
            }
            if (strcmp(request->module, "env") == 0 &&
                strcmp(request->name, "__indirect_function_table") == 0) {
                native_process_capsule *capsule =
                    native_store_active_capsule(store);
                if (capsule && capsule->engine &&
                    capsule->engine->table_count)
                    value = capsule->engine->tables[0];
                if (!value) {
                    native_linked_module *runtime =
                        native_registered_module(store, "waste-runtime");
                    exec_error ignored = {0};
                if (runtime)
                        (void)exec_find_export_table(
                            native_store_process_engine(store,
                                runtime->engine), "table", &value, &ignored);
                }
            }
            if (!value && provider && exec_find_export_table(
                    provider_engine, request->name, &value, error) != EXEC_OK) {
                if (error) {
                    error->status = EXEC_ERROR_NOT_FOUND;
                    snprintf(error->message, sizeof(error->message),
                             "unresolved table import %.96s.%.96s",
                             request->module, request->name);
                }
                goto fail;
            }
            if (!value) {
                if (error) {
                    error->status = EXEC_ERROR_NOT_FOUND;
                    snprintf(error->message, sizeof(error->message),
                             "unresolved table import %.96s.%.96s",
                             request->module, request->name);
                }
                goto fail;
            }
            /* The shared-library loader coordinates table sizing across
             * executables that import `env.__indirect_function_table` from
             * `waste-runtime`.  Growing an arbitrary imported table before
             * wasm_load's spec-conformant limit check would mask
             * incompatible imports (e.g. assert_unlinkable with a larger
             * minimum than the exporter declares).  Restrict the grow to
             * the shared runtime table. */
            if (value->size < request->descriptor.table.limits.minimum &&
                strcmp(request->module, "env") == 0 &&
                strcmp(request->name, "__indirect_function_table") == 0 &&
                native_store_grow_table(
                    value, request->descriptor.table.limits.minimum,
                    error) != EXEC_OK)
                goto fail;
            tables[nt++] = (exec_table_import){request->module,
                                               request->name, value};
        } else if (request->kind == WASM_IMPORT_MEMORY) {
            exec_memory *value = provider ? NULL :
                (strcmp(request->module, "spectest") == 0 &&
                 strcmp(request->name, "memory") == 0 ?
                 &store->spectest_memory : NULL);
            if (strcmp(request->module, "env") == 0 &&
                strcmp(request->name, "memory") == 0) {
                native_process_capsule *capsule =
                    native_store_active_capsule(store);
                if (capsule && capsule->engine)
                    value = capsule->engine->memory;
                if (!value) {
                    native_linked_module *runtime =
                        native_registered_module(store, "waste-runtime");
                    exec_error ignored = {0};
                if (runtime)
                        (void)exec_find_export_memory(
                            native_store_process_engine(store,
                                runtime->engine), "memory", &value, &ignored);
                }
            }
            if (!value && provider && exec_find_export_memory(
                    provider_engine, request->name, &value, error) != EXEC_OK) {
                if (error) {
                    error->status = EXEC_ERROR_NOT_FOUND;
                    snprintf(error->message, sizeof(error->message),
                             "unresolved memory import %.96s.%.96s",
                             request->module, request->name);
                }
                goto fail;
            }
            if (!value) {
                if (error) {
                    error->status = EXEC_ERROR_NOT_FOUND;
                    snprintf(error->message, sizeof(error->message),
                             "unresolved memory import %.96s.%.96s",
                             request->module, request->name);
                }
                goto fail;
            }
            /* A process memory reserves a sparse virtual address space in
             * memory->pages, while linear_pages remains the Wasm-visible
             * memory size.  Executables linked with --import-memory may
             * require a larger initial linear memory than the runtime image
             * that owns the address space.  Grow the linear mapping before
             * wasm_load_module validates the import and applies data
             * segments. */
            if (value->process_virtual_memory &&
                value->linear_pages < request->descriptor.memory.limits.minimum) {
                exec_status resize_status = exec_memory_resize_pages(
                    value, request->descriptor.memory.limits.minimum, error);
                if (resize_status != EXEC_OK) goto fail;
            }
            memories[nm++] = (exec_memory_import){request->module,
                                                  request->name, value};
        } else if (request->kind == WASM_IMPORT_GLOBAL) {
            exec_global *value = NULL;
            /* PIC shared library globals: __memory_base, __table_base, and
             * __stack_pointer are provided by the library load context. */
            if (store->library_load_ctx.active &&
                strcmp(request->module, "env") == 0) {
                /* Each DSO owns its relocation bases. The transient load
                 * context is overwritten when the next library is loaded. */
                if (strcmp(request->name, "__memory_base") == 0) {
                    value = &call_block->got_globals[ngot++];
                    *value = store->library_load_ctx.memory_base_global;
                } else if (strcmp(request->name, "__table_base") == 0) {
                    value = &call_block->got_globals[ngot++];
                    *value = store->library_load_ctx.table_base_global;
                }
                else if (strcmp(request->name, "__stack_pointer") == 0)
                    value = store->library_load_ctx.stack_pointer;
            }
            if (!value && strcmp(request->module, "env") == 0 &&
                strcmp(request->name, "__stack_pointer") == 0) {
                native_linked_module *runtime = native_registered_module(store, "waste-runtime");
                exec_error ignored = {0};
                if (runtime)
                    (void)exec_find_export_global(native_store_process_engine(
                        store, runtime->engine), request->name, &value, &ignored);
            }
            /* GOT.func and GOT.mem globals are resolved as mutable i32
             * globals.  The loader fills them after instantiation. */
            if (!value && (strcmp(request->module, "GOT.func") == 0 ||
                           strcmp(request->module, "GOT.mem") == 0)) {
                /* For now, unresolved GOT entries are provided as zero-
                 * initialized mutable globals.  The loader patches them
                 * after all libraries are instantiated. */
                exec_global *got = &call_block->got_globals[ngot++];
                got->value.type = WASM_VALTYPE_I32;
                got->value.i32 = 0;
                got->mutable_ = 1;
                value = got;
            }
            if (!value && !provider)
                value = strcmp(request->module, "spectest") == 0 ?
                    native_spectest_global(store, request->name) : NULL;
            if (!value && provider && exec_find_export_global(
                    provider_engine, request->name, &value, error) != EXEC_OK) {
                if (error) {
                    error->status = EXEC_ERROR_NOT_FOUND;
                    snprintf(error->message, sizeof(error->message),
                             "unresolved global import %.96s.%.96s",
                             request->module, request->name);
                }
                goto fail;
            }
            if (!value) {
                if (error) {
                    error->status = EXEC_ERROR_NOT_FOUND;
                    snprintf(error->message, sizeof(error->message),
                             "unresolved global import %.96s.%.96s",
                             request->module, request->name);
                }
                goto fail;
            }
            globals[ng++] = (exec_global_import){request->module,
                                                  request->name, value};
        } else if (request->kind == WASM_IMPORT_TAG) {
            exec_tag *value = NULL;
            if (provider && exec_find_export_tag(
                    provider_engine, request->name, &value, error) != EXEC_OK) {
                if (error) {
                    error->status = EXEC_ERROR_NOT_FOUND;
                    snprintf(error->message, sizeof(error->message),
                             "unresolved tag import %.96s.%.96s",
                             request->module, request->name);
                }
                goto fail;
            }
            if (!value) {
                if (error) {
                    error->status = EXEC_ERROR_NOT_FOUND;
                    snprintf(error->message, sizeof(error->message),
                             "unresolved tag import %.96s.%.96s",
                             request->module, request->name);
                }
                goto fail;
            }
            tags[ntag++] = (exec_tag_import){request->module,
                                              request->name, value};
        }
    }

    {
        exec_imports imports = {functions, nf, globals, ng,
                                memories, nm, tables, nt, tags, ntag,
                                &store->execution_control};
        load_status = wasm_instantiate_module(
            &decoded, &imports, engine_out, error);
        if (load_status != EXEC_OK && !*engine_out) {
            /* A yielding host import need not populate error.status. Preserve
             * the returned start status when disposing the partial instance;
             * otherwise failure cleanup can incorrectly return EXEC_OK. */
            if (error) error->status = load_status;
            goto fail;
        }
        /* wasm-ld uses GOT.mem/GOT.func imports for both shared objects and
         * position-independent executables.  Patch every instantiated
         * module; modules without GOT imports make this a no-op. */
        if (load_status == EXEC_OK) {
            load_status = native_store_patch_got(
                store, &decoded, globals, *engine_out, error);
            if (load_status != EXEC_OK) {
                exec_free(*engine_out);
                *engine_out = NULL;
                goto fail;
            }
        }
    }
    free(functions); free(globals); free(memories); free(tables); free(tags);
    wasm_module_dispose(&decoded);
    if (function_count || got_count) {
        call_block->next = store->call_blocks;
        store->call_blocks = call_block;
    } else {
        free(call_block->calls);
        free(call_block->got_globals);
        free(call_block);
    }
    return load_status;

fail:
    free(functions); free(globals); free(memories); free(tables); free(tags);
    wasm_module_dispose(&decoded);
    free(call_block->calls); free(call_block->got_globals); free(call_block);
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

/* ---- shared library loading ---- */

#include "wasm/reader.h"

int native_parse_dylink(const uint8_t *bytes, size_t size,
                        native_dylink_info *info) {
    if (!bytes || !info || size < 8) return -1;
    memset(info, 0, sizeof(*info));
    /* Validate wasm magic and version. */
    if (bytes[0] != 0x00 || bytes[1] != 0x61 ||
        bytes[2] != 0x73 || bytes[3] != 0x6d) return -1;
    wasm_reader reader;
    wasm_reader_init(&reader, bytes + 8, size - 8);
    /* Walk sections looking for custom section (id 0) named "dylink.0". */
    while (wasm_reader_remaining(&reader) > 0) {
        uint8_t section_id;
        uint32_t section_size;
        if (!wasm_reader_read_u8(&reader, &section_id) ||
            !wasm_reader_read_u32(&reader, &section_size))
            return -1;
        wasm_reader section;
        if (!wasm_reader_read_subreader(&reader, section_size, &section))
            return -1;
        if (section_id != 0) continue;
        uint32_t name_length;
        const uint8_t *name_bytes;
        if (!wasm_reader_read_u32(&section, &name_length) ||
            !wasm_reader_read_bytes(&section, name_length, &name_bytes))
            continue;
        if (name_length == 8 &&
            memcmp(name_bytes, "dylink.0", 8) == 0) {
            /* WASM_DYLINK_MEM_INFO sub-section (type 1). */
            uint8_t subsection_type;
            uint32_t subsection_size;
            if (!wasm_reader_read_u8(&section, &subsection_type) ||
                !wasm_reader_read_u32(&section, &subsection_size))
                return -1;
            if (subsection_type != 1) return -1;
            wasm_reader sub;
            if (!wasm_reader_read_subreader(&section, subsection_size, &sub))
                return -1;
            if (!wasm_reader_read_u32(&sub, &info->memory_size) ||
                !wasm_reader_read_u32(&sub, &info->memory_alignment) ||
                !wasm_reader_read_u32(&sub, &info->table_size) ||
                !wasm_reader_read_u32(&sub, &info->table_alignment))
                return -1;
            return 0;
        }
    }
    return -1;
}

int native_store_resolve_library(native_store *store, const char *name,
                                 char *path_out, size_t path_size) {
    if (!store || !name || !path_out || !path_size) return -POSIX_EINVAL;
    struct posix_kernel *kernel = store->kernel;
    if (!kernel) return -POSIX_ENOSYS;
    /* Try search paths in order. */
    const char *prefixes[] = {"/usr/lib/", "/lib/"};
    const char *suffixes[] = {"", ".wasm", ".so.wasm"};
    for (int p = 0; p < 2; p++) {
        for (int s = 0; s < 3; s++) {
            char trial[POSIX_PATH_NODE_NAME_MAX];
            int n = snprintf(trial, sizeof(trial), "%s%s%s",
                             prefixes[p], name, suffixes[s]);
            if (n < 0 || (size_t)n >= sizeof(trial)) continue;
            posix_path_metadata meta;
            if (posix_kernel_path_stat(kernel, (const uint8_t *)trial,
                                       (size_t)n, 1, &meta) == 0 &&
                meta.kind == POSIX_NODE_REGULAR) {
                size_t len = (size_t)n;
                if (len + 1 > path_size) continue;
                memcpy(path_out, trial, len + 1);
                return 0;
            }
        }
    }
    return -POSIX_ENOENT;
}

static int native_store_allocate_library_region(
        native_process_capsule *capsule, uint64_t page_count,
        uint64_t *first_page_out) {
    uint64_t limit;
    uint64_t candidate;
    if (!capsule || !page_count || !first_page_out) return -POSIX_EINVAL;
    limit = capsule->virtual_page_limit ? capsule->virtual_page_limit :
        EXEC_MEM32_MAX_PAGES;
    /* The process layout owns the top sixteen pages as stack and the page
     * immediately below them as a guard.  Keep DSOs below that fixed range
     * even while exec preflight is running against a capsule whose old
     * image-region metadata may subsequently be rebuilt. */
    if (limit <= 17 || page_count > limit - 17) return -POSIX_ENOMEM;
    candidate = limit - 17 - page_count;
    for (;;) {
        if (!native_process_capsule_region_is_reserved(
                capsule, candidate, page_count, NULL)) {
            int result = native_process_capsule_reserve_region(
                capsule, candidate, page_count,
                NATIVE_PROCESS_REGION_LIBRARY);
            if (result == 0) *first_page_out = candidate;
            return result;
        }
        if (candidate == 0) break;
        candidate--;
    }
    return -POSIX_ENOMEM;
}

native_loaded_library *native_store_find_library(native_store *store,
                                                 const char *name) {
    if (!store || !name) return NULL;
    native_process_capsule *capsule = native_store_active_capsule(store);
    if (!capsule) return NULL;
    for (uint32_t i = 0; i < capsule->loaded_library_count; i++) {
        if (strcmp(capsule->loaded_libraries[i].name, name) == 0)
            return &capsule->loaded_libraries[i];
    }
    return NULL;
}

static int native_store_load_library_sync(native_store *store, const char *path,
                              exec_error *error) {
    if (!store || !path) {
        if (error) {
            error->status = EXEC_ERROR_FORMAT;
            snprintf(error->message, sizeof(error->message),
                     "invalid shared-library request");
        }
        return -POSIX_EINVAL;
    }
    native_process_capsule *capsule = native_store_active_capsule(store);
    if (!capsule) {
        if (error) {
            error->status = EXEC_ERROR_FORMAT;
            snprintf(error->message, sizeof(error->message),
                     "shared-library load has no active process");
        }
        return -POSIX_EINVAL;
    }

    /* Check if already loaded. */
    for (uint32_t i = 0; i < capsule->loaded_library_count; i++) {
        if (capsule->loaded_libraries[i].engine &&
            strcmp(capsule->loaded_libraries[i].path, path) == 0) {
            capsule->loaded_libraries[i].ref_count++;
            return 0;
        }
    }
    if (capsule->loaded_library_count >= NATIVE_LOADED_LIBRARY_MAX) {
        if (error) {
            error->status = EXEC_ERROR_TRAP;
            snprintf(error->message, sizeof(error->message),
                     "shared-library table is full");
        }
        return -POSIX_ENOMEM;
    }

    /* Fork clones allocate only the inherited entries. Reserve room before
     * appending another DSO, and before publishing any loader state. */
    if (capsule->loaded_library_count >= capsule->loaded_library_capacity) {
        native_loaded_library *libraries = realloc(capsule->loaded_libraries,
            NATIVE_LOADED_LIBRARY_MAX * sizeof(*libraries));
        if (!libraries) return -POSIX_ENOMEM;
        memset(libraries + capsule->loaded_library_capacity, 0,
            (NATIVE_LOADED_LIBRARY_MAX - capsule->loaded_library_capacity) *
                sizeof(*libraries));
        capsule->loaded_libraries = libraries;
        capsule->loaded_library_capacity = NATIVE_LOADED_LIBRARY_MAX;
    }

    /* Load bytes from VFS. */
    uint8_t *bytes = NULL;
    size_t size = 0;
    posix_path_metadata meta;
    size_t path_len = strlen(path);
    int err = posix_kernel_path_read_snapshot(
        store->kernel, (const uint8_t *)path, path_len,
        NATIVE_EXEC_BYTES_MAX, &bytes, &size, &meta);
    if (err != 0) {
        if (error) {
            error->status = EXEC_ERROR_NOT_FOUND;
            snprintf(error->message, sizeof(error->message),
                     "cannot read shared library %.180s (errno %d)", path,
                     -err);
        }
        return err;
    }

    /* Parse dylink.0 section. */
    native_dylink_info dylink;
    if (native_parse_dylink(bytes, size, &dylink) != 0) {
        free(bytes);
        if (error)
            snprintf(error->message, sizeof(error->message),
                     "missing dylink.0 section in %.200s", path);
        return -POSIX_ENOEXEC;
    }

    /* Allocate memory region for library data. */
    uint32_t memory_pages = (dylink.memory_size + 65535) / 65536;
    if (memory_pages == 0) memory_pages = 1;
    uint32_t memory_base = 0;
    if (dylink.memory_size > 0) {
        uint64_t memory_first_page = 0;
        exec_memory *shared_memory = capsule->engine ?
            capsule->engine->memory : NULL;
        int region_status = shared_memory ?
            native_store_allocate_library_region(
                capsule, memory_pages, &memory_first_page) : -POSIX_ENOMEM;
        uint64_t required_pages = memory_first_page + memory_pages;
        if (region_status != 0 ||
            (required_pages > shared_memory->pages &&
             exec_memory_reserve_virtual_pages(
                 shared_memory, required_pages, NULL) != EXEC_OK) ||
            exec_memory_map_pages(
                shared_memory, memory_first_page, memory_pages,
                EXEC_MEMORY_PROT_READ | EXEC_MEMORY_PROT_WRITE, 0, NULL) !=
                EXEC_OK) {
            free(bytes);
            if (error) {
                error->status = EXEC_ERROR_TRAP;
                snprintf(error->message, sizeof(error->message),
                         "cannot reserve %u pages for %.160s", memory_pages,
                         path);
            }
            return -POSIX_ENOMEM;
        }
        memory_base = (uint32_t)(memory_first_page * EXEC_PAGE_SIZE);
        /* Align within the page if needed. */
        if (dylink.memory_alignment > 0) {
            uint32_t align = 1u << dylink.memory_alignment;
            uint32_t remainder = memory_base % align;
            if (remainder) memory_base += align - remainder;
        }
    }

    /* Allocate table space for the library's indirect-callable functions.
     * The shared table lives in the process's main engine; grow it by
     * dylink.table_size so __table_base points past existing entries. */
    uint32_t table_base = 0;
    if (dylink.table_size > 0 && capsule->engine &&
        capsule->engine->table_count > 0) {
        exec_table *shared_table = capsule->engine->tables[0];
        if (shared_table) {
            uint64_t old_size = shared_table->size;
            uint64_t new_size = old_size + dylink.table_size;
            if (new_size < old_size || native_store_grow_table(
                    shared_table, new_size, error) != EXEC_OK) {
                free(bytes);
                if (error) {
                    error->status = EXEC_ERROR_TRAP;
                    snprintf(error->message, sizeof(error->message),
                             "cannot grow the function table for %.150s",
                             path);
                }
                return -POSIX_ENOMEM;
            }
            table_base = (uint32_t)old_size;
        }
    }

    /* Set up the library load context so native_load_module resolves
     * __memory_base and __table_base during import resolution. */
    store->library_load_ctx.active = 1;
    store->library_load_ctx.memory_base_global.value.type = WASM_VALTYPE_I32;
    store->library_load_ctx.memory_base_global.value.i32 =
        (int32_t)memory_base;
    store->library_load_ctx.memory_base_global.mutable_ = 0;
    store->library_load_ctx.table_base_global.value.type = WASM_VALTYPE_I32;
    store->library_load_ctx.table_base_global.value.i32 = (int32_t)table_base;
    store->library_load_ctx.table_base_global.mutable_ = 0;
    /* The stack pointer comes from the main module's exported
     * __stack_pointer global.  Look it up from the first linked engine
     * in the process capsule. */
    store->library_load_ctx.stack_pointer = NULL;
    if (capsule->engine) {
        exec_global *sp = NULL;
        exec_error ignored = {0};
        if (exec_find_export_global(capsule->engine, "__stack_pointer",
                                    &sp, &ignored) == EXEC_OK)
            store->library_load_ctx.stack_pointer = sp;
    }
    if (!store->library_load_ctx.stack_pointer) {
        native_linked_module *runtime = native_registered_module(store, "waste-runtime");
        exec_global *sp = NULL;
        exec_error ignored = {0};
        if (runtime && exec_find_export_global(native_store_process_engine(
                store, runtime->engine), "__stack_pointer", &sp, &ignored) == EXEC_OK)
            store->library_load_ctx.stack_pointer = sp;
    }

    /* Load the module through the normal resolution pipeline. */
    waste_exec_engine *lib_engine = NULL;
    exec_status status = native_load_module(store, NULL, bytes, size,
                                            &lib_engine, error);
    store->library_load_ctx.active = 0;
    free(bytes);

    if (status != EXEC_OK) {
        if (error && !error->message[0]) {
            error->status = status;
            snprintf(error->message, sizeof(error->message),
                     "cannot instantiate shared library %.160s", path);
        }
        if (lib_engine) exec_free(lib_engine);
        return -POSIX_ENOEXEC;
    }

    /* wasm-ld PIC modules initialize passive data in their start function,
     * then expose a second phase that rebases pointer-bearing data after the
     * loader has supplied __memory_base and patched GOT globals.  This must
     * run before constructors consume any of those pointers. */
    uint32_t relocs_idx;
    if (exec_find_export(lib_engine, "__wasm_apply_data_relocs",
                         &relocs_idx, NULL) == EXEC_OK) {
        exec_error reloc_error = {0};
        exec_status reloc_status = exec_invoke(
            lib_engine, relocs_idx, NULL, 0, NULL, 0, &reloc_error);
        if (reloc_status != EXEC_OK) {
            if (error) {
                *error = reloc_error;
                if (!error->message[0])
                    snprintf(error->message, sizeof(error->message),
                             "shared-library relocation failed for %.140s",
                             path);
            }
            exec_free(lib_engine);
            return -POSIX_ENOEXEC;
        }
    }

    /* Run __wasm_call_ctors if exported. */
    uint32_t ctors_idx;
    if (exec_find_export(lib_engine, "__wasm_call_ctors",
                         &ctors_idx, NULL) == EXEC_OK) {
        exec_error ctor_error = {0};
        exec_status ctor_status = exec_invoke(
            lib_engine, ctors_idx, NULL, 0, NULL, 0, &ctor_error);
        if (ctor_status != EXEC_OK) {
            if (error) {
                *error = ctor_error;
                if (!error->message[0])
                    snprintf(error->message, sizeof(error->message),
                             "shared-library constructor failed for %.140s",
                             path);
            }
            exec_free(lib_engine);
            return -POSIX_ENOEXEC;
        }
    }

    /* Extract the library name from the path for registration. */
    const char *basename = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') basename = p + 1;
    char reg_name[WAST_MAX_EXPORT_NAME];
    snprintf(reg_name, sizeof(reg_name), "%s", basename);
    /* Strip .so.wasm or .wasm suffix for the registration name. */
    size_t rlen = strlen(reg_name);
    if (rlen > 8 && strcmp(reg_name + rlen - 8, ".so.wasm") == 0)
        reg_name[rlen - 8] = '\0';
    else if (rlen > 5 && strcmp(reg_name + rlen - 5, ".wasm") == 0)
        reg_name[rlen - 5] = '\0';

    /* Register the library engine in the store so subsequent modules
     * can import from it.  native_store_add takes wast_module pointers;
     * inline the registration to avoid synthesizing that struct. */
    if (store->module_count == store->module_capacity) {
        int next_cap = store->module_capacity ?
                       store->module_capacity * 2 : 16;
        native_linked_module *next = realloc(
            store->modules, (size_t)next_cap * sizeof(*next));
        if (!next) return -POSIX_ENOMEM;
        store->modules = next;
        store->module_capacity = next_cap;
    }
    native_linked_module *linked =
        &store->modules[store->module_count++];
    memset(linked, 0, sizeof(*linked));
    linked->engine = lib_engine;
    linked->module = NULL;
    snprintf(linked->id, sizeof(linked->id), "%s", reg_name);
    snprintf(linked->registered, sizeof(linked->registered), "%s",
             reg_name);

    /* Record in the capsule's loaded library list. */
    native_loaded_library *lib =
        &capsule->loaded_libraries[capsule->loaded_library_count++];
    snprintf(lib->name, sizeof(lib->name), "%s", reg_name);
    snprintf(lib->path, sizeof(lib->path), "%s", path);
    lib->engine = lib_engine;
    lib->memory_base = memory_base;
    lib->table_base = table_base;
    lib->memory_size = dylink.memory_size;
    lib->table_size = dylink.table_size;
    lib->ref_count = 1;
    lib->initialized = 1;
    return 0;
}

int native_store_load_library(native_store *store, const char *path,
                              exec_error *error) {
    if (!store) return -POSIX_EINVAL;
    /* Module starts, relocations and constructors form one synchronous
     * loader transaction. A pump yield cannot resume this C call's local
     * state. Keep timeout/cancellation polling active, but defer cooperative
     * event-loop yields until the import has returned to its caller. */
    uint64_t quantum = store->execution_control.pump_quantum_ns;
    store->execution_control.pump_quantum_ns = 0;
    int status = native_store_load_library_sync(store, path, error);
    store->execution_control.pump_quantum_ns = quantum;
    return status;
}

static void native_store_run_library_dtors(waste_exec_engine *engine) {
    if (!engine) return;
    uint32_t dtors_idx;
    exec_error dtor_error = {0};
    if (exec_find_export(engine, "__wasm_call_dtors",
                         &dtors_idx, NULL) == EXEC_OK)
        (void)exec_invoke(engine, dtors_idx, NULL, 0, NULL, 0,
                          &dtor_error);
}

static int native_store_unregister_engine(native_store *store,
                                          waste_exec_engine *engine) {
    int removed = 0;
    if (!store || !engine) return 0;
    for (int i = 0; i < store->module_count;) {
        if (store->modules[i].engine != engine) {
            i++;
            continue;
        }
        if (i + 1 < store->module_count)
            memmove(&store->modules[i], &store->modules[i + 1],
                    (size_t)(store->module_count - i - 1) *
                        sizeof(*store->modules));
        store->module_count--;
        memset(&store->modules[store->module_count], 0,
               sizeof(*store->modules));
        removed = 1;
    }
    return removed;
}

void native_store_release_process_libraries(
        native_store *store, native_process_capsule *capsule) {
    if (!store || !capsule) return;
    for (uint32_t i = capsule->loaded_library_count; i > 0; i--) {
        native_loaded_library *lib = &capsule->loaded_libraries[i - 1];
        waste_exec_engine *engine = lib->engine;
        if (!engine) continue;
        native_store_run_library_dtors(engine);
        /* Process exit releases its complete address space even though an
         * ordinary dlclose currently leaves virtual ranges reserved.  DSO
         * engines import the process memory, so explicitly remove their data
         * mappings before the process graph is destroyed or cloned again. */
        if (lib->memory_size && engine->memory) {
            uint64_t first_page = lib->memory_base / EXEC_PAGE_SIZE;
            uint64_t last_byte = (uint64_t)lib->memory_base +
                                 lib->memory_size;
            uint64_t last_page = (last_byte + EXEC_PAGE_SIZE - 1) /
                                 EXEC_PAGE_SIZE;
            if (last_page > first_page)
                (void)exec_memory_unmap_pages(
                    engine->memory, first_page, last_page - first_page, NULL);
        }
        /* A provider loaded by this process is registered with its exact
         * engine pointer.  Inherited providers point at graph clones instead;
         * native_process_capsule_destroy owns and releases those clones. */
        if (native_store_unregister_engine(store, engine))
            exec_free(engine);
        lib->engine = NULL;
        lib->ref_count = 0;
        lib->initialized = 0;
        lib->name[0] = '\0';
        lib->path[0] = '\0';
    }
    capsule->loaded_library_count = 0;
}

int native_store_unload_library(native_store *store, uint32_t index) {
    if (!store) return -POSIX_EINVAL;
    native_process_capsule *capsule = native_store_active_capsule(store);
    if (!capsule) return -POSIX_EINVAL;
    if (index >= capsule->loaded_library_count) return -POSIX_EINVAL;
    native_loaded_library *lib = &capsule->loaded_libraries[index];
    if (!lib->engine || !lib->ref_count) return -POSIX_EINVAL;
    if (lib->ref_count > 1) { lib->ref_count--; return 0; }

    waste_exec_engine *engine = lib->engine;

    /* Run __wasm_call_dtors if the library exports a destructor. */
    native_store_run_library_dtors(engine);

    /* Remove the store module registration so future import resolution
     * no longer finds this library's exports. */
    (void)native_store_unregister_engine(store, engine);

    /* A dlsym result is a borrowed table reference. Do not leave a freed
     * function owner reachable by indirect calls or the next fork clone. */
    exec_table *table = capsule->engine && capsule->engine->table_count ?
        capsule->engine->tables[0] : NULL;
    if (table) {
        for (uint64_t i = 0; i < table->size; i++) {
            if (table->elements[i].owner == engine) {
                table->elements[i].owner = NULL;
                table->elements[i].func_idx = 0;
            }
        }
    }

    /* Free the engine.  Memory/table regions remain allocated — reclaiming
     * regions from the middle of linear memory is not yet supported. */
    for (uint32_t i = 0; i < capsule->linked_engine_count; i++) {
        if (capsule->linked_engines[i] == engine)
            capsule->linked_engines[i] = NULL;
    }
    if (engine) exec_free(engine);

    /* Mark the slot as unused.  The slot is not compacted so that existing
     * dlopen handles (1-based indices) remain stable for other callers. */
    lib->ref_count = 0;
    lib->engine = NULL;
    lib->name[0] = '\0';
    lib->path[0] = '\0';
    return 0;
}
