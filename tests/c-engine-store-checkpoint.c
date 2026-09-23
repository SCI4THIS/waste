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
    memory->max_pages = 4;
    memory->has_max = 1;
    memory->data = calloc(1, EXEC_PAGE_SIZE);
    memory->data[13] = 0x5a;
    provider->memories[0] = memory;
    provider->memory_count = 1;
    provider->memory = memory;
    provider->owns_memories[0] = 1;
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
    memset(&error, 0, sizeof(error));
    check(native_store_checkpoint_capture(&store, &checkpoint, &error) == EXEC_OK,
          "capture succeeds");

    memory->data[13] = 0xa5;
    memory->data = realloc(memory->data, 2 * EXEC_PAGE_SIZE);
    memset(memory->data + EXEC_PAGE_SIZE, 0xcc, EXEC_PAGE_SIZE);
    memory->pages = 2;
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
    check(native_store_checkpoint_restore(&checkpoint, &error) == EXEC_OK,
          "restore succeeds");
    check(provider->memories[0] == consumer->memories[0],
          "imported memory identity is preserved");
    check(provider->tables[0] == consumer->tables[0],
          "imported table identity is preserved");
    check(provider->globals[0] == consumer->globals[0],
          "imported global identity is preserved");
    check(memory->pages == 1 && memory->data[13] == 0x5a,
          "memory bytes and growth are restored");
    check(table->size == 1 && table->elements[0].func_idx == 17,
          "table contents and growth are restored");
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
        provider_clone->memory->data[13] = 0x3c;
        check(provider->memory->data[13] == 0x5a,
              "cloned provider memory is isolated");
        check(exec_clone_resolve(provider_clone, provider) == provider_clone,
              "clone provider resolution is local");
        exec_free(provider_clone);
    }

    native_store_checkpoint_destroy(&checkpoint);
    free(table->elements);
    free(memory->data);
    free(provider);
    free(consumer);
    printf("C-engine store checkpoint: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
