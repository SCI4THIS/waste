#include "engine_internal.h"
#include "runtime_internal.h"
#include "instantiate.h"
#include "wasm/decode.h"
#include "include/waste.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run(waste_exec_engine *engine, const char *name, int32_t a, int32_t b,
               int32_t expected, exec_status expected_status) {
    wasm_value args[2] = {{.type=WASM_VALTYPE_I32, .i32=a}, {.type=WASM_VALTYPE_I32, .i32=b}};
    wasm_value result[1];
    uint32_t function;
    int count = 0;
    exec_error error = {0};
    exec_status status = exec_find_export(engine, name, &function, &error);
    if (status == EXEC_OK) status = exec_invoke(engine, function, args, 2, result, &count, &error);
    if (status != expected_status || (status == EXEC_OK && (count != 1 || result[0].i32 != expected))) {
        fprintf(stderr, "%s failed: status=%d result=%d error=%s\n", name, status,
                count ? result[0].i32 : 0, error.message);
        return 0;
    }
    return 1;
}

static int run_pair(waste_exec_engine *engine) {
    wasm_value args[2] = {{.type=WASM_VALTYPE_I32, .i32=11}, {.type=WASM_VALTYPE_I32, .i32=22}};
    wasm_value results[WAST_MAX_RESULTS]; uint32_t function; int count = 0; exec_error error = {0};
    exec_status status = exec_find_export(engine, "pair", &function, &error);
    if (status == EXEC_OK) status = exec_invoke(engine, function, args, 2, results, &count, &error);
    if (status != EXEC_OK || count != 2 || results[0].i32 != 11 || results[1].i32 != 22) {
        fprintf(stderr, "pair failed: status=%d count=%d error=%s\n", status, count, error.message);
        return 0;
    }
    return 1;
}

static int run_scalar_globals(waste_exec_engine *engine) {
    const char *names[] = {"global-i64", "global-f32", "global-f64"};
    wasm_valtype types[] = {WASM_VALTYPE_I64, WASM_VALTYPE_F32, WASM_VALTYPE_F64};
    wasm_value args[2] = {{.type=WASM_VALTYPE_I32}, {.type=WASM_VALTYPE_I32}};
    for (int i = 0; i < 3; i++) {
        uint32_t function; wasm_value result[1]; int count=0; exec_error error={0};
        exec_status status=exec_find_export(engine,names[i],&function,&error);
        if (status==EXEC_OK) status=exec_invoke(engine,function,args,2,result,&count,&error);
        int match = result[0].type == types[i];
        if (i==0) match = match && result[0].i64 == INT64_C(0x1122334455667788);
        if (i==1) match = match && result[0].f32 == -3.5f;
        if (i==2) match = match && result[0].f64 == 9.25;
        if (status!=EXEC_OK || count!=1 || !match) { fprintf(stderr,"%s failed: %s\n",names[i],error.message); return 0; }
    }
    return 1;
}

static exec_status host_add(void *data, const wasm_value *args, int count,
                            wasm_value *results, int *result_count, exec_error *error,
                            const waste_exec_engine *caller) {
    (void)data; (void)error; (void)caller;
    if (count != 2) return EXEC_ERROR_TRAP;
    results[0].type=WASM_VALTYPE_I32;
    results[0].i32=(int32_t)((uint32_t)args[0].i32 + (uint32_t)args[1].i32);
    *result_count=1; return EXEC_OK;
}

static int test_imports(const char *path) {
    FILE *file=fopen(path,"rb"); if(!file) return 0;
    fseek(file,0,SEEK_END); long length=ftell(file); rewind(file);
    uint8_t *bytes=malloc((size_t)length); if(!bytes) return 0;
    if(fread(bytes,1,(size_t)length,file)!=(size_t)length) {
        free(bytes); fclose(file); return 0;
    }
    fclose(file);
    exec_host_import binding={"host","add",host_add,NULL,NULL,0,0,
                              EXEC_HOST_CONTROL_NONE};
    exec_imports imports={.functions=&binding,.function_count=1};
    waste_exec_engine *engine=NULL; exec_error error={0};
    exec_status status=exec_load_with_imports(bytes,(size_t)length,&imports,&engine,&error); free(bytes);
    if(status!=EXEC_OK) { fprintf(stderr,"import load: %s\n",error.message); return 0; }
    int ok=run(engine,"direct",20,22,42,EXEC_OK) && run(engine,"wrapped",19,23,42,EXEC_OK);
    exec_free(engine); return ok;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file=fopen(path,"rb"); if(!file) return NULL;
    if(fseek(file,0,SEEK_END)!=0) { fclose(file); return NULL; }
    long length=ftell(file); if(length<0) { fclose(file); return NULL; } rewind(file);
    uint8_t *bytes=malloc((size_t)length); if(!bytes) { fclose(file); return NULL; }
    if(fread(bytes,1,(size_t)length,file)!=(size_t)length) { free(bytes); fclose(file); return NULL; }
    fclose(file); *size=(size_t)length; return bytes;
}

static int test_extern_aliases(const char *provider_path, const char *consumer_path) {
    size_t size; uint8_t *bytes=read_file(provider_path,&size); exec_error error={0};
    waste_exec_engine *provider=NULL,*consumer=NULL;
    if(!bytes || exec_load(bytes,size,&provider,&error)!=EXEC_OK) { free(bytes); fprintf(stderr,"provider load: %s\n",error.message); return 0; }
    free(bytes);
    exec_global *global=NULL; exec_memory *memory=NULL; exec_table *table=NULL;
    if(exec_find_export_global(provider,"global",&global,&error)!=EXEC_OK ||
       exec_find_export_memory(provider,"memory",&memory,&error)!=EXEC_OK ||
       exec_find_export_table(provider,"table",&table,&error)!=EXEC_OK) {
        fprintf(stderr,"provider export: %s\n",error.message); exec_free(provider); return 0;
    }
    exec_global_import gi={"provider","global",global};
    exec_memory_import mi={"provider","memory",memory};
    exec_table_import ti={"provider","table",table};
    exec_imports imports={.globals=&gi,.global_count=1,.memories=&mi,.memory_count=1,.tables=&ti,.table_count=1};
    bytes=read_file(consumer_path,&size);
    if(!bytes || exec_load_with_imports(bytes,size,&imports,&consumer,&error)!=EXEC_OK) {
        free(bytes); fprintf(stderr,"consumer load: %s\n",error.message); exec_free(provider); return 0;
    }
    free(bytes);
    exec_global *global2=NULL; exec_memory *memory2=NULL; exec_table *table2=NULL;
    int ok=exec_find_export_global(consumer,"global",&global2,&error)==EXEC_OK && global2==global &&
           exec_find_export_memory(consumer,"memory",&memory2,&error)==EXEC_OK && memory2==memory &&
           exec_find_export_table(consumer,"table",&table2,&error)==EXEC_OK && table2==table &&
           run(consumer,"set-global",73,0,73,EXEC_OK) && run(provider,"read-global",0,0,73,EXEC_OK) &&
           run(consumer,"store",24,0x12345678,0x12345678,EXEC_OK) &&
           run(provider,"read-memory",24,0,0x12345678,EXEC_OK) &&
           run(consumer,"grow",1,0,1,EXEC_OK) && memory->pages==2;
    exec_free(consumer);
    if(!ok) fprintf(stderr,"extern alias test failed: %s\n",error.message);
    exec_free(provider); return ok;
}

static int test_decoded_module_instances(const uint8_t *bytes, size_t size) {
    wasm_module module;
    wasm_decode_error decode_error = {0};
    exec_error error = {0};
    waste_exec_engine *first = NULL;
    waste_exec_engine *second = NULL;
    int ok;

    if (wasm_decode_module(bytes, size, &module, &decode_error) !=
        WASM_DECODE_OK) {
        fprintf(stderr, "decode once: %s at %zu\n", decode_error.message,
                decode_error.offset);
        return 0;
    }
    if (module.source == bytes || !module.owned_source ||
        wasm_instantiate_module(&module, NULL, &first, &error) != EXEC_OK ||
        wasm_instantiate_module(&module, NULL, &second, &error) != EXEC_OK) {
        fprintf(stderr, "double instantiate: %s\n", error.message);
        wasm_module_dispose(&module);
        exec_free(first);
        exec_free(second);
        return 0;
    }
    /* Instances retain fully decoded state, not the module's owned bytes. */
    wasm_module_dispose(&module);
    waste_exec_engine *clone = NULL;
    int clone_ok = exec_clone_engine(first, &clone, &error) == EXEC_OK;
    if (clone_ok) {
        exec_memory *source_memory = first->memories[0];
        exec_memory *clone_memory = clone->memories[0];
        uint8_t source_value = 0;
        uint8_t clone_value = 0x7b;
        clone_ok = source_memory != clone_memory &&
                   source_memory->page_data[0] != NULL &&
                   clone_memory->page_data[0] == source_memory->page_data[0] &&
                   source_memory->pages == 1 && clone_memory->pages == 1 &&
                   exec_memory_write(clone_memory, 128, &clone_value, 1,
                                     &error) == EXEC_OK &&
                   clone_memory->page_data[0] != source_memory->page_data[0] &&
                   exec_memory_read(source_memory, 128, &source_value, 1,
                                    &error) == EXEC_OK && source_value == 0;
        clone_ok = clone_ok &&
                   exec_memory_share_pages(clone_memory, 0, source_memory, 0,
                                           1, &error) == EXEC_OK;
        clone_value = 0x4d;
        clone_ok = clone_ok &&
                   exec_memory_write(clone_memory, 128, &clone_value, 1,
                                     &error) == EXEC_OK &&
                   exec_memory_read(source_memory, 128, &source_value, 1,
                                    &error) == EXEC_OK &&
                   source_value == clone_value;
    }
    exec_free(clone);
    ok = run(first, "store-load", 16, 0x12345678, 0x12345678, EXEC_OK) &&
         run(second, "load-data", 16, 0, 0, EXEC_OK) &&
         run(first, "global", 91, 0, 91, EXEC_OK) &&
         run(second, "read-global", 0, 0, 5, EXEC_OK) &&
         run(first, "grow", 1, 0, 1, EXEC_OK) &&
         run(first, "size", 0, 0, 2, EXEC_OK) &&
         run(second, "size", 0, 0, 1, EXEC_OK) &&
         run(first, "load-data", 8, 0, 0x12345678, EXEC_OK) &&
         run(second, "load-data", 8, 0, 0x12345678, EXEC_OK);
    ok = clone_ok && ok;
    if (!ok) fprintf(stderr, "decoded module instance isolation failed\n");
    exec_free(first);
    exec_free(second);
    return ok;
}

static int test_public_api(const uint8_t *bytes, size_t size) {
    waste_module *module = NULL;
    waste_instance *instance = NULL;
    waste_error error = {0};
    waste_value arguments[2] = {
        {.type = WASM_VALTYPE_I32, .i32 = 20},
        {.type = WASM_VALTYPE_I32, .i32 = 22}
    };
    waste_value result = {0};
    uint32_t function = 0;
    size_t result_count = 0;
    int ok = waste_module_decode(bytes, size, &module, &error) == WASTE_OK &&
             waste_instance_create(module, &instance, &error) == WASTE_OK &&
             waste_instance_find_function(instance, "add", &function,
                                          &error) == WASTE_OK &&
             waste_instance_invoke(instance, function, arguments, 2,
                                   &result, 1, &result_count,
                                   &error) == WASTE_OK &&
             result_count == 1 && result.type == WASM_VALTYPE_I32 &&
             result.i32 == 42;
    if (!ok) fprintf(stderr, "public API: %s\n", error.message);
    waste_instance_delete(instance);
    waste_module_delete(module);
    return ok;
}

static int test_memory_access_contract(void) {
    exec_memory memory = {0};
    exec_error error = {0};
    uint8_t source[32];
    uint8_t result[32];
    memory.page_data = calloc(2, sizeof(*memory.page_data));
    memory.page_protection = malloc(2);
    if (!memory.page_data || !memory.page_protection) {
        free(memory.page_data);
        free(memory.page_protection);
        return 0;
    }
    memory.page_protection[0] = EXEC_MEMORY_PROT_READ | EXEC_MEMORY_PROT_WRITE;
    memory.page_protection[1] = EXEC_MEMORY_PROT_READ | EXEC_MEMORY_PROT_WRITE;
    memory.mappings = calloc(1, sizeof(*memory.mappings));
    if (!memory.mappings) {
        exec_memory_release(&memory);
        return 0;
    }
    memory.mappings[0].page_count = 2;
    memory.mappings[0].protection = EXEC_MEMORY_PROT_READ |
                                     EXEC_MEMORY_PROT_WRITE;
    memory.mapping_count = 1;
    memory.mapping_capacity = 1;
    memory.pages = 2;
    memory.linear_pages = 2;
    memory.max_pages = 4;
    for (size_t i = 0; i < sizeof(source); i++) source[i] = (uint8_t)(i + 1);

    memset(result, 0xa5, sizeof(result));
    int ok = exec_memory_read(&memory, EXEC_PAGE_SIZE - 16, result,
                              sizeof(result), &error) == EXEC_OK;
    for (size_t i = 0; ok && i < sizeof(result); i++) ok = result[i] == 0;
    ok = ok && memory.page_data[0] == NULL && memory.page_data[1] == NULL &&
         exec_memory_write(&memory, EXEC_PAGE_SIZE - 16, source,
                               sizeof(source), &error) == EXEC_OK &&
             exec_memory_read(&memory, EXEC_PAGE_SIZE - 16, result,
                              sizeof(result), &error) == EXEC_OK &&
             memcmp(source, result, sizeof(source)) == 0;
    ok = ok && memory.page_data[0] != NULL && memory.page_data[1] != NULL;
    memset(result, 0, sizeof(result));
    ok = ok && exec_memory_fill(&memory, EXEC_PAGE_SIZE - 8, 0xa5, 16,
                                &error) == EXEC_OK &&
         exec_memory_read(&memory, EXEC_PAGE_SIZE - 8, result, 16,
                          &error) == EXEC_OK;
    for (size_t i = 0; ok && i < 16; i++) ok = result[i] == 0xa5;

    ok = ok && exec_memory_copy(&memory, EXEC_PAGE_SIZE - 4, &memory,
                                EXEC_PAGE_SIZE - 16, 16, &error) == EXEC_OK &&
         exec_memory_read(&memory, EXEC_PAGE_SIZE - 4, result, 16,
                          &error) == EXEC_OK;
    for (size_t i = 0; ok && i < 16; i++)
        ok = result[i] == (i < 8 ? (uint8_t)(i + 1) : 0xa5);

    ok = ok && exec_memory_resize_pages(&memory, 3, &error) == EXEC_OK &&
         memory.pages == 3 && memory.page_data[2] == NULL &&
         exec_memory_fill(&memory, 2 * EXEC_PAGE_SIZE, 0, EXEC_PAGE_SIZE,
                          &error) == EXEC_OK && memory.page_data[2] == NULL &&
         exec_memory_fill(&memory, 2 * EXEC_PAGE_SIZE, 0x5a, 1,
                          &error) == EXEC_OK && memory.page_data[2] != NULL;

    ok = ok && exec_memory_set_protection(&memory, 1, 1,
                                          EXEC_MEMORY_PROT_READ, &error) ==
             EXEC_OK && memory.mapping_count == 3 &&
         exec_memory_read(&memory, EXEC_PAGE_SIZE, result, 1, &error) ==
             EXEC_OK &&
         exec_memory_write(&memory, EXEC_PAGE_SIZE, source, 1, &error) ==
             EXEC_ERROR_TRAP &&
         exec_memory_set_protection(&memory, 1, 1,
                                    EXEC_MEMORY_PROT_READ |
                                    EXEC_MEMORY_PROT_WRITE, &error) == EXEC_OK;

    ok = ok && exec_memory_set_protection(&memory, 1, 2,
                                          EXEC_MEMORY_PROT_READ, &error) ==
             EXEC_OK && memory.mapping_count == 3 &&
         exec_memory_write(&memory, EXEC_PAGE_SIZE, source, 1, &error) ==
             EXEC_ERROR_TRAP &&
         exec_memory_write(&memory, 2 * EXEC_PAGE_SIZE, source, 1,
                           &error) == EXEC_ERROR_TRAP &&
         exec_memory_set_protection(&memory, 2, 1,
                                    EXEC_MEMORY_PROT_READ |
                                    EXEC_MEMORY_PROT_WRITE, &error) == EXEC_OK &&
         exec_memory_set_protection(&memory, 2, 2,
                                    EXEC_MEMORY_PROT_READ, &error) ==
             EXEC_ERROR_TRAP &&
         exec_memory_write(&memory, 2 * EXEC_PAGE_SIZE, source, 1,
                           &error) == EXEC_OK &&
         exec_memory_set_protection(&memory, 3, 1, 0x80, &error) ==
             EXEC_ERROR_TRAP &&
         exec_memory_write(&memory, 2 * EXEC_PAGE_SIZE, source, 1, &error) ==
             EXEC_OK;

    error.status = EXEC_OK;
    ok = ok && exec_memory_read(&memory, 3 * EXEC_PAGE_SIZE - 4,
                                result, 8, &error) == EXEC_ERROR_TRAP &&
         exec_memory_write(&memory, UINT64_MAX, source, 1, &error) ==
             EXEC_ERROR_TRAP &&
         exec_memory_fill(&memory, 0, 0, 0, &error) == EXEC_OK;
    exec_memory_release(&memory);
    exec_memory unmapped = {0};
    ok = ok && exec_memory_resize_pages(&unmapped, 3, &error) == EXEC_OK;
    uint8_t mapped_value = 0x6a;
    ok = ok && exec_memory_write(&unmapped, EXEC_PAGE_SIZE, &mapped_value, 1,
                                 &error) == EXEC_OK;
    ok = ok && exec_memory_unmap_pages(&unmapped, 1, 1, &error) == EXEC_OK &&
         unmapped.page_data[1] == NULL && unmapped.page_protection[1] == 0 &&
         unmapped.mapping_count == 2 &&
         memset(&error, 0, sizeof(error)) == &error &&
         exec_memory_read(&unmapped, EXEC_PAGE_SIZE, result, 1, &error) ==
             EXEC_ERROR_TRAP &&
         error.memory_fault == EXEC_MEMORY_FAULT_UNMAPPED &&
         error.memory_fault_address == EXEC_PAGE_SIZE &&
         exec_memory_write(&unmapped, EXEC_PAGE_SIZE, &mapped_value, 1,
                           &error) == EXEC_ERROR_TRAP;
    ok = ok && exec_memory_map_pages(&unmapped, 1, 1,
                                     EXEC_MEMORY_PROT_READ |
                                     EXEC_MEMORY_PROT_WRITE, 0, &error) ==
             EXEC_OK && unmapped.mapping_count == 3 &&
         exec_memory_write(&unmapped, EXEC_PAGE_SIZE, &mapped_value, 1,
                           &error) == EXEC_OK &&
         exec_memory_read(&unmapped, EXEC_PAGE_SIZE, result, 1, &error) ==
             EXEC_OK && result[0] == mapped_value;
    ok = ok && exec_memory_map_pages(&unmapped, 1, 1,
                                     EXEC_MEMORY_PROT_READ, 0, &error) ==
             EXEC_ERROR_TRAP && unmapped.mapping_count == 3 &&
         exec_memory_map_pages(&unmapped, 0, 1,
                               EXEC_MEMORY_PROT_READ |
                               EXEC_MEMORY_PROT_WRITE, 0x80, &error) ==
             EXEC_ERROR_TRAP && unmapped.page_protection[0] ==
             (EXEC_MEMORY_PROT_READ | EXEC_MEMORY_PROT_WRITE) &&
         exec_memory_unmap_pages(&unmapped, 0, 1, &error) == EXEC_OK &&
         exec_memory_map_pages(&unmapped, 0, 1, 0, 0, &error) == EXEC_OK &&
         exec_memory_page_is_mapped(&unmapped, 0) &&
         exec_memory_read(&unmapped, 0, result, 1, &error) == EXEC_ERROR_TRAP &&
         exec_memory_set_protection(&unmapped, 0, 1,
                                    EXEC_MEMORY_PROT_READ |
                                    EXEC_MEMORY_PROT_WRITE, &error) == EXEC_OK &&
         exec_memory_write(&unmapped, 0, &mapped_value, 1, &error) == EXEC_OK &&
         exec_memory_unmap_pages(&unmapped, 3, 1, &error) == EXEC_ERROR_TRAP &&
         unmapped.mapping_count == 3 && unmapped.page_protection[1] ==
             (EXEC_MEMORY_PROT_READ | EXEC_MEMORY_PROT_WRITE) &&
         exec_memory_reserve_virtual_pages(&unmapped, 5, &error) == EXEC_OK &&
         unmapped.pages == 5 && !exec_memory_page_is_mapped(&unmapped, 3) &&
         exec_memory_map_pages(&unmapped, 3, 2,
                               EXEC_MEMORY_PROT_READ |
                               EXEC_MEMORY_PROT_WRITE, 0, &error) == EXEC_OK &&
         exec_memory_page_is_mapped(&unmapped, 4) &&
         exec_memory_promote_linear_pages(&unmapped, 5, &error) == EXEC_OK &&
         exec_memory_resize_pages(&unmapped, 6, &error) == EXEC_OK &&
         unmapped.linear_pages == 6 &&
         exec_memory_page_is_mapped(&unmapped, 4) &&
         exec_memory_page_is_mapped(&unmapped, 5);
    exec_memory limited = {0};
    ok = ok && exec_memory_resize_pages(&limited, 3, &error) == EXEC_OK;
    limited.has_max = 1;
    limited.max_pages = 4;
    ok = ok && exec_memory_reserve_virtual_pages(&limited, 5, &error) ==
             EXEC_ERROR_TRAP && limited.pages == 3;
    limited.virtual_max_pages = 8;
    ok = ok && exec_memory_reserve_virtual_pages(&limited, 6, &error) ==
             EXEC_OK && limited.pages == 6 && limited.linear_pages == 3;
    ok = ok && exec_memory_map_pages(
             &limited, 5, 1, EXEC_MEMORY_PROT_READ |
             EXEC_MEMORY_PROT_WRITE, 0, &error) == EXEC_OK;
    limited.process_virtual_memory = 1;
    ok = ok && exec_memory_write(&limited, 5 * EXEC_PAGE_SIZE,
                                 &mapped_value, 1, &error) == EXEC_OK &&
         exec_memory_read(&limited, 5 * EXEC_PAGE_SIZE, result, 1,
                          &error) == EXEC_OK && result[0] == mapped_value;
    exec_memory_release(&limited);
    exec_memory_release(&unmapped);
    if (!ok) fprintf(stderr, "memory access contract test failed: %s\n",
                     error.message);
    return ok;
}

int main(int argc, char **argv) {
    if (argc != 5) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 2;
    fseek(file, 0, SEEK_END); long length = ftell(file); rewind(file);
    uint8_t *bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length) return 2;
    fclose(file);
    int isolated = test_decoded_module_instances(bytes, (size_t)length);
    int public_api = test_public_api(bytes, (size_t)length);
    int memory_access = test_memory_access_contract();
    waste_exec_engine *engine = NULL; exec_error error = {0};
    exec_status status = exec_load(bytes, (size_t)length, &engine, &error); free(bytes);
    if (status != EXEC_OK) { fprintf(stderr, "load: %s\n", error.message); return 1; }
    int ok = run(engine, "add", INT32_MAX, 1, INT32_MIN, EXEC_OK) &&
             run(engine, "rotl", 1, 31, INT32_MIN, EXEC_OK) &&
             run(engine, "locals", 11, 22, 22, EXEC_OK) &&
             run(engine, "choose", 7, 9, 7, EXEC_OK) &&
             run(engine, "call", 20, 22, 42, EXEC_OK) &&
             run(engine, "early", 7, 9, 7, EXEC_OK) &&
             run(engine, "ifelse", 1, 9, 1, EXEC_OK) &&
             run(engine, "ifelse", 0, 9, 9, EXEC_OK) &&
             run(engine, "branch", 7, 9, 7, EXEC_OK) &&
             run(engine, "branch-if", 7, 1, 7, EXEC_OK) &&
             run(engine, "branch-if", 7, 0, 0, EXEC_OK) &&
             run(engine, "countdown", 5, 0, 0, EXEC_OK) &&
             run(engine, "branch-table", 33, 0, 33, EXEC_OK) &&
             run(engine, "branch-table", 44, 9, 44, EXEC_OK) &&
             run(engine, "multi-call", 44, 9, 35, EXEC_OK) &&
             run(engine, "multi-block", 44, 9, 35, EXEC_OK) &&
             run_pair(engine) &&
             run_scalar_globals(engine) &&
             run(engine, "global-null", 0, 0, 1, EXEC_OK) &&
             run(engine, "load-data", 8, 0, 0x12345678, EXEC_OK) &&
             run(engine, "load8-s", 12, 0, -128, EXEC_OK) &&
             run(engine, "store-load", 16, 0x76543210, 0x76543210, EXEC_OK) &&
             run(engine, "global", 91, 0, 91, EXEC_OK) &&
             run(engine, "size", 0, 0, 1, EXEC_OK) &&
             run(engine, "grow", 1, 0, 1, EXEC_OK) &&
             run(engine, "size", 0, 0, 2, EXEC_OK) &&
             run(engine, "grow", 1, 0, -1, EXEC_OK) &&
             run(engine, "load-data", 131071, 0, 0, EXEC_ERROR_TRAP) &&
             run(engine, "div_s", INT32_MIN, -1, 0, EXEC_ERROR_TRAP) &&
             run(engine, "div_u", 1, 0, 0, EXEC_ERROR_TRAP);
    exec_free(engine);
    return isolated && public_api && memory_access && ok && test_imports(argv[2]) &&
           test_extern_aliases(argv[3],argv[4]) ? 0 : 1;
}
