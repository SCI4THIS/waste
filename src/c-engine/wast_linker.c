#include "wast_linker.h"
#include "wast_encode.h"
#include "wast_runner.h"

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

/* ---- tag helpers ---- */

static const wast_tag *native_find_exported_tag(
        const native_linked_module *provider, const char *name) {
    if (!provider || !provider->module) return NULL;
    for (int i = 0; i < provider->module->export_count; i++) {
        const wast_export *export_ = &provider->module->exports[i];
        if (export_->kind == 4 &&
            export_->index < (uint32_t)provider->module->tag_count &&
            strcmp(export_->name, name) == 0)
            return &provider->module->tags[export_->index];
    }
    for (int i = 0; i < provider->module->tag_count; i++) {
        const wast_tag *tag = &provider->module->tags[i];
        if (tag->has_export_name && strcmp(tag->export_name, name) == 0)
            return tag;
    }
    return NULL;
}

static int native_tag_signature_matches(const wast_tag *left,
                                         const wast_tag *right) {
    return left->param_count == right->param_count &&
           memcmp(left->params, right->params,
                  (size_t)left->param_count * sizeof(left->params[0])) == 0;
}

/* ---- binary import scanner ---- */

#define LINKER_MAX_IMPORTS 512

typedef struct { const uint8_t *p, *end; } bin_reader;
typedef struct {
    char module[WAST_MAX_EXPORT_NAME];
    char name[WAST_MAX_EXPORT_NAME];
    uint8_t kind;
} import_request;

static int read_u8(bin_reader *r, uint8_t *v) {
    if (r->p >= r->end) return 0;
    *v = *r->p++;
    return 1;
}

static int read_leb(bin_reader *r, uint32_t *v) {
    uint32_t out = 0;
    int shift = 0;
    uint8_t b;
    do {
        if (shift >= 35 || !read_u8(r, &b)) return 0;
        out |= (uint32_t)(b & 0x7f) << shift;
        shift += 7;
    } while (b & 0x80);
    *v = out;
    return 1;
}

static int read_leb64(bin_reader *r, uint64_t *v) {
    uint64_t out = 0;
    int shift = 0;
    uint8_t b;
    do {
        if (shift >= 70 || !read_u8(r, &b)) return 0;
        if (shift == 63 && (b & 0xfeu)) return 0;
        out |= (uint64_t)(b & 0x7f) << shift;
        shift += 7;
    } while (b & 0x80);
    *v = out;
    return 1;
}

static int read_name(bin_reader *r, char *out) {
    uint32_t n;
    if (!read_leb(r, &n) || n >= WAST_MAX_EXPORT_NAME ||
        (size_t)(r->end - r->p) < n)
        return 0;
    memcpy(out, r->p, n);
    out[n] = '\0';
    r->p += n;
    return 1;
}

static int skip_limits(bin_reader *r) {
    uint32_t flags;
    uint64_t value;
    if (!read_leb(r, &flags) || !read_leb64(r, &value)) return 0;
    if (flags & 1u) return read_leb64(r, &value);
    return 1;
}

static int skip_valtype(bin_reader *r) {
    uint8_t type, byte;
    if (!read_u8(r, &type)) return 0;
    if (type != 0x63 && type != 0x64) return 1;
    do { if (!read_u8(r, &byte)) return 0; } while (byte & 0x80);
    return 1;
}

static int scan_imports(const uint8_t *bytes, size_t size,
                         import_request *req, uint32_t *count) {
    bin_reader r = {bytes, bytes + size};
    uint32_t section_size, n;
    if (size < 8) return 0;
    r.p += 8;
    while (r.p < r.end) {
        uint8_t id;
        if (!read_u8(&r, &id) || !read_leb(&r, &section_size) ||
            (size_t)(r.end - r.p) < section_size)
            return 0;
        bin_reader s = {r.p, r.p + section_size};
        r.p += section_size;
        if (id != 2) continue;
        if (!read_leb(&s, &n) || n > LINKER_MAX_IMPORTS) return 0;
        for (uint32_t i = 0; i < n; i++) {
            uint8_t kind;
            if (*count >= LINKER_MAX_IMPORTS ||
                !read_name(&s, req[*count].module) ||
                !read_name(&s, req[*count].name) ||
                !read_u8(&s, &kind))
                return 0;
            req[*count].kind = kind;
            (*count)++;
            if (kind == 0) {
                uint32_t ignored;
                if (!read_leb(&s, &ignored)) return 0;
            } else if (kind == 1) {
                if (!skip_valtype(&s) || !skip_limits(&s)) return 0;
            } else if (kind == 2) {
                if (!skip_limits(&s)) return 0;
            } else if (kind == 3) {
                uint8_t mut;
                if (!skip_valtype(&s) || !read_u8(&s, &mut)) return 0;
            } else if (kind == 4) {
                uint32_t ignored;
                if (!read_leb(&s, &ignored)) return 0;
            } else {
                return 0;
            }
        }
        return s.p == s.end;
    }
    return 1;
}

/* ---- module loading ---- */

exec_status native_load_module(native_store *store,
                                const wast_module *module,
                                const uint8_t *bytes, size_t size,
                                waste_exec_engine **engine_out,
                                exec_error *error) {
    /* Resolve tag imports before loading so the executor can preserve tag
     * identity across module boundaries. */
    for (int i = 0; i < module->tag_count; i++) {
        const wast_tag *tag = &module->tags[i];
        if (!tag->is_import) continue;
        native_linked_module *provider = native_registered_module(
            store, tag->import_module);
        const wast_tag *provided = native_find_exported_tag(
            provider, tag->import_name);
        if (!provided) {
            error->status = EXEC_ERROR_NOT_FOUND;
            snprintf(error->message, sizeof(error->message),
                     "unresolved tag import %s.%s", tag->import_module,
                     tag->import_name);
            return error->status;
        }
        if (!native_tag_signature_matches(tag, provided)) {
            error->status = EXEC_ERROR_FORMAT;
            snprintf(error->message, sizeof(error->message),
                     "incompatible tag import type for %s.%s",
                     tag->import_module, tag->import_name);
            return error->status;
        }
    }

    size_t function_count = 0, global_count = 0;
    size_t memory_count = 0, table_count = 0, tag_count = 0;
    import_request *binary_imports = NULL;
    uint32_t binary_import_count = 0;

    for (int i = 0; i < module->func_count; i++)
        function_count += module->funcs[i].is_import != 0;
    for (int i = 0; i < module->global_count; i++)
        global_count += module->globals[i].is_import != 0;
    for (int i = 0; i < module->memory_count; i++)
        memory_count += module->memories[i].is_import != 0;
    for (int i = 0; i < module->table_count; i++)
        table_count += module->tables[i].is_import != 0;
    for (int i = 0; i < module->tag_count; i++)
        tag_count += module->tags[i].is_import != 0;

    /* A (module binary ...) deliberately bypasses the text module model,
     * so derive its imports from the embedded bytes.  The ordinary text path
     * keeps using its richer metadata (including typed tags). */
    if (function_count == 0 && global_count == 0 && memory_count == 0 &&
        table_count == 0 && tag_count == 0) {
        binary_imports = calloc(LINKER_MAX_IMPORTS, sizeof(*binary_imports));
        if (!binary_imports ||
            !scan_imports(bytes, size, binary_imports,
                          &binary_import_count)) {
            free(binary_imports);
            error->status = EXEC_ERROR_FORMAT;
            snprintf(error->message, sizeof(error->message),
                     "invalid binary module import section");
            return error->status;
        }
        for (uint32_t i = 0; i < binary_import_count; i++) {
            function_count += binary_imports[i].kind == 0;
            table_count += binary_imports[i].kind == 1;
            memory_count += binary_imports[i].kind == 2;
            global_count += binary_imports[i].kind == 3;
            tag_count += binary_imports[i].kind == 4;
        }
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
    if (function_count)
        call_block->calls = calloc(function_count, sizeof(*call_block->calls));
    if ((function_count && (!functions || !call_block->calls)) ||
        (global_count && !globals) || (memory_count && !memories) ||
        (table_count && !tables) || (tag_count && !tags) || !call_block) {
        free(functions); free(globals); free(memories); free(tables); free(tags);
        if (call_block) { free(call_block->calls); free(call_block); }
        free(binary_imports);
        error->status = EXEC_ERROR_FORMAT;
        snprintf(error->message, sizeof(error->message),
                 "out of memory linking module");
        return EXEC_ERROR_FORMAT;
    }

    size_t nf = 0, ng = 0, nm = 0, nt = 0, ntag = 0;

    /* ---- text-format imports ---- */
    for (int i = 0; i < module->func_count; i++) if (module->funcs[i].is_import) {
        const wast_func *function = &module->funcs[i];
        native_linked_module *provider = native_registered_module(
            store, function->import_module);
        functions[nf].module = function->import_module;
        functions[nf].name = function->import_name;
        if (!provider && strcmp(function->import_module, "spectest") == 0 &&
            native_spectest_has_function(function->import_name)) {
            functions[nf].function = native_spectest_noop;
        } else if (!provider && store->host_resolver) {
            native_host_binding binding;
            if (store->host_resolver(function->import_module,
                                      function->import_name,
                                      store->host_context, &binding)) {
                functions[nf].function = binding.function;
                functions[nf].host_data = binding.host_data;
                functions[nf].control = binding.control;
            }
        } else if (provider) {
            uint32_t index = 0, type_index = 0;
            exec_status status = exec_find_export(
                provider->engine, function->import_name, &index, error);
            if (status != EXEC_OK && store->host_resolver) {
                native_host_binding binding;
                if (store->host_resolver(function->import_module,
                                          function->import_name,
                                          store->host_context, &binding)) {
                    functions[nf].function = binding.function;
                    functions[nf].host_data = binding.host_data;
                    functions[nf].control = binding.control;
                    memset(error, 0, sizeof(*error));
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
    }
    for (int i = 0; i < module->global_count; i++) if (module->globals[i].is_import) {
        const wast_global *global = &module->globals[i];
        native_linked_module *provider = native_registered_module(
            store, global->import_module);
        exec_global *value = NULL;
        if (!provider && strcmp(global->import_module, "spectest") == 0)
            value = native_spectest_global(store, global->import_name);
        else if (provider) {
            exec_status status = exec_find_export_global(
                provider->engine, global->import_name, &value, error);
            if (status != EXEC_OK) goto fail;
        }
        globals[ng++] = (exec_global_import){global->import_module,
                                             global->import_name, value};
    }
    for (int i = 0; i < module->memory_count; i++) if (module->memories[i].is_import) {
        const wast_memory *memory = &module->memories[i];
        native_linked_module *provider = native_registered_module(
            store, memory->import_module);
        exec_memory *value = NULL;
        if (!provider && strcmp(memory->import_module, "spectest") == 0 &&
            strcmp(memory->import_name, "memory") == 0)
            value = &store->spectest_memory;
        else if (provider) {
            exec_status status = exec_find_export_memory(
                provider->engine, memory->import_name, &value, error);
            if (status != EXEC_OK) goto fail;
        }
        memories[nm++] = (exec_memory_import){memory->import_module,
                                              memory->import_name, value};
    }
    for (int i = 0; i < module->table_count; i++) if (module->tables[i].is_import) {
        const wast_table *table = &module->tables[i];
        native_linked_module *provider = native_registered_module(
            store, table->import_module);
        exec_table *value = NULL;
        if (!provider && strcmp(table->import_module, "spectest") == 0 &&
            strcmp(table->import_name, "table") == 0)
            value = &store->spectest_table;
        else if (provider) {
            exec_status status = exec_find_export_table(
                provider->engine, table->import_name, &value, error);
            if (status != EXEC_OK) goto fail;
        }
        tables[nt++] = (exec_table_import){table->import_module,
                                           table->import_name, value};
    }
    for (int i = 0; i < module->tag_count; i++) if (module->tags[i].is_import) {
        const wast_tag *tag = &module->tags[i];
        native_linked_module *provider = native_registered_module(
            store, tag->import_module);
        exec_tag *value = NULL;
        if (provider) {
            exec_status status = exec_find_export_tag(
                provider->engine, tag->import_name, &value, error);
            if (status != EXEC_OK) goto fail;
        }
        tags[ntag++] = (exec_tag_import){tag->import_module,
                                         tag->import_name, value};
    }

    /* ---- binary-format imports ---- */
    for (uint32_t i = 0; i < binary_import_count; i++) {
        import_request *request = &binary_imports[i];
        native_linked_module *provider = native_registered_module(
            store, request->module);
        if (request->kind == 0) {
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
                        memset(error, 0, sizeof(*error));
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
        } else if (request->kind == 1) {
            exec_table *value = provider ? NULL :
                (strcmp(request->module, "spectest") == 0 &&
                 strcmp(request->name, "table") == 0 ?
                 &store->spectest_table : NULL);
            if (provider && exec_find_export_table(
                    provider->engine, request->name, &value, error) != EXEC_OK)
                goto fail;
            tables[nt++] = (exec_table_import){request->module,
                                               request->name, value};
        } else if (request->kind == 2) {
            exec_memory *value = provider ? NULL :
                (strcmp(request->module, "spectest") == 0 &&
                 strcmp(request->name, "memory") == 0 ?
                 &store->spectest_memory : NULL);
            if (provider && exec_find_export_memory(
                    provider->engine, request->name, &value, error) != EXEC_OK)
                goto fail;
            memories[nm++] = (exec_memory_import){request->module,
                                                  request->name, value};
        } else if (request->kind == 3) {
            exec_global *value = provider ? NULL :
                (strcmp(request->module, "spectest") == 0 ?
                 native_spectest_global(store, request->name) : NULL);
            if (provider && exec_find_export_global(
                    provider->engine, request->name, &value, error) != EXEC_OK)
                goto fail;
            globals[ng++] = (exec_global_import){request->module,
                                                  request->name, value};
        } else if (request->kind == 4) {
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
        exec_status status = exec_load_with_imports(bytes, size, &imports,
                                                     engine_out, error);
        if (status != EXEC_OK) goto fail;
    }
    free(functions); free(globals); free(memories); free(tables); free(tags);
    free(binary_imports);
    if (function_count) {
        call_block->next = store->call_blocks;
        store->call_blocks = call_block;
    } else {
        free(call_block->calls);
        free(call_block);
    }
    return EXEC_OK;

fail:
    free(functions); free(globals); free(memories); free(tables); free(tags);
    free(binary_imports);
    free(call_block->calls); free(call_block);
    return error->status;
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
