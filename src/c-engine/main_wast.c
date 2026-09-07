#include "wast_types.h"
#include "wast_encode.h"
#include "waste_exec.h"
#include "wast_runner.h"
#include "wast_general.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *basename_simple(const char *path) {
    const char *last = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') last = p + 1;
    return last;
}

/* Emit a JSON string with escaping */
static void json_string(const char *s) {
    putchar('"');
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"') fputs("\\\"", stdout);
        else if (ch == '\\') fputs("\\\\", stdout);
        else if (ch == '\n') fputs("\\n", stdout);
        else if (ch == '\r') fputs("\\r", stdout);
        else if (ch == '\t') fputs("\\t", stdout);
        else if (ch < 0x20 || ch >= 0x80) printf("\\u%04x", (unsigned)ch);
        else putchar((int)ch);
    }
    putchar('"');
}

typedef struct {
    waste_exec_engine *engine;
    uint32_t func_idx;
} native_linked_func;

typedef struct native_call_block {
    native_linked_func *calls;
    struct native_call_block *next;
} native_call_block;

typedef struct {
    waste_exec_engine *engine;
    const wast_module *module;
    char id[WAST_MAX_EXPORT_NAME];
    char registered[WAST_MAX_EXPORT_NAME];
} native_linked_module;

typedef struct {
    native_linked_module *modules;
    int module_count;
    int module_capacity;
    native_call_block *call_blocks;
    waste_exec_engine **orphan_engines;
    int orphan_count;
    int orphan_capacity;
    exec_memory spectest_memory;
    exec_table spectest_table;
    exec_global spectest_i32;
    exec_global spectest_i64;
    exec_global spectest_f32;
    exec_global spectest_f64;
} native_store;

static exec_status native_linked_call(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error) {
    native_linked_func *function = (native_linked_func *)data;
    return exec_invoke(function->engine, function->func_idx, args, arg_count,
                       results, result_count, error);
}

static exec_status native_spectest_noop(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error) {
    (void)data;
    (void)args;
    (void)arg_count;
    (void)results;
    (void)error;
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

static void native_store_init(native_store *store) {
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

static void native_store_free(native_store *store) {
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

static int native_store_keep_orphan(native_store *store,
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

static native_linked_module *native_registered_module(native_store *store,
                                                       const char *name) {
    for (int i = store->module_count; i > 0; i--)
        if (strcmp(store->modules[i - 1].registered, name) == 0)
            return &store->modules[i - 1];
    return NULL;
}

static waste_exec_engine *native_selected_engine(native_store *store,
                                                  const char *id) {
    if (!id || !id[0])
        return store->module_count ?
               store->modules[store->module_count - 1].engine : NULL;
    for (int i = store->module_count; i > 0; i--)
        if (strcmp(store->modules[i - 1].id, id) == 0)
            return store->modules[i - 1].engine;
    return NULL;
}

static int native_store_add(native_store *store, waste_exec_engine *engine,
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

static const wast_module *native_find_definition(const wast_script *script,
                                                 int before_group,
                                                 const char *id) {
    for (int i = before_group - 1; i >= 0; i--) {
        const wast_module *module = &script->groups[i].module;
        if (module->is_definition && strcmp(module->id, id) == 0)
            return module;
    }
    return NULL;
}

static exec_global *native_spectest_global(native_store *store,
                                           const char *name) {
    if (strcmp(name, "global_i32") == 0) return &store->spectest_i32;
    if (strcmp(name, "global_i64") == 0) return &store->spectest_i64;
    if (strcmp(name, "global_f32") == 0) return &store->spectest_f32;
    if (strcmp(name, "global_f64") == 0) return &store->spectest_f64;
    return NULL;
}

static const wast_tag *native_find_exported_tag(
        const native_linked_module *provider, const char *name) {
    if (!provider || !provider->module) return NULL;
    /* A single tag may have multiple standalone exports.  The tag metadata's
     * convenience export_name can retain only one of them, so use the module
     * export table as the authoritative mapping. */
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

static exec_status native_load_module(native_store *store,
                                      const wast_module *module,
                                      const uint8_t *bytes, size_t size,
                                      waste_exec_engine **engine_out,
                                      exec_error *error) {
    /* Tags are not executable yet, but their imports still participate in
     * ordinary module linking and must match an exported tag signature. */
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

    exec_host_import *functions = calloc(function_count, sizeof(*functions));
    exec_global_import *globals = calloc(global_count, sizeof(*globals));
    exec_memory_import *memories = calloc(memory_count, sizeof(*memories));
    exec_table_import *tables = calloc(table_count, sizeof(*tables));
    exec_tag_import *tags = calloc(tag_count, sizeof(*tags));
    native_call_block *call_block = calloc(1, sizeof(*call_block));
    if (function_count) call_block->calls = calloc(function_count, sizeof(*call_block->calls));
    if ((function_count && (!functions || !call_block->calls)) ||
        (global_count && !globals) || (memory_count && !memories) ||
        (table_count && !tables) || (tag_count && !tags) || !call_block) {
        free(functions); free(globals); free(memories); free(tables); free(tags);
        if (call_block) { free(call_block->calls); free(call_block); }
        error->status = EXEC_ERROR_FORMAT;
        snprintf(error->message, sizeof(error->message), "out of memory linking module");
        return EXEC_ERROR_FORMAT;
    }

    size_t nf = 0, ng = 0, nm = 0, nt = 0, ntag = 0;
    for (int i = 0; i < module->func_count; i++) if (module->funcs[i].is_import) {
        const wast_func *function = &module->funcs[i];
        native_linked_module *provider = native_registered_module(
            store, function->import_module);
        functions[nf].module = function->import_module;
        functions[nf].name = function->import_name;
        if (!provider && strcmp(function->import_module, "spectest") == 0 &&
            native_spectest_has_function(function->import_name)) {
            functions[nf].function = native_spectest_noop;
        } else if (provider) {
            uint32_t index = 0, type_index = 0;
            exec_status status = exec_find_export(
                provider->engine, function->import_name, &index, error);
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

    {
        exec_imports imports = {functions, nf, globals, ng,
                                memories, nm, tables, nt, tags, ntag};
        exec_status status = exec_load_with_imports(bytes, size, &imports,
                                                    engine_out, error);
        if (status != EXEC_OK) goto fail;
    }
    free(functions); free(globals); free(memories); free(tables); free(tags);
    if (function_count) {
        call_block->next = store->call_blocks;
        store->call_blocks = call_block;
    } else {
        free(call_block);
    }
    return EXEC_OK;

fail:
    free(functions); free(globals); free(memories); free(tables); free(tags);
    free(call_block->calls); free(call_block);
    return error->status;
}

static uint8_t *encode_group_module(const wast_group *group, size_t *size_out,
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
        static const char prefix[] = "(module ";
        size_t length = group->raw_module.length;
        if (length > SIZE_MAX - sizeof(prefix) - 1) {
            snprintf(error, 256, "quoted module is too large");
            return NULL;
        }
        size_t source_length = sizeof(prefix) - 1 + length + 1;
        char *source = malloc(source_length);
        if (!source) {
            snprintf(error, 256, "out of memory copying quoted module");
            return NULL;
        }
        memcpy(source, prefix, sizeof(prefix) - 1);
        if (length)
            memcpy(source + sizeof(prefix) - 1,
                   group->raw_module.bytes, length);
        source[source_length - 1] = ')';
        uint8_t *wasm = NULL;
        if (waste_wat_compile(source, source_length, &wasm, size_out,
                              error, 256) != 0)
            wasm = NULL;
        free(source);
        return wasm;
    }
    return wast_encode_module(&group->module, size_out, error);
}

/* ---- normal run mode ---- */

static int run_normal(const char *path) {
    const char *filename = basename_simple(path);

    wast_script *script = (wast_script *)calloc(1, sizeof(*script));
    if (!script) return 1;
    int parse_rc = wast_parse_file(path, script);
    if (parse_rc != 0) {
        fprintf(stderr, "parse error in %s: %s\n", path, script->error);
        printf("{\"file\":");
        json_string(filename);
        printf(",\"error\":");
        json_string(script->error);
        printf(",\"assertions\":[],\"passed\":0,\"total\":0}\n");
        wast_script_free(script); free(script); return 1;
    }

    int total_passed = 0;
    int total_count  = 0;
    int first_assertion = 1;
    native_store store;
    native_store_init(&store);

    printf("{\"file\":");
    json_string(filename);
    printf(",\"assertions\":[\n");

    for (int g = 0; g < script->group_count; g++) {
        wast_group *group = &script->groups[g];

        /* Definitions are templates.  A module instance below encodes and
         * instantiates the definition afresh, which gives its globals,
         * tables, memories, and tags distinct identities. */
        if (group->module.is_definition) continue;

        const wast_module *load_module = &group->module;
        if (group->module.instance_of[0]) {
            load_module = native_find_definition(
                script, g, group->module.instance_of);
            if (!load_module) {
                fprintf(stderr, "unknown module definition %s\n",
                        group->module.instance_of);
                continue;
            }
        }

        if (group->has_module_assertion && group->has_validation_error) {
            int ok = group->module_assert_kind == WAST_ASSERT_INVALID ||
                     group->module_assert_kind == WAST_ASSERT_MALFORMED;
            if (!first_assertion) printf(",\n");
            first_assertion = 0;
            printf("{\"index\":%d,\"func\":\"(module)\",\"pass\":%s,\"error\":",
                   total_count, ok ? "true" : "false");
            if (ok) printf("null"); else json_string(group->validation_error);
            printf("}");
            total_count++;
            if (ok) total_passed++;
            continue;
        }

        char encode_error[256] = {0};
        size_t bin_size = 0;
        wast_group encode_group = *group;
        encode_group.module = *load_module;
        uint8_t *bin = encode_group_module(&encode_group, &bin_size,
                                           encode_error);
        if (!bin) {
            fprintf(stderr, "encode error (group %d): %s\n", g, encode_error);
            if (group->has_module_assertion) {
                int ok = group->module_assert_kind != WAST_ASSERT_TRAP;
                if (!first_assertion) printf(",\n");
                first_assertion = 0;
                printf("{\"index\":%d,\"func\":\"(module)\",\"pass\":%s,\"error\":",
                       total_count, ok ? "true" : "false");
                if (ok) printf("null"); else json_string(encode_error);
                printf("}");
                total_count++;
                if (ok) total_passed++;
                continue;
            }
            for (int i = 0; i < group->assertion_count; i++) {
                if (!first_assertion) printf(",\n");
                first_assertion = 0;
                printf("{\"index\":%d,\"func\":", total_count + i);
                json_string(script->assertions[group->assertion_start + i].func_name);
                printf(",\"pass\":false,\"error\":");
                json_string(encode_error);
                printf("}");
            }
            total_count += group->assertion_count;
            continue;
        }

        waste_exec_engine *engine = NULL;
        exec_error exec_err;
        memset(&exec_err, 0, sizeof(exec_err));
        exec_status st = native_load_module(&store, load_module,
                                            bin, bin_size, &engine, &exec_err);
        free(bin);
        if (group->has_module_assertion) {
            int ok = group->module_assert_kind == WAST_ASSERT_TRAP ?
                st == EXEC_ERROR_TRAP : st != EXEC_OK;
            if (!first_assertion) printf(",\n");
            first_assertion = 0;
            printf("{\"index\":%d,\"func\":\"(module)\",\"pass\":%s,\"error\":",
                   total_count, ok ? "true" : "false");
            if (ok) printf("null");
            else json_string(st == EXEC_OK ? "module unexpectedly instantiated" :
                             exec_err.message);
            printf("}");
            total_count++;
            if (ok) total_passed++;
            if (engine && !native_store_keep_orphan(&store, engine)) {
                /* Preserve any funcrefs installed into imported tables even
                 * if the lifetime bookkeeping itself cannot grow. */
            }
            continue;
        }
        if (st != EXEC_OK) {
            fprintf(stderr, "load error (group %d): %s\n", g, exec_err.message);
            for (int i = 0; i < group->assertion_count; i++) {
                if (!first_assertion) printf(",\n");
                first_assertion = 0;
                printf("{\"index\":%d,\"func\":", total_count + i);
                json_string(script->assertions[group->assertion_start + i].func_name);
                printf(",\"pass\":false,\"error\":");
                json_string(exec_err.message);
                printf("}");
            }
            total_count += group->assertion_count;
            continue;
        }

        if (!native_store_add(&store, engine, &group->module, load_module)) {
            exec_free(engine);
            engine = NULL;
            snprintf(exec_err.message, sizeof(exec_err.message),
                     "out of memory retaining module instance");
            for (int i = 0; i < group->assertion_count; i++) {
                if (!first_assertion) printf(",\n");
                first_assertion = 0;
                printf("{\"index\":%d,\"func\":", total_count + i);
                json_string(script->assertions[group->assertion_start + i].func_name);
                printf(",\"pass\":false,\"error\":");
                json_string(exec_err.message);
                printf("}");
            }
            total_count += group->assertion_count;
            continue;
        }

        for (int i = 0; i < group->assertion_count; i++) {
            const wast_assertion *a = &script->assertions[group->assertion_start + i];
            waste_exec_engine *selected = native_selected_engine(&store,
                                                                  a->module_id);
            exec_error aerr;
            memset(&aerr, 0, sizeof(aerr));
            exec_status ast;
            if (!selected) {
                aerr.status = EXEC_ERROR_NOT_FOUND;
                snprintf(aerr.message, sizeof(aerr.message),
                         "unknown module id");
                ast = EXEC_ERROR_NOT_FOUND;
            } else {
                ast = wast_run_assertion(selected, a, &aerr);
            }
            int ok = (ast == EXEC_OK);
            if (ok) total_passed++;

            if (!first_assertion) printf(",\n");
            first_assertion = 0;

            printf("{\"index\":%d,\"func\":", total_count + i);
            json_string(a->func_name);
            printf(",\"pass\":%s,\"error\":", ok ? "true" : "false");
            if (ok || aerr.message[0] == '\0') {
                printf("null");
            } else {
                json_string(aerr.message);
            }
            printf("}");
        }
        total_count += group->assertion_count;
    }

    printf("\n],\"passed\":%d,\"total\":%d}\n", total_passed, total_count);
    native_store_free(&store);
    wast_script_free(script); free(script);
    return (total_passed == total_count) ? 0 : 1;
}

/* ---- browser-spec mode ---- */

/*
 * Emit a single wasm_value as a JSON object:
 * {"type":N,"data":[0,...,0],"nan_mode":[0,...,0]}
 * data is always 16 bytes (little-endian for scalars, raw for v128).
 */
static void json_value_spec(const wasm_value *v) {
    uint8_t data[16] = {0};
    switch (v->type) {
        case WASM_VALTYPE_I32: {
            uint32_t tmp; memcpy(&tmp, &v->i32, 4);
            data[0]=(uint8_t)(tmp);     data[1]=(uint8_t)(tmp>>8);
            data[2]=(uint8_t)(tmp>>16); data[3]=(uint8_t)(tmp>>24);
            break;
        }
        case WASM_VALTYPE_I64: {
            uint64_t tmp; memcpy(&tmp, &v->i64, 8);
            for (int k = 0; k < 8; k++) data[k] = (uint8_t)(tmp >> (k*8));
            break;
        }
        case WASM_VALTYPE_F32: {
            uint32_t tmp; memcpy(&tmp, &v->f32, 4);
            data[0]=(uint8_t)(tmp);     data[1]=(uint8_t)(tmp>>8);
            data[2]=(uint8_t)(tmp>>16); data[3]=(uint8_t)(tmp>>24);
            break;
        }
        case WASM_VALTYPE_F64: {
            uint64_t tmp; memcpy(&tmp, &v->f64, 8);
            for (int k = 0; k < 8; k++) data[k] = (uint8_t)(tmp >> (k*8));
            break;
        }
        case WASM_VALTYPE_V128:
            memcpy(data, v->v128.bytes, 16);
            break;
        case WASM_VALTYPE_FUNCREF:
        case WASM_VALTYPE_EXTERNREF:
        case WASM_VALTYPE_FUNCREF_NONNULL:
        case WASM_VALTYPE_EXTERNREF_NONNULL:
        case WASM_VALTYPE_ANYREF:
        case WASM_VALTYPE_EQREF:
        case WASM_VALTYPE_I31REF:
        case WASM_VALTYPE_STRUCTREF:
        case WASM_VALTYPE_ARRAYREF:
        case WASM_VALTYPE_ANYREF_NONNULL:
        case WASM_VALTYPE_EQREF_NONNULL:
        case WASM_VALTYPE_I31REF_NONNULL:
        case WASM_VALTYPE_STRUCTREF_NONNULL:
        case WASM_VALTYPE_ARRAYREF_NONNULL:
        case WASM_VALTYPE_EXNREF:
        case WASM_VALTYPE_EXNREF_NONNULL:
        case WASM_VALTYPE_NULLREF:
        case WASM_VALTYPE_NULLFUNCREF:
        case WASM_VALTYPE_NULLEXNREF:
        case WASM_VALTYPE_NULLEXTERNREF: {
            uint32_t tmp = v->ref;
            data[0]=(uint8_t)tmp; data[1]=(uint8_t)(tmp>>8);
            data[2]=(uint8_t)(tmp>>16); data[3]=(uint8_t)(tmp>>24);
            break;
        }
    }

    printf("{\"type\":%d,\"data\":[", (int)v->type);
    for (int i = 0; i < 16; i++) {
        if (i) putchar(',');
        printf("%d", (int)data[i]);
    }
    printf("],\"nan_mode\":[");
    for (int i = 0; i < 16; i++) {
        if (i) putchar(',');
        printf("%d", (int)v->nan_mode[i]);
    }
    printf("]}");
}

static int run_browser_spec(const char *path) {
    const char *filename = basename_simple(path);

    wast_script *script = (wast_script *)calloc(1, sizeof(*script));
    if (!script) return 1;
    int parse_rc = wast_parse_file(path, script);
    if (parse_rc != 0 && script->group_count == 0) {
        fprintf(stderr, "parse error in %s: %s\n", path, script->error);
        printf("{\"file\":");
        json_string(filename);
        printf(",\"error\":");
        json_string(script->error);
        printf(",\"groups\":[]}\n");
        wast_script_free(script); free(script); return 1;
    }
    if (parse_rc != 0) {
        fprintf(stderr, "partial parse of %s: %s (emitting %d groups)\n",
                path, script->error, script->group_count);
    }

    printf("{\"file\":");
    json_string(filename);
    printf(",\"groups\":[\n");

    for (int g = 0; g < script->group_count; g++) {
        if (g) printf(",\n");
        wast_group *group = &script->groups[g];

        char encode_error[256] = {0};
        size_t bin_size = 0;
        uint8_t *bin = encode_group_module(group, &bin_size, encode_error);

        printf("{\"id\":");
        json_string(group->module.id);
        printf(",\"register\":");
        json_string(group->module.register_name);
        printf(",\"module_assertion\":");
        if (group->has_module_assertion) {
            printf("{\"kind\":%d,\"expected\":", (int)group->module_assert_kind);
            json_string(group->expected_module_error);
            printf(",\"validation_error\":");
            if (group->has_validation_error) json_string(group->validation_error);
            else printf("null");
            putchar('}');
        } else {
            printf("null");
        }
        printf(",\"module_hex\":\"");
        if (bin) {
            for (size_t i = 0; i < bin_size; i++) printf("%02x", bin[i]);
            free(bin);
        }
        printf("\",\"assertions\":[\n");

        for (int i = 0; i < group->assertion_count; i++) {
            const wast_assertion *a = &script->assertions[group->assertion_start + i];
            if (i) printf(",\n");
            printf("{\"func\":");
            json_string(a->func_name);
            printf(",\"action\":");
            json_string(a->action_kind == WAST_ACTION_GET ? "get" : "invoke");
            printf(",\"kind\":%d", (int)a->kind);
            printf(",\"module\":");
            json_string(a->module_id);
            printf(",\"args\":[");
            for (int j = 0; j < a->arg_count; j++) {
                if (j) putchar(',');
                json_value_spec(&a->args[j]);
            }
            printf("],\"alts\":[");
            for (int alt = 0; alt < a->alt_count; alt++) {
                if (alt) putchar(',');
                putchar('[');
                for (int r = 0; r < a->result_count; r++) {
                    if (r) putchar(',');
                    json_value_spec(&a->alternatives[alt][r]);
                }
                putchar(']');
            }
            printf("]}");
        }
        printf("\n]}");
    }

    printf("\n]}\n");
    wast_script_free(script); free(script);
    return 0;
}

/* ---- detect whether a file needs the general interpreter ----
   Files with SIMD keywords go through the flex/bison path;
   everything else goes through the general WAT interpreter.      */

/* ---- entry point ---- */

int main(int argc, char *argv[]) {
    if (argc == 3 && strcmp(argv[1], "--browser-spec") == 0)
        return run_browser_spec(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--general") == 0)
        return wast_general_run(argv[2]);
    if (argc == 2) {
        return run_normal(argv[1]);
    }
    fprintf(stderr, "usage: %s [--browser-spec|--general] <file.wast>\n", argv[0]);
    return 1;
}
