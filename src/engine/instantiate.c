#include "instantiate.h"

#include <stdlib.h>
#include <string.h>

enum { WASM_PAGE_SIZE = 65536 };

exec_status wasm_instantiate_module(const wasm_module *module,
                                    const exec_imports *imports,
                                    waste_exec_engine **engine_out,
                                    exec_error *error) {
    if (!module || !module->source || !module->owned_source ||
        !module->fully_decoded)
        return exec_fail(error, EXEC_ERROR_FORMAT,
                               "module is not an owned decoded module");
    return exec_load_decoded_with_imports(
        module->source, module->source_size, module, imports, engine_out,
        error);
}

exec_status wasm_instance_allocate_memory(exec_memory *memory,
                                          uint64_t initial,
                                          uint64_t maximum,
                                          uint32_t flags,
                                          exec_error *error) {
    size_t bytes;
    if (!memory || initial > SIZE_MAX / WASM_PAGE_SIZE)
        return exec_fail(error, EXEC_ERROR_FORMAT,
                               "memory allocation failed");
    bytes = (size_t)initial * WASM_PAGE_SIZE;
    memset(memory, 0, sizeof(*memory));
    memory->data = calloc(bytes ? bytes : 1, 1);
    if (!memory->data)
        return exec_fail(error, EXEC_ERROR_FORMAT,
                               "memory allocation failed");
    memory->pages = initial;
    memory->max_pages = maximum;
    memory->has_max = (uint8_t)(flags & 1u);
    memory->is_64 = (uint8_t)((flags & 4u) != 0);
    return EXEC_OK;
}

exec_status wasm_instance_allocate_table(exec_table *table,
                                         waste_exec_engine *owner,
                                         wasm_valtype element_type,
                                         uint64_t initial,
                                         uint64_t maximum,
                                         uint32_t flags,
                                         exec_error *error) {
    if (!table || initial > SIZE_MAX / sizeof(*table->elements))
        return exec_fail(error, EXEC_ERROR_FORMAT,
                               "table allocation failed");
    memset(table, 0, sizeof(*table));
    table->elements = malloc((initial ? (size_t)initial : 1) *
                             sizeof(*table->elements));
    if (!table->elements)
        return exec_fail(error, EXEC_ERROR_FORMAT,
                               "table allocation failed");
    for (uint64_t i = 0; i < initial; i++) {
        table->elements[i].owner = NULL;
        table->elements[i].func_idx = 0;
        table->elements[i].type = element_type;
        table->elements[i].dynamic_type = element_type;
    }
    table->size = initial;
    table->max_size = maximum;
    table->has_max = (uint8_t)(flags & 1u);
    table->is_64 = (uint8_t)((flags & 4u) != 0);
    table->element_type = element_type;
    table->type_owner = owner;
    return EXEC_OK;
}

exec_status wasm_instance_copy_segment(uint8_t **destination,
                                       const uint8_t *source,
                                       uint32_t length,
                                       exec_error *error) {
    uint8_t *copy = NULL;
    if (!destination || (length && !source))
        return exec_fail(error, EXEC_ERROR_FORMAT,
                               "invalid segment allocation");
    if (length) {
        copy = malloc(length);
        if (!copy)
            return exec_fail(error, EXEC_ERROR_FORMAT,
                                   "segment allocation failed");
        memcpy(copy, source, length);
    }
    *destination = copy;
    return EXEC_OK;
}

exec_status wasm_instance_allocate_elements(exec_table_element **elements,
                                            uint32_t count,
                                            exec_error *error) {
    if (!elements)
        return exec_fail(error, EXEC_ERROR_FORMAT,
                               "invalid element segment allocation");
    *elements = count ? calloc(count, sizeof(**elements)) : NULL;
    if (count && !*elements)
        return exec_fail(error, EXEC_ERROR_FORMAT,
                               "element segment allocation failed");
    return EXEC_OK;
}

void wasm_instance_initialize_global(exec_global *global,
                                     waste_exec_engine *owner,
                                     wasm_valtype declared_type,
                                     uint8_t mutable_,
                                     const wasm_value *initial) {
    memset(global, 0, sizeof(*global));
    global->value = *initial;
    global->value.type = declared_type;
    global->type_owner = owner;
    global->mutable_ = mutable_;
}

void wasm_instance_initialize_tag(exec_tag *tag,
                                  waste_exec_engine *owner,
                                  uint32_t type_index) {
    tag->type_owner = owner;
    tag->type_index = type_index;
}

void *wasm_instance_resize_objects(void *objects, size_t old_count,
                                   size_t new_count, size_t item_size,
                                   exec_status failure_status,
                                   const char *message,
                                   exec_error *error) {
    void *resized;
    if (!item_size || new_count < old_count || new_count > SIZE_MAX / item_size) {
        exec_fail(error, failure_status, message);
        return NULL;
    }
    resized = realloc(objects, new_count * item_size);
    if (!resized) {
        exec_fail(error, failure_status, message);
        return NULL;
    }
    memset((uint8_t *)resized + old_count * item_size, 0,
           (new_count - old_count) * item_size);
    return resized;
}

void *wasm_instance_allocate_objects(size_t count, size_t item_size,
                                     exec_status failure_status,
                                     const char *message,
                                     exec_error *error) {
    void *objects;
    if (!item_size || count > SIZE_MAX / item_size) {
        exec_fail(error, failure_status, message);
        return NULL;
    }
    objects = calloc(count, item_size);
    if (count && !objects)
        exec_fail(error, failure_status, message);
    return objects;
}
