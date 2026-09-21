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
    free(engine->clone_bindings);
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
    if (!engine->shared_static)
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
    if (!engine->shared_static) {
        free(engine->types);
        free(engine->funcs);
        free(engine->exports);
    }
    for (uint32_t i = 0; i < engine->memory_count; i++)
        if (engine->owns_memories[i]) free(engine->memories[i]->data);
    for (uint32_t i = engine->import_table_count; i < engine->table_count; i++)
        free(engine->owned_tables[i].elements);
    free(engine);
}

exec_status exec_clone_engine(const waste_exec_engine *source,
                              waste_exec_engine **clone_out,
                              exec_error *error) {
    waste_exec_engine *clone;
    if (!source || !clone_out)
        return exec_fail(error, EXEC_ERROR_FORMAT, "invalid engine clone");
    *clone_out = NULL;
    clone = calloc(1, sizeof(*clone));
    if (!clone)
        return exec_fail(error, EXEC_ERROR_TRAP, "engine clone allocation failed");
    *clone = *source;
    clone->shared_static = 1;
    clone->gc_objects = NULL;
    clone->exception_objects = NULL;
    clone->jump_snapshots = NULL;
    clone->jump_snapshot_count = 0;
    clone->jump_snapshot_capacity = 0;
    memset(clone->local_frames, 0, sizeof(clone->local_frames));
    memset(clone->operand_frames, 0, sizeof(clone->operand_frames));
    memset(clone->control_frames, 0, sizeof(clone->control_frames));
    memcpy(clone->local_frame_capacities, source->local_frame_capacities,
           sizeof(clone->local_frame_capacities));

    for (uint32_t i = source->import_global_count; i < source->global_count; i++) {
        clone->owned_globals[i] = source->owned_globals[i];
        clone->globals[i] = &clone->owned_globals[i];
    }
    for (uint32_t i = source->import_memory_count; i < source->memory_count; i++) {
        clone->owned_memories[i] = source->owned_memories[i];
        clone->memories[i] = &clone->owned_memories[i];
        clone->owns_memories[i] = 1;
        clone->owned_memories[i].data = NULL;
        if (source->owned_memories[i].pages) {
            size_t bytes = (size_t)source->owned_memories[i].pages * EXEC_PAGE_SIZE;
            clone->owned_memories[i].data = malloc(bytes);
            if (!clone->owned_memories[i].data) goto failure;
            memcpy(clone->owned_memories[i].data,
                   source->owned_memories[i].data, bytes);
        }
    }
    for (uint32_t i = source->import_table_count; i < source->table_count; i++) {
        clone->owned_tables[i] = source->owned_tables[i];
        clone->tables[i] = &clone->owned_tables[i];
        clone->owned_tables[i].elements = NULL;
        if (source->owned_tables[i].size) {
            size_t bytes = (size_t)source->owned_tables[i].size *
                           sizeof(*clone->owned_tables[i].elements);
            clone->owned_tables[i].elements = malloc(bytes);
            if (!clone->owned_tables[i].elements) goto failure;
            memcpy(clone->owned_tables[i].elements,
                   source->owned_tables[i].elements, bytes);
        }
    }
    for (uint32_t i = 0; i < source->elem_count; i++) {
        if (!source->elem_lengths[i]) continue;
        size_t bytes = (size_t)source->elem_lengths[i] *
                       sizeof(*source->elem_values[i]);
        clone->elem_values[i] = malloc(bytes);
        if (!clone->elem_values[i]) goto failure;
        memcpy(clone->elem_values[i], source->elem_values[i], bytes);
    }
    for (uint32_t i = 0; i < source->data_count; i++) {
        if (!source->data_seg_lengths[i]) continue;
        clone->data_segs[i] = malloc(source->data_seg_lengths[i]);
        if (!clone->data_segs[i]) goto failure;
        memcpy(clone->data_segs[i], source->data_segs[i],
               source->data_seg_lengths[i]);
    }
    if (source->gc_object_count) {
        clone->gc_objects = calloc(source->gc_object_count,
                                    sizeof(*clone->gc_objects));
        if (!clone->gc_objects) goto failure;
        clone->gc_object_capacity = source->gc_object_count;
        clone->gc_object_count = source->gc_object_count;
        for (uint32_t i = 0; i < source->gc_object_count; i++) {
            clone->gc_objects[i] = source->gc_objects[i];
            clone->gc_objects[i].values = NULL;
            uint32_t length = source->gc_objects[i].length;
            if (length) {
                clone->gc_objects[i].values = malloc((size_t)length *
                                                     sizeof(wasm_value));
                if (!clone->gc_objects[i].values) goto failure;
                memcpy(clone->gc_objects[i].values,
                       source->gc_objects[i].values,
                       (size_t)length * sizeof(wasm_value));
            }
        }
    }
    if (source->exception_object_count) {
        size_t bytes = (size_t)source->exception_object_count *
                       sizeof(*clone->exception_objects);
        clone->exception_objects = malloc(bytes);
        if (!clone->exception_objects) goto failure;
        memcpy(clone->exception_objects, source->exception_objects, bytes);
        clone->exception_object_capacity = source->exception_object_count;
    }
    for (uint32_t i = 0; i < EXEC_MAX_CALL_DEPTH; i++) {
        if (source->local_frames[i] && clone->local_frame_capacities[i]) {
            size_t bytes = (size_t)clone->local_frame_capacities[i] *
                           sizeof(wasm_value);
            clone->local_frames[i] = malloc(bytes);
            if (!clone->local_frames[i]) goto failure;
            memcpy(clone->local_frames[i], source->local_frames[i], bytes);
        }
        if (source->operand_frames[i]) {
            clone->operand_frames[i] = malloc(sizeof(*clone->operand_frames[i]));
            if (!clone->operand_frames[i]) goto failure;
            *clone->operand_frames[i] = *source->operand_frames[i];
        }
        if (source->control_frames[i]) {
            clone->control_frames[i] = malloc(EXEC_MAX_CONTROL *
                                              sizeof(*clone->control_frames[i]));
            if (!clone->control_frames[i]) goto failure;
            memcpy(clone->control_frames[i], source->control_frames[i],
                   EXEC_MAX_CONTROL * sizeof(*clone->control_frames[i]));
        }
    }
    if (source->jump_snapshot_count) {
        clone->jump_snapshots = calloc(source->jump_snapshot_count,
                                       sizeof(*clone->jump_snapshots));
        if (!clone->jump_snapshots) goto failure;
        clone->jump_snapshot_capacity = source->jump_snapshot_count;
        clone->jump_snapshot_count = source->jump_snapshot_count;
        for (uint32_t i = 0; i < source->jump_snapshot_count; i++) {
            clone->jump_snapshots[i] = source->jump_snapshots[i];
            clone->jump_snapshots[i].locals = NULL;
            if (source->jump_snapshots[i].local_count) {
                size_t bytes = (size_t)source->jump_snapshots[i].local_count *
                               sizeof(wasm_value);
                clone->jump_snapshots[i].locals = malloc(bytes);
                if (!clone->jump_snapshots[i].locals) goto failure;
                memcpy(clone->jump_snapshots[i].locals,
                       source->jump_snapshots[i].locals, bytes);
            }
        }
    }
    if (source->memory == source->memories[0] && source->memory_count)
        clone->memory = clone->memories[0];
    *clone_out = clone;
    return EXEC_OK;

failure:
    exec_free(clone);
    return exec_fail(error, EXEC_ERROR_TRAP, "engine clone allocation failed");
}

exec_status exec_clone_engine_bind(waste_exec_engine *clone,
                                   const exec_clone_binding *bindings,
                                   uint32_t binding_count,
                                   exec_error *error) {
    if (!clone || (binding_count && !bindings))
        return exec_fail(error, EXEC_ERROR_FORMAT, "invalid clone bindings");
    free(clone->clone_bindings);
    clone->clone_bindings = NULL;
    clone->clone_binding_count = 0;
    if (!binding_count) return EXEC_OK;
    clone->clone_bindings = malloc((size_t)binding_count *
                                   sizeof(*clone->clone_bindings));
    if (!clone->clone_bindings)
        return exec_fail(error, EXEC_ERROR_TRAP, "clone bindings allocation failed");
    memcpy(clone->clone_bindings, bindings,
           (size_t)binding_count * sizeof(*clone->clone_bindings));
    clone->clone_binding_count = binding_count;
    return EXEC_OK;
}

waste_exec_engine *exec_clone_resolve(const waste_exec_engine *caller,
                                      const waste_exec_engine *source) {
    if (!caller || !source) return (waste_exec_engine *)source;
    for (uint32_t i = 0; i < caller->clone_binding_count; i++)
        if (caller->clone_bindings[i].source == source)
            return caller->clone_bindings[i].clone;
    return (waste_exec_engine *)source;
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
