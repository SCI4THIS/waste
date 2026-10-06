#include "engine_internal.h"
#include "runtime_internal.h"
#include "store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int failures;

static void check(int condition, const char *message) {
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

int main(void) {
    native_store store;
    memset(&store, 0, sizeof(store));
    waste_exec_engine *provider = calloc(1, sizeof(*provider));
    waste_exec_engine *consumer = calloc(1, sizeof(*consumer));
    if (!provider || !consumer) return 2;

    exec_memory *memory = &provider->owned_memories[0];
    memory->pages = 1;
    memory->linear_pages = 1;
    memory->max_pages = 4;
    memory->has_max = 1;
    memory->page_data = calloc(1, sizeof(*memory->page_data));
    memory->page_protection = malloc(1);
    memory->mappings = calloc(1, sizeof(*memory->mappings));
    memory->mapping_count = 1;
    memory->mapping_capacity = 1;
    memory->page_protection[0] = EXEC_MEMORY_PROT_READ |
                                 EXEC_MEMORY_PROT_WRITE;
    memory->mappings[0].first_page = 0;
    memory->mappings[0].page_count = 1;
    memory->mappings[0].protection = memory->page_protection[0];
    uint8_t initial_value = 0x5a;
    check(exec_memory_write(memory, 13, &initial_value, 1, NULL) == EXEC_OK,
          "memory fixture initializes through the access API");
    exec_memory *shared_memory = &provider->owned_memories[1];
    shared_memory->max_pages = 4;
    shared_memory->has_max = 1;
    check(exec_memory_resize_pages(shared_memory, 1, NULL) == EXEC_OK &&
          exec_memory_share_pages(shared_memory, 0, memory, 0, 1, NULL) ==
              EXEC_OK,
          "shared checkpoint fixture initializes through the memory API");
    provider->memories[0] = memory;
    provider->memories[1] = shared_memory;
    provider->memory_count = 2;
    provider->memory = memory;
    provider->owns_memories[0] = 1;
    provider->owns_memories[1] = 1;
    consumer->memories[0] = memory;
    consumer->memory_count = 1;
    consumer->import_memory_count = 1;

    exec_table *table = &provider->owned_tables[0];
    table->size = 1;
    table->max_size = 4;
    table->has_max = 1;
    table->elements = calloc(1, sizeof(*table->elements));
    table->elements[0].func_idx = 17;
    provider->tables[0] = table;
    provider->table_count = 1;
    consumer->tables[0] = table;
    consumer->table_count = 1;
    consumer->import_table_count = 1;

    /* Capture the host table even before any module imports it. */
    exec_table *wide = &store.spectest_table64;
    wide->size = 10;
    wide->max_size = 20;
    wide->has_max = 1;
    wide->is_64 = 1;
    wide->element_type = WASM_VALTYPE_FUNCREF;
    wide->elements = calloc(12, sizeof(*wide->elements));
    if (!wide->elements) return 2;

    exec_global *global = &provider->owned_globals[0];
    global->mutable_ = 1;
    global->value.type = WASM_VALTYPE_I32;
    global->value.i32 = 7;
    provider->globals[0] = global;
    provider->global_count = 1;
    consumer->globals[0] = global;
    consumer->global_count = 1;
    consumer->import_global_count = 1;

    native_linked_module modules[2];
    memset(modules, 0, sizeof(modules));
    modules[0].engine = provider;
    modules[1].engine = consumer;
    store.modules = modules;
    store.module_count = 2;
    store.module_capacity = 2;

    native_store_checkpoint checkpoint;
    native_store_checkpoint_init(&checkpoint);
    exec_error error;
    memset(&error, 0, sizeof(error));
    posix_file_object *mapping_object = NULL;
    int mapping_writable = 0;
    int mapping_fd = -1;
    posix_path_metadata mapping_metadata;
    memset(&mapping_metadata, 0, sizeof(mapping_metadata));
    mapping_metadata.kind = POSIX_NODE_REGULAR;
    mapping_metadata.mode = 0600;
    mapping_metadata.inode = 41;

    store.processes[0].used = 1;
    store.processes[0].capsule.handler.kind = NATIVE_PROCESS_HANDLER_WAST;
    store.processes[0].capsule.handler.source = malloc(1);
    store.processes[0].capsule.handler.source_size = 1;
    check(store.processes[0].capsule.handler.source != NULL,
          "handler checkpoint fixture allocates");
    check(native_store_checkpoint_capture(&store, &checkpoint, &error) ==
              EXEC_ERROR_UNSUPPORTED &&
              error.status == EXEC_ERROR_UNSUPPORTED,
          "live handler checkpoint is rejected");
    check(checkpoint.impl == NULL,
          "rejected handler checkpoint does not publish a snapshot");
    free(store.processes[0].capsule.handler.source);
    memset(&store.processes[0], 0, sizeof(store.processes[0]));
    store.processes[0].used = 1;
    store.processes[0].pid = 1;
    store.processes[0].kernel = posix_kernel_create(0);
    check(store.processes[0].kernel != NULL,
          "process checkpoint fixture creates a kernel");
    check(store.processes[0].kernel &&
          posix_kernel_path_add_data(store.processes[0].kernel,
                                     "/checkpoint-map", &mapping_metadata,
                                     (const uint8_t *)"x", 1) == 0 &&
          (mapping_fd = posix_kernel_open(
              store.processes[0].kernel,
              (const uint8_t *)"/checkpoint-map", 15, POSIX_O_RDWR, 0)) >= 0 &&
          posix_kernel_file_retain(store.processes[0].kernel, mapping_fd,
                                   &mapping_object, &mapping_writable) == 0 &&
          (store.processes[0].capsule.file_mappings = calloc(
              1, sizeof(*store.processes[0].capsule.file_mappings))) != NULL &&
          (store.processes[0].capsule.regions = calloc(
              1, sizeof(*store.processes[0].capsule.regions))) != NULL,
          "process checkpoint fixture records mapping topology");
    store.processes[0].capsule.file_mappings[0] =
        (native_process_file_mapping){EXEC_PAGE_SIZE * 2, EXEC_PAGE_SIZE, 0,
                                      mapping_object, 1, 1};
    store.processes[0].capsule.file_mapping_count = 1;
    store.processes[0].capsule.file_mapping_capacity = 1;
    store.processes[0].capsule.regions[0] =
        (native_process_region){2, 1, NATIVE_PROCESS_REGION_MAPPING};
    store.processes[0].capsule.region_count = 1;
    store.processes[0].capsule.region_capacity = 1;
    if (mapping_fd >= 0)
        check(posix_kernel_close(store.processes[0].kernel, mapping_fd) == 0,
              "process checkpoint fixture closes source descriptor");
    memset(&error, 0, sizeof(error));
    check(native_store_checkpoint_capture(&store, &checkpoint, &error) == EXEC_OK,
          "capture succeeds");
    posix_kernel_file_release(
        store.processes[0].capsule.file_mappings[0].file_object);
    free(store.processes[0].capsule.file_mappings);
    store.processes[0].capsule.file_mappings = NULL;
    store.processes[0].capsule.file_mapping_count = 0;
    free(store.processes[0].capsule.regions);
    store.processes[0].capsule.regions = NULL;
    store.processes[0].capsule.region_count = 0;
    native_store_checkpoint nested;
    native_store_checkpoint_init(&nested);

    wide->size = 12;
    wide->elements[0].owner = provider;
    wide->elements[0].func_idx = 17;
    wide->elements[0].type = WASM_VALTYPE_FUNCREF;
    wide->elements[0].dynamic_type = WASM_VALTYPE_FUNCREF;
    uint8_t changed_value = 0xa5;
    check(exec_memory_write(memory, 13, &changed_value, 1, NULL) == EXEC_OK,
          "memory fixture mutates through the access API");
    check(exec_memory_resize_pages(memory, 2, NULL) == EXEC_OK,
          "memory fixture grows through the memory API");
    uint8_t second_page_value = 0xcc;
    check(exec_memory_fill(memory, EXEC_PAGE_SIZE, second_page_value,
                           EXEC_PAGE_SIZE, NULL) == EXEC_OK,
          "memory fixture fills through the memory API");
    check(native_store_checkpoint_capture(&store, &nested, &error) == EXEC_OK,
          "nested checkpoint captures shared post-growth state");
    table->elements = realloc(table->elements, 2 * sizeof(*table->elements));
    table->elements[1].func_idx = 99;
    table->size = 2;
    global->value.i32 = 99;
    provider->active_call_depth = 1;
    provider->yield_frames[0].valid = 1;
    provider->yield_frames[0].func_idx = 17;
    provider->yield_frames[0].pc = 23;
    provider->local_frame_capacities[0] = 1;
    provider->local_frames[0] = calloc(1, sizeof(*provider->local_frames[0]));
    provider->local_frames[0][0].type = WASM_VALTYPE_I32;
    provider->local_frames[0][0].i32 = -1;
    uint8_t restored_value = 0;
    check(native_store_checkpoint_restore(&nested, &error) == EXEC_OK &&
          memory->pages == 2 &&
          exec_memory_read(shared_memory, 13, &restored_value, 1, NULL) ==
              EXEC_OK && restored_value == 0xa5 &&
          memory->page_data[0] == shared_memory->page_data[0],
          "nested restore preserves shared page identity and growth");
    check(wide->size == 12 && wide->elements[0].owner == provider &&
          wide->elements[0].func_idx == 17,
          "nested restore retains spectest table64 growth and function owner");
    uint8_t nested_mutation = 0x77;
    check(exec_memory_write(memory, 13, &nested_mutation, 1, NULL) == EXEC_OK,
          "nested restored memory accepts a follow-up mutation");
    check(native_store_checkpoint_restore(&checkpoint, &error) == EXEC_OK,
          "restore succeeds");
    check(store.processes[0].capsule.file_mapping_count == 1 &&
          store.processes[0].capsule.file_mappings[0].file_object ==
              mapping_object && store.processes[0].capsule.region_count == 1,
          "restore recovers process mapping topology");
    check(provider->memories[0] == consumer->memories[0],
          "imported memory identity is preserved");
    check(provider->tables[0] == consumer->tables[0],
          "imported table identity is preserved");
    check(provider->globals[0] == consumer->globals[0],
          "imported global identity is preserved");
    check(exec_memory_read(memory, 13, &restored_value, 1, NULL) == EXEC_OK,
          "restored memory remains readable");
    check(memory->pages == 1 && restored_value == 0x5a,
          "memory bytes and growth are restored");
    check(table->size == 1 && table->elements[0].func_idx == 17,
          "table contents and growth are restored");
    check(wide->size == 10 && wide->max_size == 20 && wide->has_max &&
          wide->is_64 && wide->element_type == WASM_VALTYPE_FUNCREF &&
          wide->elements[0].owner == NULL && wide->elements[9].owner == NULL,
          "restore resets unimported spectest table64 contents and growth");
    check(global->value.i32 == 7, "global value is restored");
    check(provider->active_call_depth == 0 &&
          !provider->yield_frames[0].valid &&
          provider->local_frames[0] == NULL,
          "linked evaluator yield state is restored");

    waste_exec_engine *provider_clone = NULL;
    exec_clone_binding binding;
    memset(&error, 0, sizeof(error));
    check(exec_clone_engine(provider, &provider_clone, &error) == EXEC_OK &&
          provider_clone != NULL, "provider clone succeeds");
    if (provider_clone) {
        binding.source = provider;
        binding.clone = provider_clone;
        check(exec_clone_engine_bind(provider_clone, &binding, 1, &error) ==
                  EXEC_OK, "provider clone binding succeeds");
        uint8_t clone_value = 0x3c;
        check(exec_memory_write(provider_clone->memory, 13, &clone_value, 1,
                                &error) == EXEC_OK,
              "cloned provider memory accepts writes");
        uint8_t provider_value = 0;
        check(exec_memory_read(provider->memory, 13, &provider_value, 1,
                               &error) == EXEC_OK && provider_value == clone_value,
              "cloned provider preserves explicit shared memory");
        check(exec_clone_resolve(provider_clone, provider) == provider_clone,
              "clone provider resolution is local");
        exec_free(provider_clone);
    }

    native_store_checkpoint_destroy(&checkpoint);
    native_store_checkpoint_destroy(&nested);
    if (store.processes[0].capsule.file_mappings) {
        for (uint32_t i = 0; i < store.processes[0].capsule.file_mapping_count; i++)
            posix_kernel_file_release(
                store.processes[0].capsule.file_mappings[i].file_object);
        free(store.processes[0].capsule.file_mappings);
    }
    free(store.processes[0].capsule.regions);
    posix_kernel_destroy(store.processes[0].kernel);
    free(table->elements);
    free(wide->elements);
    exec_memory_release(memory);
    exec_memory_release(shared_memory);
    free(provider);
    free(consumer);
    printf("C-engine store checkpoint: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
