#include "store.h"
#include "runtime_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    exec_memory *object;
    uint8_t *data;
    uint64_t pages;
    uint64_t max_pages;
    uint8_t has_max;
    uint8_t is_64;
} memory_snapshot;

typedef struct {
    exec_table *object;
    exec_table_element *elements;
    uint64_t size;
    uint64_t max_size;
    uint8_t has_max;
    uint8_t is_64;
    wasm_valtype element_type;
    const waste_exec_engine *type_owner;
} table_snapshot;

typedef struct {
    exec_global *object;
    exec_global value;
} global_snapshot;

typedef struct {
    exec_gc_object object;
    wasm_value *values;
} gc_snapshot;

typedef struct {
    waste_exec_engine *engine;
    uint8_t elem_dropped[WAST_MAX_ELEM_SEGS];
    uint8_t data_dropped[WAST_MAX_DATA_SEGS];
    uint32_t elem_count;
    uint32_t data_count;
    uint8_t instantiation_trapped;
    uint32_t next_opaque_ref;
    uint32_t gc_object_count;
    uint32_t gc_object_capacity;
    gc_snapshot *gc_objects;
    uint32_t exception_object_count;
    uint32_t exception_object_capacity;
    exec_exception_object *exception_objects;
    char instantiation_error[256];
} engine_snapshot;

typedef struct {
    memory_snapshot *memories;
    size_t memory_count;
    table_snapshot *tables;
    size_t table_count;
    global_snapshot *globals;
    size_t global_count;
    engine_snapshot *engines;
    size_t engine_count;
    exec_memory *spectest_memory;
    exec_table *spectest_table;
    exec_global *spectest_globals[4];
} checkpoint_impl;

static exec_status checkpoint_error(exec_error *error, const char *message) {
    if (error) {
        error->status = EXEC_ERROR_FORMAT;
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return EXEC_ERROR_FORMAT;
}

static int grow_array(void **array, size_t *capacity, size_t count,
                      size_t element_size) {
    if (count <= *capacity) return 1;
    size_t next = *capacity ? *capacity : 8;
    while (next < count) {
        if (next > SIZE_MAX / 2) return 0;
        next *= 2;
    }
    if (element_size && next > SIZE_MAX / element_size) return 0;
    void *grown = realloc(*array, next * element_size);
    if (!grown) return 0;
    *array = grown;
    *capacity = next;
    return 1;
}

static int add_memory(checkpoint_impl *impl, exec_memory *memory) {
    if (!memory) return 1;
    for (size_t i = 0; i < impl->memory_count; i++)
        if (impl->memories[i].object == memory) return 1;
    size_t capacity = impl->memory_count;
    if (!grow_array((void **)&impl->memories, &capacity,
                    impl->memory_count + 1, sizeof(*impl->memories))) return 0;
    memory_snapshot *snapshot = &impl->memories[impl->memory_count++];
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->object = memory;
    return 1;
}

static int add_table(checkpoint_impl *impl, exec_table *table) {
    if (!table) return 1;
    for (size_t i = 0; i < impl->table_count; i++)
        if (impl->tables[i].object == table) return 1;
    size_t capacity = impl->table_count;
    if (!grow_array((void **)&impl->tables, &capacity,
                    impl->table_count + 1, sizeof(*impl->tables))) return 0;
    table_snapshot *snapshot = &impl->tables[impl->table_count++];
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->object = table;
    return 1;
}

static int add_global(checkpoint_impl *impl, exec_global *global) {
    if (!global) return 1;
    for (size_t i = 0; i < impl->global_count; i++)
        if (impl->globals[i].object == global) return 1;
    size_t capacity = impl->global_count;
    if (!grow_array((void **)&impl->globals, &capacity,
                    impl->global_count + 1, sizeof(*impl->globals))) return 0;
    global_snapshot *snapshot = &impl->globals[impl->global_count++];
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->object = global;
    return 1;
}

static int add_engine(checkpoint_impl *impl, waste_exec_engine *engine) {
    if (!engine) return 1;
    for (size_t i = 0; i < impl->engine_count; i++)
        if (impl->engines[i].engine == engine) return 1;
    size_t capacity = impl->engine_count;
    if (!grow_array((void **)&impl->engines, &capacity,
                    impl->engine_count + 1, sizeof(*impl->engines))) return 0;
    engine_snapshot *snapshot = &impl->engines[impl->engine_count++];
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->engine = engine;
    return 1;
}

static int collect_engine(checkpoint_impl *impl, waste_exec_engine *engine) {
    if (!engine) return 1;
    for (uint32_t i = 0; i < engine->memory_count; i++)
        if (!add_memory(impl, engine->memories[i])) return 0;
    for (uint32_t i = 0; i < engine->table_count; i++)
        if (!add_table(impl, engine->tables[i])) return 0;
    for (uint32_t i = 0; i < engine->global_count; i++)
        if (!add_global(impl, engine->globals[i])) return 0;
    return 1;
}

static void free_impl(checkpoint_impl *impl) {
    if (!impl) return;
    for (size_t i = 0; i < impl->memory_count; i++) free(impl->memories[i].data);
    for (size_t i = 0; i < impl->table_count; i++) free(impl->tables[i].elements);
    for (size_t i = 0; i < impl->engine_count; i++) {
        engine_snapshot *snapshot = &impl->engines[i];
        for (uint32_t j = 0; j < snapshot->gc_object_count; j++)
            free(snapshot->gc_objects[j].values);
        free(snapshot->gc_objects);
        free(snapshot->exception_objects);
    }
    free(impl->memories);
    free(impl->tables);
    free(impl->globals);
    free(impl->engines);
    free(impl);
}

void native_store_checkpoint_init(native_store_checkpoint *checkpoint) {
    if (checkpoint) checkpoint->impl = NULL;
}

void native_store_checkpoint_destroy(native_store_checkpoint *checkpoint) {
    if (!checkpoint) return;
    free_impl((checkpoint_impl *)checkpoint->impl);
    checkpoint->impl = NULL;
}

static int copy_memory(memory_snapshot *snapshot) {
    exec_memory *memory = snapshot->object;
    snapshot->pages = memory->pages;
    snapshot->max_pages = memory->max_pages;
    snapshot->has_max = memory->has_max;
    snapshot->is_64 = memory->is_64;
    if (!memory->pages) return 1;
    if (memory->pages > SIZE_MAX / EXEC_PAGE_SIZE) return 0;
    size_t bytes = (size_t)memory->pages * EXEC_PAGE_SIZE;
    snapshot->data = malloc(bytes);
    if (!snapshot->data) return 0;
    if (memory->data) memcpy(snapshot->data, memory->data, bytes);
    else memset(snapshot->data, 0, bytes);
    return 1;
}

static int copy_table(table_snapshot *snapshot) {
    exec_table *table = snapshot->object;
    snapshot->size = table->size;
    snapshot->max_size = table->max_size;
    snapshot->has_max = table->has_max;
    snapshot->is_64 = table->is_64;
    snapshot->element_type = table->element_type;
    snapshot->type_owner = table->type_owner;
    if (!table->size) return 1;
    if (table->size > SIZE_MAX / sizeof(*table->elements)) return 0;
    snapshot->elements = malloc((size_t)table->size * sizeof(*table->elements));
    if (!snapshot->elements) return 0;
    if (table->elements)
        memcpy(snapshot->elements, table->elements,
               (size_t)table->size * sizeof(*table->elements));
    else
        memset(snapshot->elements, 0,
               (size_t)table->size * sizeof(*table->elements));
    return 1;
}

static int copy_engine(engine_snapshot *snapshot) {
    waste_exec_engine *engine = snapshot->engine;
    memcpy(snapshot->elem_dropped, engine->elem_dropped,
           sizeof(snapshot->elem_dropped));
    memcpy(snapshot->data_dropped, engine->data_dropped,
           sizeof(snapshot->data_dropped));
    snapshot->elem_count = engine->elem_count;
    snapshot->data_count = engine->data_count;
    snapshot->instantiation_trapped = engine->instantiation_trapped;
    snapshot->next_opaque_ref = engine->next_opaque_ref;
    snapshot->gc_object_count = engine->gc_object_count;
    snapshot->gc_object_capacity = engine->gc_object_capacity;
    snapshot->exception_object_count = engine->exception_object_count;
    snapshot->exception_object_capacity = engine->exception_object_capacity;
    memcpy(snapshot->instantiation_error, engine->instantiation_error,
           sizeof(snapshot->instantiation_error));
    if (snapshot->gc_object_count) {
        snapshot->gc_objects = calloc(snapshot->gc_object_count,
                                      sizeof(*snapshot->gc_objects));
        if (!snapshot->gc_objects) return 0;
        for (uint32_t i = 0; i < snapshot->gc_object_count; i++) {
            snapshot->gc_objects[i].object = engine->gc_objects[i];
            uint32_t length = engine->gc_objects[i].length;
            if (!length) continue;
            snapshot->gc_objects[i].values = malloc((size_t)length * sizeof(wasm_value));
            if (!snapshot->gc_objects[i].values) return 0;
            if (engine->gc_objects[i].values)
                memcpy(snapshot->gc_objects[i].values, engine->gc_objects[i].values,
                       (size_t)length * sizeof(wasm_value));
        }
    }
    if (snapshot->exception_object_count) {
        snapshot->exception_objects = malloc((size_t)snapshot->exception_object_count *
                                              sizeof(*snapshot->exception_objects));
        if (!snapshot->exception_objects) return 0;
        memcpy(snapshot->exception_objects, engine->exception_objects,
               (size_t)snapshot->exception_object_count *
               sizeof(*snapshot->exception_objects));
    }
    return 1;
}

exec_status native_store_checkpoint_capture(native_store *store,
                                            native_store_checkpoint *checkpoint,
                                            exec_error *error) {
    if (!store || !checkpoint) return checkpoint_error(error, "null checkpoint argument");
    native_store_checkpoint_destroy(checkpoint);
    checkpoint_impl *impl = calloc(1, sizeof(*impl));
    if (!impl) return checkpoint_error(error, "checkpoint allocation failed");
    if (!add_memory(impl, &store->spectest_memory) ||
        !add_table(impl, &store->spectest_table) ||
        !add_global(impl, &store->spectest_i32) ||
        !add_global(impl, &store->spectest_i64) ||
        !add_global(impl, &store->spectest_f32) ||
        !add_global(impl, &store->spectest_f64)) {
        free_impl(impl); return checkpoint_error(error, "checkpoint allocation failed");
    }
    for (int i = 0; i < store->module_count; i++)
        if (!add_engine(impl, store->modules[i].engine)) goto alloc_fail;
    for (int i = 0; i < store->orphan_count; i++)
        if (!add_engine(impl, store->orphan_engines[i])) goto alloc_fail;
    for (size_t i = 0; i < impl->engine_count; i++)
        if (!collect_engine(impl, impl->engines[i].engine)) goto alloc_fail;
    for (size_t i = 0; i < impl->memory_count; i++)
        if (!copy_memory(&impl->memories[i])) goto alloc_fail;
    for (size_t i = 0; i < impl->table_count; i++)
        if (!copy_table(&impl->tables[i])) goto alloc_fail;
    for (size_t i = 0; i < impl->global_count; i++)
        impl->globals[i].value = *impl->globals[i].object;
    for (size_t i = 0; i < impl->engine_count; i++)
        if (!copy_engine(&impl->engines[i])) goto alloc_fail;
    checkpoint->impl = impl;
    return EXEC_OK;
alloc_fail:
    free_impl(impl);
    return checkpoint_error(error, "checkpoint allocation failed");
}

static exec_status restore_memory(memory_snapshot *snapshot, exec_error *error) {
    exec_memory *memory = snapshot->object;
    if (snapshot->pages > SIZE_MAX / EXEC_PAGE_SIZE)
        return checkpoint_error(error, "memory checkpoint is too large");
    size_t bytes = (size_t)snapshot->pages * EXEC_PAGE_SIZE;
    uint8_t *data = NULL;
    if (bytes) {
        data = realloc(memory->data, bytes);
        if (!data) return checkpoint_error(error, "memory restore allocation failed");
        memcpy(data, snapshot->data, bytes);
    } else {
        free(memory->data);
    }
    memory->data = data;
    memory->pages = snapshot->pages;
    memory->max_pages = snapshot->max_pages;
    memory->has_max = snapshot->has_max;
    memory->is_64 = snapshot->is_64;
    return EXEC_OK;
}

static exec_status restore_table(table_snapshot *snapshot, exec_error *error) {
    exec_table *table = snapshot->object;
    if (snapshot->size > SIZE_MAX / sizeof(*table->elements))
        return checkpoint_error(error, "table checkpoint is too large");
    size_t bytes = (size_t)snapshot->size * sizeof(*table->elements);
    exec_table_element *elements = bytes ? realloc(table->elements, bytes) : NULL;
    if (bytes && !elements) return checkpoint_error(error, "table restore allocation failed");
    if (bytes) memcpy(elements, snapshot->elements, bytes);
    else free(table->elements);
    table->elements = elements;
    table->size = snapshot->size;
    table->max_size = snapshot->max_size;
    table->has_max = snapshot->has_max;
    table->is_64 = snapshot->is_64;
    table->element_type = snapshot->element_type;
    table->type_owner = snapshot->type_owner;
    return EXEC_OK;
}

exec_status native_store_checkpoint_restore(native_store_checkpoint *checkpoint,
                                            exec_error *error) {
    if (!checkpoint || !checkpoint->impl)
        return checkpoint_error(error, "empty checkpoint");
    checkpoint_impl *impl = checkpoint->impl;
    for (size_t i = 0; i < impl->memory_count; i++) {
        exec_status status = restore_memory(&impl->memories[i], error);
        if (status != EXEC_OK) return status;
    }
    for (size_t i = 0; i < impl->table_count; i++) {
        exec_status status = restore_table(&impl->tables[i], error);
        if (status != EXEC_OK) return status;
    }
    for (size_t i = 0; i < impl->global_count; i++)
        *impl->globals[i].object = impl->globals[i].value;
    for (size_t i = 0; i < impl->engine_count; i++) {
        engine_snapshot *snapshot = &impl->engines[i];
        waste_exec_engine *engine = snapshot->engine;
        memcpy(engine->elem_dropped, snapshot->elem_dropped, sizeof(engine->elem_dropped));
        memcpy(engine->data_dropped, snapshot->data_dropped, sizeof(engine->data_dropped));
        engine->elem_count = snapshot->elem_count;
        engine->data_count = snapshot->data_count;
        engine->instantiation_trapped = snapshot->instantiation_trapped;
        engine->next_opaque_ref = snapshot->next_opaque_ref;
        memcpy(engine->instantiation_error, snapshot->instantiation_error,
               sizeof(engine->instantiation_error));
        for (uint32_t j = 0; j < engine->gc_object_count; j++) free(engine->gc_objects[j].values);
        free(engine->gc_objects);
        engine->gc_objects = NULL;
        engine->gc_object_count = 0;
        engine->gc_object_capacity = snapshot->gc_object_capacity;
        if (snapshot->gc_object_count) {
            engine->gc_objects = calloc(snapshot->gc_object_count, sizeof(*engine->gc_objects));
            if (!engine->gc_objects) return checkpoint_error(error, "GC restore allocation failed");
            for (uint32_t j = 0; j < snapshot->gc_object_count; j++) {
                engine->gc_objects[j] = snapshot->gc_objects[j].object;
                uint32_t length = engine->gc_objects[j].length;
                if (length) {
                    engine->gc_objects[j].values = malloc((size_t)length * sizeof(wasm_value));
                    if (!engine->gc_objects[j].values)
                        return checkpoint_error(error, "GC restore allocation failed");
                    memcpy(engine->gc_objects[j].values, snapshot->gc_objects[j].values,
                           (size_t)length * sizeof(wasm_value));
                }
            }
            engine->gc_object_count = snapshot->gc_object_count;
        }
        free(engine->exception_objects);
        engine->exception_objects = NULL;
        engine->exception_object_count = 0;
        engine->exception_object_capacity = snapshot->exception_object_capacity;
        if (snapshot->exception_object_count) {
            engine->exception_objects = malloc((size_t)snapshot->exception_object_count *
                                                sizeof(*engine->exception_objects));
            if (!engine->exception_objects)
                return checkpoint_error(error, "exception restore allocation failed");
            memcpy(engine->exception_objects, snapshot->exception_objects,
                   (size_t)snapshot->exception_object_count *
                   sizeof(*engine->exception_objects));
            engine->exception_object_count = snapshot->exception_object_count;
        }
    }
    return EXEC_OK;
}
