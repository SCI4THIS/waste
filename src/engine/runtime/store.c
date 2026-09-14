#include "runtime/store.h"
#include "wasm/wasm_encode.h"
#include "script/wast_runner.h"
#include "wasm/wasm_decode.h"
#include "runtime/instantiate.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- cross-module call trampoline ---- */

static exec_status native_linked_call(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error) {
    native_linked_func *function = (native_linked_func *)data;
    return exec_invoke(function->engine, function->func_idx, args, arg_count,
                       results, result_count, error);
}

/* ---- spectest helpers ---- */

static exec_status native_spectest_noop(void *data, const wasm_value *args,
                                         int arg_count, wasm_value *results,
                                         int *result_count, exec_error *error) {
    (void)data; (void)args; (void)arg_count; (void)results; (void)error;
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
    store->spectest_memory.pages = 1;
    store->spectest_memory.max_pages = 2;
    store->spectest_memory.has_max = 1;
    store->spectest_memory.data = calloc(65536, 1);
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
}

void native_store_free(native_store *store) {
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
    memset(store, 0, sizeof(*store));
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
                if (status != EXEC_OK) goto fail;
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
