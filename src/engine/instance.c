#include "engine_internal.h"
#include "runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

exec_status exec_fail(exec_error *error, exec_status status, const char *msg) {
    if (error) {
        error->status = status;
        snprintf(error->message, sizeof(error->message), "%s", msg);
    }
    return status;
}

void exec_free(waste_exec_engine *engine) {
    if (!engine) return;
    runtime_free_jump_snapshots(engine);
    for (uint32_t i = 0; i < EXEC_MAX_CALL_DEPTH; i++) {
        free(engine->local_frames[i]);
        free(engine->operand_frames[i]);
        free(engine->control_frames[i]);
    }
    for (uint32_t i = 0; i < engine->elem_count; i++)
        free(engine->elem_values[i]);
    for (uint32_t i = 0; i < engine->data_count; i++)
        free(engine->data_segs[i]);
    for (uint32_t i = 0; i < engine->gc_object_count; i++)
        free(engine->gc_objects[i].values);
    free(engine->gc_objects);
    free(engine->exception_objects);
    for (uint32_t i = 0; i < engine->func_count; i++) {
        if (engine->funcs[i].code) {
            for (uint32_t j = 0; j < engine->funcs[i].code_size; j++) {
                if (engine->funcs[i].code[j].opcode == 0x0e) {
                    uint32_t *depths;
                    memcpy(&depths, engine->funcs[i].code[j].v128_imm.bytes,
                           sizeof(depths));
                    free(depths);
                }
                free(engine->funcs[i].code[j].catches);
            }
        }
        free(engine->funcs[i].code);
    }
    free(engine->types);
    free(engine->funcs);
    free(engine->exports);
    for (uint32_t i = 0; i < engine->memory_count; i++)
        if (engine->owns_memories[i]) free(engine->memories[i]->data);
    for (uint32_t i = engine->import_table_count; i < engine->table_count; i++)
        free(engine->owned_tables[i].elements);
    free(engine);
}

exec_status exec_find_export(const waste_exec_engine *engine,
                             const char *name, uint32_t *function_out,
                             exec_error *error) {
    if (!engine || !name || !function_out)
        return exec_fail(error, EXEC_ERROR_FORMAT, "null argument");
    for (uint32_t i = 0; i < engine->export_count; i++) {
        if (engine->exports[i].kind == 0 &&
            strcmp(engine->exports[i].name, name) == 0) {
            *function_out = engine->exports[i].index;
            return EXEC_OK;
        }
    }
    return exec_fail(error, EXEC_ERROR_NOT_FOUND, "export not found");
}

exec_status exec_get_func_type_index(const waste_exec_engine *engine,
                                     uint32_t function,
                                     uint32_t *type_out,
                                     exec_error *error) {
    if (!engine || !type_out ||
        function >= engine->import_func_count + engine->func_count)
        return exec_fail(error, EXEC_ERROR_FORMAT, "invalid function index");
    *type_out = function < engine->import_func_count ?
        engine->import_func_types[function] :
        engine->funcs[function - engine->import_func_count].type_index;
    return EXEC_OK;
}

static exec_status find_extern_export(const waste_exec_engine *engine,
                                      const char *name, uint8_t kind,
                                      uint32_t *index_out,
                                      exec_error *error) {
    if (!engine || !name || !index_out)
        return exec_fail(error, EXEC_ERROR_FORMAT, "null argument");
    for (uint32_t i = 0; i < engine->export_count; i++) {
        if (engine->exports[i].kind == kind &&
            strcmp(engine->exports[i].name, name) == 0) {
            *index_out = engine->exports[i].index;
            return EXEC_OK;
        }
    }
    return exec_fail(error, EXEC_ERROR_NOT_FOUND, "export not found");
}

exec_status exec_find_export_global(const waste_exec_engine *engine,
                                    const char *name, exec_global **global_out,
                                    exec_error *error) {
    uint32_t index = 0;
    if (!global_out)
        return exec_fail(error, EXEC_ERROR_FORMAT, "null argument");
    exec_status status = find_extern_export(engine, name, 3, &index, error);
    if (status == EXEC_OK) *global_out = engine->globals[index];
    return status;
}

exec_status exec_find_export_memory(const waste_exec_engine *engine,
                                    const char *name, exec_memory **memory_out,
                                    exec_error *error) {
    uint32_t index = 0;
    if (!memory_out)
        return exec_fail(error, EXEC_ERROR_FORMAT, "null argument");
    exec_status status = find_extern_export(engine, name, 2, &index, error);
    if (status == EXEC_OK) *memory_out = engine->memories[index];
    return status;
}

exec_status exec_find_export_table(const waste_exec_engine *engine,
                                   const char *name, exec_table **table_out,
                                   exec_error *error) {
    uint32_t index = 0;
    if (!table_out)
        return exec_fail(error, EXEC_ERROR_FORMAT, "null argument");
    exec_status status = find_extern_export(engine, name, 1, &index, error);
    if (status == EXEC_OK) *table_out = engine->tables[index];
    return status;
}

exec_status exec_find_export_tag(const waste_exec_engine *engine,
                                 const char *name, exec_tag **tag_out,
                                 exec_error *error) {
    uint32_t index = 0;
    if (!tag_out)
        return exec_fail(error, EXEC_ERROR_FORMAT, "null argument");
    exec_status status = find_extern_export(engine, name, 4, &index, error);
    if (status == EXEC_OK) *tag_out = engine->tags[index];
    return status;
}
